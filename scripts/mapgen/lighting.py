"""Baked terrain lightmaps (map generator phase 8, step 2).

The client draws terrain as tiles x lightmap x 2 and nothing else
(docs/mapgen/FORMATS.md, "Lightmaps"), one 512 px image per chunk, north at
the top. Retail bakes are top-down renders of the lit scene (DESIGN.md,
"Lighting"): a bright, tinted base, a gentle slope term, soft shadows from
terrain and every object, darkening where objects meet the ground, and a
blue tint under water. This module reproduces that from the same inputs the
game has: the heightfield, the placed objects (their real ZMS geometry) and
the water.

Everything is computed over the whole map at once at texel resolution
(16,000 cm / 512 = 31.25 cm), so chunk edges agree (retail's per-chunk bakes
leave a faint seam). Internal arrays have row 0 = south, like the
heightfield; `chunk_images` flips each chunk to the file's north-up order.

Light model, per texel (RGB, 0-255, 128 = neutral in game):

    lit   = ambient + sun * max(0, n.l) * sun_visibility
    final = lit * contact * canopy                (water: blended to a blue)

sun_visibility = terrain shadow x object shadow, both soft. `contact`
darkens the ground under and right beside low geometry (rock footprints,
trunks, walls), `canopy` mildly under foliage.
"""

import math
import os
import struct
import subprocess
import tempfile

import numpy as np
from PIL import Image

from . import catalogue, terrain

TEXEL_CM = 16000.0 / 512
PX = 512                      # texels per chunk side
VERT = 64                     # height-grid cells per chunk side


# ----------------------------------------------------------------- settings

def sun_vec(az, el):
    """Unit vector toward the light: az = compass bearing it comes FROM
    (clockwise from north), el = degrees above the horizon. x east, y north, z up."""
    a, e = math.radians(az), math.radians(el)
    return np.array([math.sin(a) * math.cos(e), math.cos(a) * math.cos(e), math.sin(e)])


DEFAULT = {
    # retail Junon: light from ENE (survey: 54-66 deg) -- elevation settled
    # against retail JG02 (see PLAN.md phase 8, step 2)
    "sun_az": 62.0,
    "sun_el": 45.0,
    # open flat ground, RGB, and the share of it that stays in full shadow
    # (retail JG01/JG02: open ground (161, 156, 151); shadow 0.70)
    "open_rgb": (161.0, 156.0, 151.0),
    "shadow_ratio": 0.70,
    # softness (penumbra), metres
    "terrain_soft_m": 1.5,
    "object_soft_m": 0.6,
    # contact darkening from solid geometry that touches the ground (within
    # this height): its footprint, which the object hides in game, and a
    # short halo just beyond it. Retail's averaged rock darkness at 3-6 m
    # from the centre is mostly the rock's own area, so a wide halo (first
    # try: 3 m at 0.5) put dark rings round trunks and rocks in game.
    "contact_height_m": 0.25,
    "footprint_radius_m": 0.5,
    "footprint_strength": 0.6,
    "halo_radius_m": 1.2,
    "halo_strength": 0.3,
    # sun shadow of solid geometry: the share of the sun it blocks, once
    # (not per surface); retail rock shadows are weak and round
    "solid_shadow": 0.8,
    # foliage overhead: mild sky occlusion
    "canopy_radius_m": 1.5,
    "canopy_strength": 0.45,
    # retail plane lightmaps are top-down renders: a canopy shows its leaf
    # colour (this share of the texel at full cover)
    "canopy_tint": 0.45,
    # leaf cards: a texture's opaque share is its coverage, this its darkness
    "leaf_shadow": 0.6,
    # under water (retail: roughly (40-100, 70-115, 120-190))
    "water_rgb": (66.0, 86.0, 159.0),
    "water_full_cm": 60.0,
}


# ----------------------------------------------------------------- helpers

