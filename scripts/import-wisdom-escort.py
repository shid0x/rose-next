"""The Wisdom Tree escort: the Tree Keeper, his fighting twin, the ambushers.

Design: doc/wisdom-tree-escort-brief.md. The run itself is the game server's
(src/sho_gameserver/src/wisdom_escort.*); this script writes the data it uses.
**Row numbers, the trigger name and the post are constants in both files: change
them together.**

What it writes:
  * LIST_NPC 1132 [Tree Keeper] Faren: the town NPC at the great tree, on the
    Ranger Paul's row and model (1155, a ranger at rest), with the escort's bow in
    his hand (col 5; the client hangs an NPC's weapon as a monster's). Placed in the MOB lump
    of JG04 `34_31.IFO` at the post, (5544, 5373) m, south of the trunk. The
    server hides him while a run is out (CObjNPC::VSet_SHOW) and keeps his event
    value 0 at 1 meanwhile.
  * His dialog, EM26-002.con (LIST_EVENT row 232, next to Kay's EM26-001 at 231),
    written by `quest-editor con-toll`: "walk the roots with him" fires
    `WisdomTree-Start` when its conditions pass, and a "busy" answer shows while
    his event value is not 0.
  * Quest 5521 "The Great Tree's Rounds" (LIST_QUEST + LIST_QUEST_S.STL), 15
    minutes on its timer: the run is a quest in the log while it lasts. That is what
    puts "!" over the keeper -- the client's quest icons count a dialog trigger that
    registers a quest (src/client/event/CLAUDE.md) -- and it shows the time left.
  * Pattern WisdomTree in QP401.QSD (beside the Cerberus register trigger):
    - `WisdomTree-Start`: COND_013 selects the keeper, COND_011 needs his event
      value 0, COND_003 level >= 40; registers the quest. The server recognises the
      trigger by name once it passed (classUSER::Do_QuestTRIGGER) and starts the run.
    - `WisdomTree-Join`: registers the quest; the server fires it for the party
      members on the roster.
    - `WisdomTree-End`: has the quest, finishes it (REWD_000 op 0); the server fires
      it for everyone when the run ends, and for anyone in the forest still holding
      it outside a run (a refused start, a logout mid-run).
  * LIST_NPC 225 [Tree Keeper] Faren, the escort: a monster row (Grunter Warrior
    136's columns, so RegenCharacter accepts it) spawned by the server on the
    players' team. Model: Paul's mesh on the robber skeleton with Kay's motions
    (1131: walk, run, attack, hit, die -- the town NPCs on that skeleton never
    fought, but every clip is there). His attack clip fires on event 22, a bow
    shot, so he carries a bow: the Elf Bow (LIST_WEAPON 206) on the right-hand
    dummy p_00, as the forest's Ranger Clowns carry theirs; its arrow (bullet
    151) flies and the hit lands on impact. He runs between stops (450, ~70% of
    a player; at his 200 walk the party waited on him). No EXP, no drop.
  * LIST_NPC 226-229, the ambushers, each a copy of a forest monster with its own
    AI and a sickly green glow: Blight Grunter (136 Grunter Warrior), Blight
    Smouly (52 Elder Smouly), Withered Guardian (208 Guardian Tree), Grunter
    Blightchief (209 Grunter King). New rows on purpose: AI is per row, and the
    forest's own monsters keep theirs. Their template's EXP and drops stay; the
    death trigger (col 41) and target col 25 are cleared.
  * Four AI files (FILE_AI 2265-2268), appended:
    - WISDOM_KEEPER.AIP: every second while stopped, attack an enemy within 12 m;
      when hit, 40% to turn on the attacker. No leash, no wander: the server
      walks him between his stops.
    - WISDOM_AMBUSH.AIP: the server sends each one at the keeper the tick after it
      spawns. Stopped, they attack an enemy within 25 m; a leash of 45 m from
      where they spawned; **when hit, 40% to turn on the attacker** -- how a
      player pulls one off the keeper (ROSE has no threat table; the brief).
    - WISDOM_GUARDIAN.AIP / WISDOM_CHIEF.AIP: the ambusher's, plus the boss's attack
      every 10 hits it takes (var 2), cast only on a target inside it; the
      Blightchief also swings it now and then at a target running off.
  * The two bosses' attacks (2026-11-03, after the first test), both drawn on the
    ground while cast (col 83). Measured with combat-sim.py on typical level-55
    builds: ~355 a Rootquake / ~420 a Cleaver on a Knight (1180 HP), ~260 / ~310 on
    a Mage (690 HP); normal hits are 60-120.
    - LIST_SKILL 7203 Rootquake (the Withered Guardian): the Warden's Seal Burst row,
      a 9 m type-17 circle around it with its 40% slow for 4 s; a ~2 s wind-up.
    - LIST_SKILL 7204 Blight Cleaver (the Blightchief): the Grunter King's own axe
      (Strike, slots 8/9) as three 15 x 2 m lines fanned 30 degrees apart -- stand
      between them, or behind it. The fan is a new shape: LIST_SKILL col 84,
      io_skill.h SKILL_FAN, measured by the server and drawn by the client.
    - Their own clips (CLIP_SLOTS): the tree's release with event 74 (the
      shockwave); the axe split where it is up, the raise played slowly as the
      wind-up and the slam onward as the release, the second swing animation only.

Run, then bake and restart (LIST_NPC, LIST_NPC.CHR, LIST_SKILL, FILE_AI, QP401.QSD,
the clips and the IFO are server data; the .CON and LTB are client data). After
changing AI or skills: `audit-ai-skill-refs.py --dry-run`.

    python scripts/import-wisdom-escort.py --dry-run
    python scripts/import-wisdom-escort.py
    python scripts/import-wisdom-escort.py --verify

Backups of everything touched: build/wisdom-escort/orig/ (taken once, before the
first write). Test in game with `/escort start|status|skip|stop` (GM).
"""
import argparse
import copy
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import rosedata as oro  # noqa: E402
from rosefmt import ifo, lnav, qsd, zmo as rfzmo  # noqa: E402

BUILD_DIR = os.path.join(ROOT, "build", "wisdom-escort")

