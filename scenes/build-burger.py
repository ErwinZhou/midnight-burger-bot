"""Build the original Midnight Burger Bot scene in Blender 5.2.

Run inside Blender:
    blender --background --python build-burger.py

The script deliberately uses only generated geometry and vertex colors.
"""

import math
import os

import bpy
from mathutils import Vector


HERE = os.path.dirname(os.path.abspath(__file__))
BLEND_PATH = os.path.join(HERE, "burger.blend")
PREVIEW_PATH = os.path.join(HERE, "burger-preview.png")
INGREDIENT_PREVIEW_PATH = os.path.join(HERE, "burger-ingredients-preview.png")


def rgb(value):
	value = value.lstrip("#")
	return tuple(int(value[index:index + 2], 16) / 255.0 for index in (0, 2, 4)) + (1.0,)


COLORS = {
	"cream": rgb("#F7E6C4"),
	"counter": rgb("#2D6A73"),
	"counter_edge": rgb("#19464D"),
	"steel": rgb("#A9B7BE"),
	"steel_dark": rgb("#52666F"),
	"red": rgb("#C83E4D"),
	"red_dark": rgb("#772733"),
	"yellow": rgb("#F2B233"),
	"orange": rgb("#D97A2B"),
	"bun": rgb("#C98A42"),
	"bun_light": rgb("#F1D18A"),
	"patty": rgb("#4A2C1A"),
	"patty_light": rgb("#6B4024"),
	"lettuce": rgb("#6DBE45"),
	"lettuce_dark": rgb("#3E812F"),
	"tomato": rgb("#D93B2B"),
	"tomato_light": rgb("#F16A55"),
	"onion": rgb("#F0E4EE"),
	"pickle": rgb("#7A9A3B"),
	"bacon": rgb("#A54827"),
	"bacon_light": rgb("#E6AD68"),
	"sauce": rgb("#B01B1B"),
	"floor": rgb("#26323C"),
	"wall": rgb("#18232C"),
	"screen": rgb("#79F2C0"),
	"black": rgb("#101419"),
	"white": rgb("#FFFFFF"),
}


def append_box(vertices, faces, face_colors, center, size, color):
	cx, cy, cz = center
	sx, sy, sz = (axis * 0.5 for axis in size)
	base = len(vertices)
	vertices.extend([
		(cx - sx, cy - sy, cz - sz), (cx + sx, cy - sy, cz - sz),
		(cx + sx, cy + sy, cz - sz), (cx - sx, cy + sy, cz - sz),
		(cx - sx, cy - sy, cz + sz), (cx + sx, cy - sy, cz + sz),
		(cx + sx, cy + sy, cz + sz), (cx - sx, cy + sy, cz + sz),
	])
	faces.extend([
		(base + 0, base + 3, base + 2, base + 1),
		(base + 4, base + 5, base + 6, base + 7),
		(base + 0, base + 1, base + 5, base + 4),
		(base + 1, base + 2, base + 6, base + 5),
		(base + 2, base + 3, base + 7, base + 6),
		(base + 3, base + 0, base + 4, base + 7),
	])
	face_colors.extend([color] * 6)


def append_cylinder(vertices, faces, face_colors, center, radius, depth, color, segments=24, axis="Z"):
	cx, cy, cz = center
	base = len(vertices)
	for level in (-0.5, 0.5):
		for index in range(segments):
			angle = 2.0 * math.pi * index / segments
			a = radius * math.cos(angle)
			b = radius * math.sin(angle)
			if axis == "Z":
				vertices.append((cx + a, cy + b, cz + level * depth))
			elif axis == "Y":
				vertices.append((cx + a, cy + level * depth, cz + b))
			else:
				vertices.append((cx + level * depth, cy + a, cz + b))
	bottom_center = len(vertices)
	vertices.append(center)
	top_center = len(vertices)
	if axis == "Z":
		vertices.append((cx, cy, cz + depth * 0.5))
		vertices[bottom_center] = (cx, cy, cz - depth * 0.5)
	elif axis == "Y":
		vertices.append((cx, cy + depth * 0.5, cz))
		vertices[bottom_center] = (cx, cy - depth * 0.5, cz)
	else:
		vertices.append((cx + depth * 0.5, cy, cz))
		vertices[bottom_center] = (cx - depth * 0.5, cy, cz)
	for index in range(segments):
		next_index = (index + 1) % segments
		faces.append((base + index, base + next_index, base + segments + next_index, base + segments + index))
		face_colors.append(color)
		faces.append((bottom_center, base + next_index, base + index))
		face_colors.append(color)
		faces.append((top_center, base + segments + index, base + segments + next_index))
		face_colors.append(color)


