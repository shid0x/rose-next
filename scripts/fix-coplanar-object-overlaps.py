#!/usr/bin/env python3
"""Separate map-object placements whose faces lie in exactly the same plane.

The bug
-------
Some walls in Junon Polis (and a handful of objects elsewhere) "flash" while the
camera moves and settle the moment it stops. It is z-fighting, but not the kind
the 24-bit depth buffer fixed: the two surfaces are not *close*, they are the
*same plane*. A mapper tiles a long wall with one 20 m segment
(`portstairwall01`, DECO 161 in Junon Polis) placed every 10-19 m, so consecutive
copies overlap by 0.6-9.8 m with their faces exactly coplanar (measured
separation 0.000 cm). Two coplanar triangles with different vertices never
rasterise to bit-identical depth, so whichever wins each pixel is decided by
rounding noise that changes with every sub-pixel camera move. It settles when
the camera stops because `zz_camera_follow::interpolate_camera` approaches its
target exponentially -- the camera keeps creeping for a couple of seconds after
the mouse is released, then is bit-stable, and a stable camera gives a stable
(if arbitrary) winner.

The fight is invisible where the two copies look identical (same mesh, same
texture, tiling period matching the stride) and shows exactly where they differ:
each placement has its own lightmap cell, so a baked shadow or a brightness
step in one copy flickers against the other. That is why it reads as a
rectangle, a thin line, or a blotch on an otherwise uniform wall -- the shape
is the overlap region, not the object. Depth precision cannot help (the
separation is zero), depth bias cannot help (both surfaces are ordinary map
geometry with nothing to tell them apart), and it reproduces in every ROSE
client because the data is the same.

The fix
-------
Move one placement of each overlapping pair by a few centimetres along the
overlapping faces' normal, so the two surfaces are separated by at least
`--min-sep` (default 1 cm). At 24 bits that separation is resolved out to
~400 m, and a 1 cm ledge in a 10 m wall is under a pixel beyond 8 m. Nothing
else changes: the IFO record keeps its ordinal (the lightmap `.lit` keys objects
by 1-based position in the lump, so a duplicate placement must be *moved*, not
deleted), its rotation, its scale and its object id. Only the twelve position
bytes are rewritten, and the pre-fix bytes are kept in
`build/coplanar-overlaps/manifest.json` for `--restore`.

Placements are nodes, overlapping pairs are edges; each connected component is
levelled greedily so adjacent nodes end up at different multiples of `--step`
along a direction chosen per node to have a useful component along every one
of its overlapping planes. The result is re-checked in memory, with the same
float32 rounding the client applies (`Position + 520000` in cm, ulp 1/16 cm),
before anything is written. Same-placement overlaps (two parts of one object,
the `road01`/`road01top` family) are deliberately out of scope: they are a
ZSC/mesh authoring matter, they are already separated by a non-zero gap, and
the 24-bit buffer handles them.

Usage
-----
    python scripts/fix-coplanar-object-overlaps.py --dry-run        # report only
    python scripts/fix-coplanar-object-overlaps.py                  # apply
    python scripts/fix-coplanar-object-overlaps.py --verify         # expect none left
    python scripts/fix-coplanar-object-overlaps.py --restore        # undo from manifest
    --zone 2 --zone 22   limit to zone ids (LIST_ZONE rows)
    --only D48           fix only this object's fights (repeatable); every other
                         placement is pinned, and the zone is refused if the plan
                         would move one. Check the direction by hand: the planner
                         separates faces but cannot tell a wall's room side, and
                         the wrong way hides the object behind the wall.
    --min-area CM2       ignore overlaps smaller than this (default 400 = 20x20 cm)
    --min-sep CM         planes closer than this count as fighting (default 1.0)
    --step CM            nudge unit (default 0.5)

Idempotent: a second run finds no pair under `--min-sep` and changes nothing.
Re-run after any `import-*.py` that brings in map .IFO files.

Record of the first run (2026-09-22)
------------------------------------
60 zone rows scanned, 612 fighting pairs in 25 map folders, 594 records moved,
none by more than 7.5 cm (the Golden Colosseum's 12,000 m2 floor slabs); 40
by 0.5 cm, 50 by 1, 323 by 1.5, 138 by 2, 43 by more. Nearly all of it is in
six maps: Union War 122, Golden Colosseum 110, Junon Polis 99, Sikuku
Underground Prison 73, Junon Cartel/Crusader Training Camp 43 each (the two
folders are byte-identical copies), Forgotten Temple B1 35. Field maps are
clean. One exact duplicate: Junon Polis DECO 153 written into both 36_31 and
36_32, sunk 100 m. Not yet validated in game.

Two things the first attempts got wrong, kept here so they are not re-learnt:
back-to-back faces (one copy's top on another's bottom) are not a fight
because materials default to ZZ_CULLMODE_CW, and dropping the facing sign
doubled Union War's count and pushed a stone wall 6 cm into the ground to dodge
a phantom; and pairs already 1-13 cm apart must be carried as constraints with
signed per-plane separations, or moving a neighbour makes them coplanar --
plus a 0.2 cm planning margin, without which rounded normals and float32
positions left 0.86-0.92 cm gaps under a 1 cm target.
"""
import argparse
import collections
import glob
import importlib.util
import json
import math
import os
import struct
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA = os.path.join(ROOT, "data")
BUILD = os.path.join(ROOT, "build", "coplanar-overlaps")
MANIFEST = os.path.join(BUILD, "manifest.json")
ZONE_STB = os.path.join(DATA, "3DDATA", "STB", "LIST_ZONE.STB")

