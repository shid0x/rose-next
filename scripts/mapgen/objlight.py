"""Object lightmaps (map generator phase 8, step 3).

How the client uses them (verified 2026-10-03, docs/mapgen/FORMATS.md
"Lightmaps"):

* A part listed in x_y/LightMap/ObjectLightMapData.lit (IFO lump 1) or
  BuildingLightMapData.lit (lump 3) switches to shader_lightmap_nolit and
  loses ALL vertex lighting: final = texture x zone light diffuse x
  lightmap x 2. The lightmap must carry every bit of shading; 128 = the
  texture as drawn. Unlisted parts keep simple_lit (n.l + a 0.86 ambient).
* The cell: col = pos % ppw, row = pos // ppw of a square atlas whose
  side is ppp * ppw; the vertex shader samples (uv1 + (col, row)) / ppw from
  the mesh's SECOND uv set, unflipped (v = 0 is the top row). pixels_per_part,
  tga_name and lightmap_index are ignored by the client (written as retail
  does).
* obj_index is the 1-based ordinal of the record in that chunk's IFO lump;
  part_index the ZSC part (the client's GetPartIndex is the identity when
  the root is part 0, true of every JG object). NOTHING is bounds-checked:
  an entry past the lump, for a missing lump, or for a part past the
  object's part count crashes or corrupts the client.
* The material cache is keyed by (cell, atlas): two parts sharing a cell
  draw with the first one's texture. Every part gets its own cell.
* Atlases: DXT5, exactly 3 mip levels (setMipmapLevel(3); a fuller chain
  bleeds between cells at display quality 3/4), each island's colour
  spread to its cell's edges (cells have no gutter; the uv1 inset is only
  0.02 of a cell).
* Only meshes laid out position | normal | uv0 | uv1 (ZMS format 390, or
  386 with the normal forced) read uv1 correctly under the lightmap
  shader; an object with any other part stays unlisted.

Shading (consistent with the terrain bake, mapgen/lighting.py): per texel
at its 3D point, ambient x sky + sun x max(0, n.l) x visibility (light());
n is the face normal from the winding (retail meshes store broken vertex
normals); foliage (alpha-cut parts) uses a constant n.l, as retail's
leaves are nearly flat; visibility from occlusion maps of the terrain,
solid parts and leaves along the sun and 13 sky directions.
"""

import math
import os
import struct
import time

import numpy as np
from PIL import Image

from . import areas, catalogue, lighting, lit

LIT_FORMATS = (390, 386)         # pos|normal|uv0|uv1, pos|uv0|uv1
ORIGIN_CM = 520000.0             # IFO positions are zone-centre-relative
CHUNK_CM = 16000.0

OBJECT_DEFAULT = {
    # objects are drawn at gain x the terrain model's ambient; the sun term
    # x object_sun on top. Fitted on retail JG02 (mapgen-lightmap-survey.py
    # compare-objects JG02 1 1 2): faces in full sun 124 (retail 130),
    # turned away 58 (58), in shadow 56 (32), undersides 44 (36) -- shadows
    # lighter than retail's on purpose. 128 = the texture as drawn.
    "object_gain": 0.8,
    "object_sun": 1.2,
    # n.l a leaf card is lit with (retail foliage barely follows its normal;
    # leaves median 95-110 against retail 97-122 over JG01/02/04/07)
    "foliage_ndl": 0.7,
    # share of the ambient a face loses when it sees none of the sky
    # (solid parts: the sky it faces; leaves: the whole sky)
    "sky_occlusion": 0.5,
    "foliage_sky": 0.3,
    # how much of the sun a solid occluder blocks (terrain bake: 0.8)
    "solid_block": 0.8,
    # occlusion map resolution and depth bias: sun, sky
    "shadow_res_cm": 25.0,
    "shadow_bias_cm": 30.0,
    "sky_res_cm": 50.0,
    "sky_bias_cm": 50.0,
    # texels under the ground are darker (retail: down to black)
    "buried_cm": 20.0,
    "buried_factor": 0.6,
    # atlases: cells per side at most this many pixels
    "atlas_max_px": 512,
}


# ----------------------------------------------------------------- meshes

