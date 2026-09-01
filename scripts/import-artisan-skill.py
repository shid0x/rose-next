"""Import a gun burst-fire attack skill for the Artisan, from the RoseZA dump.

    python scripts/import-artisan-skill.py --dry-run
    python scripts/import-artisan-skill.py
    python scripts/import-artisan-skill.py --verify
    python scripts/import-artisan-skill.py --restore

Why
---
The Artisan had **zero exclusive attack skills** -- every offensive skill in the
Dealer tree is class 44 (whole line) or 67 (Bourgeois). It is the only class in
the game whose job change adds nothing to a fight, and no amount of passive
tuning invents an attack skill. So one is brought in.

The source
----------
`RoseZA test client`, row 2461 "Aimed Triple Shot": SKILL_TYPE 3, weapon 232 gun
+ 233 launcher, SKILL_DAMAGE_TYPE 1, action type 92, 5 ranks.

Action type 92 is what makes it a burst. At the gun column it resolves to
`GUN_3ATTACK_M1.ZMO`, which carries **three attack frames** -- and the attack
frame count, not any STB column, is what multiplies the damage.

It was chosen over the alternatives (667's Daze Attack / Poison Shot / Acid Shot,
137 and RoseZA's Hypno Shot -- all natively class 68) for two reasons:

* **It needs no new files.** Every animation and effect it references already
  exists in our data, traced through the mapping in
  `doc/skill-import-investigation.md`:

      casting anim   type  95 -> FILE_MOTION 487 -> gun_casting_m1.ZMO      ours
      casting repeat type 131 -> FILE_MOTION 611 -> near_rotate01_m1.zmo    ours
      action anim    type  92 -> FILE_MOTION 432 -> gun_3attack_m1.zmo      ours
      cast fx  1041 _heavy_casting_01.eft   bullet fx 180 healing_04.eft    ours
      hit fx    110 gun_blowup_01.eft       dummy fx  171                   ours
      sounds 131 / 148 / 67                                                 ours

* **It has no status effect.** The 667 candidates each reference LIST_STATUS rows
  120, 184 and 205 -- our status table has **62 rows**, theirs has 277 -- so each
  would need its status row imported or re-pointed on top of the skill.

Only presentation is taken from the source. Every gameplay number below is ours.

What is re-pointed
------------------
    class      44 (whole Dealer line)  ->  68 (Artisan Job = 422/432)
    name       "Aimed Triple Shot"     ->  "Calibrated Burst"
    prereq     RoseZA skill 2301       ->  Craft Mastery (2081) rank 6
    icon       566 (their atlas)       ->  172 (ours; swap with add-skill-icon.py)
    ranks      5                       ->  10, to match every other skill here
    power / cooldown / MP / SP         ->  authored, see below

"Calibrated Burst" rather than keeping the source name: we already have a
"Triple Shot" (the Dealer's rank 11-20 Twin Shot continuation) and two skills
with near-identical names in the same tree would be needlessly confusing. The
new name is also a maker's word, which suits the class.

The numbers
-----------
    power   80 -> 200   against the motion's 3 attack frames
    cd     3.6 -> 2.8s  -- the shortest cooldown of any attack skill in the game
    MP      26 -> 55
    prereq  Craft Mastery rank 6, which is itself the Artisan gate

Measured against the Dealer kit at level 200: ~672 dps, sitting alongside Triple
Shot's 709 rather than above it. The difference is that Triple Shot needs twenty
ranks of a shared skill while this needs ten of an Artisan-only one, and this
fires far more often -- which is the identity.

At 2.8 seconds it costs about **1,180 MP a minute against roughly 270 of regen**,
so it cannot be held down. That is deliberate: the fastest gun in the game is a
burst, not a rotation.

Where it lands in the tree
--------------------------
Under `Craft Mastery LEVEL="6"` in `skilltree_dealer.xml`, beside Refine Item,
Gem Cutting and Cart Craft -- the node where the Artisan branch already begins.

Traps this had to avoid
-----------------------
* **The STL key is per *skill*, not per rank.** All ten rows carry one key
  (`LSkill7002`) into LIST_SKILL_S.STL, while the *server* reads the name from
  STB column 0. A new skill needs both or it is nameless on one side.
* `SKILL_1LEV_INDEX` (col 1) must point at the family's first row on every rank,
  or the per-rank machinery in every rebalance script here refuses to touch it.
* **The tree XML ships LF-only.** It is read and written with `newline=""` so
  Python's text mode does not rewrite all 59 lines to CRLF just to add one node --
  which also keeps `--restore` byte-exact.
* **How often a skill strikes is a property of its animation, not its row.**
  `SKILL_ANI_HIT_COUNT` (col 70) is read by nothing; the damage sites pass
  `m_pCurMOTION->m_wTatalAttackFrame` as `wHitCNT`, counted from the ZMO's
  frame-event table at load. This skill strikes three times because action type
  92 resolves to `GUN_3ATTACK_M1.ZMO` (3 attack frames), and the power curve is
  sized against that. Col 70 is written to 3 only so the table does not lie.
* **Three strikes is still one damage number.** `Get_SkillDAMAGE` ends in
  `iDamage *= wHitCNT` and the caller sends a single `Send_gsv_DAMAGE_OF_SKILL`,
  so the animation plays out and one multiplied figure lands -- which is how
  every multi-hit skill in this game has always presented.

Idempotent: records what it appended in a sidecar, so a second run is a no-op and
--restore removes the rows again. data/ is gitignored, so this file is the record.
"""
import argparse
import io
import json
import os
import struct
import sys
import shutil
import xml.etree.ElementTree as ET

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STB_DIR = os.path.join(ROOT, "data", "3DDATA", "STB")
SKILL_STB = os.path.join(STB_DIR, "LIST_SKILL.STB")
SKILL_STL = os.path.join(STB_DIR, "LIST_SKILL_S.STL")
TREE_XML = os.path.join(ROOT, "data", "3DDATA", "CONTROL", "xml", "skilltree_dealer.xml")
SIDECAR = os.path.join(STB_DIR, "LIST_SKILL.artisan-import.json")
TREE_ART = os.path.join(ROOT, "data", "3DDATA", "CONTROL", "RES",
                        "DEALER_CRAFT_MASTERY.DDS")
