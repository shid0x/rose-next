#!/usr/bin/env python3
"""Give shipped DDS textures a real mip chain, so the client stops building one at runtime.

WHY
---
Character-spawn hitches were fixed first (see the motion-parser change in
zz_motion::load); what remained were 20-140 ms frames whose entire cost was
`shadow` -> immediate flush -> D3DXCreateTextureFromFileInMemoryEx. Splitting
that call showed the file read is ~0.1 ms per texture and the *create* is
everything else, at anywhere between 0.20 ms and 11.13 ms for a single
texture -- a 55x spread, so not a fixed overhead.

zz_renderer_d3d.cpp carries a note from the original team explaining it:

    512x512 texture
    DXT1 - miplevel1 = 0.729ms, 131KB
    DXT1 - miplevel2 = 57ms, 131KB

Same file, same size. The cost is D3DX *generating* a mip chain rather than
copying the file's, which for a DXT source means decompress, box-filter and
recompress for every level. No D3DXCreateTextureFromFileInMemoryEx flag avoids
the recompress, so this cannot be tuned away in the renderer.

data/SCRIPTS/INIT.LUA asks for 3 mip levels via setMipmapLevel(3), and that cap
is load-bearing -- object lightmaps are a gutterless atlas and a deeper chain
would bleed between neighbouring cells (see the root CLAUDE.md). Taking 3 levels
from a file that ships its own chain is a cheap copy. Building 3 levels for a
file that ships none is the expensive path, and it is pure waste: the same work,
on every client, on every load, forever.

An in-game capture settled it beyond correlation. Every slow create logged by
"r_d3d: slow texture create" -- 196 of 196, no exceptions -- had src_mips=1,
costing 1300 ms in one short session across 163 distinct files.

WHAT IT DOES
------------
Rewrites every DDS under data/ that has no mip chain, adding a full one and
keeping the pixel format, the dimensions and the legacy DX9 header. Files that
already have a chain are skipped, so re-running is a no-op and safe.

Uses thirdparty/directxtex-2020.9.30/texconv.exe, which was already vendored in
this repo and used by nothing at all.

TRAPS
-----
- **-dx9 is mandatory.** DirectXTex defaults to writing a "DX10" extended DDS
  header for some formats, and the client's D3DX9 loader cannot read those. A
  converted file would simply fail to load, and the engine's degrade-not-die
  path would draw the object untextured rather than tell you why. --verify
  rejects a DX10 header explicitly for that reason.
- **Backups go outside data/.** src/pipeline/src/pack.rs walks the data tree
  filtering only *hidden* entries -- there is no extension filter -- so a .bak
  left beside a texture gets baked into the .vfs. They go to
  build/dds-mipmap-backup/ instead.
- **-nowic is mandatory, and this one shipped a visible bug before it was found.**
  texconv filters through the Windows Imaging Component by default, and WIC
  darkens RGB badly when downsampling these textures -- measured at 36% on a
  terrain tile and 71% on an NPC body, with the alpha channel untouched. Only the
  lower mips are affected, so on screen it is a black wash that appears at
  distance and clears as you walk in. -if TRIANGLE also happens to avoid it, but
  only because WIC has no triangle filter and silently falls back to the same
  code path; -nowic is the flag that actually says what is meant. -if BOX on top
  matches what D3DX generated at runtime before any of this, keeping the visual
  result as close to the old behaviour as possible.
- **DirectXTex's own mip generator is power-of-two only** and returns
  E_FAIL [mipmaps] otherwise, so NPOT textures are skipped. No loss: they are
  minimaps, UI resources and effect strips, and the engine forces miplevels=1 for
  image textures regardless (zz_renderer_d3d download_texture, get_for_image()).
- **--verify samples decoded mip brightness, not just its presence.** The first
  version of this script checked dimensions, format and mip count, passed
  cleanly, and shipped chains that were 36-71% too dark. "The mips exist" and
  "the mips are correct" are different claims, and a verifier that cannot fail
  the actual defect is decoration. Needs Pillow; it says so when it cannot check.
- **Re-encoding a DXT file is lossy.** The top level is decompressed and
  recompressed, so the result is not bit-identical to the original. BC1/2/3
  endpoint selection is near-idempotent in practice, but this is why --restore
  exists.
- **A full chain is roughly a third larger.** The no-mip files total ~194 MB, so
  expect ~65 MB of growth. rose.vfs has a hard 2 GB limit whose failure mode is
  silent and extremely confusing (see the root CLAUDE.md), so re-check the
  archive size after the next bake.
- Since data/ is gitignored, this docstring is the only committed record of the
  change. Put new reasoning here, not just in a commit message.
"""

