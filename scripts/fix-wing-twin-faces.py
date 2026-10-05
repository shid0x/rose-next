"""Remove the z-fighting back-face twins from a two-sided back-item mesh.

Usage:
    python scripts/fix-wing-twin-faces.py --dry-run
    python scripts/fix-wing-twin-faces.py
    python scripts/fix-wing-twin-faces.py --verify
    python scripts/fix-wing-twin-faces.py --restore

The case (2026-10-05): Phantom Shadow Wings (6:1074, Jrose) flickered in one small
patch of the left wing while the idle animation moved it. The wing is two parts;
the outer one, archangel_wing02_0214_wa.ZMS, is drawn with a TWO-SIDED material
(ZSC material flag 2) -- and 110 of its 640 triangles also come with a twin: the
same three positions and UVs, the opposite winding, and their own vertices whose
normals point the other way. That is how an exporter fakes double-sided geometry
for a one-sided material. With a two-sided one, both twins draw from every angle
at exactly the same depth, one lit as facing the camera and one as facing away,
and which wins flips as the wing moves: a bright/dark flicker. 10 of the 110 sit
in the outer feather tips, beyond the inner part's extent, where the report was.

The fix drops one triangle of each pair. Which one: the copy facing the default
camera. The avatar's back dummy (p_03 in MALE/FEMALE.ZMD, DUMMY_IDX_BACK) maps
its local -y to world +Y, i.e. straight behind the character (the character
faces -Y, the dummy sits at y = +7.9 cm), so a back item's local -y side is the
side the camera sees; every pair here has exactly one copy facing local -y. The
kept copy draws two-sided like the other 420 triangles of the mesh, so seen from
the front it is lit as the rest of the wing already is.

The mesh also stores a triangle strip after the face list, and the engine draws
the strip when one is present (zz_mesh_tool.cpp load_mesh_8 -> TYPE_STRIP), so the
strip is cleared: the engine then draws the face list (TYPE_LIST), as it does for
149 of our 209 v8 back meshes. Vertices are left in place (the unused ones cost
nothing); the bounding box and pool type are unchanged. Refuses a mesh with
material-id face ranges (none here), which removing faces would invalidate.

The mesh is shared by Fallen Gabriel Wings (6:962), which is fixed with it.
Client-only data: re-bake. Originals go to build/wing-twin-faces/.
"""
import argparse, os, shutil, struct, sys
import numpy as np

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")
DATA = os.path.join(ROOT, "data")
BACKUP = os.path.join(ROOT, "build", "wing-twin-faces")
MESHES = [r"3Ddata\AVATAR\BACK\archangel_wing02_0214_wa.ZMS"]
EXPECTED_TWINS = {MESHES[0]: 110}


def parse(b):
    e = b.index(b"\0")
    if b[:e] != b"ZMS0008":
        raise ValueError("not a ZMS0008 mesh")
    p = e + 1
    fmt = struct.unpack_from("<I", b, p)[0]
    p += 4 + 24
    nb = struct.unpack_from("<H", b, p)[0]
    p += 2 + 2 * nb
    nv = struct.unpack_from("<H", b, p)[0]
    p += 2
    pos = np.frombuffer(b, "<f4", nv * 3, p).reshape(nv, 3).astype(float)
    p += 12 * nv
    if not fmt & 4:
        raise ValueError("mesh has no normals")
    nrm = np.frombuffer(b, "<f4", nv * 3, p).reshape(nv, 3).astype(float)
    p += 12 * nv
    if fmt & 8:
        p += 16 * nv
    if (fmt & 16) and (fmt & 32):
        p += 24 * nv
    if fmt & 64:
        p += 12 * nv
    for bit in (128, 256, 512, 1024):
        if fmt & bit:
            p += 8 * nv
    face_at = p
    nf = struct.unpack_from("<H", b, p)[0]
    p += 2
    faces = np.frombuffer(b, "<u2", nf * 3, p).reshape(nf, 3).astype(int)
    p += 6 * nf
    nmat = struct.unpack_from("<H", b, p)[0]
    mat_at = p
    p += 2 + 2 * nmat
    nstrip = struct.unpack_from("<H", b, p)[0]
    strip_at = p
    p += 2 + 2 * nstrip
    tail = b[p:]                                    # v8 pool type
    if len(tail) != 2:
        raise ValueError("unexpected %d bytes after the strip" % len(tail))
    return dict(pos=pos, nrm=nrm, faces=faces, face_at=face_at, nmat=nmat,
                mat_at=mat_at, nstrip=nstrip, strip_at=strip_at, tail=tail)


