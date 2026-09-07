#!/usr/bin/env python3
"""Export Blender object hierarchy to the Game2 Scene chunk format."""

from __future__ import annotations

import math
import os
from pathlib import Path
import struct
import sys
import tempfile

import bpy


SUPPORTED_OBJECT_TYPES = {"MESH", "CAMERA", "EMPTY", "LIGHT"}


def command_arguments() -> tuple[str, str]:
	if "--" not in sys.argv:
		raise SystemExit(
			"usage: blender --background --python export-scene.py -- "
			"<source.blend[:collection]> <destination.scene>"
		)
	arguments = sys.argv[sys.argv.index("--") + 1:]
	if len(arguments) != 2:
		raise SystemExit(
			"expected exactly two exporter arguments: "
			"<source.blend[:collection]> <destination.scene>"
		)
	return arguments[0], arguments[1]


def split_source_selector(selector: str) -> tuple[Path, str | None]:
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


def project_objects(root_collection) -> list:
	"""Collect objects recursively, excluding underscore-prefixed helpers."""
	objects = set()
	visited_collections = set()

	def visit(collection) -> None:
		if collection in visited_collections:
			return
		visited_collections.add(collection)
		if collection.name.startswith("_"):
			return
		for obj in collection.objects:
			if obj.name.startswith("_"):
				continue
			if obj.instance_collection is not None:
				raise RuntimeError(
					"Collection instances are not supported by this project exporter: "
					+ obj.name
				)
			if obj.type not in SUPPORTED_OBJECT_TYPES:
				raise RuntimeError(
					"Unsupported Blender object type '%s' on '%s'"
					% (obj.type, obj.name)
				)
			if obj.type == "MESH" and obj.data.name.startswith("_"):
				continue
			objects.add(obj)
		for child in collection.children:
			visit(child)

	visit(root_collection)
	if not objects:
		raise RuntimeError("Selected collection contains no exportable objects")
	return list(objects)


def topological_order(objects: list) -> list:
	"""Return parents before children with stable alphabetical sibling order."""
	object_set = set(objects)
	for obj in objects:
		if obj.parent is not None and obj.parent not in object_set:
			raise RuntimeError(
				"Object '%s' has parent '%s' outside the exported collection"
				% (obj.name, obj.parent.name)
			)

	ordered = []
	visiting = set()
	visited = set()

	def visit(obj) -> None:
		if obj in visited:
			return
		if obj in visiting:
			raise RuntimeError("Parent cycle detected at object: " + obj.name)
		visiting.add(obj)
		if obj.parent is not None:
			visit(obj.parent)
		visiting.remove(obj)
		visited.add(obj)
		ordered.append(obj)

	for obj in sorted(objects, key=lambda item: item.name):
		visit(obj)
	return ordered


class StringTable:
	def __init__(self) -> None:
		self.payload = bytearray()

	def append(self, value: str) -> tuple[int, int]:
		encoded = value.encode("utf-8")
		begin = len(self.payload)
		self.payload.extend(encoded)
		return begin, len(self.payload)


def finite_values(values, label: str) -> tuple[float, ...]:
	result = tuple(float(value) for value in values)
	if not all(math.isfinite(value) for value in result):
		raise RuntimeError(label + " contains a non-finite value")
	return result


def relative_transform(obj):
	if obj.parent is None:
		matrix = obj.matrix_world.copy()
	else:
		matrix = obj.parent.matrix_world.inverted_safe() @ obj.matrix_world
	position, rotation, scale = matrix.decompose()
	rotation.normalize()
	return (
		finite_values(position, obj.name + " position"),
		finite_values((rotation.x, rotation.y, rotation.z, rotation.w), obj.name + " rotation"),
		finite_values(scale, obj.name + " scale"),
	)


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


def export_scene(source: Path, collection_name: str | None, destination: Path) -> None:
	load_blend_file(source)
	root_collection = selected_collection(collection_name)
	objects = topological_order(project_objects(root_collection))

	names = [obj.name for obj in objects]
	if len(names) != len(set(names)):
		raise RuntimeError("Exported object names must be unique")
	index_by_object = {obj: index for index, obj in enumerate(objects)}

	light_names = [obj.name for obj in objects if obj.type == "LIGHT"]
	if light_names:
		raise RuntimeError(
			"This project uses hardcoded runtime lighting; remove Blender lights: "
			+ ", ".join(light_names)
		)
	cameras = [obj for obj in objects if obj.type == "CAMERA"]
	if len(cameras) != 1:
		raise RuntimeError(
			"Burger Bot requires exactly one camera; found %d" % len(cameras)
		)

	strings = StringTable()
	hierarchy_blob = bytearray()
	mesh_blob = bytearray()
	camera_blob = bytearray()

	for obj in objects:
		parent_index = 0xFFFFFFFF if obj.parent is None else index_by_object[obj.parent]
		name_begin, name_end = strings.append(obj.name)
		position, rotation, scale = relative_transform(obj)
		hierarchy_blob.extend(struct.pack(
			"<3I3f4f3f",
			parent_index,
			name_begin,
			name_end,
			*position,
			*rotation,
			*scale,
		))

		if obj.type == "MESH":
			mesh_begin, mesh_end = strings.append(obj.data.name)
			mesh_blob.extend(struct.pack(
				"<3I", index_by_object[obj], mesh_begin, mesh_end
			))

	for camera in cameras:
		data = camera.data
		if data.type != "PERSP":
			raise RuntimeError("Only perspective cameras are supported: " + camera.name)
		if data.sensor_fit != "VERTICAL":
			raise RuntimeError(
				"Camera sensor_fit must be VERTICAL for stable exported FOV: "
				+ camera.name
			)
		if data.lens <= 0.0 or data.sensor_height <= 0.0:
			raise RuntimeError("Camera has invalid lens or sensor height: " + camera.name)
		fov_degrees = math.degrees(
			2.0 * math.atan(data.sensor_height / (2.0 * data.lens))
		)
		camera_blob.extend(struct.pack(
			"<I4s3f",
			index_by_object[camera],
			b"pers",
			fov_degrees,
			float(data.clip_start),
			float(data.clip_end),
		))

	output = b"".join((
		chunk(b"str0", bytes(strings.payload)),
		chunk(b"xfh0", bytes(hierarchy_blob)),
		chunk(b"msh0", bytes(mesh_blob)),
		chunk(b"cam0", bytes(camera_blob)),
		chunk(b"lmp0", b""),
	))
	write_atomic(destination, output)
	print(
		"Scene export complete: %d transforms, %d meshes, %d cameras, %d bytes -> %s"
		% (
			len(objects),
			len(mesh_blob) // 12,
			len(cameras),
			len(output),
			destination,
		)
	)


def main() -> None:
	selector, destination_text = command_arguments()
	source, collection_name = split_source_selector(selector)
	destination = Path(destination_text).expanduser().resolve()
	if destination.suffix.lower() != ".scene":
		raise SystemExit("Scene destination must end in .scene: " + str(destination))
	export_scene(source, collection_name, destination)


if __name__ == "__main__":
	main()
