"""Regenerate missing mipmap chains on imported textures.

Why
---
Textures ported from another ROSE data set can arrive with a **single** mip level.
Ours never do: every one of the 691 pre-existing `3DDATA/NPC` textures ships a full
chain. The engine honours whatever the file declares at most quality settings, so a
one-level texture is sampled at level 0 no matter how far away it is:

    zz_renderer_d3d.cpp, download_texture()
        else if ((miplevels < 0) && (state.mipmap_level < 0))
                miplevels = image_info.MipLevels;      // <- the file's own count
        else if (miplevels < 0)
                miplevels = state.mipmap_level;        // <- D3DX generates instead

and `zz_render_state.cpp` sets `mipmap_level = -1` for quality levels 3, 4 and 5,
`3` for level 2. So at medium and below the chain is never generated and minified
surfaces alias -- large smooth areas (a robe, a cloak) read as a dark speckled
patch that snaps back to normal as you walk up to it. At "high" the same texture
looks fine, which makes it easy to mistake for a driver quirk.

The Oro import (`import-oro.py`) brought in 66 such textures; the repro was
[Royal Vizier] Kaltet XIV's lower body.

What it does
------------
Runs `thirdparty/directxtex-2020.9.30/texconv.exe` over every texture that declares
one mip level but is big enough to need a chain, regenerating it **in the same
format and at the same dimensions** with a full chain (`-m 0`). Files that already
have a chain are skipped, so it is idempotent and safe to re-run.

    python scripts/fix-texture-mipmaps.py --dry-run
    python scripts/fix-texture-mipmaps.py
    python scripts/fix-texture-mipmaps.py --root data/3DDATA/NPC

Makes a `.bak` beside each rewritten file and verifies the result by re-reading the
header (format, size and mip count must all be as intended). Note `data/` is
gitignored, so this script is the only committed record of the change -- and note
`pack.rs` bakes stray `.bak` files into the VFS, so clean them before a bake.

DDS header fields used (little-endian, after the 4-byte "DDS " magic):
    +4  dwSize      +8  dwFlags     +12 dwHeight   +16 dwWidth
    +24 dwDepth     +28 dwMipMapCount           (DDSD_MIPMAPCOUNT = 0x20000)
    +84 ddspf.dwFourCC          +88 ddspf.dwRGBBitCount
"""
import argparse, os, shutil, struct, subprocess, sys, tempfile

TEXCONV = os.path.join("thirdparty", "directxtex-2020.9.30", "texconv.exe")
DDSD_MIPMAPCOUNT = 0x20000
MIN_SIZE = 64                      # below this a chain buys nothing

# FourCC -> texconv -f argument. Uncompressed files keep a straight BGRA format.
FOURCC_FORMAT = {b"DXT1": "DXT1", b"DXT3": "DXT3", b"DXT5": "DXT5"}


def dds_header(path):
    """(width, height, declared_mips, fourcc, rgbbits) or None if not a DDS."""
    try:
        with open(path, "rb") as fh:
            b = fh.read(128)
    except OSError:
        return None
    if len(b) < 128 or b[:4] != b"DDS ":
        return None
    _, flags, h, w = struct.unpack_from("<4I", b, 4)
    mips, = struct.unpack_from("<I", b, 4 + 24)
    fourcc, rgbbits = struct.unpack_from("<4sI", b, 4 + 80)
    return w, h, (mips if (flags & DDSD_MIPMAPCOUNT) else 1), fourcc, rgbbits


def full_chain_length(w, h):
    n, t = 1, max(w, h)
    while t > 1:
        t //= 2
        n += 1
    return n


def needs_mips(info):
    w, h, mips, fourcc, _ = info
    return mips <= 1 and max(w, h) > MIN_SIZE and full_chain_length(w, h) > 1