def append_profile(vertices, faces, face_colors, rings, color, segments=32, wave=0.0, frequency=6, accent=None):
	base = len(vertices)
	for ring_index, (height, radius) in enumerate(rings):
		for index in range(segments):
			angle = 2.0 * math.pi * index / segments
			local_radius = radius + wave * math.sin(frequency * angle + ring_index * 0.7)
			vertices.append((local_radius * math.cos(angle), local_radius * math.sin(angle), height))
	for ring_index in range(len(rings) - 1):
		for index in range(segments):
			next_index = (index + 1) % segments
			faces.append((
				base + ring_index * segments + index,
				base + ring_index * segments + next_index,
				base + (ring_index + 1) * segments + next_index,
				base + (ring_index + 1) * segments + index,
			))
			face_colors.append(accent if accent and (index + ring_index) % 7 == 0 else color)
	bottom_center = len(vertices)
	vertices.append((0.0, 0.0, rings[0][0]))
	top_center = len(vertices)
	vertices.append((0.0, 0.0, rings[-1][0]))
	for index in range(segments):
		next_index = (index + 1) % segments
		faces.append((bottom_center, base + next_index, base + index))
		face_colors.append(color)
		faces.append((top_center, base + (len(rings) - 1) * segments + index, base + (len(rings) - 1) * segments + next_index))
		face_colors.append(accent if accent and index % 4 == 0 else color)


def append_torus(vertices, faces, face_colors, major_radius, minor_radius, height, color, major_segments=28, minor_segments=8):
	base = len(vertices)
	for major in range(major_segments):
		a = 2.0 * math.pi * major / major_segments
		for minor in range(minor_segments):
			b = 2.0 * math.pi * minor / minor_segments
			radius = major_radius + minor_radius * math.cos(b)
			vertices.append((radius * math.cos(a), radius * math.sin(a), height + minor_radius * math.sin(b)))
	for major in range(major_segments):
		for minor in range(minor_segments):
			next_major = (major + 1) % major_segments
			next_minor = (minor + 1) % minor_segments
			faces.append((
				base + major * minor_segments + minor,
				base + next_major * minor_segments + minor,
				base + next_major * minor_segments + next_minor,
				base + major * minor_segments + next_minor,
			))
			face_colors.append(color)


def make_object(name, vertices, faces, face_colors, location=(0.0, 0.0, 0.0), parent=None, rotation=(0.0, 0.0, 0.0), scale=(1.0, 1.0, 1.0), bevel=0.025, mesh_name=None):
	mesh = bpy.data.meshes.new(mesh_name or name)
	mesh.from_pydata(vertices, [], faces)
	mesh.update()
	color_layer = mesh.color_attributes.new(name="Color", type="BYTE_COLOR", domain="CORNER")
	for polygon, color in zip(mesh.polygons, face_colors):
		for loop_index in polygon.loop_indices:
			color_layer.data[loop_index].color = color
	try:
		mesh.color_attributes.active_color = color_layer
	except AttributeError:
		mesh.color_attributes.active_color_index = 0
	obj = bpy.data.objects.new(name, mesh)
	MAIN.objects.link(obj)
	obj.location = location
	obj.rotation_euler = rotation
	obj.scale = scale
	obj.parent = parent
	obj.data.materials.append(VERTEX_MATERIAL)
	if bevel > 0.0:
		modifier = obj.modifiers.new(name="EdgeBevel", type="BEVEL")
		modifier.width = bevel
		modifier.segments = 1
		modifier.limit_method = "ANGLE"
	return obj


