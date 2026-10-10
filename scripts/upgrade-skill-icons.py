"""Swap our DXT5-mangled skill icons for crisp copies, and adopt 667's redesigns.

Usage (from the repo root):
    python scripts/upgrade-skill-icons.py --recover    [--dry-run]   # phase A
    python scripts/upgrade-skill-icons.py --modernize  [--dry-run]   # phase B
    python scripts/upgrade-skill-icons.py --pick ROW=IDX667 [...]    # phase C, by hand
    python scripts/upgrade-skill-icons.py --verify
    python scripts/upgrade-skill-icons.py --restore

Why (2026-10-10, survey in build/skill-icon-quality/):
    Our SKILL01-03.DDS came DXT5-compressed in the original dump. Every
    retail-lineage client ships them uncompressed (667/139/Evo/RoseZA/Jrose as
    16-bit R5G6B5, tsuki as 24-bit for sheets 1-2), and the difference shows:
    a 40 px icon keeps a third of its colours through DXT5, fine lines smear and
    gradients get block noise. The engine uploads the file as-is and draws it
    1:1 through D3DX sprites, so the file is the whole story.

Phase A, --recover: for every real sprite in our sheets 1-3, find the same art
    in a crisp source and paste it over ours. Sources, in order: tsuki's 24-bit
    sheet at the same sprite index (same layout as ours for sheets 1-2), then
    the best 667 sprite by art match (667 keeps our legacy art as an
    uncompressed block around its sprites 1016-1248, plus its own referenced
    copies with restyled, colour-coded frames). A match is the icon interior
    (5..35 px) within 22/255 mean abs diff. If the whole cell also matches
    (<= 30) the full cell is pasted; otherwise only the interior, so 667's new
    frame colours do not leak into our set. The three sheets are rewritten as
    512x512 uncompressed BGRA (the format add-skill-icon.py already uses for
    the extension sheets); the mip chain is rebuilt with texconv as that script
    does. No sprite index moves.

Phase B, --modernize: for each of our skills whose name matches a 667 skill
    whose icon is real art *and* a different design from ours, append 667's
    icon to our extension sheet (skill04.dds and onwards, through the same
    append path as add-skill-icon.py) and point every rank row of that skill
    (same name, same old icon) at it in LIST_SKILL.STB col 51. 667 renamed most
    of its tree, so this reaches ~27 skills; the rest need --pick.

Phase C, --pick ROW=IDX: ROW is one of our LIST_SKILL rows, IDX a 667 sprite
    index (see build/skill-icon-quality/667-new-icons.png). Appends the sprite
    and patches every rank row of that skill, like phase B.

Placeholders: 550 of 667's 1521 sprites are gray placeholders (flat button,
    diagonal checker, textured stone). They are detected by pixels, never by
    the table -- interior colourfulness (mean of max-min over RGB) under 10;
    real icons bottom out near 16 -- and refused everywhere, --pick included.

Undo: originals are copied to build/skill-icons-667/ on the first write
    (TSI, every sheet touched) and the STB cells to a sidecar
    data/3DDATA/STB/LIST_SKILL.icons-667.json (cell-level; *.json is excluded
    from the bake). --restore puts all of it back. Note the sheet/TSI restore
    is whole-file: an icon added with add-skill-icon.py *after* this script
    ran would be lost by --restore; run that first.

Deploy: client-only data (TSI + DDS go in the VFS). The server reads col 51
    only for the /add skill cheat; restart it anyway after --modernize/--pick.
"""
import argparse, collections, importlib.util, io, json, os, shutil, struct, sys

import numpy as np
from PIL import Image

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.chdir(REPO)  # add-skill-icon.py's paths are repo-relative

def _load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    argv, sys.argv = sys.argv, [path]
    try:
        spec.loader.exec_module(mod)
    finally:
        sys.argv = argv
    return mod

