"""Stock the Muris merchants, who currently sell nothing.

Muris ships with two merchant NPCs whose four sell-tab columns are all blank:

    row 2122   [Weapon Merchant] Huzam
    row 2124   [Armor Merchant] Azim

This fills **Azim** with the level 210 and 220 armour tiers. Huzam is *not* filled,
and cannot be without other work first -- see the hard limit below.

Azim's layout
-------------
One tab per base class, twelve items each, well inside the 48 slots a tab has:

    lv210 job sets   2 sets x 4 pieces   (cap / body / gauntlet / boots)
    lv220 "Crystal"  1 set  x 4 pieces

The class split is not guesswork and not read off the item names. scripts/
import-armour-tier.py appended the lv210 tier one set per *second* job in ascending
job order (11 Knight, 12 Champion, 13 Mage, 14 Cleric, 15 Raider, 16 Scout,
17 Bourgeois, 18 Artisan) and maps them to base classes with
`JOB_TO_CLASS = {11,12 -> 51 Soldier; 13,14 -> 52 Muse; 15,16 -> 53 Hawker;
17,18 -> 54 Dealer}`, so the eight rows land in pairs. The Crystal tier was appended
in base-class order (51, 52, 53, 54) directly *before* them. The item names
corroborate both -- Magic Witch under Muse, Golden Arrow under Hawker, Billionaire
and Craftmans under Dealer.

Note the armour carries **no class gate at all** -- the only requirement on these
rows is type 31 (character level). The tabs are a shopping convenience, not an
enforcement; anyone can buy any of it.

Why Huzam is empty
------------------
A shop slot stores an item as the packed `type * 1000 + id`, read back by
`CObjNPC::Get_SellITEM` into a **short** and handed to `tagBaseITEM::Init(uint)`.
That format cannot represent an item id above 999: a weapon at row 1379 encodes as
`8 * 1000 + 1379 = 9379`, which decodes as **type 9, id 379** -- a different item
entirely, from a different table.

Our level 210 and 230 weapon tiers were appended to LIST_WEAPON.STB, which is now
1380 rows long, so they sit at rows 1355-1379. **Twenty-five of the twenty-six are
unsellable by construction.** Only three relevant items encode legally:

    weapon  381  Dual Tornado Rifles    (lv210, an older row)
    subwpn  306  Golden Angel Shield    (lv210)
    subwpn  307  Ancient Davion Shield  (lv230)

Three items is not a shop, so nothing is written for Huzam rather than leaving a
misleading stub. The fix is to renumber the weapon tiers into ids below 999 --
LIST_WEAPON has 430 free rows down there -- but that is a real migration, not a data
tweak: LIST_WEAPON.ZSC is indexed 1:1 with the STB row, so the model object has to
move with each row, and any weapon already owned by a character would change id
underneath the database.

Idempotent, verifiable, reversible.

Usage:
    python scripts/stock-muris-shops.py --dry-run
    python scripts/stock-muris-shops.py
    python scripts/stock-muris-shops.py --verify
    python scripts/stock-muris-shops.py --revert
"""
import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
STB = os.path.join(ROOT, "data", "3DDATA", "STB")
SIDECAR = os.path.join(STB, "LIST_SELL.muris-shops.json")

AZIM_NPC_ROW = 2124
SELL_TAB_COLS = (21, 22, 23, 24)
SLOT0_COL = 2                       # STORE_ITEM(I, T) = get_int32(I, 2 + T)
SLOT_COUNT = 48

# (table, item type, first Crystal row, first lv210 row). The two tiers are
# contiguous per table because they were appended back to back, Crystal first.
SLOTS = [
    ("LIST_CAP", 2, 977, 981),
    ("LIST_BODY", 3, 904, 908),
    ("LIST_ARMS", 4, 880, 884),
    ("LIST_FOOT", 5, 878, 882),
]
CLASSES = ["Soldier", "Muse", "Hawker", "Dealer"]


def load_importer():
    spec = importlib.util.spec_from_file_location(
        "import_oro", os.path.join(HERE, "import-oro.py"))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, ["import-oro.py", "--help"]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    return mod


def tab_items(k):
    """The twelve encoded item numbers for class index k (0=Soldier .. 3=Dealer)."""
    out = []
    for _tbl, typ, crystal0, lv210_0 in SLOTS:
        out.append(typ * 1000 + crystal0 + k)          # Crystal, one per class
        out.append(typ * 1000 + lv210_0 + 2 * k)       # lv210 set A
        out.append(typ * 1000 + lv210_0 + 2 * k + 1)   # lv210 set B
    return out