def box_object(name, size, color, location=(0.0, 0.0, 0.0), parent=None, center=None, bevel=0.025, rotation=(0.0, 0.0, 0.0), mesh_name=None):
	vertices, faces, face_colors = [], [], []
	if center is None:
		center = (0.0, 0.0, size[2] * 0.5)
	append_box(vertices, faces, face_colors, center, size, color)
	return make_object(name, vertices, faces, face_colors, location, parent, rotation, bevel=bevel, mesh_name=mesh_name)


def cylinder_object(name, radius, depth, color, location=(0.0, 0.0, 0.0), parent=None, axis="Z", bevel=0.025, rotation=(0.0, 0.0, 0.0), mesh_name=None, segments=24):
	vertices, faces, face_colors = [], [], []
	center = (0.0, 0.0, depth * 0.5) if axis == "Z" else (0.0, 0.0, 0.0)
	append_cylinder(vertices, faces, face_colors, center, radius, depth, color, segments, axis)
	return make_object(name, vertices, faces, face_colors, location, parent, rotation, bevel=bevel, mesh_name=mesh_name)


def profile_object(name, rings, color, location=(0.0, 0.0, 0.0), parent=None, wave=0.0, frequency=6, accent=None, bevel=0.0, segments=32):
	vertices, faces, face_colors = [], [], []
	append_profile(vertices, faces, face_colors, rings, color, segments, wave, frequency, accent)
	return make_object(name, vertices, faces, face_colors, location, parent, bevel=bevel)


def look_at(obj, target):
	direction = Vector(target) - obj.location
	obj.rotation_euler = direction.to_track_quat("-Z", "Y").to_euler()


# Start from a genuinely original, empty file.
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete(use_global=False)
for collection in list(bpy.data.collections):
	bpy.data.collections.remove(collection)
for mesh in list(bpy.data.meshes):
	if mesh.users == 0:
		bpy.data.meshes.remove(mesh)
for camera_data in list(bpy.data.cameras):
	if camera_data.users == 0:
		bpy.data.cameras.remove(camera_data)
for light_data in list(bpy.data.lights):
	if light_data.users == 0:
		bpy.data.lights.remove(light_data)
for material in list(bpy.data.materials):
	if material.users == 0:
		bpy.data.materials.remove(material)
MAIN = bpy.data.collections.new("Main")
bpy.context.scene.collection.children.link(MAIN)

# One material makes Blender previews show the same Color attribute exported to the game.
VERTEX_MATERIAL = bpy.data.materials.new("VertexColorPreview")
VERTEX_MATERIAL.use_nodes = True
nodes = VERTEX_MATERIAL.node_tree.nodes
links = VERTEX_MATERIAL.node_tree.links
nodes.clear()
output = nodes.new("ShaderNodeOutputMaterial")
shader = nodes.new("ShaderNodeBsdfPrincipled")
vertex_color = nodes.new("ShaderNodeVertexColor")
vertex_color.layer_name = "Color"
links.new(vertex_color.outputs["Color"], shader.inputs["Base Color"])
shader.inputs["Roughness"].default_value = 0.72
if "Emission Color" in shader.inputs:
	links.new(vertex_color.outputs["Color"], shader.inputs["Emission Color"])
	shader.inputs["Emission Strength"].default_value = 0.12
links.new(shader.outputs["BSDF"], output.inputs["Surface"])

