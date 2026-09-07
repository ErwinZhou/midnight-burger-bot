#!/usr/bin/env python3
"""Export Blender mesh data to the Game2 PNCT chunk format."""

from __future__ import annotations

import math
import os
from pathlib import Path
import struct
import sys
import tempfile

import bpy


VERTEX_SIZE = 36
SUPPORTED_COLOR_DOMAINS = {"CORNER", "POINT"}


def command_arguments() -> tuple[str, str]:
	"""Return the source selector and destination after Blender's `--`."""
	if "--" not in sys.argv:
		raise SystemExit(
			"usage: blender --background --python export-meshes.py -- "
			"<source.blend[:collection]> <destination.pnct>"
		)
	arguments = sys.argv[sys.argv.index("--") + 1:]
	if len(arguments) != 2:
		raise SystemExit(
			"expected exactly two exporter arguments: "
			"<source.blend[:collection]> <destination.pnct>"
		)
	return arguments[0], arguments[1]


def split_source_selector(selector: str) -> tuple[Path, str | None]:
	"""Split `file.blend:Collection` without breaking a Windows drive prefix."""
	direct_path = Path(selector).expanduser()
	if direct_path.is_file():
		return direct_path.resolve(), None
	if ":" not in selector:
		raise SystemExit("Blender source does not exist: " + selector)
	file_text, collection_name = selector.rsplit(":", 1)
	file_path = Path(file_text).expanduser()
	if not file_path.is_file():
		raise SystemExit("Blender source does not exist: " + file_text)
	if not collection_name:
		raise SystemExit("Collection name after ':' must not be empty")
	return file_path.resolve(), collection_name


def load_blend_file(source: Path) -> None:
	"""Open the source unless Blender already has this exact file loaded."""
	current = Path(bpy.data.filepath).resolve() if bpy.data.filepath else None
	if current == source:
		print("Using Blender file already open in this process: " + str(source))
		return
	bpy.ops.wm.open_mainfile(filepath=str(source))


def selected_collection(name: str | None):
	if name is None:
		return bpy.context.scene.collection
	collection = bpy.data.collections.get(name)
	if collection is None:
		raise RuntimeError("Collection not found: " + name)
	return collection


def visible_mesh_objects(root_collection) -> list:
	"""Collect project meshes recursively while honoring underscore helpers."""
	objects = []
	visited_collections = set()

	def visit(collection) -> None:
		if collection in visited_collections:
			return
		visited_collections.add(collection)
		if collection.name.startswith("_"):
			return
		for obj in sorted(collection.objects, key=lambda item: item.name):
			if obj.name.startswith("_"):
				continue
			if obj.instance_collection is not None:
				raise RuntimeError(
					"Collection instances are not supported by this project exporter: "
					+ obj.name
				)
			if obj.type != "MESH":
				continue
			if obj.data.name.startswith("_"):
				continue
			objects.append(obj)
		for child in sorted(collection.children, key=lambda item: item.name):
			visit(child)

	visit(root_collection)
	return objects


def representatives_by_mesh(objects: list) -> list[tuple[object, object]]:
	"""Choose one deterministic object for every shared mesh datablock."""
	users = {}
	for obj in objects:
		users.setdefault(obj.data, []).append(obj)
	if not users:
		raise RuntimeError("Selected collection contains no exportable meshes")

	representatives = []
	for source_mesh, candidates in users.items():
		# Prefer the object carrying the modifier stack. This lets linked scene
		# instances share the evaluated geometry exported for their source object.
		candidates.sort(key=lambda obj: (-len(obj.modifiers), obj.name))
		representative = candidates[0]
		modifier_users = [obj.name for obj in candidates if len(obj.modifiers) > 0]
		if len(modifier_users) > 1:
			print(
				"WARNING: shared mesh '%s' has modifiers on multiple objects; "
				"using '%s'. Users: %s"
				% (source_mesh.name, representative.name, ", ".join(modifier_users))
			)
		representatives.append((source_mesh, representative))

	representatives.sort(key=lambda pair: pair[0].name)
	return representatives


def color_byte(value: float) -> int:
	if not math.isfinite(value):
		raise RuntimeError("Vertex color contains a non-finite value")
	return max(0, min(255, int(value * 255.0)))


def checked_vector(values, label: str) -> tuple[float, ...]:
	result = tuple(float(value) for value in values)
	if not all(math.isfinite(value) for value in result):
		raise RuntimeError(label + " contains a non-finite value")
	return result