ask = _load("add_skill_icon", os.path.join("scripts", "add-skill-icon.py"))
oro = _load("import_oro", os.path.join("scripts", "import-oro.py"))

RES = ask.RES_DIR
TSI = ask.TSI
SKILL_STB = ask.SKILL_STB
STL = os.path.join("data", "3DDATA", "STB", "LIST_SKILL_S.STL")
SIDECAR = os.path.join("data", "3DDATA", "STB", "LIST_SKILL.icons-667.json")
BACKUP = os.path.join("build", "skill-icons-667")
MANIFEST = os.path.join(BACKUP, "manifest.json")
TESTCLIENTS = r"C:\Users\Thomas\Desktop\Testclients"
SRC_667 = os.path.join(TESTCLIENTS, "667", "extracted data", "3DDATA")
SRC_TSUKI = os.path.join(TESTCLIENTS, "tsuki", "3Ddata")
ICON_COL = 51
NAME_KEY_COL = 86
CELL = 40
SHEET = 512
INTERIOR = slice(5, 35)
MATCH_INTERIOR = 22.0     # mean abs diff over the interior, same art
MATCH_FULL = 30.0         # over the whole cell, same frame too
BLANK_SAT = 10.0          # interior colourfulness below this = placeholder


# ---------------------------------------------------------------- readers
def find_file(root, rel):
    cur = root
    for part in rel.replace("/", "\\").split("\\"):
        names = {n.lower(): n for n in os.listdir(cur)}
        if part.lower() not in names:
            return None
        cur = os.path.join(cur, names[part.lower()])
    return cur


def tsi_sprites(path):
    """[(texid, x1, y1)] in global order, plus texture names."""
    f = io.BytesIO(open(path, "rb").read())
    ntex, = struct.unpack("<h", f.read(2))
    textures = []
    for _ in range(ntex):
        nlen, = struct.unpack("<h", f.read(2))
        textures.append(f.read(nlen).split(b"\x00")[0].decode("ascii"))
        f.read(4)
    total, = struct.unpack("<h", f.read(2))
    sprites = []
    for _ in range(ntex):
        cnt, = struct.unpack("<h", f.read(2))
        for _ in range(cnt):
            texid, x1, y1, x2, y2, _col = struct.unpack("<h4iI", f.read(22))
            f.read(32)
            sprites.append((texid, x1, y1))
    assert total == len(sprites)
    return textures, sprites


class Atlas:
    """A client's skill icon atlas: TSI + sheets, cells as RGB arrays."""
    def __init__(self, root3d):
        self.res = find_file(root3d, r"CONTROL\RES")
        self.textures, self.sprites = tsi_sprites(find_file(self.res, "SKILLICON.TSI"))
        self.sheets = {}

    def sheet(self, texid):
        if texid not in self.sheets:
            p = find_file(self.res, self.textures[texid])
            self.sheets[texid] = Image.open(p).convert("RGB") if p else None
        return self.sheets[texid]

    def cell(self, idx):
        """40x40 RGB int16 array; the part past the sheet edge is -1 (invalid)."""
        texid, x, y = self.sprites[idx]
        sh = self.sheet(texid)
        if sh is None:
            return None
        a = np.full((CELL, CELL, 3), -1, np.int16)
        w, h = min(CELL, SHEET - x), min(CELL, SHEET - y)
        a[:h, :w] = np.asarray(sh.crop((x, y, x + w, y + h))).astype(np.int16)
        return a


def is_blank(cell):
    inner = cell[INTERIOR, INTERIOR]
    valid = inner[inner[:, :, 0] >= 0]
    if len(valid) == 0:
        return True
    sat = (valid.max(axis=1) - valid.min(axis=1)).mean()
    return sat < BLANK_SAT


def diff(a, b, region=None):
    """Mean abs diff over pixels valid in both (None if nothing to compare)."""
    if region is not None:
        a, b = a[region, region], b[region, region]
    m = (a[:, :, 0] >= 0) & (b[:, :, 0] >= 0)
    if not m.any():
        return None
    return float(np.abs(a[m] - b[m]).mean())


