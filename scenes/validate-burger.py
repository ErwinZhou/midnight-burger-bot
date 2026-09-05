"""Validate the generated Burger Bot pnct and scene files."""

import json
import math
import os
import struct


HERE = os.path.dirname(os.path.abspath(__file__))
DIST = os.path.abspath(os.path.join(HERE, "..", "dist"))
PNCT_PATH = os.path.join(DIST, "burger.pnct")
SCENE_PATH = os.path.join(DIST, "burger.scene")

EXPECTED_MESHES = {
	"ArmBase", "ArmYaw", "ArmShoulder", "ArmUpper", "ArmForearm", "ArmWrist",
	"GripperPalm", "GripperFinger",
	"BunBottom", "Patty", "Lettuce", "CheeseSlice", "BunTop", "TomatoSlice",
	"OnionRing", "PickleSlice", "BaconStrip", "SauceBlob",
	"Counter", "Plate", "RecipeBoard", "IngredientBin", "Floor", "WallBack",
	"BotTorso", "BotHead", "Tray", "TrashBin", "ServeBell", "ConveyorBelt",
}

INGREDIENTS = {
	"BunBottom", "Patty", "Lettuce", "CheeseSlice", "BunTop", "TomatoSlice",
	"OnionRing", "PickleSlice", "BaconStrip", "SauceBlob",
}

EXPECTED_PARENTS = {
	"Plate": "Counter",
	"RecipeBoard": "Counter",
	"IngredientBin.1": "Counter",
	"IngredientBin.2": "Counter",
	"IngredientBin.3": "Counter",
	"IngredientBin.4": "Counter",
	"IngredientBin.5": "Counter",
	"IngredientBin.6": "Counter",
	"BotTorso": "Counter",
	"BotHead": "BotTorso",
	"Tray": "Counter",
	"ServeBell": "Counter",
	"ArmBase": "Counter",
	"ArmYaw": "ArmBase",
	"ArmShoulder": "ArmYaw",
	"ArmUpper": "ArmShoulder",
	"ArmForearm": "ArmUpper",
	"ArmWrist": "ArmForearm",
	"GripperPalm": "ArmWrist",
	"GripperFinger.L": "GripperPalm",
	"GripperFinger.R": "GripperPalm",
}


def read_chunks(path):
	data = open(path, "rb").read()
	chunks = {}
	offset = 0
	while offset < len(data):
		assert offset + 8 <= len(data), "%s has a truncated chunk header" % path
		magic, length = struct.unpack_from("<4sI", data, offset)
		offset += 8
		assert offset + length <= len(data), "%s has a truncated %r chunk" % (path, magic)
		name = magic.decode("ascii")
		assert name not in chunks, "%s repeats chunk %s" % (path, name)
		chunks[name] = data[offset:offset + length]
		offset += length
	return chunks


def get_string(strings, begin, end):
	assert 0 <= begin <= end <= len(strings)
	return strings[begin:end].decode("utf8")


pnct = read_chunks(PNCT_PATH)
assert set(pnct) == {"pnct", "str0", "idx0"}
assert len(pnct["pnct"]) % 36 == 0
assert len(pnct["idx0"]) % 16 == 0
vertex_count = len(pnct["pnct"]) // 36
triangle_count = vertex_count // 3
assert vertex_count % 3 == 0
assert triangle_count <= 8000, "triangle budget exceeded: %d" % triangle_count

mesh_ranges = {}
for offset in range(0, len(pnct["idx0"]), 16):
	name_begin, name_end, vertex_begin, vertex_end = struct.unpack_from("<IIII", pnct["idx0"], offset)
	name = get_string(pnct["str0"], name_begin, name_end)
	assert name not in mesh_ranges
	assert 0 <= vertex_begin < vertex_end <= vertex_count
	assert (vertex_end - vertex_begin) % 3 == 0
	mesh_ranges[name] = (vertex_begin, vertex_end)

