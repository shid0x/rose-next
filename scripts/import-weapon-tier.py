"""Author a new weapon tier, using models imported from another ROSE data set.

Unlike the armour tier, this is NOT a stat upgrade lifted from the source. Evo
scaled armour up (~4x ours) and weapons *down*: at level 200 RoseZA's weapons are
about 0.42x our attack power AND slower (their STB speed values run ~6 higher
across every type, and attack_speed = 1500/(value+5), so higher is slower). On
items we both carry under the same name -- Caliburn, Jabberwock's Nail -- type,
attack range, motion type and the magic-damage flag all match exactly and only
ATK and speed differ, so that is a real balance difference, not a column misread.

The source therefore contributes **models and icons only**. Everything that
affects combat is authored here:

  * ATK = our own current best for that weapon type x STEP. Our natural tier
    increment measured across 185 -> 200 is about +9% for 15 levels, so +12% is
    the proportionate step for a 20-level one.
  * Attack speed is copied from OUR existing top weapon of the same type, never
    from the source, for the reason above.
  * Required level is the tier's level.

Two of the fourteen weapon types have no usable donor art in RoseZA -- it has a
single level-18 dual gun, and its only "blunt" weapons are crafting tools with
ATK 5 -- so those fall back to re-using our own existing top model for the type.
That keeps every class's progression complete rather than leaving two types
stranded a tier behind.

Re-running is safe: a weapon whose name already exists here is skipped.

Usage:
    python scripts/import-weapon-tier.py --source "C:\\path\\to\\data" --dry-run
    python scripts/import-weapon-tier.py --source "C:\\path\\to\\data"
"""
import argparse
import importlib.util
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OURS = os.path.join(ROOT, "data")
WEAPON_STB = os.path.join("3DDATA", "STB", "LIST_WEAPON.STB")

COL_TYPE, COL_ATK, COL_SPEED = 4, 35, 36
TYPE_NAMES = {
    211: "1H sword", 212: "1H blunt", 221: "2H sword", 222: "spear", 223: "2H axe",
    231: "bow", 232: "gun", 233: "launcher", 241: "staff", 242: "wand",
    251: "katar", 252: "dual", 253: "dual gun", 271: "crossbow",
}

# Proper nouns for the generated names -- "Rift Greatsword" reads like an item,
# "Rift 2H Sword" reads like a spreadsheet cell.
WEAPON_NOUN = {
    211: "Sword", 212: "Mace", 221: "Greatsword", 222: "Spear", 223: "Battleaxe",
    231: "Bow", 232: "Rifle", 233: "Launcher", 241: "Staff", 242: "Wand",
    251: "Katar", 252: "Dual Blades", 253: "Dual Guns", 271: "Crossbow",
}

# The source's crafting implements are typed as blunt weapons but carry ATK 5;
# they are tools, not weapons, and must not be picked as donor art.
MIN_DONOR_ATK = 50

TIER_NAME = "Rift"            # the tier's prefix in item names
REQ_LEVEL = 220
STEP = 1.12                   # +12%: our own 185->200 step is +9% over 15 levels
SOURCE_TIER_LEVEL = 200       # donor models are drawn from the source's top tier


def load_importer():
    spec = importlib.util.spec_from_file_location(
        "import_item", os.path.join(HERE, "import-item.py"))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, ["import-item.py"]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    return mod


def req_level(row):
    for c in (19, 21):
        if row[c].strip() == b"31":
            v = row[c + 1].strip()
            return int(v) if v.isdigit() else 0
    return 0


def num(row, col):
    v = row[col].strip()
    return int(v) if v.lstrip(b"-").isdigit() else 0


