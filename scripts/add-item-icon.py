"""Add a custom item icon to the ITEM1.TSI atlas from a PNG.

Usage:
    python scripts/add-item-icon.py monarchicon.png --weapon-row 1354
    python scripts/add-item-icon.py art.png --name my_icon --dry-run

What it does:
    1. Downscales the PNG to a 40x40 RGBA cell (any input size works)
    2. Writes it into an extension icon sheet (icon51.dds, icon52.dds, ... --
       512x512 uncompressed BGRA DDS, 13x13 grid of 40x40 cells). Extends the
       newest extension sheet until its 169 cells are full, then starts a new one.
    3. Appends a sprite entry to 3DDATA/CONTROL/RES/ITEM1.TSI; the new sprite's
       global index (printed) is what goes into an item STB icon column
    4. With --weapon-row N, also patches LIST_WEAPON.STB col 9 of that row

Atlas facts: original data ships icon01-icon50.dds (DXT5) with 169 sprites each
= indices 0-8449; extension sheets continue from 8450. The sprite list in the
TSI is ordered by sheet, so appending to the LAST sheet keeps all existing
indices stable. Icon-only changes need a client restart, not a server restart
(unless the STB was patched too -- then restart the game server as well).
"""
import argparse, io, os, struct, shutil, sys

from PIL import Image

RES_DIR = os.path.join("data", "3DDATA", "CONTROL", "RES")
TSI = os.path.join(RES_DIR, "ITEM1.TSI")
SHEET_SIZE = 512
CELL = 40
GRID = 13                    # 13x13 cells per sheet
CELLS_PER_SHEET = GRID * GRID
FIRST_EXT_SHEET = 51         # original data uses icon01..icon50

# ---------------------------------------------------------------- TSI
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
    return struct.pack("<h4iI", texid, x, y, x + CELL, y + CELL, 0) + sid

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
    """Read the top mip of one of our uncompressed extension sheets.

    Validate the pixel format from the header rather than from the file length.
    An earlier version asserted `len(d) == 128 + SHEET_SIZE**2 * 4`, which broke
    the moment something regenerated icon51.dds with a mip chain (10 levels, so
    1,398,228 bytes instead of 1,048,704) -- and since add_icon() fills the last
    extension sheet before starting a new one, that blocked *every* new item
    icon, from a PNG or from another data set's atlas. The trailing mips are
    simply ignored; we only ever composite into level 0.

    Note dds_write() still emits no mip chain, which is what this script has
    always done and what the client has always been shipped. Mips are pointless
    for a 40px-cell icon atlas drawn at 1:1, and generating them would bleed
    neighbouring cells into each other at the low levels.
    """
    with open(path, "rb") as fh:
        d = fh.read()
    if d[:4] != b"DDS " or len(d) < 128:
        sys.exit("%s is not a DDS file" % path)
    h, w = struct.unpack_from("<II", d, 12)
    pf_flags, fourcc, bitcount = struct.unpack_from("<I4sI", d, 80)
    top = SHEET_SIZE * SHEET_SIZE * 4
    if (w, h) != (SHEET_SIZE, SHEET_SIZE) or bitcount != 32 \
            or not (pf_flags & 0x41) or fourcc != b"\x00\x00\x00\x00" \
            or len(d) < 128 + top:
        sys.exit("%s is not one of our uncompressed %dx%d BGRA extension sheets "
                 "(got %dx%d, fourcc %r, %d bpp)"
                 % (path, SHEET_SIZE, SHEET_SIZE, w, h, fourcc, bitcount))
    bgra = Image.frombytes("RGBA", (SHEET_SIZE, SHEET_SIZE), d[128:128 + top])
    c0, c1, c2, c3 = bgra.split()  # file bytes are B,G,R,A -> swap back
    return Image.merge("RGBA", (c2, c1, c0, c3))