def find_twins(m):
    """[(drop, keep)] for every reversed twin pair."""
    pos, nrm, faces = m["pos"], m["nrm"], m["faces"]
    key = lambda f: tuple(sorted(tuple(np.round(pos[i], 4)) for i in f))
    groups = {}
    for i, f in enumerate(faces):
        groups.setdefault(key(f), []).append(i)
    out = []
    for g in groups.values():
        if len(g) == 1:
            continue
        if len(g) != 2:
            raise ValueError("a triangle appears %d times; not a twin pair" % len(g))
        a, c = g
        def normal(i):
            t = pos[faces[i]]
            n = np.cross(t[1] - t[0], t[2] - t[0])
            return n / np.linalg.norm(n)
        if normal(a) @ normal(c) > -0.99:
            raise ValueError("faces %d/%d share positions but are not reversed" % (a, c))
        ya, yc = normal(a)[1], normal(c)[1]
        if ya * yc >= 0 or min(abs(ya), abs(yc)) < 0.05:
            raise ValueError("faces %d/%d: cannot tell which faces the camera" % (a, c))
        out.append((a, c) if ya > 0 else (c, a))   # keep the copy facing local -y
    return out


def rebuild(b, m, drop):
    keep = [f for i, f in enumerate(m["faces"]) if i not in drop]
    body = struct.pack("<H", len(keep)) + b"".join(struct.pack("<3H", *f) for f in keep)
    mats = b[m["mat_at"]:m["strip_at"]]
    return b[:m["face_at"]] + body + mats + struct.pack("<H", 0) + m["tail"]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()
    bad = 0
    for rel in MESHES:
        path = os.path.join(DATA, rel.replace("\\", os.sep))
        bak = os.path.join(BACKUP, os.path.basename(path))
        if args.restore:
            if os.path.exists(bak):
                shutil.copy2(bak, path)
                print("restored", rel)
            else:
                print("no backup for", rel)
            continue
        b = open(path, "rb").read()
        m = parse(b)
        twins = find_twins(m)
        if args.verify:
            ok = not twins and m["nstrip"] == 0
            print("%s: %d twin pair(s), strip %d -> %s"
                  % (rel, len(twins), m["nstrip"], "OK" if ok else "NOT FIXED"))
            bad += not ok
            continue
        if not twins:
            print("%s: already fixed" % rel)
            continue
        if len(twins) != EXPECTED_TWINS[rel]:
            sys.exit("%s: %d twin pairs, expected %d -- the file is not the one this "
                     "script was written for" % (rel, len(twins), EXPECTED_TWINS[rel]))
        if m["nmat"]:
            sys.exit("%s has material-id face ranges; removing faces would break them" % rel)
        drop = {d for d, _ in twins}
        out = rebuild(b, m, drop)
        n = parse(out)
        assert len(n["faces"]) == len(m["faces"]) - len(drop) and n["nstrip"] == 0
        assert not find_twins(n)
        print("%s: %d faces -> %d (dropped %d back-facing twins), strip %d -> 0"
              % (rel, len(m["faces"]), len(n["faces"]), len(drop), m["nstrip"]))
        if args.dry_run:
            continue
        os.makedirs(BACKUP, exist_ok=True)
        if not os.path.exists(bak):
            shutil.copy2(path, bak)
        with open(path, "wb") as fh:
            fh.write(out)
    if args.dry_run:
        print("DRY RUN - nothing written")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