import argparse
import collections
import pathlib
import struct
import subprocess
import sys

REPO = pathlib.Path(__file__).resolve().parent.parent
DATA = REPO / "data"
TEXCONV = REPO / "thirdparty" / "directxtex-2020.9.30" / "texconv.exe"
BACKUP = REPO / "build" / "dds-mipmap-backup"

DDSD_MIPMAPCOUNT = 0x20000
DDPF_FOURCC = 0x4
DDPF_ALPHAPIXELS = 0x1
DDSCAPS2_CUBEMAP = 0x200
DDSCAPS2_VOLUME = 0x200000


class Header(object):
    __slots__ = ("width", "height", "mips", "fourcc", "bits", "amask",
                 "pf_flags", "caps2")


def read_header(path):
    """Parse the parts of a DDS header we care about, or None if it is not a DDS."""
    try:
        with open(path, "rb") as f:
            head = f.read(128)
    except OSError:
        return None
    if len(head) < 128 or head[:4] != b"DDS ":
        return None
    h = Header()
    flags = struct.unpack_from("<I", head, 8)[0]
    h.height, h.width = struct.unpack_from("<II", head, 12)
    mips = struct.unpack_from("<I", head, 28)[0]
    h.mips = mips if (flags & DDSD_MIPMAPCOUNT) else 0
    h.pf_flags = struct.unpack_from("<I", head, 80)[0]
    h.fourcc = head[84:88]
    h.bits = struct.unpack_from("<I", head, 88)[0]
    h.amask = struct.unpack_from("<I", head, 104)[0]
    h.caps2 = struct.unpack_from("<I", head, 112)[0]
    return h


def target_format(h):
    """DXGI format for texconv -f that preserves what the file already is.

    Returns None for anything we would rather leave alone than guess at.
    """
    if h.pf_flags & DDPF_FOURCC:
        return {
            b"DXT1": "BC1_UNORM",
            b"DXT3": "BC2_UNORM",
            b"DXT5": "BC3_UNORM",
        }.get(h.fourcc)

    has_alpha = bool(h.pf_flags & DDPF_ALPHAPIXELS) and h.amask != 0
    if h.bits == 32:
        return "B8G8R8A8_UNORM" if has_alpha else "B8G8R8X8_UNORM"
    if h.bits == 24:
        # No 24bpp DXGI format exists, so promoting to 32bpp is the only option
        # and costs a third more bytes. Only a handful of files are affected.
        return "B8G8R8X8_UNORM"
    if h.bits == 16:
        if h.amask in (0xF000, 0x000F):
            return "B4G4R4A4_UNORM"
        if has_alpha:
            return "B5G5R5A1_UNORM"
        return "B5G6R5_UNORM"
    return None


def is_pow2(v):
    return v > 0 and (v & (v - 1)) == 0


def is_excluded(path):
    """Textures that must never be given a mip chain, with the reason.

    Object lightmaps are a **gutterless atlas**: each map-object part owns one
    cell of a shared texture and SetLightMap addresses it with a UV transform, so
    there is no padding between cells and a filter that knows nothing about the
    grid blends a part's lighting into its neighbours'. The root CLAUDE.md spells
    this out and says a full chain is unsafe for them.

    Shipping a chain makes that reachable in a way it was not before. The engine
    caps loads at 3 levels (INIT.LUA setMipmapLevel(3)), but the levels now come
    from texconv's default FANT filter, whose support reaches past one texel,
    rather than from D3DX's strict 2x2 box -- so more of the neighbouring cell
    bleeds in. A lightmap multiplies into object colour, so the symptom is a dark
    wash that appears at distance and vanishes as you walk in. Which is exactly
    what was reported after the first run of this script.

    These are 32-512 px textures and contribute almost nothing to load cost, so
    there is no reason to take the risk.
    """
    u = str(path).upper()
    if "LIGHTMAP" in u:
        return "object lightmap atlas (gutterless, must not be mipped)"
    return None


