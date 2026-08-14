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

# The tier to import. Source rows happen to line up across the four tables for
# this set; that is NOT generally true (row 732 is a plate mail in LIST_BODY but
# a Merchant Hat in LIST_CAP), so any new tier needs its rows looked up per table.
TIER_NAME = "Crystal"
SOURCE_ROW = {51: 242, 52: 243, 53: 244, 54: 245}
REQ_LEVEL = 220
UPLIFT = 900 / 437                      # what the Crystal Soldier tier already used

# The source misspells "Crystal" on three of the four Hawker pieces.
NAME_FIXES = {"Cyrstal": "Crystal"}


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


def scan(imp, base, skip_rows=()):
    """{(class, slot): (best_def, best_res)} and {(class, slot): {names}}."""
    best, names = {}, {}
    for slot in SLOTS:
        _, _, rows, _, d = imp.stb_read(os.path.join(base, "3DDATA", "STB", TABLE[slot] + ".STB"))
        for i in range(1, rows - 1):
            nm = d[i][0].decode("euc-kr", "replace").strip()
            if not nm:
                continue
            try:
                cls = int(d[i][16] or 0)
            except ValueError:
                continue
            if cls not in CLASSES:
                continue
            names.setdefault((cls, slot), set()).add(nm)
            if (cls, slot) in skip_rows and i in skip_rows[(cls, slot)]:
                continue
            try:
                dfn, res = int(d[i][31] or 0), int(d[i][32] or 0)
            except ValueError:
                continue
            if dfn > best.get((cls, slot), (0, 0))[0]:
                best[(cls, slot)] = (dfn, res)
    return best, names


def our_names(imp):
    """{(class, slot): {names already in our table}} -- used to skip re-imports."""
    out = {}
    for slot in SLOTS:
        _, _, rows, _, d = imp.stb_read(os.path.join(OURS, "3DDATA", "STB", TABLE[slot] + ".STB"))
        for i in range(1, rows - 1):
            nm = d[i][0].decode("euc-kr", "replace").strip()
            if nm:
                out.setdefault(slot, set()).add(nm.lower())
    return out


def fix_name(nm):
    for bad, good in NAME_FIXES.items():
        nm = nm.replace(bad, good)
    return nm


def tier_names(imp, source):
    """{(class, slot): source item name} for every piece of this tier."""
    out = {}
    for slot in SLOTS:
        _, _, _, _, d = imp.stb_read(
            os.path.join(source, "3DDATA", "STB", TABLE[slot] + ".STB"))
        for cls, row in SOURCE_ROW.items():
            out[(cls, slot)] = fix_name(d[row][0].decode("euc-kr", "replace").strip())
    return out


def build_plan(imp, source):
    """Per-class DEF/RES targets from our own ceiling, split by our own shares.

    The ceiling deliberately ignores this tier's own pieces. Without that, a
    second run measures the gear the first run added and targets 2x *that*, so
    every re-run silently inflates the tier -- and the skip-by-name guard hides
    it, because the numbers still print.
    """
    names = tier_names(imp, source)
    exclude = {n.lower() for n in names.values()}
    ceiling = {}
    for slot in SLOTS:
        _, _, rows, _, d = imp.stb_read(
            os.path.join(OURS, "3DDATA", "STB", TABLE[slot] + ".STB"))
        for i in range(1, rows - 1):
            nm = d[i][0].decode("euc-kr", "replace").strip()
            if not nm or nm.lower() in exclude:
                continue
            try:
                cls = int(d[i][16] or 0)
                dfn, res = int(d[i][31] or 0), int(d[i][32] or 0)
            except ValueError:
                continue
            if cls not in CLASSES:
                continue
            if dfn > ceiling.get((cls, slot), (0, 0))[0]:
                ceiling[(cls, slot)] = (dfn, res)

    plan = {}
    for cls in CLASSES:
        dtot = sum(ceiling[(cls, s)][0] for s in SLOTS)
        rtot = sum(ceiling[(cls, s)][1] for s in SLOTS)
        dT, rT = round(dtot * UPLIFT), round(rtot * UPLIFT)
        for s in SLOTS:
            plan[(cls, s)] = (round(dT * ceiling[(cls, s)][0] / dtot),
                              round(rT * ceiling[(cls, s)][1] / rtot))
    return plan, our_names(imp), names


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--source", required=True, help="path to the source data directory")
    ap.add_argument("--only", help="comma-separated class ids to import (default: all)")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not os.path.isdir(os.path.join(OURS, "3DDATA")):
        sys.exit("data/3DDATA not found")
    imp = load_importer()
    plan, have, names = build_plan(imp, args.source)
    want = ([int(x) for x in args.only.split(",")] if args.only else sorted(CLASSES))

    print(f"== {TIER_NAME} tier, required level {REQ_LEVEL}, uplift {UPLIFT:.3f}x\n")
    print(f"{'class':<9}{'slot':<6}{'source name':<26}{'DEF':>6}{'RES':>6}   action")
    jobs = []
    for cls in want:
        row = SOURCE_ROW[cls]
        for slot in SLOTS:
            src_name = names[(cls, slot)]
            dfn, res = plan[(cls, slot)]
            done = src_name.lower() in have.get(slot, set())
            print(f"  {CLASSES[cls]:<8}{slot:<6}{src_name:<26}{dfn:>6}{res:>6}   "
                  f"{'SKIP (already imported)' if done else 'import'}")
            if not done:
                jobs.append((cls, slot, row, src_name, dfn, res))
    if not jobs:
        print("\nnothing to do.")
        return

    print(f"\n{len(jobs)} piece(s) to import\n")
    for cls, slot, row, src_name, dfn, res in jobs:
        cmd = [sys.executable, os.path.join(HERE, "import-item.py"),
               "--type", slot, "--source", args.source, "--source-row", str(row),
               "--def", str(dfn), "--res", str(res), "--req-level", str(REQ_LEVEL),
               "--copy-icon", "--name", src_name]
        if args.dry_run:
            cmd.append("--dry-run")
        print(f"--- {CLASSES[cls]} {slot}: {src_name}")
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        for line in (r.stdout + r.stderr).splitlines():
            if any(k in line for k in ("WARNING", "importing", "model:", "icon:",
                                       "verified", "DONE", "Error", "error")):
                print("    " + line)
        if r.returncode != 0:
            sys.exit(f"import failed for {CLASSES[cls]} {slot} (exit {r.returncode}):\n"
                     f"{r.stdout}{r.stderr}")

    print("\ndone." + ("  (dry run)" if args.dry_run else
                       "  Rebake the VFS and restart servers + client."))


if __name__ == "__main__":
    main()
