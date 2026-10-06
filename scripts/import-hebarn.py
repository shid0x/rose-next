#!/usr/bin/env python3
"""Import Rose Brasil's [God] Hebarn as a challenge boss at the Ancient Oasis Shrine (zone 80).

What Brasil has, and what it does not
-------------------------------------
Rose Brasil (Testclients\\rose brazil\\Extracted) places [God] Hebarn (its LIST_NPC
4350) mute on the arena of its zone 111 "Dangerous Spawn": "of course he won't speak
to one of your slaves". The model is `BOSS_weilian` -- a horseman, two meshes (the
rider BODY and the horse MA, Chinese 马) on one 67-bone skeleton that is a 3ds Max
Biped used as a quadruped -- with ten clips: stand, move, run, att1 (lance thrust),
att2 (rear and stomp), att3 (charge), stay/stay2 (the same rearing taunt), dam,
die. No dump we own has this model besides Brasil (not Wish, tsuki, Jrose,
RoseZA...), and **nobody gave him AI or skills**: his kit here is ours.

How the fight starts (the "challenge accepted" flow)
----------------------------------------------------
Two LIST_NPC rows share the model: TALK, a type-999 NPC who stands at the shrine
and talks, and BOSS, the monster. One QSD trigger is the whole challenge, laid out
like retail's hidden-quest NPCs that turn into monsters (QH-101.QSD 209_01: COND_013
select the NPC, REWD_034 hide it, REWD_008 spawn the monster at it):

  `Hebarn-Challenge`  COND_003 level >= 220, COND_004 >= 10 Devourer Horn in the
                      bag, COND_013 select TALK; REWD_001 takes the horns, REWD_034
                      hides TALK, REWD_008 spawns BOSS at TALK's feet (team 100).
  `Hebarn-Return`     COND_013 select TALK; REWD_034 shows it again.

While TALK is hidden nobody can start a second fight. BOSS's AI fires
`Hebarn-Return` (AICOND17 select TALK + AIACT30) when it dies, and also when it
has existed 15 minutes and stands idle (nobody fighting it): it shouts, fires the
trigger and kills itself (AIACT23). A server restart rebuilds every NPC visible
(CObjNPC m_bShow = true), so TALK can never stay hidden for good. The death route
is the AI's dead pattern, not LIST_NPC col 41: that column only asks the killer's
*client* to check the event (Send_gsv_CHECK_NPC_EVENT), which fails if the killer
logs out. A hidden NPC still runs its trigger timer (CObjNPC::Proc has no m_bShow
gate). The suicide is Add_DAMAGE on itself, which may run the dead pattern, so the
leave event sets monster variable 1 first and the drop event requires it at 0 --
a god who walks away pays nothing.

The dialog is `quest-editor con-toll` (offer if you qualify, "come back when..." if
not), gated client-side on QF_checkQuestCondition of the same trigger; COND_013
answers true on the client.

The kit
-------
  normal attack   att1, the lance thrust (slot 2, melee event 21 at frame 25).
  7019 Hellfire Stomp   cast on slot 6 (stay, rearing), released on slot 7 (att2,
                  the stomp): Stun Blast's row (3611), range 8 m, 12 m radius,
                  power 230, 4 s stun, cast 150% / release 120% speed. From the "damaged" pattern on a hit
                  counter with the target within 7 m, and once at 30% HP (the
                  enrage, with a shout) -- see STOMP_EVERY.
  7020 Infernal Charge  cast on slot 8 (stay2), released on slot 9 (att3, the
                  charge): Charge's row (3603), power 330, range 12 m, at 25% of
                  "attack move" checks (while he chases) against a target 5-11 m
                  away: he lunges from where he is rather than running you down.
**Every cast is gated on the target already being in range** (STOMP_REACH,
CHARGE_REACH): a row with range 0 falls back to the monster's attack range
(CObjMOB::Get_AttackRange, here ~7.8 m), and a cast ordered from further away makes
the server chase first. The client abandons a remote cast that has not started
within 5 s, so a kited chase-to-cast lands its damage and status with no animation
(first test, 2026-10-06: Stomp ordered at 13:44:48, started at 13:45:07, its stun
and 1098 damage popped from nowhere).
Brasil's att2/att3 carry the *melee* hit event (21) at frames 29/22; a skill only
presents at 24/34 (projectile) or 25/26 (immediate hit) -- client ActionEVENT and
CMotion::m_bHasProjectileFireFrame -- so our copies of those two clips carry 25
there. att1 keeps 21: it is the normal attack. Effects are borrowed: the rows'
own (Stun Blast 1611, Charge 1881 + impact 1035); Brasil ships none.
Damage reference: Fireball at power 230 landed ~1440 on a 6102-HP tester from a
2900-ATK boss (CLAUDE.md, import-monster-skills).

Stats ("the ultimate boss of the game", 2026-10-06)
---------------------------------------------------
Level 240, HP 1500 per level (~360k, twice Deadly Drake, until now the strongest
spawned boss), ATK 3300, HIT 1450, AVOID 450, size 150% (Brasil used 180%: ~16 m
tall). DEF 1001 / RES 898 are the level-240 boss values of
rebalance-endgame-curve.py (trend x BOSS_MULTIPLIER, what the Sanctuary kings and
Deadly Drake Alpha carry): that pass scopes *every* row at level 200+, and its HP
fallback counts Hebarn as a boss, so a --restore and re-run leaves him unchanged. A
DEF near the player's ATK is the damage floor the pass exists to prevent; his edge
is HP, ATK, HIT and the kit. Sounds and material from Deadly Drake (2729),
bare-hand hit effect 403 (the retail default). EXP is only seeded: BOSS is in no
REGEN lump, so rebalance-exp-rewards.py may not price it -- check its --verify.
Level 240 is outside the 60-199 window every balance pass fits, so no trend moves.

Loot
----
One ITEM_DROP row (appended), col 20 = 100 so every roll uses it: Karkia's thirteen
mythical weapons (Deadly Drake's pools), the eight lv240 gauntlets/boots, a redirect
to the four lv240 bodies and one to the four caps, the two top shields and five
materials. Low slots win when the drop variable is small (Get_DropITEM), so the
weapons sit first. On top, the dead pattern drops (AIACT17, to the killer) one lv240
body at 80% and one cap at 80%: deliberately generous for a fight that costs ten
Devourer Horns -- OFFERING / DEATH_DROPS are the knobs.

Placement
---------
The flattest ground in the zone (under 0.2 m across 10 m): the sand terrace by the
north arm of the oasis pond, (5030, 5232), 21 m from [Lojala Smith] Ekblovo. Picked
from the heightmap and checked with map-editor shots. Not final -- the user may
move him.

Usage
-----
    python scripts/import-hebarn.py --dry-run
    python scripts/import-hebarn.py
    python scripts/import-hebarn.py --verify
Then restart the servers (LIST_NPC, LIST_SKILL, ITEM_DROP, FILE_AI, AI_s.STB, the
QSD and the IFO are server data) and re-bake the VFS (model, clips, CHR, ZSC,
LIST_NPC, LIST_SKILL, the .CON and its LTB rows, the IFO are client data).
Backups of every table before the first write go to build/hebarn/bak; new files
(model, clips, .aip, .CON) are simply deleted to undo.
"""
import argparse
import copy
import importlib.util
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA_ROOT = os.path.join(ROOT, "data")
DEFAULT_SRC = r"C:\Users\Thomas\Desktop\Testclients\rose brazil\Extracted"
BUILD_DIR = os.path.join(ROOT, "build", "hebarn")
sys.path.insert(0, HERE)