def collect(root):
    """Every DDS under root with no mip chain, plus a tally of what was skipped."""
    todo = []
    skipped = collections.Counter()
    for p in sorted(root.rglob("*")):
        if p.suffix.upper() != ".DDS" or not p.is_file():
            continue
        h = read_header(p)
        if h is None:
            skipped["not a DDS"] += 1
            continue
        if h.mips > 1:
            skipped["already has mips"] += 1
            continue
        why = is_excluded(p)
        if why:
            skipped[why] += 1
            continue
        if h.caps2 & (DDSCAPS2_CUBEMAP | DDSCAPS2_VOLUME):
            skipped["cubemap/volume"] += 1
            continue
        if h.width < 2 or h.height < 2:
            skipped["too small to mip"] += 1
            continue
        if not (is_pow2(h.width) and is_pow2(h.height)):
            # DirectXTex's own mip generator (which -nowic selects, and which we
            # need because WIC darkens the result) only handles power-of-two
            # sizes; it returns E_FAIL [mipmaps] otherwise. No loss: every NPOT
            # texture here is a minimap, a UI resource or an effect strip, and the
            # engine forces miplevels=1 for image textures anyway
            # (zz_renderer_d3d download_texture, tex->get_for_image()). They are
            # drawn at 1:1 and loaded once.
            skipped["non-power-of-two (UI/minimap, never mipped)"] += 1
            continue
        fmt = target_format(h)
        if fmt is None:
            skipped["unrecognised pixel format"] += 1
            continue
        todo.append((p, h, fmt))
    return todo, skipped


def convert(todo, dry_run):
    """Run texconv, batched by (output directory, format) to avoid 1200 spawns."""
    groups = collections.defaultdict(list)
    for p, _h, fmt in todo:
        groups[(p.parent, fmt)].append(p)

    converted = 0
    failed = []
    for (out_dir, fmt), paths in sorted(groups.items()):
        rel = out_dir.relative_to(DATA)
        if dry_run:
            print("  would convert %3d file(s) -> %-16s %s" % (len(paths), fmt, rel))
            converted += len(paths)
            continue

        for p in paths:
            b = BACKUP / p.relative_to(DATA)
            b.parent.mkdir(parents=True, exist_ok=True)
            if not b.exists():
                b.write_bytes(p.read_bytes())

        # -nowic and -if BOX are both load-bearing:
        #
        #   -nowic  texconv filters through the Windows Imaging Component by
        #           default, and WIC darkens RGB badly when it downsamples these
        #           textures -- measured at 36% on a terrain tile and 71% on an
        #           NPC body, with the alpha channel untouched. On screen that is
        #           a black wash that appears at distance and clears as you walk
        #           in, because only the lower mips are affected. Forcing
        #           DirectXTex's own filters reproduces mip0's brightness exactly.
        #           (-if TRIANGLE also works, but only because WIC has no triangle
        #           filter and it silently falls back to the same code path.)
        #   -if BOX a strict 2x2 box is what D3DX generated at runtime before any
        #           of this, so it keeps the visual result as close to the old
        #           behaviour as possible. This is a load-time fix, not a
        #           re-authoring.
        cmd = [str(TEXCONV), "-nologo", "-y", "-dx9", "-m", "0", "-nowic",
               "-if", "BOX", "-f", fmt, "-o", str(out_dir)] + [str(p) for p in paths]
        proc = subprocess.run(cmd, capture_output=True, text=True)
        if proc.returncode != 0:
            failed.append((rel, fmt, (proc.stdout or proc.stderr).strip()[:200]))
            continue
        converted += len(paths)
        print("  %3d file(s) -> %-16s %s" % (len(paths), fmt, rel))
    return converted, failed


