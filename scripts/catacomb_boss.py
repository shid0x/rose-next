#!/usr/bin/env python3
"""The Karkia catacombs' first boss: the Crypt Necromancer (milestone 2).

Design: doc/karkia-catacombs-implementation-brief.md, section 6 ("Boss"). Applied by
`import-catacombs.py`; this file is the plan and its builders. **The constants here and
in src/sho_gameserver/src/catacombs.cpp must agree** (the row, the two skills and their
motions, the reach of each attack).

    python scripts/catacomb_boss.py                # the plan: row, skills, clips

Who runs the fight
------------------
**The controller, not the AI file.** CCatacombs owns the boss: it spawns it in the boss
room at each opening and 15 minutes after a kill, orders its two attacks on a clock
(so the pace does not depend on how many players are hitting it, as an "every N hits"
AI rule does), calls the adds at its HP thresholds and puts it back when the room has
been empty for a minute. The AI file only finds a target, answers whoever hits it and
runs home past its leash.

The body
--------
LIST_NPC 2556, a copy of the Necromancer (2414, tsuki's model, `import-necromancer.py`)
at level 240 with a skull, larger (size 250). Row 2414 stays the level-120 field
monster it was validated as. The CHR entry is a copy with three clips of its own:

  * slot 6 / 7, the spell pair: the cast clip as it is (ATT001, 38 frames, played at
    60%: 2.1 s) and **KCAT_BURST**, the release (ATT01) from its frame 5 on, one area
    frame where its first pulse was. The server lands a skill when the release starts
    and multiplies it by the release's attack frames: ATT01 carries three.
  * slot 8 / 9, the staff pair: its own swing (ATT2, hit on frame 20) split where the
    staff is up. **KCAT_LANCE_WINDUP** is frames 0-16, played at 27% (2.1 s) while the
    lines are on the ground; **KCAT_LANCE** the rest.
Soul Siphon (the field Necromancer's drain) is left out: a three-hit strike nobody can
step out of is not what this fight is about, and its heal is nothing on a boss's HP.

The two attacks (making-monsters.md, section 6)
-----------------------------------------------
  * **Grave Burst** (LIST_SKILL 7205, from Seal Burst): a circle round it, 10 m. Run out.
  * **Soul Lances** (7206, from Charge): a fan of five lines, 22 m long and 2 m wide,
    24 degrees apart, aimed at one player. Step between two lines, or behind it.
Both are drawn on the ground for the whole wind-up (col 83), about 2.1 s. Neither
carries a status: in the second phase they come one after the other, and a slow from
the first would decide the second.

Power is sized on level-240 average-gear builds in the combat simulator (`SIZING`):
a hit is meant to take 30-45% of a typical player's HP.

Stats
-----
Level 240. HP, attack and the rest are frozen in `STATS` with the measurement beside
them. EXP is seeded only (rebalance-exp-rewards.py owns the column). Drop table 913
(`add-karkia-drops.py`): the red weapons, a placeholder until the loot is tailored.
"""
import copy
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import rosedata as oro  # noqa: E402
from rosefmt import zmo as rfzmo  # noqa: E402
import catacomb_monsters as mon  # noqa: E402

NPC_STB_REL, NPC_STL_REL, NPC_CHR_REL = mon.NPC_STB_REL, mon.NPC_STL_REL, mon.NPC_CHR_REL
AI_STB_REL, AI_DIR_REL = mon.AI_STB_REL, mon.AI_DIR_REL
SKILL_STB_REL = r"3DDATA\STB\LIST_SKILL.STB"
SKILL_STL_REL = r"3DDATA\STB\LIST_SKILL_S.STL"

C_NAME, C_MODEL, C_WALK, C_RUN, C_SIZE, C_RWEAPON, C_LWEAPON, C_LEVEL, C_HP, C_ATK, C_HIT, \
    C_DEF, C_RES, C_AVOID, C_ASPD, C_MAGIC, C_AI, C_EXP, C_DROP, C_MONEY, C_DROPRATE = range(21)
C_TARGET, C_RANGE, C_TYPE, C_DEADEVENT, C_HEIGHT = 25, 26, 27, 41, 42

