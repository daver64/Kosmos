# A scripted edit, to show that Blender can change a mesh without anyone clicking: widens it and paints it red.
#   blender -b --python tools/blender_example_edit.py -- part.glb
# It writes the result back to the same file, just as "Send to Kosmos" does, so Kosmos reloads it.
import sys

import bpy

path = sys.argv[sys.argv.index("--") + 1]
bpy.ops.object.select_all(action="SELECT")
bpy.ops.object.delete()
bpy.ops.import_scene.gltf(filepath=path)
for obj in bpy.context.scene.objects:
    if obj.type != "MESH":
        continue
    for vertex in obj.data.vertices:
        vertex.co.x *= 1.35
        vertex.co.y *= 1.35
    for layer in obj.data.color_attributes:
        for item in layer.data:
            item.color = (0.85, 0.12, 0.08, 1.0)
bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", export_yup=True, export_apply=True, export_colors=True)
