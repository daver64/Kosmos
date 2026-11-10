"""Export NASA's Gateway Core GLB for Kosmos.

Run: blender -b -t 4 --python-exit-code 1 --python scripts/export-gateway.py
"""

from pathlib import Path

import bpy

root = Path(__file__).resolve().parents[1]
source = root / "import/nasa/New/Gateway Core.glb"
output = root / "addons/nasa-gateway/gateway.glb"

bpy.ops.wm.read_factory_settings(use_empty=True)
if bpy.ops.import_scene.gltf(filepath=str(source)) != {"FINISHED"}:
    raise RuntimeError(f"Could not import {source}")
objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
if not objects:
    raise RuntimeError("Gateway contains no meshes")

for obj in objects:
    obj.data.calc_loop_triangles()
    count = len(obj.data.loop_triangles)
    if count > 20000:
        modifier = obj.modifiers.new("Triangle budget", "DECIMATE")
        modifier.ratio = 20000 / count
        modifier.use_collapse_triangulate = True
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=modifier.name)

output.parent.mkdir(parents=True, exist_ok=True)
if bpy.ops.export_scene.gltf(filepath=str(output), export_format="GLB",
                             export_yup=True, export_animations=False,
                             export_draco_mesh_compression_enable=False) != {"FINISHED"}:
    raise RuntimeError(f"Could not export {output}")
print(f"Exported Gateway to {output}")