# ---- the row
BOSS = 2556
BOSS_SRC = 2414                     # Necromancer (import-necromancer.py)
BOSS_NAME = "Crypt Necromancer"
BOSS_STRID = "LCATMOB2556"
BOSS_AI_ROW = mon.FIRST_AI_ROW + len(mon.ROSTER)    # 2275
BOSS_AI = "KCAT_NECROMANCER.AIP"
DROP_TABLE = 913
MARK_SKULL = 16
STATS = {
    C_WALK: 200, C_RUN: 520,        # a little slower than the field one (600): the lances can be outrun
    C_SIZE: 250, C_HEIGHT: 430,     # 190 / 325 on the field one
    C_LEVEL: 240,
    C_HP: 3000,                     # x level = 720,000 (SIZING)
    C_ATK: 2500, C_HIT: 1250, C_DEF: 950, C_RES: 1000, C_AVOID: 350, C_ASPD: 100,
    C_EXP: 60000, C_DROP: DROP_TABLE, C_MONEY: 0, C_DROPRATE: 80,
    C_TYPE: MARK_SKULL,
}

# ---- the AI
SEEK, LEASH = 20, 45                # m: the room is 60 m across, its corner 42 m from the middle
TURN_PCT = 15                       # when hit by someone else: chance to turn on them

# ---- the skills
SK_NAME, SK_LINE, SK_TYPE, SK_RANGE, SK_SCOPE, SK_POWER = 0, 1, 5, 6, 8, 9
SK_STATUS, SK_SUCCESS, SK_DURATION, SK_DMGTYPE = 11, 13, 14, 15
SK_CAST_MOTION, SK_CAST_SPEED, SK_ACT_MOTION, SK_ACT_SPEED = 52, 53, 68, 69
SK_TELEGRAPH, SK_FAN, SK_STL_KEY = 83, 84, 86
FX_CAST, FX_SHOCKWAVE, FX_DARK_BURST, FX_BLOOD_OUT = 1381, 400, 1302, 1602   # all one-shot
SND_CAST, SND_SKILL_ATTACK, SND_HIT = 1001, 131, 142

BURST = 7205
BURST_NAME = "Grave Burst"
BURST_DESC = "Gathers the dead air of the crypt and bursts it all around. Get away from it."
BURST_SRC = 7023                    # Seal Burst: type 17, a circle round the caster
BURST_MOTION = 6
BURST_RADIUS = 1000                 # cm from its centre
# How near the target must be for the cast to start with no walk first. Blank, the
# server uses the boss's swing reach (5.5 m); the controller orders the burst on the
# nearest player within 7.5 m (catacombs.cpp BURST_REACH).
BURST_RANGE = 800
BURST_POWER = 400
BURST_CELLS = {
    SK_TYPE: 17, SK_RANGE: BURST_RANGE, SK_SCOPE: BURST_RADIUS, SK_POWER: BURST_POWER,
    SK_STATUS: "", SK_SUCCESS: "", SK_DURATION: "", 21: "", 23: "",   # Seal Burst's slow
    SK_CAST_MOTION: BURST_MOTION, SK_CAST_SPEED: 60,        # 38 frames: 2.1 s to get out
    SK_ACT_MOTION: BURST_MOTION + 1, SK_ACT_SPEED: 100,
    56: FX_CAST, 57: 999, 58: SND_CAST,                     # casting effect 0: event 44
    62: FX_SHOCKWAVE, 63: 999, 64: SND_SKILL_ATTACK,        # casting effect 2: event 74
    74: FX_DARK_BURST, 75: 999, 76: SND_HIT,
    SK_TELEGRAPH: 1,
}

