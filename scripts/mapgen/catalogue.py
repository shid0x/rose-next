"""Decoration catalogue: what each object is, how big, and how retail places it (phase 5).

Sources:

* `LIST_DECO_<type>.ZSC` (`LIST_ZONE` col 11): parts, meshes, transforms and
  per-part collision (property 29 = SWITCH_COLLISION, io_model.cpp:51-52;
  0 = none, 4 = polygon, 12 = polygon + not moveable).
* The editor's `LIST_TERRAIN_OBJECT_<type>.STB`: row = object id; col 0 =
  name (UTF-8 Korean); col 1 = source path, whose folder is the category
  (TREE, STONE, GRASS, VILLAGE, ETC, SPECIAL).
* ZMS headers for the real size. The ZSC's stored boxes are wrong
  (scripts/audit-zsc-bounds.py). v7/v8 headers are in metres, v6 in cm.
* Retail placements in IFO lump 1 of the given zones: uses, scale, sink
  below the ground, slope band and the brush underneath, per object.

Everything is plain numbers in a JSON (stats/jg_decoration.json), so
placement never re-reads the corpus.
"""

import json
import math
import os
import re
import struct

import numpy as np

from . import chunk, ifo, terrain, tiles
from .paint import SLOPE_BANDS, corner_slopes

TAG_POS, TAG_ROT, TAG_SCALE, TAG_PARENT, TAG_COLLISION = 1, 2, 3, 7, 29
MESH_SCALE = {6: 1.0, 7: 100.0, 8: 100.0}            # header units -> cm


def read_zsc(path):
    """Objects of a map ZSC: [{"parts": [{mesh, pos, rot(x,y,z,w), scale, parent, collision}]}], meshes."""
    raw = open(path, "rb").read()
    p = [0]

    def take(fmt):
        v = struct.unpack_from(fmt, raw, p[0])
        p[0] += struct.calcsize(fmt)
        return v if len(v) > 1 else v[0]

    def cstr():
        e = raw.index(b"\0", p[0])
        s = raw[p[0]:e].decode("latin-1")
        p[0] = e + 1
        return s

    meshes = [cstr() for _ in range(take("<h"))]
    for _ in range(take("<h")):                       # materials
        cstr()
        p[0] += 9 * 2 + 4 + 2 + 12
    for _ in range(take("<h")):                       # effects
        cstr()
    objects = []
    for _ in range(take("<h")):
        take("<3i")
        parts = []
        n = take("<h")
        if n:
            for _ in range(n):
                part = {"mesh": take("<h"), "pos": (0.0, 0.0, 0.0), "rot": (0.0, 0.0, 0.0, 1.0),
                        "scale": (1.0, 1.0, 1.0), "parent": -1, "collision": 0}
                take("<h")                            # material
                while True:
                    tag = take("<B")
                    if tag == 0:
                        break
                    size = take("<B")
                    end = p[0] + size
                    if tag == TAG_POS:
                        part["pos"] = take("<3f")
                    elif tag == TAG_ROT:
                        w, x, y, z = take("<4f")
                        part["rot"] = (x, y, z, w)
                    elif tag == TAG_SCALE:
                        part["scale"] = take("<3f")
                    elif tag == TAG_PARENT:
                        part["parent"] = take("<h") - 1
                    elif tag == TAG_COLLISION:
                        part["collision"] = take("<h")
                    p[0] = end
                parts.append(part)
            for _ in range(take("<h")):               # per-object effects
                take("<2h")
                while True:
                    tag = take("<B")
                    if tag == 0:
                        break
                    # Not `p[0] += take("<B")`: that reads p[0] BEFORE take()
                    # advances it, losing the size byte (same trap as in
                    # audit-zsc-bounds.py).
                    size = take("<B")
                    p[0] += size
            p[0] += 24                                # stored bbox (wrong; unused)
        objects.append({"parts": parts})
    if p[0] != len(raw):
        raise ValueError("%s: parsed %d of %d bytes" % (path, p[0], len(raw)))
    return meshes, objects


def _quat(q):
    x, y, z, w = q
    n = math.sqrt(x * x + y * y + z * z + w * w) or 1.0
    x, y, z, w = x / n, y / n, z / n, w / n
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y + z * w), 2 * (x * z - y * w)],
                     [2 * (x * y - z * w), 1 - 2 * (x * x + z * z), 2 * (y * z + x * w)],
                     [2 * (x * z + y * w), 2 * (y * z - x * w), 1 - 2 * (x * x + y * y)]])


def mesh_box(data_dir, rel):
    try:
        with open(os.path.join(data_dir, rel.replace("\\", os.sep)), "rb") as f:
            head = f.read(64)
    except OSError:
        return None
    magic = head[:8].split(b"\0")[0].decode("latin-1")
    scale = MESH_SCALE.get(int(magic[3:]) if magic[:3] == "ZMS" and magic[3:].isdigit() else 0)
    if scale is None:
        return None
    return np.array(struct.unpack_from("<3f", head, 12)) * scale, np.array(struct.unpack_from("<3f", head, 24)) * scale