# IFO positions are relative to the zone centre (map 32,32); the client adds
# 32 * 16000 + 8000 cm on x and y (io_terrain.cpp, ReadObjINFO) in float32.
WORLD_OFF = 32 * 16000 + 8000
LUMP_OBJECT, LUMP_CNST = 1, 3
MAX_LEVEL = 12


def _load(name):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"), os.path.join(HERE, name))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


azb = _load("audit-zsc-bounds.py")   # load_zsc, quat_matrix, transform_point, part_chain
rdr = _load("rose-data-reader.py")   # Stb


def f32(x):
    return struct.unpack("<f", struct.pack("<f", x))[0]


# ------------------------------------------------------------------ readers

class Reader:
    def __init__(self, b):
        self.b, self.p = b, 0

    def u16(self):
        v = struct.unpack_from("<H", self.b, self.p)[0]; self.p += 2; return v

    def u32(self):
        v = struct.unpack_from("<I", self.b, self.p)[0]; self.p += 4; return v

    def f(self, n):
        v = struct.unpack_from("<%df" % n, self.b, self.p); self.p += 4 * n; return v

    def cstr(self):
        e = self.b.index(b"\0", self.p); s = self.b[self.p:e]; self.p = e + 1
        return s.decode("latin-1")


_mesh_cache = {}


def load_mesh(rel):
    """(verts_cm, faces) or None. v7/v8 store metres, v6 stores cm (zz_mesh_tool)."""
    key = rel.lower()
    if key in _mesh_cache:
        return _mesh_cache[key]
    path = os.path.join(DATA, rel.replace("\\", os.sep).replace("/", os.sep))
    out = None
    try:
        b = open(path, "rb").read()
        r = Reader(b)
        magic = r.cstr()
        ver = int(magic[3:])
        fmt = r.u32()
        r.f(6)                                    # min / max
        if ver in (7, 8):
            nb = r.u16(); r.p += 2 * nb
            nv = r.u16()
            verts = [tuple(c * 100.0 for c in r.f(3)) for _ in range(nv)]
            if fmt & 4: r.p += 12 * nv            # normal
            if fmt & 8: r.p += 16 * nv            # colour
            if (fmt & 16) and (fmt & 32): r.p += 24 * nv   # weights + indices
            if fmt & 64: r.p += 12 * nv           # tangent
            for bit in (128, 256, 512, 1024):     # uv0..3
                if fmt & bit: r.p += 8 * nv
            nf = r.u16()
            faces = [(r.u16(), r.u16(), r.u16()) for _ in range(nf)]
        elif ver == 6:
            nb = r.u32(); r.p += 8 * nb
            nv = r.u32()
            verts = []
            for _ in range(nv):
                r.u32(); verts.append(r.f(3))
            if fmt & 4: r.p += 16 * nv
            if fmt & 8: r.p += 20 * nv
            if (fmt & 16) and (fmt & 32): r.p += 36 * nv
            if fmt & 64: r.p += 16 * nv
            for bit in (128, 256, 512, 1024):
                if fmt & bit: r.p += 12 * nv
            nf = r.u32()
            faces = []
            for _ in range(nf):
                r.u32(); faces.append(tuple(r.u32() for _ in range(3)))
        else:
            raise ValueError("unknown mesh version %d" % ver)
        out = (verts, faces)
    except (OSError, ValueError, struct.error) as e:
        print("  mesh %s: %s" % (rel, e))
    _mesh_cache[key] = out
    return out