def upsample(a, factor, offset=0.5):
    """Bilinear resample of a vertex grid (rows+1 x cols+1, vertex spacing =
    `factor` output cells) to the output grid whose cell i has its centre at
    (i + offset) / factor in vertex units."""
    rows, cols = a.shape[0] - 1, a.shape[1] - 1
    ry = (np.arange(rows * factor) + offset) / factor
    rx = (np.arange(cols * factor) + offset) / factor
    r0 = np.clip(np.floor(ry).astype(int), 0, rows - 1)
    c0 = np.clip(np.floor(rx).astype(int), 0, cols - 1)
    fr = (ry - r0)[:, None]
    fc = (rx - c0)[None, :]
    return (a[r0][:, c0] * (1 - fr) * (1 - fc) + a[r0 + 1][:, c0] * fr * (1 - fc)
            + a[r0][:, c0 + 1] * (1 - fr) * fc + a[r0 + 1][:, c0 + 1] * fr * fc)


def blur(a, radius_px):
    """Separable blur: three box passes (close to a Gaussian of sigma ~ radius)."""
    r = int(round(radius_px))
    if r < 1:
        return a
    out = a.astype(np.float32)
    for axis in (0, 1):
        for _ in range(3):
            pad = [(0, 0), (0, 0)]
            pad[axis] = (r + 1, r)
            p = np.pad(out, pad, mode="edge")
            c = np.cumsum(p, axis=axis, dtype=np.float64)
            k = 2 * r + 1
            if axis == 0:
                out = ((c[k:] - c[:-k]) / k).astype(np.float32)
            else:
                out = ((c[:, k:] - c[:, :-k]) / k).astype(np.float32)
    return out


def vertex_normals(field):
    gy, gx = np.gradient(field, terrain.GRID_CM)
    n = np.stack([-gx, -gy, np.ones_like(field)], -1)
    return n / np.linalg.norm(n, axis=-1, keepdims=True)


def terrain_visibility(field, sun, max_m=200.0, step_m=1.0, factor=2):
    """Fraction of the sun each point of a grid `factor` x finer than the
    height grid sees past the terrain (0/1, soft edges come from blurring).
    Returns that grid (rows*factor+1 points per side)."""
    h = upsample_points(field, factor)
    g = terrain.GRID_CM / 100.0 / factor
    sx, sy, sz = sun
    horiz = math.hypot(sx, sy) or 1e-6
    t = sz / horiz                                    # rise per metre toward the sun
    ux, uy = sx / horiz, sy / horiz
    rows, cols = h.shape
    yy, xx = np.mgrid[0:rows, 0:cols].astype(np.float32)
    lit = np.ones(h.shape, bool)
    d = step_m
    while d <= max_m:
        r, c = yy + uy * d / g, xx + ux * d / g
        inside = (r >= 0) & (r <= rows - 1) & (c >= 0) & (c <= cols - 1)
        r0 = np.clip(np.floor(r).astype(int), 0, rows - 2)
        c0 = np.clip(np.floor(c).astype(int), 0, cols - 2)
        fr, fc = r - r0, c - c0
        hh = (h[r0, c0] * (1 - fr) * (1 - fc) + h[r0 + 1, c0] * fr * (1 - fc)
              + h[r0, c0 + 1] * (1 - fr) * fc + h[r0 + 1, c0 + 1] * fr * fc)
        lit &= ~(inside & (hh > h + d * 100.0 * t + 10.0))
        d += step_m
    return lit.astype(np.float32)


def upsample_points(a, factor):
    """Bilinear resample of a vertex grid to a point grid `factor` x finer
    (corners kept: (rows*factor+1) x (cols*factor+1))."""
    rows, cols = a.shape
    ry = np.arange((rows - 1) * factor + 1) / factor
    rx = np.arange((cols - 1) * factor + 1) / factor
    r0 = np.clip(np.floor(ry).astype(int), 0, rows - 2)
    c0 = np.clip(np.floor(rx).astype(int), 0, cols - 2)
    fr = (ry - r0)[:, None]
    fc = (rx - c0)[None, :]
    return (a[r0][:, c0] * (1 - fr) * (1 - fc) + a[r0 + 1][:, c0] * fr * (1 - fc)
            + a[r0][:, c0 + 1] * (1 - fr) * fc + a[r0 + 1][:, c0 + 1] * fr * fc)