# Hidden on purpose: pack.rs walks the data tree filtering only *hidden* entries and
# applies no extension filter, so a plain .bak next to the art gets baked into the .vfs.
ART_BACKUP = os.path.join(ROOT, "data", "3DDATA", "CONTROL", "RES",
                          ".DEALER_CRAFT_MASTERY.DDS.orig")

# CSkillTreeDlg::Draw blits the page art at m_sPosition + (20, 75), then draws 40x40
# icons on top at their XML offsets -- so art-local + ART_ORIGIN = dialog-local.
# There is NO line-drawing code anywhere in the dialog: every box and every connector
# in the skill tree is painted into this DDS. A new skill gets no box and no line
# unless it is added here, which is why the first attempt looked broken.
ART_ORIGIN = (20, 75)
ART_LINE_RGBA = (74, 81, 99, 255)   # sampled from the existing spine
ART_SPINE_X = 74                    # vertical spine that every branch hangs off
ART_SPINE_BOTTOM = 296              # where it stopped, at the 2621 branch
ART_BOX = (178, 355)                # art-local top-left of the new box
ART_BOX_SIZE = 42                   # boxes are 42x42, 1px border

# Icon sits 1px in horizontally and 2px vertically from the box border -- the same
# relationship retail uses (2621: box art(178,275) -> icon dialog(199,352)).
TREE_OFFSET = (ART_BOX[0] + ART_ORIGIN[0] + 1, ART_BOX[1] + ART_ORIGIN[1] + 2)

RANKS = 10
NAME = "Calibrated Burst"
DESC = ("A tuned three-round burst. The fastest attack skill an Artisan can fire, "
        "and far too expensive to hold down.")
PREREQ_SKILL, PREREQ_RANK = 2081, 6      # Craft Mastery, at the Artisan gate
CLASS_ARTISAN = 68
ICON = 172


def lin(a, b, n=RANKS):
    return [round(a + (b - a) * k / (n - 1)) for k in range(n)]


POWER = lin(80, 200)
RELOAD = lin(18, 14)        # x0.2s -> 3.6s down to 2.8s
MP = lin(26, 55)
SP = lin(9, 34)             # skill points to learn each rank