LANCES = 7206
LANCES_NAME = "Soul Lances"
LANCES_DESC = "Sweeps its staff and sends five lances of dark force along the ground. Stand between two, or behind it."
LANCES_SRC = 3603                   # Charge: type 7, a strip ahead
LANCES_MOTION = 8
LANCES_LENGTH, LANCES_WIDTH = 2200, 200     # cm, each line
LANCES_FAN = 5 * 1000 + 24          # five lines, 24 degrees apart
LANCES_RANGE = 2000                 # cm: cast on a target this near, no chase first
LANCES_POWER = 450
LANCES_CELLS = {
    SK_TYPE: 7, SK_RANGE: LANCES_RANGE, SK_SCOPE: LANCES_LENGTH * 10000 + LANCES_WIDTH,
    SK_POWER: LANCES_POWER,
    SK_CAST_MOTION: LANCES_MOTION, SK_CAST_SPEED: 27,       # the 17-frame raise: 2.1 s
    SK_ACT_MOTION: LANCES_MOTION + 1, SK_ACT_SPEED: 100,
    56: FX_CAST, 57: 999, 58: SND_CAST,
    62: FX_DARK_BURST, 63: 999, 64: SND_SKILL_ATTACK,
    74: FX_BLOOD_OUT, 75: 999, 76: SND_HIT,
    SK_TELEGRAPH: 1, SK_FAN: LANCES_FAN,
}
SKILLS = (  # row, source row, name, cells, description
    (BURST, BURST_SRC, BURST_NAME, BURST_CELLS, BURST_DESC),
    (LANCES, LANCES_SRC, LANCES_NAME, LANCES_CELLS, LANCES_DESC),
)

# ---- the clips (the boss's own copies; making-monsters.md sections 3 and 6)
CLIP_DIR = r"3Ddata\MOTION\NPC\NECROMANCER"
CLIP_BURST = CLIP_DIR + r"\KCAT_BURST.ZMO"
CLIP_LANCE_WINDUP = CLIP_DIR + r"\KCAT_LANCE_WINDUP.ZMO"
CLIP_LANCE = CLIP_DIR + r"\KCAT_LANCE.ZMO"
BURST_FROM = 5                      # ATT01's first pulse lands on its frame 9
LANCE_SPLIT = 17                    # ATT2's hit is on its frame 20
CLIP_SLOTS = {BURST_MOTION + 1: CLIP_BURST, LANCES_MOTION: CLIP_LANCE_WINDUP,
              LANCES_MOTION + 1: CLIP_LANCE}
# what the source clips must still be: slot -> (frames, events)
SOURCE_CLIPS = {
    2: (41, {17: 31, 20: 21}),
    7: (38, {7: 35, 9: 10, 12: 35, 14: 20, 18: 35, 20: 25, 21: 64}),
}

# The measurement behind STATS and the two powers: combat simulator, level-240 builds in
# average gear, typical play, 2026-11-10 (combatsim.formulas.normal_attack / skill_damage
# on Simulation.make_monster() -> make_player(), 3,000 rolls each; DPS from 6 fights).
SIZING = """
job          HP   swing        Grave Burst   Soul Lances   DPS on the boss
Knight     8978   264 ( 3%)   1271 (14%)    1427 (16%)    3552
Champion   8593   437 ( 5%)   1444 (17%)    1621 (19%)    4166
Mage       3715   407 (11%)   1123 (30%)    1261 (34%)    1289
Cleric     5460   164 ( 3%)    805 (15%)     903 (17%)     696
Raider     4823   442 ( 9%)   1327 (28%)    1491 (31%)    2058
Scout      3966   446 (11%)   1330 (34%)    1493 (38%)    1780
Bourgeois  4291   478 (11%)   1380 (32%)    1549 (36%)    2139
Artisan    3911   460 (12%)   1355 (35%)    1522 (39%)    1769
Median DPS 1919: 720,000 HP is about 6 minutes for one such player standing still (more
with the dodging), 2 minutes for three; a Champion alone about 3. Both skills always
land at level 240 (the level gate), so a hit taken is a hit not dodged.
"""


def P(root, rel):
    return os.path.join(root, rel.replace("\\", "/"))


def b(v):
    return str(v).encode("latin-1")


def load(name, fname):
    """Import a sibling script as a module without running its main()."""
    import importlib.util
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, fname))
    mod = importlib.util.module_from_spec(spec)
    argv, sys.argv = sys.argv, [fname, "--help"]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = argv
    return mod


_heb = None


def heb():
    """import-hebarn.py's AI record encoders, loaded on first use."""
    global _heb
    if _heb is None:
        _heb = load("import_hebarn", "import-hebarn.py")
    return _heb


# ---------------------------------------------------------------------- clips
def cleared(blob, keep):
    """The clip with every event zeroed, then `keep` ({frame: event}) written."""
    _, ev = rfzmo.events(blob)
    changes = {i: 0 for i, e in enumerate(ev) if e}
    changes.update(keep)
    return rfzmo.with_events(blob, changes)


