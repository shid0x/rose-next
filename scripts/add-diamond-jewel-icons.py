"""Give the 18 Diamond rings, earrings and necklaces an icon (2026-11-02).

Symptom: a Diamond Ring / Earring / Necklace draws as a white square in the bag
and on the paper doll (classic and UI2 alike), and the GM item browser does not
list it at all (it skips rows whose icon is 0, catalog.rs).

Cause: LIST_JEWEL 252-370 is an accessory set someone appended without its STL
links (fix-item-names.py named it). Its top tier, Diamond -- one row above
Platinum in each of the 18 families, ring / earring / necklace x STR DEX INT CON
SEN CHA -- never got an icon either: column 9 is blank, which reads as sprite 0,
the top-left cell of icon01.dds, a nearly white picture. The missing
descriptions are the same unfinished import, not the cause (Bronze to Platinum
have none and draw fine).

No dump has this tier. Every LIST_JEWEL.STB under Desktop\\Testclients was
searched by row content (every column but name and icon) on 2026-11-02: the
only matches are our own copies, all with icon 0. A Diamond row is its
Platinum row with quality 60 and one more point of stat, so the art is ours to
make.

Art: each Diamond icon is its family's Platinum icon with the metal turned to
ice -- deep blue shadows, cyan mid-tones, white highlights. Coloured parts (gems)
keep their colour, and the cell's blue background is left as it is: the item is
told from the background by its difference to the median cell of icon40.dds +
icon41.dds, the two sheets the set lives on. ``--preview`` draws Bronze to
Diamond side by side for every family and changes nothing.

The icons are appended to the atlas by add-item-icon.py, labelled
``ljewNNN_diamond``, and found again by that label on a re-run.

    python scripts/add-diamond-jewel-icons.py --preview out.png
    python scripts/add-diamond-jewel-icons.py --dry-run
    python scripts/add-diamond-jewel-icons.py
    python scripts/add-diamond-jewel-icons.py --verify
    python scripts/add-diamond-jewel-icons.py --restore

Undo: --restore puts column 9 back from build/add-diamond-jewel-icons/
manifest.json. The appended icons stay in the atlas (unused, harmless).
LIST_JEWEL.STB and ITEM1.TSI are also saved whole in that folder before the
first write, as a safety net. `data/` is gitignored, so this file is the record.

Client data only (the server never reads an icon): re-bake the VFS, no server
restart.
"""

import argparse
import importlib.util
import json
import os
import re
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
sys.path.insert(0, HERE)
from rosefmt import dds  # noqa: E402
from rosefmt.stb import Stb  # noqa: E402
from rosefmt.tsi import tsi_read  # noqa: E402

STB_DIR = os.path.join(REPO, "data", "3DDATA", "STB")
RES = os.path.join(REPO, "data", "3DDATA", "CONTROL", "RES")
JEWEL = os.path.join(STB_DIR, "LIST_JEWEL.STB")
TSI = os.path.join(RES, "ITEM1.TSI")
BACKUP_DIR = os.path.join(REPO, "build", "add-diamond-jewel-icons")
MANIFEST = os.path.join(BACKUP_DIR, "manifest.json")

COL_ICON = 9
CELL = 40
NAME = re.compile(r"^Diamond (Ring|Earring|Necklace) +\((STR|DEX|INT|CON|SEN|CHA)\)$")
EXPECTED = 18
BACKGROUND_SHEETS = ("icon40.dds", "icon41.dds")

# Ice ramp over the metal's luminance (0-1 RGB): shadows -> mid-tones -> highlights.
ICE_LOW = (0.10, 0.22, 0.45)
ICE_MID = (0.45, 0.85, 1.00)
ICE_HIGH = (0.95, 1.00, 1.00)


