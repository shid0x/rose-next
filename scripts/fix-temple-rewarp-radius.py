"""Stop the Forgotten Temple's endless reload loop for the 91-130 level bands.

Tester report (2026-11-12): turn in the Ice Key Fragments, talk to Est, enter
the Forgotten Temple, and the client reloads the map forever (the client log
is ~45 ``warp:`` lines in 70 s and nothing else; a relog loops again, since
the character is saved inside).

The temple (zones 56/57) runs ``2051-10`` on every arrival (LIST_ZONE col 22).
That chain, in ``QP201.QSD``, picks the player's band by quest (2051 = lv
71-90, 2052 = lv 91-110, 2053 = lv 111-130), and for the two upper bands it
starts with a rewarp:

* ``2052-11`` -- quest 2052, a Resurrection Spellbook, and within 20 m of B1's
  entrance (COND_006)  ->  warp to the 91-110 start point, 724 m away
* ``2053-11`` -- the same for 2053, to the 111-130 start point, 809 m away

so a player of those bands who arrives at the entrance (a login there, a GM
teleport) is moved to the band's own start. On the server, COND_006's radius
is scaled twice: ``CQuestTRIGGER::Init_COND`` multiplies it by 100 at load
(retail), and since dbdb8335 (2026-08-19) ``F_QSTCOND006`` multiplies it by 100
again at check time. "20 m" is 2 km, the start point is inside it, and every
arrival there passed the condition and warped to the same spot. Reproduced on a
local server: quest 2052, three spellbooks, login in zone 56 -> ``2052-11``
passed on every arrival at (566423, 483490), five warps a second. The 71-90
band never reaches these triggers (``2051-10`` / ``2051-10-0`` keep it first),
which is why a 2051 character enters normally.

The x10,000 radius is a known bug left alone on purpose (decision 11 in
doc/pathfinding-and-z-plan.md: fixing it would make 39 quests need a re-test),
so this is a data fix for the two triggers that loop on it: their radius goes
from 20 to 1. On today's server that is 100 m around the entrance (the start
points stay 7x that far out, so the rewarp still works and cannot repeat); if
the scaling is ever fixed it becomes 1 m, which a +/-5 m login scatter almost
never meets, so the rewarp would just stop -- never loop. An audit of every
zone's arrival chain found no other warp into its own zone.

The QSD is read by both sides. The server is what loops: restart the game
server. The client only replays the passed trigger's rewards (a warp is a
no-op there), so a client re-bake is not needed for the fix; bake it with the
next data release to keep the two copies equal.

Idempotent, --dry-run, verifies after writing. Backup in
build/fix-temple-rewarp-radius/.
"""
import argparse
import os
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

from rosefmt import qsd  # noqa: E402
from rosefmt.binio import read_file  # noqa: E402

PATH = os.path.join(ROOT, "data", "3DDATA", "QUESTDATA", "QP201.QSD")
BACKUP_DIR = os.path.join(ROOT, "build", "fix-temple-rewarp-radius")

TRIGGERS = ("2052-11", "2053-11")
COND_POSITION = 6
TEMPLE_ZONE = 56
OLD_RADIUS, NEW_RADIUS = 20, 1
# STR_COND_006: u32 size, i32 type, i32 zone, i32 x, i32 y, i32 z, i32 radius
RADIUS_OFF = 24


def radius_fields(blob):
    """[(trigger, absolute offset of iRadius, zone, radius)] for the targets."""
    out = []
    base = 0
    for name, ents, raw in qsd.walk(blob):
        base = blob.index(raw, base)
        if name not in TRIGGERS:
            base += len(raw)
            continue
        at = base + 1 + 8 + 2 + struct.unpack_from("<h", raw, 9)[0]
        for etype, ent in ents:
            if (etype & 0xFFFF) == COND_POSITION:
                zone, = struct.unpack_from("<i", ent, 8)
                radius, = struct.unpack_from("<i", ent, RADIUS_OFF)
                out.append((name, at + RADIUS_OFF, zone, radius))
            at += len(ent)
        base += len(raw)
    return out


def patch(blob):
    """Return (new blob, [(trigger, old, new)], {trigger names seen})."""
    b = bytearray(blob)
    changes = []
    found = set()
    for name, off, zone, radius in radius_fields(blob):
        if zone != TEMPLE_ZONE:
            raise SystemExit("%s: position condition names zone %d, expected %d"
                             % (name, zone, TEMPLE_ZONE))
        found.add(name)
        if radius == OLD_RADIUS:
            struct.pack_into("<i", b, off, NEW_RADIUS)
            changes.append((name, radius, NEW_RADIUS))
        elif radius != NEW_RADIUS:
            raise SystemExit("%s: unexpected radius %d" % (name, radius))
    return bytes(b), changes, found


def verify(path):
    problems = []
    blob = read_file(path)
    ok, _ = qsd.parse_ok(blob)
    if not ok:
        problems.append("%s: does not parse" % path)
    _, changes, found = patch(blob)
    if changes:
        problems.append("%s: %d radius still %d" % (path, len(changes), OLD_RADIUS))
    if found != set(TRIGGERS):
        problems.append("%s: missing %s" % (path, sorted(set(TRIGGERS) - found)))
    return problems


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    blob = read_file(PATH)
    new, changes, found = patch(blob)
    missing = set(TRIGGERS) - found
    if missing:
        raise SystemExit("QP201.QSD: triggers not found: %s" % sorted(missing))
    for name, old, value in changes:
        print("QP201.QSD %s position radius %d -> %d" % (name, old, value))
    if not changes:
        print("QP201.QSD: already patched")
        return
    if args.dry_run:
        print("dry run: nothing written")
        return
    os.makedirs(BACKUP_DIR, exist_ok=True)
    backup = os.path.join(BACKUP_DIR, "QP201.QSD")
    if not os.path.exists(backup):
        shutil.copyfile(PATH, backup)
    with open(PATH, "wb") as fh:
        fh.write(new)
    problems = verify(PATH)
    if problems:
        raise SystemExit("\n".join(problems))
    print("written and verified; restart the game server")


if __name__ == "__main__":
    main()