SRC_NPC = 4350                      # Brasil's [God] Hebarn
TALK, BOSS = 4147, 4148             # ours: appended to LIST_NPC
TALK_NAME, BOSS_NAME = "[God] Hebarn", "Hebarn"
TALK_STRID, BOSS_STRID = "LNPC4147", "LNPC4148"
TALK_TEMPLATE = 2114                # [Lojala Smith] Ekblovo: row shape + IFO record
BOSS_TEMPLATE = 2729                # Deadly Drake: sounds, material, HP gauge

NPC_STB_REL = r"3DDATA\STB\LIST_NPC.STB"
NPC_STL_REL = r"3DDATA\STB\LIST_NPC_S.STL"
NPC_CHR_REL = r"3DDATA\NPC\LIST_NPC.CHR"
ZSC_REL = r"3DDATA\NPC\PART_NPC.ZSC"
SKILL_STB_REL = r"3DDATA\STB\LIST_SKILL.STB"
DROP_STB_REL = r"3DDATA\STB\ITEM_DROP.STB"
AI_STB_REL = r"3DDATA\STB\FILE_AI.STB"
AI_STR_REL = r"3DDATA\AI\AI_s.STB"
EVENT_STB_REL = r"3DDATA\STB\LIST_EVENT.STB"
QSD_REL = r"3DDATA\QUESTDATA\QP401.QSD"
MAPS_REL = r"3Ddata\MAPS\ORO\ODOS01"
MOTION_REL = r"3Ddata\MOTION\NPC\BOSS_weilian"

# LIST_NPC columns (game numbering)
C_NAME, C_MON, C_WALK, C_RUN, C_SIZE, C_RHAND, C_LHAND = 0, 1, 2, 3, 4, 5, 6
C_LV, C_HP, C_ATK, C_HIT, C_DEF, C_RES, C_AVOID, C_ASPD = 7, 8, 9, 10, 11, 12, 13, 14
C_KIND, C_AI, C_EXP, C_DROP, C_MONEY, C_RATE = 15, 16, 17, 18, 19, 20
C_SELL = (21, 22, 23, 24)
C_REACH, C_TYPE, C_FACE, C_HANDHIT, C_EDITOR = 26, 27, 29, 33, 36
C_EFFECTS, C_STRID, C_DEADEVENT, C_PVP = 39, 40, 41, 43

BOSS_STATS = {C_MON: "BOSS_weilian.mon", C_WALK: 200, C_RUN: 700, C_SIZE: 150,
              C_LV: 240, C_HP: 1500, C_ATK: 3300, C_HIT: 1450, C_DEF: 1001, C_RES: 898,
              C_AVOID: 450, C_ASPD: 100, C_KIND: 1, C_EXP: 250000, C_MONEY: 0, C_RATE: 100,
              C_REACH: 600, C_TYPE: 11, C_HANDHIT: 403}
BOSS_BLANK = (C_RHAND, C_LHAND, *C_SELL, C_FACE, C_EDITOR, C_EFFECTS, C_DEADEVENT)
TALK_STATS = {C_MON: "BOSS_weilian.mon", C_SIZE: 150}
TALK_BLANK = (C_FACE, C_EDITOR)

