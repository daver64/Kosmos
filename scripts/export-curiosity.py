"""Export NASA's Curiosity Blender model for Kosmos.

Run: blender -b -t 4 'import/nasa/Curiosity Rover (MSL) (Dirty).blend' \
    --python scripts/export-curiosity.py
"""

import math
from pathlib import Path

import bpy
from mathutils import Matrix

root = Path(__file__).resolve().parents[1]
output = root / "addons/nasa-curiosity/curiosity.glb"
output.parent.mkdir(parents=True, exist_ok=True)

# Flatten the source hierarchy, keeping visible rover meshes but not the
# pivot markers, camera placeholders or interior stand-ins.
keep = []
for obj in list(bpy.context.scene.objects):
    materials = [slot.material for slot in obj.material_slots] if obj.type == "MESH" else []
    if obj.type != "MESH" or not materials or any(m is None or m.name in {"pivot", "base_gray"} for m in materials):
        continue
    mesh = obj.data.copy()
    mesh.transform(Matrix.Translation((0, 0, -0.8)) @ Matrix.Rotation(math.pi, 4, "Z") @ obj.matrix_world)
    copy = bpy.data.objects.new(obj.name + "_export", mesh)
    bpy.context.scene.collection.objects.link(copy)
    keep.append((copy, obj.name))

# The source uses Diffuse BSDF nodes, which glTF does not export as textured
# materials. Connect the existing packed images to Principled shaders instead.
for material in bpy.data.materials:
    if not material.use_nodes:
        continue
    image = next((node.image for node in material.node_tree.nodes
                  if node.type == "TEX_IMAGE" and node.image), None)
    if not image:
        continue
    nodes = material.node_tree.nodes
    nodes.clear()
    shader = nodes.new("ShaderNodeBsdfPrincipled")
    shader.inputs["Roughness"].default_value = 0.8
    texture = nodes.new("ShaderNodeTexImage")
    texture.image = image
    out = nodes.new("ShaderNodeOutputMaterial")
    material.node_tree.links.new(texture.outputs["Color"], shader.inputs["Base Color"])
    material.node_tree.links.new(shader.outputs["BSDF"], out.inputs["Surface"])

for obj in list(bpy.context.scene.objects):
    if all(obj != item[0] for item in keep):
        bpy.data.objects.remove(obj, do_unlink=True)
bpy.ops.object.select_all(action="DESELECT")
for obj, name in keep:
    obj.name = name
    obj.select_set(True)

result = bpy.ops.export_scene.gltf(
    filepath=str(output), export_format="GLB", use_selection=True,
    export_yup=True, export_animations=False,
)
if result != {"FINISHED"}:
    raise RuntimeError(f"Curiosity export failed: {result}")
print(f"Exported {len(keep)} rover meshes to {output}")
