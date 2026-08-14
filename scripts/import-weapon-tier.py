"""Add a weapon tier, using original models imported from another ROSE data set.

The source contributes **models and icons only**; every stat that touches combat
is authored here. Two independent reasons:

  * Scale. QQ-iROSE's weapons average 1.51x our attack power at level 200 (their
    2H axe 600 to our 397); RoseZA's are 0.42x ours and slower. Neither is our
    curve, so copying either would move the whole game rather than add a tier.
  * Attack speed. `attack_speed = 1500 / (STB value + 5)`, so a *higher* stored
    value is a SLOWER weapon. Evo-era values run ~6 above ours across every
    type, which silently costs about a third of the attack rate. Speed is taken
    from our own weapon of the same type instead.

Picking donors by "highest ATK at the source's top tier" does not work, and that
is how the first attempt produced a tier of duplicates: the highest-ATK level-200
weapons in another iROSE-lineage data set are the *shared retail* ones -- our
Caliburn is their Caliburn. Tiers therefore name their source rows explicitly,
and `--check-art` verifies that every one brings a mesh we do not already have.

Re-running is safe: a weapon whose name already exists here is skipped.

Usage:
    python scripts/import-weapon-tier.py --tier r2 --source "C:\\path" --check-art
    python scripts/import-weapon-tier.py --tier r2 --source "C:\\path" --dry-run
    python scripts/import-weapon-tier.py --tier r2 --source "C:\\path"
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
WEAPON_ZSC = os.path.join("3DDATA", "WEAPON", "LIST_WEAPON.ZSC")

COL_NAME, COL_TYPE, COL_ICON, COL_ATK, COL_SPEED = 0, 4, 9, 35, 36
TYPE_NAMES = {
    211: "1H sword", 212: "1H blunt", 221: "2H sword", 222: "spear", 223: "2H axe",
    231: "bow", 232: "gun", 233: "launcher", 241: "staff", 242: "wand",
    251: "katar", 252: "dual", 253: "dual gun", 271: "crossbow",
}

# Our own measured tier increment is about +9% ATK per 15 levels (185 -> 200), so
# `step` is that rate carried over the tier's distance from our level-200 top.
TIERS = {
    # QQ-iROSE's "r2" set: one model per type under 3ddata/weapon/r2/, and the
    # only complete set in that data set whose art we do not already own. It
    # appears at source levels 201 and 230 sharing the same meshes; the 230 rows
    # are the ones taken. Crossbow uses row 75, not 76/77 -- all three are the
    # same Akela Bowgun, but only 75 carries an icon from the r2 block (3384-
    # 3404); the others point at unrelated "P2P"-branded sprites.
    "r2": {
        "label": "Viper / Akela / Matrix / Oasis",
        "req_level": 230,
        "step": 1.18,
        "rows": {211: 26, 212: 55, 221: 124, 222: 185, 223: 153, 231: 225,
                 232: 252, 233: 280, 241: 325, 242: 354, 251: 424, 252: 451,
                 271: 75},
        "strip": "",
    },
    # QQ-iROSE's "newwep" set under 3ddata/weapon/weapon/newwep/. Every model in
    # it appears twice: once at source level 215 named "P2P ..." and once at 255
    # with a clean name. The 255 rows are the donors, because the *icons* differ
    # -- the 215 block (6503-6515) has a "P2P" watermark painted into the sprite
    # itself, not just the name, so stripping the prefix would still ship
    # cash-shop branding on every inventory square. Same meshes either way.
    # No launcher or dual gun in this set; those types keep their existing best
    # until the r2 tier.
    "newwep": {
        "label": "Arcidian / Befoul / Intrepid",
        "req_level": 210,
        "step": 1.06,
        "rows": {211: 597, 212: 598, 221: 600, 222: 602, 223: 601, 231: 603,
                 232: 604, 241: 605, 242: 606, 251: 607, 252: 608, 271: 599},
        "strip": "",
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


def num(row, col):
    v = row[col].strip()
    return int(v) if v.lstrip(b"-").isdigit() else 0


def req_level(row):
    for c in (19, 21):
        if row[c].strip() == b"31":
            v = row[c + 1].strip()
            return int(v) if v.isdigit() else 0
    return 0


def source_names(imp, base):
    """{row: english name}. Some data sets (QQ) leave the STB name column empty
    and keep names only in the STL, so the STL is the authority."""
    _, _, rows, cols, d = imp.stb_read(os.path.join(base, WEAPON_STB))
    try:
        keys, langs = imp.stl_read(os.path.join(base, "3DDATA", "STB", "LIST_WEAPON_S.STL"))
    except FileNotFoundError:
        return {i: d[i][COL_NAME].decode("euc-kr", "replace").strip() for i in range(1, rows - 1)}
    kmap = {k: i for i, (k, _) in enumerate(keys)}
    lang = 1 if len(langs) > 1 else 0
    out = {}
    for i in range(1, rows - 1):
        key = d[i][cols - 2]
        if key.strip() and key in kmap:
            out[i] = langs[lang][kmap[key]][0].decode("utf-8", "replace").strip()
        else:
            out[i] = d[i][COL_NAME].decode("euc-kr", "replace").strip()
    return out


def our_best(imp):
    """{type: (atk, speed, name)} -- the ATK to beat and the speed to keep.

    Measured strictly below the lowest tier this script manages, so every tier
    is a multiple of the same pre-existing ceiling. Taking "our current best"
    literally makes each tier compound on the last: importing the level-230 set
    first and then the 210 one would size the 210 weapons at 1.06x the 230s,
    i.e. stronger than the tier above them.
    """
    floor = min(t["req_level"] for t in TIERS.values())
    _, _, rows, _, d = imp.stb_read(os.path.join(OURS, WEAPON_STB))
    best = {}
    for i in range(1, rows - 1):
        nm = d[i][COL_NAME].decode("euc-kr", "replace").strip()
        if not nm or req_level(d[i]) >= floor:
            continue
        t, atk = num(d[i], COL_TYPE), num(d[i], COL_ATK)
        if t in TYPE_NAMES and atk > best.get(t, (0,))[0]:
            best[t] = (atk, num(d[i], COL_SPEED), nm)
    return best


def check_art(imp, base, tier):
    """Confirm every donor brings a mesh we do not already have."""
    ourz = imp.Zsc(os.path.join(OURS, WEAPON_ZSC))
    srcz = imp.Zsc(os.path.join(base, WEAPON_ZSC))
    have = {imp.norm(m) for m in ourz.meshes}
    ok = True
    print(f"{'type':<10}{'row':>6}   art")
    for t, row in sorted(tier["rows"].items()):
        if row >= len(srcz.objects) or not srcz.objects[row][1]:
            print(f"  {TYPE_NAMES[t]:<10}{row:>6}   NO MODEL")
            ok = False
            continue
        meshes = [imp.norm(srcz.meshes[p[0]]) for p in srcz.objects[row][1]]
        fresh = [m for m in meshes if m not in have]
        if not fresh:
            print(f"  {TYPE_NAMES[t]:<10}{row:>6}   *** all meshes already ours: {meshes}")
            ok = False
        else:
            print(f"  {TYPE_NAMES[t]:<10}{row:>6}   new: {', '.join(fresh)}")
    return ok


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--tier", required=True, choices=sorted(TIERS))
    ap.add_argument("--source", required=True)
    ap.add_argument("--check-art", action="store_true",
                    help="only report whether each donor model is new to us")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    imp = load_importer()
    tier = TIERS[args.tier]
    if args.check_art:
        ok = check_art(imp, args.source, tier)
        print("\n" + ("every donor brings new art." if ok else
                      "*** some donors duplicate art we already have"))
        sys.exit(0 if ok else 1)

    best = our_best(imp)
    names = source_names(imp, args.source)
    _, _, srows, _, sd = imp.stb_read(os.path.join(args.source, WEAPON_STB))
    _, _, orows, ocols, od = imp.stb_read(os.path.join(OURS, WEAPON_STB))
    have = {od[i][COL_NAME].decode("euc-kr", "replace").strip().lower()
            for i in range(1, orows - 1) if od[i][COL_NAME].strip()}

    print(f"== {args.tier} tier ({tier['label']}), level {tier['req_level']}, "
          f"ATK = our best x {tier['step']}\n")
    print(f"{'type':<10}{'row':>5}{'our best':>10}{'->':^4}{'new':>5}{'spd':>5}   name")
    jobs = []
    for t, row in sorted(tier["rows"].items()):
        if t not in best:
            print(f"  {TYPE_NAMES[t]:<10}{row:>5}   (we have no weapon of this type)")
            continue
        our_atk, our_spd, _ = best[t]
        new_atk = round(our_atk * tier["step"])
        nm = names.get(row, "").replace(tier["strip"], "").strip()
        if not nm:
            sys.exit(f"source row {row} has no resolvable name")
        src_lv = req_level(sd[row])
        done = nm.lower() in have
        print(f"  {TYPE_NAMES[t]:<10}{row:>5}{our_atk:>10}{'->':^4}{new_atk:>5}{our_spd:>5}   "
              f"{nm} (source lv{src_lv}, ATK {num(sd[row], COL_ATK)})"
              + ("   SKIP" if done else ""))
        if not done:
            jobs.append((t, row, nm, new_atk, our_spd))

    if not jobs:
        print("\nnothing to do.")
        return
    print(f"\n{len(jobs)} weapon(s) to import\n")
    for t, row, nm, atk, spd in jobs:
        cmd = [sys.executable, os.path.join(HERE, "import-item.py"),
               "--type", "weapon", "--source", args.source, "--source-row", str(row),
               "--atk", str(atk), "--atk-speed", str(spd),
               "--req-level", str(tier["req_level"]), "--name", nm, "--copy-icon"]
        if args.dry_run:
            cmd.append("--dry-run")
        print(f"--- {TYPE_NAMES[t]}: {nm}")
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        for line in (r.stdout + r.stderr).splitlines():
            if any(k in line for k in ("WARNING", "importing", "model:", "icon:",
                                       "ATTACK_", "verified", "DONE", "rror")):
                print("    " + line)
        if r.returncode != 0:
            sys.exit(f"failed on {nm} (exit {r.returncode}):\n{r.stdout}{r.stderr}")
    print("\ndone." + ("  (dry run)" if args.dry_run else
                       "  Rebake the VFS and restart servers + client."))


if __name__ == "__main__":
    main()
