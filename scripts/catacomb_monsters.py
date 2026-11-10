#!/usr/bin/env python3
"""The Karkia catacombs' monsters: rows, AI files and spawn points (milestone 2).

Design: doc/karkia-catacombs-implementation-brief.md, section 6. `import-catacombs.py`
applies everything here; this file is the plan and its builders.

    python scripts/catacomb_monsters.py            # the plan: rows, AI files, spawn map

The roster
----------
Six new LIST_NPC rows (2550-2555) at levels 235-240, the crypts' level band. Each is a
copy of one of Spire Village's "Alpha" rows (2690-2698: the Cemetery's undead, already
tuned for levels 228-240), so no art comes in and the theme fits: victims, ghouls,
wolves, spiders, the quarantine officer and, as the corridor elite, the devil child.
What a crypt row changes from its source:

  * name, STL key (LCATMOB<row>), its own LIST_NPC.CHR entry (a copy);
  * level, and the stats that follow level, by the step the Cemetery -> Alpha rows
    themselves took per level (HP +1.7%, ATK +0.4%, DEF and RES +0.5%, EXP +1.1%;
    accuracy and dodge unchanged). The sources sit at 228-238, so this moves little;
  * the drop table: 912, the crypts' own (`add-karkia-drops.py`);
  * the death trigger (col 41), cleared: the Alpha rows carry Spire quest triggers;
  * the AI file: a crypt copy of the source's, below.

The AI files
------------
Each crypt row runs a copy of its Alpha AI (KCAT_*.AIP, FILE_AI rows 2269-2274),
edited by `crypt_ai` rather than rewritten, so every skill the monster casts stays:

  * **leash 30 m** (the Alpha files run 80 m home): two cells. Until milestone 3 a
    chase longer than the 1 m path search can solve goes straight, through walls on
    screen;
  * **aggro 12 m** (15-20 m): until milestone 4 a monster sees through walls, and a
    wall is 15 m from the next corridor's middle;
  * idle wandering turns back 6 m from the spawn point (10-15 m), so a pack stays in
    its cell; calls for help reach 12 m;
  * **no summons**: the Alpha files summon a Ghost Seed on death at night, rarely a
    Corroded Golem or a Revived Veteran, and the Murilo more Murilos. Those are Spire
    rows with Spire AI, and a summon lands at random around its caller, walls or not.
    A crypt's own rare spawn can come back as a later idea.

The spawn points
----------------
One regen point in the middle of every other cell (a checkerboard), none in the hall,
the boss room, or any cell touching either: a revived player does not stand up in a
fight, and a boss fight is not joined through the wall. A cell's middle is open on
every day's maze, so the points stay when the walls start moving (milestone 3). Each
point is one of its crypt's three packs (`PACKS`), picked by a hash of the cell;
Reminiscence leans on victims and spiders, Sorrow on wolves and ghouls.

Regen rules (scripts/CLAUDE.md, "A Regen Point's Cap Is Not A Cap"): tacticPoint 100,
per-slot counts of 1-2 against a cap of 3-4, a species weighted by repeating its slot.
Interval 40 s: a cleared corridor stays quiet for a while. Range 4 m: inside the cell.
"""
import copy
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rosedata as oro  # noqa: E402
from rosefmt import aip, ifo  # noqa: E402
import catacomb_maze as maze  # noqa: E402

NPC_STB_REL = r"3DDATA\STB\LIST_NPC.STB"
NPC_STL_REL = r"3DDATA\STB\LIST_NPC_S.STL"
NPC_CHR_REL = r"3DDATA\NPC\LIST_NPC.CHR"
AI_STB_REL = r"3DDATA\STB\FILE_AI.STB"
AI_DIR_REL = r"3DDATA\AI"

C_NAME, C_LEVEL, C_HP, C_ATK, C_HIT, C_DEF, C_RES, C_AVOID = 0, 7, 8, 9, 10, 11, 12, 13
C_AI, C_EXP, C_DROP, C_TARGET, C_TYPE, C_DEADEVENT = 16, 17, 18, 25, 27, 41

DROP_TABLE = 912                    # add-karkia-drops.py: the crypts' table; rows 90/91 mirror it
GROWTH = {C_HP: 1.017, C_ATK: 1.004, C_DEF: 1.005, C_RES: 1.005, C_EXP: 1.011}   # per level
MARK_LEADER = 8                     # the bronze star (fix-target-marks.py)

LEASH_M = 30
AGGRO_M = 12
ALLY_M = 15                         # "move to an ally" searches
WANDER_M = 6
HELP_M = 12