def check_source_rows(m):
    """Refuse to run if the tier rows are not where the script thinks they are."""
    bad = []
    for tbl, _typ, crystal0, lv210_0 in SLOTS:
        z = m.Stb(os.path.join(STB, tbl + ".STB"))
        for r, want in [(crystal0, 220), (crystal0 + 3, 220),
                        (lv210_0, 210), (lv210_0 + 7, 210)]:
            lv = 0
            for a, b in ((19, 20), (21, 22)):
                if z.get(r, a).strip() == b"31":
                    v = z.get(r, b).strip()
                    lv = int(v) if v.lstrip(b"-").isdigit() else 0
            if lv != want or not z.get(r, 0).strip():
                bad.append(f"{tbl} row {r}: level {lv}, expected {want}")
    if bad:
        sys.exit("tier rows are not where expected -- refusing to write:\n  "
                 + "\n  ".join(bad))


def free_sell_rows(m, need):
    """Rows that are empty AND referenced by no NPC, so filling them is safe."""
    s = m.Stb(os.path.join(STB, "LIST_SELL.STB"))
    n = m.Stb(os.path.join(STB, "LIST_NPC.STB"))
    referenced = set()
    for i in range(1, n.rows):
        for c in SELL_TAB_COLS:
            v = n.get(i, c).strip()
            if v.isdigit() and int(v) > 0:
                referenced.add(int(v))
    out = []
    for r in range(1, s.rows):
        if r in referenced:
            continue
        if any(s.get(r, c).strip() for c in range(0, s.cols)):
            continue
        out.append(r)
        if len(out) == need:
            return out
    sys.exit(f"only found {len(out)} free unreferenced LIST_SELL rows, need {need}")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--revert", action="store_true")
    args = ap.parse_args()

    m = load_importer()
    saved = {}
    if os.path.exists(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = json.load(fh)

    if args.verify:
        if not saved:
            sys.exit("no sidecar -- not applied")
        s = m.Stb(os.path.join(STB, "LIST_SELL.STB"))
        n = m.Stb(os.path.join(STB, "LIST_NPC.STB"))
        bad = []
        for k, row in enumerate(saved["rows"]):
            want = tab_items(k)
            got = [int(s.get(row, SLOT0_COL + i).strip() or 0) for i in range(len(want))]
            if got != want:
                bad.append(f"LIST_SELL row {row} contents differ")
            if int(n.get(AZIM_NPC_ROW, SELL_TAB_COLS[k]).strip() or 0) != row:
                bad.append(f"Azim tab {k} does not point at row {row}")
        print(f"    Azim: 4 tabs -> LIST_SELL rows {saved['rows']}, "
              f"{len(bad)} problems" + (": " + "; ".join(bad) if bad else ""))
        sys.exit(1 if bad else 0)

    if args.revert:
        if not saved:
            sys.exit("no sidecar -- nothing to revert")
        s = m.Stb(os.path.join(STB, "LIST_SELL.STB"))
        n = m.Stb(os.path.join(STB, "LIST_NPC.STB"))
        for row in saved["rows"]:
            for c in range(0, s.cols):
                s.set(row, c, b"")
        for c in SELL_TAB_COLS:
            n.set(AZIM_NPC_ROW, c, b"")
        s.save(args.dry_run)
        n.save(args.dry_run)
        if not args.dry_run:
            os.remove(SIDECAR)
        print(f"    cleared LIST_SELL rows {saved['rows']} and Azim's four tabs")
        return

    if saved:
        print("already applied -- nothing to do (use --revert to undo)")
        return

    check_source_rows(m)
    rows = free_sell_rows(m, 4)
    s = m.Stb(os.path.join(STB, "LIST_SELL.STB"))
    n = m.Stb(os.path.join(STB, "LIST_NPC.STB"))
    stl = m.Stl(os.path.join(STB, "LIST_SELL_S.STL"))

    print(f"stocking Azim (LIST_NPC row {AZIM_NPC_ROW}) using LIST_SELL rows {rows}\n")
    for k, row in enumerate(rows):
        items = tab_items(k)
        label = f"{CLASSES[k]} Armor"
        key = f"LSEL{row}"
        s.set(row, 0, label.encode("latin-1"))
        s.set(row, 1, key.encode("latin-1"))
        for i, v in enumerate(items):
            s.set(row, SLOT0_COL + i, str(v).encode("ascii"))
        n.set(AZIM_NPC_ROW, SELL_TAB_COLS[k], str(row).encode("ascii"))
        if not stl.has(key):
            stl.append(key, row, label)
        print(f"    tab {k}  row {row:>3}  {label:<15} {len(items)} items: "
              f"{' '.join(str(v) for v in items)}")

    if args.dry_run:
        print("\ndry run -- nothing written")
        return
    s.save(False)
    n.save(False)
    stl.save(False)
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({"rows": rows, "npc": AZIM_NPC_ROW}, fh, indent=1)
    print("\ndone. Restart the game server (STBs are cached) and rebake the client VFS.")
    print("Huzam is deliberately untouched -- see the module docstring.")


if __name__ == "__main__":
    main()