# ----------------------------------------------------------------- object geometry

class Shapes:
    """Every part of every object as model-space triangles (cm), with the
    shadow opacity of its texture: 1 for solid textures, the share of opaque
    texels for alpha-cut ones (leaf cards cast partial, dappled shadows when
    several overlap). ZSC alpha flags are unreliable (96% of materials claim
    alpha test), so the texture's own alpha decides."""

    def __init__(self, data_dir, zscs):
        self.data_dir = data_dir
        self.zscs = zscs              # {"OBJECT": (meshes, objs), "CNST": ...}
        self.cache = {}
        self.texinfo = {}

    def texture(self, tex):
        """(opaque share, mean RGB of the opaque texels, alpha-cut?) of a texture."""
        if tex not in self.texinfo:
            cover, rgb, cut = 1.0, (110.0, 120.0, 70.0), False
            path = os.path.join(self.data_dir, tex.replace("\\", os.sep)) if tex else ""
            try:
                im = Image.open(path).convert("RGBA")
                a = np.asarray(im).astype(np.float32)
                opaque = a[..., 3] > 127
                cover = float(opaque.mean())
                cut = cover < 0.97
                if opaque.any():
                    rgb = tuple(float(v) for v in a[opaque][:, :3].mean(0))
            except (OSError, ValueError):
                pass
            self.texinfo[tex] = (cover, rgb, cut)
        return self.texinfo[tex]

    def parts(self, lump, oid):
        """[(triangles (n, 3, 3) model cm, (opaque share, mean rgb, alpha-cut))]"""
        key = (lump, oid)
        if key not in self.cache:
            meshes, objs = self.zscs[lump]
            out = []
            if 0 <= oid < len(objs):
                parts = objs[oid]["parts"]
                for i, part in enumerate(parts):
                    if not 0 <= part["mesh"] < len(meshes):
                        continue
                    t = catalogue.mesh_triangles(self.data_dir, meshes[part["mesh"]])
                    if t is None or not len(t):
                        continue
                    tri = catalogue._to_model(parts, i, t.reshape(-1, 3)).reshape(-1, 3, 3)
                    out.append((tri, self.texture(part.get("texture", ""))))
            self.cache[key] = out
        return self.cache[key]


