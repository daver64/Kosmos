#!/usr/bin/env python3
"""Convert Orbiter .msh meshes (MSHX1) into Wavefront OBJ/MTL for Kosmos.

    msh_import.py <addon root | Meshes dir | file.msh> [--out DIR] [--scale S] [--textures DIR]
    msh_import.py --selftest

Orbiter is left-handed with the ship's nose along +Z. Kosmos is right-handed with the nose along -Z, so the
converter negates Z (positions and normals) and keeps the index order; the mirror flips the apparent winding, so
Orbiter's clockwise front faces come out counter-clockwise, which is what simlib expects. V is flipped for OBJ.

Orbiter materials become MTL entries (diffuse -> Kd, specular -> Ks, emissive -> Ke). A group's texture and material
are combined into one MTL entry named <material>_<texture>. DDS textures are decoded with Pillow and written as PNG,
because simlib's model loader reads PNG/JPG/BMP/TGA for OBJ materials.
"""
import argparse
import os
import re
import shutil
import sys
import tempfile


class Group:
    def __init__(self):
        self.label = ""
        self.material = 0
        self.texture = 0
        self.nonormal = False
        self.vertices = []   # (x, y, z, nx, ny, nz, u, v)
        self.triangles = []


class Mesh:
    def __init__(self):
        self.groups = []
        self.materials = []  # (name, diffuse, specular, emissive)
        self.textures = []


def nums(line):
    out = []
    for t in line.split(";")[0].split():
        try:
            out.append(float(t))
        except ValueError:
            out.append(0.0)  # Windows "-1.#IND" / "1.#QNAN"
    return out


def parse_msh(path):
    with open(path, "rb") as f:
        lines = f.read().decode("latin1").replace("\r", "").split("\n")
    if not lines or not lines[0].startswith("MSHX1"):
        raise ValueError(f"{path}: not an Orbiter MSHX1 mesh")
    mesh = Mesh()
    i = 1
    while i < len(lines):
        line = lines[i].strip()
        word = line.split(";")[0].split()
        i += 1
        if not word:
            continue
        key = word[0].upper()
        if key == "GROUPS":
            count = int(word[1])
            for _ in range(count):
                g = Group()
                while i < len(lines):
                    w = lines[i].split(";")[0].split()
                    cur = lines[i]
                    i += 1
                    if not w:
                        continue
                    k = w[0].upper()
                    if k == "LABEL":
                        g.label = cur.strip()[5:].strip()
                    elif k == "MATERIAL":
                        g.material = int(w[1])
                    elif k == "TEXTURE":
                        g.texture = int(w[1])
                    elif k == "NONORMAL":
                        g.nonormal = True
                    elif k == "GEOM":
                        nv, nt = int(w[1]), int(w[2])
                        for _ in range(nv):
                            v = nums(lines[i])
                            i += 1
                            if g.nonormal:
                                v = (v + [0, 0, 0, 0, 0])[:3] + [0.0, 0.0, 0.0] + (v[3:5] if len(v) >= 5 else [0.0, 0.0])
                            else:
                                v = (v + [0] * 8)[:8] if len(v) >= 6 else (v + [0] * 8)[:8]
                            g.vertices.append(tuple(v))
                        for _ in range(nt):
                            t = [int(x) for x in lines[i].split(";")[0].split()]
                            i += 1
                            g.triangles.append(tuple(t[:3]))
                        break
                mesh.groups.append(g)
        elif key == "MATERIALS":
            names = []
            for _ in range(int(word[1])):
                names.append(lines[i].strip())
                i += 1
            by_name = {}
            while len(by_name) < len(names) and i < len(lines):
                w = lines[i].split()
                i += 1
                if w and w[0].upper() == "MATERIAL":
                    name = lines[i - 1].strip()[8:].strip()
                    rows = [nums(lines[i + k]) for k in range(4)]
                    i += 4
                    by_name[name] = (rows[0][:4], rows[2][:4], rows[3][:3])
            for name in names:
                mesh.materials.append((name,) + by_name.get(name, ([1, 1, 1, 1], [0, 0, 0, 1], [0, 0, 0])))
        elif key == "TEXTURES":
            for _ in range(int(word[1])):
                mesh.textures.append(lines[i].strip().split()[0])
                i += 1
    return mesh