NPC_STB_REL = r"3DDATA\STB\LIST_NPC.STB"
NPC_STL_REL = r"3DDATA\STB\LIST_NPC_S.STL"
NPC_CHR_REL = r"3DDATA\NPC\LIST_NPC.CHR"
AI_STB_REL = r"3DDATA\STB\FILE_AI.STB"
AI_DIR_REL = r"3DDATA\AI"
EVENT_STB_REL = r"3DDATA\STB\LIST_EVENT.STB"
QUEST_STB_REL = r"3DDATA\STB\LIST_QUEST.STB"
QUEST_STL_REL = r"3DDATA\STB\LIST_QUEST_S.STL"
QSD_REL = r"3DDATA\QUESTDATA\QP401.QSD"
SKILL_STB_REL = r"3DDATA\STB\LIST_SKILL.STB"
SKILL_STL_REL = r"3DDATA\STB\LIST_SKILL_S.STL"
LTB_REL = r"3DDATA\EVENT\ulngtb_con.ltb"
MAP_REL = r"3DDATA\MAPS\JUNON\JG04"
IFO_NAME = "34_31.IFO"
LNAV_NAME = "JG04.lnav"
TRACKED = [NPC_STB_REL, NPC_STL_REL, NPC_CHR_REL, AI_STB_REL, EVENT_STB_REL, QUEST_STB_REL,
           QUEST_STL_REL, QSD_REL, LTB_REL, MAP_REL + "\\" + IFO_NAME, SKILL_STB_REL, SKILL_STL_REL]

C_NAME, C_MODEL, C_WALK, C_RUN, C_SIZE, C_RWEAPON, C_LWEAPON, C_LEVEL, C_HP, C_ATK, C_HIT, \
    C_DEF, C_RES, C_AVOID, C_ASPD, C_MAGIC, C_AI, C_EXP, C_DROP, C_MONEY, C_DROPRATE = range(21)
C_TARGET, C_RANGE, C_TYPE, C_HANDHIT, C_GLOW, C_DEADEVENT, C_HEIGHT = 25, 26, 27, 33, 39, 41, 42
C_SOUNDS = (30, 31, 32)

# ---- the town keeper (the dialog)
KEEPER = 1132                       # blank, no CHR entry; next to Kay (1131)
KEEPER_TEMPLATE = 1155              # [Ranger] Paul: row and model
KEEPER_NAME = "[Tree Keeper] Faren"
KEEPER_STRID = "LWTKNPC1132"
KEEPER_BOW = 206                    # Elf Bow (lv 47), in his hand at the tree and on the round
POST = (5544.0, 5373.0)             # world m: STOPS[0] in wisdom_escort.cpp
KEEPER_ROT = (0.0, 0.0, -0.3338068723678589, -0.9426414966583252)   # Kay's
EVENT_ROW = 232                     # blank; Kay's EM26-001.con is 231
KEEPER_CON = "EM26-002.con"
TOLL_KEY = "wtk"
QSD_PATTERN, START_TRIGGER = "WisdomTree", "WisdomTree-Start"
JOIN_TRIGGER, END_TRIGGER = "WisdomTree-Join", "WisdomTree-End"
FREE_VALUE = 0                      # his event value while no run is out
MIN_LEVEL = 40
AT_LEVEL, OP_EQ, OP_GE = 31, 0, 2
QOP_FINISH, QOP_REGISTER = 0, 1     # REWD_000 ops (quest-editor gen.rs QuestOp)

# ---- the quest in the log while a run lasts: the "!" over the keeper (the client's
# quest icons count a dialog trigger that registers a quest, REWD_000 op 1), the name
# and the timer in the journal
QUEST = 5521                        # one past the end of LIST_QUEST
QUEST_TEMPLATE = 5520               # quest-editor's last quest: an individual quest row
QUEST_STRID = "LWTKQST5521"
QUEST_TIME = 90                     # 10 s units (CQUEST::GetRemainTIME): RUN_LIMIT_MS
QUEST_TEXT = (
    "The Great Tree's Rounds",
    "Follow the Tree Keeper around the great tree in the Forest of Wisdom and protect "
    "him from whatever is attacking it. If he falls, or the time runs out, the round "
    "is lost.",
    "Thank you for coming. Something is hurting the great tree. I check its roots "
    "every day, and today I fear I will need your help. Stay close to me.",
    "The round is over.",
)
QUEST_EDITOR = os.path.join(ROOT, "bin", "release", "quest-editor.exe")
DIALOG = {
    "--greet": "This tree watched over the forest long before Junon had a name. Lately "
               "something gnaws at its roots, and every day I walk around it to see what "
               "harm was done.",
    "--ask": "Can I help you check on the tree?",
    "--bye": "Farewell.",
    "--offer": "You would come with me? Then stay close. I go from the east roots round to "
               "the pond, and whatever is hurting the tree may still be there. If I fall, or "
               "if we take too long, I turn back. Bring your party: all of you who stand "
               "near me come along.",
    "--lack": "These woods are too dangerous for you yet. Come back when you have reached "
              "level 40.",
    "--accept": "Let's go.",
    "--decline": "Not now.",
    "--later": "I will come back.",
    "--closed": "I am out on my rounds with others right now. Come back in a little while.",
}

# ---- the escort (spawned by the server on TEAMNO_USER)
ESCORT = 225                        # blank, no CHR entry, beside the forest monsters
ESCORT_TEMPLATE = 136               # Grunter Warrior: a monster row's columns
ESCORT_MOTIONS = 1131               # Kay: the robber skeleton with walk/attack/hit/die
ESCORT_STRID = "LWTKMOB0225"
ESCORT_STATS = {
    C_MODEL: "parel.mon",           # /mon refuses a row with no model text
    C_WALK: 200, C_RUN: 450, C_SIZE: 120,   # he runs between stops: ~70% of a player
    C_RWEAPON: KEEPER_BOW, C_LWEAPON: "",   # his attack clip is a bow shot
    C_LEVEL: 55, C_HP: 220,         # 12,100 HP: a wave left alone takes about a minute
    C_ATK: 230, C_HIT: 200, C_DEF: 200, C_RES: 160, C_AVOID: 90, C_ASPD: 100, C_MAGIC: "",
    C_AI: None,                     # KEEPER_AI_ROW, at build time
    C_EXP: 0, C_DROP: 0, C_MONEY: 0, C_DROPRATE: 0,
    C_RANGE: 1000,                  # a bow's reach (the forest's Ranger Clowns: 25 m)
    C_TYPE: 1,
    C_HANDHIT: 403,                 # the retail default impact
}