def read_ifo_objects(path):
    """Every DECO/CNST placement with the byte offset of its position field."""
    b = open(path, "rb").read()
    r = Reader(b)
    n = r.u32()
    lumps = [(r.u32(), r.u32()) for _ in range(n)]
    objs = []
    for t, off in lumps:
        if t not in (LUMP_OBJECT, LUMP_CNST):
            continue
        r.p = off
        cnt = r.u32()
        for k in range(cnt):
            ln = b[r.p]; r.p += 1 + ln
            oid = struct.unpack_from("<i", b, r.p + 8)[0]
            r.p += 20                             # warp, event, type, id, mapx, mapy
            rot = r.f(4)
            pos_off = r.p
            pos = r.f(3)
            scl = r.f(3)
            objs.append(dict(path=path, file=os.path.basename(path), lump=t, ordinal=k, id=oid,
                             rot=rot, scale=scl, pos_off=pos_off, raw_pos=pos))
    return objs


def world_pos(raw):
    return (f32(f32(raw[0]) + WORLD_OFF), f32(f32(raw[1]) + WORLD_OFF), f32(raw[2]))


def zone_list():
    s = rdr.Stb(ZONE_STB, "cp949")
    out = []
    for r in range(s.rows):
        zon = s.s(r, 1)
        if not zon:
            continue
        folder = os.path.join(DATA, os.path.dirname(zon.replace("\\", "/")).replace("/", os.sep))
        if not os.path.isdir(folder):
            continue
        deco = s.s(r, 11).replace("\\\\", "\\")
        cnst = s.s(r, 12).replace("\\\\", "\\")
        out.append(dict(id=r, name=s.s(r, 0), folder=folder, deco=deco, cnst=cnst))
    return out


_zsc_cache = {}


def load_zsc(rel):
    if not rel.lower().endswith(".zsc"):
        return None                               # placeholder rows ("string")
    p = os.path.join(DATA, rel.replace("\\", os.sep).replace("/", os.sep))
    key = p.lower()
    if key not in _zsc_cache:
        try:
            _zsc_cache[key] = azb.load_zsc(p)
        except (OSError, ValueError) as e:
            print("  zsc %s: %s" % (rel, e))
            _zsc_cache[key] = None
    return _zsc_cache[key]


def ifo_files(folder):
    seen, out = set(), []
    for f in glob.glob(os.path.join(folder, "*.IFO")) + glob.glob(os.path.join(folder, "*.ifo")):
        k = os.path.normcase(os.path.abspath(f))
        if k not in seen:
            seen.add(k); out.append(f)
    return sorted(out)


# ----------------------------------------------------------------- geometry

def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


def norm(a):
    l = math.sqrt(dot(a, a))
    return (a[0] / l, a[1] / l, a[2] / l) if l > 0 else None