def serialize_mesh(obj, depsgraph) -> tuple[bytes, int]:
	"""Return evaluated triangle vertices and their vertex count."""
	evaluated_object = obj.evaluated_get(depsgraph)
	mesh = evaluated_object.to_mesh(
		preserve_all_data_layers=True,
		depsgraph=depsgraph,
	)
	try:
		mesh.calc_loop_triangles()
		if not mesh.loop_triangles:
			raise RuntimeError("Mesh has no triangles: " + obj.data.name)

		if len(mesh.color_attributes) != 1:
			raise RuntimeError(
				"Mesh '%s' must have exactly one color attribute; found %d"
				% (obj.data.name, len(mesh.color_attributes))
			)
		colors = mesh.color_attributes.active_color
		if colors is None:
			raise RuntimeError("Mesh has no active color attribute: " + obj.data.name)
		if colors.name != "Color":
			raise RuntimeError(
				"Mesh '%s' active color attribute is '%s', expected 'Color'"
				% (obj.data.name, colors.name)
			)
		if colors.domain not in SUPPORTED_COLOR_DOMAINS:
			raise RuntimeError(
				"Mesh '%s' uses unsupported color domain '%s'"
				% (obj.data.name, colors.domain)
			)

		uv_data = None
		if mesh.uv_layers.active is not None:
			uv_data = mesh.uv_layers.active.data

		payload = bytearray()
		for triangle in mesh.loop_triangles:
			for loop_index in triangle.loops:
				loop = mesh.loops[loop_index]
				vertex = mesh.vertices[loop.vertex_index]
				position = checked_vector(vertex.co, obj.data.name + " position")
				if hasattr(mesh, "corner_normals"):
					normal_source = mesh.corner_normals[loop_index].vector
				else:
					normal_source = loop.normal
				normal = checked_vector(normal_source, obj.data.name + " normal")

				if colors.domain == "CORNER":
					color = colors.data[loop_index].color
				else:
					color = colors.data[loop.vertex_index].color
				# The runtime material is opaque. Blender may synthesize zero-alpha
				# corners for modifier-created faces, so validate every source channel
				# but deliberately write a stable opaque alpha.
				converted_color = tuple(color_byte(channel) for channel in color)
				rgba = converted_color[:3] + (255,)

				if uv_data is None:
					uv = (0.0, 0.0)
				else:
					uv = checked_vector(uv_data[loop_index].uv, obj.data.name + " UV")

				payload.extend(struct.pack("<3f3f4B2f", *position, *normal, *rgba, *uv))

		vertex_count = len(mesh.loop_triangles) * 3
		if len(payload) != vertex_count * VERTEX_SIZE:
			raise RuntimeError("Internal PNCT vertex-size mismatch")
		return bytes(payload), vertex_count
	finally:
		evaluated_object.to_mesh_clear()


def chunk(magic: bytes, payload: bytes) -> bytes:
	if len(magic) != 4:
		raise ValueError("Chunk magic must be exactly four bytes")
	return struct.pack("<4sI", magic, len(payload)) + payload


def write_atomic(destination: Path, payload: bytes) -> None:
	destination.parent.mkdir(parents=True, exist_ok=True)
	temporary_name = None
	try:
		with tempfile.NamedTemporaryFile(
			mode="wb",
			dir=destination.parent,
			prefix=destination.name + ".",
			suffix=".tmp",
			delete=False,
		) as temporary:
			temporary.write(payload)
			temporary.flush()
			os.fsync(temporary.fileno())
			temporary_name = temporary.name
		os.replace(temporary_name, destination)
	except BaseException:
		if temporary_name and os.path.exists(temporary_name):
			os.unlink(temporary_name)
		raise


def export_pnct(source: Path, collection_name: str | None, destination: Path) -> None:
	load_blend_file(source)
	root_collection = selected_collection(collection_name)
	mesh_objects = visible_mesh_objects(root_collection)
	representatives = representatives_by_mesh(mesh_objects)
	depsgraph = bpy.context.evaluated_depsgraph_get()

	vertex_blob = bytearray()
	string_blob = bytearray()
	index_blob = bytearray()
	vertex_cursor = 0

	for source_mesh, representative in representatives:
		name_bytes = source_mesh.name.encode("utf-8")
		name_begin = len(string_blob)
		string_blob.extend(name_bytes)
		name_end = len(string_blob)

		mesh_bytes, mesh_vertex_count = serialize_mesh(representative, depsgraph)
		vertex_begin = vertex_cursor
		vertex_blob.extend(mesh_bytes)
		vertex_cursor += mesh_vertex_count
		index_blob.extend(
			struct.pack("<4I", name_begin, name_end, vertex_begin, vertex_cursor)
		)
		print(
			"mesh %-24s object %-24s vertices %d"
			% (source_mesh.name, representative.name, mesh_vertex_count)
		)

	output = b"".join((
		chunk(b"pnct", bytes(vertex_blob)),
		chunk(b"str0", bytes(string_blob)),
		chunk(b"idx0", bytes(index_blob)),
	))
	write_atomic(destination, output)
	print(
		"PNCT export complete: %d meshes, %d vertices, %d bytes -> %s"
		% (len(representatives), vertex_cursor, len(output), destination)
	)


def main() -> None:
	selector, destination_text = command_arguments()
	source, collection_name = split_source_selector(selector)
	destination = Path(destination_text).expanduser().resolve()
	if destination.suffix.lower() != ".pnct":
		raise SystemExit("PNCT destination must end in .pnct: " + str(destination))
	export_pnct(source, collection_name, destination)


if __name__ == "__main__":
	main()