def read_zms(data_dir, rel):
    """(positions cm (n, 3), faces (m, 3), uv1 (n, 2), format) of a v7/v8
    ZMS (zz_mesh_tool.cpp load_mesh_8: position, normal, colour, skin,
    tangent, uv0..uv3, faces), or None. v6 meshes are never lit in retail."""
    try:
        with open(os.path.join(data_dir, rel.replace("\\", os.sep)), "rb") as f:
            b = f.read()
    except OSError:
        return None
    e = b.index(b"\0")
    magic = b[:e].decode("latin-1")
    if magic not in ("ZMS0007", "ZMS0008"):
        return None
    p = e + 1
    fmt = struct.unpack_from("<I", b, p)[0]
    p += 4 + 24
    nb = struct.unpack_from("<H", b, p)[0]
    p += 2 + 2 * nb
    nv = struct.unpack_from("<H", b, p)[0]
    p += 2
    pos = np.frombuffer(b, "<f4", nv * 3, p).reshape(nv, 3).astype(float) * 100.0
    p += 12 * nv
    if fmt & 4:
        p += 12 * nv                                   # normal
    if fmt & 8:
        p += 16 * nv                                   # colour (float4)
    if (fmt & 16) and (fmt & 32):
        p += 24 * nv                                   # skin: float4 weights + 4 x u16
    if fmt & 64:
        p += 12 * nv                                   # tangent
    uvs = []
    for bit in (128, 256, 512, 1024):
        if fmt & bit:
            uvs.append(np.frombuffer(b, "<f4", nv * 2, p).reshape(nv, 2).astype(float))
            p += 8 * nv
    nf = struct.unpack_from("<H", b, p)[0]
    p += 2
    faces = np.frombuffer(b, "<u2", nf * 3, p).reshape(nf, 3).astype(int)
    uv1 = uvs[1] if len(uvs) >= 2 else None
    return pos, faces, uv1, fmt


class Parts:
    """Per ZSC object: its parts as (model-space triangle corners cm (m, 3, 3),
    uv1 corners (m, 3, 2), (opaque share, mean rgb, alpha-cut), mesh path),
    and whether the whole object can be lightmapped."""

    def __init__(self, data_dir, zscs, shapes):
        self.data_dir = data_dir
        self.zscs = zscs
        self.shapes = shapes                  # lighting.Shapes, for texture coverage
        self.cache = {}

    def get(self, lump, oid):
        key = (lump, oid)
        if key not in self.cache:
            meshes, objs = self.zscs[lump]
            out, ok, why = [], True, ""
            if not 0 <= oid < len(objs) or not objs[oid]["parts"]:
                ok, why = False, "no such object"
            else:
                parts = objs[oid]["parts"]
                roots = [i for i, pt in enumerate(parts) if pt["parent"] < 0]
                if roots != [0]:
                    ok, why = False, "root is not part 0 (GetPartIndex would remap)"
                for i, part in enumerate(parts):
                    rel = meshes[part["mesh"]] if 0 <= part["mesh"] < len(meshes) else ""
                    z = read_zms(self.data_dir, rel) if rel else None
                    if z is None or z[3] not in LIT_FORMATS or z[2] is None:
                        ok, why = False, "%s: format %s, no uv1 or not v7/v8" % (rel, z[3] if z else None)
                        out.append(None)
                        continue
                    pos, faces, uv1, fmt = z
                    tri = catalogue._to_model(parts, i, pos)[faces]
                    out.append((tri, uv1[faces], self.shapes.texture(part.get("texture", "")), rel))
            self.cache[key] = (out, ok, why)
        return self.cache[key]


def cell_px(table, rel, extent_m):
    """Cell size for a part: retail's choice for that mesh (217 of 220 JG
    meshes always get the same one), else by size (retail medians: 32 px
    parts 4.8 m, 64 px 11 m, 128 px 26 m)."""
    stem = os.path.splitext(os.path.basename(rel.replace("\\", "/")))[0].lower()
    if stem in table:
        return int(table[stem])
    return 32 if extent_m <= 7 else 64 if extent_m <= 18 else 128 if extent_m <= 45 else 256


