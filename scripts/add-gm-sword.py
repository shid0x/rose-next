"""Append a "GM Sword" to LIST_WEAPON as a new item ID, for content testing.

Usage:
    python scripts/add-gm-sword.py --dry-run
    python scripts/add-gm-sword.py
    python scripts/add-gm-sword.py --verify

It is a straight clone of an existing one-handed sword (model, icon, ground-drop
model, sounds, effect are all reused as-is) with the stats replaced. Nothing
existing is modified -- the row, the LIST_WEAPON.ZSC object and the STL entry are
all appended, so the item is invisible to every existing drop table and quest.

Idempotent: re-running detects the row by name and stops. Spawn it with
`/item 8:<id>` (GM access_level 2048).

Why these numbers
-----------------
    ATTACK_POWER 9999  A one-handed sword's ATK is `STR*0.75 + LV*0.2 +
        (weaponAP + gradeATK) * (STR*0.05 + 29)/30` (CObjAVT::Cal_ATTACK), so the
        weapon term passes through at roughly 1:1. Monster damage is
        `ATK*(suc*0.03+26)*(ATK-DEF+250) / ((DEF+AVOID*0.4+5)*145)` and clamps at
        GameStaticConfig::MAX_DAMAGE (9999). Our hardest monsters sit near DEF
        1080 / AVOID 500, whose divisor is big enough that ~3000 ATK only lands
        ~1000 damage on them; 9999 is what actually pins the cap across the whole
        level range. All the intermediate math is float, so nothing overflows.
    ATTACK_SPEED 5     attack_speed = 1500/(value+5), so 5 -> 150 against the
        fastest shipped weapon's 100. The client plays the attack animation at
        attack_speed/100, so this is 1.5x -- fast, without outrunning the
        hit frame that consumes the queued DamageEvent.
    ADD_DATA AT_HIT +300, AT_ATK_SPD +50
        The two ITEM_ADD_DATA slots. Note CUserDATA::Cal_AddAbility does
        `m_iAddValue[nType] += nValue` with nType read straight from the STB, so
        the type MUST be a real AT_* value or the client writes out of bounds;
        and nValue is a `short`. AT_HIT does not beat the level gate (see below),
        it only stops ordinary misses.
    DURABITY 127       tagBaseITEM::m_cDurability is a 7-bit bitfield, so 127 is
        the true maximum. Weapon life decays on
        `RANDOM(710) + 1 - (durability + 600) >= 0`, which can never fire above
        110 -- so the sword never wears out and never drops to the ATK-0 branch.
        Durability also feeds hit rate (`durability * 0.8` in Cal_HIT).
    QUALITY 99         retail maximum; feeds hit rate as `quality * 0.6`.
    No NEED_DATA / EQUIP_REQUIRE_CLASS / REQUIRE_UNION
        Equippable by any class, any level, any union -- the point is to hand it
        to a fresh test character.
    USE_RESTRICTION 7  DONT_SELL | DONT_DROP_EXCHANGE | ENABLE_KEEPING: cannot be
        sold or traded into the economy by accident, but can still be banked.
    WEIGHT 0, BASE_PRICE 0, no craft/product columns.

What this deliberately does NOT fix
-----------------------------------
No weapon can beat the normal-attack level gate. CCal::Get_SuccessRATE discards
the attack outright when `(player_lv + 10) - monster_lv * 1.05 + rand(1..50)` is
non-positive, before ATK or HIT are looked at, and Get_DAMAGE then misses ~94% of
the time whenever the success value lands under 20. So a level-30 character still
cannot auto-attack a level-200 monster with this sword; test at a level near the
content, or use skills (their gate is the gentler `lv + 20 - mlv`).
"""
import argparse, importlib.util, os, struct, sys

HERE = os.path.dirname(os.path.abspath(__file__))
OURS = "data"
STB_REL = r"3DDATA\STB\LIST_WEAPON.STB"
STL_REL = r"3DDATA\STB\LIST_WEAPON_S.STL"
ZSC_REL = r"3DDATA\WEAPON\LIST_WEAPON.ZSC"

