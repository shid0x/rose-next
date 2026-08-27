"""Audit the per-object bounding boxes cached in LIST_*.ZSC against real mesh geometry.

Every ZSC object record ends with six floats -- a model-space AABB (min xyz, max
xyz) in client units (cm). **Those six floats are broken in every ZSC we have**,
ours and retail alike: whatever tool wrote them scaled only the X axis into world
units and left Y and Z in raw mesh units, so the stored box is ~100x too thin and
~100x too short. For a one-part object with no part offset the relationship is
exact:

    stored.x == mesh.x * 100      <- correct (client cm)
    stored.y == mesh.y            <- 100x too small
    stored.z == mesh.z            <- 100x too small

Verified on LIST_CNST_ODT (our Oro import) and LIST_DECO_JPT (untouched retail
Junon), so this is a retail-era exporter bug, not something an import did. Run
this script with no arguments and read the "axis scale signature" block -- it
re-derives the relationship per file rather than assuming it.

Why it mattered
---------------
`CMAP_PATCH::MakeAABBFromObject` used to feed those floats through
`TransformOBB2AABB` to decide how far an object reaches beyond its 10 m patch,
and `CQuadPatchManager` then culled whole patches -- and every object standing on
them -- against the result. Muris' canyon walls registered as a 336 m x 4.0 m x
1.1 m ribbon lying on the ground instead of a 336 m x 396 m x 114 m wall; turning
the camera pushed the ribbon out of the frustum and `RemoveFromScene()` deleted
the wall while it filled the screen. See doc/zsc-bounding-boxes.md.

The client no longer reads the field: it asks the engine for the world AABB the
renderer already derives from mesh min/max (`getVisibleWorldMinMax`). So this
script is a **verification and asset-audit tool**, not a required repair:

  * After a client change, the ranked under-coverage list predicts exactly which
    objects would pop. Nothing on it should pop any more.
  * After any `import-*.py` run that appends ZSC objects, it confirms the new
    entries' boxes are sane instead of finding out months later from a popping
    wall.
  * It reports ZSC parts whose mesh file is missing or unreadable, which is a
    different class of bug (see "Missing Assets Must Degrade" in CLAUDE.md).

`--fix` rewrites the six floats in place. It is belt-and-braces -- nothing in the
workspace reads the field today. The client uses engine bounds; the xadet map
editor parses the field into `ZSC.Object.BoundingBox` and never reads it back
(it builds its own boxes via `ObjectManager.CreateBox`), and has no ZSC save
path, so it cannot re-break a repaired file. The record length is unchanged, so
the rewrite is a pure in-place patch: parse, seek, overwrite 24 bytes.

Traps encoded here, each of which silently yields wrong numbers
--------------------------------------------------------------
* **Mesh units depend on the ZMS version.** `zz_mesh_tool::load_mesh_minmax`
  applies `ZZ_XFORM_IN` (x0.01) to the header min/max for version < 7 only. So
  v7/v8 headers are metres (cm = value * 100) and v6 headers are already cm
  (cm = value * 1). We ship 22 v6 meshes; treating them like v8 is a 100x error.
* **Part transforms chain through parents.** `CFixedPART::LoadVisible` links a
  part to its parent and sets a *relative* position, so a part's model-space
  transform is the composition up the chain. Applying only the part's own
  transform happens to work for the big cliff objects (identity parents) and is
  wrong in general.
* **Part positions are cm, mesh extents are mesh units.** Mixing them is the
  original bug; do not reproduce it.
* **A zero-part object has no effects block and no bounding box.** The record
  ends right after the part count. Reading six floats there desynchronises the
  whole file. Both the client (`CMODEL::Load`) and the editor bail the same way.

ZSC binary layout, for reference (validated: parsing every LIST_*.ZSC in data/
lands exactly on EOF):

    i16 mesh_count       ; mesh_count   * cstring
    i16 texture_count    ; each: cstring, 9 * i16, f32 alpha, i16 glow, 3 * f32
    i16 effect_count     ; effect_count * cstring
    i16 object_count     ; each:
        i32 cylinder_radius, i32 cylinder_x, i32 cylinder_y
        i16 part_count
        if part_count == 0: record ends here
        part_count * { i16 mesh_id, i16 texture_id, {u8 tag, u8 len, len bytes}* , u8 0 }
        i16 obj_effect_count
        obj_effect_count * { i16 effect_id, i16 effect_type, {u8 tag, u8 len, ...}*, u8 0 }
        f32 min_x, min_y, min_z, max_x, max_y, max_z      <- the field this audits

ZMS header, for reference:

    char[8] "ZMS000n\0" | i32 vertex_format | f32 min[3] | f32 max[3] | i16 bone_count | ...
"""
import argparse
import glob
import math
import os
import shutil
import struct
import sys

