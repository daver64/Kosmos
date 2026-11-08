"""Export NASA's decoded Space Exploration Vehicle GLB for Kosmos.

Run: blender -b -t 4 --python-exit-code 1 --python scripts/export-sev.py
"""

from pathlib import Path

import bpy
from mathutils import Matrix

root = Path(__file__).resolve().parents[1]
source = root / "import/nasa/Space Exploration Vehicle (Blender).glb"
output = root / "addons/nasa-sev/sev.glb"

bpy.ops.wm.read_factory_settings(use_empty=True)
result = bpy.ops.import_scene.gltf(filepath=str(source))
if result != {"FINISHED"}:
    raise RuntimeError(f"SEV import failed: {result}")
objects = [obj for obj in bpy.context.scene.objects if obj.type == "MESH"]
if len(objects) != 1:
    raise RuntimeError(f"Expected one SEV mesh, found {len(objects)}")
obj = objects[0]
obj.data.transform(Matrix.Translation((0, -0.87, 1.4)) @ obj.matrix_world)
obj.matrix_world = Matrix.Identity(4)
obj.data.calc_loop_triangles()
modifier = obj.modifiers.new("Triangle budget", "DECIMATE")
modifier.ratio = min(1.0, 100000 / len(obj.data.loop_triangles))
modifier.use_collapse_triangulate = True
bpy.context.view_layer.objects.active = obj
obj.select_set(True)
bpy.ops.object.modifier_apply(modifier=modifier.name)
for image in bpy.data.images:
    if image.size[0] > 2048 or image.size[1] > 2048:
        scale = 2048 / max(image.size)
        image.scale(max(1, round(image.size[0] * scale)), max(1, round(image.size[1] * scale)))
output.parent.mkdir(parents=True, exist_ok=True)
result = bpy.ops.export_scene.gltf(
    filepath=str(output), export_format="GLB", use_selection=True,
    export_yup=True, export_animations=False, export_draco_mesh_compression_enable=False,
)
if result != {"FINISHED"}:
    raise RuntimeError(f"SEV export failed: {result}")
obj.data.calc_loop_triangles()
print(f"Exported SEV: {len(obj.data.loop_triangles)} triangles to {output}")