# --- Set: 12 unique mesh datablocks, IngredientBin instanced six times. ---
floor = box_object("Floor", (20.0, 20.0, 0.2), COLORS["floor"], location=(0.0, 0.0, -0.2), bevel=0.0)
wall = box_object("WallBack", (20.0, 0.2, 6.0), COLORS["wall"], location=(0.0, 3.15, 0.0), bevel=0.02)
counter = box_object("Counter", (12.0, 5.0, 1.0), COLORS["counter"], bevel=0.04)

plate_vertices, plate_faces, plate_colors = [], [], []
append_profile(plate_vertices, plate_faces, plate_colors, [(0.0, 0.68), (0.03, 0.78), (0.10, 0.80), (0.12, 0.62)], COLORS["cream"], segments=32)
plate = make_object("Plate", plate_vertices, plate_faces, plate_colors, location=(1.55, -0.65, 1.0), parent=counter, bevel=0.015)

board_vertices, board_faces, board_colors = [], [], []
append_box(board_vertices, board_faces, board_colors, (0.0, 0.0, 1.25), (3.0, 0.16, 2.0), COLORS["black"])
append_box(board_vertices, board_faces, board_colors, (0.0, 0.0, 0.30), (0.18, 0.18, 0.60), COLORS["steel_dark"])
append_box(board_vertices, board_faces, board_colors, (0.0, -0.09, 1.25), (2.55, 0.04, 1.55), COLORS["screen"])
recipe_board = make_object("RecipeBoard", board_vertices, board_faces, board_colors, location=(4.1, 2.35, 1.70), parent=counter, bevel=0.035)

bin_vertices, bin_faces, bin_colors = [], [], []
append_box(bin_vertices, bin_faces, bin_colors, (0.0, 0.0, 0.05), (1.30, 1.10, 0.10), COLORS["steel_dark"])
append_box(bin_vertices, bin_faces, bin_colors, (-0.60, 0.0, 0.33), (0.10, 1.10, 0.56), COLORS["steel"])
append_box(bin_vertices, bin_faces, bin_colors, (0.60, 0.0, 0.33), (0.10, 1.10, 0.56), COLORS["steel"])
append_box(bin_vertices, bin_faces, bin_colors, (0.0, 0.50, 0.33), (1.10, 0.10, 0.56), COLORS["steel"])
append_box(bin_vertices, bin_faces, bin_colors, (0.0, -0.50, 0.20), (1.10, 0.10, 0.30), COLORS["steel"])
bin_mesh_owner = make_object("IngredientBin.1", bin_vertices, bin_faces, bin_colors, location=(-3.9, 1.25, 1.0), parent=counter, bevel=0.025, mesh_name="IngredientBin")
ingredient_bins = [bin_mesh_owner]
for index, x in enumerate((-2.55, -1.2, 0.15, 1.5, 2.85), start=2):
	obj = bpy.data.objects.new("IngredientBin.%d" % index, bin_mesh_owner.data)
	MAIN.objects.link(obj)
	obj.location = (x, 1.25, 1.0)
	obj.parent = counter
	ingredient_bins.append(obj)

bot_torso = box_object("BotTorso", (1.60, 0.80, 1.50), COLORS["red"], location=(-5.0, 0.25, 1.0), parent=counter, bevel=0.10)
head_vertices, head_faces, head_colors = [], [], []
append_box(head_vertices, head_faces, head_colors, (0.0, 0.0, 0.60), (1.20, 1.0, 1.20), COLORS["cream"])
append_box(head_vertices, head_faces, head_colors, (0.0, -0.515, 0.62), (0.90, 0.04, 0.62), COLORS["black"])
append_box(head_vertices, head_faces, head_colors, (-0.22, -0.54, 0.72), (0.13, 0.03, 0.13), COLORS["screen"])
append_box(head_vertices, head_faces, head_colors, (0.22, -0.54, 0.72), (0.13, 0.03, 0.13), COLORS["screen"])
append_box(head_vertices, head_faces, head_colors, (0.0, -0.54, 0.48), (0.35, 0.03, 0.08), COLORS["screen"])
bot_head = make_object("BotHead", head_vertices, head_faces, head_colors, location=(0.0, 0.0, 1.5), parent=bot_torso, bevel=0.07)

