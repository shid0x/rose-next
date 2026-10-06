"""Make a zone's minimap from the map itself, the way retail made them.

    python scripts/make-minimap.py render  --zone 22 [--scale 4]   # editor -> build/minimap/<FOLDER>/
    python scripts/make-minimap.py style   --zone 22               # render -> minimap.png
    python scripts/make-minimap.py compare --zone 22 [22 25 ...]   # side by side with the zone's DDS
    python scripts/make-minimap.py install --zone 22 [--dry-run]   # minimap.png -> the zone's DDS
    python scripts/make-minimap.py restore --zone 22
    python scripts/make-minimap.py all     --zone 22 [--install]   # render + style + compare

--zone takes LIST_ZONE rows or map folder names (JG01); several are fine.

Retail minimaps are orthographic screenshots of the map from straight
above, made with a 3ds Max plugin that is lost, then framed as parchment
and labelled by hand. This rebuilds the screenshot half with the map
editor (`Map Editor.exe --shots` with `ortho` views, Engine/ShotRunner.cs),
which draws terrain, its lightmaps and every object the way the editor
shows them.

How the client places a minimap (CMinimapDLG::CalculateDisplayPos):
64 px per 160 m chunk (2.5 m a pixel, one terrain grid cell), and the
texture carries a one-chunk margin on every side, which the frame fills;
LIST_ZONE col 8 names the DDS and cols 9/10 the chunk folder x_y of the
top-left chunk inside the margin. So pixel (64, 64) is world
(160 * startX, 160 * (65 - startY)) metres, north up. A zone that has a
minimap is rendered over exactly its texture's extent, so the result lines
up pixel for pixel with the retail picture (`compare`); a zone without one
(NOMAP, a generated zone) gets the extent of its chunks.

render: the editor draws the extent at `--scale` times the final
resolution, in tiles of at most 2048 px, with the editing helpers, sky,
NPCs and monsters hidden and the water planes in an opaque key colour
(depth-tested, so whatever stands above the water still covers it). The
stitched picture is build/minimap/<FOLDER>/render.png.

style: box-downsamples it (water coverage kept per pixel), paints the
water (blue, darker away from the shore), fills chunks the map does not
have from their surroundings, frames it with a retail parchment frame cut
to size and marks the warp gates with their destination's name. When the
zone already has a minimap, the land grade and the water colours are
fitted to it, so a regenerated map keeps its zone's look; otherwise the
Junon defaults apply. `--set key=value` overrides a setting (STYLE).
STYLE's comment records what was measured against retail and why there
is no relief shading and no sharpening.

compare: compare.png, ours beside the original, and the mean difference
inside the frame. First run (2026-10-06, 0-255 per channel): JD04 9.3,
KCEMETERY 8.7, EJ01 10.1, JG01 14.6, JD01 16.3, JG03 16.7, JPT01 19.4,
LP01 21.5. The worst two are data, not style: our LP01 lacks the chunks
of its south-east, and JPT01's west chunks carry no ocean plane, so the
render shows seabed where retail painted sea.

install writes build/minimap/<FOLDER>/minimap.png as the zone's DDS (DXT5,
full mip chain, as retail), backing the original up to
build/minimap/<FOLDER>/original/ on the first install; for a zone with no
minimap it also fills LIST_ZONE cols 8-10. Client-only data apart from the
STB cells (the server ignores them): re-bake the VFS (scripts/pack.ps1).
"""

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys

import numpy as np
from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
_spec = importlib.util.spec_from_file_location("mapgen_zone", os.path.join(HERE, "mapgen-zone.py"))
mz = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mz)
from mapgen import lighting, prefab  # noqa: E402

OUT = os.path.join(mz.REPO, "build", "minimap")

PX_PER_CHUNK = 64          # MINIMAP_RESOLUTION_PER_MAP
M_PER_PX = 2.5             # 160 m / 64 px
CHUNK_M = 160.0
TILE_PX = 2048             # largest render target the editor is asked for
WATER_KEY = (255, 0, 255)  # what the editor paints water with
VOID = (127, 127, 127)     # the editor's clear colour: no terrain there


# ----------------------------------------------------------------- zones