def mesh_vertices(data_dir, rel):
    """Vertex positions in cm, or None. Layout as in fix-coplanar-object-overlaps.py
    load_mesh (v7/v8 in metres, v6 in cm), positions only."""
    try:
        with open(os.path.join(data_dir, rel.replace("\\", os.sep)), "rb") as f:
            b = f.read()
    except OSError:
        return None
    e = b.index(b"\0")
    magic = b[:e].decode("latin-1")
    ver = int(magic[3:]) if magic[:3] == "ZMS" and magic[3:].isdigit() else 0
    p = e + 1 + 4 + 24                                  # format u32, min/max 6 floats
    if ver in (7, 8):
        nb = struct.unpack_from("<H", b, p)[0]
        p += 2 + 2 * nb
        nv = struct.unpack_from("<H", b, p)[0]
        p += 2
        return np.frombuffer(b, "<f4", nv * 3, p).reshape(nv, 3).astype(float) * 100.0
    if ver == 6:
        nb = struct.unpack_from("<I", b, p)[0]
        p += 4 + 8 * nb
        nv = struct.unpack_from("<I", b, p)[0]
        p += 4
        rec = np.frombuffer(b, "<f4", nv * 4, p).reshape(nv, 4)   # u32 index + xyz
        return rec[:, 1:].astype(float)
    return None


def _to_model(parts, i, pts):
    j, seen = i, set()
    while 0 <= j < len(parts) and j not in seen:
        seen.add(j)
        q = parts[j]
        pts = (pts * np.array(q["scale"])) @ _quat(q["rot"]) + np.array(q["pos"])
        j = q["parent"]
    return pts


def collision_profile(data_dir, meshes, obj, step_cm=10.0):
    """[[z_cm, r_cm], ...]: for each 10 cm slice of model height, the furthest
    horizontal reach from the object's origin of its COLLIDING geometry.

    The client collides with spheres around the character's feet and body
    (cobjchar_collision.cpp:510-530). Only colliding geometry near the ground
    blocks walking, so a tree's canopy (one colliding part with the trunk in
    some meshes) must not count. The bounding box made a big tree block a
    10 m circle.
    """
    slices = {}
    for i, part in enumerate(obj["parts"]):
        if not part["collision"] or not 0 <= part["mesh"] < len(meshes):
            continue
        v = mesh_vertices(data_dir, meshes[part["mesh"]])
        if v is None or not len(v):
            continue
        v = _to_model(obj["parts"], i, v)
        r = np.hypot(v[:, 0], v[:, 1])
        for zb, rr in zip(np.floor(v[:, 2] / step_cm).astype(int), r):
            if rr > slices.get(zb, -1.0):
                slices[zb] = rr
    return [[int(z * step_cm), round(float(r), 1)] for z, r in sorted(slices.items())]


def blocking_radius(profile, scale, sink_cm, band_cm=(0.0, 250.0)):
    """Horizontal blocking radius (cm) of a placed object: its colliding geometry
    between the ground and `band_cm[1]` above it, after scale and sink."""
    r = 0.0
    for z, rr in profile:
        h = sink_cm + scale * (z + 5.0)          # slice centre, height above the ground
        if band_cm[0] <= h <= band_cm[1]:
            r = max(r, rr * scale)
    return r


def object_box(data_dir, meshes, obj, colliding_only=False):
    """Model-space AABB (cm) of an object's parts, through the parent chain.
    Returns (lo, hi, missing meshes)."""
    lo, hi, missing = np.full(3, np.inf), np.full(3, -np.inf), []
    parts = obj["parts"]
    for i, part in enumerate(parts):
        if colliding_only and not part["collision"]:
            continue
        box = mesh_box(data_dir, meshes[part["mesh"]]) if 0 <= part["mesh"] < len(meshes) else None
        if box is None:
            missing.append(meshes[part["mesh"]] if 0 <= part["mesh"] < len(meshes) else "#%d" % part["mesh"])
            continue
        corners = np.array([[box[(c >> k) & 1][k] for k in range(3)] for c in range(8)])
        j, seen = i, set()
        while 0 <= j < len(parts) and j not in seen:
            seen.add(j)
            q = parts[j]
            corners = (corners * np.array(q["scale"])) @ _quat(q["rot"]) + np.array(q["pos"])
            j = q["parent"]
        lo, hi = np.minimum(lo, corners.min(0)), np.maximum(hi, corners.max(0))
    if not np.isfinite(lo[0]):
        return None, None, missing
    return lo, hi, missing


