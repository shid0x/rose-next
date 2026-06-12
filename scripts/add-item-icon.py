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
    with open(path, "rb") as fh:
        d = fh.read()
    assert d[:4] == b"DDS " and len(d) == 128 + SHEET_SIZE * SHEET_SIZE * 4, \
        "%s is not one of our uncompressed extension sheets" % path
    bgra = Image.frombytes("RGBA", (SHEET_SIZE, SHEET_SIZE), d[128:])
    c0, c1, c2, c3 = bgra.split()  # file bytes are B,G,R,A -> swap back
    return Image.merge("RGBA", (c2, c1, c0, c3))

# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("png", help="source image (any size; downscaled to 40x40)")
    ap.add_argument("--name", help="sprite label stored in the TSI (default: png filename)")
    ap.add_argument("--weapon-row", type=int, help="also set LIST_WEAPON.STB icon col of this row")
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
    dds_write(os.path.join(RES_DIR, sheet_name), sheet, args.dry_run)

    label = args.name or os.path.splitext(os.path.basename(args.png))[0]
    if new_sheet:
        textures.append(texture_entry(sheet_name))
        blocks.append((1, sprite_entry(len(textures) - 1, cell, label)))
    else:
        cnt, raw = blocks[-1]
        blocks[-1] = (cnt + 1, raw + sprite_entry(len(textures) - 1, cell, label))

    bak = TSI + ".bak-icons"
    if not args.dry_run and not os.path.exists(bak):
        shutil.copy2(TSI, bak)
    tsi_write(TSI, textures, blocks, args.dry_run)

    new_index = total_before
    print("icon added: sheet=%s cell=%d (x=%d y=%d)  ->  icon index %d" % (sheet_name, cell, x, y, new_index))

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