NAME = "GM Sword"
DESC = "GM test weapon. Not obtainable in game."
# Clone source: the strongest shipped one-handed sword (WEAPON_TYPE 211) that
# uses the standard one-hand motion set. Row 1368 "Arcidian Sword" looks flashier
# but is the only 211 in the table on motion type 5, an evo-era import artefact --
# cloning it would carry that risk along for no benefit.
SOURCE_NAME = "Viper Blade"

# col -> value. Column numbers are game columns (STBDATA::get_*), i.e. what the
# ITEM_* / WEAPON_* macros in rose/io/stb.h use; an STB editor shows them one to
# the right because it counts the row-name column.
OVERRIDES = {
    3: "7",       # ITEM_USE_RESTRICTION: no sell, no drop/exchange, bankable
    5: "0",       # ITEM_BASE_PRICE
    7: "0",       # ITEM_WEIGHT
    8: "99",      # ITEM_QUALITY  -> hit rate
    12: "", 13: "", 14: "", 15: "",   # craft/product columns: not craftable
    16: "",       # ITEM_EQUIP_REQUIRE_CLASS: any class
    17: "", 18: "",                   # ITEM_EQUIP_REQUIRE_UNION
    19: "", 20: "", 21: "", 22: "",   # ITEM_NEED_DATA: no level/stat requirement
    23: "",       # ITEM_NEED_UNION 0 (add-data slot 0 applies to every union)
    24: "20",     # ITEM_ADD_DATA_TYPE  0 = AT_HIT
    25: "300",    # ITEM_ADD_DATA_VALUE 0
    26: "",       # ITEM_NEED_UNION 1
    27: "24",     # ITEM_ADD_DATA_TYPE  1 = AT_ATK_SPD
    28: "50",     # ITEM_ADD_DATA_VALUE 1
    29: "127",    # ITEM_DURABITY (7-bit field max; >110 == never wears out)
    34: "1",      # WEAPON_MOTION_TYPE: standard one-hand
    35: "9999",   # WEAPON_ATTACK_POWER
    36: "5",      # WEAPON_ATTACK_SPEED -> 1500/(5+5) = 150
}