def _raster_tris(acc, tris_px, mode, opacity):
    """Rasterise triangles (n, 3, 2) in texel coordinates (x east, y north)
    into `acc`. mode "max": acc = max(acc, opacity) over covered texels;
    "layers": acc += -log(1 - opacity) (transmission multiplies);
    "paint": acc (H, W, 3) = opacity (an RGB triple) over covered texels."""
    H, W = acc.shape[:2]
    lo = np.floor(tris_px.min(1)).astype(int)
    hi = np.ceil(tris_px.max(1)).astype(int)
    keep = (hi[:, 0] >= 0) & (hi[:, 1] >= 0) & (lo[:, 0] < W) & (lo[:, 1] < H)
    if mode == "paint":
        weight = np.asarray(opacity, np.float32)
    else:
        weight = opacity if mode == "max" else -math.log(max(1e-3, 1.0 - min(opacity, 0.999)))
    for tri, (x0, y0), (x1, y1) in zip(tris_px[keep], lo[keep], hi[keep]):
        x0, y0 = max(x0, 0), max(y0, 0)
        x1, y1 = min(x1, W - 1), min(y1, H - 1)
        if x1 < x0 or y1 < y0:
            continue
        (ax, ay), (bx, by), (cx, cy) = tri
        den = (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
        if abs(den) < 1e-6:
            continue
        ys, xs = np.mgrid[y0:y1 + 1, x0:x1 + 1]
        px, py = xs + 0.5, ys + 0.5
        w1 = ((by - cy) * (px - cx) + (cx - bx) * (py - cy)) / den
        w2 = ((cy - ay) * (px - cx) + (ax - cx) * (py - cy)) / den
        inside = (w1 >= 0) & (w2 >= 0) & (w1 + w2 <= 1)
        if not inside.any():
            continue                                  # sub-texel: below what a texel shows
        sub = acc[y0:y1 + 1, x0:x1 + 1]
        if mode == "max":
            np.maximum(sub, np.where(inside, weight, 0.0), out=sub)
        elif mode == "layers":
            sub += np.where(inside, weight, 0.0)
        else:
            sub[inside] = weight


def hull_fan(pts):
    """Convex hull of 2D points as a triangle fan (n, 3, 2); empty if degenerate."""
    p = np.unique(np.round(pts, 3), axis=0)
    if len(p) < 3:
        return np.zeros((0, 3, 2))
    p = p[np.lexsort((p[:, 1], p[:, 0]))]

    def half(points):
        out = []
        for q in points:
            while len(out) >= 2 and ((out[-1][0] - out[-2][0]) * (q[1] - out[-2][1])
                                     - (out[-1][1] - out[-2][1]) * (q[0] - out[-2][0])) <= 0:
                out.pop()
            out.append(q)
        return out
    hull = half(p)[:-1] + half(p[::-1])[:-1]
    if len(hull) < 3:
        return np.zeros((0, 3, 2))
    h = np.array(hull)
    return np.stack([np.repeat(h[:1], len(h) - 2, 0), h[1:-1], h[2:]], 1)


def ground_cm(field, x, y):
    """Terrain height (cm) at field-local positions (cm), bilinear, clamped."""
    g = terrain.GRID_CM
    rows, cols = field.shape
    c = np.clip(x / g, 0, cols - 1.001)
    r = np.clip(y / g, 0, rows - 1.001)
    c0, r0 = np.floor(c).astype(int), np.floor(r).astype(int)
    fc, fr = c - c0, r - r0
    return (field[r0, c0] * (1 - fr) * (1 - fc) + field[r0 + 1, c0] * fr * (1 - fc)
            + field[r0, c0 + 1] * (1 - fr) * fc + field[r0 + 1, c0 + 1] * fr * fc)


def clusters(pts, gap):
    """Index groups of 2D points joined by steps shorter than `gap`."""
    n = len(pts)
    d = np.hypot(*(pts[:, None, :] - pts[None, :, :]).transpose(2, 0, 1))
    near = d < gap
    seen = np.zeros(n, bool)
    out = []
    for i in range(n):
        if seen[i]:
            continue
        stack, group = [i], []
        seen[i] = True
        while stack:
            j = stack.pop()
            group.append(j)
            for k in np.nonzero(near[j] & ~seen)[0]:
                seen[k] = True
                stack.append(k)
        out.append(group)
    return out


def object_layers(shapes, placed, sun, shape_px, cfg, field=None):
    """(shadow optical depth, contact coverage, canopy optical depth, canopy
    colour) on the texel grid. Shadows: each triangle projected along the sun
    onto the plane of the object's base; an alpha-cut (leaf) part casts
    leaf_shadow x its opaque share per layer. Contact: solid geometry within
    contact_height_m of the ground, straight down. Canopy: leaf geometry,
    straight down, with its texture's colour. With `field`, shadows land on
    the terrain (two refinements from the base plane): a bridge's shadow
    falls on the ravine floor, not at deck level."""
    H, W = shape_px
    shadow = np.zeros(shape_px, np.float32)          # leaves: optical depth, layers stack
    solid = np.zeros(shape_px, np.float32)           # solid parts: one fixed-depth shadow
    contact = np.zeros(shape_px, np.float32)
    canopy = np.zeros(shape_px, np.float32)
    canopy_rgb = np.zeros((H, W, 3), np.float32)
    s = np.asarray(sun, float)
    low_cm = cfg["contact_height_m"] * 100.0
    for q in placed:
        parts = shapes.parts(q.get("lump", "OBJECT"), q["id"])
        if not parts:
            continue
        rot = catalogue._quat(q["rot"])
        scale = np.array(q["scale"], float)
        pos = np.array([q["x"], q["y"], q["z"]], float)
        ground = q["z"] - q.get("sink", 0.0)
        foliage = bool(q.get("foliage"))
        no_contact = q.get("category") == "BRIDGE"        # a deck resting near the bank is not a rock
        for tri, (cover, rgb, cut) in parts:
            if foliage and not cut:
                # small plants whose texture has no alpha (grass022): leaves anyway
                cut, cover = True, 0.5
            op = cfg["leaf_shadow"] * cover if cut else cfg["solid_shadow"]
            w = (tri.reshape(-1, 3) * scale) @ rot + pos
            if w[:, 2].max() - w[:, 2].min() < 50.0 and w[:, 2].max() - ground < 100.0:
                continue                  # a flat plate lying on the ground (pad02 under bamboo): a decal
            above = np.clip(w[:, 2] - ground, 0.0, None)
            proj = w[:, :2] - s[None, :2] * (above / s[2])[:, None]
            if field is not None:
                for _ in range(2):
                    zg = ground_cm(field, proj[:, 0], proj[:, 1])
                    above = np.clip(w[:, 2] - zg, 0.0, None)
                    proj = w[:, :2] - s[None, :2] * (above / s[2])[:, None]
            tp = (proj / TEXEL_CM).reshape(-1, 3, 2)
            if cut:
                _raster_tris(shadow, tp, "layers", op)
            else:
                # one shadow however many surfaces a solid object has: a
                # cart's planks, both faces of each, its wheels and canvas
                # stacked to near black (user, in game)
                _raster_tris(solid, tp, "max", op)
            z = w[:, 2].reshape(-1, 3) - ground
            down = (w[:, :2] / TEXEL_CM).reshape(-1, 3, 2)
            if cut:
                _raster_tris(canopy, down, "layers", op)
                _raster_tris(canopy_rgb, down, "paint", rgb)
            else:
                # the footprint of a part's low vertices, filled: rock models
                # are open shells and left a hollow ring; vertices, not
                # triangles, or a branch starting near the ground spans
                # metres; one hull per clump, or a bamboo cluster's three
                # stalks (one mesh, 18 m apart) made one big black triangle
                low = (z.reshape(-1) < low_cm) & (not no_contact)
                if low.sum() >= 3:
                    pts = down.reshape(-1, 2)[low]
                    for group in clusters(pts, 300.0 / TEXEL_CM):
                        if len(group) >= 3:
                            _raster_tris(contact, hull_fan(pts[group]), "max", 1.0)
    return shadow, solid, contact, canopy, canopy_rgb


# ----------------------------------------------------------------- bake

def bake(field, placed, shapes, water=(), cfg=None):
    """RGB lightmap over the whole map, (rows*8, cols*8, 3) float, row 0 =
    south, for a heightfield of rows+1 x cols+1 vertices (cm). `placed`:
    objects as the generator stores them (field-local x, y cm; z; rot; scale;
    sink; lump; id). `water`: [(vertex mask, level cm)]."""
    c = dict(DEFAULT)
    c.update(cfg or {})
    sun = sun_vec(c["sun_az"], c["sun_el"])
    rows, cols = field.shape[0] - 1, field.shape[1] - 1
    f8 = PX // VERT                                       # 8 texels per height cell
    H, W = rows * f8, cols * f8
    texel_m = TEXEL_CM / 100.0

    # slope term from vertex normals, bilinear to texels
    n = vertex_normals(field)
    ndl = np.clip(sum(upsample(n[..., k], f8) * sun[k] for k in range(3)), 0.0, None)

    # terrain shadow: on a 2x finer point grid, then to texels, softened
    tv = terrain_visibility(field, sun, factor=2)
    tv = upsample(tv, f8 // 2)
    tv = blur(tv, c["terrain_soft_m"] / texel_m)

    # objects
    shadow_od, solid, contact, canopy_od, canopy_rgb = object_layers(shapes, placed, sun, (H, W), c, field)
    ov = np.exp(-blur(shadow_od, c["object_soft_m"] / texel_m)) * (1.0 - blur(solid, c["object_soft_m"] / texel_m))
    footprint = blur(contact, c["footprint_radius_m"] / texel_m)
    halo = blur(contact, c["halo_radius_m"] / texel_m)
    canopy_sharp = 1.0 - np.exp(-canopy_od)
    canopy = 1.0 - np.exp(-blur(canopy_od, c["canopy_radius_m"] / texel_m))

    # colours: open flat ground = ambient + sun * sin(el); full shadow = ratio
    open_rgb = np.array(c["open_rgb"], np.float32)
    ambient = open_rgb * c["shadow_ratio"]
    sun_rgb = open_rgb * (1.0 - c["shadow_ratio"]) / max(1e-3, sun[2])
    vis = tv * ov
    lit = ambient[None, None, :] + sun_rgb[None, None, :] * (ndl * vis)[..., None]
    lit *= ((1.0 - c["footprint_strength"] * np.clip(footprint, 0, 1))
            * (1.0 - c["halo_strength"] * np.clip(halo, 0, 1)))[..., None]
    lit *= (1.0 - c["canopy_strength"] * canopy)[..., None]
    # the canopy seen from above: its leaf colour at the texel's light level
    if c["canopy_tint"] > 0:
        lum = lit[..., 0] * 0.299 + lit[..., 1] * 0.587 + lit[..., 2] * 0.114
        leaf_lum = np.maximum(1.0, canopy_rgb[..., 0] * 0.299 + canopy_rgb[..., 1] * 0.587
                              + canopy_rgb[..., 2] * 0.114)
        leaf = canopy_rgb / leaf_lum[..., None] * lum[..., None]
        a = (c["canopy_tint"] * canopy_sharp)[..., None]
        lit = lit * (1 - a) + leaf * a

    # water: blend toward the water colour with depth
    if water:
        h_t = upsample(field, f8)
        wet = np.zeros((H, W), np.float32)
        for mask, level in water:
            m = upsample(mask.astype(np.float32), f8) > 0.5
            depth = np.where(m, level - h_t, 0.0)
            wet = np.maximum(wet, np.clip(depth / c["water_full_cm"], 0, 1))
        wcol = np.array(c["water_rgb"], np.float32)[None, None, :] * (0.8 + 0.2 * vis)[..., None]
        lit = lit * (1 - wet[..., None]) + wcol * wet[..., None]
    return np.clip(lit, 0, 255)


def chunk_images(lightmap, width, height):
    """{(cx, cy) offsets from the SW chunk: (512, 512, 3) uint8, row 0 = north}."""
    out = {}
    img = np.round(lightmap).astype(np.uint8)
    for cy in range(height):
        for cx in range(width):
            out[(cx, cy)] = np.ascontiguousarray(img[cy * PX:(cy + 1) * PX, cx * PX:(cx + 1) * PX][::-1])
    return out


# ----------------------------------------------------------------- DDS

TEXCONV = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "thirdparty",
                       "directxtex-2020.9.30", "texconv.exe")


def encode_dds(images):
    """{key: (512, 512, 3) uint8} -> {key: DDS bytes}: DXT5 with a full mip
    chain and the legacy DX9 header, as retail plane lightmaps are. Encoded
    by the vendored texconv (-nowic -if BOX, see add-dds-mipmaps.py: WIC
    darkens downsampled mips)."""
    out = {}
    with tempfile.TemporaryDirectory() as tmp:
        names = {}
        for i, (key, a) in enumerate(sorted(images.items())):
            name = "lm%03d" % i
            Image.fromarray(a, "RGB").save(os.path.join(tmp, name + ".png"))
            names[key] = name
        cmd = [os.path.abspath(TEXCONV), "-nologo", "-y", "-dx9", "-m", "0", "-nowic", "-if", "BOX",
               "-f", "DXT5", "-o", tmp] + [os.path.join(tmp, n + ".png") for n in names.values()]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError("texconv failed: %s" % (r.stdout or r.stderr)[-400:])
        for key, name in names.items():
            with open(os.path.join(tmp, name + ".dds"), "rb") as f:
                out[key] = f.read()
    return out


def dds_info(data):
    """(width, height, mip count, fourcc) of a DDS file's header."""
    h, w, mips = struct.unpack_from("<III", data, 12)[0], struct.unpack_from("<I", data, 16)[0], \
        struct.unpack_from("<I", data, 28)[0]
    return w, h, mips, data[84:88].decode("latin-1")