def row_cells(base_row, n):
    """The 87 game columns for rank n+1. Presentation from RoseZA 2461, rest ours."""
    c = {
        0: NAME,
        1: base_row,                # SKILL_1LEV_INDEX -- family, every rank
        2: n + 1,                   # SKILL_LEVEL
        3: SP[n],
        4: 1,                       # tab: the attack tab, as Twin Shot
        5: 3,                       # SKILL_TYPE 3, attack-motion
        7: 5,                       # target filter: enemy PC + monsters
        9: POWER[n],
        10: 1,                      # SKILL_HARM
        15: 1,                      # SKILL_DAMAGE_TYPE 1, weapon formula
        16: 17, 17: MP[n],          # cost slot 0 = AT_MP
        20: RELOAD[n],
        29: 2,                      # SKILL_ACTION_MODE, as the source
        30: 232, 31: 233,           # gun, launcher
        35: CLASS_ARTISAN,
        39: PREREQ_SKILL, 40: PREREQ_RANK,
        51: ICON,
        52: 95, 53: 150,            # casting anim + speed
        54: 131,                    # casting repeat
        56: 1041, 57: 3, 58: 131,   # casting effect / dummy point / sound
        68: 92, 69: 175,            # action anim (gun_3attack, 3 attack frames) + speed
        70: 3,                      # SKILL_ANI_HIT_COUNT -- cosmetic. Nothing reads
                                    # this column; the real multiplier is the
                                    # motion's attack-frame count. Set to match
                                    # the animation so the table stays honest.
        71: 180, 72: 1, 73: 148,    # bullet effect / point / sound
        74: 110, 75: 999, 76: 67,   # hit effect / root dummy / sound
        77: 171, 78: 1, 79: 67,     # dummy hit effect / point / sound
        85: 30,                     # level-up cost / 100
        86: f"LSkill{base_row}",    # STL key -- one per skill, shared by all ranks
    }
    return [str(c.get(i, "")).encode("latin-1") for i in range(87)]


# ------------------------------------------------------------------ STB / STL
def stb_read(path):
    with open(path, "rb") as fh:
        d = fh.read()
    f = io.BytesIO(d)
    f.read(4)
    offset, rows, cols = struct.unpack("<III", f.read(12))
    return d, offset, rows, cols


def stb_append_rows(path, rows_cells, dry):
    """Append N rows. Row labels are the row number, as elsewhere in this table."""
    d, offset, rows, cols = stb_read(path)
    for rc in rows_cells:
        assert len(rc) == cols - 1, (len(rc), cols - 1)
    first_id = rows - 1
    name_block = b""
    for i in range(len(rows_cells)):
        lbl = str(first_id + i).encode("ascii")
        name_block += struct.pack("<H", len(lbl)) + lbl
    cells = b""
    for rc in rows_cells:
        cells += b"".join(struct.pack("<H", len(c)) + c for c in rc)
    out = (d[0:4]
           + struct.pack("<III", offset + len(name_block), rows + len(rows_cells), cols)
           + d[16:offset] + name_block + d[offset:] + cells)
    if not dry:
        with open(path, "wb") as fh:
            fh.write(out)
    return first_id


def stb_truncate_rows(path, count):
    """Remove the last `count` data rows -- the inverse of the append above.

    The header layout, from `classSTB::Open` (and the parser in
    `scripts/rose-data-reader.py`), is:

        "STB1" u32 data_offset u32 raw_rows u32 raw_cols
        u32 row_height
        (raw_cols + 1) x u16 column width      <- note the +1
        raw_cols       x pstr16 column name
        raw_rows       x pstr16 row name       <- first is the column-title line
        @data_offset: (raw_rows-1) x (raw_cols-1) x pstr16 cell

    `stb_read` hands back the *raw* counts, so there are exactly `cols` column
    names and `rows` row names -- getting either of those off by one silently
    desynchronises every following length prefix and corrupts the file.
    """
    d, offset, rows, cols = stb_read(path)
    f = io.BytesIO(d)
    f.seek(offset)

    def cell():
        n, = struct.unpack("<H", f.read(2))
        return f.read(n)

    grid = [[cell() for _ in range(cols - 1)] for _ in range(rows - 1)]
    tail = f.read()
    grid = grid[:-count]

    h = io.BytesIO(d[16:offset])
    header = h.read(4 + 2 * (cols + 1))          # row_height + column widths
    for _ in range(cols):                        # raw_cols column names
        n, = struct.unpack("<H", h.read(2))
        header += struct.pack("<H", n) + h.read(n)
    for _ in range(rows - count):                # raw_rows row names, less the dropped
        n, = struct.unpack("<H", h.read(2))
        header += struct.pack("<H", n) + h.read(n)

    body = b"".join(struct.pack("<H", len(c)) + c for row in grid for c in row)
    with open(path, "wb") as fh:
        fh.write(d[0:4] + struct.pack("<III", 16 + len(header), rows - count, cols)
                 + header + body + tail)


