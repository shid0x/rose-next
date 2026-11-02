#!/usr/bin/env python3
"""Erase a phantom shadow from the Karkia Cemetery's ground lightmaps.

The bug
-------
In the Desolate Cemetery (zone 87, ``KCEMETERY``) at about (4857, 5537) m, the
ground carries a large shadow with nothing to cast it: a hard-edged footprint
with "arms" (a round body ~40 m across, a long arm east, two horns north)
inside a soft, blotchy cloud about 110 m wide -- the look of a big dead tree,
roots and crown. No object stands within 60 m, in our IFOs or in the Jrose
dump's, and every IFO lump of the zone parses. The shadow is in the plane
lightmap itself (``x_y/x_y_PLANELIGHTINGMAP.DDS``), with the same pixel values
in Jrose's file: their map was baked with the object in place, the object was
deleted afterwards, and the lightmap was never re-baked. It is inherited data,
not something the Karkia import or the client did. It reached us through
chunks 30_30 (the footprint and most of the cloud), 29_30 (the cloud's west
lobe) and 30_29 (the cloud's faint northern edge).

The fix
-------
Repaint only the phantom; every other texel keeps Jrose's bake. Re-lighting the
whole zone (``relight-zone.py bake``) would also remove it, but restyles all 49
chunks, and our baker's relief only correlates ~0.5 with Jrose's.

1. Bake the zone's ground with our baker into a temporary folder
   (``relight-zone.py bake --zone 87 --set objects=false --out``; data/ is not
   touched). It gives the true terrain relief under the phantom, with none of it.
2. In a 3x3 mosaic around 30_30, a hand polygon (``POLY``) bounds the cloud,
   its edge through open ground clear of the real shadows nearby (crypts
   DECO 104/105/108, grass tufts DECO 73). The ground brightness inside it is
   interpolated from lit ground outside (push-pull), and the phantom is what is
   more than 4 levels darker than that: the connected part holding the
   footprint, holes closed, feathered. The grass tuft on the cloud's northern
   edge (``KEEP``) is cut out of the mask, so its shadow stays.
3. The fill is that interpolated brightness, plus our bake's mid-frequency
   relief scaled to the strength Jrose's relief has on the surrounding ground,
   plus fine grain at the surrounding grain's strength (seeded, so a re-run is
   the same). Blended by the feathered mask.
4. The DDS files stay DXT1 with their full mip chain and original header: the
   new image is encoded by the vendored texconv (the flags
   ``mapgen.lighting.encode_dds`` uses), and only the 4x4 blocks whose
   footprint holds a changed texel are taken from it, at every mip level. All
   other blocks stay byte-identical to the original file.

Usage
-----
    python scripts/fix-karkia-phantom-shadow.py --dry-run   # bake + build, write the preview only
    python scripts/fix-karkia-phantom-shadow.py             # apply
    python scripts/fix-karkia-phantom-shadow.py --restore   # put the original files back

Each file is identified by hash: a run patches files that are still the
recorded originals, reports files already patched, and refuses anything else
(a re-import or relight changed the zone since; look before forcing it).
Originals are kept in ``build/fix-karkia-phantom-shadow/orig/``, with
``preview.png`` (before / after, contrast-stretched) beside them. Re-bake the
VFS afterwards (scripts/pack.ps1); servers do not read lightmaps.

Record of the run (2026-11-02)
------------------------------
110,341 texels replaced outright and 19,345 blended; the phantom's mean
brightness went from 54.0 to 98.4 against 100.1 on the lit ground around it.
30_30: 108,910 texels changed, 9,370 DXT1 blocks (all levels); 29_30: 11,704
and 1,118; 30_29: 2,087 and 245. Mean DXT1 error on changed texels 1.3-1.4
levels; mip levels 1-3 decode as close to the box-filtered top level as the
original file's own. Two runs gave the same bytes (``PATCHED_SHA``).
"""

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np
from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
from mapgen import lighting  # noqa: E402
from rosefmt import dds  # noqa: E402

ZONE_DIR = os.path.join(ROOT, "data", "3DDATA", "MAPS", "KARKIA", "KCEMETERY")
BUILD = os.path.join(ROOT, "build", "fix-karkia-phantom-shadow")
SIDE = 512                       # plane lightmap size
FX0, FY0 = 29, 29                # mosaic = chunk folders 29..31 x 29..31