# ---------------------------------------------------------------- main
def add_icon(art, label, dry):
    """Append one 40x40 icon to our atlas; returns its global sprite index.

    Takes a PIL image rather than a path so callers that already hold the art
    (e.g. import-item.py cropping it out of another data set's atlas) don't have
    to round-trip through a temp file. Importantly this also means the caller
    gets the index back as a value -- the earlier pattern of scraping it from
    stdout breaks the moment the wording changes.
    """
    if not os.path.exists(TSI):
        sys.exit("run from the repo root (%s not found)" % TSI)

    art = art.convert("RGBA")
    if art.size != (CELL, CELL):
        print("resizing %dx%d -> %dx%d" % (*art.size, CELL, CELL))
        art = art.resize((CELL, CELL), Image.LANCZOS)

    textures, blocks = tsi_read(TSI)
    total_before = sum(c for c, _ in blocks)

    # extend the last extension sheet if it has room, else start a new one
    last_name = textures[-1][0].lower()
    ext_nums = [int(n[4:-4]) for n, _ in textures
                if n.lower().startswith("icon") and n.lower().endswith(".dds")
                and n[4:-4].isdigit() and int(n[4:-4]) >= FIRST_EXT_SHEET]
    if ext_nums and last_name == "icon%02d.dds" % max(ext_nums) and blocks[-1][0] < CELLS_PER_SHEET:
        sheet_name = textures[-1][0]
        cell = blocks[-1][0]
        sheet = dds_read_bgra(os.path.join(RES_DIR, sheet_name))
        new_sheet = False
    else:
        num = max(ext_nums) + 1 if ext_nums else FIRST_EXT_SHEET
        sheet_name = "icon%02d.dds" % num
        cell = 0
        sheet = Image.new("RGBA", (SHEET_SIZE, SHEET_SIZE), (0, 0, 0, 0))
        new_sheet = True

    x, y = (cell % GRID) * CELL, (cell // GRID) * CELL
    sheet.paste(art, (x, y))
    dds_write(os.path.join(RES_DIR, sheet_name), sheet, dry)

    if new_sheet:
        textures.append(texture_entry(sheet_name))
        blocks.append((1, sprite_entry(len(textures) - 1, cell, label)))
    else:
        cnt, raw = blocks[-1]
        blocks[-1] = (cnt + 1, raw + sprite_entry(len(textures) - 1, cell, label))

    bak = TSI + ".bak-icons"
    if not dry and not os.path.exists(bak):
        shutil.copy2(TSI, bak)
    tsi_write(TSI, textures, blocks, dry)

    new_index = total_before
    print("icon added: sheet=%s cell=%d (x=%d y=%d)  ->  icon index %d" % (sheet_name, cell, x, y, new_index))
    return new_index


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("png", help="source image (any size; downscaled to 40x40)")
    ap.add_argument("--name", help="sprite label stored in the TSI (default: png filename)")
    ap.add_argument("--weapon-row", type=int, help="also set LIST_WEAPON.STB icon col of this row")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    label = args.name or os.path.splitext(os.path.basename(args.png))[0]
    new_index = add_icon(Image.open(args.png), label, args.dry_run)

    if args.weapon_row is not None:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        imp = __import__("import-weapon")
        stb_path = os.path.join("data", imp.STB_REL)
        d, offset, rows, cols, data = imp.stb_read(stb_path)
        if args.weapon_row >= rows - 1:
            sys.exit("weapon row %d out of range" % args.weapon_row)
        old = data[args.weapon_row][9]
        data[args.weapon_row][9] = str(new_index).encode("ascii")
        cells = b"".join(struct.pack("<H", len(c)) + c for r in data for c in r)
        if not args.dry_run:
            sbak = stb_path + ".bak-icons"
            if not os.path.exists(sbak):
                shutil.copy2(stb_path, sbak)
            with open(stb_path, "wb") as fh:
                fh.write(d[:offset] + cells)
        print("LIST_WEAPON.STB row %d icon: %s -> %d (server restart needed)" % (args.weapon_row, old.decode() or "''", new_index))

    # verify
    if not args.dry_run:
        vtex, vblocks = tsi_read(TSI)
        assert sum(c for c, _ in vblocks) == total_before + 1
        vsheet = dds_read_bgra(os.path.join(RES_DIR, sheet_name))
        assert vsheet.crop((x, y, x + CELL, y + CELL)).tobytes() == art.tobytes(), "sheet pixels mismatch"
        print("verified: TSI has %d sprites, sheet pixels match the source art" % (total_before + 1))
    else:
        print("DRY RUN - no files written")

if __name__ == "__main__":
    main()
