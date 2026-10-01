#!/usr/bin/env python3
"""Re-package the Jrose "shields" that are not shields (LIST_SUBWPN 306-334).

WHY
---
The 2026-09-06 Jrose sub-weapon import (doc/jrose-subwpn-batch1.txt) took 35
models by ZSC attach dummy -- dummy 2 is the shield point -- and wrote every one
as item class **261 Shield**. In game most of them are small caster mirrors,
bucklers, gadgets and trinkets (0.27-0.63 m against 0.9-1.1 m for a real
shield), and a level-240 player handed one as the top of the ladder reads it as
a downgrade. Jrose itself files seven of them as class 263 and gates the
mirrors to casters (job 64, INT + MP-regen options) and the bucklers to light
classes (job 66, DEX + avoid). The user checked every endgame row on the avatar
(2026-10-01): four are shields, the rest are not.

The retail table already has the classes for this, and STR_ITEMTYPE.STL names
them: 261 Shield, **262 Support Tool** (the Book of Standards -> Magic Pearl
caster ladder, rows 61-82, INT-gated, +Max MP / +MP regen, RES-heavy) and
**263 Dolls** (+Charm cosmetics, plus the one serious house charm, Asper Charm
93). So this is a column edit, not a new system.

WHAT CLASS 261 DOES IN CODE (and therefore what a converted row loses)
---------------------------------------------------------------------
  * the tooltip prints the DEF/RES line (`CItem` SUBWPN case in item.cpp);
    other classes printed name + type + options only, so their DEF/RES --
    which `Cal_DEFENCE` / `Cal_RESIST` DO sum for every equip slot -- were
    invisible. item.cpp now prints the line for any sub-weapon whose DEF or
    RES is non-zero.
  * Soldier/Knight "Shield Barrier" (381-390) and "Endure" (421-430) require a
    261 in the left hand. Casters and hawkers never have them.
  * 40% of incoming durability wear lands on a 261 (cobjavt.cpp).
  * `AT_PSV_SHIELD_DEF` (102) is granted by no skill -- dead.
  * The attach point is the model's ZSC dummy, not the class, so a converted
    item keeps its look. No row carries a job gate; any one-hand right weapon
    allows any sub-weapon.

THE NEW LADDERS
---------------
Shields (261), one per tier, heavy line. Sized at 12-13% of the armour set's
DEF (sets: lv210 668, 220 975, 230 1299, 240 1430) -- the share the first two
imported shields had against a 900-DEF set before the 220-240 armour tiers were
filled. Righteous Shield NEO (1.06 m, the best-looking of the four) goes on top
and was obtainable nowhere; it now sits in the boss tables.

    210  Garm Shield           DEF  90  RES 32  STR 310
    220  Golden Angel Shield   DEF 120  RES 40  STR 375   (was lv210)
    230  Ancient Davion Shield DEF 155  RES 48  STR 375
    240  Righteous Shield NEO  DEF 180  RES 55  STR 375   (was lv155)

Support Tools (262), casters, INT gate (the old STR gate value one for one --
they were already 250-280, sized for the 300 stat cap). Stats continue the
retail ladder along the rows RoseZA/667 planned but never named (83-90).

Charms (263), two flavours by gate: light (DEX; +Avoid, +Crit) and melee (STR;
+Max HP, +Hit). DEF a third of the tier's shield, RES equal to it, options on
the Golden Angel's +20 at lv210 rising to +30 at 240. The 263 type string is
renamed "Dolls" -> "Charm": the retail dolls literally give +Charm, so it reads
right for both.

Every converted 230/240 row also loses the +150 DEF / +45 STR options it had
inherited from the Ancient Davion template -- the Davion itself included (its
options are re-authored on a curve). A visible nerf on items in players' hands,
accepted in alpha.

Rows with "Shield" in the name are renamed; every converted description is
rewritten (the shields' own "Shield Exclusive for ETC iROSE" filler too).

DISTRIBUTION
------------
Huzam (NPC 2122, Muris weapon merchant, LIST_SELL tab 8) keeps the three
buyable shields and the 210/230 mirrors plus Chronos Aspis. The level 140-190
charms and the silver feather, which had no source at all, join Chester's
Magic Weapon tab (470), after the retail Support Tool ladder. The Karkia and
Oro drop buckets are rewritten by add-karkia-drops.py (`--rewrite`) and
add-oro-drops.py (`--restore`, then re-run); their lists name the new mix.

Idempotent; `--dry-run` / `--verify` / `--restore`. The sidecar in build/ holds
the previous value of every cell and STL entry touched, so restore is exact
and cell-level (LIST_SELL and LIST_SUBWPN are written by other scripts too).
After running: restart the servers and bake; the client needs the item.cpp
tooltip change to show a tool's RES.
"""
import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STB_DIR = os.path.join(ROOT, "data", "3DDATA", "STB")
SUBWPN_STB = os.path.join(STB_DIR, "LIST_SUBWPN.STB")
SUBWPN_STL = os.path.join(STB_DIR, "LIST_SUBWPN_S.STL")
SELL_STB = os.path.join(STB_DIR, "LIST_SELL.STB")
ITEMTYPE_STL = os.path.join(STB_DIR, "STR_ITEMTYPE.STL")
SIDECAR = os.path.join(ROOT, "build", "subweapon-repackage.json")

