"""Texture helpers the data scripts share: decoding through texconv, and the hard cut-out.

* ``texconv(args, out)`` -- run DirectXTex's texconv, fail loudly when it writes nothing.
* ``decode_rgba(path)`` -- the top level of any DDS as an (h, w, 4) uint8 RGBA array.
* ``cutout_levels(rgba, ref)`` / ``cutout_dds(path, ref)`` -- a texture made a hard
  cut-out: alpha 0 or 255 only, at every mip level, each level cut at the threshold
  that keeps level 0's coverage.

Why a cut-out (src/engine/CLAUDE.md, "Transparency Antialiasing"): a map material with
texture alpha is alpha-blended, and the engine's two-pass alpha parts draw texels under
224 without depth. A texture meant to be solid but stored half-transparent (Junon's
market tent cloth at 204) is then see-through and loses to whatever draws after it,
and a very soft gradient (Haian Coast's hanging vines) leaves a wide blended edge. Used
by ``import-china-rose.py`` (``cutout_textures``) and ``set-texture-cutout.py``.

Imported as ``from rosedata import texture``; it needs Pillow, so ``rosedata`` itself
does not load it.
"""
import os
import shutil
import subprocess
import tempfile

import numpy as np
from PIL import Image

from rosefmt import dds

_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
TEXCONV = os.path.join(_ROOT, "thirdparty", "directxtex-2020.9.30", "texconv.exe")

DDSD_MIPMAPCOUNT, DDSCAPS_COMPLEX, DDSCAPS_MIPMAP = 0x20000, 0x8, 0x400000
CUTOUT_SOURCES = (b"DXT3", b"DXT5")      # the cut-out keeps an alpha channel: DXT5 out


def texconv(args, out):
    r = subprocess.run([TEXCONV, "-nologo", "-y"] + args, capture_output=True, text=True)
    if r.returncode != 0 or not os.path.isfile(out):
        raise SystemExit("texconv failed: %s" % (r.stdout or r.stderr)[-400:])
    return out


def decode_rgba(path):
    """The top level of any DDS as (h, w, 4) uint8 RGBA, through texconv."""
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, "src.dds")          # texconv writes beside a path it cannot parse
        shutil.copyfile(path, src)
        png = texconv(["-ft", "png", "-m", "1", "-f", "R8G8B8A8_UNORM", "-o", tmp, src],
                      os.path.join(tmp, "src.png"))
        return np.asarray(Image.open(png).convert("RGBA"))


def cutout_levels(rgba, ref):
    """A full mip chain of (h, w, 4) uint8 levels whose alpha is 0 or 255 only.

    Level 0 is cut at `ref` with its colour untouched. Each smaller level box-averages
    the alpha of the level above *before* its cut (never the cut one: a strand would
    erode level by level) and is cut at the threshold that keeps level 0's coverage
    -- DirectXTex's coverage-preserving idea, binarised. Colour is averaged over the
    visible texels only (by alpha), so no clear texel's fill colour bleeds in."""
    a = rgba[..., 3].astype(np.float64) / 255.0
    c = rgba[..., :3].astype(np.float64)
    target = float((a >= ref / 255.0).mean())
    out = []
    while True:
        t = ref / 255.0 if not out else (np.quantile(a, 1.0 - target) if target > 0 else 2.0)
        out.append(np.dstack([np.clip(np.rint(c), 0, 255), np.where(a >= t, 255, 0)]).astype(np.uint8))
        h, w = a.shape
        if h == 1 and w == 1:
            return out
        h2, w2 = max(1, h // 2), max(1, w // 2)
        A = a.reshape(h2, h // h2, w2, w // w2)
        C = c.reshape(h2, h // h2, w2, w // w2, 3)
        wsum = A.sum((1, 3))
        weighted = (C * A[..., None]).sum((1, 3)) / np.maximum(wsum, 1e-12)[..., None]
        c = np.where(wsum[..., None] > 1e-12, weighted, C.mean((1, 3)))
        a = A.mean((1, 3))


def cutout_dds(path, ref):
    """The texture at `path` as a hard cut-out DXT5 with its full chain, as DDS bytes:
    each level encoded on its own (texconv, -m 1) and the levels joined under level 0's
    header, so no encoder re-filters the cut alpha. Deterministic: the same source and
    reference give the same bytes."""
    h0 = dds.parse_header(path)
    if h0.fourcc not in CUTOUT_SOURCES:
        raise SystemExit(f"{path} is {h0.fourcc!r}: a cut-out needs an alpha channel (DXT3/DXT5)")
    levels = cutout_levels(decode_rgba(path), ref)
    head, payload = None, []
    with tempfile.TemporaryDirectory() as tmp:
        for i, lv in enumerate(levels):
            src_lv = os.path.join(tmp, f"l{i}.dds")
            dds.write_bgra(src_lv, Image.fromarray(lv, "RGBA"))
            os.makedirs(os.path.join(tmp, "out"), exist_ok=True)
            out = texconv(["-dx9", "-m", "1", "-f", "BC3_UNORM", "-o", os.path.join(tmp, "out"), src_lv],
                          os.path.join(tmp, "out", f"l{i}.dds"))
            h, body = dds.split(open(out, "rb").read())
            if head is None:
                head = h
            payload.append(body)
    head.mip_count = len(levels)
    head.flags |= DDSD_MIPMAPCOUNT
    head.caps |= DDSCAPS_COMPLEX | DDSCAPS_MIPMAP
    return dds.join(head, b"".join(payload))
