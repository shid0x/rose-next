"""Stock the Muris merchants, who currently sell nothing.

Muris ships two merchant NPCs whose four sell-tab columns are all blank:

    row 2122   [Weapon Merchant] Huzam   -> the lv210 and lv230 weapon tiers
    row 2124   [Armor Merchant] Azim     -> the lv210 and lv220 armour tiers

Azim
----
One tab per base class, twelve items each, well inside the 48 slots a tab has:

    lv210 job sets   2 sets x 4 pieces   (cap / body / gauntlet / boots)
    lv220 "Crystal"  1 set  x 4 pieces

Laid out as a grid rather than poured in sequentially, because a shop tab is 8
columns x 6 rows (dlgstore.xml stacks six 41px STORE_MIDDLE strips, 48 slots) and
the client positions each icon by its slot index, so an empty slot is a real gap:

    col       0            1            2
    row 0     lv210 A cap  lv210 B cap  Crystal cap
    row 1     lv210 A body lv210 B body Crystal body
    row 2     lv210 A arms lv210 B arms Crystal arms
    row 3     lv210 A foot lv210 B foot Crystal foot

One set per column, one body part per row, ascending level left to right. Filling
slots 0..11 in order instead would run all four caps along the top row and wrap
mid-set, which is what it did before.

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

Huzam
-----
Three tabs: lv210 weapons (13), lv230 weapons (13), and the two shields. Both
weapon tiers are one item per weapon type, so a tab is a complete set.

Huzam needs the widened shop-slot encoding (`rose/common/store_item_code.h`) and a
client and server built with it. Retail packs a slot as `type * 1000 + id`, which
cannot express an id above 999: the lv230 weapons sit at LIST_WEAPON rows 1355-1367
and the lv210 ones at 1368-1379, so a row-1379 weapon packed the old way becomes
`8 * 1000 + 1379 = 9379` and decodes as type 9 id 379 -- a subweapon. Twenty-five of
the twenty-six were unsellable before that change. Anything at or above
`kWideBase` (100000) is the wide form; everything below is untouched legacy, which
is why `Dual Tornado Rifles` (row 381, encode 8381) sits in the lv210 tab in the old
form while its neighbours use the new one.

**If you run this against a client or server built before that helper existed, the
wide-form slots decode as garbage.** Ship all three together.

Membership is derived by scanning the item tables for requirement type 31 (character
level) rather than hardcoding rows, so a re-import cannot silently leave this stale;
the expected counts are asserted before anything is written.

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

SELL_TAB_COLS = (21, 22, 23, 24)
SLOT0_COL = 2                       # STORE_ITEM(I, T) = get_int32(I, 2 + T)
SLOT_COUNT = 48
GRID_COLS = 8                       # 48 slots over the 6 rows dlgstore.xml draws

# Mirrors rose/common/store_item_code.h. Keep the two in step.
LEGACY_MAX_ITEM_NO = 999
WIDE_BASE = 100000

AZIM_NPC_ROW = 2124
HUZAM_NPC_ROW = 2122

# (table, item type, first Crystal row, first lv210 row). The two tiers are
# contiguous per table because they were appended back to back, Crystal first.
ARMOUR_SLOTS = [
    ("LIST_CAP", 2, 977, 981),
    ("LIST_BODY", 3, 904, 908),
    ("LIST_ARMS", 4, 880, 884),
    ("LIST_FOOT", 5, 878, 882),
]
CLASSES = ["Soldier", "Muse", "Hawker", "Dealer"]

# Huzam's tabs: (label, table, item type, required level, expected item count).
WEAPON_TABS = [
    ("Weapons lv210", "LIST_WEAPON", 8, 210, 13),
    ("Weapons lv230", "LIST_WEAPON", 8, 230, 13),
]
SHIELD_LEVELS = (210, 230)
SHIELD_EXPECTED = 2


def encode_item(item_type, item_no):
    """Same split as Rose::Store::encode_store_item."""
    if item_no <= LEGACY_MAX_ITEM_NO:
        return item_type * 1000 + item_no
    return item_type * WIDE_BASE + item_no


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


def req_level(z, row):
    """Requirement pairs live at cols 19/20 and 21/22; type 31 is character level."""
    for a, b in ((19, 20), (21, 22)):
        if z.get(row, a).strip() == b"31":
            v = z.get(row, b).strip()
            return int(v) if v.lstrip(b"-").isdigit() else 0
    return 0


def rows_at_level(m, table, level):
    z = m.Stb(os.path.join(STB, table + ".STB"))
    return [r for r in range(1, z.rows)
            if z.get(r, 0).strip() and req_level(z, r) == level]


def armour_tab(k):
    """{slot: encoded item} for class index k (0=Soldier .. 3=Dealer).

    One set per column, one body part per row -- see the layout in the docstring.
    Sparse on purpose: the client places each icon at its slot index, so the unused
    columns of each row stay empty instead of the next set wrapping into them.
    """
    out = {}
    for row, (_tbl, typ, crystal0, lv210_0) in enumerate(ARMOUR_SLOTS):
        sets = [lv210_0 + 2 * k,        # lv210 set A
                lv210_0 + 2 * k + 1,    # lv210 set B
                crystal0 + k]           # lv220 Crystal, highest level last
        for col, item_row in enumerate(sets):
            out[row * GRID_COLS + col] = encode_item(typ, item_row)
    return out


def build_plan(m):
    """[(npc_row, label, [encoded items]), ...] in tab order, or exit on a mismatch."""
    # --- Azim: verify the tier rows are where we think, then lay out four tabs
    bad = []
    for tbl, _typ, crystal0, lv210_0 in ARMOUR_SLOTS:
        z = m.Stb(os.path.join(STB, tbl + ".STB"))
        for r, want in [(crystal0, 220), (crystal0 + 3, 220),
                        (lv210_0, 210), (lv210_0 + 7, 210)]:
            if req_level(z, r) != want or not z.get(r, 0).strip():
                bad.append(f"{tbl} row {r}: level {req_level(z, r)}, expected {want}")
    if bad:
        sys.exit("armour tier rows are not where expected -- refusing to write:\n  "
                 + "\n  ".join(bad))

    plan = [(AZIM_NPC_ROW, f"{CLASSES[k]} Armor", armour_tab(k)) for k in range(4)]

    # --- Huzam: derive membership from the tables rather than hardcoding rows
    for label, table, typ, level, expect_n in WEAPON_TABS:
        rows = rows_at_level(m, table, level)
        if len(rows) != expect_n:
            sys.exit(f"{table} has {len(rows)} items at level {level}, expected {expect_n} "
                     f"-- refusing to write; re-check the tier import")
        plan.append((HUZAM_NPC_ROW, label,
                     {i: encode_item(typ, r) for i, r in enumerate(rows)}))

    shields = []
    for level in SHIELD_LEVELS:
        shields += [encode_item(9, r) for r in rows_at_level(m, "LIST_SUBWPN", level)]
    if len(shields) != SHIELD_EXPECTED:
        sys.exit(f"found {len(shields)} shields, expected {SHIELD_EXPECTED}")
    plan.append((HUZAM_NPC_ROW, "Shields", {i: v for i, v in enumerate(shields)}))

    for npc, label, slots in plan:
        bad = [s for s in slots if not (0 <= s < SLOT_COUNT)]
        if bad:
            sys.exit(f"tab {label!r} places items at slots {bad}, outside 0..{SLOT_COUNT - 1}")
    per_npc = {}
    for npc, _label, _slots in plan:
        per_npc[npc] = per_npc.get(npc, 0) + 1
    for npc, n in per_npc.items():
        if n > len(SELL_TAB_COLS):
            sys.exit(f"NPC {npc} needs {n} tabs but only {len(SELL_TAB_COLS)} exist")
    return plan


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
        stl = m.Stl(os.path.join(STB, "LIST_SELL_S.STL"))
        plan = build_plan(m)
        bad = []
        for (npc, label, slots), row in zip(plan, saved["rows"]):
            want = {i: slots.get(i, 0) for i in range(SLOT_COUNT)}
            got = {i: int(s.get(row, SLOT0_COL + i).strip() or 0) for i in range(SLOT_COUNT)}
            if got != want:
                bad.append(f"row {row} ({label}) contents differ")
            # A tab whose name key is missing renders unlabeled -- the bug that hid
            # Huzam's second and third tabs. Check it, do not assume it.
            key = f"LSEL{row}"
            if s.get(row, 1).decode("latin-1", "replace") != key or not stl.has(key):
                bad.append(f"row {row} ({label}) has no name entry ({key})")
        for npc in (AZIM_NPC_ROW, HUZAM_NPC_ROW):
            want = [r for (p, _l, _i), r in zip(plan, saved["rows"]) if p == npc]
            got = [int(n.get(npc, c).strip() or 0) for c in SELL_TAB_COLS]
            if got[:len(want)] != want or any(got[len(want):]):
                bad.append(f"NPC {npc} tabs are {got}, expected {want} then blank")
        print(f"    {len(plan)} tabs -> LIST_SELL rows {saved['rows']}, "
              f"{len(bad)} problems" + (": " + "; ".join(bad) if bad else ""))
        sys.exit(1 if bad else 0)

    if args.revert:
        if not saved:
            sys.exit("no sidecar -- nothing to revert")
        s = m.Stb(os.path.join(STB, "LIST_SELL.STB"))
        n = m.Stb(os.path.join(STB, "LIST_NPC.STB"))
        stl = m.Stl(os.path.join(STB, "LIST_SELL_S.STL"))
        for row in saved["rows"]:
            for c in range(0, s.cols):
                s.set(row, c, b"")
        for npc in saved.get("npcs", [AZIM_NPC_ROW]):
            for c in SELL_TAB_COLS:
                n.set(npc, c, b"")
        # Drop the tab-name keys too. Leaving them behind orphans an STL entry that
        # a later run silently reuses, which is how Huzam ended up with unnamed tabs
        # while Azim looked fine.
        drop = {f"LSEL{r}" for r in saved["rows"]}
        keep = [i for i, (k, _) in enumerate(stl.keys)
                if k.decode("latin-1", "replace") not in drop]
        removed = len(stl.keys) - len(keep)
        if removed:
            stl.keys = [stl.keys[i] for i in keep]
            stl.langs = [[rows[i] for i in keep] for rows in stl.langs]
        s.save(args.dry_run)
        n.save(args.dry_run)
        if removed:
            stl.save(args.dry_run)
        if not args.dry_run:
            os.remove(SIDECAR)
        print(f"    cleared LIST_SELL rows {saved['rows']}, {removed} name keys, "
              f"and the tabs on {saved.get('npcs', [AZIM_NPC_ROW])}")
        return

    if saved:
        print("already applied -- nothing to do (use --revert to undo)")
        return

    plan = build_plan(m)
    rows = free_sell_rows(m, len(plan))
    s = m.Stb(os.path.join(STB, "LIST_SELL.STB"))
    n = m.Stb(os.path.join(STB, "LIST_NPC.STB"))
    stl = m.Stl(os.path.join(STB, "LIST_SELL_S.STL"))

    per_npc = {}
    named = 0
    print(f"stocking {len(set(p for p, _l, _i in plan))} merchants "
          f"using LIST_SELL rows {rows}\n")
    for (npc, label, slots), row in zip(plan, rows):
        key = f"LSEL{row}"
        s.set(row, 0, label.encode("latin-1"))
        s.set(row, 1, key.encode("latin-1"))
        for i in range(SLOT_COUNT):
            s.set(row, SLOT0_COL + i, str(slots[i]).encode("ascii") if i in slots else b"")
        # The client reads the tab caption from LIST_SELL_S.STL via col 1, not from
        # col 0. Without this the tab draws with no label and is effectively hidden.
        if not stl.has(key):
            stl.append(key, row, label)
            named += 1
        slot = per_npc.setdefault(npc, 0)
        n.set(npc, SELL_TAB_COLS[slot], str(row).encode("ascii"))
        per_npc[npc] = slot + 1
        wide = sum(1 for v in slots.values() if v >= WIDE_BASE)
        print(f"    npc {npc}  tab {slot}  row {row:>3}  {label:<15} "
              f"{len(slots):>2} items ({wide} wide-form), slots "
              f"{min(slots)}..{max(slots)}")
    print(f"\n    {named} tab-name keys added to LIST_SELL_S.STL")

    if args.dry_run:
        print("\ndry run -- nothing written")
        return
    s.save(False)
    n.save(False)
    stl.save(False)
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({"rows": rows, "npcs": sorted(per_npc)}, fh, indent=1)
    print("\ndone. Restart the game server and rebake the client VFS.")
    print("Huzam's wide-form slots need a client AND server built with "
          "rose/common/store_item_code.h -- ship all three together.")


if __name__ == "__main__":
    main()