# Mosaic pixels: x = (world_x_m - 4640) * 3.2, y = (5760 - world_y_m) * 3.2.
SEED = (694, 713)                # inside the footprint: (4857, 5537) m
POLY = [(470, 530), (560, 495), (700, 495), (800, 550), (852, 650), (852, 780), (795, 868),
        (715, 902), (640, 925), (570, 905), (495, 880), (448, 800), (436, 700), (446, 600)]
KEEP = [(652, 523, 16)]          # (x, y, radius): DECO 73 grass tuft at (4843.6, 5596.6) m
DARKER = 4.0                     # levels below the interpolated ground that count as phantom

ORIGINAL_SHA = {
    "30_29": "19d59b30086edb16550737573e9fab2c54487d230cd5bc82c747f1dae1adfd28",
    "29_30": "3ac0a524e57ac49818d7d488fe4ee5ab0870b04c90fdda557c3ddb6ea6961e91",
    "30_30": "eedfba10fd304be2a6329f11b749c9d2914cba852bb85535b1597a4f9f5e41d0",
}
PATCHED_SHA = {
    "30_29": "2d1091e4305c8143deab22303fcbcb10afcb02042bea26da2a39768bb9b954c6",
    "29_30": "12d394fca62e128ad7b57e1034092df22166d7e555e9c43e1c5e1ee2e39448f9",
    "30_30": "2c01e272198612b25acd6904795697dfcfddd36d70867049468480222d2bcd72",
}


def lm_path(base, fx, fy):
    n = "%d_%d" % (fx, fy)
    return os.path.join(base, n, n + "_PLANELIGHTINGMAP.DDS")