# skills: (row, name, template, {col: value})
SK_RANGE, SK_RADIUS, SK_POWER, SK_STATUS, SK_DURATION = 6, 8, 9, 11, 14
SK_CAST_MOTION, SK_SKILL_MOTION = 52, 68
SK_CAST_SPEED, SK_ACTION_SPEED = 53, 69   # % -- both sides play the AI's cast/release clips at these
STOMP, CHARGE = 7019, 7020
# The Stomp's stun is presented at the release clip's hit frame, while the server
# applied it when the cast resolved: at 100%/100% that is the 1.83 s rearing clip plus
# 0.97 s into the stomp, ~2.8 s, and a 3 s stun was all but over before the player saw
# it (2026-10-06 test: stun shown 2-3 s after its damage packet). 150%/120% brings the
# delay to ~2 s and 4 s is the Inguz precedent (CLAUDE.md: a cast's status is presented
# 1-3 s after the server applies it), so ~2 s of the visible stun is real.
SKILLS = [
    (STOMP, "Hellfire Stomp", 3611, {SK_RANGE: 800, SK_RADIUS: 1200, SK_POWER: 230, SK_STATUS: 32,
                                     SK_DURATION: 4, SK_CAST_MOTION: 6, SK_SKILL_MOTION: 7,
                                     SK_CAST_SPEED: 150, SK_ACTION_SPEED: 120}),
    (CHARGE, "Infernal Charge", 3603, {SK_RANGE: 1200, SK_POWER: 330,
                                       SK_CAST_MOTION: 8, SK_SKILL_MOTION: 9}),
]
SKILL_ROWS = 7021
# The AI casts only when its target is already inside the skill's range (metres,
# below the row's col 6), so the server never has to chase to cast. A chase-to-cast
# is not safe: the client gives a remote cast 5 s to start and then abandons it, and
# a target who keeps running makes the chase longer -- the 2026-10-06 test had a
# stomp start 19 s after the order, its damage and stun popped with no animation.
STOMP_REACH = 7
CHARGE_REACH = (5, 11)
# Which pattern runs when matters as much as the conditions (CObjAI::Proc /
# CAI_FILE): "attack move" runs ONLY while he is moving towards a target out of
# reach -- never while he stands and swings -- and "damaged" runs on
# HEADER_DAMAGED_RATE % of the hits he takes (the header's iSecondOfAttackMove field,
# which CAI_FILE stores as m_iRateDamagedAI despite its name). So the Charge, a
# punish for running, lives in "attack move"; the Stomp, a melee skill, lives in
# "damaged" on a hit counter (monster variable 2): every STOMP_EVERY evaluated hits,
# ~30 s solo and ~8 s for four players in melee.
HEADER_DAMAGED_RATE = 50
STOMP_EVERY = 16

# Brasil's release clips, melee hit (21) -> immediate skill hit (25)
CLIP_EVENTS = {"ATT2.ZMO": {29: (21, 25)}, "ATT3.ZMO": {22: (21, 25)}}

# the challenge
LEVEL = 220
OFFERING = (12, 276, 10)            # 10 x Devourer Horn
OFFER_WHERE = 12                    # COND_004 iWhere: the bag (import-ulverick.py)
AT_LEVEL, OP_GE, OP_TAKE = 31, 2, 0
TEAM_MOB = 100
SPAWN_RANGE = 1                     # retail's value for an at-the-NPC spawn
QSD_PATTERN = "Hebarn"
TRIG_CHALLENGE, TRIG_RETURN = "Hebarn-Challenge", "Hebarn-Return"
EVENT_ROW = 139
TALK_CON = "EM80-005.con"
CON_KEY = "heb"
QUEST_EDITOR = os.path.join(ROOT, "bin", "release", "quest-editor.exe")
CON_TEXT = {
    "--greet": "So another mortal crawls to my shrine. I am Hebarn, God of Malice. "
               "Arua's pets come to pray here; I come to watch them fail.",
    "--ask": "I challenge you, Hebarn!",
    "--bye": "I will leave you to your shrine.",
    "--offer": "Bold. A god is not fought for free: lay ten Devourer Horns before me "
               "as tribute, and I will grant you the honour of dying by my lance.",
    "--lack": "You bring nothing, and you are nothing. Come back when you are level "
              f"{LEVEL} with ten Devourer Horns in your bag, and I may deign to crush you.",
    "--accept": "Take your tribute. Face me!",
    "--decline": "...Not today.",
    "--later": "I will be back, Hebarn.",
}

# placement (world metres)
TALK_POS = (5030.0, 5232.0)
CHUNK_CM, GRID_CM, ORIGIN_CM = 16000, 250, 520000
MAP_XY_OFF, POS_OFF = 12, 36

# loot
WEAPONS = (1389, 1392, 1395, 1398, 1401, 1404, 1407, 1410, 1383, 1386, 1413, 1416, 1419)
GAUNTLETS, BOOTS = (892, 893, 894, 895), (890, 891, 892, 893)
BODIES, CAPS = (916, 917, 918, 919), (989, 990, 991, 992)
T_CAP, T_BODY, T_ARMS, T_FOOT, T_WEAPON, T_SUBWPN, T_USE, T_NATURAL = 2, 3, 4, 5, 8, 9, 10, 12
DROP_LABEL = "Hebarn, God of Malice"
DEATH_DROPS = [[T_BODY * 1000 + n for n in BODIES] + [-1],     # AIACT17 picks 1 of 5
               [T_CAP * 1000 + n for n in CAPS] + [-1]]

# the AI
AI_FILE = "HEBARN.AIP"
AI_ROW = 2252
LINES = {   # AI_s.STB, appended; AIACT28 type 1 = zone shout, 0 = local chat
    "spawn": "So be it, mortal. Your tribute is accepted - now kneel, or burn!",
    "enrage": "You dare wound a god?! Feel the fire of Hebarn!",
    "kill": "Pathetic.",
    "death": "This body falls... but Hebarn is eternal. I will be waiting at my shrine.",
    "leave": "You waste a god's time. Begone.",
}
IDLE_LIFETIME = 900                 # s before an idle Hebarn walks away