# game columns (rose/io/stb.h)
C_CLASS, C_PRICE, C_QUALITY = 4, 5, 8
C_REQ = (19, 20, 21, 22)            # (ability, value) x2
C_ADD = (24, 25, 27, 28)            # (ability, value) x2
C_DEF, C_RES = 31, 32

SHIELD, TOOL, CHARM = 261, 262, 263
AT_STR, AT_DEX, AT_INT = 10, 11, 12
AT_ATK, AT_DEF, AT_HIT, AT_AVOID, AT_CRIT = 18, 19, 20, 22, 26
AT_RECOVER_MP, AT_MAX_HP, AT_MAX_MP, AT_LEVEL = 28, 38, 39, 31

T_SUBWPN = 9
SELL_SLOTS = range(2, 50)           # 48 item slots per tab
HUZAM_TAB, CHESTER_TAB = 8, 470

# charm shape by level: (DEF, RES, option size)
CHARM_TIER = {140: (20, 15, 10), 150: (21, 17, 11), 165: (24, 20, 13),
              170: (25, 22, 13), 175: (26, 24, 14), 190: (30, 28, 16),
              210: (30, 32, 20), 215: (35, 36, 22), 220: (40, 40, 24),
              225: (46, 44, 26), 230: (52, 48, 27), 235: (56, 52, 28),
              240: (60, 55, 30)}


def shield(frag, lv, dfn, res, strg, adds, name, desc, price=None, quality=None):
    return dict(frag=frag, cls=SHIELD, lv=lv, gate=(AT_STR, strg), df=dfn, res=res,
                adds=adds, name=name, desc=desc, price=price, quality=quality)


def tool(frag, lv, intg, dfn, res, mp, regen, name, desc):
    return dict(frag=frag, cls=TOOL, lv=lv, gate=(AT_INT, intg), df=dfn, res=res,
                adds=((AT_MAX_MP, mp), (AT_RECOVER_MP, regen)), name=name, desc=desc,
                price=None, quality=None)


def light(frag, lv, dex, name, desc):
    dfn, res, n = CHARM_TIER[lv]
    return dict(frag=frag, cls=CHARM, lv=lv, gate=(AT_DEX, dex), df=dfn, res=res,
                adds=((AT_AVOID, n), (AT_CRIT, n)), name=name, desc=desc,
                price=None, quality=None)


def melee(frag, lv, strg, name, desc):
    dfn, res, n = CHARM_TIER[lv]
    return dict(frag=frag, cls=CHARM, lv=lv, gate=(AT_STR, strg), df=dfn, res=res,
                adds=((AT_MAX_HP, n * 20), (AT_HIT, n)), name=name, desc=desc,
                price=None, quality=None)


