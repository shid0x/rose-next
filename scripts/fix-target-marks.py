"""Repair the monster type-mark sheet: missing boss/elite marks, two muddy icons.

The small icon either side of a focused monster's name (classic `CNameBox::
DrawTargetMark`, UI2 `RoseRmlTargetFrame`) is a cell of `3DDATA/CONTROL/RES/
TARGETMARK.DDS`, and the cell index is the monster's `NPC_TYPE` (LIST_NPC game
col 27) used as-is. `TARGETMARK.TSI` defines all 36 cells of the 6x6 grid (19 px
pitch, 18x18 sprites); only some hold art.

Our sheet has the same layout as ruff's (139 / Evo 137 / QQ share it), but **cell
11 is blank**. Cell 11 is the boss crown in every reference client, and 42 of our
LIST_NPC rows use type 11 -- Aqua Giant, Evil Jack, Ant Devil, Poison Master, Lord
Storm, King Ulverick, Moon Pastor Laguz, the Blue/Red Dragons ... -- so every boss
showed no mark at all: the code finds a valid TSI cell and draws transparent
pixels.

Two cells hold art that is a smeared, re-compressed copy of the original (Jrose's
sheet has the same damage): **1**, the red demon face of the most common monster
type (210 rows), and **15**, Rudolph Santa. Those are *replaced* from 667, whose
art is identical on screen to RoseZA's; ruff's is a slightly lossier copy of it.

Cells 2-10 (skull coin, weapons, stars) were not damaged but drawn in an older,
warmer palette (gold blades, olive bows); 667/RoseZA's revision is cooler and
cleaner (silver blades, orange bows), so they are replaced too and the whole
1-11/15/16 set reads as one style. Jrose's 12-14 keep the older warm style --
no other dump has those cells.

Cell 16 is the white skull RoseZA and 667 use for *Elite* monsters (Elite
Wolverine, Elite Behemoth King, Sikuku High Chief, Hebarn Saboteur ...). Our first
user is [God] Hebarn's boss row 4148 (`import-hebarn.py`).

Cells 12-14 exist only in Jrose's sheet (a crown, a star and a crown), and three
Karkia rows imported from Jrose use them: D=Seed 2704 at 12, Corroded Golem 2687
and Revived Veteran Warrior 2688 at 13. 14 has no user here yet.

11 and 16 come from 667's sheet (tsuki's is identical). A "fill" cell is written
only if ours is fully transparent there, so real art is never overwritten; a
"replace" cell is written whenever it differs from its source. Re-running is a
no-op either way.

Compare cells by bytes (`same()`), never with `ImageChops.difference(...)
.getbbox()`: on RGBA that looks at the alpha channel only, so two icons with the
same outline and different colours compare equal -- which is how the first cut of
this script missed cells 1 and 15.

**Format.** Ours was DXT5 with a full mip chain. DXT5 codes 4x4 blocks, which do
not line up with the 19 px cells (cell 11's left edge shares blocks with cell
10), so re-compressing would re-quantise neighbouring icons. The sheet is written
uncompressed (B8G8R8A8, `-dx9` header, full mip chain via the vendored texconv)
from the decoded pixels, so every existing cell keeps exactly the colours the
client drew before -- `--verify` checks that bit for bit. 22 KB -> ~87 KB.

Client-only data (the server never reads it): re-bake the VFS. The original is
kept in build/target-marks/; nothing else edits this file, so `--restore` is a
whole-file copy.

    python scripts/fix-target-marks.py --dry-run
    python scripts/fix-target-marks.py
    python scripts/fix-target-marks.py --verify
    python scripts/fix-target-marks.py --restore
"""

import argparse
import os
import shutil
import subprocess
import sys
import tempfile

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
TARGET = os.path.join(REPO, "data", "3DDATA", "CONTROL", "RES", "TARGETMARK.DDS")
DUMPS = r"C:\Users\Thomas\Desktop\Testclients"
SOURCES = {
    "667": os.path.join(DUMPS, "667", "extracted data", "3DDATA", "CONTROL", "RES", "TARGETMARK.DDS"),
    "Jrose": os.path.join(DUMPS, "Jrose", "3Ddata", "Control", "RES", "TARGETMARK.DDS"),
}
TEXCONV = os.path.join(REPO, "thirdparty", "directxtex-2020.9.30", "texconv.exe")
BACKUP_DIR = os.path.join(REPO, "build", "target-marks")
BACKUP = os.path.join(BACKUP_DIR, "TARGETMARK.DDS")

GRID_COLS, PITCH = 6, 19
FILL, REPLACE = "fill", "replace"
CELLS = {   # cell: (label, source dump, mode)
    1: ("demon face", "667", REPLACE),
    2: ("skull coin", "667", REPLACE),
    3: ("sword", "667", REPLACE),
    4: ("crossed swords", "667", REPLACE),
    5: ("bow", "667", REPLACE),
    6: ("crossbow", "667", REPLACE),
    7: ("staff", "667", REPLACE),
    8: ("bronze star", "667", REPLACE),
    9: ("silver star", "667", REPLACE),
    10: ("gold star", "667", REPLACE),
    11: ("crown (boss)", "667", FILL),
    12: ("crown, Jrose", "Jrose", FILL),
    13: ("star, Jrose", "Jrose", FILL),
    14: ("crown 2, Jrose", "Jrose", FILL),
    15: ("Rudolph Santa", "667", REPLACE),
    16: ("white skull (elite)", "667", FILL),
}