def load(name, fname):
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


oro = load("import_oro", "import-oro.py")                      # STB/STL/CHR/ZSC/IFO codecs
mon = load("audit_ai_monster_refs", "audit-ai-monster-refs.py")  # .aip codec
fate = load("import_oro_fate", "import-oro-fate.py")           # QSD codec
kd = load("add_karkia_drops", "add-karkia-drops.py")           # drop-row encoding
from mapgen import prefab  # noqa: E402

BAK_TRACKED = [NPC_STB_REL, NPC_STL_REL, NPC_CHR_REL, ZSC_REL, SKILL_STB_REL, DROP_STB_REL,
               AI_STB_REL, AI_STR_REL, EVENT_STB_REL, QSD_REL, r"3DDATA\EVENT\ulngtb_con.ltb"]


def P(root, rel):
    return os.path.join(root, rel.replace("\\", "/"))


def find_ci(folder, name):
    hit = [f for f in os.listdir(folder) if f.lower() == name.lower()]
    if not hit:
        raise SystemExit(f"{name} not found in {folder}")
    return os.path.join(folder, hit[0])


def write_if_changed(path, blob, dry):
    if os.path.isfile(path) and open(path, "rb").read() == blob:
        return False
    if not dry:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if os.path.isfile(path):
            keep = os.path.join(BUILD_DIR, "orig", os.path.relpath(path, ROOT))
            if not os.path.isfile(keep):
                os.makedirs(os.path.dirname(keep), exist_ok=True)
                shutil.copyfile(path, keep)
        with open(path, "wb") as fh:
            fh.write(blob)
    return True


def sweep_baks(ours):
    """Move the codecs' .bak files out of data/ -- pack.ps1 refuses to bake them."""
    moved = 0
    extra = [r"3DDATA\EVENT\%s" % f for f in os.listdir(P(ours, r"3DDATA\EVENT"))
             if f.lower().endswith(".bak")]
    for rel in BAK_TRACKED + [e[:-4] for e in extra]:
        bak = P(ours, rel) + ".bak"
        if not os.path.isfile(bak):
            continue
        dest = os.path.join(BUILD_DIR, "bak", rel.replace("\\", "/") + ".bak")
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        if os.path.isfile(dest):
            os.remove(bak)                 # keep the oldest pre-import copy
        else:
            shutil.move(bak, dest)
        moved += 1
    if moved:
        print(f"moved {moved} .bak file(s) to {os.path.join(BUILD_DIR, 'bak')}")


def b(v):
    return str(v).encode("latin-1")


# ------------------------------------------------------------------ AI encoding
# Layouts from src/common/shared/cai_file.h, sizes and padding checked against
# the retail files in data/3DDATA/AI (CD padding as the AI editor wrote it).
def _cond(n, payload):
    return struct.pack("<II", 8 + len(payload), 0x04000000 | (n + 1)) + payload


def _act(n, payload):
    return struct.pack("<II", 8 + len(payload), 0x0B000000 | (n + 1)) + payload


PAD1, PAD2, PAD3 = b"\xcd", b"\xcd\xcd", b"\xcd\xcd\xcd"


def c_enemies_near(metres, count=1):       # AICOND02: count enemies within metres
    return _cond(2, struct.pack("<iB", metres, 0) + PAD1 + struct.pack("<hhH", -100, 100, count))


def c_moved_from_spawn(metres):            # AICOND03: moved at least metres from birth
    return _cond(3, struct.pack("<i", metres))


def c_target_at_least(metres):             # AICOND04 cMoreLess 0: target >= metres away
    return _cond(4, struct.pack("<iB", metres, 0) + PAD3)


def c_target_within(metres):               # AICOND04 cMoreLess 1: target <= metres away
    return _cond(4, struct.pack("<iB", metres, 1) + PAD3)


def c_hp_at_most(pct):                     # AICOND06 cMoreLess 1: own HP% <= pct
    return _cond(6, struct.pack("<IB", pct, 1) + PAD3)


def c_chance(pct):                         # AICOND07
    return _cond(7, struct.pack("<B", pct) + PAD3)


def c_attacker_not_target():               # AICOND09
    return _cond(9, b"")


def c_select_npc(npc):                     # AICOND17: server-wide NPC lookup
    return _cond(17, struct.pack("<i", npc))


def c_var(var, value, op=0):               # AICOND28: monster variable (0 '=')
    return _cond(28, struct.pack("<h", var) + PAD2 + struct.pack("<iB", value, op) + PAD3)


def c_lived(seconds):                      # AICOND30
    return _cond(30, struct.pack("<i", seconds))


def a_attack_found():                      # AIACT13: attack what the condition found
    return _act(13, b"")


def a_attack_attacker():                   # AIACT15
    return _act(15, b"")


def a_run_home(metres):                    # AIACT16: back within metres of birth
    return _act(16, struct.pack("<i", metres))


def a_drop(items, to_owner=1):             # AIACT17: one of five, -1 = nothing
    return _act(17, struct.pack("<5h", *items) + PAD2 + struct.pack("<i", to_owner))


def a_suicide():                           # AIACT23
    return _act(23, b"")


def a_cast(skill, motion, target=1):       # AIACT24: 1 = current target
    return _act(24, struct.pack("<B", target) + PAD1 + struct.pack("<hh", skill, motion) + PAD2)