VICTIM, GHOUL, WOLF, MURILO, OFFICER, FIEND = 2550, 2551, 2552, 2553, 2554, 2555
FIRST_AI_ROW = 2269                 # one past the end of FILE_AI on 2026-11-10
# row, source (Alpha) row, name, level, AI file, target mark (None = the source's)
ROSTER = (
    (VICTIM, 2691, "Entombed Victim", 235, "KCAT_VICTIM.AIP", None),
    (GHOUL, 2693, "Entombed Ghoul", 236, "KCAT_GHOUL.AIP", None),
    (WOLF, 2695, "Grave Wolf", 236, "KCAT_WOLF.AIP", None),
    (MURILO, 2696, "Tomb Murilo", 235, "KCAT_MURILO.AIP", None),
    (OFFICER, 2692, "Entombed Officer", 238, "KCAT_OFFICER.AIP", None),
    # the corridor elite: ~1.5x a plain monster's HP, four times its EXP, as its source
    (FIEND, 2698, "Crypt Fiend", 240, "KCAT_FIEND.AIP", MARK_LEADER),
)

# ---- spawn points
REGEN_INTERVAL_S = 40
REGEN_RANGE_M = 4
TACTIC_POINT = 100
# crypt -> packs: (five basic slots, two tactics slots, cap); a slot is (row, count)
PACKS = {
    90: (
        ([(VICTIM, 1), (GHOUL, 1), (VICTIM, 1), (GHOUL, 1), (VICTIM, 1)], [(OFFICER, 1), (FIEND, 1)], 3),
        ([(MURILO, 1), (MURILO, 2), (MURILO, 1), (VICTIM, 1), (MURILO, 2)], [(FIEND, 1), (MURILO, 2)], 4),
        ([(OFFICER, 1), (GHOUL, 1), (VICTIM, 1), (OFFICER, 1), (GHOUL, 1)], [(FIEND, 1), (OFFICER, 1)], 3),
    ),
    91: (
        ([(WOLF, 1), (WOLF, 2), (WOLF, 1), (GHOUL, 1), (WOLF, 1)], [(FIEND, 1), (WOLF, 2)], 4),
        ([(GHOUL, 1), (VICTIM, 1), (GHOUL, 1), (VICTIM, 1), (GHOUL, 1)], [(OFFICER, 1), (FIEND, 1)], 3),
        ([(OFFICER, 1), (GHOUL, 1), (WOLF, 1), (OFFICER, 1), (GHOUL, 1)], [(FIEND, 1), (OFFICER, 1)], 3),
    ),
}
PACK_SALT = 0x4B43415450414B01      # "KCATPAK", version 1


def P(root, rel):
    return os.path.join(root, rel.replace("\\", "/"))


def find_ci(root, rel):
    """The file for a data-relative path, matched case-insensitively per component."""
    cur = root
    for part in rel.replace("\\", "/").split("/"):
        hit = [f for f in os.listdir(cur) if f.lower() == part.lower()] if os.path.isdir(cur) else []
        cur = os.path.join(cur, hit[0] if hit else part)
    return cur


def b(v):
    return str(v).encode("latin-1")


def strid(row_no):
    return "LCATMOB%d" % row_no


def ai_row(row_no):
    return FIRST_AI_ROW + [m[0] for m in ROSTER].index(row_no)


# ---------------------------------------------------------------------- the rows
def monster_row(n, row_no, src, name, level, mark):
    row = list(n.d[src])
    steps = level - int(n.s(src, C_LEVEL))
    row[C_NAME] = name.encode("latin-1")
    row[C_LEVEL] = b(level)
    for c, g in GROWTH.items():
        row[c] = b(int(round(int(n.s(src, c)) * g ** steps)))
    row[C_AI] = b(ai_row(row_no))
    row[C_DROP] = b(DROP_TABLE)
    if mark is not None:
        row[C_TYPE] = b(mark)
    for c in (C_TARGET, C_DEADEVENT, *oro.NPC_SELL_TAB_COLS):
        row[c] = b""
    row[oro.NPC_STRID_COL] = strid(row_no).encode()
    row[oro.NPC_PVP_COL] = oro.DEFAULT_PVP_STATE
    return row


def npc_rows(ours, dry):
    n = oro.Stb(P(ours, NPC_STB_REL))
    stl = oro.Stl(P(ours, NPC_STL_REL))
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    dirty = {"stb": False, "stl": False, "chr": False}
    for row_no, src, name, level, _ai, mark in ROSTER:
        cur = n.get(row_no, C_NAME).decode("latin-1").strip()
        if cur and cur != name:
            raise SystemExit(f"LIST_NPC row {row_no} is {cur!r}: not free")
        if chr_.chars[row_no] is not None and not cur:
            raise SystemExit(f"LIST_NPC row {row_no} is blank but has a CHR entry: not free")
        want = monster_row(n, row_no, src, name, level, mark)
        if n.d[row_no] != want:
            n.d[row_no] = want
            dirty["stb"] = True
            print(f"    {'LIST_NPC.STB':26s} row {row_no} {name} (lv {level}, from {src})")
        if not stl.has(strid(row_no)):
            stl.append(strid(row_no), row_no, name)
            dirty["stl"] = True
            print(f"    {'LIST_NPC_S.STL':26s} +key {strid(row_no)}")
        if chr_.chars[row_no] != chr_.chars[src]:
            chr_.chars[row_no] = copy.deepcopy(chr_.chars[src])
            dirty["chr"] = True
            print(f"    {'LIST_NPC.CHR':26s} entry {row_no}")
    if dirty["stb"]:
        n.save(dry, keep_backup=False)
    if dirty["stl"]:
        stl.save(dry, keep_backup=False)
    if dirty["chr"]:
        chr_.save(dry, keep_backup=False)
    if not any(dirty.values()):
        print(f"    {'the monster rows':26s} already in place")


