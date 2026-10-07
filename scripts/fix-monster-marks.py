"""Give spawned monsters the type mark that says what they are.

The icon beside a focused monster's name is TARGETMARK.DDS cell `NPC_TYPE`
(LIST_NPC game col 27; see fix-target-marks.py for the sheet). 667's test client
names every row with its category, which pins down what retail meant:

    1 Basic   2 Guard   3 Fighter   4 Warrior   5 Ranger (bow)   6 Hunter (crossbow)
    7 Magician (staff)  8 Leader (bronze star)  9 Captain (silver star)
    10 Sub-boss (gold star)  11 King (crown)  16 Elite (skull; ours: bosses)

An audit of the 448 monster rows that some REGEN lump spawns (2026-10-07)
found the marks often said something else. This script applies the reviewed
outcome, one row at a time, in five groups:

  * king       -- bosses named King/Queen get the crown. Seven Oro kings showed a
                  role icon (Terrasaurus King a bow) because import-oro-667.py
                  folded 667's composite codes (75 = King-Ranger) to the role
                  digit; it now keeps the rank. Eight classic kings had the gold
                  star.
  * boss       -- unique bosses (a lone long-respawn spawn point, or a boss
                  sidecar) get the skull: Ulverick's Leader/General, the Oro
                  Devourer/Mecha/Giant Snapper, Deadly Drake (both), Executor
                  Kera, Deader Rex ... King Ulverick keeps his crown. Sikuku
                  Veteran 539 is a lone spawn at normal HP and keeps its role.
  * rank->role -- ordinary-strength monsters (HP < ~1.15x their level trend)
                  wearing a star go back to their role, 667's where it has one.
                  Worst case: the Eldeon prison carpet (Sikuku Cannibal, Jailer,
                  Guard, Warden, Predator, Raknu Prisoner: 1500+ spawn points)
                  all wore the sub-boss gold star.
  * shooter    -- ranged attackers (NPC_ATK_RANGE >= 600; melee is <= 450,
                  nothing between) get the bow, gun users the crossbow, ranged
                  magic (NPC_IS_MAGIC_DAMAGE) the staff.
  * star       -- stars re-graded by HP against the level trend: bronze below
                  1.4x, silver 1.4-2x, gold 2-3.5x (leaders at 1.15-1.3x kept).

Deliberately unchanged: the Halloween event bosses (Evil Jack, Ant Devil,
Poison Master, Lord Storm) keep the crown; Luna's gem quartet stays unmarked;
melee-range casters keep the staff (they cast skills); level-1 critters; crowns
and the Jrose marks 12/13 are never demoted on HP alone. Role icons 2-4 are
retail flavour now: the balance passes flattened Guard/Fighter/Warrior stats.

The HP ratio is the row's HP over the median of plain-role, non-boss spawned
monsters within ten levels -- recorded in the comments for review.

Each entry carries the name and the value it was reviewed against; a row whose
name or current value is something else is refused (a re-import moved it), so
the script never writes over a change it has not seen. Re-run it after any
import-*.py that rewrites LIST_NPC. The server only compares this column with
900, so client data only: re-bake.

    python scripts/fix-monster-marks.py --dry-run
    python scripts/fix-monster-marks.py
    python scripts/fix-monster-marks.py --verify
    python scripts/fix-monster-marks.py --restore

Undo is per cell (manifest in build/monster-marks/), never a whole-file copy.
"""

import argparse
import importlib.util
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, ".."))
STB = os.path.join(REPO, "data", "3DDATA", "STB", "LIST_NPC.STB")
BACKUP_DIR = os.path.join(REPO, "build", "monster-marks")
MANIFEST = os.path.join(BACKUP_DIR, "manifest.json")
TYPE_COL = 27