# ZSC part flag tags, from CFixedPART::Load in src/client/io_model.cpp.
TAG_END, TAG_POS, TAG_ROT, TAG_SCALE, TAG_PARENT = 0, 1, 2, 3, 7

# Mesh units -> client units (cm). zz_mesh_tool::load_mesh_minmax applies
# ZZ_XFORM_IN (0.01) below version 7, so those headers are already cm.
MESH_SCALE_BY_VERSION = {6: 1.0, 7: 100.0, 8: 100.0}

# A stored box this much larger than the geometry is corrupt, not merely loose.
# 100 m is far past any legitimate authoring slack.
OVERSIZE_REPORT_CM = 10000.0


class Reader:
    def __init__(self, data):
        self.b, self.p = data, 0

    def u8(self):
        v = self.b[self.p]
        self.p += 1
        return v

    def i16(self):
        v = struct.unpack_from("<h", self.b, self.p)[0]
        self.p += 2
        return v

    def i32(self):
        v = struct.unpack_from("<i", self.b, self.p)[0]
        self.p += 4
        return v

    def f32(self):
        v = struct.unpack_from("<f", self.b, self.p)[0]
        self.p += 4
        return v

    def vec3(self):
        return (self.f32(), self.f32(), self.f32())

    def cstr(self):
        end = self.b.index(b"\0", self.p)
        s = self.b[self.p:end]
        self.p = end + 1
        return s.decode("latin-1")


# ----------------------------------------------------------------- ZSC / ZMS

def load_zsc(path):
    """Parse a ZSC. Records each object's bbox byte offset so --fix can patch it."""
    raw = open(path, "rb").read()
    r = Reader(raw)

    meshes = [r.cstr() for _ in range(r.i16())]
    for _ in range(r.i16()):          # texture table
        r.cstr()
        for _ in range(9):
            r.i16()
        r.f32()
        r.i16()
        r.vec3()
    effects = [r.cstr() for _ in range(r.i16())]

    objects = []
    for idx in range(r.i16()):
        obj = {"idx": idx, "parts": [], "bbox": None, "bbox_offset": None}
        r.i32(), r.i32(), r.i32()     # cylinder radius / x / y
        part_count = r.i16()
        if part_count == 0:
            objects.append(obj)       # no effects block and no bbox -- record ends
            continue

        for _ in range(part_count):
            part = {"mesh": r.i16(), "texture": r.i16(), "pos": (0.0, 0.0, 0.0),
                    "rot": (0.0, 0.0, 0.0, 1.0), "scale": (1.0, 1.0, 1.0), "parent": -1}
            while True:
                tag = r.u8()
                if tag == TAG_END:
                    break
                size = r.u8()
                end = r.p + size
                if tag == TAG_POS:
                    part["pos"] = r.vec3()
                elif tag == TAG_ROT:
                    w, x, y, z = r.f32(), r.f32(), r.f32(), r.f32()
                    part["rot"] = (x, y, z, w)
                elif tag == TAG_SCALE:
                    part["scale"] = r.vec3()
                elif tag == TAG_PARENT:
                    part["parent"] = r.i16() - 1
                r.p = end
            obj["parts"].append(part)

        for _ in range(r.i16()):       # per-object effects
            r.i16(), r.i16()
            while True:
                tag = r.u8()
                if tag == TAG_END:
                    break
                # NOT `r.p += r.u8()`: augmented assignment reads r.p *before*
                # calling u8(), which itself advances r.p past the size byte, so
                # the sum silently loses that byte and every flag desyncs by one.
                size = r.u8()
                r.p += size

        obj["bbox_offset"] = r.p
        obj["bbox"] = (r.vec3(), r.vec3())
        objects.append(obj)

    if r.p != len(raw):
        raise ValueError("%s: parsed %d of %d bytes -- layout mismatch"
                         % (path, r.p, len(raw)))
    return {"path": path, "raw": raw, "meshes": meshes, "effects": effects,
            "objects": objects}


_mesh_cache = {}