class Zone:
    """A LIST_ZONE row with a map, and where its minimap goes."""

    def __init__(self, row, stb):
        self.row = row
        cell = lambda c: stb.get(row, c).decode("latin-1").strip()  # noqa: E731
        self.name = cell(mz.COL_NAME)
        self.zon = cell(mz.COL_ZON)
        self.zdir = os.path.dirname(mz.P(self.zon))
        self.folder = os.path.basename(self.zdir).upper()
        self.minimap_rel = cell(mz.COL_MINIMAP)
        self.start = (int(cell(mz.COL_MM_X) or 0), int(cell(mz.COL_MM_Y) or 0))
        self.out = os.path.join(OUT, self.folder)

    @property
    def has_minimap(self):
        return self.minimap_rel and self.minimap_rel.upper() != "NOMAP"

    @property
    def minimap_path(self):
        return mz.P(self.minimap_rel) if self.has_minimap else None

    def chunks(self):
        """{(x, y) chunk folder numbers} that carry a heightmap."""
        out = set()
        for f in os.listdir(self.zdir):
            m = re.match(r"(\d+)_(\d+)\.him$", f, re.I)
            if m:
                out.add((int(m.group(1)), int(m.group(2))))
        return out

    def extent(self):
        """(startX, startY, width px, height px): the existing minimap's, else
        the chunks' bounding box plus the one-chunk margin."""
        path = self.minimap_path
        if path and os.path.exists(path) and self.start[0] > 0:
            w, h = Image.open(path).size
            return self.start[0], self.start[1], w, h
        ch = self.chunks()
        xs, ys = [c[0] for c in ch], [c[1] for c in ch]
        return (min(xs), min(ys), (max(xs) - min(xs) + 3) * PX_PER_CHUNK,
                (max(ys) - min(ys) + 3) * PX_PER_CHUNK)


def world_rect(ext):
    """Editor metres (x0, y0, x1, y1) covered by the whole texture."""
    sx, sy, w, h = ext
    x0 = CHUNK_M * (sx - 1)
    ytop = CHUNK_M * (66 - sy)
    return x0, ytop - h * M_PER_PX, x0 + w * M_PER_PX, ytop


def zones_for(args):
    stb = mz.oro.Stb(mz.P(mz.ZONE_STB))
    out = []
    for a in args:
        if a.isdigit():
            row = int(a)
            if row >= stb.rows or not stb.get(row, mz.COL_ZON).strip():
                raise SystemExit("zone %s has no map in LIST_ZONE" % a)
            out.append(Zone(row, stb))
            continue
        hit = [r for r in range(stb.rows) if stb.get(r, mz.COL_ZON).strip()
               and os.path.basename(os.path.dirname(stb.get(r, mz.COL_ZON).decode("latin-1").strip())).lower() == a.lower()]
        if not hit:
            raise SystemExit("no LIST_ZONE row uses a map folder named %s" % a)
        out.append(Zone(hit[0], stb))
    return out


# ----------------------------------------------------------------- render

def cmd_render(z, scale):
    ext = z.extent()
    x0, y0, x1, y1 = world_rect(ext)
    W, H = ext[2] * scale, ext[3] * scale
    os.makedirs(z.out, exist_ok=True)
    tdir = os.path.join(z.out, "tiles")
    shutil.rmtree(tdir, ignore_errors=True)
    os.makedirs(tdir)
    tiles = []
    mpp = M_PER_PX / scale
    for r, top in enumerate(range(0, H, TILE_PX)):
        for c, left in enumerate(range(0, W, TILE_PX)):
            tw, th = min(TILE_PX, W - left), min(TILE_PX, H - top)
            rect = (x0 + left * mpp, y1 - (top + th) * mpp, x0 + (left + tw) * mpp, y1 - top * mpp)
            tiles.append(("t%d_%d" % (r, c), left, top, tw, th, rect))
    job = os.path.join(tdir, "job.txt")
    with open(job, "w", encoding="utf-8", newline="\n") as f:
        f.write("# written by make-minimap.py render %s\n" % z.folder)
        f.write("zone %d\nout %s\n" % (z.row, tdir))
        f.write("hide %s Sky NPCs Monsters\nsettle 90\n" % mz.SHOT_HIDE)
        f.write("water %d %d %d\n" % WATER_KEY)
        for name, _, _, tw, th, rect in tiles:
            f.write("ortho %s %s %d %d\n" % (name, " ".join("%.4f" % v for v in rect), tw, th))
    print("render zone %d (%s): %dx%d at %dx, %d tile(s)" % (z.row, z.folder, ext[2], ext[3], scale, len(tiles)))
    try:
        rc = subprocess.run([mz.EDITOR_EXE, "--shots", job], cwd=mz.DATA, timeout=900).returncode
    except subprocess.TimeoutExpired:
        raise SystemExit("the editor did not finish within 15 minutes (see data/Map Editor.log)")
    done = os.path.join(tdir, "shots.txt")
    lines = open(done, encoding="utf-8").read().split() if os.path.exists(done) else []
    if rc != 0 or not lines or lines[-1] != "done":
        raise SystemExit("the editor stopped early (exit %d; see data/Map Editor.log)" % rc)
    full = Image.new("RGB", (W, H))
    for name, left, top, _, _, _ in tiles:
        full.paste(Image.open(os.path.join(tdir, name + ".png")).convert("RGB"), (left, top))
    full.save(os.path.join(z.out, "render.png"))
    with open(os.path.join(z.out, "render.json"), "w", encoding="utf-8") as f:
        json.dump({"zone": z.row, "folder": z.folder, "extent": list(ext), "scale": scale}, f, indent=1)
    shutil.rmtree(tdir, ignore_errors=True)
    print("  %s" % os.path.join(z.out, "render.png"))


