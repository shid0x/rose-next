"""Add a custom skill icon to the SKILLICON.TSI atlas from a PNG.

Usage:
    python scripts/add-skill-icon.py inspector.png --skill-row 7001
    python scripts/add-skill-icon.py art.png --name my_icon --dry-run

What it does:
    1. Downscales the PNG to a 40x40 RGBA cell (any input size works)
    2. Writes it into an extension icon sheet (skill04.dds, skill05.dds, ... --
       512x512 uncompressed BGRA DDS, 13x13 grid of 40x40 cells). Extends the
       newest extension sheet until its 169 cells are full, then starts a new one.
    3. Appends a sprite entry to 3DDATA/CONTROL/RES/SKILLICON.TSI; the new
       sprite's global index (printed) is what goes into LIST_SKILL.STB col 51
       (SKILL_ICON_NO)
    4. With --skill-row N, also patches LIST_SKILL.STB col 51 of that row

Atlas facts: original data ships skill01-skill03.dds with 169 sprites each
= indices 0-506; extension sheets continue from 507. The sprite list in the
TSI is ordered by sheet, so appending to the LAST sheet keeps all existing
indices stable. Sprite rects in SKILLICON.TSI use the x..x+39 convention of
the original sheets (ITEM1.TSI uses x..x+40); we match the skill convention.

After running: rebake/deploy client data (TSI + DDS are client-side). If the
STB was patched, restart the game server too (it reads SKILL_ICON_NO for the
/add skill cheat).
"""
import argparse, io, os, struct, shutil, subprocess, sys

from PIL import Image

RES_DIR = os.path.join("data", "3DDATA", "CONTROL", "RES")
# Backups live OUTSIDE data/: src/pipeline/src/pack.rs walks the data tree
# filtering only *hidden* entries and applies no extension filter, so a .bak
# left beside the atlas gets baked into the .vfs. Same rule as
# add-dds-mipmaps.py, which uses build/dds-mipmap-backup/.
BACKUP_DIR = os.path.join("build", "skill-icon-backup")
TSI = os.path.join(RES_DIR, "SKILLICON.TSI")
SKILL_STB = os.path.join("data", "3DDATA", "STB", "LIST_SKILL.STB")
SKILL_ICON_COL = 51          # game col (SKILL_ICON_NO)
SHEET_SIZE = 512
CELL = 40
GRID = 13                    # 13x13 cells per sheet
CELLS_PER_SHEET = GRID * GRID
FIRST_EXT_SHEET = 4          # original data uses skill01..skill03
SHEET_FMT = "skill%02d.dds"

# ---------------------------------------------------------------- TSI
def backup_once(path):
    """Copy path under BACKUP_DIR the first time only, preserving its name."""
    dst = os.path.join(BACKUP_DIR, os.path.basename(path))
    if not os.path.exists(dst):
        os.makedirs(BACKUP_DIR, exist_ok=True)
        shutil.copy2(path, dst)
        print("  backed up %s -> %s" % (os.path.basename(path), dst))


def tsi_read(path):
    with open(path, "rb") as fh:
        f = io.BytesIO(fh.read())
    ntex, = struct.unpack("<h", f.read(2))
    textures = []  # (name, raw_entry_bytes)
    for _ in range(ntex):
        start = f.tell()
        nlen, = struct.unpack("<h", f.read(2))
        name = f.read(nlen).split(b"\x00")[0].decode("ascii")
        f.read(4)  # colorkey
        f.seek(start)
        raw = f.read(2 + nlen + 4)
        textures.append((name, raw))
    total, = struct.unpack("<h", f.read(2))
    blocks = []  # (count, raw_block_bytes)
    for _ in range(ntex):
        cnt, = struct.unpack("<h", f.read(2))
        blocks.append((cnt, f.read(cnt * 54)))
    assert f.read() == b"", "trailing bytes in TSI"
    assert total == sum(c for c, _ in blocks), "TSI sprite count mismatch"
    return textures, blocks

def tsi_write(path, textures, blocks, dry):
    assert len(textures) == len(blocks)
    out = [struct.pack("<h", len(textures))]
    out += [raw for _, raw in textures]
    out.append(struct.pack("<h", sum(c for c, _ in blocks)))
    for cnt, raw in blocks:
        out.append(struct.pack("<h", cnt))
        out.append(raw)
    if not dry:
        with open(path, "wb") as fh:
            fh.write(b"".join(out))

def texture_entry(name):
    nb = name.encode("ascii") + b"\x00"
    return (name, struct.pack("<h", len(nb)) + nb + struct.pack("<I", 0))