def mesh_bounds_cm(data_root, rel):
    """ZMS header min/max converted to client units (cm). None if unreadable."""
    if rel in _mesh_cache:
        return _mesh_cache[rel]
    path = os.path.join(data_root, rel.replace("/", os.sep).replace("\\", os.sep))
    result = None
    try:
        # 8 magic + 4 format + 12 min + 12 max = 36 bytes minimum.
        with open(path, "rb") as fh:
            head = fh.read(64)
        magic = head[:8].split(b"\0")[0].decode("latin-1")
        version = int(magic[3:]) if magic[:3] == "ZMS" else 0
        scale = MESH_SCALE_BY_VERSION.get(version)
        if scale is not None:
            lo = struct.unpack_from("<3f", head, 12)
            hi = struct.unpack_from("<3f", head, 24)
            result = (tuple(v * scale for v in lo), tuple(v * scale for v in hi), version)
    except (OSError, ValueError, struct.error):
        result = None
    _mesh_cache[rel] = result
    return result


# ------------------------------------------------------------------- geometry

def quat_matrix(q):
    """Row-vector (D3D) rotation matrix from an (x, y, z, w) quaternion."""
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    return ((1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)),
            (2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)),
            (2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)))


def transform_point(v, scale, rot, pos):
    v = (v[0] * scale[0], v[1] * scale[1], v[2] * scale[2])
    return tuple(v[0] * rot[0][k] + v[1] * rot[1][k] + v[2] * rot[2][k] + pos[k]
                 for k in range(3))


def part_chain(parts, index):
    """Part indices from `index` up to its root, nearest-first."""
    chain, seen = [], set()
    while 0 <= index < len(parts) and index not in seen:
        seen.add(index)
        chain.append(index)
        index = parts[index]["parent"]
    return chain


def object_bounds_cm(zsc, obj, data_root):
    """The model-space AABB the ZSC bbox is supposed to hold, in cm.

    Returns (min, max, missing_meshes). min/max are None when no part resolved.
    """
    lo = [float("inf")] * 3
    hi = [float("-inf")] * 3
    missing = []
    for i, part in enumerate(obj["parts"]):
        if not (0 <= part["mesh"] < len(zsc["meshes"])):
            missing.append("<mesh index %d out of range>" % part["mesh"])
            continue
        rel = zsc["meshes"][part["mesh"]]
        bounds = mesh_bounds_cm(data_root, rel)
        if bounds is None:
            missing.append(rel)
            continue
        mesh_lo, mesh_hi, _ = bounds
        corners = [tuple(mesh_hi[k] if (c >> k) & 1 else mesh_lo[k] for k in range(3))
                   for c in range(8)]
        for node in part_chain(obj["parts"], i):
            p = obj["parts"][node]
            rot = quat_matrix(p["rot"])
            corners = [transform_point(v, p["scale"], rot, p["pos"]) for v in corners]
        for v in corners:
            for k in range(3):
                lo[k] = min(lo[k], v[k])
                hi[k] = max(hi[k], v[k])
    if lo[0] == float("inf"):
        return None, None, missing
    return tuple(lo), tuple(hi), missing


def overhang_cm(stored, computed_lo, computed_hi):
    """Worst distance the real geometry reaches outside the stored box, per axis set."""
    lo, hi = stored
    horizontal = max(lo[0] - computed_lo[0], computed_hi[0] - hi[0],
                     lo[1] - computed_lo[1], computed_hi[1] - hi[1])
    vertical = max(lo[2] - computed_lo[2], computed_hi[2] - hi[2])
    return horizontal, vertical


def oversize_cm(stored, computed_lo, computed_hi):
    """Worst distance the stored box reaches outside the real geometry.

    The mirror of overhang, and a separate class of bug: a box that is far too
    *large* never hides anything, it just pins whatever reads it. LIST_CNST_ODT
    object 7 (the Muris cart group) carries +/-10,539,319 units -- about 105 km --
    which would keep a patch resident permanently if anything still consulted it.
    """
    lo, hi = stored
    return max(computed_lo[0] - lo[0], hi[0] - computed_hi[0],
               computed_lo[1] - lo[1], hi[1] - computed_hi[1],
               computed_lo[2] - lo[2], hi[2] - computed_hi[2])


# -------------------------------------------------------------------- signature