def placement_triangles(zsc, obj, pos):
    """World-space triangles of one placement at world position `pos` (cm)."""
    rec = zsc["objects"][obj["id"]]
    orot = azb.quat_matrix(obj["rot"])
    tris = []
    for pi, part in enumerate(rec["parts"]):
        if not (0 <= part["mesh"] < len(zsc["meshes"])):
            continue
        m = load_mesh(zsc["meshes"][part["mesh"]])
        if not m:
            continue
        verts, faces = m
        pts = list(verts)
        for node in azb.part_chain(rec["parts"], pi):
            p = rec["parts"][node]
            rot = azb.quat_matrix(p["rot"])
            pts = [azb.transform_point(v, p["scale"], rot, p["pos"]) for v in pts]
        pts = [azb.transform_point(v, obj["scale"], orot, pos) for v in pts]
        for f in faces:
            if max(f) < len(pts):
                tris.append((pts[f[0]], pts[f[1]], pts[f[2]]))
    return tris


def tri_plane(t):
    """Facing normal and plane offset. The sign is kept: the engine culls back
    faces (ZZ_CULLMODE_CW), so a face lying on another placement's *back* face
    is never drawn against it and is neither a fight nor a constraint."""
    n = norm(cross(sub(t[1], t[0]), sub(t[2], t[0])))
    if n is None:
        return None
    return n, dot(n, t[0])


def project(t, n):
    a = (1.0, 0.0, 0.0) if abs(n[0]) < 0.9 else (0.0, 1.0, 0.0)
    u = norm(cross(n, a)); v = cross(n, u)
    return [(dot(p, u), dot(p, v)) for p in t]


def clip_poly(subject, clip):
    def inside(p, a, b):
        return (b[0] - a[0]) * (p[1] - a[1]) - (b[1] - a[1]) * (p[0] - a[0]) >= -1e-9

    def inter(p, q, a, b):
        x1, y1 = p; x2, y2 = q; x3, y3 = a; x4, y4 = b
        den = (x1 - x2) * (y3 - y4) - (y1 - y2) * (x3 - x4)
        if abs(den) < 1e-12:
            return p
        tt = ((x1 - x3) * (y3 - y4) - (y1 - y3) * (x3 - x4)) / den
        return (x1 + tt * (x2 - x1), y1 + tt * (y2 - y1))

    area = 0.0
    for i in range(len(clip)):
        a, b = clip[i], clip[(i + 1) % len(clip)]
        area += a[0] * b[1] - b[0] * a[1]
    if area < 0:
        clip = clip[::-1]
    out = subject
    for i in range(len(clip)):
        a, b = clip[i], clip[(i + 1) % len(clip)]
        inp, out = out, []
        if not inp:
            break
        s = inp[-1]
        for e in inp:
            if inside(e, a, b):
                if not inside(s, a, b):
                    out.append(inter(s, e, a, b))
                out.append(e)
            elif inside(s, a, b):
                out.append(inter(s, e, a, b))
            s = e
    return out


def poly_area(p):
    a = 0.0
    for i in range(len(p)):
        x, y = p[i]; x2, y2 = p[(i + 1) % len(p)]
        a += x * y2 - x2 * y
    return abs(a) / 2.0


def aabb(t):
    return ([min(p[k] for p in t) for k in range(3)], [max(p[k] for p in t) for k in range(3)])


# ----------------------------------------------------------------- detector

