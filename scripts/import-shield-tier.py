"""Add the level 210 / 230 shields.

Knights had no level-appropriate offhand at all. Every one of our 18 shields is
required level **0**, and the ladder tops out at DEF 59 (Vivor Shield) against a
900-DEF armour set -- there was no shield progression to have fallen behind,
there was no progression.

Both donors come from QQ-iROSE, both bring art we do not have, and both are real
shields rather than the joke offhands in that table (which also holds a Tron
shield and a literal iPad).

DEF is authored, not copied: the source values are 183/192 on QQ's inflated
scale. 88 and 122 are our own ceiling of 59 taken through the same 1.5x and
2.06x steps the armour tiers used, which keeps a shield at roughly 13% of a full
armour set -- the proportion it already has today.

Re-running is safe: a shield whose name already exists here is skipped.

Usage:
    python scripts/import-shield-tier.py --source "C:\\path\\to\\QQiroseData" --dry-run
    python scripts/import-shield-tier.py --source "C:\\path\\to\\QQiroseData"
"""
import argparse
import importlib.util
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OURS = os.path.join(ROOT, "data")
SUBWPN_STB = os.path.join("3DDATA", "STB", "LIST_SUBWPN.STB")

# (source row, name, required level, DEF, RES)
SHIELDS = [
    (115, "Golden Angel Shield",   210,  88, 30),
    (29,  "Ancient Davion Shield", 230, 122, 41),
]


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


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--source", required=True)
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    imp = load_importer()
    _, _, rows, _, d = imp.stb_read(os.path.join(OURS, SUBWPN_STB))
    have = {d[i][0].decode("euc-kr", "replace").strip().lower()
            for i in range(1, rows - 1) if d[i][0].strip()}

    jobs = [s for s in SHIELDS if s[1].lower() not in have]
    for row, name, lv, dfn, res in SHIELDS:
        print(f"  row {row:<5} {name:<24} lv{lv} DEF {dfn:>4} RES {res:>3}"
              + ("   SKIP (already imported)" if name.lower() in have else ""))
    if not jobs:
        print("\nnothing to do.")
        return

    print()
    for row, name, lv, dfn, res in jobs:
        cmd = [sys.executable, os.path.join(HERE, "import-item.py"),
               "--type", "subwpn", "--source", args.source, "--source-row", str(row),
               "--def", str(dfn), "--res", str(res), "--req-level", str(lv),
               "--copy-icon", "--name", name]
        if args.dry_run:
            cmd.append("--dry-run")
        print(f"--- {name}")
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        for line in (r.stdout + r.stderr).splitlines():
            if any(k in line for k in ("importing", "model:", "icon:", "DEFENCE",
                                       "RESIST", "required", "verified", "DONE", "rror")):
                print("    " + line)
        if r.returncode != 0:
            sys.exit(f"failed on {name} (exit {r.returncode}):\n{r.stdout}{r.stderr}")
    print("\ndone." + ("  (dry run)" if args.dry_run else
                       "  Rebake the VFS and restart servers + client."))


if __name__ == "__main__":
    main()