MARKS = {   # row: (name, reviewed value(s), new value)
    # ---- king
    203: ('Jelly King', 9, 11),   # HP 1.96x
    205: ('Woopie King', 10, 11),   # HP 2.96x
    207: ('Aqua King', 10, 11),   # HP 5.71x
    209: ('Grunter King', 10, 11),   # HP 6.03x
    212: ('Krawfy King', 10, 11),   # HP 5.39x
    376: ('Dreadnaught King', 10, 11),   # HP 8.38x
    382: ('Behemoth King', 10, 11),   # HP 12.99x
    396: ('Astarot King', 10, 11),   # HP 16.03x
    2207: ('Crowned Asper King', 7, 11),   # HP 3.66x
    2247: ('Desert Scavenger King', 3, 11),   # HP 5.36x
    2254: ('Poisontail Scorpio King', 4, 11),   # HP 3.65x
    2259: ('Prehistoric Mastyx King', 5, 11),   # HP 6.61x
    2265: ('Fearsome Terrasaurus King', 5, 11),   # HP 6.61x
    2276: ('Camouflaged Armastyx King', 5, 11),   # HP 6.61x
    2281: ('Golden Scarab King', 2, 11),   # HP 4.24x
    # ---- boss
    217: ('Krawfy Guardian', 10, 16),   # HP 10.73x
    219: ('Goblin Guardian', 10, 16),   # HP 8.88x
    532: ("Ulverick's Leader", 8, 16),   # HP 4.81x
    533: ("Ulverick's General", 9, 16),   # HP 8.28x
    1542: ('Frozen Tooth', 10, 16),   # HP 1.71x
    1571: ('Sikuku Elite Slaughterer', 10, 16),   # HP 2.36x
    1572: ('Cursed Ant Vagabond', 10, 16),   # HP 2.10x
    1582: ('Ikaness Heavygear', 10, 16),   # HP 2.12x
    1591: ('Moss Golem', 11, 16),   # HP 4.70x
    1719: ('Executor Kera', 11, 16),   # HP 5.18x
    2221: ('Giant Fiery Snapper', (7, 11), 16),   # HP 5.96x; 11 = import-oro-667's king fold
    2226: ('Grand Master Devourer', (6, 11), 16),   # HP 5.38x
    2296: ('Ikaness Mecha', (2, 11), 16),   # HP 6.00x
    2699: ('Deadly Drake Alpha', 11, 16),   # HP 8.93x
    2729: ('Deadly Drake', 11, 16),   # HP 13.50x
    4068: ('Deader Rex', 11, 16),   # HP 8.54x
    # ---- rank->role
    157: ('Doonga Leader', 8, 1),   # HP 1.07x
    366: ('Vulcan Leader', 8, 2),   # HP 0.62x
    437: ('Giant Pumpkin', 8, 4),   # HP 1.14x
    440: ('Pomic Ghost', 8, 1),   # HP 0.88x
    444: ('Giant Antares', 8, 3),   # HP 0.90x
    447: ('Dark Scavenger', 8, 3),   # HP 0.86x
    448: ('Sandstorm Scavenger', 9, 3),   # HP 1.08x
    542: ('Sikuku Hunter', 9, 1),   # HP 0.87x
    1531: ('Sabertooth', 8, 3),   # HP 0.76x
    1532: ('Little Frozen Tooth', 9, 3),   # HP 0.97x
    1533: ('Little Frozen Tooth', 9, 3),   # HP 1.00x
    1541: ('Mountain Bebeg Leader', 8, 4),   # HP 0.78x
    1711: ('Raknu Prisoner', 10, 1),   # HP 0.96x
    1712: ('Raknu Prisoner', 10, 1),   # HP 0.96x
    1713: ('Sikuku Cannibal', 10, 4),   # HP 0.96x
    1714: ('Sikuku Jailer', 10, 3),   # HP 0.96x
    1715: ('Sikuku Guard', 10, 2),   # HP 0.96x
    1716: ('Sikuku Warden', 10, 4),   # HP 0.96x
    1717: ('Sikuku Predator', 10, 1),   # HP 0.96x
    1718: ('Sikuku Cannibal Chef', 10, 2),   # HP 0.96x
    2549: ('Beeberu', 9, 7),   # HP 0.89x, ranged magic
    2690: ('D=Victim Alpha', 10, 1),   # HP 0.96x
    2691: ('D=Victim Alpha', 10, 1),   # HP 1.02x
    2692: ('Revived Quarantine Officer Alpha', 9, 1),   # HP 0.86x
    2701: ('D=Victim', 10, 1),   # HP 0.73x
    2702: ('D=Victim', 10, 1),   # HP 0.78x
    2703: ('Revived Quarantine Officer', 9, 1),   # HP 0.61x
    # ---- shooter
    31: ('Dalping', 1, 5),
    41: ('Flanae', 1, 7),
    42: ('Elder Flanae', 3, 7),
    43: ('Red Flanae', 3, 7),
    45: ('Big Flanae', 2, 7),
    51: ('Smouly', 1, 7),
    52: ('Elder Smouly', 3, 7),
    261: ('Goblin Jar', 2, 5),
    274: ('Goblin Server', 1, 5),
    275: ('Gold Mine Goblin Server', 3, 5),
    361: ('FrostWorm', 2, 7),
    362: ('Elder FrostWorm', 2, 7),
    371: ('Basilisk', 2, 5),
    571: ('Rotten Tree', 1, 5),
    1721: ('Sikuku Infiltrator', 6, 7),
    1947: ('Police Zombie', 3, 6),
    1948: ('Policewoman Zombie', 3, 6),
    2218: ('Fiery Snapper', 4, 7),
    2527: ('Calaplasinos', 3, 7),
    2697: ('Evil Eye Alpha', 1, 5),
    2716: ('Evil Eye', 1, 5),
    2719: ('Woodnoid', 4, 5),
    # ---- star
    138: ('Grunter Leader', 8, 10),   # HP 2.12x
    139: ('Grunter Captain', 9, 10),   # HP 2.65x
    158: ('Doonga Captain', 9, 8),   # HP 1.39x
    283: ('Gold Mine Goblin Mage', 9, 8),   # HP 1.39x
    284: ('Goblin Leader', 9, 8),   # HP 1.39x
    344: ('Yeti Captain', 8, 9),   # HP 1.42x
    353: ('Rider Captain', 9, 10),   # HP 2.00x
    369: ('Winter Maul Leader', 8, 9),   # HP 1.55x
    1590: ('Moss Ant', 9, 8),   # HP 1.30x
}