def axis_signature(zsc, data_root):
    """Ratio of stored extent to true extent per axis, over trivially-placed objects.

    Only single-part objects at the origin with unit scale are used, so the
    comparison is unambiguous. This is what proves the "X scaled, Y/Z not"
    diagnosis for a given file instead of assuming it.
    """
    samples = {0: [], 1: [], 2: []}
    for obj in zsc["objects"]:
        if len(obj["parts"]) != 1 or obj["bbox"] is None:
            continue
        part = obj["parts"][0]
        if part["pos"] != (0.0, 0.0, 0.0) or part["scale"] != (1.0, 1.0, 1.0):
            continue
        lo, hi, missing = object_bounds_cm(zsc, obj, data_root)
        if lo is None or missing:
            continue
        for k in range(3):
            true_extent = hi[k] - lo[k]
            if true_extent > 1e-3:
                stored_extent = obj["bbox"][1][k] - obj["bbox"][0][k]
                samples[k].append(stored_extent / true_extent)
    # Median, not mean: one object with a near-degenerate extent on some axis
    # skews an average enough to make a clean 1.00 / 0.01 signature look noisy.
    def median(values):
        if not values:
            return None
        s = sorted(values)
        mid = len(s) // 2
        return s[mid] if len(s) % 2 else 0.5 * (s[mid - 1] + s[mid])

    return {k: (median(v), len(v)) for k, v in samples.items()}


# ------------------------------------------------------------------------ main

def iter_zsc_paths(data_root, patterns):
    seen, out = set(), []
    for pattern in patterns:
        for path in sorted(glob.glob(pattern, recursive=True)):
            key = os.path.normcase(os.path.abspath(path))
            if key not in seen and os.path.isfile(path):
                seen.add(key)
                out.append(path)
    return out


