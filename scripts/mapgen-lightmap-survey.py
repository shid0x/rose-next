#!/usr/bin/env python3
"""Survey of retail Junon lightmaps (map generator phase 8, step 1: lighting).

Read-only. Re-derives every number in docs/mapgen/FORMATS.md "Lightmaps"
and PLAN.md phase 8 from the files in data/:

    python scripts/mapgen-lightmap-survey.py zones  [JG01 ...]   # per zone: format, colours, sun fit, seams
    python scripts/mapgen-lightmap-survey.py trees  [JG01 ...]   # averaged tree/rock shadow profiles
    python scripts/mapgen-lightmap-survey.py objects [JG01 ...]  # object lightmaps: coverage, cells, atlases
    python scripts/mapgen-lightmap-survey.py chunk JG02 1 2      # one chunk next to a sun-only model (PNG)
    python scripts/mapgen-lightmap-survey.py compare JG02 0 1 3 [key=value ...]
        # bake a 3x3-chunk window of a retail zone with mapgen/lighting.py
        # (its own terrain, objects, water) and score it against the real
        # lightmap: luminance correlation and the averaged tree / rock
        # shadow profiles. This is how lighting.DEFAULT was chosen.

How the client uses the plane lightmap (settled from code, see FORMATS.md):
terrain colour = tiles x lightmap x 2 (MODULATE2X, LIST_SKY col 5 = 5 for
every sky), times the zone light's diffuse colour; no vertex lighting on
terrain at all. Each chunk's image covers exactly that chunk, north at the
top row, west at the left column, clamped at the edges.

Coordinates here: "mosaic" = all chunks' lightmaps side by side at 512 px
per chunk (256 px maps are upsampled), row 0 = south like the heightfield;
vertex i of a chunk sits on the texel edge 8i.
"""

import argparse
import importlib.util
import json
import os
import re
import struct
import sys
from collections import Counter

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
_spec = importlib.util.spec_from_file_location("mapgen_zone", os.path.join(HERE, "mapgen-zone.py"))
mz = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mz)
from mapgen import catalogue, ifo, lighting, lit, prefab, terrain  # noqa: E402

PX = 512
OUT = os.path.join(mz.BUILD, "lightmap-survey")


# ----------------------------------------------------------------- loading

def zone_dir(row):
    zstb = mz.oro.Stb(mz.P(mz.ZONE_STB))
    return os.path.dirname(mz.P(zstb.get(row, mz.COL_ZON).decode("latin-1")))


def load_zone(row):
    """(zone dir, heightfield (row 0 = south), SW chunk slot, lightmap mosaic
    (H, W, 3) float, chunks-with-a-lightmap mask, {size: count}, [(lump, record)])."""
    zdir = zone_dir(row)
    field, (x0, y0) = prefab._zone_field(zdir)
    nyc, nxc = (field.shape[0] - 1) // 64, (field.shape[1] - 1) // 64
    mosaic = np.full((nyc * PX, nxc * PX, 3), np.nan, np.float32)
    have = np.zeros((nyc, nxc), bool)
    sizes = Counter()
    for f in os.listdir(zdir):
        m = re.match(r"(\d+)_(\d+)$", f)
        if not m or not os.path.isdir(os.path.join(zdir, f)):
            continue
        x, y = int(m.group(1)), 64 - int(m.group(2))
        lm = [g for g in os.listdir(os.path.join(zdir, f)) if g.lower().endswith("_planelightingmap.dds")]
        if not lm:
            continue
        im = Image.open(os.path.join(zdir, f, lm[0]))
        sizes[im.size] += 1
        rgb = im.convert("RGB")
        if im.size != (PX, PX):
            rgb = rgb.resize((PX, PX), Image.BILINEAR)
        a = np.asarray(rgb).astype(np.float32)[::-1]          # image row 0 = north
        cy, cx = y - y0, x - x0
        if 0 <= cy < nyc and 0 <= cx < nxc:
            mosaic[cy * PX:(cy + 1) * PX, cx * PX:(cx + 1) * PX] = a
            have[cy, cx] = True
    objs = []
    for fn in os.listdir(zdir):
        if fn.lower().endswith(".ifo"):
            with open(os.path.join(zdir, fn), "rb") as fh:
                m = ifo.parse(fh.read())
            for lump in (ifo.OBJECT, ifo.CNST):
                objs += [(lump, r) for r in (m.lump(lump) or [])]
    return zdir, field, (x0, y0), mosaic, have, sizes, objs