# ---- the AI
KEEPER_AI_ROW, KEEPER_AI = 2265, "WISDOM_KEEPER.AIP"   # one past the end of FILE_AI
AMBUSH_AI_ROW, AMBUSH_AI = 2266, "WISDOM_AMBUSH.AIP"
GUARDIAN_AI_ROW, GUARDIAN_AI = 2267, "WISDOM_GUARDIAN.AIP"
CHIEF_AI_ROW, CHIEF_AI = 2268, "WISDOM_CHIEF.AIP"
KEEPER_SEEK = 12                    # m
AMBUSH_SEEK = 25                    # m
AMBUSH_LEASH = 45                   # m from where it spawned
TURN_PCT = 40                       # when hit: chance to turn on the attacker
SKILL_EVERY = 10                    # hits a boss takes between two of its attacks (var 2)

# ---- the ambushers: (row, template, name, STL key, AI row)
GUARDIAN, CHIEF = 228, 229
AMBUSHERS = (
    (226, 136, "Blight Grunter", "LWTKMOB0226", AMBUSH_AI_ROW),
    (227, 52, "Blight Smouly", "LWTKMOB0227", AMBUSH_AI_ROW),
    (GUARDIAN, 208, "Withered Guardian", "LWTKMOB0228", GUARDIAN_AI_ROW),
    (CHIEF, 209, "Grunter Blightchief", "LWTKMOB0229", CHIEF_AI_ROW),
)
BLIGHT_GLOW = "080170030"           # RRRGGGBBB: a sickly green

# ---- the two bosses' attacks (2026-11-03, after the first test: "make those two
# encounters a bit more interesting"). Both are drawn on the ground while they are cast
# (col 83, client groundtelegraph.cpp). Rows one past the end of LIST_SKILL; keys
# LMobSkill<row> (name-monster-skills-players.py's convention).
SK_NAME, SK_LINE, SK_TYPE, SK_RANGE, SK_SCOPE, SK_POWER = 0, 1, 5, 6, 8, 9
SK_STATUS, SK_SUCCESS, SK_DURATION, SK_DMGTYPE = 11, 13, 14, 15
SK_CAST_MOTION, SK_CAST_SPEED, SK_ACT_MOTION, SK_ACT_SPEED = 52, 53, 68, 69
SK_TELEGRAPH, SK_FAN, SK_STL_KEY = 83, 84, 86
FX_SHOCKWAVE, FX_LEAF_HIT, FX_STRIKE_CAST, FX_STRIKE_HIT, FX_EARTHSPLIT = 400, 393, 1031, 370, 3003
SND_SKILL_ATTACK, SND_MAGIC_HIT, SND_AXE = 131, 93, 67

# The Withered Guardian's Rootquake: the usual circle to step out of. The Warden's Seal
# Burst row (a type-17 burst around the caster with a 40% slow, status 15 + cols 21/23).
ROOTQUAKE = 7203
ROOTQUAKE_NAME = "Rootquake"
ROOTQUAKE_STRID = "LMobSkill7203"
ROOTQUAKE_DESC = "Tears its roots up out of the ground all around it, slowing whoever they catch."
ROOTQUAKE_SRC = 7023                # Seal Burst
ROOTQUAKE_MOTION = 6                # the tree's cast slot 6, released on 7
ROOTQUAKE_RADIUS = 900              # cm from its centre: its bark reaches ~3 m at 3.4x
ROOTQUAKE_REACH = 7                 # m: cast only on a target inside the burst
ROOTQUAKE_POWER = 120
ROOTQUAKE_CELLS = {
    SK_TYPE: 17, SK_RANGE: "", SK_SCOPE: ROOTQUAKE_RADIUS, SK_POWER: ROOTQUAKE_POWER,
    SK_DURATION: 4,                 # the slow, 4 s (it shows 1-3 s late)
    SK_CAST_MOTION: ROOTQUAKE_MOTION, SK_CAST_SPEED: 50,     # 31 frames: ~2 s to step out
    SK_ACT_MOTION: ROOTQUAKE_MOTION + 1, SK_ACT_SPEED: 100,
    56: "", 57: "", 58: "",         # the Warden's lightning
    62: FX_SHOCKWAVE, 63: 999, 64: SND_SKILL_ATTACK,   # casting effect 2: event 74, the eruption
    74: FX_LEAF_HIT, 75: 999, 76: SND_MAGIC_HIT,
    SK_TELEGRAPH: 1,
}

# The Grunter Blightchief's Blight Cleaver: the Grunter King's own axe skill (Strike 2988,
# slots 8/9) made a fan of three lines on the ground -- stand between them, or behind it.
# A new shape: io_skill.h SKILL_FAN (col 84), measured by the server
# (CObjCHAR::Skill_DamageToAROUND) and drawn by the client. Charge's row (our type-7 strip).
CLEAVER = 7204
CLEAVER_NAME = "Blight Cleaver"
CLEAVER_STRID = "LMobSkill7204"
CLEAVER_DESC = "Brings its axe down in three rending lines. Stand between them, or behind it."
CLEAVER_SRC = 3603                  # Charge
CLEAVER_MOTION = 8                  # wind-up on slot 8, the axe on 9
CLEAVER_LENGTH, CLEAVER_WIDTH = 1500, 200   # cm: each line 15 x 2 m from its centre
CLEAVER_FAN = 3 * 1000 + 30         # three lines, 30 degrees apart: 2 m+ between them 6 m out
CLEAVER_RANGE = 1200                # cm: cast on a target this close, no chase first
CLEAVER_REACH = 10                  # m
CLEAVER_POWER = 150
CLEAVER_CELLS = {
    SK_TYPE: 7, SK_RANGE: CLEAVER_RANGE, SK_SCOPE: CLEAVER_LENGTH * 10000 + CLEAVER_WIDTH,
    SK_POWER: CLEAVER_POWER,
    SK_CAST_MOTION: CLEAVER_MOTION, SK_CAST_SPEED: 20,       # the 12-frame raise: ~2 s
    SK_ACT_MOTION: CLEAVER_MOTION + 1, SK_ACT_SPEED: 100,
    56: FX_STRIKE_CAST, 57: 999, 58: SND_SKILL_ATTACK,     # casting effect 0: event 44, the raise
    62: FX_EARTHSPLIT, 63: 999, 64: "",                    # casting effect 2: event 74, the crack
    74: FX_STRIKE_HIT, 75: 999, 76: SND_AXE,
    SK_TELEGRAPH: 1, SK_FAN: CLEAVER_FAN,
}
SKILLS = (  # row, source row, name, cells, STL key, description
    (ROOTQUAKE, ROOTQUAKE_SRC, ROOTQUAKE_NAME, ROOTQUAKE_CELLS, ROOTQUAKE_STRID, ROOTQUAKE_DESC),
    (CLEAVER, CLEAVER_SRC, CLEAVER_NAME, CLEAVER_CELLS, CLEAVER_STRID, CLEAVER_DESC),
)