def find_overlaps(placements, sep_limit, min_area):
    """Overlapping parallel faces of different placements closer than `sep_limit`.

    {(i, j): {"area": cm2, "sep": min |s|, "planes": {normal: {signed s}}}} with
    i < j and s = d_i - d_j along the canonical normal. Pairs under the caller's
    fighting threshold are the defects; the rest are constraints the plan must
    not violate when it moves a neighbour.
    """
    buckets = collections.defaultdict(list)
    for idx, pl in enumerate(placements):
        for t in pl["tris"]:
            plane = tri_plane(t)
            if not plane:
                continue
            n, d = plane
            lo, hi = aabb(t)
            nk = (round(n[0] * 200), round(n[1] * 200), round(n[2] * 200))
            buckets[(nk, int(math.floor(d / sep_limit)))].append((idx, t, n, d, lo, hi))
    pairs = {}

    def consider(a, b):
        i, ti, ni, di, lo_i, hi_i = a
        j, tj, nj, dj, lo_j, hi_j = b
        if i == j or dot(ni, nj) < 0.99995:
            return
        if abs(di - dj) >= sep_limit:
            return
        if any(lo_i[k] > hi_j[k] + sep_limit or lo_j[k] > hi_i[k] + sep_limit for k in range(3)):
            return
        ar = poly_area(clip_poly(project(ti, ni), project(tj, ni)))
        if ar < 1.0:
            return
        if i > j:
            i, j, di, dj = j, i, dj, di
        key = (i, j)
        e = pairs.setdefault(key, {"area": 0.0, "sep": sep_limit, "planes": collections.defaultdict(set)})
        e["area"] += ar
        e["sep"] = min(e["sep"], abs(di - dj))
        e["planes"][tuple(round(v, 4) for v in ni)].add(round(di - dj, 2))

    for (nk, dk), lst in buckets.items():
        nxt = buckets.get((nk, dk + 1), ())
        for i in range(len(lst)):
            for j in range(i + 1, len(lst)):
                consider(lst[i], lst[j])
            for b in nxt:
                consider(lst[i], b)
    return {k: v for k, v in pairs.items() if v["area"] >= min_area}


def fighting(pairs, min_sep):
    return {k: v for k, v in pairs.items() if v["sep"] < min_sep}


def load_zone(zone):
    tabs = {LUMP_OBJECT: load_zsc(zone["deco"]) if zone["deco"] else None,
            LUMP_CNST: load_zsc(zone["cnst"]) if zone["cnst"] else None}
    placements = []
    for f in ifo_files(zone["folder"]):
        try:
            objs = read_ifo_objects(f)
        except (OSError, ValueError, struct.error) as e:
            print("  ifo %s: %s" % (f, e))
            continue
        for o in objs:
            tab = tabs[o["lump"]]
            if not tab or not (0 <= o["id"] < len(tab["objects"])):
                continue
            o["zsc"] = tab
            o["orig_raw"] = o["raw_pos"]
            o["pos"] = world_pos(o["raw_pos"])
            o["tris"] = placement_triangles(tab, o, o["pos"])
            placements.append(o)
    return placements


def label(o):
    return "%s%d %s#%d" % ("D" if o["lump"] == LUMP_OBJECT else "C", o["id"], o["file"], o["ordinal"])


# ------------------------------------------------------------------- solver

# Plan against a slightly larger gap than the one we test for: the planes'
# normals are rounded, and the client rounds positions to float32 (an ulp is
# 1/16 cm at world scale), so a nominal 1.00 cm can land at 0.9.
PLAN_MARGIN = 0.2