# id -> plan. `frag` must appear in the row's current name or the script refuses
# (the table has shifted). `name` None keeps the current name.
ROWS = {
    # ---- the four shields ------------------------------------------------
    309: shield("Garm", 210, 90, 32, 310, ((AT_STR, 20), (AT_ATK, 20)), None,
                "A narrow guard shaped to leave a crossbow arm free."),
    306: shield("Golden Angel", 220, 120, 40, 375, ((AT_STR, 25), (AT_ATK, 20)), None,
                "Gilded wings over a steel heart. It has turned worse than you.",
                price=500000, quality=70),
    307: shield("Ancient Davion", 230, 155, 48, 375, ((AT_STR, 35), (AT_DEF, 60)), None,
                "Dug out of a tomb that was old when Junon was young, and still whole."),
    330: shield("Righteous Shield NEO", 240, 180, 55, 375, ((AT_STR, 45), (AT_DEF, 80)), None,
                "The old Righteous pattern, reforged for the end of the world. "
                "Nothing in Karkia has marked it yet.",
                price=1000000, quality=99),
    # ---- support tools (262): the caster line ----------------------------
    324: tool("Steam Feather (Silver", 170, 240, 9, 46, 235, 7, None,
              "A silvered wing of gears, humming on the forearm. It keeps your mana as a clock keeps time."),
    326: tool("Chronos Aspis", 200, 250, 10, 49, 240, 7, None,
              "A clockwork face with hands that do not tell your time. They tell your mana's."),
    308: tool("Freyja", 210, 258, 12, 55, 260, 8, None,
              "A polished disc that turns spellwork aside more readily than steel."),
    321: tool("Steam Clock (Silver", 215, 262, 13, 58, 270, 8, None,
              "A silvered gear-train on the forearm, still turning under its own tension."),
    311: tool("Cathedral", 220, 265, 14, 61, 280, 9, None,
              "A consecrated mirror-face, carried by those who mend rather than strike."),
    314: tool("Ushumgal", 230, 272, 16, 68, 300, 10, None,
              "A serpent-etched mirror that drinks the light off a spell."),
    317: tool("Evolved Ushumgal", 240, 280, 17, 73, 310, 11, None,
              "The serpent mirror, reworked. The etching has gone deeper."),
    # ---- charms (263), light: DEX gate, avoid + crit ---------------------
    329: light("Red Feather Compass", 140, 160, None,
               "A cased compass on a bracer. The needle rarely agrees with north, "
               "but it always knows where the blow is coming from."),
    327: light("Lero Escudo", 150, 170, None,
               "A small pendant-guard on a wound chain. Light enough to forget, until it matters."),
    334: light("Gryphon Lumen", 165, 190, None,
               "A gryphon crest that catches light it was not given."),
    325: light("Steam Feather (Black", 170, 210, None,
               "A blackened wing of gears, humming on the forearm. It moves a heartbeat before you do."),
    332: light("Black Heaven Dragon", 190, 300, None,
               "A coiled drake worked into a bracer, in a black that gives nothing back."),
    322: light("Steam Clock (Black", 215, 314, None,
               "A blackened gear-train on the forearm, still turning under its own tension."),
    312: light("Ashura", 220, 318, None,
               "Small, fast, and meant for someone already moving."),
    315: light("Zlatorog", 230, 325, None,
               "Gold-horned and light in the hand, for all its weight of ornament."),
    318: light("Evolved Zlatorog", 240, 332, None,
               "The gold horn, reworked, and quicker than it looks."),
    # ---- charms (263), melee: STR gate, max HP + hit ---------------------
    323: melee("Steam Feather (Gold", 170, 210, None,
               "A brass wing of gears, humming on the forearm. It steadies the arm that carries it."),
    333: melee("Black Heaven Lion", 175, 270, None,
               "A lion's face in the same black, jaw open."),
    310: melee("Fafnir", 210, 375, "Fafnir's Scale",
               "A single drake scale on an iron cuff. Heavy, and it shows."),
    320: melee("Steam Clock (Gold", 215, 360, None,
               "A brass gear-train on the forearm, still turning under its own tension."),
    313: melee("Reconquista", 220, 375, "Reconquista Crest",
               "A knight's crest, faced and bossed for the line."),
    331: melee("Lord Knight", 225, 375, "Lord Knight Crest",
               "A command crest, meant to be seen from the back of a line."),
    316: melee("Lindwurm", 230, 375, "Lindwurm Talon",
               "A wyrm's talon bound to the wrist. It does not give for anything."),
    328: melee("Lord of Riot", 235, 375, None,
               "A slab of iron on a strap, built for a fight already going badly."),
    319: melee("Evolved Lindwurm", 240, 375, "Evolved Lindwurm Talon",
               "The wyrm talon, reworked. Very little gets past the arm that wears it."),
}

# LIST_SELL: whole tab 8, and rows appended to tab 470 after its existing stock
HUZAM_STOCK = [309, 306, 307, 308, 314, 326]
CHESTER_ADD = [329, 327, 334, 323, 324, 325, 333, 332]

ITEMTYPE_RENAME = {"263": ("Charm", "Dolls")}         # key -> (new, expected old)


def load(name):
    spec = importlib.util.spec_from_file_location(
        name.replace("-", "_"), os.path.join(HERE, name + ".py"))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, [name]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    return mod