def convert_texture(src_dir, name, out_dir):
    """Decode a texture to PNG in out_dir and return the file name, or None when it cannot be found or read."""
    if name.startswith(";"):
        return None  # commented out in the mesh
    name = name.replace("\\", "/")  # Orbiter paths use backslashes
    stem = os.path.splitext(os.path.basename(name))[0]
    target = os.path.join(out_dir, stem + ".png")
    if os.path.exists(target):
        return stem + ".png"
    candidates = []
    for base in src_dir:
        for ext in ("", ".dds", ".DDS", ".bmp", ".png", ".jpg", ".tga"):
            candidates.append(os.path.join(base, name if not ext else os.path.splitext(name)[0] + ext))
    lower = {}
    for base in src_dir:
        if os.path.isdir(base):
            for dirpath, _, files in os.walk(base):
                for f in files:
                    lower[(base, os.path.relpath(os.path.join(dirpath, f), base).lower())] = os.path.join(dirpath, f)
    path = next((c for c in candidates if os.path.isfile(c)), None)
    if path is None:
        for base in src_dir:
            hit = lower.get((base, name.lower()))
            if hit:
                path = hit
                break
            for ext in (".dds", ".bmp", ".png", ".jpg", ".tga"):
                hit = lower.get((base, stem.lower() + ext))
                if hit:
                    path = hit
                    break
            if path:
                break
    if path is None:
        print(f"  texture {name}: not found", file=sys.stderr)
        return None
    try:
        from PIL import Image
        image = Image.open(path)
        image.load()
        image = image.convert("RGBA")
        if all(p[3] == 255 for p in image.getdata()):
            image = image.convert("RGB")
        os.makedirs(out_dir, exist_ok=True)
        image.save(target)
        return stem + ".png"
    except Exception as e:  # unsupported DDS variants fall back to untextured
        print(f"  texture {name}: {e}", file=sys.stderr)
        return None


def write_obj(mesh, out_base, texture_dirs, scale=1.0, tex_out=None):
    """Writes out_base.obj/.mtl. Returns (vertex count, triangle count, textures written)."""
    out_dir = os.path.dirname(out_base) or "."
    tex_out = tex_out or out_dir
    os.makedirs(out_dir, exist_ok=True)
    converted = {}
    mtl_names = {}
    mtl_lines = []

    def material_for(g):
        screen = g.label if re.search(r"DISPLAY$|DISP$", g.label or "") else ""
        key = (g.material, g.texture, screen)
        if key in mtl_names:
            return mtl_names[key]
        mat = mesh.materials[g.material - 1] if 0 < g.material <= len(mesh.materials) else None
        tex = mesh.textures[g.texture - 1] if 0 < g.texture <= len(mesh.textures) else None
        base = re.sub(r"[^A-Za-z0-9_]+", "_", (mat[0] if mat else "default").strip("- ") or "default")
        name = base + ("_" + re.sub(r"[^A-Za-z0-9_]+", "_", os.path.splitext(tex)[0]) if tex else "")
        if screen:
            name += "_" + screen # a display group keeps its own material so the game can draw into it
        n, k = name, 2
        while n in mtl_names.values():
            n, k = f"{name}_{k}", k + 1
        mtl_names[key] = n
        diffuse = mat[1] if mat else [1, 1, 1, 1]
        specular = mat[2] if mat else [0, 0, 0, 1]
        emissive = mat[3] if mat else [0, 0, 0]
        mtl_lines.append(f"newmtl {n}")
        mtl_lines.append("Kd %.3f %.3f %.3f" % tuple(diffuse[:3]) if not tex else "Kd 1.000 1.000 1.000")
        mtl_lines.append("Ks %.3f %.3f %.3f" % tuple(specular[:3]))
        mtl_lines.append("Ke %.3f %.3f %.3f" % tuple(emissive[:3]))
        if len(diffuse) > 3 and diffuse[3] < 1.0:
            mtl_lines.append("d %.3f" % diffuse[3])
        if tex:
            if tex not in converted:
                converted[tex] = convert_texture(texture_dirs, tex, tex_out)
            if converted[tex]:
                rel = os.path.relpath(os.path.join(tex_out, converted[tex]), out_dir).replace(os.sep, "/")
                mtl_lines.append("map_Kd " + rel)
        mtl_lines.append("")
        return n

    obj = [f"# converted from an Orbiter .msh by msh_import.py", f"mtllib {os.path.basename(out_base)}.mtl"]
    base_index = 1
    tris = 0
    for gi, g in enumerate(mesh.groups):
        if not g.triangles:
            continue
        obj.append(f"g {re.sub(r'[^A-Za-z0-9_]+', '_', g.label) or 'group%d' % gi}")
        obj.append(f"usemtl {material_for(g)}")
        for v in g.vertices:
            obj.append("v %.5f %.5f %.5f" % (v[0] * scale, v[1] * scale, -v[2] * scale))
        for v in g.vertices:
            obj.append("vn %.4f %.4f %.4f" % (v[3], v[4], -v[5]))
        # A render target is stored bottom-up, so display groups keep Orbiter's V instead of flipping it.
        is_display = bool(re.search(r"DISPLAY$|DISP$", g.label or ""))
        for v in g.vertices:
            obj.append("vt %.5f %.5f" % (v[6], v[7] if is_display else 1.0 - v[7]))
        for a, b, c in g.triangles:
            a, b, c = (x + base_index for x in (a, b, c))
            obj.append(f"f {a}/{a}/{a} {b}/{b}/{b} {c}/{c}/{c}")
        base_index += len(g.vertices)
        tris += len(g.triangles)
    with open(out_base + ".obj", "w") as f:
        f.write("\n".join(obj) + "\n")
    with open(out_base + ".mtl", "w") as f:
        f.write("\n".join(mtl_lines) + "\n")
    return base_index - 1, tris, sum(1 for v in converted.values() if v)