def plan_nudges(placements, pairs, min_sep, step, only=()):
    """{idx: (direction, level)} so that every fighting pair ends >= min_sep apart
    and no constraint pair is pushed under it. With `only`, every other
    placement is pinned: levelled first, at zero, so the named ones move."""
    adj = collections.defaultdict(dict)           # v -> u -> planes
    for (i, j), e in pairs.items():
        adj[i][j] = e["planes"]
        adj[j][i] = e["planes"]
    fight = fighting(pairs, min_sep)
    nodes = set()
    for i, j in fight:
        nodes.add(i); nodes.add(j)
    target = min_sep + PLAN_MARGIN

    def shift(v, u, dv, lv, du, lu, n):
        """Change of the signed separation s = d_i - d_j for pair (min, max)."""
        mv = lv * step * dot(dv, n)
        mu = lu * step * dot(du, n)
        return (mv - mu) if v < u else (mu - mv)

    plan = {}                                     # processed fighting nodes
    order = sorted(nodes, key=lambda i: (selected(placements[i], only), -len(adj[i])))
    for v in order:
        if not selected(placements[v], only):
            plan[v] = ((0.0, 0.0, 0.0), 0)
            continue
        normals, others = set(), set()
        for u in adj[v]:
            if (min(u, v), max(u, v)) in fight:
                normals |= set(adj[v][u].keys())
            else:
                others |= set(adj[v][u].keys())
        # Candidates: the fighting normals, the constraint normals, pairwise
        # bisectors of all of them, their sum, and every one of those negated.
        # The search below keeps the smallest level over all of them, so a
        # direction that dodges a nearby constraint plane wins over the naive
        # normal when the naive one has to climb past it.
        base = set(normals) | others
        nl = sorted(base)
        cands = set(base)
        for a in range(len(nl)):
            for b in range(a + 1, len(nl)):
                for sgn in (1.0, -1.0):
                    bis = norm(tuple(nl[a][k] + sgn * nl[b][k] for k in range(3)))
                    if bis:
                        cands.add(tuple(round(c, 4) for c in bis))
        s = norm((sum(n[0] for n in normals), sum(n[1] for n in normals), sum(n[2] for n in normals)))
        if s:
            cands.add(tuple(round(c, 4) for c in s))
        cands |= {(-d[0], -d[1], -d[2]) for d in cands}
        cands = sorted(cands, key=lambda d: -min(abs(dot(d, n)) for n in normals))
        # Neighbours that constrain v now: levelled fighting nodes, and every
        # non-fighting neighbour (they never move). Unlevelled fighting
        # neighbours separate themselves from v on their own turn.
        fixed = []
        for u, planes in adj[v].items():
            if u in plan:
                fixed.append((u, planes, plan[u]))
            elif u not in nodes:
                fixed.append((u, planes, ((0.0, 0.0, 0.0), 0)))
        best = None
        for d in cands:
            for level in range(0, MAX_LEVEL + 1):
                ok = True
                for u, planes, (du, lu) in fixed:
                    for n, seps in planes.items():
                        delta = shift(v, u, d, level, du, lu, n)
                        if any(abs(s0 + delta) < target for s0 in seps):
                            ok = False; break
                    if not ok:
                        break
                if ok:
                    if best is None or level < best[1]:
                        best = (d, level)
                    break
        if best is None:
            best = (cands[0], MAX_LEVEL)
        plan[v] = best
    return {v: (d, l) for v, (d, l) in plan.items() if l > 0}


def apply_plan(placements, plan, step):
    """Move the planned placements in memory (float32-rounded like the client)."""
    for v, (d, level) in plan.items():
        o = placements[v]
        raw = o["raw_pos"]
        o["raw_pos"] = tuple(f32(raw[k] + d[k] * level * step) for k in range(3))
        o["pos"] = world_pos(o["raw_pos"])
        o["tris"] = placement_triangles(o["zsc"], o, o["pos"])


def sink_duplicates(placements, sink, verbose=True):
    """An exact duplicate (same object, rotation, scale and position, usually the
    same building written into two neighbouring chunk files) is coplanar on
    every face, so no nudge direction can separate it. Its ordinal must stay,
    so the later record is sunk `sink` cm below the map instead of deleted."""
    groups = collections.defaultdict(list)
    for idx, o in enumerate(placements):
        key = (o["lump"], o["id"], tuple(round(v, 4) for v in o["rot"]),
               tuple(round(v, 4) for v in o["scale"]), tuple(round(v, 1) for v in o["raw_pos"]))
        groups[key].append(idx)
    sunk = 0
    for key, idxs in groups.items():
        for v in idxs[1:]:
            o = placements[v]
            if o["raw_pos"] != o["orig_raw"]:
                continue                          # already handled on a previous run
            raw = o["raw_pos"]
            o["raw_pos"] = (raw[0], raw[1], f32(raw[2] - sink))
            o["pos"] = world_pos(o["raw_pos"])
            o["tris"] = placement_triangles(o["zsc"], o, o["pos"])
            sunk += 1
            if verbose:
                print("      sink %-28s duplicate of %s, %.0f m down" % (
                    label(o), label(placements[idxs[0]]), sink / 100.0))
    return sunk


def selected(o, only):
    """--only: a placement is in scope if its object label (D48, C12) is named."""
    return not only or label(o).split(" ")[0] in only