# The bosses' own clips (making-monsters.md: a changed clip is the monster's own copy):
#   - the tree's release with event 74 (the shockwave) beside its 24/34;
#   - the Grunter King's Strike axe (Captain_action_skill01, 56 frames, two swings landing
#     on 16 and 38) split where the axe is up: frames 0-11 are the wind-up, played slowly
#     while the lines are on the ground, and 12-55 the release, since the server lands an
#     area skill when its action starts. Only the first swing keeps its events (24 the
#     area, 35 its sound, 74 the crack); the second is animation only.
CLIP_GUARDIAN = r"3Ddata\MOTION\NPC\WITHERED_GUARDIAN\WG_ROOTQUAKE.ZMO"
CLIP_CHIEF_WINDUP = r"3Ddata\MOTION\NPC\GRUNTER_BLIGHTCHIEF\GB_CLEAVE_WINDUP.ZMO"
CLIP_CHIEF_CLEAVE = r"3Ddata\MOTION\NPC\GRUNTER_BLIGHTCHIEF\GB_CLEAVE.ZMO"
AXE_SPLIT = 12
CLIP_SLOTS = {GUARDIAN: {ROOTQUAKE_MOTION + 1: CLIP_GUARDIAN},
              CHIEF: {CLEAVER_MOTION: CLIP_CHIEF_WINDUP, CLEAVER_MOTION + 1: CLIP_CHIEF_CLEAVE}}


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


mon = load("audit_ai_monster_refs", "audit-ai-monster-refs.py")   # .aip builder
heb = load("import_hebarn", "import-hebarn.py")       # AI condition/action encoders


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


def keep_originals(ours):
    """Snapshot every file this script touches, once, before the first write."""
    for rel in TRACKED:
        src = find_ci(ours, rel)
        dst = os.path.join(BUILD_DIR, "orig", rel.replace("\\", "/"))
        if os.path.isfile(src) and not os.path.isfile(dst):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)


def sweep_baks(ours):
    """Move .bak files the codecs or quest-editor leave beside what they write out of
    data/ -- pack.ps1 refuses to bake while one sits there."""
    moved = 0
    for rel in TRACKED + [r"3DDATA\EVENT\x"]:
        folder = os.path.dirname(find_ci(ours, rel))
        for f in os.listdir(folder):
            if f.lower().endswith(".bak"):
                dest = os.path.join(BUILD_DIR, "bak", os.path.relpath(folder, ours), f)
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                if os.path.isfile(dest):
                    os.remove(os.path.join(folder, f))
                else:
                    shutil.move(os.path.join(folder, f), dest)
                moved += 1
    if moved:
        print(f"    {'.bak files':26s} {moved} moved to build/wisdom-escort/bak")


def write_if_changed(path, blob, dry):
    if os.path.isfile(path) and open(path, "rb").read() == blob:
        return False
    if not dry:
        with open(path, "wb") as fh:
            fh.write(blob)
    return True


# ---------------------------------------------------------------------- the clips
def cleared(blob, keep=None):
    """The clip with every event zeroed, then `keep` ({frame: event}) written."""
    _, ev = rfzmo.events(blob)
    changes = {i: 0 for i, e in enumerate(ev) if e}
    changes.update(keep or {})
    return rfzmo.with_events(blob, changes)


def build_clips(ours, chr_):
    """{data-relative path: clip bytes}, cut from the templates' own slots (refusing any
    that is not the clip this was cut for)."""
    def read(row, slot, frames, events):
        rel = chr_.motions[dict(chr_.chars[row]["anims"])[slot]].decode("latin-1")
        blob = open(find_ci(ours, rel), "rb").read()
        _, ev = rfzmo.events(blob)
        got = {i: e for i, e in enumerate(ev or []) if e}
        if rfzmo.read_header(blob)[1] != frames or got != events:
            raise SystemExit(f"{rel} is not the {frames}-frame clip with events {events} this "
                             f"was cut for (has {got}) -- refusing")
        return blob

    tree = read(208, ROOTQUAKE_MOTION + 1, 31, {10: 34, 12: 24})
    axe = read(209, CLEAVER_MOTION + 1, 56, {14: 35, 16: 25, 36: 35, 38: 25})
    return {
        CLIP_GUARDIAN: rfzmo.with_events(tree, {11: 74}),
        CLIP_CHIEF_WINDUP: cleared(rfzmo.sliced(axe, 0, AXE_SPLIT), {0: 44}),
        CLIP_CHIEF_CLEAVE: cleared(rfzmo.sliced(axe, AXE_SPLIT, 56),
                                   {14 - AXE_SPLIT: 35, 16 - AXE_SPLIT: 24, 17 - AXE_SPLIT: 74}),
    }


def clips(ours, dry):
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    for rel, blob in build_clips(ours, chr_).items():
        path = P(ours, rel)
        if not dry:
            os.makedirs(os.path.dirname(path), exist_ok=True)
        changed = write_if_changed(path, blob, dry)
        print(f"    {os.path.basename(rel):26s} {'written' if changed else 'in place'}")