tray_vertices, tray_faces, tray_colors = [], [], []
append_box(tray_vertices, tray_faces, tray_colors, (0.0, 0.0, 0.04), (2.0, 1.4, 0.08), COLORS["steel"])
append_box(tray_vertices, tray_faces, tray_colors, (0.0, 0.66, 0.10), (2.0, 0.08, 0.20), COLORS["steel_dark"])
append_box(tray_vertices, tray_faces, tray_colors, (0.0, -0.66, 0.10), (2.0, 0.08, 0.20), COLORS["steel_dark"])
tray = make_object("Tray", tray_vertices, tray_faces, tray_colors, location=(4.8, -1.65, 1.0), parent=counter, bevel=0.025)

trash = profile_object("TrashBin", [(0.0, 0.43), (0.08, 0.50), (1.12, 0.50), (1.20, 0.44)], COLORS["steel_dark"], location=(6.6, 1.4, 0.0), bevel=0.02, segments=24)

bell_vertices, bell_faces, bell_colors = [], [], []
append_profile(bell_vertices, bell_faces, bell_colors, [(0.0, 0.25), (0.06, 0.25), (0.10, 0.18), (0.28, 0.08), (0.34, 0.0)], COLORS["yellow"], segments=24)
append_cylinder(bell_vertices, bell_faces, bell_colors, (0.0, 0.0, 0.38), 0.055, 0.10, COLORS["red"], segments=16)
serve_bell = make_object("ServeBell", bell_vertices, bell_faces, bell_colors, location=(3.15, -1.45, 1.0), parent=counter, bevel=0.01)

conveyor_vertices, conveyor_faces, conveyor_colors = [], [], []
append_box(conveyor_vertices, conveyor_faces, conveyor_colors, (0.0, 0.0, 0.20), (8.0, 1.2, 0.40), COLORS["steel_dark"])
append_box(conveyor_vertices, conveyor_faces, conveyor_colors, (0.0, 0.0, 0.43), (7.70, 1.0, 0.08), COLORS["black"])
for x in (-3.1, -2.1, -1.1, -0.1, 0.9, 1.9, 2.9):
	append_box(conveyor_vertices, conveyor_faces, conveyor_colors, (x, 0.0, 0.48), (0.06, 1.0, 0.03), COLORS["steel"])
conveyor = make_object("ConveyorBelt", conveyor_vertices, conveyor_faces, conveyor_colors, location=(0.0, 4.15, 0.0), bevel=0.025)

# --- Arm: 8 mesh datablocks, 9 objects, exact pivot-first local geometry. ---
arm_base = cylinder_object("ArmBase", 0.80, 0.30, COLORS["steel_dark"], location=(-1.5, -0.75, 1.0), parent=counter, bevel=0.035, segments=28)
arm_yaw = cylinder_object("ArmYaw", 0.60, 0.60, COLORS["red"], location=(0.0, 0.0, 0.30), parent=arm_base, bevel=0.04, segments=28)
arm_shoulder = cylinder_object("ArmShoulder", 0.30, 0.70, COLORS["yellow"], location=(0.0, 0.0, 0.57), parent=arm_yaw, axis="Y", bevel=0.03, segments=20)
arm_upper = cylinder_object("ArmUpper", 0.175, 2.50, COLORS["red"], parent=arm_shoulder, rotation=(0.0, math.radians(28.0), 0.0), bevel=0.025, segments=16)
arm_forearm = cylinder_object("ArmForearm", 0.14, 2.00, COLORS["cream"], location=(0.0, 0.0, 2.50), parent=arm_upper, rotation=(0.0, math.radians(-72.0), 0.0), bevel=0.02, segments=16)
arm_wrist = box_object("ArmWrist", (0.40, 0.40, 0.40), COLORS["steel_dark"], location=(0.0, 0.0, 2.0), parent=arm_forearm, center=(0.0, 0.0, 0.0), bevel=0.06, rotation=(0.0, math.radians(44.0), 0.0))
gripper_palm = box_object("GripperPalm", (0.60, 0.40, 0.15), COLORS["yellow"], location=(0.0, 0.0, 0.30), parent=arm_wrist, center=(0.0, 0.0, 0.0), bevel=0.035)