def scoped(placements, pairs, min_sep, only):
    """Drop the fighting pairs --only leaves alone. Nearby non-fighting pairs
    stay: they are the constraints any move must still respect."""
    if not only:
        return pairs
    return {k: v for k, v in pairs.items()
            if v["sep"] >= min_sep
            or selected(placements[k[0]], only) or selected(placements[k[1]], only)}


def solve_zone(placements, args, verbose=True):
    """Iterate detect -> plan -> move until nothing fights. Returns leftover pairs."""
    sep_limit = args.min_sep + MAX_LEVEL * args.step * 2
    left = {}
    if not args.only:
        sink_duplicates(placements, args.sink, verbose)
    for rnd in range(4):
        pairs = scoped(placements, find_overlaps(placements, sep_limit, args.min_area),
                       args.min_sep, args.only)
        left = fighting(pairs, args.min_sep)
        if not left:
            break
        plan = plan_nudges(placements, pairs, args.min_sep, args.step, args.only)
        if not plan:
            break
        if verbose:
            for v, (d, level) in sorted(plan.items()):
                print("      move %-28s %4.1f cm along (%.2f, %.2f, %.2f)%s" % (
                    label(placements[v]), level * args.step, d[0], d[1], d[2],
                    "" if rnd == 0 else "  (round %d)" % (rnd + 1)))
        apply_plan(placements, plan, args.step)
    else:
        pairs = scoped(placements, find_overlaps(placements, sep_limit, args.min_area),
                       args.min_sep, args.only)
        left = fighting(pairs, args.min_sep)
    return left


# --------------------------------------------------------------------- main