def mip_mean_rgb(path, level, blk):
    """Mean RGB of one mip level, by rebuilding it as a standalone single-level DDS.

    Needs Pillow. Returns None if it is unavailable or the level cannot be read.
    """
    try:
        from PIL import Image
    except ImportError:
        return None
    import io as _io
    try:
        d = open(path, "rb").read()
    except OSError:
        return None
    if len(d) < 128:
        return None
    head = bytearray(d[:128])
    flags = struct.unpack_from("<I", head, 8)[0]
    h, w = struct.unpack_from("<II", d, 12)
    off = 128
    for lv in range(level + 1):
        lw, lh = max(1, w >> lv), max(1, h >> lv)
        sz = max(1, (lw + 3) // 4) * max(1, (lh + 3) // 4) * blk
        if off + sz > len(d):
            return None
        if lv == level:
            hh = bytearray(head)
            struct.pack_into("<II", hh, 12, lh, lw)
            struct.pack_into("<I", hh, 28, 1)
            struct.pack_into("<I", hh, 8, flags & ~DDSD_MIPMAPCOUNT)
            struct.pack_into("<I", hh, 20, sz)
            try:
                im = Image.open(_io.BytesIO(bytes(hh) + d[off:off + sz])).convert("RGBA")
            except Exception:
                return None
            px = im.load()
            total = 0
            for y in range(lh):
                for x in range(lw):
                    r, g, b, _a = px[x, y]
                    total += r + g + b
            return total / (lw * lh * 3.0)
        off += sz
    return None


def verify_brightness(touched, sample=40):
    """Compare mip0 and mip1 mean RGB on a sample of converted files.

    This exists because the first version of this script checked only that a mip
    chain was *present* -- right dimensions, right format, DX9 header -- and
    shipped chains whose RGB was 36-71% too dark, because texconv filters through
    WIC by default. "The mips exist" and "the mips are correct" are different
    claims and only the second one matters. A verifier that cannot fail the actual
    defect is decoration.
    """
    bad = []
    checked = 0
    step = max(1, len(touched) // sample)
    for p, h, _fmt in touched[::step]:
        if not (h.pf_flags & DDPF_FOURCC):
            continue  # only the block formats are laid out predictably enough
        blk = 8 if h.fourcc == b"DXT1" else 16
        m0 = mip_mean_rgb(p, 0, blk)
        m1 = mip_mean_rgb(p, 1, blk)
        if m0 is None or m1 is None:
            continue
        checked += 1
        if m0 > 1.0 and (1.0 - m1 / m0) > 0.15:
            bad.append((p, "mip1 is %.0f%% darker than mip0 (%.0f vs %.0f)"
                        % (100.0 * (1.0 - m1 / m0), m1, m0)))
    return bad, checked


def verify_converted(touched):
    """Re-read every touched file: mips must exist, geometry must be unchanged."""
    bad = []
    for p, h, _fmt in touched:
        h2 = read_header(p)
        if h2 is None:
            bad.append((p, "no longer a readable DDS"))
        elif h2.mips <= 1:
            bad.append((p, "still has no mip chain"))
        elif (h2.width, h2.height) != (h.width, h.height):
            bad.append((p, "dimensions changed %dx%d -> %dx%d"
                        % (h.width, h.height, h2.width, h2.height)))
        elif (h2.pf_flags & DDPF_FOURCC) and h2.fourcc == b"DX10":
            bad.append((p, "written with a DX10 header (client cannot read it)"))
    return bad


def restore(only_excluded=False):
    """Put originals back. only_excluded limits it to files the current rules say
    should never have been converted, which is how a bad exclusion is corrected
    without undoing the whole pass."""
    if not BACKUP.is_dir():
        print("no backups at %s" % BACKUP)
        return 1
    n = 0
    for b in BACKUP.rglob("*"):
        if not b.is_file():
            continue
        target = DATA / b.relative_to(BACKUP)
        if only_excluded and not is_excluded(target):
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(b.read_bytes())
        n += 1
    scope = "excluded " if only_excluded else ""
    print("restored %d %sfile(s) from %s" % (n, scope, BACKUP))
    if only_excluded and n:
        print("re-bake the VFS for this to reach the client")
    return 0


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dry-run", action="store_true",
                    help="report what would change and touch nothing")
    ap.add_argument("--verify", action="store_true",
                    help="only check that no DDS under data/ is missing a mip chain")
    ap.add_argument("--restore", action="store_true",
                    help="put every pre-conversion original back")
    ap.add_argument("--restore-excluded", action="store_true",
                    help="put back only the files the current exclusion rules say "
                         "should never have been converted")
    ap.add_argument("--subdir", default=None,
                    help="limit to a subtree of data/, e.g. 3DDATA/TERRAIN")
    args = ap.parse_args()

    if args.restore or args.restore_excluded:
        return restore(only_excluded=args.restore_excluded)

    if not DATA.is_dir():
        print("no data/ directory at %s" % DATA)
        return 1
    if not args.verify and not TEXCONV.is_file():
        print("texconv.exe not found at %s" % TEXCONV)
        return 1

    root = DATA / args.subdir if args.subdir else DATA
    if not root.is_dir():
        print("no such subtree: %s" % root)
        return 1

    todo, skipped = collect(root)
    total_bytes = sum(p.stat().st_size for p, _h, _f in todo)

    print("scanned %s" % root)
    for k, v in skipped.most_common():
        print("  skipped %-28s %5d" % (k, v))
    print("  need a mip chain             %5d  (%.1f MB)"
          % (len(todo), total_bytes / 1048576.0))
    for fmt, n in collections.Counter(f for _p, _h, f in todo).most_common():
        print("      %-16s %5d" % (fmt, n))

    if args.verify:
        conv = []
        if BACKUP.is_dir():
            for b in sorted(BACKUP.rglob("*")):
                if not b.is_file() or b.suffix.upper() != ".DDS":
                    continue
                cur = DATA / b.relative_to(BACKUP)
                h = read_header(cur)
                if h is not None and h.mips > 1:
                    conv.append((cur, h, None))
        dark, checked = verify_brightness(conv)
        if dark:
            print("\nFAIL: generated mips are too dark on %d of %d sampled file(s)"
                  % (len(dark), checked))
            for pp, why in dark[:10]:
                print("   %-52s %s" % (pp.relative_to(DATA), why))
            return 1
        if checked:
            print("  brightness checked on            %5d sampled converted file(s)"
                  % checked)
        elif conv:
            print("  brightness NOT checked (install Pillow to enable it)")

        if todo:
            print("\nFAIL: %d file(s) still have no mip chain" % len(todo))
            for p, _h, _f in todo[:20]:
                print("   %s" % p.relative_to(DATA))
            return 1
        print("\nOK: every DDS carries a mip chain")
        return 0

    if not todo:
        print("\nnothing to do")
        return 0

    print()
    converted, failed = convert(todo, args.dry_run)

    if args.dry_run:
        print("\ndry run: %d file(s) would be converted" % converted)
        print("originals would be backed up under %s" % BACKUP)
        return 0

    print("\nconverted %d file(s)" % converted)
    for rel, fmt, msg in failed:
        print("  FAILED %-16s %s: %s" % (fmt, rel, msg))

    bad = verify_converted(todo)
    if bad:
        print("\nVERIFY FAILED on %d file(s):" % len(bad))
        for p, why in bad[:20]:
            print("   %-58s %s" % (p.relative_to(DATA), why))
        print("\nrun with --restore to put the originals back")
        return 1

    dark, checked = verify_brightness(todo)
    if dark:
        print("\nVERIFY FAILED: generated mips are too dark on %d of %d sampled file(s):"
              % (len(dark), checked))
        for p, why in dark[:10]:
            print("   %-52s %s" % (p.relative_to(DATA), why))
        print("\nrun with --restore to put the originals back")
        return 1

    print("verified: all %d file(s) now carry a mip chain, "
          "with unchanged dimensions and a DX9 header" % len(todo))
    if checked:
        print("verified: mip brightness matches mip0 on %d sampled file(s)" % checked)
    else:
        print("NOTE: mip brightness was NOT sampled (no block-format files in this "
              "batch, or Pillow missing) -- run --verify to check the whole set")
    print("\nRe-bake the VFS, then check rose*.vfs against the 2 GB limit.")
    return 0 if not failed else 1


if __name__ == "__main__":
    sys.exit(main())