finger_vertices, finger_faces, finger_colors = [], [], []
append_box(finger_vertices, finger_faces, finger_colors, (0.36, 0.0, -0.12), (0.60, 0.12, 0.25), COLORS["steel"])
append_box(finger_vertices, finger_faces, finger_colors, (0.62, 0.0, -0.30), (0.12, 0.12, 0.35), COLORS["steel_dark"])
finger_left = make_object("GripperFinger.L", finger_vertices, finger_faces, finger_colors, location=(0.0, -0.13, 0.0), parent=gripper_palm, bevel=0.025, mesh_name="GripperFinger")
finger_right = bpy.data.objects.new("GripperFinger.R", finger_left.data)
MAIN.objects.link(finger_right)
finger_right.location = (0.0, 0.13, 0.0)
finger_right.scale.x = -1.0
finger_right.parent = gripper_palm

# --- Ingredients: six visible supplies plus four optional off-camera prototypes. ---
ingredient_x = -8.0
ingredient_y = -7.0
ingredient_step = 1.55
ingredients = []

ingredients.append(profile_object("BunBottom", [(0.0, 0.42), (0.06, 0.50), (0.24, 0.55), (0.35, 0.48)], COLORS["bun"], location=(ingredient_x, ingredient_y, 0.0), accent=COLORS["bun_light"], bevel=0.01))
ingredients.append(profile_object("Patty", [(0.0, 0.45), (0.04, 0.50), (0.16, 0.51), (0.20, 0.46)], COLORS["patty"], location=(ingredient_x + ingredient_step, ingredient_y, 0.0), wave=0.025, frequency=7, accent=COLORS["patty_light"]))
ingredients.append(profile_object("Lettuce", [(0.0, 0.53), (0.06, 0.58), (0.12, 0.53)], COLORS["lettuce"], location=(ingredient_x + 2 * ingredient_step, ingredient_y, 0.0), wave=0.07, frequency=8, accent=COLORS["lettuce_dark"]))

cheese_vertices, cheese_faces, cheese_colors = [], [], []
cheese_outline = [(-0.62, -0.62, 0.01), (0.0, -0.54, 0.05), (0.62, -0.62, 0.01), (0.54, 0.0, 0.05), (0.62, 0.62, 0.01), (0.0, 0.54, 0.05), (-0.62, 0.62, 0.01), (-0.54, 0.0, 0.05)]
cheese_vertices.extend([(x, y, 0.0) for x, y, _ in cheese_outline])
cheese_vertices.extend(cheese_outline)
for index in range(8):
	next_index = (index + 1) % 8
	cheese_faces.append((index, next_index, 8 + next_index, 8 + index))
	cheese_colors.append(COLORS["orange"])
cheese_faces.append(tuple(range(7, -1, -1)))
cheese_colors.append(COLORS["orange"])
cheese_faces.append(tuple(range(8, 16)))
cheese_colors.append(COLORS["yellow"])
ingredients.append(make_object("CheeseSlice", cheese_vertices, cheese_faces, cheese_colors, location=(ingredient_x + 3 * ingredient_step, ingredient_y, 0.0), bevel=0.015))

ingredients.append(profile_object("BunTop", [(0.0, 0.50), (0.08, 0.55), (0.24, 0.50), (0.40, 0.35), (0.50, 0.08)], COLORS["bun"], location=(ingredient_x + 4 * ingredient_step, ingredient_y, 0.0), accent=COLORS["bun_light"], segments=36))
ingredients.append(profile_object("TomatoSlice", [(0.0, 0.43), (0.02, 0.48), (0.08, 0.48), (0.10, 0.43)], COLORS["tomato"], location=(ingredient_x + 5 * ingredient_step, ingredient_y, 0.0), accent=COLORS["tomato_light"], segments=28))

