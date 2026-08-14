"""Bring Oro monsters' accuracy and evasion onto our own scale.

The symptom is that Oro fights feel like you cannot land a hit -- and that is
literally true for some monsters. Measured against a level 216 Raider with HIT
463 (server `/stats`), normal-attack hit chance is:

    Sikuku Predator (our content, lv207, AVOID 421)   83%
    Mastyx          (Oro, lv218, AVOID 370)           62%
    Terrasaurus     (Oro, lv220, AVOID 533)           36%
    Hooded Asper    (Oro, lv206, AVOID 775)           13%
    Fiery Snapper   (Oro, lv220, AVOID 666)            4%

The cause is not per-monster tuning, it is a scale mismatch across the whole
import. In the level 150-260 band our monsters sit at AVOID median 300 / HIT
median 442; Oro's sit at 556 / 905. Evo-era monsters both dodge ~1.8x more and
land ~2x more accurately than anything else in this game.

Why that produces a cliff rather than a slope: CCal::Get_DAMAGE does not scale
smoothly with accuracy. `Get_SuccessRATE` returns a score, and if it comes back
under 20 the attack is discarded unless a d100 clears 94 -- about a 95% miss. A
raider's score against these monsters lands right on that threshold, so a modest
AVOID difference flips whole fights between "fine" and "unplayable".

Both columns are scaled by a single factor each, so the relative differences
*within* Oro are preserved -- an Asper still dodges more than a Mastyx, it just
does so on our curve instead of the source's.

Scope is deliberately narrow: monsters that spawn **only** in Oro zones, are
named, and are level 150+. The spawn lists are read from the map REGEN lumps
rather than assuming an id range, because Chick and Chicken (rows 1275/1276)
also spawn there and must not be touched, and rows 6/7 are nameless placeholders.

This does NOT address the six bosses' DEF (3905-4145 makes them immune to damage
regardless of accuracy) or their level (up to 245, above our cap). Those are
separate.

Idempotent: records the original values in a sidecar so a second run is a no-op
and --restore can put them back. --dry-run and --verify are available.

Usage:
    python scripts/rebalance-oro-accuracy.py --dry-run
    python scripts/rebalance-oro-accuracy.py
    python scripts/rebalance-oro-accuracy.py --verify
    python scripts/rebalance-oro-accuracy.py --restore
"""
import argparse
import importlib.util
import json
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
NPC_STB = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_NPC.STB")
SIDECAR = os.path.join(ROOT, "data", "3DDATA", "STB", "LIST_NPC.oro-accuracy.json")

COL_LEVEL, COL_HIT, COL_AVOID = 7, 10, 13
MIN_LEVEL = 150
AVOID_FACTOR = 0.61          # Oro median 556 -> ~339, ours is 300
HIT_FACTOR = 0.51            # Oro median 905 -> ~462, ours is 442

ORO_MAP_DIRS = {"TOWN", "OROIP", "ODP01", "ODC01", "ODD01", "ODD02", "ODD03",
                "ODD04", "ODD05", "ODOS01", "ODRP01", "ODE01"}


def load_oro():
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


def regen_mobs(extra):
    """Mob ids named by one REGEN point: a name, then two (name, mob, count) lists."""
    o = 0

    def bstr():
        nonlocal o
        n = extra[o]
        o += 1
        s = extra[o:o + n]
        o += n
        return s

    bstr()
    ids = []
    for _ in range(2):
        cnt, = struct.unpack_from("<i", extra, o)
        o += 4
        for _ in range(cnt):
            bstr()
            mob, _num = struct.unpack_from("<ii", extra, o)
            o += 8
            ids.append(mob)
    return ids


def spawn_map(oro):
    """{mob id: {map dir names it spawns in}} across every map we have."""
    import collections
    import glob
    where = collections.defaultdict(set)
    for path in glob.glob(os.path.join(ROOT, "data", "3DDATA", "MAPS", "**", "*.IFO"),
                          recursive=True):
        zone = os.path.basename(os.path.dirname(path)).upper()
        try:
            buf, bounds = oro.read_ifo(path)
            objs, _ = oro.read_lump(buf, bounds, oro.LUMP_REGEN)
        except Exception:
            continue
        for o in objs or []:
            try:
                for mob in regen_mobs(o["extra"]):
                    if mob > 0:
                        where[mob].add(zone)
            except Exception:
                pass
    return where


