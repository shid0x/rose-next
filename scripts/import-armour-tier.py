"""Import one complete armour tier (4 slots x 4 classes) from another ROSE data set.

Drives scripts/import-item.py once per piece, with stats we author rather than
the source's. That is the whole point: RoseZA gear is scaled roughly 4x ours, so
importing verbatim would not add a tier, it would delete the difficulty of every
monster we already have.

How the numbers are chosen
--------------------------
Everything is derived from OUR live tables, so the plan re-derives if our data
changes rather than sitting here as stale magic numbers:

  * Each class's target = its own current best-in-slot total x UPLIFT. Using each
    class's *own* ceiling keeps our existing class balance instead of importing
    RoseZA's, which is far more spread out (their Soldier set is 2.1x their
    Hawker set; ours are within 1.2x of each other).
  * Slot split uses OUR proportions (body ~45%, cap ~18%, arms ~15%, foot ~21%),
    not the source's. RoseZA's split is erratic and its Crystal Hawker suit is a
    plain outlier -- 13.8% of the set's DEF in the chest, against 28-32% in the
    boots -- which would have shipped a chest weaker than the gloves.
  * RES is targeted the same way off our own RES ceiling, so Muse keeps the
    highest resistance without inheriting the source's caster-gear quirks.

UPLIFT is fixed at the ratio the first imported set already used (900/437, the
Crystal Soldier tier), so later tiers stay comparable to it.

Re-running is safe: a piece whose name already exists in our table is skipped, so
this never appends a second copy.

Usage:
    python scripts/import-armour-tier.py --source "C:\\path\\to\\data" --dry-run
    python scripts/import-armour-tier.py --source "C:\\path\\to\\data"
    python scripts/import-armour-tier.py --source ... --only 52,53,54
"""
import argparse
import importlib.util
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OURS = os.path.join(ROOT, "data")

CLASSES = {51: "Soldier", 52: "Muse", 53: "Hawker", 54: "Dealer"}
SLOTS = ("body", "cap", "arms", "foot")
TABLE = {"body": "LIST_BODY", "cap": "LIST_CAP", "arms": "LIST_ARMS", "foot": "LIST_FOOT"}

# LIST_CLASS.STB row -> the broad class it belongs to. Job-specific gear (rows
# 11-18, one per second job) still draws its budget from the broad class's
# ceiling, because that is what the wearer's alternatives are. Verified our
# LIST_CLASS.STB matches the source's exactly, so these indices mean the same
# thing in both data sets.
JOB_TO_CLASS = {11: 51, 12: 51, 13: 52, 14: 52, 15: 53, 16: 53, 17: 54, 18: 54}

# Tiers. `sets` maps the item's class-filter value to its source row -- which is
# NOT generally the same row across the four tables (row 732 is a plate mail in
# LIST_BODY but a Merchant Hat in LIST_CAP); it happens to line up for both of
# these, and the script checks that the row it reads is really the set it wants.
#
# `budget` picks the DEF/RES total each class aims for:
#   "uplift"   - multiply that class's own pre-tier ceiling
#   "midpoint" - sit halfway between the pre-tier ceiling and an existing tier,
#                which is what makes a level-210 set a real step rather than a
#                sidegrade of the 200s or a near-copy of the 220s
TIERS = {
    "crystal": {
        "name": "Crystal",
        "req_level": 220,
        "sets": {51: 242, 52: 243, 53: 244, 54: 245},
        "budget": ("uplift", 900 / 437),
        # The source misspells "Crystal" on three of the four Hawker pieces.
        "name_fixes": {"Cyrstal": "Crystal"},
        "ignore_above": 200,
    },
    "lv210": {
        "name": "lv210 job sets",
        "req_level": 210,
        # one set per second job: Knight, Champion, Mage, Cleric, Raider,
        # Scout, Bourgeois, Artisan
        "sets": {11: 47, 12: 46, 13: 76, 14: 77, 15: 106, 16: 107, 17: 136, 18: 137},
        "budget": ("midpoint", 220),
        "name_fixes": {},
        "ignore_above": 200,
    },
}


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


def fix_name(nm, tier):
    for bad, good in tier["name_fixes"].items():
        nm = nm.replace(bad, good)
    return nm


def read_rows(imp, base, slot):
    _, _, rows, cols, d = imp.stb_read(
        os.path.join(base, "3DDATA", "STB", TABLE[slot] + ".STB"))
    return rows, d


def req_level(row):
    for c in (19, 21):
        if row[c].strip() == b"31":
            v = row[c + 1].strip()
            return int(v) if v.isdigit() else 0
    return 0


def tier_names(imp, source, tier):
    """{(set_key, slot): source item name} for every piece of this tier."""
    out = {}
    for slot in SLOTS:
        _, d = read_rows(imp, source, slot)
        for key, row in tier["sets"].items():
            out[(key, slot)] = fix_name(d[row][0].decode("euc-kr", "replace").strip(), tier)
    return out


def our_names(imp):
    """{slot: {names already in our table}} -- used to skip re-imports."""
    out = {}
    for slot in SLOTS:
        rows, d = read_rows(imp, OURS, slot)
        for i in range(1, rows - 1):
            nm = d[i][0].decode("euc-kr", "replace").strip()
            if nm:
                out.setdefault(slot, set()).add(nm.lower())
    return out