onion_vertices, onion_faces, onion_colors = [], [], []
append_torus(onion_vertices, onion_faces, onion_colors, 0.41, 0.04, 0.04, COLORS["onion"], major_segments=28, minor_segments=8)
ingredients.append(make_object("OnionRing", onion_vertices, onion_faces, onion_colors, location=(ingredient_x + 6 * ingredient_step, ingredient_y, 0.0), bevel=0.0))

pickle_vertices, pickle_faces, pickle_colors = [], [], []
for cx, cy in ((-0.20, -0.10), (0.20, -0.08), (0.0, 0.20)):
	append_cylinder(pickle_vertices, pickle_faces, pickle_colors, (cx, cy, 0.03), 0.20, 0.06, COLORS["pickle"], segments=18)
ingredients.append(make_object("PickleSlice", pickle_vertices, pickle_faces, pickle_colors, location=(ingredient_x + 7 * ingredient_step, ingredient_y, 0.0), bevel=0.01))

bacon_vertices, bacon_faces, bacon_colors = [], [], []
segments = 10
for index in range(segments + 1):
	x = -0.45 + 0.90 * index / segments
	y_offset = 0.035 * math.sin(index * math.pi * 0.75)
	bacon_vertices.extend([(x, -0.125 + y_offset, 0.0), (x, 0.125 + y_offset, 0.0), (x, -0.125 + y_offset, 0.05), (x, 0.125 + y_offset, 0.05)])
for index in range(segments):
	base = 4 * index
	next_base = 4 * (index + 1)
	bacon_faces.extend([
		(base + 1, next_base + 1, next_base, base),
		(next_base + 2, next_base + 3, base + 3, base + 2),
		(next_base, next_base + 2, base + 2, base),
		(base + 3, next_base + 3, next_base + 1, base + 1),
	])
	stripe = COLORS["bacon_light"] if index % 3 == 1 else COLORS["bacon"]
	bacon_colors.extend([COLORS["bacon"], stripe, COLORS["bacon"], COLORS["bacon"]])
# close both ends so the bevel has a consistent outward surface
bacon_faces.extend([(0, 2, 3, 1), (4 * segments, 4 * segments + 1, 4 * segments + 3, 4 * segments + 2)])
bacon_colors.extend([COLORS["bacon"]] * 2)
ingredients.append(make_object("BaconStrip", bacon_vertices, bacon_faces, bacon_colors, location=(ingredient_x + 8 * ingredient_step, ingredient_y, 0.0), bevel=0.01))
ingredients.append(profile_object("SauceBlob", [(0.0, 0.34), (0.02, 0.40), (0.06, 0.36)], COLORS["sauce"], location=(ingredient_x + 9 * ingredient_step, ingredient_y, 0.0), wave=0.035, frequency=7, segments=28))

# The six numbered bins display the six ingredients used by the core game.
# Their mesh-local undersides remain at z=0; this transform simply rests each
# supply slightly above its bin floor. Optional ingredients remain off camera.
bin_supplies = (
	ingredients[0], # BunBottom
	ingredients[1], # Patty
	ingredients[2], # Lettuce
	ingredients[3], # CheeseSlice
	ingredients[5], # TomatoSlice
	ingredients[4], # BunTop
)
for ingredient, ingredient_bin in zip(bin_supplies, ingredient_bins):
	ingredient.parent = ingredient_bin
	ingredient.location = (0.0, 0.0, 0.12)
	if ingredient.name == "CheeseSlice":
		# The full-size cheese intentionally overhangs a burger, so only its
		# static bin-display instance is scaled to fit inside the bin walls.
		ingredient.scale = (0.72, 0.72, 1.0)