def build_clips(ours, chr_):
    """{data-relative path: clip bytes}, cut from the Necromancer's own slots (refusing a
    clip that is no longer the one this was cut for)."""
    def read(slot):
        frames, events = SOURCE_CLIPS[slot]
        rel = chr_.motions[dict(chr_.chars[BOSS_SRC]["anims"])[slot]].decode("latin-1")
        blob = open(mon.find_ci(ours, rel), "rb").read()
        _, ev = rfzmo.events(blob)
        got = {i: e for i, e in enumerate(ev or []) if e}
        if rfzmo.read_header(blob)[1] != frames or got != events:
            raise SystemExit(f"{rel} is not the {frames}-frame clip with events {events} this "
                             f"was cut for (has {got}) -- refusing")
        return blob

    release, swing = read(7), read(2)
    hit = 20 - LANCE_SPLIT
    return {
        # 35 the strike's sound, 24 the area's payload, 74 the shockwave on the ground
        CLIP_BURST: cleared(rfzmo.sliced(release, BURST_FROM, 38),
                            {7 - BURST_FROM: 35, 9 - BURST_FROM: 24, 10 - BURST_FROM: 74}),
        CLIP_LANCE_WINDUP: cleared(rfzmo.sliced(swing, 0, LANCE_SPLIT), {0: 44}),
        CLIP_LANCE: cleared(rfzmo.sliced(swing, LANCE_SPLIT, 41), {hit - 2: 35, hit: 24, hit + 1: 74}),
    }


def clips(ours, dry):
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    for rel, blob in build_clips(ours, chr_).items():
        path = mon.find_ci(ours, rel)
        changed = not (os.path.isfile(path) and open(path, "rb").read() == blob)
        if changed and not dry:
            with open(path, "wb") as fh:
                fh.write(blob)
        print(f"    {os.path.basename(rel):26s} {'written' if changed else 'in place'}")


def motion_index(chr_, rel):
    """The CHR motion list's index of a clip, appended when it is not there."""
    key = rel.encode("latin-1")
    hit = [i for i, m in enumerate(chr_.motions) if m.lower() == key.lower()]
    if hit:
        return hit[0]
    chr_.motions.append(key)
    return len(chr_.motions) - 1


def chr_entry(chr_):
    e = copy.deepcopy(chr_.chars[BOSS_SRC])
    slots = {s for s, _ in e["anims"]}
    if not set(CLIP_SLOTS) <= slots:
        raise SystemExit(f"LIST_NPC.CHR {BOSS_SRC} has no slots {sorted(set(CLIP_SLOTS) - slots)}")
    e["anims"] = [(s, motion_index(chr_, CLIP_SLOTS[s]) if s in CLIP_SLOTS else i) for s, i in e["anims"]]
    return e


# ---------------------------------------------------------------------- the row
def boss_row(n):
    row = list(n.d[BOSS_SRC])
    row[C_NAME] = BOSS_NAME.encode("latin-1")
    for c, v in STATS.items():
        row[c] = b(v)
    row[C_AI] = b(BOSS_AI_ROW)
    for c in (C_TARGET, C_DEADEVENT, *oro.NPC_SELL_TAB_COLS):
        row[c] = b""
    row[oro.NPC_STRID_COL] = BOSS_STRID.encode()
    row[oro.NPC_PVP_COL] = oro.DEFAULT_PVP_STATE
    return row


def npc_row(ours, dry):
    n = oro.Stb(P(ours, NPC_STB_REL))
    stl = oro.Stl(P(ours, NPC_STL_REL))
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    cur = n.get(BOSS, C_NAME).decode("latin-1").strip()
    if cur and cur != BOSS_NAME:
        raise SystemExit(f"LIST_NPC row {BOSS} is {cur!r}: not free")
    if chr_.chars[BOSS] is not None and not cur:
        raise SystemExit(f"LIST_NPC row {BOSS} is blank but has a CHR entry: not free")
    quiet = True
    if n.d[BOSS] != boss_row(n):
        n.d[BOSS] = boss_row(n)
        n.save(dry, keep_backup=False)
        quiet = False
        print(f"    {'LIST_NPC.STB':26s} row {BOSS} {BOSS_NAME}")
    if not stl.has(BOSS_STRID):
        stl.append(BOSS_STRID, BOSS, BOSS_NAME)
        stl.save(dry, keep_backup=False)
        quiet = False
        print(f"    {'LIST_NPC_S.STL':26s} +key {BOSS_STRID}")
    want = chr_entry(chr_)
    if chr_.chars[BOSS] != want:
        chr_.chars[BOSS] = want
        chr_.save(dry, keep_backup=False)
        quiet = False
        print(f"    {'LIST_NPC.CHR':26s} entry {BOSS}")
    if quiet:
        print(f"    {'the boss row':26s} already in place")


