# Started by Kosmos ("Edit in Blender"): blender --python kosmos_edit.py -- <mesh file>
# Imports the mesh, and writes it back to the same file whenever you save (Ctrl+S) or use File > Send to Kosmos.
# Kosmos reloads the file as soon as it changes. Closing Blender resumes the simulation.
import os
import sys

import bpy

path = os.path.abspath(sys.argv[sys.argv.index("--") + 1])
ext = os.path.splitext(path)[1].lower()


def clear_scene():
    bpy.ops.object.select_all(action="SELECT")
    bpy.ops.object.delete()


def import_mesh():
    if ext in (".glb", ".gltf"):
        bpy.ops.import_scene.gltf(filepath=path)
    elif ext == ".obj":
        if hasattr(bpy.ops.wm, "obj_import"):
            bpy.ops.wm.obj_import(filepath=path)
        else:
            bpy.ops.import_scene.obj(filepath=path)
    else:
        raise RuntimeError("Kosmos can edit .glb, .gltf and .obj meshes, not " + ext)


def try_export(op, **options):
    # Option names differ between Blender versions; drop the ones this version does not know.
    while True:
        try:
            return op(**options)
        except TypeError as error:
            unknown = [k for k in options if k not in ("filepath",) and ('"%s"' % k in str(error) or "'%s'" % k in str(error))]
            if not unknown:
                raise
            for k in unknown:
                options.pop(k)


def export_mesh():
    # Kosmos ships point their nose along -Z with +Y up, which is also glTF's and the OBJ exporter's default.
    if ext in (".glb", ".gltf"):
        try_export(bpy.ops.export_scene.gltf, filepath=path, export_format="GLB" if ext == ".glb" else "GLTF_SEPARATE",
                   export_yup=True, export_apply=True, export_colors=True, export_vertex_color="ACTIVE")
    elif hasattr(bpy.ops.wm, "obj_export"):
        try_export(bpy.ops.wm.obj_export, filepath=path, forward_axis="NEGATIVE_Z", up_axis="Y", apply_modifiers=True)
    else:
        try_export(bpy.ops.export_scene.obj, filepath=path, axis_forward="-Z", axis_up="Y")
    print("[kosmos] wrote", path)


@bpy.app.handlers.persistent
def on_save(*_):
    export_mesh()


class KOSMOS_OT_send(bpy.types.Operator):
    bl_idname = "kosmos.send"
    bl_label = "Send to Kosmos"
    bl_description = "Write the mesh back to the file Kosmos is watching"

    def execute(self, context):
        export_mesh()
        self.report({"INFO"}, "Sent to Kosmos")
        return {"FINISHED"}


def menu(self, context):
    self.layout.separator()
    self.layout.operator(KOSMOS_OT_send.bl_idname, icon="EXPORT")


clear_scene()
import_mesh()
# Saving a .blend beside the mesh means Ctrl+S needs no file dialog.
bpy.ops.wm.save_as_mainfile(filepath=path + ".blend")
bpy.utils.register_class(KOSMOS_OT_send)
bpy.types.TOPBAR_MT_file.append(menu)
bpy.app.handlers.save_post.append(on_save)