def world_tris(tri, rec_pos_local, rot, scale):
    """Model triangles -> field-local world (scale, then rotate, then move),
    as the client places an IFO object (root transform = the IFO's)."""
    w = (tri.reshape(-1, 3) * np.asarray(scale, float)) @ catalogue._quat(rot) + np.asarray(rec_pos_local, float)
    return w.reshape(-1, 3, 3)


# ----------------------------------------------------------------- rasterising

def _coverage(u, v, W, H, tol, batch=1 << 20):
    """Every texel of a W x H grid whose centre (x + 0.5, y + 0.5) lies in a
    triangle (barycentric tolerance tol), for triangles with corners u, v
    (m, 3) in texel units. Yields batches of (triangle, x, y, w1, w2, w3);
    vectorised over triangles of similar size (power-of-two bounding boxes)."""
    if not len(u):
        return
    lo_u = np.maximum(np.floor(u.min(1)), 0).astype(np.int64)
    hi_u = np.minimum(np.ceil(u.max(1)), W - 1).astype(np.int64)
    lo_v = np.maximum(np.floor(v.min(1)), 0).astype(np.int64)
    hi_v = np.minimum(np.ceil(v.max(1)), H - 1).astype(np.int64)
    (ax, bx, cx), (ay, by, cy) = u.T, v.T
    den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
    ok = (hi_u >= lo_u) & (hi_v >= lo_v) & (np.abs(den) > 1e-12)
    span = np.maximum(hi_u - lo_u, hi_v - lo_v) + 1
    tier = np.ceil(np.log2(np.maximum(span, 1))).astype(int)
    for tr in np.unique(tier[ok]):
        B = 1 << int(tr)
        ks = np.nonzero(ok & (tier == tr))[0]
        oy, ox = np.mgrid[0:B, 0:B]
        step = max(1, batch // (B * B))
        for s0 in range(0, len(ks), step):
            k = ks[s0:s0 + step]
            xs = lo_u[k, None, None] + ox
            ys = lo_v[k, None, None] + oy
            valid = (xs <= hi_u[k, None, None]) & (ys <= hi_v[k, None, None])
            px, py = xs + 0.5, ys + 0.5
            d = den[k, None, None]
            w1 = ((by - cy)[k, None, None] * (px - cx[k, None, None])
                  + (cx - bx)[k, None, None] * (py - cy[k, None, None])) / d
            w2 = ((cy - ay)[k, None, None] * (px - cx[k, None, None])
                  + (ax - cx)[k, None, None] * (py - cy[k, None, None])) / d
            w3 = 1.0 - w1 - w2
            ins = valid & (w1 >= -tol) & (w2 >= -tol) & (w3 >= -tol)
            if ins.any():
                ti = np.broadcast_to(k[:, None, None], ins.shape)[ins]
                yield ti, xs[ins], ys[ins], w1[ins], w2[ins], w3[ins]


# ----------------------------------------------------------------- occlusion maps

class ShadowMap:
    """Occlusion along one direction s: every occluder triangle projected
    along s onto the z = 0 plane (u, v in cm), storing how far toward s it
    reaches (t = p . s). A point is behind an occluder when an occluder
    covering its projection reaches further toward s."""

    def __init__(self, sun, bounds_cm, res_cm):
        self.s = np.asarray(sun, float)
        self.res = res_cm
        (x0, y0), (x1, y1) = bounds_cm
        self.u0, self.v0 = x0, y0
        self.W = int(math.ceil((x1 - x0) / res_cm)) + 1
        self.H = int(math.ceil((y1 - y0) / res_cm)) + 1
        self.solid = np.full((self.H, self.W), -np.inf, np.float32)
        self.leaf_max = np.full((self.H, self.W), -np.inf, np.float32)
        self.leaf_min = np.full((self.H, self.W), np.inf, np.float32)
        self.leaf_n = np.zeros((self.H, self.W), np.float32)
        self.leaf_op = 0.3

    def project(self, p):
        p = np.asarray(p, float)
        uv = p[..., :2] - self.s[:2] * (p[..., 2:3] / self.s[2])
        t = p @ self.s
        return (uv[..., 0] - self.u0) / self.res, (uv[..., 1] - self.v0) / self.res, t

    def add(self, tris, leaf=False):
        """Rasterise world triangles (m, 3, 3) into the map."""
        if not len(tris):
            return
        u, v, t = self.project(tris)
        for ti, xs, ys, w1, w2, w3 in _coverage(u, v, self.W, self.H, 1e-6):
            tt = (w1 * t[ti, 0] + w2 * t[ti, 1] + w3 * t[ti, 2]).astype(np.float32)
            idx = ys * self.W + xs
            if leaf:
                np.maximum.at(self.leaf_max.reshape(-1), idx, tt)
                np.minimum.at(self.leaf_min.reshape(-1), idx, tt)
                np.add.at(self.leaf_n.reshape(-1), idx, 1.0)
            else:
                np.maximum.at(self.solid.reshape(-1), idx, tt)

    def visibility(self, pts, bias_cm, solid_block, batch=1 << 20):
        """Share of the light along s each point (n, 3) receives."""
        out = np.empty(len(pts), np.float32)
        for b0 in range(0, len(pts), batch):
            u, v, t = self.project(pts[b0:b0 + batch])
            iu = np.clip(np.floor(u).astype(int), 0, self.W - 1)
            iv = np.clip(np.floor(v).astype(int), 0, self.H - 1)
            vis = np.ones(len(t), np.float32)
            blocked = self.solid[iv, iu] > t + bias_cm
            vis[blocked] *= 1.0 - solid_block
            lmax, lmin, n = self.leaf_max[iv, iu], self.leaf_min[iv, iu], self.leaf_n[iv, iu]
            behind_all = lmin > t + bias_cm
            behind_some = ~behind_all & (lmax > t + bias_cm)
            layers = np.where(behind_all, n, np.where(behind_some, n * 0.5, 0.0))
            vis *= (1.0 - self.leaf_op) ** np.minimum(layers, 4.0)
            out[b0:b0 + batch] = vis
        return out


def terrain_tris(field, step=1):
    """The heightfield as world triangles (cm), for the occlusion maps."""
    g = 250.0
    rows, cols = field.shape
    r, c = np.mgrid[0:rows - 1:step, 0:cols - 1:step]
    r, c = r.ravel(), c.ravel()
    r1, c1 = np.minimum(r + step, rows - 1), np.minimum(c + step, cols - 1)

    def P(rr, cc):
        return np.stack([cc * g, rr * g, field[rr, cc]], -1)
    a, b, cc_, d = P(r, c), P(r, c1), P(r1, c), P(r1, c1)
    return np.concatenate([np.stack([a, b, d], 1), np.stack([a, d, cc_], 1)])


def occlusion_map(direction, field, terrain, occluders, res_cm, leaf_op):
    """A ShadowMap along `direction` holding the terrain (triangles from
    terrain_tris) and every occluder (world triangles, is-leaf); it covers
    where the terrain projects, plus room for objects up to 60 m tall."""
    d = np.asarray(direction, float)
    rows, cols = field.shape
    tv = np.stack([np.tile(np.arange(cols) * 250.0, rows), np.repeat(np.arange(rows) * 250.0, cols),
                   field.ravel()], 1)
    uv = tv[:, :2] - d[:2] * (tv[:, 2:3] / d[2])
    reach = 6000.0 * math.hypot(d[0], d[1]) / d[2] + 1000.0
    sm = ShadowMap(d, ((uv[:, 0].min() - reach, uv[:, 1].min() - reach),
                       (uv[:, 0].max() + reach, uv[:, 1].max() + reach)), res_cm)
    sm.leaf_op = leaf_op
    sm.add(terrain)
    for w, leaf in occluders:
        sm.add(w, leaf=leaf)
    return sm


# The sky a texel sees: 13 directions, 6 low (25 deg), 6 high (55 deg,
# between the low ones) and the zenith. Fitted on retail JG02: retail's
# baker darkens undersides, nooks and the feet of objects by the sky they
# cannot see; with this term the bake's correlation with retail's solid
# cells goes from 0.56 to 0.73 (0.70 within a part).
SKY_DIRS = [(az, 25.0) for az in range(0, 360, 60)] + [(az, 55.0) for az in range(30, 390, 60)] + [(0.0, 90.0)]


def _den_vertical():
    """The least sky weight a vertical face gets (sum of max(0, n.d) over
    SKY_DIRS, smallest over the compass): the sky term's floor divisor."""
    ds = np.array([lighting.sun_vec(az, el) for az, el in SKY_DIRS])
    a = np.radians(np.arange(0, 360, 5))
    n = np.stack([np.sin(a), np.cos(a), np.zeros_like(a)], 1)
    return float(np.clip(n @ ds.T, 0, None).sum(1).min())


def client_accepts(pos, slot):
    """Whether the client keeps an IFO record at pos (zone-centre cm) in
    chunk slot (cx, cy): CMAP::AddObject (io_terrain.cpp:1867-1877) takes
    the f32 world position less the chunk origin, in 10 m patches truncated
    toward zero, and drops the record outside 0..15. A dropped record's
    object slot goes to the next record, so a .lit entry for it would light
    that other object, with part indices that may not exist."""
    for k in (0, 1):
        w = np.float32(np.float32(pos[k]) + np.float32(520000.0))
        local = math.trunc(float(w) - slot[k] * CHUNK_CM)
        if not 0 <= math.trunc(local / 1000.0) <= 15:
            return False
    return True


# ----------------------------------------------------------------- texels

def cell_texels(P, tri_w, tri_uv):
    """Texels of a P x P cell the part's uv1 triangles cover: (row/col (n, 2),
    world point (n, 3), face normal (n, 3)). A texel's centre is (x + 0.5) / P
    in uv1 (the 0.02 barycentric slack takes in texels the edge grazes); where
    triangles overlap the last one wins, as when drawn."""
    empty = np.zeros((0, 2), int), np.zeros((0, 3)), np.zeros((0, 3))
    if not len(tri_w):
        return empty
    n = np.cross(tri_w[:, 1] - tri_w[:, 0], tri_w[:, 2] - tri_w[:, 0])
    ln = np.linalg.norm(n, axis=1)
    good = ln > 1e-9
    if not good.any():
        return empty
    n = n[good] / ln[good][:, None]
    tw, tuv = tri_w[good], tri_uv[good] * P
    got = list(_coverage(tuv[..., 0], tuv[..., 1], P, P, 0.02))
    if not got:
        return empty
    ti, xs, ys, w1, w2, w3 = (np.concatenate(a) for a in zip(*got))
    order = np.argsort(ti, kind="stable")                     # draw order
    ti, xs, ys, w1, w2, w3 = ti[order], xs[order], ys[order], w1[order], w2[order], w3[order]
    flat = ys * P + xs
    _, last = np.unique(flat[::-1], return_index=True)
    keep = len(flat) - 1 - last
    ti, xs, ys, w1, w2, w3 = ti[keep], xs[keep], ys[keep], w1[keep], w2[keep], w3[keep]
    pts = w1[:, None] * tw[ti, 0] + w2[:, None] * tw[ti, 1] + w3[:, None] * tw[ti, 2]
    return np.stack([ys, xs], 1), pts, n[ti]


def _light_terms(cfg, terrain_cfg):
    """(object settings, terrain settings, sun vector, ambient rgb, sun rgb)."""
    c = dict(OBJECT_DEFAULT)
    c.update(cfg or {})
    t = dict(lighting.DEFAULT)
    t.update(terrain_cfg or {})
    sun = lighting.sun_vec(t["sun_az"], t["sun_el"])
    open_rgb = np.array(t["open_rgb"], np.float32)
    ambient = open_rgb * t["shadow_ratio"] * c["object_gain"]
    sun_rgb = open_rgb * (1.0 - t["shadow_ratio"]) / max(1e-3, sun[2]) * c["object_gain"] * c["object_sun"]
    return c, t, sun, ambient, sun_rgb


def small_plant_rgb(cfg=None, terrain_cfg=None):
    """The one colour every grass and flower part gets: open-air light, no
    shadow and no sky occlusion. Retail leaves grass unshaded under trees
    (user, in game, 2026-10-03, Adventurer's Plain), and per-clump shading
    read as random dark patches."""
    c, t, sun, ambient, sun_rgb = _light_terms(cfg, terrain_cfg)
    return (ambient + sun_rgb * c["foliage_ndl"]).astype(np.float32)


def light(field, occluders, pts, nrm, foliage, cfg=None, terrain_cfg=None, log=None, detail=False):
    """The lightmap colour (n, 3) of texels at world points pts with face
    normals nrm (n, 3); foliage (n,) marks leaf texels (constant n.l, sky
    from every direction alike). occluders: [(world triangles, is-leaf)].

    colour = ambient x (1 - k x (1 - sky)) + sun x n.l x sun visibility,
    ambient and sun as the terrain bake's open ground, x object_gain; sky =
    the unoccluded share of the sky directions the face looks at (0 for a
    face looking down); texels under the ground x buried_factor. With
    detail, also returns {"vis": sun visibility, "sky": sky term}."""
    c, t, sun, ambient, sun_rgb = _light_terms(cfg, terrain_cfg)
    pts = np.asarray(pts, np.float32)
    nrm = np.asarray(nrm, np.float32)
    q = pts + nrm * 8.0
    terr = terrain_tris(field)
    leaf_op = t["leaf_shadow"] * 0.5
    t0 = time.time()
    sm = occlusion_map(sun, field, terr, occluders, c["shadow_res_cm"], leaf_op)
    vis = sm.visibility(q, c["shadow_bias_cm"], c["solid_block"])
    del sm
    num = np.zeros(len(q), np.float32)
    den = np.zeros(len(q), np.float32)
    fnum, fden = np.zeros(len(q), np.float32), 0.0
    if c["sky_occlusion"] > 0 or c["foliage_sky"] > 0:
        for az, el in SKY_DIRS:
            d = lighting.sun_vec(az, el)
            sm = occlusion_map(d, field, terr, occluders, c["sky_res_cm"], leaf_op)
            v = sm.visibility(q, c["sky_bias_cm"], 1.0)
            del sm
            w = np.clip(nrm @ d.astype(np.float32), 0, None)
            num += w * v
            den += w
            fnum += math.sin(math.radians(el)) * v
            fden += math.sin(math.radians(el))
        # a face that sees less of the sky than a vertical one counts the
        # directions it cannot face as blocked, so the term fades as a face
        # turns down instead of jumping from full to none at ~62 deg below
        sky = num / np.maximum(den, _den_vertical())
        skyf = fnum / fden
    else:
        sky = skyf = np.ones(len(q), np.float32)
    if log:
        log("  object lighting: sun + %d sky maps, %d texels, %.0f s" % (len(SKY_DIRS), len(q), time.time() - t0))
    ndl = np.where(foliage, c["foliage_ndl"], np.clip(nrm @ sun.astype(np.float32), 0, None))
    occl = np.where(foliage, c["foliage_sky"] * (1.0 - skyf), c["sky_occlusion"] * (1.0 - sky))
    col = ambient[None, :] * (1.0 - occl)[:, None] + sun_rgb[None, :] * (ndl * vis)[:, None]
    ground = lighting.ground_cm(field, pts[:, 0].astype(float), pts[:, 1].astype(float))
    col[pts[:, 2] < ground - c["buried_cm"]] *= c["buried_factor"]
    if detail:
        return col.astype(np.float32), {"vis": vis, "sky": sky}
    return col.astype(np.float32)


def _fill_edges(col, cov):
    """Every uncovered texel takes its nearest covered texel's colour, so
    bilinear and mip sampling at an island's edge never reaches black."""
    if cov.all():
        return col
    _, nr, nc = areas.nearest(cov)
    return col[nr, nc]


# ----------------------------------------------------------------- bake

def bake(field, chunk_records, parts, cell_table, cfg=None, terrain_cfg=None, x0=0, y0=0, log=None):
    """Object lightmaps for every chunk.

    chunk_records: {(cx, cy) slot: {"OBJECT": [ifo.Record, ...], "CNST": [...],
    "foliage": set of record ordinals (lump OBJECT) that are grass/flowers}}
    in IFO order. Returns {(cx, cy): {"OBJECT": lit bytes, "CNST": lit bytes,
    "atlases": {name: (P*ppw, P*ppw, 3) uint8}, "skipped": n}}."""
    c = dict(OBJECT_DEFAULT)
    c.update(cfg or {})

    # place every object once: world triangles per part
    placed = {}
    for slot, lumps in chunk_records.items():
        for lump in ("OBJECT", "CNST"):
            for i, rec in enumerate(lumps.get(lump, [])):
                prs, ok, why = parts.get(lump, rec.obj_id)
                local = (rec.pos[0] + ORIGIN_CM - x0 * CHUNK_CM, rec.pos[1] + ORIGIN_CM - y0 * CHUNK_CM, rec.pos[2])
                wl = [None if pr is None else world_tris(pr[0], local, rec.rot, rec.scale) for pr in prs]
                placed[(slot, lump, i)] = (rec, prs, ok, why, wl)
    # occluders: solid parts and leaves of every object but grass and flowers
    occluders = [(w, pr[2][2]) for (slot, lump, i), (rec, prs, ok, why, wl) in placed.items()
                 if not (lump == "OBJECT" and i in chunk_records[slot].get("foliage", ()))
                 for pr, w in zip(prs, wl) if pr is not None]

    # every part's texels: a cell rasterised from uv1; grass and flowers one
    # constant colour in a 32 px cell (small_plant_rgb); a mesh whose uv1
    # leaves the unit square (it samples a neighbour's cell anyway) one
    # flat value lit at its base
    jobs, P_pts, P_nrm, P_fol = [], [], [], []
    skipped = {}
    for slot, lumps in sorted(chunk_records.items()):
        skipped[slot] = 0
        for lump in ("OBJECT", "CNST"):
            for i, rec in enumerate(lumps.get(lump, [])):
                rec_, prs, ok, why, wl = placed[(slot, lump, i)]
                if not ok or not client_accepts(rec.pos, slot):
                    skipped[slot] += 1
                    continue
                flat = lump == "OBJECT" and i in lumps.get("foliage", ())
                for k, (pr, w) in enumerate(zip(prs, wl)):
                    tri, uv, (cover, rgb, cut), rel = pr
                    if flat:
                        jobs.append((slot, lump, i, k, 32, rel, "plant", 0))
                        continue
                    ext = float(np.ptp(w.reshape(-1, 3), axis=0).max()) / 100.0
                    P = cell_px(cell_table, rel, ext)
                    pix = None
                    if not (uv.min() < -0.01 or uv.max() > 1.01):
                        pix, pts, nrm = cell_texels(P, w, uv)
                        if not len(pix):
                            pix = None
                    if pix is None:
                        cx_, cy_ = float(np.mean(w[..., 0])), float(np.mean(w[..., 1]))
                        g = float(lighting.ground_cm(field, np.array([cx_]), np.array([cy_]))[0])
                        # 40 cm over the mesh base, or over the ground where the
                        # base is sunk (sunk grass clumps sampled underground
                        # baked at half brightness)
                        pts = np.array([[cx_, cy_, max(float(w[..., 2].min()), g) + 40.0]])
                        nrm = np.array([[0.0, 0.0, 1.0]])
                    jobs.append((slot, lump, i, k, P, rel, pix, len(pts)))
                    P_pts.append(pts)
                    P_nrm.append(nrm)
                    P_fol.append(np.full(len(pts), bool(cut or pix is None)))
    if not jobs:
        return {slot: dict(pack("%d_%d" % (slot[0], 64 - slot[1]), {"OBJECT": [], "CNST": []}, c["atlas_max_px"]),
                           skipped=skipped.get(slot, 0)) for slot in chunk_records}
    col = light(field, occluders, np.concatenate(P_pts), np.concatenate(P_nrm), np.concatenate(P_fol),
                cfg, terrain_cfg, log) if P_pts else np.zeros((0, 3), np.float32)
    plant = small_plant_rgb(cfg, terrain_cfg)

    # cells, then atlases per chunk
    cells = {slot: {"OBJECT": [], "CNST": []} for slot in chunk_records}
    o = 0
    for slot, lump, i, k, P, rel, pix, n in jobs:
        cc = col[o:o + n]
        o += n
        if isinstance(pix, str):
            colr = np.broadcast_to(plant, (P, P, 3)).astype(np.float32)
        elif pix is None:
            colr = np.broadcast_to(cc[0], (P, P, 3)).astype(np.float32)
        else:
            colr = np.zeros((P, P, 3), np.float32)
            cov = np.zeros((P, P), bool)
            colr[pix[:, 0], pix[:, 1]] = cc
            cov[pix[:, 0], pix[:, 1]] = True
            colr = _fill_edges(colr, cov)
        cells[slot][lump].append((i, k, P, colr, rel))
    out = {}
    for slot in sorted(chunk_records):
        stem = "%d_%d" % (slot[0], 64 - slot[1])
        out[slot] = pack(stem, cells[slot], c["atlas_max_px"])
        out[slot]["skipped"] = skipped.get(slot, 0)
        if log:
            log("  %s: %d parts lit, %d objects left unlit"
                % (stem, len(cells[slot]["OBJECT"]) + len(cells[slot]["CNST"]), skipped.get(slot, 0)))
    return out


def pack(stem, cells, max_px):
    """Retail's packing: per lump and cell size, cells dense from position
    0, row-major from the top-left; atlases of at most max_px; the last one
    the smallest power-of-two grid (at least 2 x 2) that fits. Returns the
    two .lit files and the atlases."""
    atlases = {}
    entries = {"OBJECT": {}, "CNST": {}}             # ordinal -> [(part, atlas name, ppw, pos, P)]
    for lump, prefix in (("CNST", "Building"), ("OBJECT", "Object")):
        by_size = {}
        for i, k, P, colr, rel in cells[lump]:
            by_size.setdefault(P, []).append((i, k, colr, rel))
        for P in sorted(by_size):
            items = by_size[P]
            per_full = max(1, max_px // P)
            n_full = per_full * per_full
            for a, start in enumerate(range(0, len(items), n_full)):
                chunk = items[start:start + n_full]
                ppw = 2
                while ppw * ppw < len(chunk):
                    ppw *= 2
                if P * ppw > max_px:
                    ppw = per_full
                name = "%s_%d_%d.dds" % (prefix, P, a)
                # unused cells take the used cells' mean colour: black there
                # would bleed into the neighbouring cells' edges at mips 1-2
                fill = np.mean([colr.reshape(-1, 3).mean(0) for i, k, colr, rel in chunk], 0)
                img = np.broadcast_to(fill, (P * ppw, P * ppw, 3)).astype(np.float32).copy()
                for pos, (i, k, colr, rel) in enumerate(chunk):
                    r, cc = divmod(pos, ppw)
                    img[r * P:(r + 1) * P, cc * P:(cc + 1) * P] = colr
                    stem_m = os.path.splitext(os.path.basename(rel.replace("\\", "/")))[0]
                    entries[lump].setdefault(i, []).append((k, name, ppw, pos, P, stem_m))
                atlases[name] = np.clip(np.round(img), 0, 255).astype(np.uint8)
    catalogue_names = sorted([n for n in atlases if n.startswith("Building")], key=_atlas_key) + \
        sorted([n for n in atlases if n.startswith("Object")], key=_atlas_key)
    lits = {}
    for lump, word in (("OBJECT", "Object"), ("CNST", "Building")):
        objs = []
        for i in sorted(entries[lump]):
            prts = []
            for k, name, ppw, pos, P, stem_m in sorted(entries[lump][i]):
                # retail: <mesh>_<Object|Building>_<obj>_<part>_<x_y>_LightingMap.tga (never read)
                tga = ("%s_%s_%d_%d_%s_LightingMap.tga" % (stem_m, word, i + 1, k, stem))[-250:]
                prts.append(lit.LitPart(tga_name=tga.encode("latin-1", "replace"),
                                        part_index=k, dds_name=name.encode(),
                                        lightmap_index=catalogue_names.index(name),
                                        pixels_per_part=P, parts_per_width=ppw, position_in_map=pos))
            objs.append(lit.LitObject(obj_index=i + 1, parts=prts))
        lits[lump] = lit.build_lit(lit.Lit(objects=objs, dds_list=[n.encode() for n in catalogue_names]))
    return {"OBJECT": lits["OBJECT"], "CNST": lits["CNST"], "atlases": atlases}


def _atlas_key(name):
    _, p, k = os.path.splitext(name)[0].split("_")
    return int(p), int(k)