def a_say(str_row, kind):                  # AIACT28: AI_s.STB row + 1
    return _act(28, struct.pack("<B", kind) + PAD3 + struct.pack("<i", str_row + 1))


def a_trigger(name):                       # AIACT30: len incl NUL, name, NUL, 1 pad
    raw = name.encode("latin-1") + b"\0"
    return _act(30, struct.pack("<h", len(raw)) + raw + b"\0")


def a_set_var(var, value, op=5):           # AIACT35, op 5 = set, 6 = add
    return _act(35, struct.pack("<h", var) + PAD2 + struct.pack("<iB", value, op) + PAD3)


def ev(name, conds, acts):
    return (name.encode("latin-1")[:31].ljust(32, b"\0"), conds, acts)


def pat(name):
    return name.encode("latin-1")[:31].ljust(32, b"\0")


def build_ai(line_rows):
    L = line_rows
    shout, local = 1, 0
    pats = [
        (pat("created"), [
            ev("challenge accepted", [c_enemies_near(30)], [a_say(L["spawn"], shout), a_attack_found()]),
            ev("spawned alone", [], [a_say(L["spawn"], shout)]),
        ]),
        (pat("stop"), [
            ev("walk away", [c_lived(IDLE_LIFETIME), c_select_npc(TALK)],
               [a_say(L["leave"], shout), a_set_var(1, 1), a_trigger(TRIG_RETURN), a_suicide()]),
            ev("go home", [c_moved_from_spawn(40)], [a_run_home(3)]),
            ev("seek", [c_enemies_near(25)], [a_attack_found()]),
        ]),
        (pat("attack move"), [       # runs only while he moves towards his target
            ev("leash", [c_moved_from_spawn(60)], [a_run_home(3)]),
            ev("charge", [c_chance(25), c_target_at_least(CHARGE_REACH[0]),
                          c_target_within(CHARGE_REACH[1])], [a_cast(CHARGE, 8)]),
        ]),
        (pat("damaged"), [           # runs on HEADER_DAMAGED_RATE % of the hits he takes
            ev("enrage", [c_hp_at_most(30), c_var(0, 0), c_target_within(STOMP_REACH)],
               [a_say(L["enrage"], shout), a_set_var(0, 1), a_cast(STOMP, 6), a_set_var(2, 0)]),
            ev("stomp", [c_var(2, STOMP_EVERY, 2), c_target_within(STOMP_REACH)],
               [a_cast(STOMP, 6), a_set_var(2, 0)]),
            ev("retaliate", [c_chance(25), c_attacker_not_target()],
               [a_attack_attacker(), a_set_var(2, 1, 6)]),
            ev("count", [], [a_set_var(2, 1, 6)]),
        ]),
        (pat("kill"), [
            ev("gloat", [c_chance(50)], [a_say(L["kill"], local)]),
        ]),
        (pat("dead"), [
            ev("fallen", [c_var(1, 0), c_select_npc(TALK)],
               [a_say(L["death"], shout)] + [a_drop(d) for d in DEATH_DROPS] + [a_trigger(TRIG_RETURN)]),
            ev("gone", [c_select_npc(TALK)], [a_trigger(TRIG_RETURN)]),
        ]),
    ]
    # header: patterns, idle check every 3 s, damaged pattern on 50% of hits taken
    return mon.build_aip((len(pats), 3, HEADER_DAMAGED_RATE), b"Hebarn, God of Malice\0", pats, b"")


# ------------------------------------------------------------------ QSD
def challenge_trigger():
    t, no, n = OFFERING
    level = struct.pack("<i", 1) + struct.pack("<iiB3x", AT_LEVEL, LEVEL, OP_GE)
    c_level = struct.pack("<Ii", 8 + len(level), 3) + level
    have = struct.pack("<i", 1) + struct.pack("<IiiB3x", t * 1000 + no, OFFER_WHERE, n, OP_GE)
    c_have = struct.pack("<Ii", 8 + len(have), 4) + have
    c_npc = struct.pack("<Iii", 12, 13, TALK)
    r_take = struct.pack("<IiIBxhB3x", 20, 0x01000001, t * 1000 + no, OP_TAKE, n, 0)
    r_hide = struct.pack("<IiB3x", 12, 0x01000022, 0)
    r_spawn = struct.pack("<IiiiB3xiiiii", 40, 0x01000008, BOSS, 1, 1, 0, 0, 0, SPAWN_RANGE, TEAM_MOB)
    return fate.qsd_build_trigger(TRIG_CHALLENGE, [c_level, c_have, c_npc], [r_take, r_hide, r_spawn])


def return_trigger():
    c_npc = struct.pack("<Iii", 12, 13, TALK)
    r_show = struct.pack("<IiB3x", 12, 0x01000022, 1)
    return fate.qsd_build_trigger(TRIG_RETURN, [c_npc], [r_show])


# ------------------------------------------------------------------ stage 1: model
def zmo_events(blob):
    if blob[-4:] not in (b"EZMO", b"3ZMO"):
        return None, None
    off, = struct.unpack_from("<I", blob, len(blob) - 8)
    n, = struct.unpack_from("<H", blob, off)
    return off + 2, list(struct.unpack_from("<%dh" % n, blob, off + 2))


def patched_clip(blob, changes):
    base, events = zmo_events(blob)
    if base is None:
        raise SystemExit("release clip has no frame-event trailer")
    out = bytearray(blob)
    for frame, (before, after) in changes.items():
        cur = events[frame]
        if cur not in (before, after):
            raise SystemExit(f"frame {frame} carries event {cur}, expected {before}")
        struct.pack_into("<h", out, base + 2 * frame, after)
    return bytes(out)