def sha(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def mosaic(base):
    m = np.zeros((3 * SIDE, 3 * SIDE, 3), np.float32)
    for fx in range(FX0, FX0 + 3):
        for fy in range(FY0, FY0 + 3):
            im = np.asarray(Image.open(lm_path(base, fx, fy)).convert("RGB"), np.float32)
            if im.shape[:2] != (SIDE, SIDE):
                raise SystemExit("%d_%d: lightmap is %s, expected %d px" % (fx, fy, im.shape[:2], SIDE))
            m[(fy - FY0) * SIDE:(fy - FY0 + 1) * SIDE, (fx - FX0) * SIDE:(fx - FX0 + 1) * SIDE] = im
    return m


def blur(a, r):
    return lighting.blur(a, r).astype(np.float64)


def blur3(a, r):
    return np.stack([blur(a[..., c], r) for c in range(a.shape[2])], -1)


def pushpull(val, w):
    """Fill val (H, W, C) where w == 0 from where w > 0: halve until every cell
    has data, then upsample and keep what was known at each level."""
    h, w_ = w.shape
    if min(h, w_) <= 2:
        m = (val * w[..., None]).sum(axis=(0, 1)) / max(w.sum(), 1e-9)
        return np.broadcast_to(m, val.shape).copy()
    h2, w2 = (h + 1) // 2, (w_ + 1) // 2
    vp = np.pad(val * w[..., None], ((0, h2 * 2 - h), (0, w2 * 2 - w_), (0, 0)))
    wp = np.pad(w, ((0, h2 * 2 - h), (0, w2 * 2 - w_)))
    vs = vp.reshape(h2, 2, w2, 2, -1).sum(axis=(1, 3))
    ws = wp.reshape(h2, 2, w2, 2).sum(axis=(1, 3))
    coarse = np.where(ws[..., None] > 0, vs / np.maximum(ws, 1e-9)[..., None], 0)
    up = np.repeat(np.repeat(pushpull(coarse, np.minimum(ws, 1.0)), 2, axis=0), 2, axis=1)[:h, :w_]
    return val * w[..., None] + blur3(up, 1) * (1 - w[..., None])


def grow(seed, allowed):
    """The 4-connected part of `allowed` holding `seed`."""
    comp = seed & allowed
    while True:
        g = comp.copy()
        g[1:] |= comp[:-1]; g[:-1] |= comp[1:]
        g[:, 1:] |= comp[:, :-1]; g[:, :-1] |= comp[:, 1:]
        g &= allowed
        if g.sum() == comp.sum():
            return comp
        comp = g


def build_patch(o, b):
    """(patched mosaic, blend weight, report lines) from the original and our bake."""
    H, W = o.shape[:2]
    yy, xx = np.mgrid[0:H, 0:W]
    im = Image.new("L", (W, H), 0)
    ImageDraw.Draw(im).polygon(POLY, fill=255)
    R = np.asarray(im) > 0
    lum = o.mean(axis=2)

    # ground brightness inside R, from lit ground outside it (not other objects' shadows)
    ring = (~R) & (blur(R.astype(np.float64), 30) > 0.01)
    ls = blur(lum, 4)
    known = ((~R) & (ls > np.percentile(ls[ring], 30))).astype(np.float64)
    lf = blur3(o, 3)
    base = blur3(pushpull(lf, known), 6)

    seed = np.zeros((H, W), bool)
    seed[SEED[1], SEED[0]] = True
    M = grow(seed, R & (blur(base.mean(axis=2) - blur(lum, 2), 5) > DARKER))
    M = blur(M.astype(np.float64), 8) > 0.3                      # close the holes
    w = np.clip(blur(M.astype(np.float64), 5) * 1.5, 0, 1) * blur(R.astype(np.float64), 3)
    for kx, ky, kr in KEEP:
        w *= np.clip((np.hypot(xx - kx, yy - ky) - kr) / 8.0, 0, 1)

    # relief and grain matched to the lit ground around the patch
    near = (blur((w > 0.01).astype(np.float64), 40) > 0.02) & (w <= 0.01)
    ref = near & (ls > np.percentile(ls[near], 30))
    blum = b.mean(axis=2)
    relief = blur(blum, 1) - blur(blum, 12)
    grain = np.random.default_rng(87).standard_normal((H, W))
    grain -= blur(grain, 1)
    grain /= grain.std()
    F = np.empty_like(base)
    for c in range(3):
        gain = (blur(o[..., c], 1) - blur(o[..., c], 12))[ref].std() / relief[ref].std()
        fine = (o[..., c] - blur(o[..., c], 1))[ref].std()
        F[..., c] = base[..., c] + gain * relief + fine * grain
    out = np.clip(np.rint(o * (1 - w[..., None]) + F * w[..., None]), 0, 255)

    core = w > 0.95
    lo = out.mean(axis=2)
    report = ["patch: %d texels at full weight, %d blended" % (core.sum(), ((w > 0.01) & ~core).sum()),
              "brightness: phantom %.1f -> %.1f, surrounding lit ground %.1f"
              % (lum[core].mean(), lo[core].mean(), lum[ref].mean())]
    return out, w, report


# ------------------------------------------------------------------ DXT1 splice

def level_sizes(side):
    out = []
    while True:
        n = max(1, (side + 3) // 4)
        out.append((side, n))
        if side == 1:
            return out
        side //= 2


def encode_dxt1(img):
    """Full-chain DXT1 of an (H, W, 3) uint8 image, same texconv flags as encode_dds."""
    with tempfile.TemporaryDirectory() as tmp:
        Image.fromarray(img, "RGB").save(os.path.join(tmp, "lm.png"))
        cmd = [os.path.abspath(lighting.TEXCONV), "-nologo", "-y", "-dx9", "-m", "0", "-nowic", "-if", "BOX",
               "-f", "DXT1", "-o", tmp, os.path.join(tmp, "lm.png")]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            raise RuntimeError("texconv failed: %s" % (r.stdout or r.stderr)[-400:])
        with open(os.path.join(tmp, "lm.dds"), "rb") as f:
            return f.read()


def splice(orig_blob, new_blob, changed):
    """orig_blob with the blocks whose top-level footprint holds a changed texel
    replaced by new_blob's, at every mip level. Returns (blob, blocks replaced)."""
    oh, opay = dds.split(orig_blob)
    nh, npay = dds.split(new_blob)
    levels = level_sizes(SIDE)
    want = sum(n * n * 8 for _, n in levels)
    for name, h, pay in (("original", oh, opay), ("new", nh, npay)):
        if h.fourcc != b"DXT1" or (h.width, h.height) != (SIDE, SIDE) or h.mips != len(levels) or len(pay) != want:
            raise SystemExit("%s DDS is not a %d px DXT1 with %d mips (%s, %d bytes)"
                             % (name, SIDE, len(levels), h, len(pay)))
    out = bytearray(opay)
    off, replaced = 0, 0
    for side, n in levels:
        foot = SIDE // n                                  # top-level texels per block edge
        hit = changed.reshape(n, foot, n, foot).any(axis=(1, 3))
        for by, bx in zip(*np.nonzero(hit)):
            i = off + (by * n + bx) * 8
            out[i:i + 8] = npay[i:i + 8]
            replaced += 1
        off += n * n * 8
    return dds.join(oh, bytes(out)), replaced


# ------------------------------------------------------------------ main

def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--dry-run", action="store_true", help="build and write the preview, touch no data")
    ap.add_argument("--restore", action="store_true", help="put the original lightmaps back")
    a = ap.parse_args()

    paths = {k: lm_path(ZONE_DIR, *map(int, k.split("_"))) for k in ORIGINAL_SHA}
    state = {k: sha(p) for k, p in paths.items()}
    orig_dir = os.path.join(BUILD, "orig")

    if a.restore:
        for k, p in paths.items():
            if state[k] == ORIGINAL_SHA[k]:
                print("%s: already the original" % k)
                continue
            src = os.path.join(orig_dir, os.path.basename(p))
            if not os.path.exists(src) or sha(src) != ORIGINAL_SHA[k]:
                raise SystemExit("%s: no verified backup in %s" % (k, orig_dir))
            if not a.dry_run:
                shutil.copyfile(src, p)
            print("%s: restored%s" % (k, " (dry run)" if a.dry_run else ""))
        return 0

    todo = [k for k in paths if state[k] == ORIGINAL_SHA[k]]
    done = [k for k in paths if PATCHED_SHA.get(k) == state[k]]
    other = [k for k in paths if k not in todo and k not in done]
    if other:
        raise SystemExit("not the recorded original or patched file, refusing: %s" % ", ".join(other))
    if not todo:
        print("already applied: %s" % ", ".join(sorted(done)))
        return 0

    print("baking the Cemetery's ground into a temporary folder (~80 s)...")
    with tempfile.TemporaryDirectory() as tmp:
        r = subprocess.run([sys.executable, os.path.join(HERE, "relight-zone.py"), "bake", "--zone", "87",
                            "--set", "objects=false", "--out", tmp], capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit("relight-zone.py bake failed:\n%s" % (r.stdout + r.stderr)[-2000:])
        b = mosaic(tmp)
    o = mosaic(ZONE_DIR)
    out, w, report = build_patch(o, b)
    for line in report:
        print("  " + line)

    os.makedirs(BUILD, exist_ok=True)
    x0, y0, x1, y1 = 380, 440, 900, 1000
    def view(a_):
        c = a_[y0:y1, x0:x1].mean(axis=2)
        return Image.fromarray(np.clip((c - 16) / (107 - 16) * 255, 0, 255).astype(np.uint8))
    pv = Image.new("L", ((x1 - x0) * 2 + 10, y1 - y0), 255)
    pv.paste(view(o), (0, 0)); pv.paste(view(out), (x1 - x0 + 10, 0))
    pv.save(os.path.join(BUILD, "preview.png"))
    print("  preview (before | after): %s" % os.path.relpath(os.path.join(BUILD, "preview.png"), ROOT))

    changed_all = (out != o).any(axis=2)
    for k in sorted(paths):
        fx, fy = map(int, k.split("_"))
        sl = (slice((fy - FY0) * SIDE, (fy - FY0 + 1) * SIDE), slice((fx - FX0) * SIDE, (fx - FX0 + 1) * SIDE))
        changed = changed_all[sl]
        if k not in todo:
            print("%s: already patched" % k)
            continue
        with open(paths[k], "rb") as f:
            orig_blob = f.read()
        blob, nblocks = splice(orig_blob, encode_dxt1(out[sl].astype(np.uint8)), changed)

        # check: header and size unchanged, and the top level decodes to the patch
        dec = np.asarray(Image.open(__import__("io").BytesIO(blob)).convert("RGB"), np.float32)
        err = np.abs(dec - out[sl])[changed]
        untouched = ~blur(changed.astype(np.float64), 4).astype(bool)
        same = np.array_equal(dec[untouched], o[sl][untouched])
        if len(blob) != len(orig_blob) or blob[:dds.HEADER_LEN] != orig_blob[:dds.HEADER_LEN] or not same \
                or err.mean() > 3.0:
            raise SystemExit("%s: check failed (size %d/%d, untouched identical %s, mean error %.2f)"
                             % (k, len(blob), len(orig_blob), same, err.mean()))
        print("%s: %d texels changed, %d DXT1 blocks replaced over 10 mip levels, mean encode error %.2f%s"
              % (k, changed.sum(), nblocks, err.mean(), " (dry run)" if a.dry_run else ""))
        print("    patched sha256 %s" % hashlib.sha256(blob).hexdigest())
        if a.dry_run:
            continue
        os.makedirs(orig_dir, exist_ok=True)
        bak = os.path.join(orig_dir, os.path.basename(paths[k]))
        if not os.path.exists(bak):
            shutil.copyfile(paths[k], bak)
        with open(paths[k], "wb") as f:
            f.write(blob)
    if not a.dry_run:
        print("done. Re-bake the VFS (scripts/pack.ps1) to see it in game.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