def find_meshes(path):
    if os.path.isfile(path):
        return [path], [os.path.dirname(path)]
    roots = [path, os.path.join(path, "orbiter"), os.path.join(path, "Orbiter")]
    for root in roots:
        for name in os.listdir(root) if os.path.isdir(root) else []:
            if name.lower() == "meshes":
                mdir = os.path.join(root, name)
                files = sorted(os.path.join(d, f) for d, _, fs in os.walk(mdir)
                               for f in fs if f.lower().endswith(".msh"))
                tex = [os.path.join(root, t) for t in os.listdir(root) if t.lower() == "textures"]
                return files, tex
    raise SystemExit(f"no Meshes folder under {path}")


def selftest():
    text = ("MSHX1\nGROUPS 2\nLABEL a\nMATERIAL 1\nTEXTURE 0\nGEOM 3 1 ; c\n0 0 1 0 0 1\n1 0 1 0 0 1\n0 1 1 0 0 1\n0 1 2\n"
            "MATERIAL 2\nTEXTURE 1\nGEOM 3 1\n0 0 0 0 0 1 0 0\n1 0 0 0 0 1 1 0\n0 1 0 0 0 1 0 1\n0 1 2\n"
            "MATERIALS 2\nred\ngrey\nMATERIAL red\n1 0 0 1\n1 0 0 1\n1 1 1 1\n0 0 0 1\n"
            "MATERIAL grey\n.5 .5 .5 1\n.5 .5 .5 1\n1 1 1 1\n0 0 0 1\nTEXTURES 1\nnone.dds D\n")
    with tempfile.TemporaryDirectory() as d:
        p = os.path.join(d, "t.msh")
        with open(p, "w") as f:
            f.write(text)
        mesh = parse_msh(p)
        assert len(mesh.groups) == 2 and mesh.groups[1].texture == 1 and mesh.textures == ["none.dds"]
        assert mesh.materials[0][0] == "red" and mesh.materials[0][1][:3] == [1, 0, 0]
        nv, nt, _ = write_obj(mesh, os.path.join(d, "t"), [d])
        assert (nv, nt) == (6, 2)
        obj = open(os.path.join(d, "t.obj")).read()
        assert "v 0.00000 0.00000 -1.00000" in obj and "vt 0.00000 1.00000" in obj
        assert "usemtl red" in obj and "usemtl grey_none" in obj
    print("selftest ok")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("path", nargs="?", help="Orbiter add-on root, its Meshes folder, or one .msh file")
    ap.add_argument("--out", help="output folder (default: <path>/models)")
    ap.add_argument("--scale", type=float, default=1.0)
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        return selftest()
    if not args.path:
        ap.error("path required")
    files, tex_dirs = find_meshes(args.path)
    out = args.out or os.path.join(args.path if os.path.isdir(args.path) else os.path.dirname(args.path), "models")
    for f in files:
        name = os.path.splitext(os.path.basename(f))[0].lower()
        try:
            nv, nt, ntex = write_obj(parse_msh(f), os.path.join(out, name), tex_dirs, args.scale, os.path.join(out, "textures"))
            print(f"{os.path.basename(f)}: {nv} vertices, {nt} triangles, {ntex} textures -> {out}/{name}.obj")
        except Exception as e:
            print(f"{os.path.basename(f)}: skipped ({e})", file=sys.stderr)


if __name__ == "__main__":
    main()
