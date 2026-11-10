"""Blender worker for tools/nasa_import.py: run with  blender -b -P tools/nasa_blender.py -- jobs.json report.json

Each job is a dict: {"src", "dst", "mode": "kit"|"prop"|"body", "fit": ["up"|"max", metres], "tris", "lay", "align", "flip"}.
  kit   - flight part: the mesh's up becomes the nose (-Z in glTF), the bottom sits at the origin, centred on the axis.
  prop  - scenery: stays upright (Y up in glTF) with its base on the ground, centred.
  body  - a small world: one unit-mean-radius mesh centred on its centre of mass, with sphere-projected UVs.
"""
import json
import math
import sys

import bmesh
import bpy
import mathutils


def clear():
    bpy.ops.wm.read_factory_settings(use_empty=True)


def meshes():
    return [o for o in bpy.context.scene.objects if o.type == "MESH"]


def join_all():
    objs = meshes()
    for o in objs:
        world = o.matrix_world.copy()
        o.parent = None
        o.matrix_world = world
    for o in list(bpy.context.scene.objects):
        if o.type != "MESH":
            bpy.data.objects.remove(o, do_unlink=True)
    bpy.ops.object.select_all(action="DESELECT")
    for o in objs:
        o.select_set(True)
    bpy.context.view_layer.objects.active = objs[0]
    if len(objs) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    if obj.data.users > 1:
        obj.data = obj.data.copy()
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    return obj


def triangles(obj):
    obj.data.calc_loop_triangles()
    return len(obj.data.loop_triangles)


def decimate(obj, budget):
    have = triangles(obj)
    if have <= budget:
        return
    mod = obj.modifiers.new("decimate", "DECIMATE")
    mod.ratio = budget / have
    mod.use_collapse_triangulate = True
    mesh = bpy.data.meshes.new_from_object(obj.evaluated_get(bpy.context.evaluated_depsgraph_get()))
    obj.modifiers.remove(mod)
    obj.data = mesh


def rotate(obj, matrix):
    obj.data.transform(matrix)
    obj.data.update()


def align_long_axis(obj):
    """Turn the mesh so the direction it is longest in (its principal axis) runs along Y."""
    import numpy as np
    points = np.array([tuple(v.co) for v in obj.data.vertices])
    centred = points - points.mean(axis=0)
    axis = np.linalg.svd(centred, full_matrices=False)[2][0]
    if axis[1] < 0:
        axis = -axis
    rotate(obj, mathutils.Vector(axis).rotation_difference(mathutils.Vector((0, 1, 0))).to_matrix().to_4x4())


def bounds(obj):
    lo = mathutils.Vector((1e30,) * 3)
    hi = mathutils.Vector((-1e30,) * 3)
    for v in obj.data.vertices:
        lo = mathutils.Vector(map(min, lo, v.co))
        hi = mathutils.Vector(map(max, hi, v.co))
    return lo, hi


def shift(obj, delta):
    for v in obj.data.vertices:
        v.co += delta
    obj.data.update()


def scale_to(obj, factor):
    for v in obj.data.vertices:
        v.co *= factor
    obj.data.update()


def shrink_textures(limit=2048):
    for img in bpy.data.images:
        if img.size[0] > limit or img.size[1] > limit:
            k = limit / max(img.size)
            img.scale(max(1, int(img.size[0] * k)), max(1, int(img.size[1] * k)))


def export(path):
    bpy.ops.export_scene.gltf(filepath=path, export_format="GLB", export_draco_mesh_compression_enable=False, export_image_format="AUTO",
                              export_apply=True, export_yup=True)


def sphere_uvs(obj):
    mesh = obj.data
    bm = bmesh.new()
    bm.from_mesh(mesh)
    layer = bm.loops.layers.uv.verify()
    for face in bm.faces:
        us = []
        for loop in face.loops:
            x, y, z = loop.vert.co
            u = math.atan2(x, z) / (2.0 * math.pi) + 0.5
            v = math.atan2(math.hypot(x, z), y) / math.pi
            loop[layer].uv = (u, 1.0 - v)
            us.append(u)
        if max(us) - min(us) > 0.5:
            for loop in face.loops:
                if loop[layer].uv[0] < 0.5:
                    loop[layer].uv[0] += 1.0
    bm.to_mesh(mesh)
    bm.free()