def stack_diff(stack, valid_stack, a, region=None):
    """diff() against every cell of a stack at once."""
    if region is not None:
        stack, valid_stack, a = stack[:, region, region], valid_stack[:, region, region], a[region, region]
    m = valid_stack & (a[:, :, 0] >= 0)[None]
    d = np.abs(stack - a[None]).sum(axis=3)        # N x h x w
    n = m.sum(axis=(1, 2)) * 3
    with np.errstate(invalid="ignore", divide="ignore"):
        return np.where(n > 0, (d * m).sum(axis=(1, 2)) / n, np.inf)


# ---------------------------------------------------------------- names
def our_names(stb):
    stl = oro.Stl(STL)
    names = {}
    for r in range(1, stb.rows):
        k = stb.get(r, NAME_KEY_COL).strip()
        if k:
            n = stl.name(k, lang=1).strip()
            if n:
                names[r] = n
    return names


def norm(s):
    return " ".join(s.lower().replace("-", " ").split())


def icon_of(stb, r):
    c = stb.get(r, ICON_COL).strip()
    return int(c) if c.isdigit() else None


# ---------------------------------------------------------------- manifest / backups
def load_manifest():
    if os.path.exists(MANIFEST):
        return json.load(open(MANIFEST))
    return {"recovered": {}, "appended": [], "sheets_backed_up": []}


def save_manifest(m, dry):
    if dry:
        return
    os.makedirs(BACKUP, exist_ok=True)
    json.dump(m, open(MANIFEST, "w"), indent=1)


def backup_file(path, m, dry):
    dst = os.path.join(BACKUP, os.path.basename(path))
    if os.path.exists(dst) or dry:
        return
    os.makedirs(BACKUP, exist_ok=True)
    shutil.copy2(path, dst)
    m["sheets_backed_up"].append(os.path.basename(path))
    print("  backed up %s -> %s" % (os.path.basename(path), dst))


def load_sidecar():
    return json.load(open(SIDECAR)) if os.path.exists(SIDECAR) else {}


def patch_rows(stb, rows, new_idx, sidecar, dry):
    for r in rows:
        key = str(r)
        if key not in sidecar:
            sidecar[key] = stb.get(r, ICON_COL).decode("latin-1")
        stb.set(r, ICON_COL, str(new_idx))
    if not dry:
        json.dump(sidecar, open(SIDECAR, "w"), indent=1)