def build(data_dir, zone_type_name, zones_dirs, ts):
    """The catalogue dict for one zone type, e.g. ('JG', JG01..JG08 dirs)."""
    meshes, objects = read_zsc(os.path.join(data_dir, "3DDATA", "JUNON", "LIST_DECO_%s.ZSC" % zone_type_name))
    names = tiles.read_stb_cells(os.path.join(data_dir, "3DDATA", "STB",
                                              "LIST_TERRAIN_OBJECT_%s.STB" % zone_type_name), "latin-1")
    nb = len(SLOPE_BANDS) - 1
    entries = {}
    for oid, obj in enumerate(objects):
        src = names[oid][1].strip() if oid < len(names) else ""
        parts = src.upper().split("\\")
        try:
            nm = names[oid][0].encode("latin-1").decode("utf-8")
        except (UnicodeDecodeError, IndexError):
            nm = ""
        lo, hi, missing = object_box(data_dir, meshes, obj)
        clo, chi, _ = object_box(data_dir, meshes, obj, colliding_only=True)
        e = {"id": oid, "name": nm, "category": parts[2] if len(parts) > 3 else "",
             "source": src, "parts": len(obj["parts"]), "missing_meshes": missing,
             "collision": sorted({p["collision"] for p in obj["parts"]}),
             "uses": 0, "scale": [], "sink_cm": [],
             "slope_bands": [0] * nb, "brushes": [0] * ts.brushes}
        if lo is not None:
            e["size_cm"] = [round(float(v), 1) for v in hi - lo]
            # horizontal reach from the object origin, the radius any yaw can sweep
            e["radius_cm"] = round(float(max(np.hypot(lo[0], lo[1]), np.hypot(lo[0], hi[1]),
                                             np.hypot(hi[0], lo[1]), np.hypot(hi[0], hi[1]))), 1)
        if clo is not None:
            e["collision_profile"] = collision_profile(data_dir, meshes, obj)
        entries[oid] = e

    land_corners = np.zeros((nb, ts.brushes), int)
    for zdir in zones_dirs:
        grid, have, (x0, y0) = tiles.load_zone_tiles(zdir)
        lattice, _ = tiles.lattice_votes(grid, ts, have)
        field = np.full((grid.shape[0] * 4 + 1, grid.shape[1] * 4 + 1), np.nan)
        for f in os.listdir(zdir):
            m = re.match(r"(\d+)_(\d+)\.him$", f, re.I)
            if m:
                x, y = int(m.group(1)), 64 - int(m.group(2))
                with open(os.path.join(zdir, f), "rb") as fh:
                    field[(y - y0) * 64:(y - y0) * 64 + 65, (x - x0) * 64:(x - x0) * 64 + 65] = \
                        chunk.parse_him(fh.read()).heights[::-1]
        with np.errstate(invalid="ignore"):
            slopes = corner_slopes(field)
        bands = np.clip(np.digitize(slopes, SLOPE_BANDS) - 1, 0, nb - 1)
        ok = (lattice >= 0) & ~np.isnan(slopes)
        np.add.at(land_corners, (bands[ok], lattice[ok]), 1)
        gx, gy = terrain.gradient(field)
        for f in os.listdir(zdir):
            if not f.lower().endswith(".ifo"):
                continue
            with open(os.path.join(zdir, f), "rb") as fh:
                recs = ifo.parse(fh.read()).lump(ifo.OBJECT) or []
            for r in recs:
                e = entries.get(r.obj_id)
                if e is None:
                    continue
                vx = (r.pos[0] + 520000) / terrain.GRID_CM - x0 * 64
                vy = (r.pos[1] + 520000) / terrain.GRID_CM - y0 * 64
                ix, iy = int(vx), int(vy)
                if not (0 <= iy < field.shape[0] - 1 and 0 <= ix < field.shape[1] - 1) or np.isnan(field[iy, ix]):
                    continue
                fx, fy = vx - ix, vy - iy
                h = (field[iy, ix] * (1 - fx) * (1 - fy) + field[iy, ix + 1] * fx * (1 - fy)
                     + field[iy + 1, ix] * (1 - fx) * fy + field[iy + 1, ix + 1] * fx * fy)
                if np.isnan(h):
                    continue
                cr, cc = int(round(vy / 4)), int(round(vx / 4))
                e["uses"] += 1
                e["scale"].append(round(float(r.scale[0]), 3))
                e["sink_cm"].append(round(float(r.pos[2] - h), 1))
                s = math.degrees(math.atan(math.hypot(gx[iy, ix], gy[iy, ix])))
                e["slope_bands"][min(int(np.digitize(s, SLOPE_BANDS)) - 1, nb - 1)] += 1
                if lattice[cr, cc] >= 0:
                    e["brushes"][int(lattice[cr, cc])] += 1
    for e in entries.values():                        # keep quantiles, not every sample
        for k in ("scale", "sink_cm"):
            v = np.array(e[k]) if e[k] else None
            e[k] = [round(float(q), 3) for q in np.percentile(v, [10, 25, 50, 75, 90])] if v is not None else []
    return {"_comment": "Decoration catalogue; regenerate with `mapgen-zone.py stats`. "
                        "scale / sink_cm are the 10/25/50/75/90th percentiles of retail placements; "
                        "slope_bands / brushes count them; land_corners counts the zones' tile corners "
                        "per (slope band, brush), so uses / corners is a density.",
            "zone_type": zone_type_name, "zones": [os.path.basename(z) for z in zones_dirs],
            "slope_bands": SLOPE_BANDS, "brushes": ts.brush_names,
            "land_corners": land_corners.tolist(),
            "objects": [entries[k] for k in sorted(entries)]}


def save(path, cat):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        json.dump(cat, f, indent=1, ensure_ascii=False)


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)