# ----------------------------------------------------------------- style

# Measured against the retail minimaps of JG01 JPT01 JD01 JG03 JD04 LP01
# EJ01 KCEMETERY, rendered over their exact extents (2026-10-06):
#  - Land needs no grade. The raw render is already within 11-23 levels
#    (mean abs, per channel) of retail; a least-squares colour transform
#    fitted on all the Junon zones made half of them worse and Luna 2x
#    worse, i.e. the residual is per-map artwork, not a global grade.
#  - No relief shading either: the ratio retail / render correlates with a
#    height-field hillshade at |r| <= 0.13 for every sun direction. Retail
#    is the lightmapped scene, nothing added.
#  - A box filter from 4x matches retail's detail (gradient energy 18.9 vs
#    18.3 on JG01, 14.2 vs 13.3 on JD04); Lanczos and any unsharp mask add
#    detail retail does not have and raise the error.
#  - Water is where retail was edited by hand: JG01 / JPT01 / LP01 paint it
#    flat blue, darker at the shore -- the medians below are JG01 + JPT01
#    by distance from the shore -- while JD01 / JG03 show the pale ocean
#    texture. The painted look is the default, with the texture faint on top.
STYLE = {
    "water_shore": (72, 112, 150),   # RGB at the shoreline
    "water_deep": (74, 138, 228),    # RGB from water_depth_m out
    "water_depth_m": 30.0,
    "water_texture": 0.08,           # strength of OCEAN01 on the water, 0 = flat
    "water_texture_px": 48,          # one ocean texture repeat, final pixels
    "brightness": 1.0,
    "saturation": 1.0,
    "frame": r"3DDATA\MAPS\JUNON\JPT01\MINIMAP.DDS",   # retail frame cut to size
    "labels": 1,
    "match_original": 1,             # fit land grade + water to the zone's own minimap
}
STYLE_HELP = {
    "water_shore": "water colour at the shore, R,G,B",
    "water_deep": "water colour in open water, R,G,B",
    "water_depth_m": "distance from the shore (m) over which the water darkens to water_deep",
    "water_texture": "how much of the game's ocean texture shows on the water (0 = flat)",
    "water_texture_px": "size of one ocean texture repeat in minimap pixels",
    "brightness": "multiplies the land colour",
    "saturation": "1 = as rendered, >1 more colourful",
    "frame": "minimap whose 64 px parchment frame is cut to size (an original, never one we wrote)",
    "labels": "1 = mark the warp gates with their destination, 0 = none",
    "match_original": "1 = when the zone has a minimap, fit the land grade and the water colours to it "
                      "(then water_shore / water_deep / brightness / saturation are not used), 0 = defaults",
}
OCEAN_TEXTURE = r"3DDATA\JUNON\WATER\OCEAN01_01.DDS"
FRAME = PX_PER_CHUNK


def parse_setting(key, value):
    if key not in STYLE:
        raise SystemExit("unknown setting %s (known: %s)" % (key, ", ".join(sorted(STYLE))))
    d = STYLE[key]
    if isinstance(d, tuple):
        v = tuple(int(x) for x in value.split(","))
        if len(v) != 3:
            raise SystemExit("%s wants R,G,B" % key)
        return v
    if isinstance(d, str):
        return value
    return type(d)(float(value))


def load_render(z):
    meta_path = os.path.join(z.out, "render.json")
    if not os.path.exists(meta_path):
        raise SystemExit("%s has no render yet: python scripts/make-minimap.py render --zone %d" % (z.folder, z.row))
    with open(meta_path, encoding="utf-8") as f:
        meta = json.load(f)
    if tuple(meta["extent"]) != tuple(z.extent()):
        raise SystemExit("%s: the render's extent %s is not the zone's %s any more; render it again"
                         % (z.folder, meta["extent"], list(z.extent())))
    big = np.asarray(Image.open(os.path.join(z.out, "render.png")).convert("RGB"))
    return meta, big


def coverage(big, s):
    """Box-downsample by s: (land colour, land / water / void coverage)."""
    H, W = big.shape[0] // s, big.shape[1] // s
    big = big[:H * s, :W * s]
    water = np.all(big == WATER_KEY, axis=2)
    void = np.all(big == VOID, axis=2)
    land = ~water & ~void

    def down(a):
        return a.reshape(H, s, W, s, *a.shape[2:]).mean(axis=(1, 3))

    lc = down(land.astype(np.float32))
    col = down(big.astype(np.float32) * land[..., None]) / np.maximum(lc, 1e-6)[..., None]
    return col, lc, down(water.astype(np.float32)), down(void.astype(np.float32))


def grade(col, st):
    if st["saturation"] != 1.0:
        grey = (col @ np.array([0.299, 0.587, 0.114], np.float32))[..., None]
        col = grey + (col - grey) * st["saturation"]
    return col * st["brightness"]