def vstr_read(f):
    n, shift = 0, 0
    while True:
        b = f.read(1)[0]
        n |= (b & 0x7F) << shift
        if not (b & 0x80):
            break
        shift += 7
    return f.read(n)


def vstr_write(b):
    n, out = len(b), b""
    while True:
        x = n & 0x7F
        n >>= 7
        out += bytes([x | (0x80 if n else 0)])
        if not n:
            break
    return out + b


def stl_read(path):
    with open(path, "rb") as fh:
        f = io.BytesIO(fh.read())
    typ = vstr_read(f)
    assert typ == b"ITST01", typ
    keycount, = struct.unpack("<I", f.read(4))
    keys = []
    for _ in range(keycount):
        k = vstr_read(f)
        idx, = struct.unpack("<I", f.read(4))
        keys.append((k, idx))
    langcount, = struct.unpack("<I", f.read(4))
    langpos = struct.unpack("<%dI" % langcount, f.read(4 * langcount))
    langs = []
    for lp in langpos:
        f.seek(lp)
        offsets = struct.unpack("<%dI" % keycount, f.read(4 * keycount))
        entries = []
        for o in offsets:
            f.seek(o)
            entries.append((vstr_read(f), vstr_read(f)))
        langs.append(entries)
    return keys, langs


def stl_write(path, keys, langs):
    keycount, langcount = len(keys), len(langs)
    header = vstr_write(b"ITST01") + struct.pack("<I", keycount)
    for k, idx in keys:
        header += vstr_write(k) + struct.pack("<I", idx)
    header += struct.pack("<I", langcount)
    langpos_at = len(header)
    header += b"\x00" * (4 * langcount)
    body, lang_positions = b"", []
    for entries in langs:
        lang_positions.append(len(header) + len(body))
        base = len(header) + len(body) + 4 * keycount
        offsets, blob = [], b""
        for n, ds in entries:
            offsets.append(base + len(blob))
            blob += vstr_write(n) + vstr_write(ds)
        body += struct.pack("<%dI" % keycount, *offsets) + blob
    out = bytearray(header + body)
    out[langpos_at:langpos_at + 4 * langcount] = struct.pack(
        "<%dI" % langcount, *lang_positions)
    with open(path, "wb") as fh:
        fh.write(bytes(out))


# ------------------------------------------------------------------ tree XML
def tree_add(base_row, dry):
    """Hang the skill under Craft Mastery LEVEL=6 -- where the Artisan branch starts.

    OFFSETX/OFFSETY are absolute inside the 564x540 dialog, NOT relative to the
    parent node (`CSkillTreeDlg::MoveWindow` does `m_sPosition + m_offset`), and
    icons are 40x40 (`CIcon::CIcon`). The rank-6 row already runs 273/337/401/465,
    so 465 collides exactly with Refine Item (2601) -- which is declared after us
    and paints straight over the top, making the skill invisible. 529 would be the
    next 64-step but ends at 569 and clips the 564px panel, so the free slot is the
    head of the row: 233 spans 233-273, clear of the parent icon at 182-222.
    """
    raw = open(TREE_XML, encoding="utf-8", newline="").read()
    if f'INDEX="{base_row}"' in raw:
        print(f"  tree: INDEX {base_row} already present, skipping")
        return
    anchor = '<SKILL INDEX="2081" OFFSETX="182" OFFSETY="79" LEVEL="6">'
    if anchor not in raw:
        sys.exit("tree: could not find the Craft Mastery rank-6 node; refusing to edit")
    node = (f'\n         <SKILL INDEX="{base_row}" '
            f'OFFSETX="{TREE_OFFSET[0]}" OFFSETY="{TREE_OFFSET[1]}"/>')
    out = raw.replace(anchor, anchor + node, 1)
    if not dry:
        open(TREE_XML, "w", encoding="utf-8", newline="").write(out)
    print(f"  tree: added INDEX {base_row} under Craft Mastery rank 6")