def sprite_entry(texid, cell, label):
    x, y = (cell % GRID) * CELL, (cell // GRID) * CELL
    sid = label.encode("ascii", "replace")[:31].ljust(32, b"\x00")
    # SKILLICON.TSI convention: right/bottom = +39 (matches skill01..03 sheets)
    return struct.pack("<h4iI", texid, x, y, x + CELL - 1, y + CELL - 1, 0) + sid

# ---------------------------------------------------------------- DDS (uncompressed BGRA)
def dds_write(path, img, dry):
    assert img.size == (SHEET_SIZE, SHEET_SIZE) and img.mode == "RGBA"
    header = struct.pack(
        "<4sIIIIIII44xIIIIIIIIII12x",
        b"DDS ", 124,
        0x0000100F,                 # CAPS|HEIGHT|WIDTH|PITCH|PIXELFORMAT
        SHEET_SIZE, SHEET_SIZE,
        SHEET_SIZE * 4, 0, 0,       # pitch, depth, mipcount
        32, 0x41, 0, 32,            # pf: size, RGB|ALPHAPIXELS, fourcc, bitcount
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000,
        0x1000, 0)                  # caps: TEXTURE, caps2; +12 pad = 128-byte header
    assert len(header) == 128, len(header)
    b, g, r, a = (img.getchannel(c) for c in ("B", "G", "R", "A"))
    pixels = Image.merge("RGBA", (b, g, r, a)).tobytes()  # BGRA byte order
    if not dry:
        with open(path, "wb") as fh:
            fh.write(header + pixels)

def dds_read_bgra(path):
    """Read mip 0 of one of our uncompressed extension sheets.

    Tolerates a mip chain. scripts/add-dds-mipmaps.py gave every DDS under
    data/ a chain -- a real load-time win, since D3DX building one at runtime
    cost 1300 ms across a single session -- so a sheet this script wrote bare
    does not stay bare. Demanding exactly the base level made the script fail
    on its own output once that pass had run.
    """
    with open(path, "rb") as fh:
        d = fh.read()
    base = SHEET_SIZE * SHEET_SIZE * 4
    assert d[:4] == b"DDS " and len(d) >= 128 + base, \
        "%s is not one of our uncompressed extension sheets" % path
    h, w = struct.unpack_from("<II", d, 12)
    mips, = struct.unpack_from("<I", d, 28)
    fourcc, bits = struct.unpack_from("<4sI", d, 84)
    assert (w, h) == (SHEET_SIZE, SHEET_SIZE) and fourcc == bytes(4) and bits == 32, \
        "%s: expected 512x512 uncompressed 32-bit (mips=%d fourcc=%r bits=%d)" % \
        (path, mips, fourcc, bits)
    bgra = Image.frombytes("RGBA", (SHEET_SIZE, SHEET_SIZE), d[128:128 + base])
    c0, c1, c2, c3 = bgra.split()  # file bytes are B,G,R,A -> swap back
    return Image.merge("RGBA", (c2, c1, c0, c3))

TEXCONV = os.path.join("thirdparty", "directxtex-2020.9.30", "texconv.exe")

def restore_mipmaps(path, dry):
    """Re-add the mip chain dds_write() drops, matching add-dds-mipmaps.py.

    dds_write emits mip 0 only. Leaving it that way silently undoes the
    mipmap pass for this atlas and puts it back on the slow runtime path
    (D3DX regenerating a chain on every client, on every load).

    The flags are not interchangeable -- see add-dds-mipmaps.py:
      -dx9   D3DX9 cannot read a DX10 extended header; the client would
             draw the icons untextured rather than report an error.
      -nowic WIC darkens RGB when downsampling (36-71% measured), which
             shows up only in the lower mips as a wash at distance.
      -if BOX  a strict 2x2 box is what D3DX produced at runtime before,
             so the visual result is unchanged.
    """
    if dry:
        print("  would restore the mip chain on %s" % os.path.basename(path))
        return
    if not os.path.exists(TEXCONV):
        print("  WARNING: texconv not found -- %s ships with no mip chain, so the"
              " client rebuilds one at load. Re-run scripts/add-dds-mipmaps.py."
              % os.path.basename(path))
        return
    cmd = [TEXCONV, "-nologo", "-y", "-dx9", "-m", "0", "-nowic",
           "-if", "BOX", "-f", "B8G8R8A8_UNORM",
           "-o", os.path.dirname(path), path]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        sys.exit("texconv failed on %s:\n%s" % (path, (r.stdout or r.stderr).strip()))
    n, = struct.unpack_from("<I", open(path, "rb").read(32), 28)
    print("  mip chain restored on %s (%d levels)" % (os.path.basename(path), n))


# ---------------------------------------------------------------- STB (in-place cell patch)
def stb_read(path):
    with open(path, "rb") as fh:
        d = fh.read()
    f = io.BytesIO(d)
    f.read(4)
    offset, rows, cols = struct.unpack("<III", f.read(12))
    f.seek(offset)
    def pstr():
        n, = struct.unpack("<H", f.read(2))
        return f.read(n)
    data = [[pstr() for _ in range(cols - 1)] for _ in range(rows - 1)]
    return d, offset, rows, cols, data

# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("png", help="source image (any size; downscaled to 40x40)")
    ap.add_argument("--name", help="sprite label stored in the TSI (default: png filename)")
    ap.add_argument("--skill-row", action="append", default=[], metavar="N|A-B",
                    help="also set the LIST_SKILL.STB icon col of this row; repeatable, "
                         "and accepts an inclusive range. A skill has one row per rank and "
                         "each carries its own icon, so a 10-rank skill needs all ten.")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not os.path.exists(TSI):
        sys.exit("run from the repo root (%s not found)" % TSI)

    art = Image.open(args.png).convert("RGBA")
    if art.size != (CELL, CELL):
        print("resizing %dx%d -> %dx%d" % (*art.size, CELL, CELL))
        art = art.resize((CELL, CELL), Image.LANCZOS)

    textures, blocks = tsi_read(TSI)
    total_before = sum(c for c, _ in blocks)

    # extend the last extension sheet if it has room, else start a new one
    last_name = textures[-1][0].lower()
    ext_nums = [int(n[5:-4]) for n, _ in textures
                if n.lower().startswith("skill") and n.lower().endswith(".dds")
                and n[5:-4].isdigit() and int(n[5:-4]) >= FIRST_EXT_SHEET]
    if ext_nums and last_name == SHEET_FMT % max(ext_nums) and blocks[-1][0] < CELLS_PER_SHEET:
        sheet_name = textures[-1][0]
        cell = blocks[-1][0]
        sheet = dds_read_bgra(os.path.join(RES_DIR, sheet_name))
        new_sheet = False
    else:
        num = max(ext_nums) + 1 if ext_nums else FIRST_EXT_SHEET
        sheet_name = SHEET_FMT % num
        cell = 0
        sheet = Image.new("RGBA", (SHEET_SIZE, SHEET_SIZE), (0, 0, 0, 0))
        new_sheet = True

    x, y = (cell % GRID) * CELL, (cell // GRID) * CELL
    sheet.paste(art, (x, y))
    dds_write(os.path.join(RES_DIR, sheet_name), sheet, args.dry_run)

    label = args.name or os.path.splitext(os.path.basename(args.png))[0]
    if new_sheet:
        textures.append(texture_entry(sheet_name))
        blocks.append((1, sprite_entry(len(textures) - 1, cell, label)))
    else:
        cnt, raw = blocks[-1]
        blocks[-1] = (cnt + 1, raw + sprite_entry(len(textures) - 1, cell, label))

    if not args.dry_run:
        backup_once(TSI)
    tsi_write(TSI, textures, blocks, args.dry_run)

    new_index = total_before
    print("icon added: sheet=%s cell=%d (x=%d y=%d)  ->  skill icon index %d" % (sheet_name, cell, x, y, new_index))

    targets = []
    for spec in args.skill_row:
        lo, _, hi = spec.partition("-")
        try:
            lo = int(lo); hi = int(hi) if hi else lo
        except ValueError:
            sys.exit("--skill-row wants N or A-B, got %r" % spec)
        if hi < lo:
            sys.exit("--skill-row range %r is backwards" % spec)
        targets.extend(range(lo, hi + 1))
    targets = sorted(set(targets))

    if targets:
        d, offset, rows, cols, data = stb_read(SKILL_STB)
        for row in targets:
            if row >= rows - 1:
                sys.exit("skill row %d out of range (table has %d)" % (row, rows - 1))
        olds = [data[r][SKILL_ICON_COL] for r in targets]
        for row in targets:
            data[row][SKILL_ICON_COL] = str(new_index).encode("ascii")
        cells = b"".join(struct.pack("<H", len(c)) + c for r in data for c in r)
        if not args.dry_run:
            backup_once(SKILL_STB)
            with open(SKILL_STB, "wb") as fh:
                fh.write(d[:offset] + cells)
        before = sorted({o.decode().strip() or "''" for o in olds})
        print("LIST_SKILL.STB rows %d-%d (%d): icon %s -> %d (restart the server)"
              % (targets[0], targets[-1], len(targets), "/".join(before), new_index))

    # verify
    if not args.dry_run:
        vtex, vblocks = tsi_read(TSI)
        assert sum(c for c, _ in vblocks) == total_before + 1
        sheet_path = os.path.join(RES_DIR, sheet_name)
        vsheet = dds_read_bgra(sheet_path)
        assert vsheet.crop((x, y, x + CELL, y + CELL)).tobytes() == art.tobytes(), "sheet pixels mismatch"
        restore_mipmaps(sheet_path, False)
        # texconv rewrites the file, so re-check mip 0 survived the round trip.
        vsheet = dds_read_bgra(sheet_path)
        assert vsheet.crop((x, y, x + CELL, y + CELL)).tobytes() == art.tobytes(), \
            "mip 0 changed when the chain was rebuilt"
        print("verified: TSI has %d sprites, sheet pixels match the source art" % (total_before + 1))
    else:
        print("DRY RUN - no files written")

if __name__ == "__main__":
    main()
