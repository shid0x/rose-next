"""Give players a way to and from Oro.

The canonical entrance does not exist here. RoseZA reaches Oro through a warp
gate in a Junon Pyramid map (`ODIP01 <-> JZP03`, zone 43) and we have no Pyramid
at all -- zones 38-48 are empty in our LIST_ZONE. Worse, the 33 warp gates the
Oro import created are all *internal*: not one leaves the 71-82 range, so before
this script a player who reached Oro could never leave.

So both legs are NPC-driven instead:

    [Historian] Jones, Junon Polis  --> zone 72, the Portal Room
    [Interplanetary Guide] Nova     --> zone 2,  Junon Polis

Nova is the canonical return: her own dialog calls the portal "your only source
of ever returning to Junon".

This script writes only the two QSD triggers. The dialog options are appended by
the quest editor, which owns the .CON codec:

    quest-editor con-warp <data> 1104 orlo   Oro-TravelToOrlo   [--hook ...] --write
    quest-editor con-warp <data> 2101 junon  Oro-TravelToJunon  [--hook ...] --write

(run-oro-travel.ps1 does both plus this script, with the dialog text filled in.)

Destinations are the zones' own `start` event positions, read out of the .ZON
rather than hard-coded -- see zon_event_positions() for the format, which is not
what you would guess: the file stores x, **z, y** and every coordinate needs a
half-zone bias added before it is a world position.

No level gate: Oro's entry zones are level 200 monsters, but the deeper areas run
to 650 against our 240 cap, so the real gate has to be rebalancing, not a
doorman.

Idempotent, --dry-run, --selftest.
"""
import argparse
import importlib.util
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA = os.path.join(ROOT, "data")
QSD = os.path.join(DATA, "3DDATA", "QUESTDATA", "QP401.QSD")

# (trigger, destination zone, .ZON to read the landing spot from)
ZONE_JUNON_POLIS = 2
ZONE_PORTAL_ROOM = 72
TRIPS = [
    ("Oro-TravelToOrlo", ZONE_PORTAL_ROOM,
     os.path.join(DATA, "3DDATA", "MAPS", "ORO", "OROIP", "OROIP.zon")),
    ("Oro-TravelToJunon", ZONE_JUNON_POLIS,
     os.path.join(DATA, "3DDATA", "MAPS", "JUNON", "JPT01", "JPT01.zon")),
]
TRAVEL_PATTERN = "OroTravel"
LANDING_EVENT = "start"

REWD_007 = 0x01000000 | 7
# A real REWD_007 to copy the on-disk shape from, so we are not hand-rolling one.
TEMPLATE_TRIGGER = "PvP10-061"
TEMPLATE_QSD = os.path.join(DATA, "3DDATA", "QUESTDATA", "PVP10.QSD")

# .ZON geometry (zonefile.cpp ReadZoneINFO / ReadEventObjINFO)
MAP_COUNT_PER_ZONE_AXIS = 64
PATCH_COUNT_PER_MAP_AXIS = 16


def load_fate():
    """Reuse the QSD splice helpers from the fate importer."""
    spec = importlib.util.spec_from_file_location(
        "import_oro_fate", os.path.join(HERE, "import-oro-fate.py"))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, ["import-oro-fate.py", "--help"]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    return mod


def zon_event_positions(path):
    """{name: (world_x, world_y)} for a .ZON's named event positions.

    Two traps, both from CZoneFILE::ReadEventObjINFO:
      * the block is LUMP_EVENT_OBJECT = 1, not the economy block;
      * each entry is stored x, z, y -- the second float is height, not y --
        and both horizontal coordinates need a half-zone bias added, derived
        from the grid metadata in LUMP_ZONE_INFO (block 0).
    """
    b = open(path, "rb").read()
    n, = struct.unpack_from("<i", b, 0)
    blocks, o = {}, 4
    for _ in range(n):
        t, off = struct.unpack_from("<ii", b, o); o += 8
        blocks[t] = off
    if 0 not in blocks or 1 not in blocks:
        raise SystemExit(f"{path}: missing zone-info or event block")

    o = blocks[0] + 12                      # skip unused int, width, height
    grid_per_patch, = struct.unpack_from("<i", b, o); o += 4
    grid_size, = struct.unpack_from("<f", b, o)
    span = int(grid_size) * grid_per_patch * PATCH_COUNT_PER_MAP_AXIS
    bias = MAP_COUNT_PER_ZONE_AXIS // 2 * span + span // 2

    o = blocks[1]
    cnt, = struct.unpack_from("<i", b, o); o += 4
    out = {}
    for _ in range(cnt):
        x, _z, y = struct.unpack_from("<fff", b, o); o += 12
        ln = b[o]; o += 1
        name = b[o:o + ln].rstrip(b"\0").decode("latin-1", "replace"); o += ln
        out.setdefault(name, (x + bias, y + bias))
    return out