def tree_remove(base_row):
    raw = open(TREE_XML, encoding="utf-8", newline="").read()
    node = (f'\n         <SKILL INDEX="{base_row}" '
            f'OFFSETX="{TREE_OFFSET[0]}" OFFSETY="{TREE_OFFSET[1]}"/>')
    if node in raw:
        open(TREE_XML, "w", encoding="utf-8", newline="").write(raw.replace(node, "", 1))
        print(f"  tree: removed INDEX {base_row}")


# ------------------------------------------------------------------ tree art
def _hide(path):
    """Set the Windows hidden attribute so pack.rs skips this file."""
    try:
        import ctypes
        ctypes.windll.kernel32.SetFileAttributesW(str(path), 0x02)
    except Exception:
        pass


def _art_draw(im):
    """Extend the spine one branch further and add the box, in the retail style."""
    from PIL import ImageDraw
    d = ImageDraw.Draw(im)
    mid = ART_BOX[1] + ART_BOX_SIZE // 2
    d.line([(ART_SPINE_X, ART_SPINE_BOTTOM), (ART_SPINE_X, mid)], fill=ART_LINE_RGBA)
    d.line([(ART_SPINE_X, mid), (ART_BOX[0], mid)], fill=ART_LINE_RGBA)
    d.rectangle([ART_BOX[0], ART_BOX[1],
                 ART_BOX[0] + ART_BOX_SIZE - 1, ART_BOX[1] + ART_BOX_SIZE - 1],
                outline=ART_LINE_RGBA)
    return im


def art_has_node(path):
    """True if the new box is already painted (probes its top-left corner pixel)."""
    from PIL import Image
    return Image.open(path).convert("RGBA").load()[ART_BOX[0], ART_BOX[1]][3] > 8


def art_add(dry):
    """Paint the new branch into the page art.

    Deliberately a plain connector, not an arrowhead: in this artwork an arrow means
    "same skill, higher rank" (2081 -> 2081 L6, 2221 -> 2221 L11). Calibrated Burst is
    a distinct skill, so it gets the same plain line as the 2111/2131/2621 branches.

    Saved UNCOMPRESSED. The original is DXT5 with 10 mips; Pillow can only write BGRA
    here (350 KB -> 1 MB, no mips). Harmless for a UI sprite -- it is blitted 1:1 and
    INIT.LUA setMipmapLevel(3) caps the chain anyway -- but it is a real format change,
    hence the backup.
    """
    from PIL import Image
    if art_has_node(TREE_ART):
        print("  art: branch already painted, skipping")
        return
    if dry:
        print("  art: would paint a box at art%s and extend the spine" % (ART_BOX,))
        return
    if not os.path.exists(ART_BACKUP):
        shutil.copyfile(TREE_ART, ART_BACKUP)
        _hide(ART_BACKUP)
        print("  art: backed up -> %s (hidden)" % os.path.basename(ART_BACKUP))
    _art_draw(Image.open(TREE_ART).convert("RGBA")).save(TREE_ART)
    print("  art: painted branch box at art%s -> dialog%s"
          % (ART_BOX, (ART_BOX[0] + ART_ORIGIN[0], ART_BOX[1] + ART_ORIGIN[1])))


def art_remove():
    if os.path.exists(ART_BACKUP):
        shutil.copyfile(ART_BACKUP, TREE_ART)
        os.remove(ART_BACKUP)
        print("  art: restored the original DXT5 page art")
    else:
        print("  art: no backup found -- page art left as-is")