def stage1(ours, src, dry):
    print("stage 1 -- the model")
    n = oro.import_characters([TALK, BOSS], ours, src, dry, "Hebarn", src_of={TALK: SRC_NPC, BOSS: SRC_NPC})
    print(f"    {'LIST_NPC.CHR':26s} {n} new entries" if n else f"    {'LIST_NPC.CHR':26s} entries already in place")
    folder = P(ours, MOTION_REL)
    for clip, changes in CLIP_EVENTS.items():
        path = os.path.join(folder, clip)
        if not os.path.isfile(path):
            if dry:
                print(f"    {clip:26s} (copied by this run) event 21 -> 25")
                continue
            path = find_ci(folder, clip)
        changed = write_if_changed(path, patched_clip(open(path, "rb").read(), changes), dry)
        print(f"    {clip:26s} skill-hit frame {'written' if changed else 'in place'}")


# ------------------------------------------------------------------ stage 2: rows
def npc_row(n, template, name, strid, stats, blank, pvp):
    row = list(n.d[template])
    row[C_NAME] = name.encode("latin-1")
    for c, v in stats.items():
        row[c] = b(v)
    for c in blank:
        row[c] = b""
    row[C_STRID] = strid.encode("latin-1")
    row[C_PVP] = pvp
    return row


def drop_cells():
    common = [(T_WEAPON, w) for w in WEAPONS]
    common += [(T_ARMS, g) for g in GAUNTLETS] + [(T_FOOT, f) for f in BOOTS]
    common += [("redirect", 1), ("redirect", 2)]
    common += [(T_SUBWPN, 330), (T_SUBWPN, 317)]
    common += [(T_NATURAL, 762), (T_NATURAL, 758), (T_NATURAL, 276), (T_NATURAL, 290), (T_USE, 35)]
    groups = {1: [(T_BODY, x) for x in BODIES], 2: [(T_CAP, x) for x in CAPS]}
    return kd.build_row(common, groups)


def stage2(ours, dry):
    print("stage 2 -- rows, skills, loot, AI")
    # loot table first: BOSS points at it
    drop = oro.Stb(P(ours, DROP_STB_REL))
    drop_row = None
    for r in range(drop.rows):
        if drop.get(r, 0).decode("latin-1") == DROP_LABEL:
            drop_row = r
    if drop_row is None:
        drop_row = drop.rows
        drop.grow_to(drop_row + 1)
    want = [b""] * drop.cols
    want[0] = DROP_LABEL.encode("latin-1")
    for slot, v in drop_cells().items():
        want[1 + slot] = b(v)
    if drop.d[drop_row] != want:
        drop.d[drop_row] = want
        drop.save(dry)
        print(f"    {'ITEM_DROP.STB':26s} row {drop_row} written")
    else:
        print(f"    {'ITEM_DROP.STB':26s} row {drop_row} in place")

    # speech lines
    ais = oro.Stb(P(ours, AI_STR_REL))
    line_rows, added = {}, 0
    for key, text in LINES.items():
        raw = text.encode("latin-1")
        hit = [r for r in range(ais.rows) if ais.get(r, 1) == raw]
        if hit:
            line_rows[key] = hit[0]
            continue
        r = ais.rows
        ais.grow_to(r + 1)
        for c in range(ais.cols):
            ais.set(r, c, raw)
        line_rows[key] = r
        added += 1
    if added:
        ais.save(dry)
    print(f"    {'AI_s.STB':26s} +{added} lines, rows {sorted(line_rows.values())}")

    # the AI file and its FILE_AI row
    ai_dir = P(ours, r"3DDATA\AI")
    changed = write_if_changed(os.path.join(ai_dir, AI_FILE), build_ai(line_rows), dry)
    print(f"    {AI_FILE:26s} {'written' if changed else 'in place'}")
    fai = oro.Stb(P(ours, AI_STB_REL))
    cell = rf"3DDATA\AI\{AI_FILE}".encode("latin-1")
    if fai.rows <= AI_ROW:
        fai.grow_to(AI_ROW + 1)
    if fai.get(AI_ROW, 0) != cell:
        if fai.get(AI_ROW, 0).strip():
            raise SystemExit(f"FILE_AI row {AI_ROW} is {fai.get(AI_ROW, 0)!r}")
        fai.set(AI_ROW, 0, cell)
        fai.save(dry)
        print(f"    {'FILE_AI.STB':26s} row {AI_ROW} -> {AI_FILE}")

    # skills
    sk = oro.Stb(P(ours, SKILL_STB_REL))
    if sk.rows < SKILL_ROWS:
        sk.grow_to(SKILL_ROWS)
    wrote = []
    for row, name, tmpl, cells in SKILLS:
        cur = sk.get(row, 0).decode("latin-1").strip()
        if cur and cur != name:
            raise SystemExit(f"LIST_SKILL row {row} is {cur!r}")
        want = list(sk.d[tmpl])
        want[0] = name.encode("latin-1")
        want[1] = b(row)
        for c, v in cells.items():
            want[c] = b(v)
        if sk.d[row] != want:
            sk.d[row] = want
            wrote.append(row)
    if wrote:
        sk.save(dry)
    print(f"    {'LIST_SKILL.STB':26s} rows {wrote or 'already in place'}")

    # NPC rows
    n = oro.Stb(P(ours, NPC_STB_REL))
    if n.rows <= BOSS:
        n.grow_to(BOSS + 1)
    rows = {
        TALK: npc_row(n, TALK_TEMPLATE, TALK_NAME, TALK_STRID, TALK_STATS, TALK_BLANK, oro.NPC_PVP_STATE),
        BOSS: npc_row(n, BOSS_TEMPLATE, BOSS_NAME, BOSS_STRID,
                      {**BOSS_STATS, C_AI: AI_ROW, C_DROP: drop_row}, BOSS_BLANK, oro.DEFAULT_PVP_STATE),
    }
    wrote = []
    for i, want in rows.items():
        cur = n.get(i, 0).decode("latin-1").strip()
        if cur and cur not in (TALK_NAME, BOSS_NAME):
            raise SystemExit(f"LIST_NPC row {i} is {cur!r}")
        if n.get(i, C_EXP).strip() and i == BOSS:
            want[C_EXP] = n.get(i, C_EXP)         # rebalance-exp-rewards.py may own it
        if n.d[i] != want:
            n.d[i] = want
            wrote.append(i)
    if wrote:
        n.save(dry)
    print(f"    {'LIST_NPC.STB':26s} rows {wrote or 'already in place'}")
    stl = oro.Stl(P(ours, NPC_STL_REL))
    keys = [(TALK_STRID, TALK, TALK_NAME), (BOSS_STRID, BOSS, BOSS_NAME)]
    new = [k for k in keys if not stl.has(k[0])]
    for k in new:
        stl.append(*k)
    if new:
        stl.save(dry)
    print(f"    {'LIST_NPC_S.STL':26s} +{len(new)} keys")