# ---------------------------------------------------------------- phase A
def recover(args):
    ours = Atlas(os.path.join("data", "3DDATA"))
    tsuki = Atlas(SRC_TSUKI)
    c667 = Atlas(SRC_667)
    m = load_manifest()

    art667 = [i for i in range(len(c667.sprites)) if c667.cell(i) is not None and not is_blank(c667.cell(i))]
    st667 = np.stack([c667.cell(i) for i in art667])
    v667 = st667[:, :, :, 0] >= 0

    sheets = {}      # texid -> RGBA image being edited
    stats = collections.Counter()
    plan = []        # (idx, source, src_idx, mode)
    for idx, (texid, x, y) in enumerate(ours.sprites):
        if ours.textures[texid].lower() not in ("skill01.dds", "skill02.dds", "skill03.dds"):
            continue
        o = ours.cell(idx)
        if is_blank(o):
            stats["blank in ours"] += 1
            continue
        best = None
        # tsuki: same index on its 24-bit sheets 1-2
        if texid <= 1 and idx < len(tsuki.sprites) and tsuki.sprites[idx][0] == texid:
            t = tsuki.cell(idx)
            di = diff(o, t, INTERIOR)
            if di is not None and di < MATCH_INTERIOR:
                best = ("tsuki", idx, diff(o, t), t)
        if best is None:
            di = stack_diff(st667, v667, o, INTERIOR)
            df = stack_diff(st667, v667, o)
            ok = np.where(di < MATCH_INTERIOR)[0]
            if len(ok):
                j = ok[np.argmin(df[ok])]
                best = ("667", art667[j], float(df[j]), st667[j])
        if best is None:
            stats["no source"] += 1
            continue
        src, sidx, dfull, cell = best
        mode = "full" if dfull <= MATCH_FULL else "interior"
        stats["%s/%s" % (src, mode)] += 1
        plan.append((idx, src, sidx, mode, cell))

    print("recover plan:", dict(stats))
    for idx, src, sidx, mode, cell in plan:
        texid, x, y = ours.sprites[idx]
        if texid not in sheets:
            sheets[texid] = ours.sheet(texid).convert("RGBA")
        img = sheets[texid]
        px = np.asarray(img).copy()
        if mode == "full":
            r0, r1 = 0, CELL
        else:
            r0, r1 = INTERIOR.start, INTERIOR.stop
        for yy in range(r0, r1):
            for xx in range(r0, r1):
                if cell[yy, xx, 0] >= 0 and y + yy < SHEET and x + xx < SHEET:
                    px[y + yy, x + xx, :3] = cell[yy, xx]
                    px[y + yy, x + xx, 3] = 255
        sheets[texid] = Image.fromarray(px, "RGBA")
        m["recovered"][str(idx)] = {"source": src, "src_idx": int(sidx), "mode": mode}

    for texid, img in sheets.items():
        path = os.path.join(RES, ours.textures[texid])
        backup_file(path, m, args.dry_run)
        ask.dds_write(path, img, args.dry_run)
        if not args.dry_run:
            ask.restore_mipmaps(path, False)
        print("  %s: %d sprites replaced%s" % (ours.textures[texid],
              sum(1 for i, *_ in plan if ours.sprites[i][0] == texid), " (dry run)" if args.dry_run else ""))
    save_manifest(m, args.dry_run)
    if not args.dry_run:
        report(plan_cells=[(i, ours.cell(i), c, mode) for i, _, _, mode, c in plan], name="report-recover.png")