# ---------------------------------------------------------------------- the AI
# AIP records: u32 size, i32 type (low byte = the condition/action number + 1), payload.
COND_SPAWN_DIST, COND_COUNT_NEAR = 3, 27      # {int metres} / {int metres; byte allied; ...}
ACT_CALL_HELP = 11                            # {int metres; int count}
ACT_MONSTER = (9, 10, 18, 20, 36, 37)         # every action that names a monster row
PAT_IDLE = 1                                  # spawn, idle, gotoatk, beaten, killed, dead


def rec_no(rec):
    return (struct.unpack_from("<i", rec, 4)[0] & 0xFF) - 1


def capped(rec, limit):
    """The record with its first payload int (a distance in metres) capped."""
    v, = struct.unpack_from("<i", rec, 8)
    return rec[:8] + struct.pack("<i", min(v, limit)) + rec[12:]


def crypt_ai(blob):
    """An Alpha AI file as its crypt copy (the module docstring lists the edits)."""
    header, title, pats, tail = aip.parse(blob)
    if len(pats) != 6:
        raise SystemExit("an Alpha AI file no longer has six patterns")
    out = []
    for pi, (pname, evs) in enumerate(pats):
        kept = []
        for ename, conds, acts in evs:
            new_conds = []
            for c in conds:
                if rec_no(c) == COND_SPAWN_DIST:
                    c = capped(c, WANDER_M if pi == PAT_IDLE else LEASH_M)
                elif rec_no(c) == COND_COUNT_NEAR:
                    c = capped(c, ALLY_M if c[12] else AGGRO_M)
                new_conds.append(c)
            new_acts = [capped(a, HELP_M) if rec_no(a) == ACT_CALL_HELP else a
                        for a in acts if rec_no(a) not in ACT_MONSTER]
            if new_acts:                      # an event that only summoned is gone
                kept.append((ename, new_conds, new_acts))
        out.append((pname, kept))
    return aip.build(header, title, out, tail)


def ai_blobs(ours):
    """{AI file name: bytes} of the six crypt copies."""
    n = oro.Stb(P(ours, NPC_STB_REL))
    fai = oro.Stb(P(ours, AI_STB_REL))
    out = {}
    for _row_no, src, _name, _level, fname, _mark in ROSTER:
        src_ai = int(n.s(src, C_AI))
        if src_ai >= FIRST_AI_ROW:
            raise SystemExit(f"LIST_NPC {src} already runs a crypt AI row")
        out[fname] = crypt_ai(open(find_ci(ours, fai.s(src_ai, 0).strip()), "rb").read())
    return out


def ai_cell(fname):
    return rf"3DDATA\AI\{fname}".encode("latin-1")


def ai_files(ours, dry):
    fai = oro.Stb(P(ours, AI_STB_REL))
    blobs = ai_blobs(ours)
    grown = False
    for row_no, _src, _name, _level, fname, _mark in ROSTER:
        r = ai_row(row_no)
        if fai.rows <= r:
            fai.grow_to(r + 1)
            grown = True
        if fai.get(r, 0) != ai_cell(fname):
            if fai.get(r, 0).strip():
                raise SystemExit(f"FILE_AI row {r} is {fai.get(r, 0)!r}: not free")
            fai.set(r, 0, ai_cell(fname))
            grown = True
            print(f"    {'FILE_AI.STB':26s} row {r} = {fname}")
        path = os.path.join(P(ours, AI_DIR_REL), fname)
        changed = not (os.path.isfile(path) and open(path, "rb").read() == blobs[fname])
        if changed and not dry:
            with open(path, "wb") as fh:
                fh.write(blobs[fname])
        print(f"    {fname:26s} {'written' if changed else 'in place'}")
    if grown:
        fai.save(dry, keep_backup=False)


# ---------------------------------------------------------------------- spawn points
def touches(i, j, rect):
    return rect[0] - 1 <= i <= rect[0] + rect[2] and rect[1] - 1 <= j <= rect[1] + rect[3]