# ------------------------------------------------------------------ stage 3: the shrine
def placement(ours):
    folder = P(ours, MAPS_REL)
    wx, wy = TALK_POS[0] * 100, TALK_POS[1] * 100
    cx, cy = int(wx // CHUNK_CM), int(wy // CHUNK_CM)
    lx, ly = wx - cx * CHUNK_CM, wy - cy * CHUNK_CM
    ifo = find_ci(folder, f"{cx}_{64 - cy}.IFO")
    field, (x0, y0) = prefab._zone_field(folder)
    z = prefab._height(field, x0, y0, wx, wy)
    buf, bounds = oro.read_ifo(ifo)
    mobs, trailing = oro.read_lump(buf, bounds, oro.LUMP_MOB)
    tmpl = [o for o in mobs or [] if struct.unpack_from("<i", o["fixed"], 8)[0] == TALK_TEMPLATE]
    if not tmpl:
        raise SystemExit(f"NPC {TALK_TEMPLATE} is not placed in {ifo}")
    fixed = bytearray(tmpl[0]["fixed"])
    struct.pack_into("<i", fixed, 8, TALK)
    struct.pack_into("<ii", fixed, MAP_XY_OFF, int(lx // GRID_CM), 63 - int(ly // GRID_CM))
    struct.pack_into("<3f", fixed, POS_OFF, wx - ORIGIN_CM, wy - ORIGIN_CM, z)
    obj = dict(tmpl[0])
    obj["fixed"] = bytes(fixed)
    obj["extra"] = struct.pack("<i", 0) + oro.put_bstr(TALK_CON.encode("latin-1"))
    obj["obj_id"] = TALK
    keep = [o for o in mobs if struct.unpack_from("<i", o["fixed"], 8)[0] != TALK]
    return ifo, buf, bounds, oro.build_object_lump(keep + [obj], trailing), z


def stage3(ours, dry):
    print("stage 3 -- the challenge")
    qp = P(ours, QSD_REL)
    blob = open(qp, "rb").read()
    out = blob
    wants = [challenge_trigger(), return_trigger()]
    missing = []
    for name, want in zip((TRIG_CHALLENGE, TRIG_RETURN), wants):
        have = fate.qsd_trigger_bytes(out, name)
        if have is None:
            qdir = os.path.dirname(qp)
            for f in os.listdir(qdir):
                if f.lower().endswith(".qsd") and f.lower() != os.path.basename(qp).lower():
                    if fate.qsd_has_trigger(open(os.path.join(qdir, f), "rb").read(), name):
                        raise SystemExit(f"{name} already exists in {f} (names are global)")
            missing.append(want)
        elif have != want:
            out = out.replace(have, want, 1)
    if missing:
        if len(missing) != len(wants):
            raise SystemExit("one Hebarn trigger exists without the other: fix QP401.QSD by hand")
        out = fate.qsd_append_pattern(out, QSD_PATTERN, wants)
    ok, _ = fate.qsd_parse_ok(out)
    if not ok or any(fate.qsd_trigger_bytes(out, nm) != w for nm, w in zip((TRIG_CHALLENGE, TRIG_RETURN), wants)):
        raise SystemExit("Hebarn triggers did not round-trip")
    if out != blob and not dry:
        oro.backup(qp)
        with open(qp, "wb") as fh:
            fh.write(out)
    print(f"    {'QP401.QSD':26s} {TRIG_CHALLENGE} + {TRIG_RETURN} "
          + ("in place" if out == blob else "written"))

    e = oro.Stb(P(ours, EVENT_STB_REL))
    erow = [TALK_NAME.encode("latin-1"), e.get(EVENT_ROW - 1, 1) or b"?", b"NPC",
            rf"3Ddata\Event\{TALK_CON}".encode("latin-1")]
    if e.d[EVENT_ROW] != erow:
        if any(x.strip() for x in e.d[EVENT_ROW]):
            raise SystemExit(f"LIST_EVENT row {EVENT_ROW} is {e.get(EVENT_ROW, 0)!r}")
        e.d[EVENT_ROW] = erow
        e.save(dry)
        print(f"    {'LIST_EVENT.STB':26s} row {EVENT_ROW} -> {TALK_CON}")

    if not os.path.isfile(QUEST_EDITOR):
        raise SystemExit(f"{QUEST_EDITOR} missing: build it (cargo build --release -p quest-editor)")
    cmd = [QUEST_EDITOR, "con-toll", ours, TALK_CON, CON_KEY, TRIG_CHALLENGE]
    for k, v in CON_TEXT.items():
        cmd += [k, v]
    if not dry:
        cmd.append("--write")
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"quest-editor con-toll failed:\n{r.stdout}\n{r.stderr}")
    for line in r.stdout.splitlines():
        if line.strip().startswith(("CREATE", "REWRITE", "UNCHANGED", "UPSERT")):
            print(f"    {'dialog':26s} {line.strip()[:110]}")

    ifo, buf, bounds, lump, z = placement(ours)
    off, end = oro.lump_block(bounds, oro.LUMP_MOB)
    if buf[off:end] == lump:
        print(f"    {os.path.basename(ifo):26s} {TALK_NAME} already placed")
    else:
        write_if_changed(ifo, oro.build_ifo(bounds, buf, {oro.LUMP_MOB: lump}), dry)
        print(f"    {os.path.basename(ifo):26s} + {TALK_NAME} at {TALK_POS} m, z {z / 100:.1f} m")


# ------------------------------------------------------------------ verify
def verify(ours):
    bad = []

    def check(ok, what):
        print(f"    {'ok ' if ok else 'BAD'} {what}")
        if not ok:
            bad.append(what)

    n = oro.Stb(P(ours, NPC_STB_REL))
    check(n.get(TALK, 0) == TALK_NAME.encode() and n.get(TALK, C_TYPE) == b"999", "LIST_NPC TALK row")
    check(n.get(BOSS, 0) == BOSS_NAME.encode() and n.get(BOSS, C_AI) == b(AI_ROW), "LIST_NPC BOSS row")
    drop_row = int(n.get(BOSS, C_DROP) or 0)
    drop = oro.Stb(P(ours, DROP_STB_REL))
    check(drop.get(drop_row, 0) == DROP_LABEL.encode(), f"ITEM_DROP row {drop_row}")
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    ok = all(i < len(chr_.chars) and chr_.chars[i] for i in (TALK, BOSS))
    check(ok and chr_.chars[TALK] == chr_.chars[BOSS], "LIST_NPC.CHR entries (shared model)")
    if ok:
        c = chr_.chars[BOSS]
        slots = {t: chr_.motions[a].decode("latin-1") for t, a in c["anims"] if a < len(chr_.motions)}
        check(slots.get(7, "").upper().endswith("ATT2.ZMO") and slots.get(9, "").upper().endswith("ATT3.ZMO"),
              "release slots 7/9 are att2/att3")
        for path in {chr_.skeletons[c["skel"]].decode("latin-1"), *slots.values()}:
            check(os.path.isfile(P(ours, path)), f"file {path}")
    for clip, changes in CLIP_EVENTS.items():
        _, events = zmo_events(open(find_ci(P(ours, MOTION_REL), clip), "rb").read())
        check(all(events[f] == after for f, (_, after) in changes.items()), f"{clip} skill-hit frame")
    sk = oro.Stb(P(ours, SKILL_STB_REL))
    for row, name, _, _ in SKILLS:
        check(sk.get(row, 0) == name.encode(), f"LIST_SKILL {row} {name}")
    fai = oro.Stb(P(ours, AI_STB_REL))
    check(fai.get(AI_ROW, 0).decode("latin-1").upper().endswith(AI_FILE), f"FILE_AI {AI_ROW}")
    blob = open(os.path.join(P(ours, r"3DDATA\AI"), AI_FILE), "rb").read()
    hdr, title, pats, tail = mon.parse_aip(blob)
    check(len(pats) == 6 and mon.build_aip(hdr, title, pats, tail) == blob, f"{AI_FILE} parses")
    q = open(P(ours, QSD_REL), "rb").read()
    check(fate.qsd_parse_ok(q)[0], "QP401.QSD parses")
    check(fate.qsd_trigger_bytes(q, TRIG_CHALLENGE) == challenge_trigger(), TRIG_CHALLENGE)
    check(fate.qsd_trigger_bytes(q, TRIG_RETURN) == return_trigger(), TRIG_RETURN)
    check(os.path.isfile(P(ours, rf"3DDATA\EVENT\{TALK_CON}")), f"dialog {TALK_CON}")
    ifo, buf, bounds, lump, _ = placement(ours)
    off, end = oro.lump_block(bounds, oro.LUMP_MOB)
    check(buf[off:end] == lump, f"{os.path.basename(ifo)} placement")
    print("verify:", "OK" if not bad else f"{len(bad)} problem(s)")
    return 1 if bad else 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--data", default=DATA_ROOT)
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("--stage", type=int, choices=(1, 2, 3))
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    a = ap.parse_args()
    if a.verify:
        return verify(a.data)
    if a.stage in (None, 1):
        stage1(a.data, a.src, a.dry_run)
    if a.stage in (None, 2):
        stage2(a.data, a.dry_run)
    if a.stage in (None, 3):
        stage3(a.data, a.dry_run)
    if not a.dry_run:
        sweep_baks(a.data)
    print("dry run: nothing written" if a.dry_run else "done -- restart the servers and re-bake the VFS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