# One fixed camera; no lights.
camera_data = bpy.data.cameras.new("Camera")
camera_data.type = "PERSP"
camera_data.sensor_fit = "VERTICAL"
camera_data.lens = 52.0
camera_data.clip_start = 0.1
camera_data.clip_end = 100.0
camera = bpy.data.objects.new("Camera", camera_data)
MAIN.objects.link(camera)
camera.location = (0.0, -17.0, 10.0)
look_at(camera, (0.0, 0.35, 1.80))
bpy.context.scene.camera = camera

# Friendly scene metadata makes validation and handoff self-contained.
bpy.context.scene["asset_title"] = "Midnight Burger Bot"
bpy.context.scene["asset_scale"] = "1 Blender unit = 10 cm"
bpy.context.scene["asset_license"] = "Original student-created geometry"
bpy.context.scene["runtime_collection"] = "Main"

# Workbench render gives a close approximation of the vertex-color-only game look.
scene = bpy.context.scene
try:
	scene.render.engine = "BLENDER_WORKBENCH"
except TypeError:
	scene.render.engine = "BLENDER_EEVEE_NEXT"
scene.render.resolution_x = 1000
scene.render.resolution_y = 650
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = "PNG"
scene.render.filepath = PREVIEW_PATH
scene.render.film_transparent = False
scene.world.color = (0.015, 0.020, 0.030)
if scene.render.engine == "BLENDER_WORKBENCH":
	scene.display.shading.light = "STUDIO"
	scene.display.shading.color_type = "VERTEX"
	scene.display.shading.show_shadows = True
	scene.display.shading.show_cavity = True
	scene.display.shading.cavity_type = "BOTH"

# Select the full visible scene and frame it when the file opens.
bpy.ops.object.select_all(action="DESELECT")
counter.select_set(True)
bpy.context.view_layer.objects.active = counter

bpy.ops.wm.save_as_mainfile(filepath=BLEND_PATH)
bpy.ops.render.render(write_still=True)

# A second render exposes all ten ingredient meshes for visual QA while
# leaving the saved runtime camera untouched.
saved_camera_location = camera.location.copy()
saved_camera_rotation = camera.rotation_euler.copy()
saved_camera_lens = camera.data.lens
hidden_for_ingredient_preview = []
for obj in MAIN.objects:
	if obj not in ingredients and obj not in {camera, floor}:
		hidden_for_ingredient_preview.append((obj, obj.hide_render))
		obj.hide_render = True
saved_ingredient_matrices = [obj.matrix_world.copy() for obj in ingredients]
saved_ingredient_parents = [obj.parent for obj in ingredients]
for index, obj in enumerate(ingredients):
	obj.parent = None
	obj.location = (ingredient_x + index * ingredient_step, ingredient_y, 0.0)
camera.location = (-1.0, -20.0, 5.8)
camera.data.lens = 30.0
look_at(camera, (-1.0, -7.0, 0.20))
scene.render.filepath = INGREDIENT_PREVIEW_PATH
bpy.ops.render.render(write_still=True)
for obj, previous_hide_render in hidden_for_ingredient_preview:
	obj.hide_render = previous_hide_render
for obj, parent, matrix_world in zip(ingredients, saved_ingredient_parents, saved_ingredient_matrices):
	obj.parent = parent
	obj.matrix_world = matrix_world
camera.location = saved_camera_location
camera.rotation_euler = saved_camera_rotation
camera.data.lens = saved_camera_lens
scene.render.filepath = PREVIEW_PATH

print("MIDNIGHT_BURGER_BOT_BUILD_COMPLETE")
print("blend=" + BLEND_PATH)
print("preview=" + PREVIEW_PATH)
print("ingredient_preview=" + INGREDIENT_PREVIEW_PATH)
print("mesh_datablocks=" + str(len(bpy.data.meshes)))
print("main_objects=" + str(len(MAIN.objects)))