def do_kit_or_prop(job):
    clear()
    bpy.ops.import_scene.gltf(filepath=job["src"])
    obj = join_all()
    decimate(obj, job["tris"])
    if job.get("align"):
        align_long_axis(obj)
    if job.get("flip"):
        rotate(obj, mathutils.Matrix.Rotation(math.pi, 4, "Z"))
    lo, hi = bounds(obj)
    size = hi - lo
    basis, metres = job["fit"]
    scale_to(obj, metres / (size.z if basis == "up" else max(size)))
    if job["mode"] == "kit" and job.get("lay", True):
        rotate(obj, mathutils.Matrix.Rotation(math.radians(-90.0), 4, "X"))
    lo, hi = bounds(obj)
    centre = (lo + hi) * 0.5
    if job["mode"] == "kit":
        shift(obj, mathutils.Vector((-centre.x, -lo.y, -centre.z)))
    else:
        shift(obj, mathutils.Vector((-centre.x, -centre.y, -lo.z)))
    lo, hi = bounds(obj)
    shrink_textures()
    export(job["dst"])
    size = hi - lo
    return {"tris": triangles(obj), "size": [round(size.x, 3), round(size.y, 3), round(size.z, 3)], "materials": len(obj.data.materials)}


def do_body(job):
    clear()
    bpy.ops.wm.stl_import(filepath=job["src"])
    obj = meshes()[0]
    bpy.context.view_layer.objects.active = obj
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bm = bmesh.new()
    bm.from_mesh(obj.data)
    bmesh.ops.remove_doubles(bm, verts=bm.verts, dist=1e-4)
    bm.to_mesh(obj.data)
    bm.free()
    if job.get("halves"):
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.separate(type="LOOSE")
        bpy.ops.object.mode_set(mode="OBJECT")
        parts = sorted(meshes(), key=triangles, reverse=True)
        if len(parts) != 2:
            raise RuntimeError(f"expected two print halves, found {len(parts)}")
        angle, dx, dy, plane = job["halves"]
        for part in parts:
            bm = bmesh.new()
            bm.from_mesh(part.data)
            caps = [face for face in bm.faces
                    if face.normal.z < -0.99999 and all(abs(v.co.z - plane) < 0.001 for v in face.verts)]
            if not caps:
                bm.free()
                raise RuntimeError("print half has no cut-plane faces")
            bmesh.ops.delete(bm, geom=caps, context="FACES_ONLY")
            bm.to_mesh(part.data)
            bm.free()
        c, s = math.cos(angle), math.sin(angle)
        transform = mathutils.Matrix(((c, s, 0, dx), (s, -c, 0, dy),
                                      (0, 0, -1, 2 * plane), (0, 0, 0, 1)))
        parts[1].data.transform(transform)
        obj = join_all()
    decimate(obj, job["tris"])
    count = len(obj.data.vertices)
    centre = sum((v.co for v in obj.data.vertices), mathutils.Vector()) / count
    shift(obj, -centre)
    mean = sum(v.co.length for v in obj.data.vertices) / count
    scale_to(obj, 1.0 / mean)
    sphere_uvs(obj)
    bpy.ops.object.shade_smooth()
    export(job["dst"])
    lo, hi = bounds(obj)
    return {"tris": triangles(obj), "size": [round(c, 3) for c in (hi - lo)], "materials": 0}


def main():
    args = sys.argv[sys.argv.index("--") + 1:]
    jobs = json.load(open(args[0]))
    report = {}
    for job in jobs:
        try:
            report[job["id"]] = do_body(job) if job["mode"] == "body" else do_kit_or_prop(job)
        except Exception as e:
            report[job["id"]] = {"error": str(e)}
        print("DONE", job["id"], report[job["id"]], flush=True)
    json.dump(report, open(args[1], "w"), indent=1)


main()
