# Renders a mesh the way Blender's viewport shows it (vertex colours, studio lighting); used for the manual's pictures.
#   blender -b --python tools/blender_render.py -- model.glb out.png
import sys

import bpy
from mathutils import Vector

src, out = sys.argv[sys.argv.index("--") + 1:][:2]
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete()
bpy.ops.import_scene.gltf(filepath=src)
scene = bpy.context.scene
scene.render.engine = "BLENDER_WORKBENCH"
scene.render.resolution_x, scene.render.resolution_y = 900, 700
scene.display.shading.color_type = "VERTEX"
scene.display.shading.light = "STUDIO"
scene.display.shading.show_cavity = True
world = bpy.data.worlds.new("world")
world.color = (0.12, 0.14, 0.18)
scene.world = world
meshes = [o for o in scene.objects if o.type == "MESH"]
points = [o.matrix_world @ Vector(c) for o in meshes for c in o.bound_box]
low = Vector(min(p[i] for p in points) for i in range(3))
high = Vector(max(p[i] for p in points) for i in range(3))
centre, size = (low + high) / 2, (high - low).length
camera = bpy.data.objects.new("camera", bpy.data.cameras.new("camera"))
scene.collection.objects.link(camera)
scene.camera = camera
camera.data.lens = 60
camera.location = centre + Vector((0.9, -1.0, 0.55)).normalized() * size * 1.9
camera.rotation_euler = (centre - camera.location).to_track_quat("-Z", "Y").to_euler()
scene.render.filepath = out
bpy.ops.render.render(write_still=True)