def load_codec():
    spec = importlib.util.spec_from_file_location("rose_codec", os.path.join(HERE, "import-oro.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def cell(npc, r):
    return npc.get(r, TYPE_COL).decode("latin-1").strip()


def name(npc, r):
    return npc.get(r, 0).decode("latin-1").strip()


def write(npc):
    blob = npc.to_bytes()
    with open(STB, "wb") as f:
        f.write(blob)


def verify(npc):
    bad = [r for r, (n, _, new) in MARKS.items() if name(npc, r) != n or cell(npc, r) != str(new)]
    for r in bad:
        print("  BAD %d %r type=%s, want %d" % (r, name(npc, r), cell(npc, r), MARKS[r][2]))
    print("verify: %s (%d rows)" % ("OK" if not bad else "FAILED", len(MARKS)))
    return not bad


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    a = ap.parse_args()

    codec = load_codec()
    raw = open(STB, "rb").read()
    npc = codec.Stb(STB)
    if npc.to_bytes() != raw:
        raise SystemExit("LIST_NPC.STB does not round-trip through the codec; refusing to write")

    if a.verify:
        sys.exit(0 if verify(npc) else 1)

    if a.restore:
        if not os.path.exists(MANIFEST):
            raise SystemExit("nothing to restore: %s missing" % MANIFEST)
        man = json.load(open(MANIFEST))
        for r, old in man.items():
            npc.set(int(r), TYPE_COL, old)
        write(npc)
        os.remove(MANIFEST)
        print("restored %d cells" % len(man))
        return

    todo, refused = [], []
    for r, (n, reviewed, new) in sorted(MARKS.items()):
        cur = cell(npc, r)
        if name(npc, r) != n:
            refused.append("%d: name is %r, reviewed %r" % (r, name(npc, r), n))
        elif cur == str(new):
            continue
        elif cur not in {str(v) for v in (reviewed if isinstance(reviewed, tuple) else (reviewed,))}:
            refused.append("%d %s: type is %s, reviewed %s" % (r, n, cur, reviewed))
        else:
            todo.append((r, n, cur, new))
    for line in refused:
        print("REFUSED " + line)
    if refused:
        raise SystemExit("%d row(s) changed since review; nothing written" % len(refused))
    if not todo:
        print("nothing to do")
        verify(npc)
        return
    for r, n, cur, new in todo:
        print("  %4d %-34s %2s -> %d" % (r, n, cur, new))
    print("%d cell(s) to change" % len(todo))
    if a.dry_run:
        return

    os.makedirs(BACKUP_DIR, exist_ok=True)
    man = json.load(open(MANIFEST)) if os.path.exists(MANIFEST) else {}
    for r, _, cur, new in todo:
        man.setdefault(str(r), cur)
        npc.set(r, TYPE_COL, str(new))
    write(npc)
    with open(MANIFEST, "w") as f:
        json.dump(man, f, indent=1, sort_keys=True)
    if not verify(codec.Stb(STB)):
        raise SystemExit("verification failed")


if __name__ == "__main__":
    main()