def gi(stb, r, c):
    v = stb.get(r, c).decode("latin-1").strip()
    return int(v) if v else 0


def planned_cells(p):
    """game col -> value for one row (None = leave alone)."""
    cells = {C_CLASS: p["cls"], C_DEF: p["df"], C_RES: p["res"],
             # col 0 is the server's ITEM_NAME (logs, blank checks): mirror a rename
             **({0: p["name"]} if p["name"] else {}),
             C_REQ[0]: p["gate"][0], C_REQ[1]: p["gate"][1],
             C_REQ[2]: AT_LEVEL, C_REQ[3]: p["lv"],
             C_ADD[0]: p["adds"][0][0], C_ADD[1]: p["adds"][0][1],
             C_ADD[2]: p["adds"][1][0], C_ADD[3]: p["adds"][1][1]}
    if p["price"] is not None:
        cells[C_PRICE] = p["price"]
    if p["quality"] is not None:
        cells[C_QUALITY] = p["quality"]
    return cells


def sell_plan(sell):
    """LIST_SELL row -> {col: value}."""
    out = {HUZAM_TAB: {c: 0 for c in SELL_SLOTS}}
    for i, item in enumerate(HUZAM_STOCK):
        out[HUZAM_TAB][2 + i] = T_SUBWPN * 1000 + item
    have = [gi(sell, CHESTER_TAB, c) for c in SELL_SLOTS]
    codes = [T_SUBWPN * 1000 + i for i in CHESTER_ADD]
    stock = [v for v in have if v and v not in codes]
    if len(stock) + len(codes) > len(SELL_SLOTS):
        raise SystemExit(f"tab {CHESTER_TAB}: {len(stock)} + {len(codes)} items will not fit")
    out[CHESTER_TAB] = {c: 0 for c in SELL_SLOTS}
    for i, v in enumerate(stock + codes):
        out[CHESTER_TAB][2 + i] = v
    return out


def stl_entry(stl, key):
    """One key's fields per language block, keyed by block *position*: the
    blocks' file offsets move when the table is rewritten, so an offset is not
    a stable name for a block but its rank is."""
    i = stl.index()[key]
    return {str(bi): [f.decode("latin-1") for f in stl.blocks[start][i]]
            for bi, start in enumerate(stl.block_starts)}


def stl_restore(stl, key, blocks):
    i = stl.index()[key]
    for bi, (_k, fields) in enumerate(sorted(blocks.items(), key=lambda kv: int(kv[0]))):
        stl.blocks[stl.block_starts[bi]][i] = [f.encode("latin-1") for f in fields]


def stl_set(stl, key, fields):
    i = stl.index()[key]
    for start in stl.block_starts:
        stl.blocks[start][i] = [f.encode("latin-1") for f in fields]