# ---------------------------------------------------------------------- the skills
def skill_strid(row):
    return "LMobSkill%d" % row      # name-monster-skills-players.py's convention


def skill_row(sk, row, src, name, cells):
    out = list(sk.d[src])
    out[SK_NAME] = name.encode("latin-1")
    out[SK_LINE] = b(row)
    for c, v in cells.items():
        out[c] = b(v)
    out[SK_STL_KEY] = skill_strid(row).encode()
    return out


def skills(ours, dry):
    sk = oro.Stb(P(ours, SKILL_STB_REL))
    dirty = False
    for row, src, name, cells, _desc in SKILLS:
        if sk.rows <= row:
            if sk.rows != row:
                raise SystemExit(f"LIST_SKILL has {sk.rows} rows: {name} ({row}) would not be the next")
            sk.grow_to(row + 1)
        cur = sk.get(row, SK_NAME).decode("latin-1").strip()
        if cur and cur != name:
            raise SystemExit(f"LIST_SKILL row {row} is {cur!r}: not free")
        want = skill_row(sk, row, src, name, cells)
        if sk.d[row] != want:
            sk.d[row] = want
            dirty = True
            print(f"    {'LIST_SKILL.STB':26s} row {row} {name} (from {src})")
    if dirty:
        sk.save(dry, keep_backup=False)
    skl = oro.Stl(P(ours, SKILL_STL_REL))
    added = 0
    for row, _src, name, _cells, desc in SKILLS:
        if not skl.has(skill_strid(row)):
            skl.append(skill_strid(row), row, name, desc)
            added += 1
    if added:
        skl.save(dry, keep_backup=False)
        print(f"    {'LIST_SKILL_S.STL':26s} +{added} keys")
    if not dirty and not added:
        print(f"    {'the boss skills':26s} already in place")


# ---------------------------------------------------------------------- the AI
def c_no_target_attacker():
    """AICOND11 as every Karkia AI opens its "damaged" pattern: no target yet."""
    h = heb()
    return h._cond(11, struct.pack("<B", 0) + h.PAD3 + struct.pack("<iB", 0, 0) + h.PAD3)


def build_ai():
    from rosefmt import aip
    h = heb()
    pats = [
        (h.pat("created"), []),                  # a spawn from code never runs it
        (h.pat("stop"), [h.ev("seek", [h.c_enemies_near(SEEK)], [h.a_attack_found()])]),
        (h.pat("attack move"), [h.ev("leash", [h.c_moved_from_spawn(LEASH)], [h.a_run_home(3)])]),
        (h.pat("damaged"), [
            h.ev("answer", [c_no_target_attacker()], [h.a_attack_attacker()]),
            h.ev("turn", [h.c_chance(TURN_PCT), h.c_attacker_not_target()], [h.a_attack_attacker()]),
        ]),
        (h.pat("kill"), []),
        (h.pat("dead"), []),
    ]
    # idle check every second, the damaged pattern on every hit taken
    return aip.build((len(pats), 1, 100), b"Crypt Necromancer\0", pats, b"")