def water_colour(lc, st):
    from mapgen import areas
    dist, _, _ = areas.nearest(lc >= 0.5)        # metres to the nearest land pixel (2.5 m grid)
    t = np.clip(dist / max(st["water_depth_m"], 1e-3), 0.0, 1.0)[..., None]
    t = t * t * (3 - 2 * t)
    col = np.array(st["water_shore"], np.float32) * (1 - t) + np.array(st["water_deep"], np.float32) * t
    if st["water_texture"] > 0:
        tex = Image.open(mz.P(OCEAN_TEXTURE)).convert("L")
        n = max(4, int(st["water_texture_px"]))
        tex = np.asarray(tex.resize((n, n), Image.BOX)).astype(np.float32)
        tex = (tex - tex.mean()) / max(tex.std(), 1e-3)
        H, W = lc.shape
        tiled = np.tile(tex, (H // n + 1, W // n + 1))[:H, :W]
        col = col * (1.0 + st["water_texture"] * 0.25 * tiled[..., None])
    return col


def push_pull(values, known):
    """`values` (H x W x C) filled where `known` is False: averaged down a
    pyramid over the known pixels, then each level's holes filled from the
    level above -- a smooth blend of the surroundings, no streaks."""
    C = values.shape[2]
    levels = []
    c, w = values * known[..., None], known.astype(np.float32)
    while True:
        levels.append((c, w))
        if max(c.shape[:2]) <= 1:            # down to one pixel: every level has data above it
            break
        h2, w2 = (c.shape[0] + 1) // 2, (c.shape[1] + 1) // 2
        cp = np.pad(c, ((0, h2 * 2 - c.shape[0]), (0, w2 * 2 - c.shape[1]), (0, 0)))
        wp = np.pad(w, ((0, h2 * 2 - w.shape[0]), (0, w2 * 2 - w.shape[1])))
        c = cp.reshape(h2, 2, w2, 2, C).sum(axis=(1, 3))
        w = wp.reshape(h2, 2, w2, 2).sum(axis=(1, 3))
    c, w = levels[-1]
    fill = c / np.maximum(w, 1e-6)[..., None]
    for c, w in reversed(levels[:-1]):
        big = np.stack([np.asarray(Image.fromarray(fill[..., k].astype(np.float32)).resize(
            (c.shape[1], c.shape[0]), Image.BILINEAR)) for k in range(C)], axis=2)
        mean = c / np.maximum(w, 1e-6)[..., None]
        a = np.clip(w, 0.0, 1.0)[..., None]
        fill = mean * a + big * (1 - a)
    return np.where(known[..., None], values, fill)


def fill_void(img, land, water, lc, wc):
    """Void pixels (no terrain: a chunk the map does not have) become more
    of their surroundings rather than a grey hole -- retail painted past the
    playable area. Water where the void is mostly bordered by water (an
    island map missing a sea chunk), the blended land colour elsewhere."""
    solid = (lc + wc) > 0.5
    if solid.all() or not solid.any():
        return img
    land_fill = push_pull(land, lc > 0.5)
    frac = push_pull((wc / np.maximum(lc + wc, 1e-6))[..., None], solid)[..., 0]
    t = np.clip((frac - 0.35) / 0.3, 0.0, 1.0)[..., None]
    t = t * t * (3 - 2 * t)
    return np.where(solid[..., None], img, land_fill * 0.94 * (1 - t) + water * t)


def match_original(z, lc, wc, land):
    """Fitted from the zone's own minimap, when it has one: an affine colour
    transform for the land (robust least squares over the land pixels
    inside the frame) and the water's shore and open-water colours. A
    regenerated minimap then keeps its zone's look -- Luna's deep blue,
    Eldeon's flat greens -- instead of Junon's. None, None without one."""
    orig = original_minimap(z)
    if not orig:
        return None, None
    ref = np.asarray(Image.open(orig).convert("RGB")).astype(np.float32)
    H, W = lc.shape
    if ref.shape[:2] != (H, W):
        return None, None
    inner = np.zeros((H, W), bool)
    inner[FRAME + 6:H - FRAME - 6, FRAME + 6:W - FRAME - 6] = True
    m = inner & (lc > 0.99)
    affine = None
    if m.sum() > 2000:
        X = np.c_[land[m], np.ones(m.sum(), np.float32)]
        Y = ref[m]
        keep = np.ones(len(X), bool)
        for _ in range(3):                         # drop labels, markers, areas that differ
            M, *_ = np.linalg.lstsq(X[keep], Y[keep], rcond=None)
            r = np.abs(X @ M - Y).sum(axis=1)
            keep = r < 2.5 * np.median(r) + 1
        affine = M
    water = None
    wm = inner & (wc > 0.99)
    if wm.sum() > 500:
        from mapgen import areas
        dist, _, _ = areas.nearest(lc >= 0.5)
        shore = wm & (dist < 7.5)
        deep = wm & (dist >= 20)
        if shore.sum() > 50 and deep.sum() > 50:
            water = (tuple(int(v) for v in np.median(ref[shore], axis=0)),
                     tuple(int(v) for v in np.median(ref[deep], axis=0)))
    return affine, water


def frame_source(st):
    """The retail picture the frame is cut from: the original, if we have
    since installed our own minimap over it."""
    rel = st["frame"]
    path = mz.P(rel)
    folder = os.path.basename(os.path.dirname(path)).upper()
    backup = os.path.join(OUT, folder, "original", os.path.basename(path))
    if os.path.exists(backup):
        path = backup
    if not os.path.exists(path):
        raise SystemExit("frame source %s not found" % rel)
    return np.asarray(Image.open(path).convert("RGB")).astype(np.float32)


def _period(strip):
    """Repeat length (px) of the frame's ornament along a horizontal strip
    (rows across the frame, columns along it)."""
    g = strip.mean(axis=(0, 2)) if strip.ndim == 3 else strip.mean(axis=0)
    g = g - g.mean()
    ac = np.correlate(g, g, "full")[len(g) - 1:]
    ac = ac / max(ac[0], 1e-6)
    lo = 6
    return lo + int(np.argmax(ac[lo:min(64, len(ac))]))


def _fit_edge(strip, length, period):
    """A horizontal frame strip (64 x L) resized to `length` columns by
    repeating whole ornament periods from its middle, so both ends still
    meet their corners exactly; a final stretch of under half a period
    absorbs the remainder."""
    L = strip.shape[1]
    s0 = min(24, L // 4)
    s1 = s0 + max(1, (L - 2 * s0) // period) * period
    q = s1 - s0
    head, tail = strip[:, :s0], strip[:, s1:]
    need = length - head.shape[1] - tail.shape[1]
    n = max(0, int(round(need / period)) * period)
    idx = s0 + (np.arange(n) % q)
    out = np.concatenate([head, strip[:, idx], tail], axis=1)
    if out.shape[1] != length:
        out = np.asarray(Image.fromarray(out.clip(0, 255).astype(np.uint8)).resize((length, out.shape[0]), Image.BICUBIC)).astype(np.float32)
    return out


def make_frame(src, W, H):
    """A W x H frame ring (64 px) from a retail minimap's. Corners as they
    are; the left, right and bottom edges from their own side; the top from
    the bottom edge mirrored, because retail labels usually sit on the top."""
    sh, sw = src.shape[:2]
    F = FRAME
    out = np.zeros((H, W, 3), np.float32)
    out[:F, :F] = src[:F, :F]
    out[:F, W - F:] = src[:F, sw - F:]
    out[H - F:, :F] = src[sh - F:, :F]
    out[H - F:, W - F:] = src[sh - F:, sw - F:]
    # every strip as rows across the frame (row 0 against the map) by columns along it
    bottom = src[sh - F:, F:sw - F]
    left = src[F:sh - F, :F].transpose(1, 0, 2)[::-1]
    right = src[F:sh - F, sw - F:].transpose(1, 0, 2)
    band = slice(2, 12)                                       # the ornament next to the map
    p = _period(bottom[band])
    out[H - F:, F:W - F] = _fit_edge(bottom, W - 2 * F, p)
    out[:F, F:W - F] = _fit_edge(bottom, W - 2 * F, p)[::-1]
    out[F:H - F, :F] = _fit_edge(left, H - 2 * F, _period(left[band]))[::-1].transpose(1, 0, 2)
    out[F:H - F, W - F:] = _fit_edge(right, H - 2 * F, _period(right[band])).transpose(1, 0, 2)
    return out


def warp_gates(z):
    """[(world x m, world y m, destination name)] of the zone's warp gates."""
    from mapgen import ifo
    warp = mz.oro.Stb(mz.P(r"3DDATA\STB\WARP.STB"))
    zstb = mz.oro.Stb(mz.P(mz.ZONE_STB))
    stl = mz.oro.Stl(mz.P(r"3DDATA\STB\LIST_ZONE_S.STL"))
    eng = stl.langs[1] if len(stl.langs) > 1 else stl.langs[0]
    names = {k.decode("latin-1"): eng[i][0] for i, (k, _) in enumerate(stl.keys)}
    out = []
    for f in sorted(os.listdir(z.zdir)):
        if not re.match(r"\d+_\d+\.ifo$", f, re.I):
            continue
        with open(os.path.join(z.zdir, f), "rb") as fh:
            recs = ifo.parse(fh.read()).lump(ifo.WARP) or []
        for r in recs:
            dest = warp.get(r.warp_id, 1).strip()
            if not dest.isdigit():
                continue
            key = zstb.get(int(dest), mz.COL_STL).decode("latin-1").strip()
            name = names.get(key, b"")
            name = name.decode("latin-1").strip() if isinstance(name, bytes) else str(name).strip()
            name = name or zstb.get(int(dest), mz.COL_NAME).decode("latin-1").strip()
            out.append(((r.pos[0] + 520000) / 100.0, (r.pos[1] + 520000) / 100.0, name))
    return out


def _font(size):
    from PIL import ImageFont
    for f in ("arialbd.ttf", "DejaVuSans-Bold.ttf", "arial.ttf"):
        try:
            return ImageFont.truetype(f, size)
        except OSError:
            continue
    return ImageFont.load_default()


def _wrap(text, font, width):
    """As few lines as fit `width`, balanced (retail: "Valley of / Luxem
    Tower", not "Valley of Luxem / Tower")."""
    words, lines, cur = text.split(), [], ""
    for w in words:
        t = (cur + " " + w).strip()
        if cur and font.getlength(t) > width:
            lines.append(cur)
            cur = w
        else:
            cur = t
    lines += [cur] if cur else []
    if len(lines) == 2:
        splits = [(" ".join(words[:i]), " ".join(words[i:])) for i in range(1, len(words))]
        lines = list(min(splits, key=lambda s: max(font.getlength(s[0]), font.getlength(s[1]))))
    return lines


def _text_image(lines, font):
    """Black text with a pale halo, as retail letters its exits; RGBA."""
    from PIL import ImageDraw, ImageFilter
    asc, desc = font.getmetrics()
    lh = asc + desc
    w = int(max(font.getlength(l) for l in lines)) + 6
    h = lh * len(lines) + 6
    txt = Image.new("L", (w, h), 0)
    d = ImageDraw.Draw(txt)
    for i, l in enumerate(lines):
        d.text(((w - font.getlength(l)) / 2, 3 + i * lh), l, fill=255, font=font)
    halo = txt.filter(ImageFilter.MaxFilter(3)).filter(ImageFilter.GaussianBlur(0.8))
    out = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    out.paste(Image.new("RGBA", (w, h), (245, 236, 210, 255)), (0, 0), halo)
    out.paste(Image.new("RGBA", (w, h), (20, 14, 8, 255)), (0, 0), txt)
    return out


def _marker(img, x, y):
    """A small red gem, outlined, at (x, y)."""
    from PIL import ImageDraw
    d = ImageDraw.Draw(img)
    r = 4
    pts = [(x, y - r), (x + r, y), (x, y + r), (x - r, y)]
    d.polygon([(px + (1 if px > x else -1 if px < x else 0), py + (1 if py > y else -1 if py < y else 0)) for px, py in pts],
              fill=(60, 10, 5))
    d.polygon(pts, fill=(215, 30, 20))
    d.polygon([(x, y - r + 2), (x + 2, y), (x, y + 1), (x - 2, y)], fill=(255, 170, 140))


def draw_labels(img, z, ext):
    """Marks every warp gate and names where it leads. A gate near an edge
    is named on the frame beside it (rotated on the left and right), as
    retail does; any other one under its marker."""
    W, H = img.size
    x0, _, _, ytop = world_rect(ext)
    font = _font(12)
    placed = []
    seen = set()
    for gx, gy, name in warp_gates(z):
        px, py = int(round((gx - x0) / M_PER_PX)), int(round((ytop - gy) / M_PER_PX))
        if not (FRAME <= px < W - FRAME and FRAME <= py < H - FRAME):
            continue
        _marker(img, px, py)
        if not name:
            continue
        key = (name, px // 48, py // 48)
        if key in seen:
            continue
        seen.add(key)
        edge = min((py - FRAME, "top"), (H - FRAME - py, "bottom"), (px - FRAME, "left"), (W - FRAME - px, "right"))
        if edge[0] < 40:
            side = edge[1]
            lines = _wrap(name, font, 96 if side in ("top", "bottom") else 260)
            t = _text_image(lines, font)
            if side == "left":
                t = t.rotate(90, expand=True)
                pos = (FRAME - t.width + 2, py - t.height // 2)
            elif side == "right":
                t = t.rotate(-90, expand=True)
                pos = (W - FRAME - 2, py - t.height // 2)
            elif side == "top":
                pos = (px - t.width // 2, FRAME - t.height + 2)
            else:
                pos = (px - t.width // 2, H - FRAME - 2)
        else:
            t = _text_image(_wrap(name, font, 120), font)
            pos = (px - t.width // 2, py + 6)
        pos = (min(max(pos[0], 4), W - t.width - 4), min(max(pos[1], 4), H - t.height - 4))
        for _ in range(4):                       # step clear of a label already there
            box = (pos[0], pos[1], pos[0] + t.width, pos[1] + t.height)
            hit = [b for b in placed if box[0] < b[2] and b[0] < box[2] and box[1] < b[3] and b[1] < box[3]]
            if not hit:
                break
            pos = (pos[0], hit[0][3] + 1) if edge[1] in ("left", "right") or edge[0] >= 40 else (hit[0][2] + 2, pos[1])
        placed.append((pos[0], pos[1], pos[0] + t.width, pos[1] + t.height))
        img.paste(t, pos, t)


def cmd_style(z, st):
    meta, big = load_render(z)
    ext = tuple(meta["extent"])
    col, lc, wc, vc = coverage(big, meta["scale"])
    H, W = lc.shape
    affine, water_rgb = match_original(z, lc, wc, col) if st["match_original"] else (None, None)
    if affine is not None:
        land = np.c_[col.reshape(-1, 3), np.ones(H * W, np.float32)] @ affine
        land = land.reshape(H, W, 3)
        print("  land grade fitted to the original")
    else:
        land = grade(col, st)
    if water_rgb:
        st = dict(st, water_shore=water_rgb[0], water_deep=water_rgb[1])
        print("  water from the original: shore %s, open %s" % water_rgb)
    water = water_colour(lc, st)
    img = (land * lc[..., None] + water * wc[..., None]) / np.maximum(lc + wc, 1e-6)[..., None]
    img = fill_void(img, land, water, lc, wc)
    frame = make_frame(frame_source(st), W, H)
    ring = np.ones((H, W), bool)
    ring[FRAME:H - FRAME, FRAME:W - FRAME] = False
    img[ring] = frame[ring]
    out = Image.fromarray(img.clip(0, 255).astype(np.uint8), "RGB")
    if st["labels"]:
        draw_labels(out, z, ext)
    path = os.path.join(z.out, "minimap.png")
    out.save(path)
    print("style %s: %dx%d -> %s" % (z.folder, W, H, path))
    return path


# ----------------------------------------------------------------- compare

def original_minimap(z):
    """The zone's own minimap from before any install of ours, or None."""
    if not z.has_minimap:
        return None
    backup = os.path.join(z.out, "original", os.path.basename(z.minimap_path))
    for p in (backup, z.minimap_path):
        if os.path.exists(p):
            return p
    return None


def cmd_compare(z):
    """compare.png: ours | the original, and how far apart they are inside
    the frame (mean abs difference per channel, 0-255)."""
    ours_path = os.path.join(z.out, "minimap.png")
    if not os.path.exists(ours_path):
        raise SystemExit("%s has no minimap.png yet: run style first" % z.folder)
    ours = Image.open(ours_path).convert("RGB")
    orig = original_minimap(z)
    if not orig:
        print("compare %s: no original minimap to compare with" % z.folder)
        return None
    ref = Image.open(orig).convert("RGB")
    a = np.asarray(ours).astype(np.float32)
    b = np.asarray(ref).astype(np.float32)
    h, w = min(a.shape[0], b.shape[0]), min(a.shape[1], b.shape[1])
    inner = (slice(FRAME, h - FRAME), slice(FRAME, w - FRAME))
    mae = float(np.abs(a[:h, :w][inner] - b[:h, :w][inner]).mean())
    gap = 8
    sheet = Image.new("RGB", (ours.width + ref.width + gap, max(ours.height, ref.height) + 18), (32, 32, 32))
    sheet.paste(ours, (0, 18))
    sheet.paste(ref, (ours.width + gap, 18))
    from PIL import ImageDraw
    d = ImageDraw.Draw(sheet)
    font = _font(12)
    d.text((4, 2), "generated (%s)" % z.folder, fill=(230, 230, 230), font=font)
    d.text((ours.width + gap + 4, 2), "original", fill=(230, 230, 230), font=font)
    path = os.path.join(z.out, "compare.png")
    sheet.save(path)
    print("compare %s: mean abs difference inside the frame %.1f -> %s" % (z.folder, mae, path))
    return mae


# ----------------------------------------------------------------- install

def _sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def _manifest_path(z):
    return os.path.join(z.out, "installed.json")


def encode_dds(img):
    """DXT5 with a full mip chain, as retail. lighting.encode_dds's box mip
    filter refuses sides that are not powers of two (384, 448, 832 are
    common here), so this one filters linearly."""
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "minimap.png")
        Image.fromarray(img, "RGB").save(src)
        cmd = [os.path.abspath(lighting.TEXCONV), "-nologo", "-y", "-dx9", "-m", "0", "-nowic", "-if", "LINEAR",
               "-f", "DXT5", "-o", tmp, src]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit("texconv failed: %s" % (r.stdout or r.stderr)[-400:])
        with open(os.path.join(tmp, "minimap.dds"), "rb") as f:
            return f.read()


def cmd_install(z, dry_run):
    src = os.path.join(z.out, "minimap.png")
    if not os.path.exists(src):
        raise SystemExit("%s has no minimap.png yet: run style first" % z.folder)
    sx, sy, w, h = z.extent()
    img = np.asarray(Image.open(src).convert("RGB"))
    if img.shape[:2] != (h, w):
        raise SystemExit("%s: minimap.png is %dx%d, the zone wants %dx%d; style it again"
                         % (z.folder, img.shape[1], img.shape[0], w, h))
    man = {}
    if os.path.exists(_manifest_path(z)):
        with open(_manifest_path(z), encoding="utf-8") as f:
            man = json.load(f)
    stb_cells = None
    if z.has_minimap:
        target = z.minimap_path
        rel = z.minimap_rel
    else:
        rel = os.path.join(os.path.dirname(z.zon), "MINIMAP.DDS")
        target = mz.P(rel)
        stb_cells = {mz.COL_MINIMAP: rel, mz.COL_MM_X: str(sx), mz.COL_MM_Y: str(sy)}
    print("install %s: %s (%dx%d, chunk %d_%d top-left)%s" % (z.folder, rel, w, h, sx, sy,
          "; LIST_ZONE row %d cols 8-10" % z.row if stb_cells else ""))
    if dry_run:
        return
    os.makedirs(z.out, exist_ok=True)
    if "original" not in man:
        backup = os.path.join(z.out, "original", os.path.basename(target))
        if os.path.exists(target):
            os.makedirs(os.path.dirname(backup), exist_ok=True)
            shutil.copyfile(target, backup)
            man["original"] = {"path": target, "backup": backup, "sha": _sha(target)}
        else:
            man["original"] = {"path": target, "backup": None, "sha": None}
    dds = encode_dds(img)
    with open(target, "wb") as f:
        f.write(dds)
    man["written"] = {"path": target, "sha": _sha(target)}
    if stb_cells:
        stb = mz.oro.Stb(mz.P(mz.ZONE_STB))
        man.setdefault("stb", {"row": z.row, "cells": {str(c): stb.get(z.row, c).decode("latin-1") for c in stb_cells}})
        for c, v in stb_cells.items():
            stb.set(z.row, c, v)
        with open(mz.P(mz.ZONE_STB), "wb") as f:
            f.write(stb.to_bytes())
        check = mz.oro.Stb(mz.P(mz.ZONE_STB))
        bad = [c for c, v in stb_cells.items() if check.get(z.row, c).decode("latin-1") != v]
        if bad:
            raise SystemExit("LIST_ZONE did not keep cols %s" % bad)
    with open(_manifest_path(z), "w", encoding="utf-8") as f:
        json.dump(man, f, indent=1)
    w2, h2, mips, fourcc = lighting.dds_info(dds)
    print("  wrote %s: %dx%d %s, %d mips. Re-bake the VFS (scripts/pack.ps1)." % (target, w2, h2, fourcc, mips))


def cmd_restore(z, dry_run):
    if not os.path.exists(_manifest_path(z)):
        print("restore %s: nothing installed" % z.folder)
        return
    with open(_manifest_path(z), encoding="utf-8") as f:
        man = json.load(f)
    o = man["original"]
    print("restore %s: %s%s" % (z.folder, o["path"], "; LIST_ZONE cells" if "stb" in man else ""))
    if dry_run:
        return
    if o["backup"]:
        shutil.copyfile(o["backup"], o["path"])
        if _sha(o["path"]) != o["sha"]:
            raise SystemExit("restore of %s does not match its backup" % o["path"])
    elif os.path.exists(o["path"]):
        os.remove(o["path"])
    if "stb" in man:
        stb = mz.oro.Stb(mz.P(mz.ZONE_STB))
        for c, v in man["stb"]["cells"].items():
            stb.set(man["stb"]["row"], int(c), v)
        with open(mz.P(mz.ZONE_STB), "wb") as f:
            f.write(stb.to_bytes())
    shutil.rmtree(os.path.join(z.out, "original"), ignore_errors=True)
    os.remove(_manifest_path(z))
    print("  back to the original. Re-bake the VFS (scripts/pack.ps1).")


# ----------------------------------------------------------------- main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("render", "style", "compare", "install", "restore", "all"):
        p = sub.add_parser(name)
        p.add_argument("--zone", nargs="+", required=True)
        if name in ("render", "all"):
            p.add_argument("--scale", type=int, default=4, help="render at this many times the final resolution")
        if name in ("style", "all"):
            p.add_argument("--set", action="append", metavar="KEY=VALUE",
                           help="override a style setting: " + "; ".join("%s (%s)" % (k, STYLE_HELP[k]) for k in STYLE))
        if name in ("install", "restore"):
            p.add_argument("--dry-run", action="store_true")
        if name == "all":
            p.add_argument("--install", action="store_true")
    a = ap.parse_args()
    st = dict(STYLE)
    for kv in getattr(a, "set", None) or []:
        if "=" not in kv:
            raise SystemExit("--set wants key=value, got %r" % kv)
        k, v = kv.split("=", 1)
        st[k.strip()] = parse_setting(k.strip(), v.strip())
    for z in zones_for(a.zone):
        if a.cmd in ("render", "all"):
            cmd_render(z, a.scale)
        if a.cmd in ("style", "all"):
            cmd_style(z, st)
        if a.cmd in ("compare", "all"):
            cmd_compare(z)
        if a.cmd == "install" or (a.cmd == "all" and a.install):
            cmd_install(z, getattr(a, "dry_run", False))
        if a.cmd == "restore":
            cmd_restore(z, a.dry_run)
    return 0


if __name__ == "__main__":
    sys.exit(main())