def measure(imp, tier, names):
    """Per broad class: the pre-tier ceiling, and any existing higher tier.

    Both deliberately ignore this tier's own pieces. Without that, a second run
    measures what the first run added and targets a multiple of *that*, and the
    skip-by-name guard hides the inflation because the numbers still print.
    """
    exclude = {n.lower() for n in names.values()}
    ceiling, higher = {}, {}
    ref = tier["budget"][1] if tier["budget"][0] == "midpoint" else None
    for slot in SLOTS:
        rows, d = read_rows(imp, OURS, slot)
        for i in range(1, rows - 1):
            nm = d[i][0].decode("euc-kr", "replace").strip()
            if not nm or nm.lower() in exclude:
                continue
            try:
                cls = int(d[i][16] or 0)
                dfn, res = int(d[i][31] or 0), int(d[i][32] or 0)
            except ValueError:
                continue
            cls = JOB_TO_CLASS.get(cls, cls)
            if cls not in CLASSES:
                continue
            lv = req_level(d[i])
            if lv <= tier["ignore_above"] and dfn > ceiling.get((cls, slot), (0, 0))[0]:
                ceiling[(cls, slot)] = (dfn, res)
            if ref is not None and lv == ref and dfn > higher.get((cls, slot), (0, 0))[0]:
                higher[(cls, slot)] = (dfn, res)
    return ceiling, higher


def build_plan(imp, source, tier):
    """DEF/RES per (set, slot), budgeted per broad class, split by OUR slot shares."""
    names = tier_names(imp, source, tier)
    ceiling, higher = measure(imp, tier, names)
    mode, arg = tier["budget"]

    targets = {}
    for cls in CLASSES:
        dtot = sum(ceiling[(cls, s)][0] for s in SLOTS)
        rtot = sum(ceiling[(cls, s)][1] for s in SLOTS)
        if mode == "uplift":
            targets[cls] = (round(dtot * arg), round(rtot * arg))
        else:
            hd = sum(higher.get((cls, s), (0, 0))[0] for s in SLOTS)
            hr = sum(higher.get((cls, s), (0, 0))[1] for s in SLOTS)
            if not hd:
                sys.exit(f"no level-{arg} gear found for {CLASSES[cls]} to sit below")
            targets[cls] = (round((dtot + hd) / 2), round((rtot + hr) / 2))

    plan = {}
    for key in tier["sets"]:
        cls = JOB_TO_CLASS.get(key, key)
        dT, rT = targets[cls]
        dtot = sum(ceiling[(cls, s)][0] for s in SLOTS)
        rtot = sum(ceiling[(cls, s)][1] for s in SLOTS)
        for s in SLOTS:
            plan[(key, s)] = (round(dT * ceiling[(cls, s)][0] / dtot),
                              round(rT * ceiling[(cls, s)][1] / rtot))
    return plan, our_names(imp), names, targets


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tier", default="crystal", choices=sorted(TIERS))
    ap.add_argument("--source", required=True, help="path to the source data directory")
    ap.add_argument("--only", help="comma-separated set keys to import (default: all)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not os.path.isdir(os.path.join(OURS, "3DDATA")):
        sys.exit("data/3DDATA not found")
    tier = TIERS[args.tier]
    imp = load_importer()
    plan, have, names, targets = build_plan(imp, args.source, tier)
    want = ([int(x) for x in args.only.split(",")] if args.only else sorted(tier["sets"]))

    print(f"== {tier['name']}, required level {tier['req_level']}, "
          f"budget {tier['budget'][0]} {tier['budget'][1]}\n")
    for cls in sorted(CLASSES):
        print(f"   {CLASSES[cls]:<8} target DEF {targets[cls][0]:>5}   RES {targets[cls][1]:>5}")
    print()
    print(f"{'set':<5}{'slot':<6}{'source name':<28}{'DEF':>6}{'RES':>6}   action")
    jobs = []
    for key in want:
        row = tier["sets"][key]
        for slot in SLOTS:
            nm = names[(key, slot)]
            dfn, res = plan[(key, slot)]
            done = nm.lower() in have.get(slot, set())
            print(f"  {key:<3}{slot:<6}{nm:<28}{dfn:>6}{res:>6}   "
                  f"{'SKIP (already imported)' if done else 'import'}")
            if not done:
                jobs.append((key, slot, row, nm, dfn, res))
    if not jobs:
        print("\nnothing to do.")
        return

    print(f"\n{len(jobs)} piece(s) to import\n")
    for key, slot, row, nm, dfn, res in jobs:
        cmd = [sys.executable, os.path.join(HERE, "import-item.py"),
               "--type", slot, "--source", args.source, "--source-row", str(row),
               "--def", str(dfn), "--res", str(res),
               "--req-level", str(tier["req_level"]), "--copy-icon", "--name", nm]
        if args.dry_run:
            cmd.append("--dry-run")
        print(f"--- set {key} {slot}: {nm}")
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        for line in (r.stdout + r.stderr).splitlines():
            if any(k in line for k in ("WARNING", "importing", "model:", "icon:",
                                       "verified", "DONE", "rror")):
                print("    " + line)
        if r.returncode != 0:
            sys.exit(f"import failed for set {key} {slot} (exit {r.returncode}):\n"
                     f"{r.stdout}{r.stderr}")

    print("\ndone." + ("  (dry run)" if args.dry_run else
                       "  Rebake the VFS and restart servers + client."))


if __name__ == "__main__":
    main()