assert set(mesh_ranges) == EXPECTED_MESHES, {
	"missing": sorted(EXPECTED_MESHES - set(mesh_ranges)),
	"unexpected": sorted(set(mesh_ranges) - EXPECTED_MESHES),
}

for name, (begin, end) in mesh_ranges.items():
	colors = set()
	min_z = math.inf
	for vertex_index in range(begin, end):
		offset = vertex_index * 36
		position_normal = struct.unpack_from("<6f", pnct["pnct"], offset)
		assert all(math.isfinite(value) for value in position_normal)
		min_z = min(min_z, position_normal[2])
		colors.add(tuple(pnct["pnct"][offset + 24:offset + 28]))
	assert colors != {(255, 255, 255, 255)}, "%s exported white" % name
	if name in INGREDIENTS:
		assert abs(min_z) <= 0.0001, "%s underside is at %.6f, expected 0" % (name, min_z)

scene = read_chunks(SCENE_PATH)
assert set(scene) == {"str0", "xfh0", "msh0", "cam0", "lmp0"}
assert len(scene["xfh0"]) % 52 == 0
assert len(scene["msh0"]) % 12 == 0
assert len(scene["cam0"]) % 20 == 0
assert len(scene["lmp0"]) == 0

transforms = []
for index, offset in enumerate(range(0, len(scene["xfh0"]), 52)):
	parent, name_begin, name_end = struct.unpack_from("<iII", scene["xfh0"], offset)
	values = struct.unpack_from("<10f", scene["xfh0"], offset + 12)
	assert parent == -1 or 0 <= parent < index
	assert all(math.isfinite(value) for value in values)
	transforms.append({
		"name": get_string(scene["str0"], name_begin, name_end),
		"parent": parent,
		"position": values[0:3],
		"rotation": values[3:7],
		"scale": values[7:10],
	})

names = [entry["name"] for entry in transforms]
assert len(names) == len(set(names)) == 37
parent_names = {
	entry["name"]: None if entry["parent"] == -1 else transforms[entry["parent"]]["name"]
	for entry in transforms
}
for child, parent in EXPECTED_PARENTS.items():
	assert parent_names[child] == parent, "%s parent is %r, expected %r" % (child, parent_names[child], parent)

scene_meshes = []
for offset in range(0, len(scene["msh0"]), 12):
	transform_index, name_begin, name_end = struct.unpack_from("<iII", scene["msh0"], offset)
	assert 0 <= transform_index < len(transforms)
	mesh_name = get_string(scene["str0"], name_begin, name_end)
	assert mesh_name in mesh_ranges
	scene_meshes.append((transforms[transform_index]["name"], mesh_name))
assert len(scene_meshes) == 36
assert scene_meshes.count(("GripperFinger.L", "GripperFinger")) == 1
assert scene_meshes.count(("GripperFinger.R", "GripperFinger")) == 1
assert sum(mesh_name == "IngredientBin" for _, mesh_name in scene_meshes) == 6

cameras = []
for offset in range(0, len(scene["cam0"]), 20):
	transform_index = struct.unpack_from("<i", scene["cam0"], offset)[0]
	assert 0 <= transform_index < len(transforms)
	cameras.append(transforms[transform_index]["name"])
assert cameras == ["Camera"]

right_finger = transforms[names.index("GripperFinger.R")]
assert right_finger["scale"][0] < 0.0

print(json.dumps({
	"status": "pass",
	"mesh_datablocks": len(mesh_ranges),
	"scene_mesh_objects": len(scene_meshes),
	"transforms": len(transforms),
	"cameras": len(cameras),
	"lights": 0,
	"vertices": vertex_count,
	"triangles": triangle_count,
	"pnct_bytes": os.path.getsize(PNCT_PATH),
	"scene_bytes": os.path.getsize(SCENE_PATH),
}, indent=2, sort_keys=True))
