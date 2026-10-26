#!/usr/bin/env python3
"""Turn a glTF binary that uses Draco mesh compression and WebP textures into a plain one Kosmos can load.

    python3 tools/glb_unpack.py in.glb out.glb

Needs `pip install DracoPy pillow numpy`. Only geometry (positions, normals, first UV set, indices) and the textures are
rewritten; everything else in the file is kept as it was.
"""
import io
import json
import struct
import sys

import DracoPy
import numpy as np
from PIL import Image


def read_glb(path):
    data = open(path, "rb").read()
    if data[:4] != b"glTF":
        raise SystemExit(f"{path}: not a GLB")
    chunk = struct.unpack("<I", data[12:16])[0]
    doc = json.loads(data[20:20 + chunk])
    rest = data[20 + chunk:]
    blob = b""
    if len(rest) >= 8:
        size = struct.unpack("<I", rest[:4])[0]
        blob = rest[8:8 + size]
    return doc, bytearray(blob)


def write_glb(path, doc, blob):
    while len(blob) % 4:
        blob.append(0)
    doc["buffers"] = [{"byteLength": len(blob)}]
    text = json.dumps(doc, separators=(",", ":")).encode()
    text += b" " * (-len(text) % 4)
    total = 12 + 8 + len(text) + 8 + len(blob)
    with open(path, "wb") as f:
        f.write(b"glTF" + struct.pack("<II", 2, total))
        f.write(struct.pack("<I", len(text)) + b"JSON" + text)
        f.write(struct.pack("<I", len(blob)) + b"BIN\0" + bytes(blob))


def add_view(doc, blob, payload, target=None):
    while len(blob) % 4:
        blob.append(0)
    view = {"buffer": 0, "byteOffset": len(blob), "byteLength": len(payload)}
    if target:
        view["target"] = target
    blob += payload
    doc["bufferViews"].append(view)
    return len(doc["bufferViews"]) - 1


def add_accessor(doc, blob, array, component, kind, target, bounds=False):
    view = add_view(doc, blob, array.tobytes(), target)
    count = len(array)
    accessor = {"bufferView": view, "componentType": component, "count": count, "type": kind}
    if bounds:
        accessor["min"] = array.min(axis=0).astype(float).tolist()
        accessor["max"] = array.max(axis=0).astype(float).tolist()
    doc["accessors"].append(accessor)
    return len(doc["accessors"]) - 1


def view_bytes(doc, blob, index):
    view = doc["bufferViews"][index]
    start = view.get("byteOffset", 0)
    return bytes(blob[start:start + view["byteLength"]])


def unpack(src, dst):
    doc, blob = read_glb(src)
    doc.setdefault("bufferViews", [])
    doc.setdefault("accessors", [])

    for mesh in doc.get("meshes", []):
        for prim in mesh["primitives"]:
            ext = prim.get("extensions", {}).get("KHR_draco_mesh_compression")
            if not ext:
                continue
            mesh_data = DracoPy.decode(view_bytes(doc, blob, ext["bufferView"]))
            points = np.asarray(mesh_data.points, dtype=np.float32).reshape(-1, 3)
            faces = np.asarray(mesh_data.faces, dtype=np.uint32).reshape(-1)
            attributes = {"POSITION": add_accessor(doc, blob, points, 5126, "VEC3", 34962, True)}
            if mesh_data.normals is not None and len(mesh_data.normals):
                attributes["NORMAL"] = add_accessor(doc, blob, np.asarray(mesh_data.normals, dtype=np.float32).reshape(-1, 3), 5126, "VEC3", 34962)
            if mesh_data.tex_coord is not None and len(mesh_data.tex_coord):
                attributes["TEXCOORD_0"] = add_accessor(doc, blob, np.asarray(mesh_data.tex_coord, dtype=np.float32).reshape(-1, 2), 5126, "VEC2", 34962)
            prim["attributes"] = attributes
            index_view = add_view(doc, blob, faces.tobytes(), 34963)
            doc["accessors"].append({"bufferView": index_view, "componentType": 5125, "count": len(faces), "type": "SCALAR"})
            prim["indices"] = len(doc["accessors"]) - 1
            del prim["extensions"]["KHR_draco_mesh_compression"]
            if not prim["extensions"]:
                del prim["extensions"]

    # WebP textures become PNG (or JPEG when there is no alpha) under the standard texture source.
    converted = {}
    for texture in doc.get("textures", []):
        webp = texture.get("extensions", {}).pop("EXT_texture_webp", None)
        if not texture.get("extensions"):
            texture.pop("extensions", None)
        if webp is not None:
            texture["source"] = webp["source"]
    for index, image in enumerate(doc.get("images", [])):
        if "bufferView" not in image or image.get("mimeType") == "image/png":
            continue
        raw = view_bytes(doc, blob, image["bufferView"])
        picture = Image.open(io.BytesIO(raw))
        if picture.format in ("JPEG", "PNG"):
            continue
        out = io.BytesIO()
        picture.convert("RGBA").save(out, "PNG")
        image["bufferView"] = add_view(doc, blob, out.getvalue())
        image["mimeType"] = "image/png"
        converted[index] = True

    for key in ("extensionsUsed", "extensionsRequired"):
        if key in doc:
            doc[key] = [e for e in doc[key] if e not in ("KHR_draco_mesh_compression", "EXT_texture_webp")]
            if not doc[key]:
                del doc[key]
    write_glb(dst, doc, blob)


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    unpack(sys.argv[1], sys.argv[2])