def scan(imp, base):
    """[(row_index, name, level, type, atk, speed)] for every named weapon."""
    _, _, rows, _, d = imp.stb_read(os.path.join(base, WEAPON_STB))
    out = []
    for i in range(1, rows - 1):
        nm = d[i][0].decode("euc-kr", "replace").strip()
        if not nm:
            continue
        t = num(d[i], COL_TYPE)
        if t not in TYPE_NAMES:
            continue
        out.append((i, nm, req_level(d[i]), t, num(d[i], COL_ATK), num(d[i], COL_SPEED)))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--source", required=True)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    imp = load_importer()
    ours = scan(imp, OURS)
    theirs = scan(imp, args.source)
    have = {w[1].lower() for w in ours}

    # our current best per type: the ATK to beat, and the speed to keep
    best = {}
    for i, nm, lv, t, atk, spd in ours:
        if atk > best.get(t, (0,))[0]:
            best[t] = (atk, spd, nm, lv)
    # the source's best donor at its top tier
    donor = {}
    for i, nm, lv, t, atk, spd in theirs:
        if lv != SOURCE_TIER_LEVEL or atk < MIN_DONOR_ATK:
            continue
        if atk > donor.get(t, (0,))[0]:
            donor[t] = (atk, i, nm)

    print(f"== {TIER_NAME} weapon tier, level {REQ_LEVEL}, ATK = our best x {STEP}\n")
    print(f"{'type':<10}{'our best':>9}{'->':^4}{'new':>5}{'spd':>5}   donor model")
    jobs = []
    for t in sorted(TYPE_NAMES):
        if t not in best:
            print(f"  {TYPE_NAMES[t]:<10}{'(we have none)':>20}")
            continue
        our_atk, our_spd, our_nm, our_lv = best[t]
        new_atk = round(our_atk * STEP)
        if t in donor:
            src_base, src_row, src_nm = args.source, donor[t][1], donor[t][2]
            note = f"{src_nm[:26]}"
        else:
            # No usable art in the source -- re-skin our own top model for the type.
            src_base, src_row, src_nm = OURS, next(
                i for i, nm, lv, tt, a, s in ours if tt == t and a == our_atk), our_nm
            note = f"{our_nm[:20]} (ours; source has no art)"
        name = f"{TIER_NAME} {WEAPON_NOUN[t]}"
        done = name.lower() in have
        print(f"  {TYPE_NAMES[t]:<10}{our_atk:>9}{'->':^4}{new_atk:>5}{our_spd:>5}   {note}"
              + ("   SKIP" if done else ""))
        if not done:
            # Re-skins source their art from our own atlas, so reuse that icon
            # index rather than appending a second identical sprite.
            reuse_icon = None
            if src_base == OURS:
                _, _, _, _, od = imp.stb_read(os.path.join(OURS, WEAPON_STB))
                reuse_icon = num(od[src_row], 9)
            jobs.append((t, src_base, src_row, name, new_atk, our_spd, reuse_icon))

    if not jobs:
        print("\nnothing to do.")
        return
    print(f"\n{len(jobs)} weapon(s) to import\n")
    for t, src_base, src_row, name, atk, spd, reuse_icon in jobs:
        cmd = [sys.executable, os.path.join(HERE, "import-item.py"),
               "--type", "weapon", "--source", src_base, "--source-row", str(src_row),
               "--atk", str(atk), "--atk-speed", str(spd),
               "--req-level", str(REQ_LEVEL), "--name", name]
        cmd += (["--icon", str(reuse_icon)] if reuse_icon is not None else ["--copy-icon"])
        if args.dry_run:
            cmd.append("--dry-run")
        print(f"--- {TYPE_NAMES[t]}: {name}")
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        for line in (r.stdout + r.stderr).splitlines():
            if any(k in line for k in ("WARNING", "importing", "model:", "icon:",
                                       "ATTACK_", "verified", "DONE", "rror")):
                print("    " + line)
        if r.returncode != 0:
            sys.exit(f"failed on {name} (exit {r.returncode}):\n{r.stdout}{r.stderr}")
    print("\ndone." + ("  (dry run)" if args.dry_run else
                       "  Rebake the VFS and restart servers + client."))


if __name__ == "__main__":
    main()