def cell_box(i):
    x, y = (i % GRID_COLS) * PITCH, (i // GRID_COLS) * PITCH
    return x, y, x + PITCH, y + PITCH


def load(path):
    return Image.open(path).convert("RGBA")


def is_blank(im, i):
    return im.crop(cell_box(i)).getchannel("A").getextrema()[1] == 0


def same(a, b):
    # Byte compare, not ImageChops.difference(...).getbbox(): on RGBA, getbbox
    # looks at the alpha channel only, so two cells with the same shape and
    # different colours read as identical.
    return a.size == b.size and a.tobytes() == b.tobytes()


def encode(im):
    """RGBA image -> uncompressed B8G8R8A8 DDS (legacy DX9 header, full mips)."""
    with tempfile.TemporaryDirectory() as tmp:
        png = os.path.join(tmp, "TARGETMARK.png")
        im.save(png)
        r = subprocess.run([TEXCONV, "-nologo", "-y", "-dx9", "-m", "0", "-nowic", "-if", "BOX",
                            "-f", "B8G8R8A8_UNORM", "-o", tmp, png], capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit("texconv failed: %s" % (r.stdout or r.stderr)[-400:])
        with open(os.path.join(tmp, "TARGETMARK.dds"), "rb") as f:
            return f.read()


def load_sources():
    srcs = {k: load(p) for k, p in SOURCES.items()}
    for k, im in srcs.items():
        if im.size[0] != 128:
            raise SystemExit("unexpected sheet width in %s: %s" % (SOURCES[k], im.size))
    return srcs


def wanted(i, orig, srcs):
    """What cell i should hold: the source's art, unless it is a fill cell the
    original sheet already had art for."""
    _, dump, mode = CELLS[i]
    return orig if mode == FILL and not is_blank(orig, i) else srcs[dump]


def plan(ours, srcs):
    """[(cell, label, dump, mode)] still to write; refuses a blank source cell."""
    todo = []
    for i, (label, dump, mode) in sorted(CELLS.items()):
        box = cell_box(i)
        if is_blank(srcs[dump], i):
            raise SystemExit("%s has no art in cell %d (%s)" % (dump, i, label))
        if mode == FILL and is_blank(ours, i):
            todo.append((i, label, dump, mode))
        elif mode == REPLACE and not same(ours.crop(box), srcs[dump].crop(box)):
            todo.append((i, label, dump, mode))
    return todo


def verify():
    if not os.path.exists(BACKUP):
        print("verify: no backup in %s -- script never applied" % BACKUP_DIR)
        return False
    ours, orig, srcs = load(TARGET), load(BACKUP), load_sources()
    ok = True
    for i in range(GRID_COLS * GRID_COLS):
        box = cell_box(i)
        if i in CELLS:
            label, dump, mode = CELLS[i]
            good = same(ours.crop(box), wanted(i, orig, srcs).crop(box))
            print("  cell %2d %-20s %-6s %-8s %s" % (i, label, dump, mode, "ok" if good else "MISMATCH"))
        else:
            good = same(ours.crop(box), orig.crop(box))
            if not good:
                print("  cell %2d changed from the original" % i)
        ok &= good
    print("verify: %s" % ("OK" if ok else "FAILED"))
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    a = ap.parse_args()

    if a.verify:
        sys.exit(0 if verify() else 1)
    if a.restore:
        if not os.path.exists(BACKUP):
            raise SystemExit("nothing to restore: %s missing" % BACKUP)
        shutil.copyfile(BACKUP, TARGET)
        print("restored %s" % TARGET)
        return

    ours, srcs = load(TARGET), load_sources()
    if ours.size != (128, 128):
        raise SystemExit("unexpected sheet size: ours %s" % (ours.size,))
    todo = plan(ours, srcs)
    if not todo:
        print("nothing to do: every cell in CELLS already holds its art")
        return
    for i, label, dump, mode in todo:
        print("cell %2d %-20s %s from %s" % (i, label, mode, dump))
    if a.dry_run:
        return

    out = ours.copy()
    for i, _, dump, _ in todo:
        out.paste(srcs[dump].crop(cell_box(i)), cell_box(i)[:2])
    data = encode(out)

    os.makedirs(BACKUP_DIR, exist_ok=True)
    if not os.path.exists(BACKUP):
        shutil.copyfile(TARGET, BACKUP)
    with open(TARGET, "wb") as f:
        f.write(data)
    print("wrote %s (%d bytes)" % (TARGET, len(data)))
    if not verify():
        shutil.copyfile(BACKUP, TARGET)
        raise SystemExit("verification failed -- original put back")


if __name__ == "__main__":
    main()