def motion_index(chr_, rel):
    """The CHR motion list's index of a clip, appended when it is not there."""
    key = rel.encode("latin-1")
    hit = [i for i, m in enumerate(chr_.motions) if m.lower() == key.lower()]
    if hit:
        return hit[0]
    chr_.motions.append(key)
    return len(chr_.motions) - 1


# ---------------------------------------------------------------------- the AI
def keeper_ai():
    pats = [
        (heb.pat("created"), []),
        (heb.pat("stop"), [heb.ev("defend", [heb.c_enemies_near(KEEPER_SEEK)], [heb.a_attack_found()])]),
        (heb.pat("attack move"), []),
        (heb.pat("damaged"), [heb.ev("answer", [heb.c_chance(TURN_PCT), heb.c_attacker_not_target()],
                                     [heb.a_attack_attacker()])]),
        (heb.pat("kill"), []),
        (heb.pat("dead"), []),
    ]
    # the idle check every second (he must answer a wave at once), damaged on every hit
    return mon.build_aip((len(pats), 1, 100), b"Tree Keeper\0", pats, b"")


def ambush_ai():
    pats = [
        (heb.pat("created"), []),    # the server sends it at the keeper
        (heb.pat("stop"), [heb.ev("seek", [heb.c_enemies_near(AMBUSH_SEEK)], [heb.a_attack_found()])]),
        (heb.pat("attack move"), [heb.ev("leash", [heb.c_moved_from_spawn(AMBUSH_LEASH)],
                                         [heb.a_run_home(3)])]),
        (heb.pat("damaged"), [heb.ev("pulled", [heb.c_chance(TURN_PCT), heb.c_attacker_not_target()],
                                     [heb.a_attack_attacker()])]),
        (heb.pat("kill"), []),
        (heb.pat("dead"), []),
    ]
    return mon.build_aip((len(pats), 2, 100), b"Wisdom Tree ambusher\0", pats, b"")