def report_pairs(placements, pairs, limit=20):
    rows = sorted(pairs.items(), key=lambda kv: -kv[1]["area"])
    for (i, j), e in rows[:limit]:
        a, b = placements[i], placements[j]
        print("      %-28s x %-28s %8.2f m2  sep %.3f cm  n=%s" % (
            label(a), label(b), e["area"] / 1e4, e["sep"], sorted(e["planes"])[0]))
    if len(rows) > limit:
        print("      ... %d more" % (len(rows) - limit))


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--zone", type=int, action="append", help="LIST_ZONE row id (repeatable)")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--min-area", type=float, default=400.0, help="cm2")
    ap.add_argument("--min-sep", type=float, default=1.0, help="cm")
    ap.add_argument("--step", type=float, default=0.5, help="cm")
    ap.add_argument("--sink", type=float, default=10000.0, help="cm to sink an exact duplicate record")
    ap.add_argument("--only", action="append", default=[],
                    help="object label to fix, e.g. D48 (repeatable): other fights are left "
                         "alone, and a zone is refused if the plan would move anything else")
    args = ap.parse_args()

    manifest = {"entries": []}
    if os.path.exists(MANIFEST):
        manifest = json.load(open(MANIFEST))

    if args.restore:
        return restore(manifest)

    zones = zone_list()
    if args.zone:
        zones = [z for z in zones if z["id"] in set(args.zone)]
    seen_folders = set()                          # several zone rows share one map (5/6, 4/10)
    zones = [z for z in zones if not (os.path.normcase(z["folder"]) in seen_folders
                                      or seen_folders.add(os.path.normcase(z["folder"])))]
    total_pairs, total_moved, unresolved, biggest = 0, 0, 0, 0.0
    new_entries = []
    for zone in zones:
        t0 = time.time()
        placements = load_zone(zone)
        if not placements:
            continue
        pairs = fighting(scoped(placements, find_overlaps(placements, args.min_sep, args.min_area),
                                args.min_sep, args.only), args.min_sep)
        print("zone %3d %-30s placements=%5d fighting-pairs=%4d  [%.0fs]" % (
            zone["id"], zone["name"][:30], len(placements), len(pairs), time.time() - t0))
        if not pairs:
            continue
        total_pairs += len(pairs)
        report_pairs(placements, pairs)
        if args.verify:
            continue
        left = solve_zone(placements, args)
        moved = [o for o in placements if o["raw_pos"] != o["orig_raw"]]
        stray = [o for o in moved if not selected(o, args.only)]
        if stray:
            unresolved += 1
            print("   !! --only: the plan would also move %s -- zone left untouched"
                  % ", ".join(label(o) for o in stray))
            continue
        for o in moved:
            dist = math.sqrt(sum((o["raw_pos"][k] - o["orig_raw"][k]) ** 2 for k in range(3)))
            if dist >= args.sink * 0.99:
                continue                          # a sunk duplicate, not a nudge
            biggest = max(biggest, dist)
            if dist > 4.0:
                print("      big move %-28s %.1f cm" % (label(o), dist))
        if left:
            unresolved += len(left)
            print("   !! %d pair(s) still fight after %d move(s) -- zone left untouched:" % (len(left), len(moved)))
            report_pairs(placements, left)
            continue
        total_moved += len(moved)
        if args.dry_run:
            continue
        by_file = collections.defaultdict(list)
        for o in moved:
            by_file[o["path"]].append(o)
        for path, items in by_file.items():
            buf = bytearray(open(path, "rb").read())
            for o in items:
                old = bytes(buf[o["pos_off"]:o["pos_off"] + 12])
                assert struct.unpack("<3f", old) == tuple(o["orig_raw"]), "IFO changed under us: %s" % path
                new = struct.pack("<3f", *o["raw_pos"])
                buf[o["pos_off"]:o["pos_off"] + 12] = new
                new_entries.append(dict(zone=zone["id"], path=os.path.relpath(path, ROOT), lump=o["lump"],
                                        ordinal=o["ordinal"], id=o["id"], pos_off=o["pos_off"],
                                        old=old.hex(), new=new.hex()))
            open(path, "wb").write(buf)
    if args.verify:
        print("VERIFY: %d fighting pair(s) under %.2f cm" % (total_pairs, args.min_sep))
        bad = 0
        for e in manifest["entries"]:
            p = os.path.join(ROOT, e["path"])
            cur = open(p, "rb").read()[e["pos_off"]:e["pos_off"] + 12].hex()
            if cur != e["new"]:
                bad += 1
                print("   manifest entry not in place: %s #%d (%s)" % (e["path"], e["ordinal"], cur))
        print("VERIFY: %d manifest entries, %d not in place" % (len(manifest["entries"]), bad))
        return 1 if (total_pairs or bad) else 0
    print("fighting pairs=%d placements moved=%d largest move=%.2f cm unresolved=%d%s" % (
        total_pairs, total_moved, biggest, unresolved, "  (dry run, nothing written)" if args.dry_run else ""))
    if new_entries:
        os.makedirs(BUILD, exist_ok=True)
        # a record moved again by a later run keeps its original bytes
        known = {(e["path"], e["pos_off"]): e for e in manifest["entries"]}
        for e in new_entries:
            k = (e["path"], e["pos_off"])
            if k in known:
                known[k]["new"] = e["new"]
            else:
                known[k] = e
        manifest["entries"] = list(known.values())
        json.dump(manifest, open(MANIFEST, "w"), indent=1)
        print("manifest: %s (%d entries)" % (MANIFEST, len(manifest["entries"])))
    return 1 if unresolved else 0


def restore(manifest):
    n, skipped = 0, 0
    by_file = collections.defaultdict(list)
    for e in manifest["entries"]:
        by_file[e["path"]].append(e)
    for rel, items in by_file.items():
        path = os.path.join(ROOT, rel)
        buf = bytearray(open(path, "rb").read())
        for e in items:
            cur = bytes(buf[e["pos_off"]:e["pos_off"] + 12]).hex()
            if cur != e["new"]:
                skipped += 1
                print("   skip %s #%d: bytes are %s, expected %s" % (rel, e["ordinal"], cur, e["new"]))
                continue
            buf[e["pos_off"]:e["pos_off"] + 12] = bytes.fromhex(e["old"])
            n += 1
        open(path, "wb").write(buf)
    if not skipped:
        os.remove(MANIFEST)
    print("restored %d record(s), skipped %d" % (n, skipped))
    return 0


if __name__ == "__main__":
    sys.exit(main())