def landing_spot(zon_path):
    pos = zon_event_positions(zon_path)
    if LANDING_EVENT not in pos:
        raise SystemExit(f"{zon_path}: no {LANDING_EVENT!r} event position "
                         f"(have {sorted(pos)})")
    x, y = pos[LANDING_EVENT]
    return int(round(x)), int(round(y))


def build(dry):
    fate = load_fate()
    blob = open(QSD, "rb").read()
    if fate.qsd_has_trigger(blob, TRIPS[0][0]):
        print("   travel triggers already present, nothing to do")
        return

    tmpl = fate.qsd_find_entity(open(TEMPLATE_QSD, "rb").read(),
                                TEMPLATE_TRIGGER, REWD_007)
    if tmpl is None:
        raise SystemExit(f"no REWD_007 template in {TEMPLATE_TRIGGER}")

    triggers = []
    for trig, zone, zon in TRIPS:
        x, y = landing_spot(zon)
        # STR_REWD_007: int iZoneSN; int iX; int iY; BYTE btPartyOpt.
        # Party option 0 -- moving someone else's whole party across a planet
        # boundary on one member's click is not what anyone means by "yes".
        rew = fate.qsd_patch(tmpl, (0, "<iii", (zone, x, y)), (12, "<B", (0,)))
        triggers.append(fate.qsd_build_trigger(trig, [], [rew]))
        print(f"   {trig:<20} -> zone {zone} at ({x}, {y})  "
              f"[displayed {x/100:.0f},{y/100:.0f}]")

    out = fate.qsd_append_pattern(blob, TRAVEL_PATTERN, triggers)
    ok, consumed = fate.qsd_parse_ok(out)
    if not ok:
        raise SystemExit(f"rebuilt QSD does not re-parse ({consumed}/{len(out)})")
    for trig, zone, _ in TRIPS:
        if not fate.qsd_has_trigger(out, trig):
            raise SystemExit(f"{trig} missing after rebuild")
        got = fate.qsd_find_entity(out, trig, REWD_007)
        gz, gx, gy = struct.unpack_from("<iii", got, 8)
        if gz != zone:
            raise SystemExit(f"{trig}: wrote zone {gz}, wanted {zone}")
    fate.write_file(QSD, out, dry)
    print(f"   QP401.QSD {len(blob)} -> {len(out)} bytes, re-parsed clean")


def selftest():
    print("== selftest")
    for _, zone, zon in TRIPS:
        pos = zon_event_positions(zon)
        x, y = landing_spot(zon)
        # A world position inside a 64-map zone is ~5.12M/2 units; anything
        # near zero means the bias or the y/z swap is wrong.
        if not (100_000 < x < 10_000_000 and 100_000 < y < 10_000_000):
            raise SystemExit(f"{zon}: implausible landing spot ({x}, {y})")
        print(f"   {os.path.basename(zon):<12} zone {zone:<3} {len(pos)} event positions, "
              f"{LANDING_EVENT} = ({x}, {y})")
    fate = load_fate()
    blob = open(QSD, "rb").read()
    ok, consumed = fate.qsd_parse_ok(blob)
    if not ok:
        raise SystemExit(f"QP401.QSD does not parse ({consumed}/{len(blob)})")
    print("   QP401.QSD parses exactly")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args()
    if args.selftest:
        selftest()
        return
    print("== Oro travel triggers")
    build(args.dry_run)
    print("\ndone." + ("  (dry run -- nothing written)" if args.dry_run else ""))
    if not args.dry_run:
        print("next: append the dialog options with quest-editor con-warp "
              "(see the module docstring), then bake + restart.")


if __name__ == "__main__":
    main()