def check_rows(sub):
    for r, p in ROWS.items():
        name = sub.get(r, 0).decode("latin-1")
        if p["frag"].lower() not in name.lower():
            raise SystemExit(f"row {r} is {name!r}, expected a name containing {p['frag']!r} "
                             "-- the table has shifted, refusing")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    oro = load("import-oro")
    ftl = load("fix-tooltip-labels")
    sub = oro.Stb(SUBWPN_STB)
    sell = oro.Stb(SELL_STB)
    names = ftl.Stl(SUBWPN_STL)
    types = ftl.Stl(ITEMTYPE_STL)
    if names.bad or types.bad:
        raise SystemExit(f"STL length-prefix damage: {names.bad or types.bad} -- fix first")

    saved = None
    if os.path.exists(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = json.load(fh)

    if args.restore:
        if not saved:
            sys.exit("no sidecar -- nothing to restore")
        n = 0
        for r, cols in saved["subwpn"].items():
            for c, v in cols.items():
                sub.set(int(r), int(c), v)
                n += 1
        for r, cols in saved["sell"].items():
            for c, v in cols.items():
                sell.set(int(r), int(c), v)
                n += 1
        for key, blocks in saved["names"].items():
            stl_restore(names, key, blocks)
        for key, blocks in saved["types"].items():
            stl_restore(types, key, blocks)
        for path, blob in ((SUBWPN_STB, sub.to_bytes()), (SELL_STB, sell.to_bytes()),
                           (SUBWPN_STL, names.to_bytes()), (ITEMTYPE_STL, types.to_bytes())):
            with open(path, "wb") as fh:
                fh.write(blob)
        os.remove(SIDECAR)
        print(f"restored {n} STB cells, {len(saved['names'])} item strings, "
              f"{len(saved['types'])} type strings; sidecar removed")
        return 0

    sells = sell_plan(sell)

    if args.verify:
        bad = []
        for r, p in sorted(ROWS.items()):
            for c, want in planned_cells(p).items():
                got = sub.get(r, c).decode("latin-1").strip() if isinstance(want, str) else gi(sub, r, c)
                if got != want:
                    bad.append(f"row {r} col {c}: {got!r} != {want!r}")
            en = names.english()[names.index()[f"LSUB{r}"]]
            if p["name"] and en[0].decode("latin-1") != p["name"]:
                bad.append(f"row {r} name {en[0]!r} != {p['name']!r}")
            if en[1].decode("latin-1") != p["desc"]:
                bad.append(f"row {r} description differs")
        for row, cols in sells.items():
            for c, want in cols.items():
                if gi(sell, row, c) != want:
                    bad.append(f"sell {row} col {c}: {gi(sell, row, c)} != {want}")
                    break
        for key, (new, _old) in ITEMTYPE_RENAME.items():
            if types.english()[types.index()[key]][0].decode("latin-1") != new:
                bad.append(f"item type {key} is not {new!r}")
        print(f"{len(ROWS)} rows, {len(sells)} shop tabs, {len(ITEMTYPE_RENAME)} type strings; "
              f"{len(bad)} problem(s)" + ("\n  " + "\n  ".join(bad[:12]) if bad else ""))
        return 1 if bad else 0

    if saved:
        print("already applied -- --restore first to re-plan")
        return 0
    check_rows(sub)

    record = {"subwpn": {}, "sell": {}, "names": {}, "types": {}}
    print("LIST_SUBWPN.STB")
    for r, p in sorted(ROWS.items(), key=lambda kv: (kv[1]["cls"], kv[1]["lv"])):
        cells = planned_cells(p)
        before = {}
        for c, v in cells.items():
            before[str(c)] = sub.get(r, c).decode("latin-1")
            sub.set(r, c, str(v))
        record["subwpn"][str(r)] = before
        key = f"LSUB{r}"
        cur = names.english()[names.index()[key]]
        record["names"][key] = stl_entry(names, key)
        new_name = p["name"] or cur[0].decode("latin-1")
        stl_set(names, key, [new_name, p["desc"]])
        kind = {SHIELD: "shield", TOOL: "tool  ", CHARM: "charm "}[p["cls"]]
        gate = {AT_STR: "STR", AT_DEX: "DEX", AT_INT: "INT"}[p["gate"][0]]
        print(f"  {r} {kind} lv{p['lv']:<3} {gate} {p['gate'][1]:<3} DEF {p['df']:>3} RES {p['res']:>2}"
              f"  +{p['adds'][0][1]}/{p['adds'][0][0]} +{p['adds'][1][1]}/{p['adds'][1][0]}"
              f"  {new_name}" + (f"  (was {cur[0].decode('latin-1')})" if p["name"] else ""))

    print("LIST_SELL.STB")
    for row, cols in sells.items():
        before = {}
        for c, v in cols.items():
            before[str(c)] = sell.get(row, c).decode("latin-1")
            sell.set(row, c, str(v) if v else "")
        record["sell"][str(row)] = before
        print(f"  tab {row}: {[v for v in cols.values() if v]}")

    print("STR_ITEMTYPE.STL")
    for key, (new, old) in ITEMTYPE_RENAME.items():
        cur = types.english()[types.index()[key]][0].decode("latin-1")
        if cur != old:
            raise SystemExit(f"item type {key} reads {cur!r}, expected {old!r}")
        record["types"][key] = stl_entry(types, key)
        stl_set(types, key, [new])
        print(f"  {key}: {old!r} -> {new!r}")

    if args.dry_run:
        print("\ndry run: nothing written")
        return 0
    os.makedirs(os.path.dirname(SIDECAR), exist_ok=True)
    for path, blob in ((SUBWPN_STB, sub.to_bytes()), (SELL_STB, sell.to_bytes()),
                       (SUBWPN_STL, names.to_bytes()), (ITEMTYPE_STL, types.to_bytes())):
        with open(path, "wb") as fh:
            fh.write(blob)
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump(record, fh, indent=1)
    print(f"\nwritten; sidecar {os.path.relpath(SIDECAR, ROOT)}")
    print("next: add-karkia-drops.py --rewrite; add-oro-drops.py --restore then re-run; "
          "restart servers; bake.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
