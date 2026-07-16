"""Add the NPC overhead quest-emoticon sprites to the UI atlas.

Usage:
    python scripts/add-quest-emoticons.py            # write (with .bak backups)
    python scripts/add-quest-emoticons.py --dry-run
    python scripts/add-quest-emoticons.py --icons available.png ing.png complete.png

What it does:
    1. Renders (or loads via --icons) three 32x32 icons and writes them into
       3DDATA/CONTROL/RES/QUESTEMOTICON.DDS (128x32 uncompressed BGRA; cells
       left-to-right: POSSIBLE "!", ING gray "?", COMPLETE "?")
    2. Appends the texture + a 3-sprite block to 3DDATA/CONTROL/RES/Ui.TSI
       (module 0 = IMAGE_RES_UI). Existing sprite indices are untouched because
       the new texture block is appended last.
    3. Upserts QUEST_EMOTICON_POSSIBLE / _ING / _COMPLETE into
       3DDATA/CONTROL/XML/UI_strID.ID with the new global sprite indices.

The client draws these from CNameBox::DrawNpcName when CObjNPC::m_nQuestSignal
is 1 (quest available), 2 (in progress; currently never set) or 3 (ready to
turn in). Re-running is idempotent: if QUESTEMOTICON.DDS is already the last
texture in the TSI its block is rewritten in place. To swap in real art later,
either replace QUESTEMOTICON.DDS (same 128x32 BGRA layout) or re-run with
--icons a.png b.png c.png.

TSI format: [i16 texCount][per tex: i16 nameLen, name(nameLen incl NUL),
u32 colorkey][i16 totalSpriteCount][per tex: i16 count, count * 54-byte sprite
{i16 texId, RECT(4*i32), u32 color, char id[32]}]. Global sprite index = position
in block order; UI_strID.ID maps NAME -> index (whitespace-separated lines).
"""
import argparse, io, os, shutil, struct, sys

from PIL import Image, ImageDraw, ImageFont

RES_DIR = os.path.join("data", "3DDATA", "CONTROL", "RES")
XML_DIR = os.path.join("data", "3DDATA", "CONTROL", "XML")
TSI = os.path.join(RES_DIR, "Ui.TSI")
STRID = os.path.join(XML_DIR, "UI_strID.ID")
DDS_NAME = "QUESTEMOTICON.DDS"
CELL = 32
TEX_W, TEX_H = 128, 32

SPRITES = [  # (name, glyph, fill) in cell order
    ("QUEST_EMOTICON_POSSIBLE", "!", (255, 216, 0, 255)),
    ("QUEST_EMOTICON_ING", "?", (168, 168, 168, 255)),
    ("QUEST_EMOTICON_COMPLETE", "?", (255, 216, 0, 255)),
]


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
        textures.append((name, f.read(2 + nlen + 4)))
    total, = struct.unpack("<h", f.read(2))
    blocks = []  # (count, raw_block_bytes)
    for _ in range(ntex):
        cnt, = struct.unpack("<h", f.read(2))
        blocks.append((cnt, f.read(cnt * 54)))
    assert f.read() == b"", "trailing bytes in TSI"
    # Retail Ui.TSI's total field is wrong (690 vs 688 actual). The client only
    # uses it for a vector reserve and fills sprites from the per-texture
    # counts, so global sprite indices come from the real block sums.
    if total != sum(c for c, _ in blocks):
        print("note: TSI total field %d != actual sprites %d (retail quirk; "
              "rewriting with the actual count)" % (total, sum(c for c, _ in blocks)))
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
    x = cell * CELL
    sid = label.encode("ascii")[:31].ljust(32, b"\x00")
    return struct.pack("<h4iI", texid, x, 0, x + CELL, CELL, 0) + sid


# ---------------------------------------------------------------- DDS (uncompressed BGRA)
def dds_write(path, img, dry):
    assert img.size == (TEX_W, TEX_H) and img.mode == "RGBA"
    header = struct.pack(
        "<4sIIIIIII44xIIIIIIIIII12x",
        b"DDS ", 124,
        0x0000100F,                 # CAPS|HEIGHT|WIDTH|PITCH|PIXELFORMAT
        TEX_H, TEX_W,
        TEX_W * 4, 0, 0,            # pitch, depth, mipcount
        32, 0x41, 0, 32,            # pf: size, RGB|ALPHAPIXELS, fourcc, bitcount
        0x00FF0000, 0x0000FF00, 0x000000FF, 0xFF000000,
        0x1000, 0)                  # caps: TEXTURE
    assert len(header) == 128, len(header)
    b, g, r, a = (img.getchannel(c) for c in ("B", "G", "R", "A"))
    pixels = Image.merge("RGBA", (b, g, r, a)).tobytes()
    if not dry:
        with open(path, "wb") as fh:
            fh.write(header + pixels)