def lum(rgb):
    return rgb[..., 0] * 0.299 + rgb[..., 1] * 0.587 + rgb[..., 2] * 0.114


def at_vertices(mosaic):
    """Mean of the 8x8 texels centred on each vertex."""
    H, W = mosaic.shape[:2]
    pad = np.pad(mosaic, ((4, 4), (4, 4), (0, 0)), constant_values=np.nan)
    blocks = pad[:(H // 8 + 1) * 8, :(W // 8 + 1) * 8].reshape(H // 8 + 1, 8, W // 8 + 1, 8, 3)
    with np.errstate(all="ignore"):
        return np.nanmean(blocks, axis=(1, 3))


def normals(field):
    gy, gx = np.gradient(field, terrain.GRID_CM)
    n = np.stack([-gx, -gy, np.ones_like(field)], -1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def sun_vec(az, el):
    """Unit vector toward the light: az = compass bearing it comes FROM
    (clockwise from north), el = degrees above the horizon."""
    a, e = np.radians(az), np.radians(el)
    return np.array([np.sin(a) * np.cos(e), np.cos(a) * np.cos(e), np.sin(e)])


def cast_shadow(field, az, el, max_m=150.0, step_m=1.25):
    """Vertices the terrain hides from the light (bilinear heightfield ray march)."""
    g = terrain.GRID_CM / 100.0
    h = np.nan_to_num(field, nan=-1e9)
    sx, sy = np.sin(np.radians(az)), np.cos(np.radians(az))
    t = np.tan(np.radians(el))
    rows, cols = h.shape
    yy, xx = np.mgrid[0:rows, 0:cols].astype(np.float32)
    shade = np.zeros(h.shape, bool)
    d = step_m
    while d <= max_m:
        r, c = yy + sy * d / g, xx + sx * d / g
        ok = (r >= 0) & (r <= rows - 1) & (c >= 0) & (c <= cols - 1)
        r0 = np.clip(np.floor(r).astype(int), 0, rows - 2)
        c0 = np.clip(np.floor(c).astype(int), 0, cols - 2)
        fr, fc = r - r0, c - c0
        hh = (h[r0, c0] * (1 - fr) * (1 - fc) + h[r0 + 1, c0] * fr * (1 - fc)
              + h[r0, c0 + 1] * (1 - fr) * fc + h[r0 + 1, c0 + 1] * fr * fc)
        shade |= ok & (hh > h + d * 100.0 * t + 20.0)
        d += step_m
    return shade


def object_mask(field, origin, objs, radius_v=4):
    x0, y0 = origin
    near = np.zeros(field.shape, bool)
    for _, r in objs:
        c = int(round((r.pos[0] + 520000) / terrain.GRID_CM)) - x0 * 64
        rr = int(round((r.pos[1] + 520000) / terrain.GRID_CM)) - y0 * 64
        near[max(0, rr - radius_v):rr + radius_v + 1, max(0, c - radius_v):c + radius_v + 1] = True
    return near


def zones_arg(names):
    return names or list(mz.PROFILE_ZONES)


# ----------------------------------------------------------------- commands

def cmd_zones(names):
    """Per zone: DDS format, open-ground and water colour, terrain-shadow ratio,
    chunk seams, and the sun fit Lum = a + b * n.l on dry open ground."""
    for name in zones_arg(names):
        zdir, field, origin, mosaic, have, sizes, objs = load_zone(mz.PROFILE_ZONES[name])
        fmts = Counter()
        for f in os.listdir(zdir):
            d = os.path.join(zdir, f)
            for g in (os.listdir(d) if os.path.isdir(d) else []):
                if g.lower().endswith("_planelightingmap.dds"):
                    b = open(os.path.join(d, g), "rb").read(128)
                    four = b[84:88].decode("latin-1") if struct.unpack("<I", b[80:84])[0] & 4 else "RGB"
                    fmts["%s %dpx %d mips" % (four, struct.unpack("<I", b[16:20])[0], struct.unpack("<I", b[28:32])[0])] += 1
        V = at_vertices(mosaic)
        L = lum(V)
        ok = ~np.isnan(L) & ~np.isnan(field)
        water = ok & (V[..., 2] > V[..., 0] + 25)
        near = object_mask(field, origin, objs)
        n = normals(field)
        # sun fit, coarse then fine, outside a rough shadow and away from water and objects
        rough = cast_shadow(field, 63, 30)
        base = ok & ~water & ~near & ~rough
        best = None
        for el in range(14, 52, 2):
            for az in range(30, 100, 2):
                dl = n[base] @ sun_vec(az, el)
                m = dl > 0.02
                A = np.vstack([np.ones(m.sum()), dl[m]]).T
                y = L[base][m]
                coef, rss, *_ = np.linalg.lstsq(A, y, rcond=None)
                r2 = 1 - rss[0] / ((y - y.mean()) ** 2).sum()
                if best is None or r2 > best[0]:
                    best = (r2, az, el, coef[0], coef[1])
        r2, az, el, a, b = best
        sh = cast_shadow(field, az, el)
        flat = ok & ~water & ~near & ~sh & (n[..., 2] > 0.97)
        open_rgb = np.median(V[flat], axis=0)
        csh = ok & ~water & ~near & sh & (n[..., 2] > 0.9)
        ratio = np.median(L[csh]) / lum(open_rgb) if csh.sum() > 50 else float("nan")
        seams = []
        for cy in range(have.shape[0]):
            for cx in range(have.shape[1] - 1):
                if have[cy, cx] and have[cy, cx + 1]:
                    e1 = lum(mosaic[cy * PX:(cy + 1) * PX, (cx + 1) * PX - 1])
                    e2 = lum(mosaic[cy * PX:(cy + 1) * PX, (cx + 1) * PX])
                    i1 = lum(mosaic[cy * PX:(cy + 1) * PX, (cx + 1) * PX - 2])
                    seams.append((np.abs(e1 - e2).mean(), np.abs(e1 - i1).mean()))
        seams = np.array(seams) if seams else np.zeros((1, 2))
        print("%s  %s" % (name, dict(fmts)))
        print("   open flat ground rgb %s, lum %.0f (x2 = %.2f in game); water rgb %s"
              % (np.round(open_rgb).astype(int), lum(open_rgb), lum(open_rgb) / 128.0,
                 np.round(np.median(V[water], axis=0)).astype(int) if water.any() else "-"))
        print("   sun from %d deg, %d deg up: lum = %.0f + %.0f * n.l (R2 %.2f); terrain shadow / open ground %.2f"
              % (az, el, a, b, r2, ratio))
        print("   chunk seams: |edge - neighbour's edge| %.1f vs |edge - next texel| %.1f" % tuple(seams.mean(0)))


def cmd_trees(names):
    """The lightmap averaged around every tree / rock (normalised by the ring
    15-20 m out), sampled along four bearings."""
    with open(os.path.join(mz.STATS_DIR, "jg_object_kinds.json"), encoding="utf-8") as f:
        by_id = {i: k for k, ids in json.load(f)["kinds"].items() for i in ids}
    with open(os.path.join(mz.STATS_DIR, "jg_decoration.json"), encoding="utf-8") as f:
        cat_of = {o["id"]: o["category"] for o in json.load(f)["objects"]}
    R = 64
    ppm = PX / 160.0
    os.makedirs(OUT, exist_ok=True)
    for name in zones_arg(names):
        zdir, field, (x0, y0), mosaic, have, sizes, objs = load_zone(mz.PROFILE_ZONES[name])
        L = lum(mosaic)
        yy, xx = np.mgrid[-R:R + 1, -R:R + 1]
        ring = np.hypot(yy, xx) > 0.75 * R
        stacks = {}
        for lump, r in objs:
            kind = by_id.get(r.obj_id, cat_of.get(r.obj_id)) if lump == ifo.OBJECT else None
            if kind not in ("TREE", "STONE"):
                continue
            ix = int(round(((r.pos[0] + 520000) / 100.0 - x0 * 160) * ppm))
            iy = int(round(((r.pos[1] + 520000) / 100.0 - y0 * 160) * ppm))
            if R <= ix < L.shape[1] - R and R <= iy < L.shape[0] - R:
                win = L[iy - R:iy + R + 1, ix - R:ix + R + 1]
                if not np.isnan(win).any() and np.median(win[ring]) >= 60:
                    stacks.setdefault(kind, []).append(win / np.median(win[ring]))
        for kind, ws in sorted(stacks.items()):
            m = np.mean(ws, 0)
            dark = np.clip(1.0 - m, 0, None) * (np.hypot(yy, xx) < 0.7 * R)
            cy, cx = (dark * yy).sum() / dark.sum(), (dark * xx).sum() / dark.sum()
            shadow_bearing = (np.degrees(np.arctan2(cx, cy)) + 360) % 360
            print("%s %s: %d objects; darkest %.2f; shadow centroid bearing %.0f deg (light from %.0f deg)"
                  % (name, kind, len(ws), m.min(), shadow_bearing, (shadow_bearing + 180) % 360))
            for bearing in (243, 153, 333, 63):
                b = np.radians(bearing)
                vals = []
                for dm in (1, 2, 3, 4, 6, 8, 10, 12, 15, 18):
                    x, y = int(round(R + np.sin(b) * dm * ppm)), int(round(R + np.cos(b) * dm * ppm))
                    vals.append(m[y - 1:y + 2, x - 1:x + 2].mean())
                print("   toward %3d deg at 1/2/3/4/6/8/10/12/15/18 m: %s" % (bearing, " ".join("%.2f" % v for v in vals)))
            Image.fromarray((np.clip(m / 1.2, 0, 1) * 255).astype(np.uint8)[::-1]).resize((256, 256), Image.NEAREST) \
                .save(os.path.join(OUT, "%s-%s.png" % (name, kind.lower())))
    print("averaged pictures (north up) in %s" % OUT)


def cmd_objects(names):
    """Object lightmaps: share of decorations / buildings with an entry, cell
    sizes, atlas sizes, and whether the lit parts' meshes have a 2nd UV set."""
    zstb = mz.oro.Stb(mz.P(mz.ZONE_STB))
    rel = lambda row, col: zstb.get(row, col).decode("latin-1").replace("\\\\", "\\")
    for name in zones_arg(names):
        row = mz.PROFILE_ZONES[name]
        zdir = zone_dir(row)
        zscs = {ifo.OBJECT: catalogue.read_zsc(os.path.join(mz.DATA, rel(row, mz.COL_DECO))),
                ifo.CNST: catalogue.read_zsc(os.path.join(mz.DATA, rel(row, mz.COL_CNST)))}
        n_obj, n_lit = Counter(), Counter()
        cells, atlas_sizes, uv1 = Counter(), Counter(), Counter()
        for f in sorted(os.listdir(zdir)):
            d = os.path.join(zdir, f)
            ifo_path = os.path.join(zdir, f + ".IFO")
            if not (os.path.isdir(d) and os.path.exists(ifo_path)):
                continue
            with open(ifo_path, "rb") as fh:
                m = ifo.parse(fh.read())
            for lump, fn in ((ifo.OBJECT, "ObjectLightMapData.lit"), (ifo.CNST, "BuildingLightMapData.lit")):
                recs = m.lump(lump) or []
                n_obj[lump] += len(recs)
                p = os.path.join(d, "LightMap", fn)
                if not os.path.exists(p):
                    continue
                with open(p, "rb") as fh:
                    L = lit.parse_lit(fh.read())
                meshes, zobjs = zscs[lump]
                for o in L.objects:
                    n_lit[lump] += 1
                    if not 1 <= o.obj_index <= len(recs):
                        continue
                    oid = recs[o.obj_index - 1].obj_id
                    for part in o.parts:
                        cells["%dpx x%d" % (part.pixels_per_part, part.parts_per_width)] += 1
                        if 0 <= oid < len(zobjs) and 0 <= part.part_index < len(zobjs[oid]["parts"]):
                            mi = zobjs[oid]["parts"][part.part_index]["mesh"]
                            try:
                                with open(os.path.join(mz.DATA, meshes[mi].replace("\\", os.sep)), "rb") as fh:
                                    head = fh.read(64)
                                fmt = struct.unpack_from("<I", head, head.index(b"\0") + 1)[0]
                                uv1["has UV1" if fmt & 256 else "no UV1"] += 1
                            except (OSError, IndexError, ValueError):
                                uv1["unreadable"] += 1
            lmdir = os.path.join(d, "LightMap")
            for g in (os.listdir(lmdir) if os.path.isdir(lmdir) else []):
                if g.lower().endswith(".dds"):
                    atlas_sizes["%dpx" % Image.open(os.path.join(lmdir, g)).size[0]] += 1
        print("%s: decorations %d (%d with a lightmap), buildings %d (%d)" %
              (name, n_obj[ifo.OBJECT], n_lit[ifo.OBJECT], n_obj[ifo.CNST], n_lit[ifo.CNST]))
        print("   cells %s; atlases %s; lit parts' meshes %s" % (cells.most_common(5), dict(atlas_sizes), dict(uv1)))


def cmd_chunk(name, cx, cy, az=63.0, el=30.0):
    """One chunk (offsets from the zone's SW chunk) beside a sun-only model."""
    zdir, field, origin, mosaic, have, sizes, objs = load_zone(mz.PROFILE_ZONES[name])
    lm = mosaic[cy * PX:(cy + 1) * PX, cx * PX:(cx + 1) * PX]
    sl = (slice(cy * 64, cy * 64 + 65), slice(cx * 64, cx * 64 + 65))
    model = 128 + 45 * np.clip(normals(field)[sl] @ sun_vec(az, el), 0, None)
    model = np.where(cast_shadow(field, az, el)[sl], 112, model)
    a = Image.fromarray(np.clip(np.nan_to_num(lm), 0, 255).astype(np.uint8)[::-1])
    b = Image.fromarray(np.clip(model, 0, 255).astype(np.uint8)[::-1]).resize((PX, PX), Image.BILINEAR).convert("RGB")
    sheet = Image.new("RGB", (PX * 2 + 8, PX), (30, 30, 30))
    sheet.paste(a, (0, 0))
    sheet.paste(b, (PX + 8, 0))
    os.makedirs(OUT, exist_ok=True)
    out = os.path.join(OUT, "%s-chunk-%d-%d.png" % (name, cx, cy))
    sheet.save(out)
    print(out)


def retail_light_inputs(row):
    """A retail zone as lighting.bake inputs: heightfield, placed objects
    (field-local cm, as the generator stores them), shapes, water."""
    zdir, field, (x0, y0), mosaic, have, sizes, objs = load_zone(row)
    zstb = mz.oro.Stb(mz.P(mz.ZONE_STB))
    rel = lambda col: zstb.get(row, col).decode("latin-1").replace("\\\\", "\\")
    shapes = lighting.Shapes(mz.DATA, {"OBJECT": catalogue.read_zsc(os.path.join(mz.DATA, rel(mz.COL_DECO))),
                                       "CNST": catalogue.read_zsc(os.path.join(mz.DATA, rel(mz.COL_CNST)))})
    f = np.nan_to_num(field, nan=float(np.nanmin(field)))
    with open(os.path.join(mz.STATS_DIR, "jg_object_kinds.json"), encoding="utf-8") as fh:
        by_id = {i: k for k, ids in json.load(fh)["kinds"].items() for i in ids}
    with open(os.path.join(mz.STATS_DIR, "jg_decoration.json"), encoding="utf-8") as fh:
        cat_of = {o["id"]: o["category"] for o in json.load(fh)["objects"]}
    placed = []
    for lump, r in objs:
        x, y = r.pos[0] + 520000 - x0 * 16000.0, r.pos[1] + 520000 - y0 * 16000.0
        if not (0 <= y / 250.0 < f.shape[0] - 1 and 0 <= x / 250.0 < f.shape[1] - 1):
            continue
        g = float(lighting.ground_cm(f, np.array([x]), np.array([y]))[0])
        kind = by_id.get(r.obj_id, cat_of.get(r.obj_id)) if lump == ifo.OBJECT else "CNST"
        placed.append({"lump": "OBJECT" if lump == ifo.OBJECT else "CNST", "id": r.obj_id, "x": x, "y": y,
                       "z": r.pos[2], "rot": tuple(r.rot), "scale": tuple(r.scale), "sink": r.pos[2] - g,
                       "kind": kind, "foliage": kind in ("GRASS", "FLOWER", "PLANT", "MUSHROOM")})
    water = []
    for fn in os.listdir(zdir):
        if fn.lower().endswith(".ifo"):
            with open(os.path.join(zdir, fn), "rb") as fh:
                o = ifo.parse(fh.read()).lump(ifo.OCEAN)
            for sx, sz, sy, ex, ez, ey in (o.rects if o is not None else []):
                c0 = int(round((min(sx, ex) + 520000) / 250.0)) - x0 * 64
                c1 = int(round((max(sx, ex) + 520000) / 250.0)) - x0 * 64
                r0 = int(round((min(sy, ey) + 520000) / 250.0)) - y0 * 64
                r1 = int(round((max(sy, ey) + 520000) / 250.0)) - y0 * 64
                mask = np.zeros(f.shape, bool)
                mask[max(0, r0):r1 + 1, max(0, c0):c1 + 1] = True
                water.append((mask & (f < sz), sz))
    return f, placed, shapes, water, mosaic


def shadow_profiles(L, placed, kind):
    """Lightmap averaged round every object of a kind, normalised by the ring
    15-20 m out, sampled toward 243 deg (the shadow) and 153 deg (the side)."""
    R, ppm = 64, PX / 160.0
    yy, xx = np.mgrid[-R:R + 1, -R:R + 1]
    ring = np.hypot(yy, xx) > 0.75 * R
    ws = []
    for q in placed:
        if q.get("kind") != kind:
            continue
        ix, iy = int(round(q["x"] / lighting.TEXEL_CM)), int(round(q["y"] / lighting.TEXEL_CM))
        if R <= ix < L.shape[1] - R and R <= iy < L.shape[0] - R:
            win = L[iy - R:iy + R + 1, ix - R:ix + R + 1]
            if not np.isnan(win).any() and np.median(win[ring]) >= 60:
                ws.append(win / np.median(win[ring]))
    if not ws:
        return None
    m = np.mean(ws, 0)
    out = {}
    for bearing in (243, 153):
        b = np.radians(bearing)
        out[bearing] = [float(m[int(round(R + np.cos(b) * d * ppm)) - 1:int(round(R + np.cos(b) * d * ppm)) + 2,
                               int(round(R + np.sin(b) * d * ppm)) - 1:int(round(R + np.sin(b) * d * ppm)) + 2].mean())
                        for d in (1, 3, 6, 10, 15)]
    return out, len(ws)


def cmd_compare(name, cx0, cy0, n, settings):
    f, placed, shapes, water, mosaic = retail_light_inputs(mz.PROFILE_ZONES[name])
    v0r, v0c = cy0 * 64, cx0 * 64
    fc = f[v0r:v0r + n * 64 + 1, v0c:v0c + n * 64 + 1]
    pc = [dict(q, x=q["x"] - v0c * 250.0, y=q["y"] - v0r * 250.0) for q in placed
          if -3000 <= q["x"] - v0c * 250.0 <= n * 16000 + 3000 and -3000 <= q["y"] - v0r * 250.0 <= n * 16000 + 3000]
    wc = [(m[v0r:v0r + n * 64 + 1, v0c:v0c + n * 64 + 1], lvl) for m, lvl in water]
    wc = [(m, lvl) for m, lvl in wc if m.any()]
    ret = mosaic[cy0 * PX:(cy0 + n) * PX, cx0 * PX:(cx0 + n) * PX]
    cfg = {}
    for kv in settings:
        k, v = kv.split("=")
        cfg[k] = float(v)
    lm = lighting.bake(fc, pc, shapes, wc, cfg)
    Lr, Lb = lum(ret), lum(lm)
    H, W = Lb.shape
    A = Lr.reshape(H // 4, 4, W // 4, 4).mean(axis=(1, 3))
    B = Lb.reshape(H // 4, 4, W // 4, 4).mean(axis=(1, 3))
    blue = ret[..., 2] > ret[..., 0] + 25
    wet = blue.reshape(H // 4, 4, W // 4, 4).mean(axis=(1, 3)) > 0.5
    dry = ~np.isnan(A) & ~wet
    print("%s window (%d, %d) %dx%d, settings %s" % (name, cx0, cy0, n, n, cfg or "defaults"))
    print("   luminance on dry ground: correlation %.3f; median retail %.0f / baked %.0f; 10th percentile %.0f / %.0f"
          % (np.corrcoef(A[dry], B[dry])[0, 1], np.median(A[dry]), np.median(B[dry]),
             np.percentile(A[dry], 10), np.percentile(B[dry], 10)))
    for kind in ("TREE", "STONE"):
        a, b = shadow_profiles(Lr, pc, kind), shadow_profiles(Lb, pc, kind)
        if a and b:
            fmt = lambda v: " ".join("%.2f" % x for x in v)
            print("   %s (%d): toward 243 deg at 1/3/6/10/15 m retail %s | baked %s" % (kind, a[1], fmt(a[0][243]), fmt(b[0][243])))
            print("   %s      side (153 deg)                retail %s | baked %s" % (" " * len(kind), fmt(a[0][153]), fmt(b[0][153])))
    os.makedirs(OUT, exist_ok=True)
    out = os.path.join(OUT, "%s-compare.png" % name)
    sheet = Image.new("RGB", (W * 2 + 8, H), (30, 30, 30))
    sheet.paste(Image.fromarray(np.clip(np.nan_to_num(ret), 0, 255).astype(np.uint8)[::-1]), (0, 0))
    sheet.paste(Image.fromarray(np.clip(lm, 0, 255).astype(np.uint8)[::-1]), (W + 8, 0))
    sheet.save(out)
    print("   retail | baked: %s" % out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    for c in ("zones", "trees", "objects"):
        sub.add_parser(c).add_argument("names", nargs="*")
    cp = sub.add_parser("compare")
    cp.add_argument("name")
    cp.add_argument("cx", type=int)
    cp.add_argument("cy", type=int)
    cp.add_argument("n", type=int)
    cp.add_argument("settings", nargs="*")
    ch = sub.add_parser("chunk")
    ch.add_argument("name")
    ch.add_argument("cx", type=int)
    ch.add_argument("cy", type=int)
    a = ap.parse_args()
    if a.cmd == "zones":
        cmd_zones(a.names)
    elif a.cmd == "trees":
        cmd_trees(a.names)
    elif a.cmd == "objects":
        cmd_objects(a.names)
    elif a.cmd == "compare":
        cmd_compare(a.name, a.cx, a.cy, a.n, a.settings)
    else:
        cmd_chunk(a.name, a.cx, a.cy)
    return 0


if __name__ == "__main__":
    sys.exit(main())