def write_fixed(zsc, findings):
    raw = bytearray(zsc["raw"])
    for entry in findings:
        obj, lo, hi = entry["obj"], entry["lo"], entry["hi"]
        struct.pack_into("<6f", raw, obj["bbox_offset"],
                         lo[0], lo[1], lo[2], hi[0], hi[1], hi[2])
    shutil.copyfile(zsc["path"], zsc["path"] + ".bak")
    with open(zsc["path"], "wb") as fh:
        fh.write(raw)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("paths", nargs="*",
                    help="ZSC files or globs (default: every LIST_*.ZSC under data/3DDATA)")
    ap.add_argument("--root", default=".", help="repo root (default: cwd)")
    ap.add_argument("--map-objects-only", action="store_true",
                    help="only LIST_CNST_* / LIST_DECO_* (the tables patch culling reads)")
    ap.add_argument("--threshold", type=float, default=1.0,
                    help="metres of overhang before an object is reported (default: 1.0)")
    ap.add_argument("--top", type=int, default=15, help="worst N objects per run (default: 15)")
    ap.add_argument("--fix", action="store_true",
                    help="rewrite the stored boxes in place (makes .bak); not required by the client")
    ap.add_argument("--verify", action="store_true",
                    help="exit non-zero if any object still under-covers beyond --threshold")
    ap.add_argument("--restore", action="store_true", help="restore every .bak and exit")
    args = ap.parse_args()

    data_root = os.path.join(args.root, "data")
    patterns = args.paths or [os.path.join(data_root, "3DDATA", "**", "LIST_*.ZSC")]

    if args.restore:
        restored = 0
        for path in iter_zsc_paths(data_root, [p + ".bak" for p in patterns]):
            shutil.move(path, path[:-4])
            restored += 1
        print("restored %d file(s) from .bak" % restored)
        return 0

    paths = iter_zsc_paths(data_root, patterns)
    if args.map_objects_only:
        paths = [p for p in paths
                 if os.path.basename(p).upper().startswith(("LIST_CNST", "LIST_DECO"))]
    if not paths:
        print("no ZSC files matched", file=sys.stderr)
        return 2

    threshold_cm = args.threshold * 100.0
    all_findings, oversize_report, missing_report = [], [], []
    total_objects = failed_files = 0

    print("=" * 78)
    print("axis scale signature   (stored extent / true extent, per axis, median)")
    print("  1.0 = correct.  0.01 = stored in mesh units where cm was meant.")
    print("  Only meaningful for map-object tables (LIST_CNST_* / LIST_DECO_*).")
    print("  Equipment tables (WEAPON, SUBWPN, ...) hang off character bones and")
    print("  never reach patch culling; their numbers are noise, not a finding.")
    print("=" * 78)
    print("%-28s %8s %8s %8s   %s" % ("file", "X", "Y", "Z", "samples"))

    parsed = []
    for path in paths:
        try:
            zsc = load_zsc(path)
        except (ValueError, struct.error, IndexError) as exc:
            print("  !! %s" % exc, file=sys.stderr)
            failed_files += 1
            continue
        parsed.append(zsc)
        sig = axis_signature(zsc, data_root)
        if any(sig[k][1] for k in range(3)):
            print("%-28s %8s %8s %8s   %d" % (
                os.path.basename(path),
                *("%8.4f" % sig[k][0] if sig[k][0] is not None else "     -"
                  for k in range(3)),
                max(sig[k][1] for k in range(3))))

    for zsc in parsed:
        for obj in zsc["objects"]:
            if not obj["parts"]:
                continue
            total_objects += 1
            lo, hi, missing = object_bounds_cm(zsc, obj, data_root)
            for rel in missing:
                missing_report.append((os.path.basename(zsc["path"]), obj["idx"], rel))
            if lo is None or obj["bbox"] is None:
                continue
            horizontal, vertical = overhang_cm(obj["bbox"], lo, hi)
            mesh_name = (zsc["meshes"][obj["parts"][0]["mesh"]]
                         if 0 <= obj["parts"][0]["mesh"] < len(zsc["meshes"]) else "?")
            if max(horizontal, vertical) > threshold_cm:
                all_findings.append({
                    "zsc": zsc, "obj": obj, "lo": lo, "hi": hi,
                    "horizontal": horizontal, "vertical": vertical, "mesh": mesh_name,
                })
            slack = oversize_cm(obj["bbox"], lo, hi)
            if slack > OVERSIZE_REPORT_CM:
                oversize_report.append({
                    "zsc": zsc, "obj": obj, "slack": slack, "mesh": mesh_name,
                })

    all_findings.sort(key=lambda f: -f["horizontal"])
    oversize_report.sort(key=lambda f: -f["slack"])

    print()
    print("=" * 78)
    print("objects whose real geometry reaches outside the stored box")
    print("=" * 78)
    print("%9s %9s  %-26s %5s  %s" % ("horiz(m)", "vert(m)", "file", "obj", "first mesh"))
    for f in all_findings[:args.top]:
        print("%9.1f %9.1f  %-26s %5d  %s" % (
            f["horizontal"] / 100.0, f["vertical"] / 100.0,
            os.path.basename(f["zsc"]["path"]), f["obj"]["idx"],
            os.path.basename(f["mesh"].replace("\\", "/"))))
    if len(all_findings) > args.top:
        print("   ... and %d more (raise --top to see them)" % (len(all_findings) - args.top))

    if oversize_report:
        print()
        print("=" * 78)
        print("stored boxes far LARGER than the geometry (corrupt, not merely loose)")
        print("=" * 78)
        print("%12s  %-26s %5s  %s" % ("slack(m)", "file", "obj", "first mesh"))
        for f in oversize_report[:args.top]:
            print("%12.1f  %-26s %5d  %s" % (
                f["slack"] / 100.0, os.path.basename(f["zsc"]["path"]), f["obj"]["idx"],
                os.path.basename(f["mesh"].replace("\\", "/"))))

    over20 = sum(1 for f in all_findings if f["horizontal"] > 2000.0)
    over50 = sum(1 for f in all_findings if f["horizontal"] > 5000.0)
    print()
    print("files scanned                 : %d (%d unparseable)" % (len(parsed), failed_files))
    print("objects with parts            : %d" % total_objects)
    print("  reported (> %.1f m overhang) : %d" % (args.threshold, len(all_findings)))
    print("  horizontal overhang > 20 m  : %d" % over20)
    print("  horizontal overhang > 50 m  : %d" % over50)
    print("  stored box oversized > 100 m: %d" % len(oversize_report))
    print("parts with missing mesh files : %d" % len(missing_report))
    for entry in missing_report[:args.top]:
        print("   %-26s obj %-4d %s" % entry)

    if args.fix:
        by_file = {}
        for f in all_findings:
            by_file.setdefault(id(f["zsc"]), (f["zsc"], []))[1].append(f)
        for zsc, findings in by_file.values():
            write_fixed(zsc, findings)
            check = load_zsc(zsc["path"])
            bad = 0
            for f in findings:
                stored = check["objects"][f["obj"]["idx"]]["bbox"]
                for k in range(3):
                    if abs(stored[0][k] - f["lo"][k]) > 0.05 or abs(stored[1][k] - f["hi"][k]) > 0.05:
                        bad += 1
            print("fixed %-26s %4d object(s)%s" % (
                os.path.basename(zsc["path"]), len(findings),
                "  !! %d did not verify" % bad if bad else ""))
        print()
        print("Wrote .bak alongside each modified file. Undo with --restore.")
        print("Rebake the VFS before this reaches a packed client.")

    if args.verify and all_findings:
        print()
        print("VERIFY FAILED: %d object(s) still under-cover by more than %.1f m"
              % (len(all_findings), args.threshold), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