def _tool(name):
    spec = importlib.util.spec_from_file_location(name.replace("-", "_"),
                                                  os.path.join(HERE, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def label(row):
    return "ljew%03d_diamond" % row


def name(stb, row):
    return stb.s(row, 0).strip()


def icon_of(stb, row):
    v = stb.s(row, COL_ICON).strip()
    return int(v) if v.lstrip("-").isdigit() else 0


def diamond_rows(stb):
    """[(diamond row, platinum row)]: every Diamond jewel, checked against the
    Platinum row directly above it."""
    out = []
    for r in range(1, stb.rows):
        m = NAME.match(name(stb, r))
        if not m:
            continue
        kind, stat = m.groups()
        plat = re.sub(r" +", " ", name(stb, r - 1))
        if plat != "Platinum %s (%s)" % (kind, stat):
            sys.exit("LIST_JEWEL %d (%s): the row above is %r, not its Platinum"
                     % (r, name(stb, r), name(stb, r - 1)))
        if icon_of(stb, r - 1) <= 0:
            sys.exit("LIST_JEWEL %d (%s) has no icon to start from" % (r - 1, plat))
        out.append((r, r - 1))
    if len(out) != EXPECTED:
        sys.exit("found %d Diamond jewels, expected %d" % (len(out), EXPECTED))
    return out


# ------------------------------------------------------------------ atlas
class Atlas:
    def __init__(self):
        self.textures, self.blocks = tsi_read(TSI)
        self.files = {f.lower(): f for f in os.listdir(RES)}
        self.sheets = {}
        self.sprites = []
        self.labels = {}
        aii = _tool("add-item-icon")
        for flat, bi, slot, tex, x1, y1, x2, y2 in aii.iter_sprites(self.blocks):
            raw = self.blocks[bi][1][slot * 54:(slot + 1) * 54]
            self.sprites.append((tex, x1, y1, x2, y2))
            text = raw[22:54].split(b"\0")[0].decode("ascii", "replace")
            self.labels.setdefault(text, flat)

    def sheet(self, name):
        import numpy as np
        key = os.path.basename(name).lower()
        if key not in self.sheets:
            img = dds.read_bgra(os.path.join(RES, self.files[key]))
            self.sheets[key] = np.asarray(img).astype(np.float32)
        return self.sheets[key]

    def cell(self, index):
        tex, x1, y1, _x2, _y2 = self.sprites[index]
        return self.sheet(self.textures[tex][0])[y1:y1 + CELL, x1:x1 + CELL].copy()

    def background(self):
        """Median of the in-bounds cells of the set's sheets: the blue cell
        background with no item on it."""
        import numpy as np
        cells = []
        for name in BACKGROUND_SHEETS:
            a = self.sheet(name)
            for j in range(12):
                for i in range(12):
                    cells.append(a[j * CELL:(j + 1) * CELL, i * CELL:(i + 1) * CELL])
        return np.median(np.stack(cells), axis=0)


def diamondize(cell, background):
    """A Platinum icon (40x40x4 float array) with its metal turned to ice."""
    import numpy as np
    from PIL import Image, ImageFilter
    rgb = cell[..., :3] / 255.0
    diff = np.abs(cell[..., :3] - background[..., :3]).sum(-1)
    mask = np.clip((diff - 40.0) / 60.0, 0, 1)
    mask = Image.fromarray((mask * 255).astype(np.uint8)).filter(ImageFilter.GaussianBlur(0.6))
    mask = np.asarray(mask).astype(np.float32)[..., None] / 255.0
    hi, lo = rgb.max(-1), rgb.min(-1)
    sat = np.where(hi > 0, (hi - lo) / np.maximum(hi, 1e-6), 0)
    lum = np.clip((0.299 * rgb[..., 0] + 0.587 * rgb[..., 1] + 0.114 * rgb[..., 2]) * 1.12, 0, 1)
    lum = lum[..., None]
    low, mid, high = (np.array(c) for c in (ICE_LOW, ICE_MID, ICE_HIGH))
    ice = np.where(lum < 0.55, low + (mid - low) * (lum / 0.55),
                   mid + (high - mid) * ((lum - 0.55) / 0.45))
    metal = np.clip(1.0 - sat * 2.2, 0, 1)[..., None]
    out = rgb * (1 - metal) + ice * metal
    out = rgb * (1 - mask) + out * mask
    result = cell.copy()
    result[..., :3] = np.clip(out * 255.0, 0, 255)
    return Image.fromarray(np.round(result).astype(np.uint8), "RGBA")


def art_for(atlas, stb, pairs):
    bg = atlas.background()
    return {row: diamondize(atlas.cell(icon_of(stb, plat)), bg) for row, plat in pairs}


# ------------------------------------------------------------------ modes
def preview(path):
    from PIL import Image, ImageDraw
    import numpy as np
    stb = Stb(JEWEL)
    pairs = diamond_rows(stb)
    atlas = Atlas()
    art = art_for(atlas, stb, pairs)
    scale, pitch = 2, 44 * 2
    img = Image.new("RGBA", (5 * pitch + 260, len(pairs) * pitch + 30), (30, 30, 30, 255))
    draw = ImageDraw.Draw(img)
    for i, text in enumerate(("Bronze", "Silver", "Gold", "Platinum", "Diamond")):
        draw.text((i * pitch + 10, 8), text, fill=(255, 255, 0, 255))
    for j, (row, plat) in enumerate(pairs):
        cells = [Image.fromarray(atlas.cell(icon_of(stb, plat - k)).astype(np.uint8), "RGBA")
                 for k in (3, 2, 1, 0)] + [art[row]]
        for i, c in enumerate(cells):
            img.alpha_composite(c.resize((CELL * scale, CELL * scale), Image.NEAREST),
                                (i * pitch + 4, 26 + j * pitch))
        draw.text((5 * pitch + 6, 26 + j * pitch + 34), "%d %s" % (row, name(stb, row)),
                  fill=(230, 230, 230, 255))
    img.save(path)
    print("preview: %s (nothing written to data/)" % path)


def apply(dry):
    stb = Stb(JEWEL)
    pairs = diamond_rows(stb)
    atlas = Atlas()
    art = art_for(atlas, stb, pairs)
    manifest = json.load(open(MANIFEST)) if os.path.exists(MANIFEST) else {"rows": {}}

    if not dry:
        os.makedirs(BACKUP_DIR, exist_ok=True)
        for path in (JEWEL, TSI):
            keep = os.path.join(BACKUP_DIR, os.path.basename(path) + ".orig")
            if not os.path.exists(keep):
                shutil.copy2(path, keep)

    aii = _tool("add-item-icon")
    changed = 0
    for row, plat in pairs:
        index = atlas.labels.get(label(row))
        if index is None:
            if dry:
                print("   %d %-26s icon would be appended (from Platinum icon %d)"
                      % (row, name(stb, row), icon_of(stb, plat)))
                continue
            cwd = os.getcwd()
            os.chdir(REPO)                       # add-item-icon works from the repo root
            try:
                index = aii.add_icon(art[row], label(row), False)
            finally:
                os.chdir(cwd)
        old = stb.s(row, COL_ICON)
        if icon_of(stb, row) == index:
            continue
        if icon_of(stb, row) > 0:
            print("   %d %-26s already has icon %d -- left alone" % (row, name(stb, row), icon_of(stb, row)))
            continue
        manifest["rows"].setdefault(str(row), {"old": old})
        manifest["rows"][str(row)]["icon"] = index
        stb.set_i(row, COL_ICON, index)
        changed += 1
        print("   %d %-26s icon %r -> %d" % (row, name(stb, row), old, index))

    if dry:
        print("dry run: nothing written")
        return 0
    if changed:
        stb.save(False, keep_backup=False)
        json.dump(manifest, open(MANIFEST, "w"), indent=1)
    # add-item-icon.py leaves ITEM1.TSI.bak-icons beside the atlas; pack.ps1
    # refuses to bake while a .bak sits under data/.
    bak = TSI + ".bak-icons"
    if os.path.exists(bak):
        shutil.move(bak, os.path.join(BACKUP_DIR, "ITEM1.TSI.bak-icons"))
    print("%d row(s) changed" % changed)
    return verify()


def verify():
    stb = Stb(JEWEL)
    pairs = diamond_rows(stb)
    atlas = Atlas()
    art = art_for(atlas, stb, pairs)
    problems = []
    for row, _plat in pairs:
        index = atlas.labels.get(label(row))
        if index is None:
            problems.append("%d: no sprite labelled %s" % (row, label(row)))
            continue
        if icon_of(stb, row) != index:
            problems.append("%d: icon %d, expected %d" % (row, icon_of(stb, row), index))
        tex, x1, y1, x2, y2 = atlas.sprites[index]
        if x2 - x1 != CELL or y2 - y1 != CELL or x2 > 512 or y2 > 512:
            problems.append("%d: sprite %d is not a whole cell on its sheet" % (row, index))
            continue
        got = atlas.cell(index).astype("uint8").tobytes()
        if got != art[row].tobytes():
            problems.append("%d: sheet pixels of sprite %d differ from the art" % (row, index))
    for p in problems:
        print("   FAIL " + p)
    print("verify: %s" % ("OK, %d Diamond jewels have their icon" % len(pairs) if not problems
                          else "%d problem(s)" % len(problems)))
    return 1 if problems else 0


def restore():
    if not os.path.exists(MANIFEST):
        sys.exit("no manifest at %s: nothing to restore" % MANIFEST)
    manifest = json.load(open(MANIFEST))
    stb = Stb(JEWEL)
    for row, entry in sorted(manifest["rows"].items(), key=lambda kv: int(kv[0])):
        stb.set(int(row), COL_ICON, entry["old"])
        print("   %s %-26s icon -> %r" % (row, name(stb, int(row)), entry["old"]))
    stb.save(False, keep_backup=False)
    os.remove(MANIFEST)
    print("restored; the appended icons stay in the atlas, unused")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    mode = ap.add_mutually_exclusive_group()
    mode.add_argument("--dry-run", action="store_true")
    mode.add_argument("--verify", action="store_true")
    mode.add_argument("--restore", action="store_true")
    mode.add_argument("--preview", metavar="PNG", help="write a Bronze-to-Diamond comparison sheet")
    args = ap.parse_args()
    if args.preview:
        return preview(args.preview)
    if args.verify:
        return verify()
    if args.restore:
        return restore()
    return apply(args.dry_run)


if __name__ == "__main__":
    sys.exit(main() or 0)