def ai_file(ours, dry):
    fai = oro.Stb(P(ours, AI_STB_REL))
    cell = mon.ai_cell(BOSS_AI)
    if fai.rows <= BOSS_AI_ROW:
        fai.grow_to(BOSS_AI_ROW + 1)
    if fai.get(BOSS_AI_ROW, 0) != cell:
        if fai.get(BOSS_AI_ROW, 0).strip():
            raise SystemExit(f"FILE_AI row {BOSS_AI_ROW} is {fai.get(BOSS_AI_ROW, 0)!r}: not free")
        fai.set(BOSS_AI_ROW, 0, cell)
        fai.save(dry, keep_backup=False)
        print(f"    {'FILE_AI.STB':26s} row {BOSS_AI_ROW} = {BOSS_AI}")
    path = os.path.join(P(ours, AI_DIR_REL), BOSS_AI)
    blob = build_ai()
    changed = not (os.path.isfile(path) and open(path, "rb").read() == blob)
    if changed and not dry:
        with open(path, "wb") as fh:
            fh.write(blob)
    print(f"    {BOSS_AI:26s} {'written' if changed else 'in place'}")


# ---------------------------------------------------------------------- apply, verify, undo
def apply(ours, dry):
    clips(ours, dry)
    skills(ours, dry)
    ai_file(ours, dry)
    npc_row(ours, dry)


def verify(ours, check):
    n = oro.Stb(P(ours, NPC_STB_REL))
    stl = oro.Stl(P(ours, NPC_STL_REL))
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    fai = oro.Stb(P(ours, AI_STB_REL))
    sk = oro.Stb(P(ours, SKILL_STB_REL))
    skl = oro.Stl(P(ours, SKILL_STL_REL))
    check(n.d[BOSS] == boss_row(n), f"LIST_NPC {BOSS} {BOSS_NAME}")
    check(stl.has(BOSS_STRID), f"LIST_NPC_S {BOSS_STRID}")
    before = len(chr_.motions)
    check(chr_.chars[BOSS] == chr_entry(chr_) and len(chr_.motions) == before, f"LIST_NPC.CHR {BOSS} and its clips")
    for rel, blob in build_clips(ours, chr_).items():
        path = mon.find_ci(ours, rel)
        check(os.path.isfile(path) and open(path, "rb").read() == blob, os.path.basename(rel))
    for row, src, name, cells, _desc in SKILLS:
        check(row < sk.rows and sk.d[row] == skill_row(sk, row, src, name, cells), f"LIST_SKILL {row} {name}")
        check(skl.has(skill_strid(row)), f"LIST_SKILL_S {skill_strid(row)}")
    path = os.path.join(P(ours, AI_DIR_REL), BOSS_AI)
    check(BOSS_AI_ROW < fai.rows and fai.get(BOSS_AI_ROW, 0) == mon.ai_cell(BOSS_AI)
          and os.path.isfile(path) and open(path, "rb").read() == build_ai(), f"FILE_AI {BOSS_AI_ROW} {BOSS_AI}")


def remove_files(ours):
    for rel in (CLIP_BURST, CLIP_LANCE_WINDUP, CLIP_LANCE, AI_DIR_REL + "\\" + BOSS_AI):
        path = mon.find_ci(ours, rel)
        if os.path.isfile(path):
            os.remove(path)
            print(f"    removed {os.path.basename(rel)}")


def main():
    ours = os.path.join(os.path.dirname(HERE), "data")
    n = oro.Stb(P(ours, NPC_STB_REL))
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    row = boss_row(n)
    print("%d %s: lv %s, HP %d, ATK %s, HIT %s, DEF %s, RES %s, AVOID %s, EXP %s, drop table %s" % (
        BOSS, BOSS_NAME, row[C_LEVEL].decode(), int(row[C_LEVEL]) * int(row[C_HP]), row[C_ATK].decode(),
        row[C_HIT].decode(), row[C_DEF].decode(), row[C_RES].decode(), row[C_AVOID].decode(),
        row[C_EXP].decode(), row[C_DROP].decode()))
    for rel, blob in build_clips(ours, chr_).items():
        _, ev = rfzmo.events(blob)
        fps, frames, _ = rfzmo.read_header(blob)
        print("  %-24s %d frames at %d fps, events %s" % (
            os.path.basename(rel), frames, fps, {i: e for i, e in enumerate(ev) if e}))
    for row_no, src, name, cells, _desc in SKILLS:
        print("  %d %-12s type %s, scope %s, power %s, cast motion %s at %s%%" % (
            row_no, name, cells[SK_TYPE], cells[SK_SCOPE], cells[SK_POWER],
            cells[SK_CAST_MOTION], cells[SK_CAST_SPEED]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