def targets(oro, stb):
    """Rows to rescale, with a reason recorded for anything Oro-adjacent skipped."""
    where = spawn_map(oro)
    picked, skipped = [], []

    def gi(r, c):
        v = stb.get(r, c).strip()
        return int(v) if v.lstrip(b"-").isdigit() else 0

    for mob, zones in sorted(where.items()):
        if not (zones & ORO_MAP_DIRS):
            continue
        name = stb.get(mob, 0).decode("latin-1", "replace").strip()
        if not zones <= ORO_MAP_DIRS:
            skipped.append((mob, name, "also spawns outside Oro"))
        elif not name:
            skipped.append((mob, name, "no name (placeholder row)"))
        elif gi(mob, COL_LEVEL) < MIN_LEVEL:
            skipped.append((mob, name, f"level {gi(mob, COL_LEVEL)} < {MIN_LEVEL}"))
        else:
            picked.append(mob)
    return picked, skipped


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    args = ap.parse_args()

    oro = load_oro()
    stb = oro.Stb(NPC_STB)

    def gi(r, c):
        v = stb.get(r, c).strip()
        return int(v) if v.lstrip(b"-").isdigit() else 0

    saved = {}
    if os.path.exists(SIDECAR):
        with open(SIDECAR, encoding="utf-8") as fh:
            saved = {int(k): v for k, v in json.load(fh).items()}

    if args.restore:
        if not saved:
            sys.exit("no sidecar -- nothing to restore")
        for row, (hit, avoid) in saved.items():
            stb.set(row, COL_HIT, str(hit))
            stb.set(row, COL_AVOID, str(avoid))
        with open(NPC_STB, "wb") as fh:
            fh.write(stb.to_bytes())
        os.remove(SIDECAR)
        print(f"restored HIT/AVOID on {len(saved)} rows; sidecar removed")
        return

    if args.verify:
        if not saved:
            sys.exit("no sidecar -- the rebalance has not been applied")
        bad = [r for r, (h, a) in saved.items()
               if gi(r, COL_HIT) != round(h * HIT_FACTOR)
               or gi(r, COL_AVOID) != round(a * AVOID_FACTOR)]
        print(f"{len(saved)} rows recorded; {len(bad)} do not match the expected values"
              + (f": {bad}" if bad else ""))
        sys.exit(1 if bad else 0)

    rows, skipped = targets(oro, stb)
    if saved:
        print(f"already applied to {len(saved)} rows (sidecar present) -- nothing to do.")
        print("re-run with --restore first if you want to change the factors.")
        return

    print(f"AVOID x{AVOID_FACTOR}   HIT x{HIT_FACTOR}   on {len(rows)} Oro-only monsters\n")
    print(f"  {'id':>5}{'lv':>5}{'AVOID':>16}{'HIT':>16}  name")
    record = {}
    for r in rows:
        hit, avoid = gi(r, COL_HIT), gi(r, COL_AVOID)
        record[r] = (hit, avoid)
        nh, na = round(hit * HIT_FACTOR), round(avoid * AVOID_FACTOR)
        print(f"  {r:>5}{gi(r, COL_LEVEL):>5}{f'{avoid} -> {na}':>16}{f'{hit} -> {nh}':>16}  "
              f"{stb.get(r, 0).decode('latin-1', 'replace')}")
        stb.set(r, COL_HIT, str(nh))
        stb.set(r, COL_AVOID, str(na))
    if skipped:
        print("\n  skipped (Oro-adjacent but out of scope):")
        for mob, name, why in skipped:
            print(f"    {mob:>5}  {name or '<unnamed>':<24} {why}")

    if args.dry_run:
        print("\ndry run -- nothing written")
        return
    with open(NPC_STB, "wb") as fh:
        fh.write(stb.to_bytes())
    with open(SIDECAR, "w", encoding="utf-8") as fh:
        json.dump({str(k): v for k, v in record.items()}, fh, indent=1)

    chk = oro.Stb(NPC_STB)
    for r, (hit, avoid) in record.items():
        got_h = chk.get(r, COL_HIT).strip()
        got_a = chk.get(r, COL_AVOID).strip()
        assert got_h == str(round(hit * HIT_FACTOR)).encode(), (r, got_h)
        assert got_a == str(round(avoid * AVOID_FACTOR)).encode(), (r, got_a)
    print(f"\ndone -- {len(record)} rows rewritten and verified. "
          f"Sidecar: {os.path.basename(SIDECAR)}")
    print("Restart the game server (it caches STBs at startup); no client rebake needed "
          "for LIST_NPC stats, but bake anyway if you are deploying.")


if __name__ == "__main__":
    main()