# ---------------------------------------------------------------- append (phases B, C)
def append_sprites(items, args, label_prefix):
    """items: [(our_row, skill_name, old_icon, cell_array, src_idx)]. Appends each cell as a new
    sprite and patches every rank row (same name, same old icon). Returns the new indices."""
    stb = oro.Stb(SKILL_STB)
    names = our_names(stb)
    m = load_manifest()
    sidecar = load_sidecar()
    textures, blocks = ask.tsi_read(TSI)
    total = sum(c for c, _ in blocks)
    new_sheets = {}        # sheet_name -> RGBA image
    out = []
    have = {a["src_idx"]: a["new_idx"] for a in m["appended"]}   # one copy per 667 sprite
    for row, nm, old_icon, cell, src_idx in items:
        if is_blank(cell):
            sys.exit("667 sprite %d is a placeholder; refusing (%s)" % (src_idx, nm))
        if src_idx in have:
            new_idx = have[src_idx]
            rows = [r for r, n in names.items() if norm(n) == norm(nm) and icon_of(stb, r) == old_icon]
            patch_rows(stb, rows, new_idx, sidecar, args.dry_run)
            m["appended"].append({"new_idx": new_idx, "src_idx": int(src_idx), "skill": nm,
                                  "rows": rows, "old_icon": old_icon, "sheet": None, "cell": None})
            out.append((nm, new_idx, rows))
            print("  %-24s 667 sprite %4d -> our icon %d (already added)  (rows %s, was %s)"
                  % (nm, src_idx, new_idx, ",".join(map(str, rows)) or "none!", old_icon))
            continue
        # extension sheet with room, else a new one (same rule as add-skill-icon.py)
        ext_nums = [int(n[5:-4]) for n, _ in textures
                    if n.lower().startswith("skill") and n.lower().endswith(".dds")
                    and n[5:-4].isdigit() and int(n[5:-4]) >= ask.FIRST_EXT_SHEET]
        last = textures[-1][0]
        if ext_nums and last.lower() == ask.SHEET_FMT % max(ext_nums) and blocks[-1][0] < ask.CELLS_PER_SHEET:
            sheet_name, cellno = last, blocks[-1][0]
            if sheet_name not in new_sheets:
                new_sheets[sheet_name] = ask.dds_read_bgra(os.path.join(RES, sheet_name))
            cnt, raw = blocks[-1]
            blocks[-1] = (cnt + 1, raw + ask.sprite_entry(len(textures) - 1, cellno, label_prefix + nm))
        else:
            sheet_name = ask.SHEET_FMT % (max(ext_nums) + 1 if ext_nums else ask.FIRST_EXT_SHEET)
            cellno = 0
            new_sheets[sheet_name] = Image.new("RGBA", (SHEET, SHEET), (0, 0, 0, 0))
            textures.append(ask.texture_entry(sheet_name))
            blocks.append((1, ask.sprite_entry(len(textures) - 1, cellno, label_prefix + nm)))
        x, y = (cellno % ask.GRID) * CELL, (cellno // ask.GRID) * CELL
        art = np.where(cell >= 0, cell, 0).astype(np.uint8)
        im = Image.fromarray(art, "RGB").convert("RGBA")
        new_sheets[sheet_name].paste(im, (x, y))
        new_idx = total
        total += 1
        have[src_idx] = new_idx
        rows = [r for r, n in names.items() if norm(n) == norm(nm) and icon_of(stb, r) == old_icon]
        patch_rows(stb, rows, new_idx, sidecar, args.dry_run)
        m["appended"].append({"new_idx": new_idx, "src_idx": int(src_idx), "skill": nm,
                              "rows": rows, "old_icon": old_icon, "sheet": sheet_name, "cell": cellno})
        out.append((nm, new_idx, rows))
        print("  %-24s 667 sprite %4d -> our icon %d  (rows %s, was %s)"
              % (nm, src_idx, new_idx, ",".join(map(str, rows)) or "none!", old_icon))
    if not items:
        print("  nothing to do")
        return out
    backup_file(TSI, m, args.dry_run)
    for sheet_name, img in new_sheets.items():
        p = os.path.join(RES, sheet_name)
        if os.path.exists(p):
            backup_file(p, m, args.dry_run)
        ask.dds_write(p, img, args.dry_run)
        if not args.dry_run:
            ask.restore_mipmaps(p, False)
    ask.tsi_write(TSI, textures, blocks, args.dry_run)
    if not args.dry_run:
        # Not Stb.save(): it drops a .bak beside the STB, and pack.ps1 refuses to
        # bake while one exists under data/. Undo is the cell-level sidecar.
        with open(SKILL_STB, "wb") as fh:
            fh.write(stb.to_bytes())
    save_manifest(m, args.dry_run)
    return out


def modernize(args):
    ours = Atlas(os.path.join("data", "3DDATA"))
    c667 = Atlas(SRC_667)
    stb = oro.Stb(SKILL_STB)
    names = our_names(stb)
    stb667 = oro.Stb(find_file(SRC_667, r"STB\LIST_SKILL.STB"))
    by_name = collections.defaultdict(list)
    for r in range(1, stb667.rows):
        n = stb667.get(r, 0).decode("latin-1").strip()
        i = icon_of(stb667, r)
        if n and i is not None and i < len(c667.sprites):
            by_name[norm(n)].append(i)
    m = load_manifest()
    done = {(a["skill"], a["old_icon"]) for a in m["appended"]}
    # a name that covers two different skills of ours (Hawker's and Dealer's
    # "Triple Shot", two "Combat Mastery") cannot be matched to 667's one skill
    # by name: left for --pick
    icons_by_name = collections.defaultdict(set)
    for r, nm in names.items():
        if icon_of(stb, r) is not None:
            icons_by_name[norm(nm)].add(icon_of(stb, r))
    ambiguous = {n for n, s in icons_by_name.items() if len(s) > 1}
    items, seen = [], set()
    for r in sorted(names):
        nm, oi = names[r], icon_of(stb, r)
        if oi is None or (norm(nm), oi) in seen:
            continue
        seen.add((norm(nm), oi))
        if (nm, oi) in done:
            continue
        if norm(nm) in ambiguous:
            if by_name.get(norm(nm)):
                print("  %-24s skipped: two of our skills share the name (icons %s); use --pick"
                      % (nm, "/".join(map(str, sorted(icons_by_name[norm(nm)])))))
            continue
        cands = [i for i in by_name.get(norm(nm), []) if c667.cell(i) is not None and not is_blank(c667.cell(i))]
        if not cands:
            continue
        o = ours.cell(oi) if oi < len(ours.sprites) else None
        c = c667.cell(cands[0])
        if o is not None and not is_blank(o):
            d = diff(o, c, INTERIOR)
            if d is not None and d < MATCH_INTERIOR:
                continue          # same design: phase A already made it crisp
        items.append((r, nm, oi, c, cands[0]))
    print("modernize: %d skills with a redesigned 667 icon" % len(items))
    out = append_sprites(items, args, "667 ")
    if not args.dry_run and out:
        report(appended=[(nm, ours.cell(oi) if oi < len(ours.sprites) else None, c)
                         for (r, nm, oi, c, s) in items], name="report-modernize.png")


def pick(args):
    ours = Atlas(os.path.join("data", "3DDATA"))
    c667 = Atlas(SRC_667)
    stb = oro.Stb(SKILL_STB)
    names = our_names(stb)
    items = []
    for spec in args.pick:
        row, _, idx = spec.partition("=")
        row, idx = int(row), int(idx)
        if row not in names:
            sys.exit("row %d has no skill name" % row)
        if idx >= len(c667.sprites):
            sys.exit("667 sprite %d out of range (%d)" % (idx, len(c667.sprites)))
        items.append((row, names[row], icon_of(stb, row), c667.cell(idx), idx))
    out = append_sprites(items, args, "667 ")
    if not args.dry_run and out:
        report(appended=[(nm, ours.cell(oi) if oi is not None and oi < len(ours.sprites) else None, c)
                         for (r, nm, oi, c, s) in items], name="report-pick-%d.png" % len(load_manifest()["appended"]))


# ---------------------------------------------------------------- report sheet
def report(plan_cells=None, appended=None, name="report.png"):
    from PIL import ImageDraw, ImageFont
    try:
        font = ImageFont.truetype("segoeui.ttf", 11)
    except Exception:
        font = ImageFont.load_default()
    rows = []
    if plan_cells:
        for idx, before, after, mode in plan_cells:
            rows.append(("#%d %s" % (idx, mode), before, after))
    if appended:
        for nm, before, after in appended:
            rows.append((nm[:18], before, after))
    S, COLS, CW, CH = 2, 8, 190, 100
    n = len(rows)
    img = Image.new("RGB", (COLS * CW + 10, 30 + ((n + COLS - 1) // COLS) * CH), (28, 28, 32))
    d = ImageDraw.Draw(img)
    d.text((8, 6), "%s: before | after (%d)" % (name, n), font=font, fill=(240, 240, 240))
    for k, (label, before, after) in enumerate(rows):
        x, y = 8 + (k % COLS) * CW, 30 + (k // COLS) * CH
        for j, c in enumerate((before, after)):
            if c is None:
                continue
            im = Image.fromarray(np.where(c >= 0, c, 0).astype(np.uint8), "RGB").resize((CELL * S, CELL * S), Image.NEAREST)
            img.paste(im, (x + j * (CELL * S + 6), y))
        d.text((x, y + CELL * S + 2), label, font=font, fill=(200, 200, 200))
    os.makedirs(BACKUP, exist_ok=True)
    p = os.path.join(BACKUP, name)
    img.save(p)
    print("  review sheet: %s" % p)


# ---------------------------------------------------------------- verify / restore
def verify():
    m = load_manifest()
    ours = Atlas(os.path.join("data", "3DDATA"))
    tsuki = Atlas(SRC_TSUKI)
    c667 = Atlas(SRC_667)
    bad = 0
    for idx, rec in m["recovered"].items():
        idx = int(idx)
        src = tsuki if rec["source"] == "tsuki" else c667
        region = None if rec["mode"] == "full" else INTERIOR
        d = diff(ours.cell(idx), src.cell(rec["src_idx"]), region)
        if d is None or d > 0.5:
            bad += 1
            print("  sprite %d differs from its source (%.2f)" % (idx, d or -1))
    for n in ("SKILL01.DDS", "SKILL02.DDS", "SKILL03.DDS"):
        p = find_file(RES, n)
        h = open(p, "rb").read(128)
        fourcc, bits = struct.unpack_from("<4sI", h, 84)
        if fourcc != bytes(4) or bits != 32:
            bad += 1
            print("  %s is not 32-bit uncompressed (%r/%d)" % (n, fourcc, bits))
    stb = oro.Stb(SKILL_STB)
    for a in m["appended"]:
        c = c667.cell(a["src_idx"])
        d = diff(ours.cell(a["new_idx"]), c)
        if d is None or d > 0.5:
            bad += 1
            print("  appended icon %d differs from 667 sprite %d" % (a["new_idx"], a["src_idx"]))
        for r in a["rows"]:
            if icon_of(stb, r) != a["new_idx"]:
                bad += 1
                print("  row %d icon is %s, expected %d" % (r, stb.get(r, ICON_COL), a["new_idx"]))
    print("verify: %d recovered sprites, %d appended icons, %d problems"
          % (len(m["recovered"]), len(m["appended"]), bad))
    return bad == 0


def restore():
    m = load_manifest()
    for n in m["sheets_backed_up"]:
        src = os.path.join(BACKUP, n)
        dst = find_file(RES, n) if n.lower().endswith((".dds", ".tsi")) else None
        if dst is None:
            dst = os.path.join(RES if n.lower() != "list_skill.stb" else os.path.dirname(SKILL_STB), n)
        shutil.copy2(src, dst)
        print("  restored %s" % dst)
    sidecar = load_sidecar()
    if sidecar:
        stb = oro.Stb(SKILL_STB)
        for r, v in sidecar.items():
            stb.set(int(r), ICON_COL, v)
        with open(SKILL_STB, "wb") as fh:      # not Stb.save(): it leaves a .bak under data/
            fh.write(stb.to_bytes())
        os.remove(SIDECAR)
        print("  restored %d LIST_SKILL cells" % len(sidecar))
    if os.path.exists(MANIFEST):
        os.remove(MANIFEST)
    print("restored")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--recover", action="store_true", help="phase A: crisp copies over our DXT5 sprites")
    ap.add_argument("--modernize", action="store_true", help="phase B: adopt 667's redesigned icons (by name)")
    ap.add_argument("--pick", action="append", default=[], metavar="ROW=IDX667", help="phase C: hand-picked 667 sprite for our row")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()
    if args.restore:
        return restore()
    if args.verify:
        return sys.exit(0 if verify() else 1)
    if not (args.recover or args.modernize or args.pick):
        ap.error("pick one of --recover / --modernize / --pick / --verify / --restore")
    if args.recover:
        recover(args)
    if args.modernize:
        modernize(args)
    if args.pick:
        pick(args)
    if args.dry_run:
        print("DRY RUN - nothing written")


if __name__ == "__main__":
    main()