def boss_ai(title, skill, motion, reach, chase):
    """The ambusher's AI, with an attack every SKILL_EVERY hits taken (monster var 2,
    counted by every hit, the pull included), cast only on a target inside it; `chase`
    also casts it now and then on a target running away."""
    count = heb.a_set_var(2, 1, 6)
    cast = [heb.a_cast(skill, motion), heb.a_set_var(2, 0)]
    move = [heb.ev("leash", [heb.c_moved_from_spawn(AMBUSH_LEASH)], [heb.a_run_home(3)])]
    if chase:
        move.append(heb.ev("on the run", [heb.c_chance(25), heb.c_var(2, SKILL_EVERY // 2, 2),
                                          heb.c_target_at_least(4), heb.c_target_within(reach)], cast))
    pats = [
        (heb.pat("created"), []),
        (heb.pat("stop"), [heb.ev("seek", [heb.c_enemies_near(AMBUSH_SEEK)], [heb.a_attack_found()])]),
        (heb.pat("attack move"), move),
        (heb.pat("damaged"), [
            heb.ev("attack", [heb.c_var(2, SKILL_EVERY, 2), heb.c_target_within(reach)], cast),
            heb.ev("pulled", [heb.c_chance(TURN_PCT), heb.c_attacker_not_target()],
                   [heb.a_attack_attacker(), count]),
            heb.ev("count", [], [count]),
        ]),
        (heb.pat("kill"), []),
        (heb.pat("dead"), []),
    ]
    return mon.build_aip((len(pats), 2, 100), title, pats, b"")


def guardian_ai():
    return boss_ai(b"Withered Guardian\0", ROOTQUAKE, ROOTQUAKE_MOTION, ROOTQUAKE_REACH, False)


def chief_ai():
    return boss_ai(b"Grunter Blightchief\0", CLEAVER, CLEAVER_MOTION, CLEAVER_REACH, True)


AI_FILES = ((KEEPER_AI_ROW, KEEPER_AI, keeper_ai), (AMBUSH_AI_ROW, AMBUSH_AI, ambush_ai),
            (GUARDIAN_AI_ROW, GUARDIAN_AI, guardian_ai), (CHIEF_AI_ROW, CHIEF_AI, chief_ai))


# ---------------------------------------------------------------------- the rows
def keeper_row(n):
    row = list(n.d[KEEPER_TEMPLATE])
    row[C_NAME] = KEEPER_NAME.encode("latin-1")
    row[C_RWEAPON] = b(KEEPER_BOW)
    row[oro.NPC_STRID_COL] = KEEPER_STRID.encode()
    for c in oro.NPC_SELL_TAB_COLS:
        row[c] = b""
    return row


def escort_row(n):
    row = list(n.d[ESCORT_TEMPLATE])
    row[C_NAME] = KEEPER_NAME.encode("latin-1")
    for c, v in ESCORT_STATS.items():
        row[c] = b(KEEPER_AI_ROW if c == C_AI else v)
    for c in (C_TARGET, C_GLOW, C_DEADEVENT, C_HEIGHT, *C_SOUNDS, *oro.NPC_SELL_TAB_COLS):
        row[c] = b""
    row[oro.NPC_STRID_COL] = ESCORT_STRID.encode()
    row[oro.NPC_PVP_COL] = oro.DEFAULT_PVP_STATE
    return row


def ambusher_row(n, row_no, template, name, strid, ai_row):
    row = list(n.d[template])
    row[C_NAME] = name.encode("latin-1")
    row[C_AI] = b(ai_row)
    row[C_GLOW] = BLIGHT_GLOW.encode()
    for c in (C_TARGET, C_DEADEVENT, *oro.NPC_SELL_TAB_COLS):
        row[c] = b""
    row[oro.NPC_STRID_COL] = strid.encode()
    row[oro.NPC_PVP_COL] = oro.DEFAULT_PVP_STATE
    if n.get(row_no, C_NAME).strip():
        row[C_EXP] = n.get(row_no, C_EXP)       # rebalance-exp-rewards.py owns it
    return row


def rows_wanted(n):
    """{row: (cells, name, STL key, CHR builder)}; a builder takes the Chr (it may add a
    clip to the motion list)."""
    def chr_copy(src, own=None):
        def build(chr_):
            e = copy.deepcopy(chr_.chars[src])
            if own:
                slots = {s for s, _ in e["anims"]}
                if not set(own) <= slots:
                    raise SystemExit(f"LIST_NPC.CHR {src} has no slots {sorted(set(own) - slots)}")
                e["anims"] = [(s, motion_index(chr_, own[s]) if s in own else i) for s, i in e["anims"]]
            return e
        return build

    def escort_chr(chr_):
        e = copy.deepcopy(chr_.chars[ESCORT_MOTIONS])
        e["models"] = list(chr_.chars[KEEPER_TEMPLATE]["models"])
        return e
    want = {KEEPER: (keeper_row(n), KEEPER_NAME, KEEPER_STRID, chr_copy(KEEPER_TEMPLATE)),
            ESCORT: (escort_row(n), KEEPER_NAME, ESCORT_STRID, escort_chr)}
    for row_no, template, name, strid, ai_row in AMBUSHERS:
        want[row_no] = (ambusher_row(n, row_no, template, name, strid, ai_row), name, strid,
                        chr_copy(template, CLIP_SLOTS.get(row_no)))
    return want


def npc_rows(ours, dry):
    n = oro.Stb(P(ours, NPC_STB_REL))
    stl = oro.Stl(P(ours, NPC_STL_REL))
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    dirty = {"stb": False, "stl": False, "chr": False}
    for row_no, (cells, name, strid, chr_build) in sorted(rows_wanted(n).items()):
        cur = n.get(row_no, C_NAME).decode("latin-1").strip()
        if cur and cur != name:
            raise SystemExit(f"our LIST_NPC row {row_no} is {cur!r}")
        if row_no < len(chr_.chars) and chr_.chars[row_no] is not None and not cur:
            raise SystemExit(f"LIST_NPC row {row_no} is blank but has a CHR entry: not free")
        if n.d[row_no] != cells:
            n.d[row_no] = cells
            dirty["stb"] = True
            print(f"    {'LIST_NPC.STB':26s} row {row_no} {name}")
        if not stl.has(strid):
            stl.append(strid, row_no, name)
            dirty["stl"] = True
            print(f"    {'LIST_NPC_S.STL':26s} +key {strid}")
        want_chr = chr_build(chr_)
        if chr_.chars[row_no] != want_chr:
            chr_.chars[row_no] = want_chr
            dirty["chr"] = True
            print(f"    {'LIST_NPC.CHR':26s} entry {row_no}")
    if dirty["stb"]:
        n.save(dry, keep_backup=False)
    if dirty["stl"]:
        stl.save(dry, keep_backup=False)
    if dirty["chr"]:
        chr_.save(dry, keep_backup=False)
    if not any(dirty.values()):
        print(f"    {'LIST_NPC rows':26s} already in place")


def ai_files(ours, dry):
    fai = oro.Stb(P(ours, AI_STB_REL))
    grown = False
    for row_no, fname, build in AI_FILES:
        cell = rf"3DDATA\AI\{fname}".encode("latin-1")
        if fai.rows <= row_no:
            fai.grow_to(row_no + 1)
            grown = True
        if fai.get(row_no, 0) != cell:
            if fai.get(row_no, 0).strip():
                raise SystemExit(f"our FILE_AI row {row_no} is {fai.get(row_no, 0)!r}")
            fai.set(row_no, 0, cell)
            grown = True
            print(f"    {'FILE_AI.STB':26s} row {row_no} = {fname}")
        changed = write_if_changed(os.path.join(P(ours, AI_DIR_REL), fname), build(), dry)
        print(f"    {fname:26s} {'written' if changed else 'in place'}")
    if grown:
        fai.save(dry, keep_backup=False)


# ---------------------------------------------------------------------- trigger, dialog
def skill_row(sk, src, name, line, cells, strid):
    row = list(sk.d[src])
    row[SK_NAME] = name.encode("latin-1")
    row[SK_LINE] = b(line)
    for c, v in cells.items():
        row[c] = b(v)
    row[SK_STL_KEY] = strid.encode()
    return row


def skills(ours, dry):
    sk = oro.Stb(P(ours, SKILL_STB_REL))
    dirty = False
    for row, src, name, cells, strid, _desc in SKILLS:
        if sk.rows <= row:
            if sk.rows != row:
                raise SystemExit(f"LIST_SKILL has {sk.rows} rows: {name} ({row}) would not be the next")
            sk.grow_to(row + 1)
        cur = sk.get(row, SK_NAME).decode("latin-1").strip()
        if cur and cur != name:
            raise SystemExit(f"our LIST_SKILL row {row} is {cur!r}")
        want = skill_row(sk, src, name, row, cells, strid)
        if sk.d[row] != want:
            sk.d[row] = want
            dirty = True
            print(f"    {'LIST_SKILL.STB':26s} row {row} {name} (from {src})")
    if dirty:
        sk.save(dry, keep_backup=False)
    skl = oro.Stl(P(ours, SKILL_STL_REL))
    added = 0
    for row, _src, name, _cells, strid, desc in SKILLS:
        if not skl.has(strid):
            skl.append(strid, row, name, desc)
            added += 1
    if added:
        skl.save(dry, keep_backup=False)
        print(f"    {'LIST_SKILL_S.STL':26s} +{added} keys")


def c_has_quest():                         # COND_000 {int iQuestSN}: selects it too
    return struct.pack("<Iii", 12, 0, QUEST)


def r_quest(op):                           # REWD_000 {int iQuestSN; u8 btOp}
    return struct.pack("<IiiB3x", 16, 0, QUEST, op)


def triggers():
    """[(name, conds, rewds)] of the WisdomTree pattern, in order.

    Start: COND_013 selects the keeper (a server-wide lookup by NPC number), COND_011
    needs his event value 0 to be FREE_VALUE, COND_003 level >= MIN_LEVEL; it
    registers the quest. Layouts from the Cerberus register trigger
    (import-cerberus.py) and quest-editor's gen.rs (COND_000, REWD_000)."""
    c_npc = struct.pack("<Iii", 12, 13, KEEPER)
    c_free = struct.pack("<IiBxhiB3x", 20, 11, 0, 0, FREE_VALUE, OP_EQ)
    abil = [(AT_LEVEL, MIN_LEVEL, OP_GE)]
    pl = struct.pack("<i", len(abil)) + b"".join(struct.pack("<iiB3x", *a) for a in abil)
    c_abil = struct.pack("<Ii", 8 + len(pl), 3) + pl
    return [(START_TRIGGER, [c_npc, c_free, c_abil], [r_quest(QOP_REGISTER)]),
            (JOIN_TRIGGER, [], [r_quest(QOP_REGISTER)]),
            (END_TRIGGER, [c_has_quest()], [r_quest(QOP_FINISH)])]


def trigger(ours, dry):
    qp = P(ours, QSD_REL)
    blob = open(qp, "rb").read()
    q = qsd.parse(blob)
    pname = QSD_PATTERN.encode("latin-1") + b"\0"
    want = [dict(check_next=0, name=name.encode("latin-1") + b"\0", conds=c, rewds=r)
            for name, c, r in triggers()]
    hit = [i for i, (n, _) in enumerate(q["patterns"]) if n == pname]
    if not hit:
        qdir = P(ours, r"3DDATA\QUESTDATA")
        for f in os.listdir(qdir):
            if f.lower().endswith(".qsd") and f.lower() != os.path.basename(qp).lower():
                fb = open(os.path.join(qdir, f), "rb").read()
                for name, _, _ in triggers():
                    if qsd.has_trigger(fb, name):
                        raise SystemExit(f"{name} already exists in {f} (names are global)")
        q["patterns"].append((pname, want))
    else:
        q["patterns"][hit[0]] = (pname, want)
    out = qsd.build(q)
    ok, _ = qsd.parse_ok(out)
    if not ok or any(qsd.trigger_bytes(out, n) != qsd.build_trigger(n, c, r) for n, c, r in triggers()):
        raise SystemExit("the WisdomTree triggers did not round-trip")
    write_if_changed(qp, out, dry)
    print(f"    {'QP401.QSD':26s} {', '.join(n for n, _, _ in triggers())} "
          + ("in place" if out == blob else "written"))


def quest_row(qs):
    row = list(qs.d[QUEST_TEMPLATE])
    row[0] = QUEST_TEXT[0].encode("latin-1")
    row[1] = b(QUEST_TIME)
    row[4] = QUEST_STRID.encode()
    return row


def quest(ours, dry):
    qs = oro.Stb(P(ours, QUEST_STB_REL))
    if qs.rows <= QUEST:
        if qs.rows != QUEST:
            raise SystemExit(f"LIST_QUEST has {qs.rows} rows: quest {QUEST} would not be the next")
        qs.grow_to(QUEST + 1)
    cur = qs.get(QUEST, 0).decode("latin-1").strip()
    if cur and cur != QUEST_TEXT[0]:
        raise SystemExit(f"our LIST_QUEST row {QUEST} is {cur!r}")
    want = quest_row(qs)
    if qs.d[QUEST] != want:
        qs.d[QUEST] = want
        qs.save(dry, keep_backup=False)
        print(f"    {'LIST_QUEST.STB':26s} row {QUEST} {QUEST_TEXT[0]!r}, {QUEST_TIME * 10} s")
    stl = oro.Stl(P(ours, QUEST_STL_REL), encoding="utf-8")
    if not stl.has(QUEST_STRID):
        stl.append(QUEST_STRID, QUEST, *QUEST_TEXT)
        stl.save(dry, keep_backup=False)
        print(f"    {'LIST_QUEST_S.STL':26s} +key {QUEST_STRID}")


def event_row(e):
    return [KEEPER_NAME.encode("latin-1"), e.get(EVENT_ROW - 1, 1), b"NPC",
            rf"3Ddata\Event\{KEEPER_CON}".encode()]


def dialog(ours, dry):
    e = oro.Stb(P(ours, EVENT_STB_REL))
    erow = event_row(e)
    if e.d[EVENT_ROW] != erow:
        if any(x.strip() for x in e.d[EVENT_ROW]):
            raise SystemExit(f"our LIST_EVENT row {EVENT_ROW} is {e.get(EVENT_ROW, 0)!r}")
        e.d[EVENT_ROW] = erow
        e.save(dry, keep_backup=False)
        print(f"    {'LIST_EVENT.STB':26s} row {EVENT_ROW} -> {KEEPER_CON}")
    if not os.path.isfile(QUEST_EDITOR):
        raise SystemExit(f"{QUEST_EDITOR} missing: build it (cargo build --release -p quest-editor)")
    cmd = [QUEST_EDITOR, "con-toll", ours, KEEPER_CON, TOLL_KEY, START_TRIGGER,
           "--open-when", str(FREE_VALUE)]
    for k, v in DIALOG.items():
        cmd += [k, v]
    r = subprocess.run(cmd + ([] if dry else ["--write"]), capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"quest-editor con-toll failed:\n{r.stdout}\n{r.stderr}")
    for line in r.stdout.splitlines():
        if line.strip().startswith(("CREATE", "REWRITE", "UNCHANGED", "UPSERT")):
            print(f"    {'dialog':26s} {line.strip()[:110]}")


# ---------------------------------------------------------------------- placement
def ground_cm(ours, x_m, y_m):
    """Terrain height under a world point, bilinear over the walk grid's vertices
    (2.5 m apart, row 0 = south)."""
    nav = lnav.parse(open(find_ci(ours, MAP_REL + "\\" + LNAV_NAME), "rb").read())
    sx, sy = int(x_m // 160), int(y_m // 160)
    h = nav.tiles[(sx, sy)].heights
    fx, fy = (x_m - sx * 160) / 2.5, (y_m - sy * 160) / 2.5
    ix, iy = int(fx), int(fy)
    tx, ty = fx - ix, fy - iy
    top = h[iy + 1][ix] * (1 - tx) + h[iy + 1][ix + 1] * tx
    bot = h[iy][ix] * (1 - tx) + h[iy][ix + 1] * tx
    return float(bot * (1 - ty) + top * ty)


def f32(*vals):
    """As the file stores them, so a record read back compares equal."""
    return tuple(struct.unpack("<f", struct.pack("<f", v))[0] for v in vals)


def keeper_record(ours):
    x_cm, y_cm = POST[0] * 100, POST[1] * 100
    sx, sy = int(x_cm // 16000), int(y_cm // 16000)
    if f"{sx}_{64 - sy}.IFO" != IFO_NAME:
        raise SystemExit(f"the post is in chunk {sx}_{64 - sy}, not {IFO_NAME}")
    return ifo.Mob(name=b"", warp_id=0, event_id=0, obj_type=3, obj_id=KEEPER,
                   map_x=int((x_cm - sx * 16000) // 250), map_y=63 - int((y_cm - sy * 16000) // 250),
                   rot=f32(*KEEPER_ROT), pos=f32(x_cm - 520000, y_cm - 520000, ground_cm(ours, *POST)),
                   scale=(1.0, 1.0, 1.0), ai=0, con_file=KEEPER_CON.encode("latin-1"))


def placement(ours, dry):
    path = find_ci(ours, MAP_REL + "\\" + IFO_NAME)
    raw = open(path, "rb").read()
    f = ifo.parse(raw)
    mobs = f.lumps(ifo.MOB)
    if len(mobs) != 1:
        raise SystemExit(f"{IFO_NAME}: {len(mobs)} MOB lumps")
    want = keeper_record(ours)
    mobs[0][:] = [m for m in mobs[0] if m.obj_id != KEEPER] + [want]
    out = ifo.build(f)
    changed = write_if_changed(path, out, dry)
    print(f"    {IFO_NAME:26s} {KEEPER_NAME} at {POST} m, z {want.pos[2] / 100:.1f} m "
          + ("written" if changed else "in place"))


# ---------------------------------------------------------------------- verify
def verify(ours):
    bad = []

    def check(ok, what):
        print(f"    {'ok  ' if ok else 'FAIL'} {what}")
        if not ok:
            bad.append(what)

    n = oro.Stb(P(ours, NPC_STB_REL))
    stl = oro.Stl(P(ours, NPC_STL_REL))
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    for row_no, (cells, name, strid, chr_build) in sorted(rows_wanted(n).items()):
        check(n.d[row_no] == cells, f"LIST_NPC {row_no} {name}")
        check(stl.has(strid), f"LIST_NPC_S {strid}")
        check(chr_.chars[row_no] == chr_build(chr_), f"LIST_NPC.CHR {row_no}")
    sk = oro.Stb(P(ours, SKILL_STB_REL))
    skl = oro.Stl(P(ours, SKILL_STL_REL))
    for row, src, name, cells, strid, _desc in SKILLS:
        check(sk.rows > row and sk.d[row] == skill_row(sk, src, name, row, cells, strid),
              f"LIST_SKILL {row} {name}")
        check(skl.has(strid), f"LIST_SKILL_S {strid}")
    for rel, blob in build_clips(ours, chr_).items():
        p = P(ours, rel)
        check(os.path.isfile(p) and open(p, "rb").read() == blob, f"{os.path.basename(rel)} matches the builder")
    fai = oro.Stb(P(ours, AI_STB_REL))
    for row_no, fname, build in AI_FILES:
        check(fai.get(row_no, 0) == rf"3DDATA\AI\{fname}".encode(), f"FILE_AI {row_no} = {fname}")
        p = os.path.join(P(ours, AI_DIR_REL), fname)
        check(os.path.isfile(p) and open(p, "rb").read() == build(), f"{fname} matches the builder")
    blob = open(P(ours, QSD_REL), "rb").read()
    for name, c, r in triggers():
        check(qsd.trigger_bytes(blob, name) == qsd.build_trigger(name, c, r), f"QP401 {name}")
    qs = oro.Stb(P(ours, QUEST_STB_REL))
    check(qs.rows > QUEST and qs.d[QUEST] == quest_row(qs), f"LIST_QUEST {QUEST}")
    check(oro.Stl(P(ours, QUEST_STL_REL), encoding="utf-8").has(QUEST_STRID),
          f"LIST_QUEST_S {QUEST_STRID}")
    e = oro.Stb(P(ours, EVENT_STB_REL))
    check(e.d[EVENT_ROW] == event_row(e), f"LIST_EVENT {EVENT_ROW}")
    check(os.path.isfile(find_ci(ours, r"3DDATA\EVENT" + "\\" + KEEPER_CON)), KEEPER_CON)
    f = ifo.parse(open(find_ci(ours, MAP_REL + "\\" + IFO_NAME), "rb").read())
    placed = [m for m in f.lumps(ifo.MOB)[0] if m.obj_id == KEEPER]
    check(placed == [keeper_record(ours)], f"{IFO_NAME} places {KEEPER} once")
    leftovers = []
    for rel in TRACKED + [r"3DDATA\EVENT\x"]:
        folder = os.path.dirname(find_ci(ours, rel))
        leftovers += [f for f in os.listdir(folder) if f.lower().endswith(".bak")]
    check(not leftovers, "no .bak left beside the files" + (f": {leftovers[:3]}" if leftovers else ""))
    print(f"{'OK' if not bad else f'{len(bad)} problem(s)'}")
    return 0 if not bad else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ours", default=os.path.join(ROOT, "data"))
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    args = ap.parse_args()
    if args.verify:
        return verify(args.ours)
    if not args.dry_run:
        keep_originals(args.ours)
    print("the bosses' attacks and clips")
    skills(args.ours, args.dry_run)
    clips(args.ours, args.dry_run)
    print("the AI")
    ai_files(args.ours, args.dry_run)
    print("the rows")
    npc_rows(args.ours, args.dry_run)
    print("the quest and its triggers")
    quest(args.ours, args.dry_run)
    trigger(args.ours, args.dry_run)
    print("the dialog")
    dialog(args.ours, args.dry_run)
    print("the keeper at the tree")
    placement(args.ours, args.dry_run)
    if not args.dry_run:
        sweep_baks(args.ours)
    print("dry run, nothing written" if args.dry_run else "done -- run --verify")
    return 0


if __name__ == "__main__":
    sys.exit(main())