def convert(path, info, texconv, dry):
    w, h, _, fourcc, rgbbits = info
    fmt = FOURCC_FORMAT.get(fourcc, "B8G8R8A8_UNORM")
    if dry:
        return fmt, None
    # texconv mirrors the input path under -o, so give it a flat scratch dir
    with tempfile.TemporaryDirectory() as tmp:
        src = os.path.join(tmp, os.path.basename(path))
        out = os.path.join(tmp, "out")
        os.makedirs(out)
        shutil.copyfile(path, src)
        r = subprocess.run([texconv, "-nologo", "-y", "-dx9",
                            "-f", fmt, "-m", "0", "-o", out, src],
                           capture_output=True, text=True)
        produced = [os.path.join(out, f) for f in os.listdir(out)]
        if r.returncode != 0 or not produced:
            return fmt, f"texconv failed: {(r.stdout + r.stderr).strip()[:160]}"
        new = produced[0]
        after = dds_header(new)
        if after is None:
            return fmt, "texconv produced something that is not a DDS"
        nw, nh, nmips, nfourcc, _ = after
        if (nw, nh) != (w, h):
            return fmt, f"size changed {w}x{h} -> {nw}x{nh}"
        if nfourcc != fourcc and fourcc in FOURCC_FORMAT:
            return fmt, f"format changed {fourcc!r} -> {nfourcc!r}"
        if nmips < full_chain_length(w, h):
            return fmt, f"still only {nmips} mips (wanted {full_chain_length(w, h)})"
        if not os.path.exists(path + ".bak"):
            shutil.copyfile(path, path + ".bak")
        shutil.copyfile(new, path)
    return fmt, None


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    # Scoped to character models on purpose. UI art (3DDATA/CONTROL/RES) is drawn
    # 1:1 as sprites and is *meant* to have a single level -- the engine forces
    # miplevels = 1 for image textures anyway (`tex->get_for_image()`), so
    # rebuilding those would be churn with no effect. Widen with --root knowingly.
    ap.add_argument("--root", default=os.path.join("data", "3DDATA", "NPC"),
                    help="directory to scan (default: data/3DDATA/NPC)")
    ap.add_argument("--dry-run", action="store_true", help="preview without writing")
    ap.add_argument("--texconv", default=TEXCONV)
    args = ap.parse_args()

    if not os.path.isdir(args.root):
        raise SystemExit(f"not found: {args.root}")
    if not args.dry_run and not os.path.isfile(args.texconv):
        raise SystemExit(f"texconv not found: {args.texconv}")

    todo = []
    scanned = 0
    for base, _, names in os.walk(args.root):
        for n in names:
            if not n.lower().endswith(".dds"):
                continue
            p = os.path.join(base, n)
            info = dds_header(p)
            if info is None:
                continue
            scanned += 1
            if needs_mips(info):
                todo.append((p, info))

    print(f"scanned {scanned} DDS files under {args.root}")
    print(f"{len(todo)} declare a single mip level and are larger than {MIN_SIZE}px\n")
    if not todo:
        print("nothing to do")
        return 0

    done, failed = 0, []
    for p, info in sorted(todo):
        w, h, _, fourcc, _ = info
        fmt, err = convert(p, info, args.texconv, args.dry_run)
        rel = os.path.relpath(p, args.root).replace("\\", "/")
        if err:
            failed.append((rel, err))
            print(f"  FAIL {rel:56s} {err}")
        else:
            done += 1
            if args.dry_run:
                print(f"  would rebuild {rel:50s} {w}x{h} {fmt} "
                      f"-> {full_chain_length(w, h)} mips")

    if args.dry_run:
        print(f"\ndry run: {done} files would be rebuilt")
        return 0

    print(f"\nrebuilt {done} file(s), {len(failed)} failed; .bak backups alongside")
    remaining = [p for p, _ in todo if needs_mips(dds_header(p))]
    print(f"verify: {len(remaining)} of them still lack a chain"
          + (f" -> {remaining[:3]}" if remaining else ""))
    return 1 if failed or remaining else 0


if __name__ == "__main__":
    sys.exit(main())