def spawn_cells():
    """The cells with a regen point: a checkerboard clear of the hall and the boss room."""
    return [(i, j) for j in range(maze.N) for i in range(maze.N)
            if (i + j) % 2 == 0 and not touches(i, j, maze.HALL) and not touches(i, j, maze.BOSS)]


def pack_of(zone, i, j):
    packs = PACKS[zone]
    return packs[maze.mix(PACK_SALT ^ (zone * 10000 + j * 100 + i)) % len(packs)]


def regen_by_chunk(zone, sw_cm, cell_cm):
    """IFO regen records per chunk slot (x, y), as mapgen.zone.build_zone takes them."""
    out = {}
    for i, j in spawn_cells():
        basic, tactic, cap = pack_of(zone, i, j)
        wx, wy = sw_cm + (i + 0.5) * cell_cm, sw_cm + (j + 0.5) * cell_cm
        rec = ifo.Regen(name=b"", pos=(wx - 520000.0, wy - 520000.0, 0.0), point_name=b"",
                        basic=[ifo.RegenMob(b"", m, c) for m, c in basic],
                        tactic=[ifo.RegenMob(b"", m, c) for m, c in tactic],
                        interval_s=REGEN_INTERVAL_S, limit=cap, range_m=REGEN_RANGE_M,
                        tactic_point=TACTIC_POINT)
        out.setdefault((int(wx // 16000), int(wy // 16000)), []).append(rec)
    return out


# ---------------------------------------------------------------------- verify, undo
def verify(ours, check):
    n = oro.Stb(P(ours, NPC_STB_REL))
    stl = oro.Stl(P(ours, NPC_STL_REL))
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    fai = oro.Stb(P(ours, AI_STB_REL))
    blobs = ai_blobs(ours)
    for row_no, src, name, level, fname, mark in ROSTER:
        check(n.d[row_no] == monster_row(n, row_no, src, name, level, mark), f"LIST_NPC {row_no} {name}")
        check(stl.has(strid(row_no)), f"LIST_NPC_S {strid(row_no)}")
        check(chr_.chars[row_no] is not None and chr_.chars[row_no] == chr_.chars[src], f"LIST_NPC.CHR {row_no}")
        r = ai_row(row_no)
        path = os.path.join(P(ours, AI_DIR_REL), fname)
        check(r < fai.rows and fai.get(r, 0) == ai_cell(fname) and os.path.isfile(path)
              and open(path, "rb").read() == blobs[fname], f"FILE_AI {r} {fname}")


def remove_files(ours):
    for _row, _src, _name, _level, fname, _mark in ROSTER:
        path = os.path.join(P(ours, AI_DIR_REL), fname)
        if os.path.isfile(path):
            os.remove(path)
            print(f"    removed {fname}")


# ---------------------------------------------------------------------- the plan, printed
def main():
    ours = os.path.join(os.path.dirname(HERE), "data")
    n = oro.Stb(P(ours, NPC_STB_REL))
    print("rows")
    for row_no, src, name, level, _fname, mark in ROSTER:
        row = monster_row(n, row_no, src, name, level, mark)

        def v(c):
            return int(row[c] or 0)
        print("  %d %-17s lv %d  HP %6d  ATK %4d  HIT %3d  DEF %3d  RES %3d  AVOID %3d  EXP %5d  mark %s  (from %d %s)" % (
            row_no, name, level, level * v(C_HP), v(C_ATK), v(C_HIT), v(C_DEF), v(C_RES), v(C_AVOID),
            v(C_EXP), row[C_TYPE].decode(), src, n.s(src, C_NAME)))
    print("AI files")
    for fname, blob in ai_blobs(ours).items():
        pats = aip.parse(blob)[2]
        print("  %-18s %d bytes, events per pattern %s" % (fname, len(blob), [len(e) for _p, e in pats]))
    names = {m[0]: m[2] for m in ROSTER}
    for zone in sorted(PACKS):
        cells = spawn_cells()
        print("crypt %d: %d points, cap %d monsters" % (zone, len(cells), sum(pack_of(zone, i, j)[2] for i, j in cells)))
        for j in reversed(range(maze.N)):
            line = ""
            for i in range(maze.N):
                if maze.in_rect(i, j, maze.HALL):
                    line += " H"
                elif maze.in_rect(i, j, maze.BOSS):
                    line += " B"
                elif (i, j) in cells:
                    line += " %d" % (PACKS[zone].index(pack_of(zone, i, j)) + 1)
                else:
                    line += " ."
            print("   " + line)
        for k, (basic, tactic, cap) in enumerate(PACKS[zone], 1):
            print("    pack %d (cap %d): %s | tactics %s" % (
                k, cap, ", ".join("%s x%d" % (names[m], c) for m, c in basic),
                ", ".join("%s x%d" % (names[m], c) for m, c in tactic)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
