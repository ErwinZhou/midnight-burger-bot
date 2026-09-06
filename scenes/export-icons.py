"""Render transparent order icons without saving changes to the source scene.

blender --background --python scenes/export-icons.py
"""
from pathlib import Path
import bpy
from mathutils import Vector

root = Path(__file__).resolve().parent.parent
names = ["BunBottom", "Patty", "Lettuce", "CheeseSlice", "BunTop",
         "TomatoSlice", "OnionRing", "PickleSlice", "BaconStrip", "SauceBlob"]
bpy.ops.wm.open_mainfile(filepath=str(root / "scenes/burger.blend"))
scene = bpy.context.scene
scene.render.engine = "CYCLES"
scene.cycles.samples = 16
scene.render.resolution_x = 192
scene.render.resolution_y = 192
scene.render.resolution_percentage = 100
scene.render.film_transparent = True
scene.render.image_settings.file_format = "PNG"
scene.render.image_settings.color_mode = "RGBA"
scene.view_settings.view_transform = "Standard"
scene.view_settings.look = "None"
scene.view_settings.exposure = 0
scene.view_settings.gamma = 1
for obj in scene.objects:
    obj.hide_render = True
world = bpy.data.worlds.new("IconWorld")
world.use_nodes = True
world.node_tree.nodes["Background"].inputs["Color"].default_value = (1, 1, 1, 1)
world.node_tree.nodes["Background"].inputs["Strength"].default_value = 0.8
scene.world = world
camera_data = bpy.data.cameras.new("IconCamera")
camera_data.type = "ORTHO"
camera = bpy.data.objects.new("IconCamera", camera_data)
scene.collection.objects.link(camera)
camera.location = (0, -3, 3)
camera.rotation_euler = (-camera.location).to_track_quat("-Z", "Y").to_euler()
scene.camera = camera
output = root / "dist/icons"
output.mkdir(exist_ok=True)
for name in names:
    source = bpy.data.objects[name]
    obj = source.copy()
    obj.data = source.data.copy()
    scene.collection.objects.link(obj)
    obj.parent = None
    obj.location = (0, 0, 0)
    obj.rotation_euler = (0, 0, 0)
    obj.scale = (1, 1, 1)
    obj.hide_render = False
    corners = [Vector(v) for v in obj.bound_box]
    low = Vector(tuple(min(v[i] for v in corners) for i in range(3)))
    high = Vector(tuple(max(v[i] for v in corners) for i in range(3)))
    obj.location = -(low + high) * 0.5
    camera_data.ortho_scale = max(high.x-low.x, high.y-low.y, high.z-low.z) * 1.35
    scene.render.filepath = str(output / (name + ".png"))
    bpy.ops.render.render(write_still=True)
    bpy.data.objects.remove(obj, do_unlink=True)
print("exported ten order icons")