# ---------------------------------------------------------------- placeholder art
def find_font():
    for name in ("arialbd.ttf", "arial.ttf", "seguisb.ttf"):
        p = os.path.join(os.environ.get("WINDIR", r"C:\Windows"), "Fonts", name)
        if os.path.exists(p):
            return p
    return None


def render_glyph(glyph, fill):
    img = Image.new("RGBA", (CELL, CELL), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    font_path = find_font()
    font = ImageFont.truetype(font_path, 30) if font_path else ImageFont.load_default()
    l, t, r, b = d.textbbox((0, 0), glyph, font=font)
    pos = ((CELL - (r - l)) // 2 - l, (CELL - (b - t)) // 2 - t)
    for dx in (-2, -1, 0, 1, 2):        # dark outline for readability in-world
        for dy in (-2, -1, 0, 1, 2):
            if dx or dy:
                d.text((pos[0] + dx, pos[1] + dy), glyph, font=font, fill=(0, 0, 0, 200))
    d.text(pos, glyph, font=font, fill=fill)
    return img


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--icons", nargs=3, metavar=("AVAILABLE", "ING", "COMPLETE"),
                    help="use these images (resized to 32x32) instead of rendered glyphs")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    for p in (TSI, STRID):
        if not os.path.exists(p):
            sys.exit("not found: %s (run from the repo root)" % p)

    # 1. texture
    sheet = Image.new("RGBA", (TEX_W, TEX_H), (0, 0, 0, 0))
    for i, (name, glyph, fill) in enumerate(SPRITES):
        cell = (Image.open(args.icons[i]).convert("RGBA").resize((CELL, CELL), Image.LANCZOS)
                if args.icons else render_glyph(glyph, fill))
        sheet.paste(cell, (i * CELL, 0))
    dds_write(os.path.join(RES_DIR, DDS_NAME), sheet, args.dry_run)
    print("wrote %s (%dx%d BGRA)" % (os.path.join(RES_DIR, DDS_NAME), TEX_W, TEX_H))

    # 2. TSI
    textures, blocks = tsi_read(TSI)
    tex_names = [n.lower() for n, _ in textures]
    if DDS_NAME.lower() in tex_names:
        texid = tex_names.index(DDS_NAME.lower())
        assert texid == len(textures) - 1, \
            "%s exists but is not the last texture; refusing to renumber sprites" % DDS_NAME
        print("TSI already has %s; rewriting its sprite block" % DDS_NAME)
    else:
        texid = len(textures)
        textures.append(texture_entry(DDS_NAME))
        blocks.append((0, b""))
    base = sum(c for c, _ in blocks[:texid])
    block = b"".join(sprite_entry(texid, i, name) for i, (name, _, _) in enumerate(SPRITES))
    blocks[texid] = (len(SPRITES), block)
    if not args.dry_run:
        shutil.copyfile(TSI, TSI + ".bak")
    tsi_write(TSI, textures, blocks, args.dry_run)
    ids = {name: base + i for i, (name, _, _) in enumerate(SPRITES)}
    print("TSI sprites:", ", ".join("%s=%d" % kv for kv in ids.items()))

    # 3. UI_strID.ID (upsert by name; keep existing line endings style = CRLF)
    with open(STRID, "r") as fh:
        lines = [ln.rstrip("\n").rstrip("\r") for ln in fh if ln.strip()]
    lines = [ln for ln in lines if ln.split()[0].upper() not in ids]
    lines += ["%s %d" % (name, idx) for name, idx in ids.items()]
    if not args.dry_run:
        shutil.copyfile(STRID, STRID + ".bak")
        with open(STRID, "w", newline="\r\n") as fh:
            fh.write("\n".join(lines) + "\n")
    print("UI_strID.ID upserted (%d lines)" % len(lines))
    if args.dry_run:
        print("dry run -- nothing written")


if __name__ == "__main__":
    main()