def load_importer():
    """import-item.py already carries the verified STB/ZSC/STL codecs."""
    spec = importlib.util.spec_from_file_location(
        "import_item", os.path.join(HERE, "import-item.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def stb_set_cells(imp, path, row, patches, dry):
    """Apply several cell overwrites in one read/write pass.

    stb_set_cell rebuilds the whole data section per call; doing that twenty
    times would be twenty rewrites of the file for one logical edit.
    """
    d, offset, rows, cols, data = imp.stb_read(path)
    if not 0 <= row < rows - 1:
        sys.exit("row %d out of range (%d rows)" % (row, rows - 1))
    for col, value in patches.items():
        if not 0 <= col < cols - 1:
            sys.exit("column %d out of range (%d data cols)" % (col, cols - 1))
        data[row][col] = value.encode("euc-kr") if isinstance(value, str) else value
    body = b"".join(struct.pack("<H", len(c)) + c for r in data for c in r)
    if not dry:
        with open(path, "wb") as fh:
            fh.write(d[:offset] + body)


def find_rows(data, name):
    return [i for i, r in enumerate(data)
            if r[0].decode("euc-kr", "replace") == name]


def report(imp, row):
    _, _, _, cols, data = imp.stb_read(os.path.join(OURS, STB_REL))
    r = data[row]

    def c(i):
        return r[i].decode("euc-kr", "replace")

    print("row %d  %-10s  key %s" % (row, c(0), c(cols - 2)))
    print("  ATK %s   speed %s (-> %d)   range %s   motion %s"
          % (c(35), c(36), 1500 // (int(c(36) or 0) + 5), c(33), c(34)))
    print("  quality %s   durability %s   weight %s   restriction %s"
          % (c(8), c(29), c(7), c(3) or "0"))
    print("  add-data: (%s, %s) (%s, %s)   need-data: (%s, %s) (%s, %s)"
          % (c(24) or "-", c(25) or "-", c(27) or "-", c(28) or "-",
             c(19) or "-", c(20) or "-", c(21) or "-", c(22) or "-"))
    print("  icon %s   field model %s   effect %s" % (c(9), c(10), c(39)))


def main():
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source-name", default=SOURCE_NAME,
                    help="name of the sword to clone (default: %s)" % SOURCE_NAME)
    ap.add_argument("--name", default=NAME)
    ap.add_argument("--desc", default=DESC)
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true",
                    help="print the existing GM Sword row and check the invariants")
    args = ap.parse_args()

    if not os.path.isdir(os.path.join(OURS, "3DDATA")):
        sys.exit("run from the repo root (data/3DDATA not found)")
    imp = load_importer()

    _, _, rows, cols, data = imp.stb_read(os.path.join(OURS, STB_REL))
    existing = find_rows(data, args.name)

    if args.verify:
        if not existing:
            sys.exit("no %r row in LIST_WEAPON.STB" % args.name)
        zsc = imp.Zsc(os.path.join(OURS, ZSC_REL))
        assert len(zsc.objects) == len(data), (
            "ZSC has %d objects to the STB's %d rows" % (len(zsc.objects), len(data)))
        keys, langs = imp.stl_read(os.path.join(OURS, STL_REL))
        for row in existing:
            report(imp, row)
            assert zsc.objects[row][1], "ZSC object %d has no model parts" % row
            key = data[row][cols - 2]
            ki = [i for i, (k, _) in enumerate(keys) if k == key]
            assert ki, "STL key %s missing" % key.decode()
            print("  STL: %r / %r"
                  % (langs[0][ki[0]][0].decode("utf-8", "replace"),
                     langs[0][ki[0]][1].decode("utf-8", "replace")))
        print("verified")
        return

    if existing:
        print("%r already exists at row %s -- nothing to do" % (args.name, existing))
        report(imp, existing[0])
        return

    src_rows = find_rows(data, args.source_name)
    if not src_rows:
        sys.exit("no weapon named %r in LIST_WEAPON.STB" % args.source_name)
    src_row = src_rows[0]
    src_icon = data[src_row][9].decode("ascii", "replace") or "0"
    print("cloning row %d %r (icon %s)" % (src_row, args.source_name, src_icon))

    argv = sys.argv
    # Clone within our own data set: same atlas, same ZSC, same assets on disk,
    # so the icon index is already correct and nothing needs copying.
    sys.argv = ["import-item.py",
                "--type", "weapon",
                "--source", OURS,
                "--source-row", str(src_row),
                "--icon", src_icon,
                "--name", args.name,
                "--desc", args.desc]
    if args.dry_run:
        sys.argv.append("--dry-run")
    try:
        imp.main()
    finally:
        sys.argv = argv

    new_id = rows - 1
    if args.dry_run:
        # The row does not exist yet, so there is nothing to patch -- show what
        # the second pass would write instead.
        print("\nwould then overwrite on row %d:" % new_id)
        for col in sorted(OVERRIDES):
            print("  col %2d: %r -> %r"
                  % (col, data[src_row][col].decode("euc-kr", "replace"), OVERRIDES[col]))
        print("\nDRY RUN - no files written (would be item 8:%d)" % new_id)
        return
    stb_set_cells(imp, os.path.join(OURS, STB_REL), new_id, OVERRIDES, False)
    print()
    report(imp, new_id)
    print("\nDONE - spawn with: /item 8:%d" % new_id)
    print("Delete the data/**/*.bak files this left behind before baking the VFS "
          "(src/pipeline packs every non-hidden file it walks).")


if __name__ == "__main__":
    main()