# ------------------------------------------------------------------ main
def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    saved = None
    if os.path.exists(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = json.load(fh)

    if args.restore:
        if not saved:
            sys.exit("no sidecar -- nothing to restore")
        base = saved["base_row"]
        _, _, rows, _ = stb_read(SKILL_STB)
        if rows - 1 != base + RANKS:
            sys.exit(f"LIST_SKILL has {rows - 1} rows, expected {base + RANKS} -- "
                     f"something else appended after this import; refusing to truncate")
        stb_truncate_rows(SKILL_STB, RANKS)
        keys, langs = stl_read(SKILL_STL)
        key = f"LSkill{base}".encode("ascii")
        if keys and keys[-1][0] == key:
            keys.pop()
            for e in langs:
                e.pop()
            stl_write(SKILL_STL, keys, langs)
        tree_remove(base)
        art_remove()
        os.remove(SIDECAR)
        print(f"restored -- removed {RANKS} skill rows, the STL key and the tree node")
        return

    if args.verify:
        if not saved:
            sys.exit("no sidecar -- the skill has not been imported")
        import importlib.util
        spec = importlib.util.spec_from_file_location(
            "rd", os.path.join(HERE, "rose-data-reader.py"))
        rd = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(rd)
        sk = rd.Stb(SKILL_STB, "utf-8")
        base = saved["base_row"]
        bad = []
        for n in range(RANKS):
            r = base + n
            want = row_cells(base, n)
            for c, w in enumerate(want):
                got = sk.get(r, c)
                if got.strip() != w.strip():
                    bad.append((r, c, got, w))
        raw = open(TREE_XML, encoding="utf-8", newline="").read()
        keys, _ = stl_read(SKILL_STL)
        print(f"rows {base}-{base + RANKS - 1}: {len(bad)} cells differ")
        for r, c, got, w in bad[:10]:
            print(f"    row {r} col {c}: {got!r} != {w!r}")
        print(f"STL key present: {any(k == f'LSkill{base}'.encode() for k, _ in keys)}")
        print(f"tree node present: {f'INDEX=\"{base}\"' in raw}")
        print("tree art branch painted:", art_has_node(TREE_ART))
        sys.exit(1 if bad else 0)

    if saved:
        print(f"already imported at row {saved['base_row']} -- nothing to do.")
        print("use --restore to remove it first if you want to re-import.")
        return

    _, _, rows, cols = stb_read(SKILL_STB)
    base = rows - 1
    print(f"{NAME}: appending {RANKS} ranks at rows {base}-{base + RANKS - 1} "
          f"({cols - 1} columns)\n")
    print(f"  {'rank':>5}{'power':>8}{'hits':>6}{'cd':>7}{'MP':>5}{'SP':>5}")
    for n in range(RANKS):
        print(f"  {n + 1:>5}{POWER[n]:>8}{3:>6}{RELOAD[n] * 0.2:>6.1f}s{MP[n]:>5}{SP[n]:>5}")
    print(f"\n  class {CLASS_ARTISAN} (Artisan), weapon 232 gun + 233 launcher, "
          f"prereq Craft Mastery rank {PREREQ_RANK}")

    if args.dry_run:
        print("\ndry run -- nothing written")
        return

    cells = [row_cells(base, n) for n in range(RANKS)]
    got = stb_append_rows(SKILL_STB, cells, False)
    assert got == base, (got, base)

    keys, langs = stl_read(SKILL_STL)
    key = f"LSkill{base}".encode("ascii")
    if not any(k == key for k, _ in keys):
        keys.append((key, len(keys)))
        for e in langs:
            e.append((NAME.encode("latin-1"), DESC.encode("latin-1")))
        stl_write(SKILL_STL, keys, langs)
        print(f"  STL: added {key.decode()}")

    tree_add(base, False)
    art_add(False)

    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({"base_row": base, "ranks": RANKS, "name": NAME}, fh, indent=1)

    print(f"\ndone -- {NAME} imported at rows {base}-{base + RANKS - 1}. "
          f"Sidecar: {os.path.basename(SIDECAR)}")
    print("Restart the game server and the client, and rebake the VFS before deploying.")
    print("NOTE: LIST_SKILL_S.STL and the tree XML both changed, so the client needs "
          "both -- and dlgskilltree reads the loose xml folder, not the VFS.")


if __name__ == "__main__":
    main()
