#!/usr/bin/env python3
"""Import tsuki's Cerberus Lair as zone 49, Luna's last boss behind a draw.

The feature is in doc/cerberus-lair-brief.md: every hour a gatekeeper at the Arumic
temple in Arumic Valley (LP02) opens registration for ten minutes, the server draws
up to five registrants, parties them and sends them into the lair, where Cerberus
sleeps; its death (or a time limit) sends everyone back. This script is the data
half. The C++ controller lives in the game server.

Staged like scripts/import-ulverick.py; every stage is idempotent, --dry-run
previews, --verify re-derives the state, --selftest proves every writer
round-trips byte-identically before anything is touched:

    --stage 1   the map folder (tsuki's MAPS\\LUNAR\\CERBERUS, its spawn point
                emptied: the server spawns Cerberus; no exit gate), the zone row and
                its name, the event point at the temple door in LP02.ZON where the
                server sends everyone back.
    --stage 2   the monsters: 2683 Sleeping Cerberus and 2682 Cerberus at tsuki's
                row numbers with OUR stats, the model, and both AIs (written here,
                no dump has one).
    --stage 3   the gatekeeper at the Arumic temple door (NPC 4149, the Arumic
                Prophet's model), the `Cerberus-Register` trigger in QP401.QSD and
                her dialog (`quest-editor con-toll --open-when 1`: a "closed" answer
                while her event value is not 1). Needs bin/release/quest-editor.exe.

What tsuki has, and what it does not
------------------------------------
  * **The map imports clean.** Zone 49 in tsuki, free here (LIST_ZONE row 49 blank,
    ITEM_DROP row 49 blank). Three chunks, 30_30 to 32_30: a 480 x 160 m strip, the
    entrance and a frozen river to the west, a crater ringed by rock to the east.
    Every object it places (LIST_DECO_LP, LIST_CNST_LMT 32) is byte-identical to
    ours and every tile texture is already here, so only the map folder is new. It
    ships .MOV grids, lightmaps and a minimap.
  * **One authored spawn point** (31_30, Cerberus x1, 122 s, cap 1), emptied: the
    game server's controller (src/sho_gameserver/src/cerberus_lair.cpp) spawns the
    sleeper itself, at that point's position (CRATER, world cm, kept in step with
    the C++ constants), whenever the lair is idle and holds no monster. A regen
    point cannot be made to respawn on demand (CRegenPOINT::Reset only zeroes its
    count and still waits a full interval), and a point beside the controller
    would put a second Cerberus in the crater.
  * **There is no way out** but the server's (2026-10-08, after the first test):
    tsuki's exit was a dead event object `cerberus_warp` (its trigger is in no
    tsuki QSD), removed. The first import put a real gate there (WARP row 105);
    it is gone, and stage 1 blanks the row it wrote (RETIRED_WARP). The game
    server also turns a save-point revive into the lair's own and refuses warp
    scrolls in the lair (CCerberusLair::IsLair).
  * **The monster rows.** tsuki 2682 is Cerberus (level 130, tsuki's scale, AI row
    1263 which does not exist anywhere, death trigger `Capture_Cerberus` from
    tsuki's mount system, dead). 2683 is the same model on CERBERUS_SLEEPING.ZMO,
    type 999, placed nowhere. Both rows are blank here. Columns 0-42 align with
    ours; every stat is ours (MONSTERS). The sound and effect columns (301/304/
    454/1401/322) are our own Thorn Hound's, the one-headed Runa_wolf4, so they
    carry over.
  * **The model** uses our wolf skeleton (WOLF3\\RUNA_WOLF3_BONE.ZMD) and our
    runa_wolf1 motions, byte-identical; import_characters dedupes them by content
    path and copies only NPC\\ANIMAL\\CERBERUS (4 meshes, 3 textures) and the
    sleeping clip. QQ-iROSE ships the same files.
  * **No AI anywhere.** QQ-iROSE's CERBERUS.AIP is our CLAN_BOSS6.AIP (the Astarot
    King) with two ids changed. Both AIs are written here (build_sleep_ai,
    build_awake_ai).

Waking up
---------
The sleeper (2683) cannot move (speeds 0) and has two rules: an enemy within
WAKE_RANGE m in its idle pattern, or any hit in its damaged pattern (header rate
100%), shouts and turns it into 2682 (AIACT09 -> CObjMOB::Change_CHAR, which
broadcasts GSV_CHANGE_NPC and swaps model, HP and AI: the AI is looked up from
the current row every tick, cobjnpc.h). Change_CHAR does not run the created
pattern, so the awake AI's idle pattern picks the fight up.

The fight
---------
Melee, plus **Infernal Leap** (LIST_SKILL 7021, a copy of 3069 -- the 5.5 m area
hit Hook 516/517 cast -- with fire on it: FILE_EFFECT 1882 thornie_firecharge as it
crouches, 1883 thornie_fireblast where it lands, the Terrasaurus King's effects).
Cast on slot 8, released on slot 9 (`runa_wolf1_action_skill01`, hit frame 25),
from the damaged pattern every BITE_EVERY hits taken while the target is within
BITE_REACH, as Hebarn's stomp. A leash (LEASH m) sends it home. Every cast is gated
on the target being in range already (Hebarn's lesson: a cast ordered from afar
makes the server chase first and the client abandons the cast after 5 s).

**Hellhounds** (2026-10-08, "make it cooler"): at 66% and again at 33% HP two
Hellhounds (LIST_NPC 2684, the Wolf King's runa_wolf3 model, level 150, no loot,
HELLHOUND.AIP) appear in the crater with a zone shout, and run to the fight. The
game server spawns them (cerberus_lair.cpp), not Cerberus's AI: the first cut summoned
them around Cerberus (AIACT20) and the client put them on the rock arches above the
fight -- a new monster stands on the highest surface at its spot (CObjMOB::Create ->
GetHeightTop) and the server sends no height. Heard, never seen (2026-10-09). The
crater is open sky. The controller's reset kills them with everything else. **The glow** (col 39, RRRGGGBBB) is a fiery red on both forms of
Cerberus and on the hounds.

**Hellfire Breath** (LIST_SKILL 7022, 2026-10-09): Fireball 3604's row and effect
chain (projectile 476, thornie_firecharge / fireblast, fire hit) with a 15 m range, a
5 m blast, fired from p_mouth (dummy 7, added to Cerberus's copy of the skeleton --
see MOUTH; the first cut used Fireball's point 5, p_06, which sits in the spiked
collar, and the bolt left from mid-body). Cast on slot 6, released on slot 7, `runa_wolf1_status_skill01`,
whose launch frames 24 and 34 fire two fireballs a cast (multi-hit is
animation-driven). Burns: Flame Heat (status 58, the Mukuroji's), 70%, 10 s. Every BREATH_EVERY hits taken (var 4) at any target within 14 m -- melee included:
from p_mouth, ~7 m ahead of and ~7.5 m above Cerberus's centre at size 800, the bolt
crosses ~10 m down to a fighter standing under the heads. (The first cut fired from the
collar, right above the body, so a melee target was hit instantly; for one build the
breath was range-only, which lost the flavour.) Also while chasing: 30% of checks at a
target 6-14 m away, so a kiter is breathed on rather than run down.
Power 200 is a first guess, to tune after a fight.

Not taken (offered 2026-10-08): a howl (area slow) and an enrage.

Stats
-----
Level 155 (Luna's open fields top out at the Behemoth King, 142). Our spawned
monsters' medians at 147-155 (import-ulverick.py: HP column 38, ATK 718, HIT
447, DEF 501, RES 430, AVOID 239) scaled for a five-player boss: HP x20 (column
800, 124k), ATK x1.4, HIT/AVOID x1.1, DEF/RES x1.2. Seeded, to be tuned in game.
EXP is only seeded too: rebalance-exp-rewards.py owns the column. Type 16, the
skull (unique boss, fix-monster-marks.py's policy). Both ids are in
balance-trend-exclude.py (level 155 sits in the 60-199 window every balance pass
fits).

Loot (stage 2 writes the death drops, stage 4 the table; 2026-10-08)
---------------------------------------------------------------------
The brief: "uniques, wings, back shields, the best that can be offered at level
150-170, strong gems". Five-ish items a kill for up to five players, built from items
that have **no source anywhere** until now (the survey checked every ITEM_DROP cell and
every shop tab):
  * every kill, from the dead pattern (AIACT17 picks one of five; an AI drop packs
    type*1000 + no into a short, so only ids under 1000): one "unique" weapon -- the
    13 named lv155 weapons (LIST_WEAPON prefix 1-8, one per weapon type) plus the two
    strongest retail violet uniques, Icicle and Falchion-Firangi (the rest of rows
    901-993 are lv100-140 grade and already drop elsewhere) -- in three groups at 1/3
    each; one lv145-155 wing or back shield; one gem [5] and one gem [6] (no drop
    anywhere goes above [3]).
  * one table roll (ITEM_DROP row 49 = the zone's own row; col 20 = 100, money 0, all
    30 slots filled), simulated at parity: a lv160-170 weapon 45% (15 weapons, the
    retail tier above lv155 that nothing dropped), a gem [6]/[7] 24%, lv165 armour
    15%, a lv150-170 wing 14% (Holy Coffin, Sandalphon, Deneb Blade, Moonlit Magic
    Circle, Skyborn Preceptor).
Left out: Jrose's Dragon Bowgun / Serpent Rod / Standard Rod (1449/1452/1453: one
placeholder model for all three and ATK above their tier), and anything above level 170.
A player 10+ levels above Cerberus (165+) gets nothing from the table
(CCal::Get_DropITEM); the dead-pattern drops still fall.

After running: audit-lightmap-index.py --zone 49, audit-ai-skill-refs.py,
add-dds-mipmaps.py --dry-run over NPC/ANIMAL/CERBERUS, rebake the VFS, restart the
servers (LIST_ZONE, LIST_NPC, LIST_NPC.CHR, FILE_AI, AI_s.STB, WARP, the IFOs and
LP02.ZON are server data). Backups: build/cerberus/.
"""
import argparse
import collections
import copy
import importlib.util
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

# --------------------------------------------------------------------- config
DEFAULT_SRC = r"C:\Users\Thomas\Desktop\Testclients\tsuki"
BUILD_DIR = os.path.join(ROOT, "build", "cerberus")

ZONE = 49
ZONE_NAME = "Cerberus Lair"
ZONE_STL_KEY = "LZON049"
ZONE_STB_REL = r"3DDATA\STB\LIST_ZONE.STB"
ZONE_STL_REL = r"3DDATA\STB\LIST_ZONE_S.STL"
ZONE_NAME_COL, ZONE_JOIN_COL, ZONE_STL_COL, ZONE_WEATHER_COL = 0, 22, 26, 27
ZONE_WEATHER = b"1"                 # Arumic Valley's own (LP02, row 53)
MAPS_REL = r"3DDATA\MAPS\LUNAR\CERBERUS"
ZON_NAME = "CERBERUS.ZON"
LAIR_FOLDER = MAPS_REL.upper().split("MAPS\\", 1)[1]     # as warp_users names it

LP02_REL = r"3DDATA\MAPS\LUNAR\LP02"
LP02_ZON = "LP02.ZON"
LP02_ZONE = 53
# The temple door: DECO 37 (arumic01-03) stands at (6261.4, 4819.1) m facing
# north-west onto the square of four crystal pillars (DECO 36, around (6210, 4876)).
TEMPLE_DOOR = (6240.0, 4842.0)      # world metres, in front of the door
LANDING = "WARP-CERBERUS-LP02"

WARP_STB_REL = r"3DDATA\STB\WARP.STB"
# the first import's exit gate, removed: stage 1 blanks the row if it still holds ours
RETIRED_WARP = 105
RETIRED_WARP_LABEL = b"CERBERUS -> LP02 (Cerberus Lair -> Arumic Valley)"
EXIT_IFO = "30_30.IFO"              # where tsuki's dead `cerberus_warp` box stood

SPAWN_IFO = "31_30.IFO"
CRATER = (510801, 554720)           # tsuki's spawn point, world cm = CRATER_X/Y in the C++

NPC_STB_REL = r"3DDATA\STB\LIST_NPC.STB"
NPC_STL_REL = r"3DDATA\STB\LIST_NPC_S.STL"
NPC_CHR_REL = r"3DDATA\NPC\LIST_NPC.CHR"
AI_STB_REL = r"3DDATA\STB\FILE_AI.STB"
AI_STR_REL = r"3DDATA\AI\AI_s.STB"
AI_DIR_REL = r"3DDATA\AI"

AWAKE, ASLEEP = 2682, 2683
# columns (game numbering)
C_NAME, C_WALK, C_RUN, C_SIZE, C_LEVEL, C_HP, C_ATK, C_HIT, C_DEF, C_RES, C_AVOID, \
    C_ASPD, C_AI, C_EXP, C_DROP, C_MONEY, C_DROPRATE = \
    0, 2, 3, 4, 7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 18, 19, 20
C_TARGET, C_TYPE, C_DEADEVENT = 25, 27, 41
C_SIZE, C_RANGE, C_GLOW = 4, 26, 39
# The hellfire look: LIST_NPC col 39 is a glow around the whole model, decimal
# RRRGGGBBB (CGameUtil::GetRGBFromString, client CObjMOB::Create), as on the
# Behemoth King (80050050).
GLOW = "220070020"
HOUND_GLOW = "200050010"
TYPE_SKULL = 16
# (name, STL key, stats)
MONSTERS = {
    AWAKE: ("Cerberus", "LCRBMOB2682", {
        C_LEVEL: 155, C_HP: 800, C_ATK: 1005, C_HIT: 492, C_DEF: 601, C_RES: 516,
        C_AVOID: 263, C_ASPD: 110, C_EXP: 16000, C_AI: 2254,
        C_DROP: 49, C_MONEY: 0, C_DROPRATE: 100, C_GLOW: GLOW}),
    ASLEEP: ("Sleeping Cerberus", "LCRBMOB2683", {
        C_WALK: 0, C_RUN: 0,
        C_LEVEL: 155, C_HP: 800, C_ATK: 1005, C_HIT: 492, C_DEF: 601, C_RES: 516,
        C_AVOID: 263, C_ASPD: 110, C_EXP: 0, C_AI: 2253,
        C_DROP: 0, C_MONEY: 0, C_DROPRATE: 0, C_GLOW: GLOW}),
}
AI_FILES = {2253: "CERBERUS_SLEEP.AIP", 2254: "CERBERUS.AIP", 2255: "HELLHOUND.AIP"}
LINES = {   # AI_s.STB, appended; AIACT28 type 1 = zone shout, 0 = local chat
    "wake": "Three heads lift from the ice. Cerberus wakes!",
    "kill": "Cerberus tears its prey apart.",
}

# ---- the hellhounds: the game server calls two at 66% of Cerberus's HP and two more at
# 33% (cerberus_lair.cpp HOUND_CALL_PCT), at the crater -- not the AI: see "The fight"
HOUND = 2684                        # blank here, no CHR entry
HOUND_NAME, HOUND_STRID = "Hellhound", "LCRBMOB2684"
HOUND_SEEK, HOUND_LEASH = 55, 70        # m: the whole arena from the crater
HOUND_TEMPLATE = 1428               # Wolf King: runa_wolf3 model, the Luna wolf family
# level 150 field monster, the medians import-ulverick.py uses, a little faster
HOUND_STATS = {C_LEVEL: 150, C_HP: 45, C_ATK: 700, C_HIT: 450, C_DEF: 480, C_RES: 420,
               C_AVOID: 240, C_ASPD: 115, C_WALK: 250, C_RUN: 800, C_SIZE: 220,
               C_EXP: 600, C_AI: 2255, C_DROP: 0, C_MONEY: 0, C_DROPRATE: 0,
               C_TYPE: 3, C_GLOW: HOUND_GLOW}

# ---- Infernal Leap: the jump with fire on it (Hook 516/517 keep 3069 as it was)
LEAP = 7021                         # one past the end of LIST_SKILL
LEAP_NAME = "Infernal Leap"
LEAP_SRC = 3069
LEAP_CAST_FX, LEAP_HIT_FX = 1882, 1883  # FILE_EFFECT: thornie_firecharge / _fireblast
SKILL_STB_REL = r"3DDATA\STB\LIST_SKILL.STB"
SK_NAME, SK_LINE, SK_CAST_FX, SK_CAST_LOC, SK_HIT_FX, SK_HIT_LOC = 0, 1, 56, 57, 74, 75
SK_RANGE, SK_RADIUS, SK_POWER, SK_STATUS, SK_SUCCESS, SK_DURATION = 6, 8, 9, 11, 13, 14
SK_CAST_MOTION, SK_SKILL_MOTION, SK_FIRE_LOC = 52, 68, 72

# ---- Hellfire Breath: two fireballs from the snout (2026-10-09, "make it feel like a boss")
BREATH = 7022
BREATH_NAME = "Hellfire Breath"
BREATH_SRC = 3604                   # Fireball (the Terrasaurus King's): projectile 476,
                                    # firecharge / fireblast / fire hit, its sounds
BREATH_MOTION = 6                   # cast on slot 6 (casting), released on slot 7:
                                    # runa_wolf1_status_skill01, launch frames 24 and 34
BREATH_CELLS = {SK_RANGE: 1500, SK_RADIUS: 500, SK_POWER: 200,
                SK_CAST_LOC: 7, SK_FIRE_LOC: 7,                   # p_mouth (MOUTH below)
                SK_STATUS: 58, SK_SUCCESS: 70, SK_DURATION: 10,   # Flame Heat, the
                SK_CAST_MOTION: BREATH_MOTION,                    # Mukuroji's burn
                SK_SKILL_MOTION: BREATH_MOTION + 1}
BREATH_CHASE = (6, 14)              # m: while chasing, it breathes on you instead
BREATH_CHASE_CHANCE = 30
BREATH_EVERY = 8                    # hits taken (monster var 4)
BREATH_REACH = 14                   # m
WAKE_RANGE = 14                     # m
BITE = LEAP                         # 5.5 m area hit with fire on it
BITE_MOTION = 8
BITE_REACH = 5                      # m
BITE_EVERY = 12                     # hits taken between two
LEASH = 45                          # m from the spawn point
SEEK = 30                           # m

# ---- the loot (stage 2 writes the death drops, stage 4 the table); docstring "Loot"
T_CAP, T_BODY, T_ARMS, T_FOOT, T_BACK, T_WEAPON, T_GEM = 2, 3, 4, 5, 6, 8, 11
# One "unique" weapon a kill: the 13 named lv155 weapons (LIST_WEAPON prefix 1-8, one
# per weapon type, no source anywhere) and the two strongest retail violet uniques
# (prefix 21: Icicle, Falchion-Firangi), also without a source. Three groups of five,
# one group per kill at 1/3 each (the dead pattern's events are first-match-wins).
UNIQUES = [
    [521, 551, 572, 593, 626],        # Death Expeller, Final Hammer, Tribal Crossbow,
                                      # Explosion Hander, Behead Axe
    [659, 685, 717, 742, 781],        # Punisher Drill, Six Stick Bow, White Night Gun,
                                      # Reverse Cannon, Sparkle Staff
    [805, 841, 865, 978, 992],        # Ouroboros Wand, Raven Knuckle, Living Dead,
                                      # Icicle, Falchion-Firangi
]
# One back item a kill: the lv145-155 wings and back shields nothing drops (ids under
# 1000: an AI drop packs type*1000 + no into a short).
BACK_DROP = [831, 870, 892, 893, 996]  # Divine Cross / Tribal / Tribe Backshield,
                                       # Hook Wing, Skyborn Wings
# Two strong gems a kill: a grade [5] and a grade [6] (no drop anywhere goes above [3]).
GEM5_DROP = [305, 315, 325, 335, 345]  # Garnet, Ruby, Sapphire, Topaz, Emerald [5]
GEM6_DROP = [306, 316, 326, 366, 376]  # Garnet, Ruby, Sapphire, Diamond, Pink Opal [6]
DROP_TABLE = 49                        # = the zone: the 20% zone roll lands here too
DROP_LABEL = "BOSS Cerberus (lv155)"

# ---- stage 3: the gatekeeper
GATEKEEPER = 4149                   # appended: no free row in Luna's NPC range
GATEKEEPER_NAME = "[Arumic Seal Keeper] Yelena"
GATEKEEPER_STRID = "LCRBNPC4149"
GATEKEEPER_TEMPLATE = 1173          # [Arumic Prophet] Olleck Basilasi, Eucar: row + model
TEMPLATE_MAP_REL = r"3DDATA\MAPS\LUNAR\LMT01"
# just inside the landing point, towards the door; facing out like the temple
GATEKEEPER_POS = (6244.0, 4838.0)
GATEKEEPER_ROT = (0.0, 0.0, 0.924, -0.383)      # DECO 37's own quaternion (x, y, z, w)
EVENT_STB_REL = r"3DDATA\STB\LIST_EVENT.STB"
EVENT_ROW = 140
GATEKEEPER_CON = "EM53-001.con"
TOLL_KEY = "crb"
QSD_REL = r"3DDATA\QUESTDATA\QP401.QSD"
QSD_PATTERN, REGISTER_TRIGGER = "Cerberus", "Cerberus-Register"
OPEN_VALUE = 1                      # the gatekeeper's event value while registration is open
MIN_LEVEL = 140
FEE = 100000                        # zuly, taken at the draw from the players drawn
AT_LEVEL, AT_MONEY = 31, 40
OP_EQ, OP_GE = 0, 2
QUEST_EDITOR = os.path.join(ROOT, "bin", "release", "quest-editor.exe")
DIALOG = {
    "--greet": "Under this temple the Arumic priests chained a beast with three heads. Every "
               "hour, on the hour, the seal thins for ten minutes, and I can send a few souls "
               "down to face it.",
    "--ask": "I want to face Cerberus.",
    "--bye": "Farewell.",
    "--offer": "The seal is thin now. Give me your name: at ten past the hour, up to five of "
               "those who signed are sent below, wherever they stand. Those chosen pay "
               "100,000 zuly; the others pay nothing.",
    "--lack": "Only those of level 140 or more, carrying 100,000 zuly, may sign. Come back "
              "when you are ready.",
    "--accept": "Sign me up.",
    "--decline": "Not now.",
    "--later": "I will come back.",
    "--closed": "The seal holds fast. It thins every hour, on the hour, for ten minutes. "
                "Come back then.",
}

BAK_TRACKED = [ZONE_STB_REL, ZONE_STL_REL, WARP_STB_REL, NPC_STB_REL, NPC_STL_REL,
               NPC_CHR_REL, r"3DDATA\NPC\PART_NPC.ZSC", AI_STB_REL, AI_STR_REL,
               EVENT_STB_REL, QSD_REL, r"3DDATA\EVENT\ulngtb_con.ltb",
               r"3DDATA\STB\ITEM_DROP.STB", SKILL_STB_REL]

CHUNK_CM, GRID_CM, ORIGIN_CM = 16000, 250, 520000
POS_OFF = 2 + 2 + 4 + 4 + 4 + 4 + 16       # warp, event, type, id, map_x, map_y, quat
MAP_XY_OFF = 2 + 2 + 4 + 4


# -------------------------------------------------------------------- helpers
def load(name, fname):
    """Import a sibling script as a module without running its main()."""
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


oro = load("import_oro", "import-oro.py")             # STB/STL/CHR/IFO codecs
mon = load("audit_ai_monster_refs", "audit-ai-monster-refs.py")   # .aip codec
heb = load("import_hebarn", "import-hebarn.py")       # AI condition/action encoders
fate = load("import_oro_fate", "import-oro-fate.py")  # QSD codec
from mapgen import ifo, lit, objlight, prefab, zon  # noqa: E402


def P(root, rel):
    return os.path.join(root, rel.replace("\\", "/"))


def ifo_files(folder):
    return sorted(f for f in os.listdir(folder) if f.lower().endswith(".ifo"))


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
    for rel in BAK_TRACKED:
        bak = P(ours, rel) + ".bak"
        if not os.path.isfile(bak):
            continue
        dest = os.path.join(BUILD_DIR, "bak", rel.replace("\\", "/") + ".bak")
        os.makedirs(os.path.dirname(dest), exist_ok=True)
        if os.path.isfile(dest):
            os.remove(bak)             # keep the oldest pre-import copy
        else:
            shutil.move(bak, dest)
        moved += 1
    if moved:
        print(f"moved {moved} .bak file(s) to {os.path.join(BUILD_DIR, 'bak')}")


def fixed_with(fixed, warp_id=None, map_xy=None, pos=None):
    """A 60-byte object header with some fields replaced:
    i16 warp, i16 event, i32 type, i32 id, i32 map_x, i32 map_y, f32 quat[4], f32 pos[3], f32 scale[3]."""
    b = bytearray(fixed)
    if warp_id is not None:
        struct.pack_into("<hh", b, 0, warp_id, 0)
    if map_xy is not None:
        struct.pack_into("<ii", b, MAP_XY_OFF, *map_xy)
    if pos is not None:
        struct.pack_into("<3f", b, POS_OFF, *pos)
    return bytes(b)


def chunk_of_world(wx_m, wy_m):
    """(ifo name, zone-centre pos cm (z 0), (map_x, map_y)) for a world position in m."""
    wx, wy = wx_m * 100, wy_m * 100
    cx, cy = int(wx // CHUNK_CM), int(wy // CHUNK_CM)
    lx, ly = wx - cx * CHUNK_CM, wy - cy * CHUNK_CM
    return (f"{cx}_{64 - cy}.IFO", (wx - ORIGIN_CM, wy - ORIGIN_CM, 0.0),
            (int(lx // GRID_CM), 63 - int(ly // GRID_CM)))


def ground(folder, wx_m, wy_m):
    field, (x0, y0) = prefab._zone_field(folder)
    z = prefab._height(field, x0, y0, wx_m * 100, wy_m * 100)
    if z != z:
        raise SystemExit(f"({wx_m}, {wy_m}) m is outside {folder}")
    return z


# ------------------------------------------------------------------- stage 1
def lair_ifo(ours, src_map, name):
    """A tsuki lair .IFO as we ship it."""
    buf, bounds = oro.read_ifo(os.path.join(src_map, name))
    repl = {}
    if name.lower() == EXIT_IFO.lower():
        objs, trailing = oro.read_lump(buf, bounds, oro.LUMP_EVENT_OBJECT)
        if objs is None or len(objs) != 1 or b"cerberus_warp" not in objs[0]["extra"]:
            raise SystemExit(f"{name}: expected tsuki's one cerberus_warp event object")
        repl[oro.LUMP_EVENT_OBJECT] = oro.build_object_lump([], trailing)
    if name.lower() == SPAWN_IFO.lower():
        objs, trailing = oro.read_lump(buf, bounds, oro.LUMP_REGEN)
        if not objs or len(objs) != 1:
            raise SystemExit(f"{name}: expected tsuki's one spawn point")
        x, y, _ = struct.unpack_from("<3f", objs[0]["fixed"], POS_OFF)
        if (round(x + ORIGIN_CM), round(y + ORIGIN_CM)) != CRATER:
            raise SystemExit(f"{name}: tsuki's spawn point moved; update CRATER and the C++")
        repl[oro.LUMP_REGEN] = oro.build_object_lump([], trailing)
    for lt in (oro.LUMP_MOB, oro.LUMP_REGEN, oro.LUMP_WARP, oro.LUMP_EVENT_OBJECT):
        if lt in repl:
            continue
        objs = oro.read_lump(buf, bounds, lt)[0]
        if objs:
            raise SystemExit(f"{name}: unexpected records in lump {lt}")
    return oro.build_ifo(bounds, buf, repl) if repl else buf


def zone_row(s_z, d_z):
    row = [s_z.get(ZONE, c) for c in range(d_z.cols)]
    row[ZONE_NAME_COL] = ZONE_NAME.encode()
    row[ZONE_JOIN_COL] = b""
    row[ZONE_STL_COL] = ZONE_STL_KEY.encode()
    row[ZONE_WEATHER_COL] = ZONE_WEATHER
    return row


def warp_users(ours):
    """{warp row: [zone folders whose gates name it]} over every map we ship."""
    out = {}
    maps = P(ours, r"3DDATA\MAPS")
    for base, _, names in os.walk(maps):
        for n in names:
            if not n.lower().endswith(".ifo"):
                continue
            try:
                buf, bounds = oro.read_ifo(os.path.join(base, n))
                objs = oro.read_lump(buf, bounds, oro.LUMP_WARP)[0] or []
            except Exception:
                continue
            for o in objs:
                w, = struct.unpack_from("<h", o["fixed"], 0)
                out.setdefault(w, []).append(os.path.relpath(base, maps))
    return out


def f32(v):
    return struct.unpack("<f", struct.pack("<f", v))[0]


def landing_event(ours):
    """The event point as the .ZON stores it (float32), so a re-run compares equal."""
    d = P(ours, LP02_REL)
    z = ground(d, *TEMPLE_DOOR)
    return zon.EventPos(x=f32((TEMPLE_DOOR[0] - 5200) * 100), z=f32(z),
                        y=f32((TEMPLE_DOOR[1] - 5200) * 100), name=LANDING.encode())


LIT_LUMPS = {"objectlightmapdata.lit": ifo.OBJECT, "buildinglightmapdata.lit": ifo.CNST}


def lair_lit(src_map, chunk_name, lit_name):
    """A lair .lit without the entries of records the client drops.

    tsuki's 30_30 carries one: OBJECT record 1, a tunnelstone04 3.2 m past the
    chunk's east edge at height 0 (the ground is ~20 m there), which
    CMAP::AddObject refuses, so tsuki never showed it either. Its entry then
    lights the next record, which has one part where the entry names two
    (audit-lightmap-index.py: CRASH). The record stays -- deleting it would shift
    every later ordinal -- and only its entry goes."""
    raw = open(os.path.join(find_ci(os.path.join(src_map, chunk_name), "LIGHTMAP"), lit_name), "rb").read()
    lt = LIT_LUMPS[lit_name.lower()]
    stem = chunk_name.split("_")
    slot = (int(stem[0]), 64 - int(stem[1]))
    recs = ifo.parse(open(find_ci(src_map, chunk_name + ".IFO"), "rb").read()).lump(lt) or []
    L = lit.parse_lit(raw)
    keep = [o for o in L.objects
            if 1 <= o.obj_index <= len(recs) and objlight.client_accepts(recs[o.obj_index - 1].pos, slot)]
    if len(keep) == len(L.objects):
        return raw, 0
    dropped = len(L.objects) - len(keep)
    L.objects = keep
    return lit.build_lit(L), dropped


def stage1(ours, src, dry):
    print("stage 1 -- map, zone row, way out")
    s_map, d_map = P(src, MAPS_REL), P(ours, MAPS_REL)
    copied = rewritten = 0
    for base, _, names in os.walk(s_map):
        sub = os.path.relpath(base, s_map)
        dbase = d_map if sub == "." else os.path.join(d_map, sub.upper())
        for name in sorted(names):
            sp, dp = os.path.join(base, name), os.path.join(dbase, name.upper())
            if name.lower().endswith(".ifo"):
                blob = lair_ifo(ours, s_map, name)
                rewritten += write_if_changed(dp, blob, dry)
                continue
            if name.lower() in LIT_LUMPS:
                blob, n = lair_lit(s_map, os.path.basename(os.path.dirname(base)), name)
                if write_if_changed(dp, blob, dry) and n:
                    print(f"    {sub + '/' + name:26s} -{n} entry for a record the client drops")
                continue
            if name.lower().endswith(".txt"):
                continue                # tsuki's editor dump of the .ZON
            if os.path.isfile(dp):
                continue
            if not dry:
                os.makedirs(dbase, exist_ok=True)
                shutil.copyfile(sp, dp)
            copied += 1
    print(f"    {'CERBERUS':26s} {copied:5d} files copied, {rewritten} .IFO written")

    s_z, d_z = oro.Stb(P(src, ZONE_STB_REL)), oro.Stb(P(ours, ZONE_STB_REL))
    if s_z.get(ZONE, 0) != b"Cerberus Lair":
        raise SystemExit(f"tsuki LIST_ZONE row {ZONE} is {s_z.get(ZONE, 0)!r}")
    cur = d_z.get(ZONE, ZONE_NAME_COL).decode("latin-1").strip()
    if cur and cur != ZONE_NAME:
        raise SystemExit(f"our LIST_ZONE row {ZONE} is {cur!r}")
    want = zone_row(s_z, d_z)
    if d_z.d[ZONE] != want:
        d_z.d[ZONE] = want
        d_z.save(dry)
        print(f"    {'LIST_ZONE.STB':26s} row {ZONE} written")
    else:
        print(f"    {'LIST_ZONE.STB':26s} row {ZONE} already in place")
    stl = oro.Stl(P(ours, ZONE_STL_REL))
    if not stl.has(ZONE_STL_KEY):
        stl.append(ZONE_STL_KEY, ZONE, ZONE_NAME)
        stl.save(dry)
        print(f"    {'LIST_ZONE_S.STL':26s} +1 key {ZONE_STL_KEY}")

    w = oro.Stb(P(ours, WARP_STB_REL))
    if w.get(RETIRED_WARP, 0) == RETIRED_WARP_LABEL:
        users = [u for u in warp_users(ours).get(RETIRED_WARP, []) if u.upper() != LAIR_FOLDER]
        if users:
            raise SystemExit(f"WARP row {RETIRED_WARP} is named by gates in {users}")
        for c in range(w.cols):
            w.set(RETIRED_WARP, c, b"")
        w.save(dry)
        print(f"    {'WARP.STB':26s} row {RETIRED_WARP} blanked (the exit gate is gone)")

    zp = find_ci(P(ours, LP02_REL), LP02_ZON)
    z = zon.parse(open(zp, "rb").read())
    events = z.lump(zon.EVENTS)
    want_ev = landing_event(ours)
    have = [e for e in events if e.name == LANDING.encode()]
    if have and have[0] == want_ev:
        print(f"    {LP02_ZON:26s} {LANDING} already in place")
    else:
        events[:] = [e for e in events if e.name != LANDING.encode()] + [want_ev]
        write_if_changed(zp, zon.build(z), dry)
        print(f"    {LP02_ZON:26s} + {LANDING} at {TEMPLE_DOOR} m, z {want_ev.z / 100:.1f} m")


# ------------------------------------------------------------------- stage 2
def b(v):
    return str(v).encode("latin-1")


def a_change(npc):                         # AIACT09: become another LIST_NPC row
    return heb._act(9, struct.pack("<H", npc) + heb.PAD2)


def build_sleep_ai(L):
    wake = [heb.a_say(L["wake"], 1), a_change(AWAKE)]
    pats = [
        (heb.pat("created"), []),
        (heb.pat("stop"), [heb.ev("stirs", [heb.c_enemies_near(WAKE_RANGE)], wake)]),
        (heb.pat("attack move"), []),
        (heb.pat("damaged"), [heb.ev("woken", [], wake)]),
        (heb.pat("kill"), []),
        (heb.pat("dead"), []),
    ]
    # idle check every second, the damaged pattern on every hit
    return mon.build_aip((len(pats), 1, 100), b"Sleeping Cerberus\0", pats, b"")


def death_events():
    """One event fires (first match): 1/3, then 1/2 of the rest, then the rest picks
    the unique group; each event also drops a back item and two gems. AIACT17 picks
    one of its five items at random; to_owner 1 = the usual ownership."""
    def drops(group):
        return [heb.a_drop([T_WEAPON * 1000 + n for n in group]),
                heb.a_drop([T_BACK * 1000 + n for n in BACK_DROP]),
                heb.a_drop([T_GEM * 1000 + n for n in GEM5_DROP]),
                heb.a_drop([T_GEM * 1000 + n for n in GEM6_DROP])]
    return [heb.ev("loot a", [heb.c_chance(33)], drops(UNIQUES[0])),
            heb.ev("loot b", [heb.c_chance(50)], drops(UNIQUES[1])),
            heb.ev("loot c", [], drops(UNIQUES[2]))]


def build_hound_ai():
    """Spawned by the server at the crater (RegenMOB never runs the created pattern),
    so the idle search must reach the whole arena: Cerberus leashes at LEASH m."""
    pats = [
        (heb.pat("created"), [heb.ev("hunt", [heb.c_enemies_near(HOUND_SEEK)], [heb.a_attack_found()])]),
        (heb.pat("stop"), [
            heb.ev("go home", [heb.c_moved_from_spawn(HOUND_LEASH)], [heb.a_run_home(3)]),
            heb.ev("seek", [heb.c_enemies_near(HOUND_SEEK)], [heb.a_attack_found()]),
        ]),
        (heb.pat("attack move"), [
            heb.ev("leash", [heb.c_moved_from_spawn(HOUND_LEASH)], [heb.a_run_home(3)]),
        ]),
        (heb.pat("damaged"), [
            heb.ev("retaliate", [heb.c_chance(30), heb.c_attacker_not_target()],
                   [heb.a_attack_attacker()]),
        ]),
        (heb.pat("kill"), []),
        (heb.pat("dead"), []),
    ]
    return mon.build_aip((len(pats), 2, 50), b"Hellhound\0", pats, b"")


def build_awake_ai(L):
    pats = [
        (heb.pat("created"), [heb.ev("hunt", [heb.c_enemies_near(SEEK)], [heb.a_attack_found()])]),
        (heb.pat("stop"), [
            heb.ev("go home", [heb.c_moved_from_spawn(LEASH)], [heb.a_run_home(3)]),
            heb.ev("seek", [heb.c_enemies_near(SEEK)], [heb.a_attack_found()]),
        ]),
        (heb.pat("attack move"), [       # runs only while it moves towards its target
            heb.ev("leash", [heb.c_moved_from_spawn(LEASH)], [heb.a_run_home(3)]),
            heb.ev("breath", [heb.c_chance(BREATH_CHASE_CHANCE), heb.c_target_at_least(BREATH_CHASE[0]),
                              heb.c_target_within(BREATH_CHASE[1])],
                   [heb.a_cast(BREATH, BREATH_MOTION)]),
        ]),
        (heb.pat("damaged"), [           # first match wins; var 2 = bite, var 4 = breath
            heb.ev("bite", [heb.c_var(2, BITE_EVERY, 2), heb.c_target_within(BITE_REACH)],
                   [heb.a_cast(BITE, BITE_MOTION), heb.a_set_var(2, 0), heb.a_set_var(4, 1, 6)]),
            heb.ev("breath", [heb.c_var(4, BREATH_EVERY, 2), heb.c_target_within(BREATH_REACH)],
                   [heb.a_cast(BREATH, BREATH_MOTION), heb.a_set_var(4, 0), heb.a_set_var(2, 1, 6)]),
            heb.ev("retaliate", [heb.c_chance(20), heb.c_attacker_not_target()],
                   [heb.a_attack_attacker(), heb.a_set_var(2, 1, 6), heb.a_set_var(4, 1, 6)]),
            heb.ev("count", [], [heb.a_set_var(2, 1, 6), heb.a_set_var(4, 1, 6)]),
        ]),
        (heb.pat("kill"), [heb.ev("maul", [heb.c_chance(50)], [heb.a_say(L["kill"], 0)])]),
        (heb.pat("dead"), death_events()),
    ]
    # idle check every 2 s, the damaged pattern on 50% of the hits taken
    return mon.build_aip((len(pats), 2, 50), b"Cerberus\0", pats, b"")


def npc_row(s_npc, d_npc, i):
    name, strid, stats = MONSTERS[i]
    row = [s_npc.get(i, c) for c in range(oro.NPC_COPY_COLS)] + list(d_npc.d[i][oro.NPC_COPY_COLS:])
    row[C_NAME] = name.encode("latin-1")
    for c, v in stats.items():
        row[c] = b(v)
    row[C_TYPE] = b(TYPE_SKULL)
    row[C_TARGET] = b""
    row[C_DEADEVENT] = b""
    for c in oro.NPC_SELL_TAB_COLS:
        row[c] = b""
    row[oro.NPC_STRID_COL] = strid.encode()
    row[oro.NPC_PVP_COL] = oro.DEFAULT_PVP_STATE
    if d_npc.get(i, 0).strip():
        row[C_EXP] = d_npc.get(i, C_EXP)        # rebalance-exp-rewards.py owns it
    return row


def ai_lines(ours, dry):
    ais = oro.Stb(P(ours, AI_STR_REL))
    rows, added = {}, 0
    for key, text in LINES.items():
        raw = text.encode("latin-1")
        hit = [r for r in range(ais.rows) if ais.get(r, 1) == raw]
        if hit:
            rows[key] = hit[0]
            continue
        r = ais.rows
        ais.grow_to(r + 1)
        for c in range(ais.cols):
            ais.set(r, c, raw)
        rows[key] = r
        added += 1
    if added:
        ais.save(dry)
    return rows, added


def ai_blobs(line_rows):
    return {2253: build_sleep_ai(line_rows), 2254: build_awake_ai(line_rows),
            2255: build_hound_ai()}


def hound_row(d_npc):
    row = list(d_npc.d[HOUND_TEMPLATE])
    row[C_NAME] = HOUND_NAME.encode()
    for c, v in HOUND_STATS.items():
        row[c] = b(v)
    for c in (C_TARGET, C_DEADEVENT, *oro.NPC_SELL_TAB_COLS):
        row[c] = b""
    row[oro.NPC_STRID_COL] = HOUND_STRID.encode()
    row[oro.NPC_PVP_COL] = oro.DEFAULT_PVP_STATE
    if d_npc.get(HOUND, 0).strip():
        row[C_EXP] = d_npc.get(HOUND, C_EXP)        # rebalance-exp-rewards.py owns it
    return row


# The skeleton is the plain wolf's (WOLF3\RUNA_WOLF3_BONE.ZMD, byte-identical), and
# none of its dummies is a mouth on Cerberus's three-headed mesh: p_06 (index 5, the
# Fireball row's firing point) floats inside the spiked collar above the heads, p_05
# sits 24 units -- about 2 m at size 800 -- inside the middle muzzle. So Cerberus's own
# copy of the skeleton (CERBERUS\RUNA_WOLF3_BONE.ZMD, which only its CHR entries name)
# gets an 8th dummy, p_mouth, at the middle head's muzzle tip: HEAD01.ZMS's central
# vertices end at y -92.5, z 79-117 (mesh space = bind pose, the wolf faces -y). The
# engine appends its own root dummy after the file's (zz_skeleton.cpp), which moves
# from index 7 to 8; nothing of Cerberus's names 7 but the breath.
SKEL_REL = r"3DDATA\NPC\ANIMAL\CERBERUS\RUNA_WOLF3_BONE.ZMD"
MOUTH = (0.0, -90.0, 95.0)          # bind pose, just inside the tip
MOUTH_NAME = b"p_mouth"
MOUTH_PARENT = 4                    # b1_head


def _q2m(w, x, y, z):
    import numpy as np
    return np.array([[1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)],
                     [2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)],
                     [2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)]])


def cerberus_skeleton(src):
    """tsuki's skeleton with p_mouth appended (ZMD0003, zz_skeleton.cpp layout)."""
    import numpy as np
    blob = open(P(src, SKEL_REL), "rb").read()
    if blob[:7] != b"ZMD0003":
        raise SystemExit(f"{SKEL_REL}: not a ZMD0003")
    o = 7
    nb, = struct.unpack_from("<I", blob, o); o += 4
    world = []
    for i in range(nb):
        parent, = struct.unpack_from("<I", blob, o); o += 4
        o = blob.index(b"\0", o) + 1
        pos = np.array(struct.unpack_from("<3f", blob, o)); o += 12
        rot = _q2m(*struct.unpack_from("<4f", blob, o)); o += 16
        if i == 0:
            world.append((rot, pos))
        else:
            pr, pp = world[parent]
            world.append((pr @ rot, pp + pr @ pos))
    count_at = o
    nd, = struct.unpack_from("<I", blob, o); o += 4
    names = []
    for _ in range(nd):
        e = blob.index(b"\0", o)
        names.append(blob[o:e]); o = e + 1 + 4 + 12 + 16
    if o != len(blob):
        raise SystemExit(f"{SKEL_REL}: {len(blob) - o} bytes after the dummies")
    if MOUTH_NAME in names:
        raise SystemExit(f"{SKEL_REL}: the source already has {MOUTH_NAME!r}")
    pr, pp = world[MOUTH_PARENT]
    local = pr.T @ (np.array(MOUTH) - pp)
    entry = MOUTH_NAME + b"\0" + struct.pack("<I3f4f", MOUTH_PARENT, *local, 1.0, 0.0, 0.0, 0.0)
    return blob[:count_at] + struct.pack("<I", nd + 1) + blob[count_at + 4:] + entry


def breath_row(sk):
    row = list(sk.d[BREATH_SRC])
    row[SK_NAME] = BREATH_NAME.encode()
    row[SK_LINE] = b(BREATH)
    for c, v in BREATH_CELLS.items():
        row[c] = b(v)
    return row


def leap_row(sk):
    row = list(sk.d[LEAP_SRC])
    row[SK_NAME] = LEAP_NAME.encode()
    row[SK_LINE] = b(LEAP)
    row[SK_CAST_FX], row[SK_CAST_LOC] = b(LEAP_CAST_FX), b"999"
    row[SK_HIT_FX], row[SK_HIT_LOC] = b(LEAP_HIT_FX), b"999"
    return row


def stage2_extras(ours, dry):
    """The hellhound row, model and STL key, and the Infernal Leap skill row."""
    d_npc = oro.Stb(P(ours, NPC_STB_REL))
    cur = d_npc.get(HOUND, 0).decode("latin-1").strip()
    if cur and cur != HOUND_NAME:
        raise SystemExit(f"our LIST_NPC row {HOUND} is {cur!r}")
    want = hound_row(d_npc)
    if d_npc.d[HOUND] != want:
        d_npc.d[HOUND] = want
        d_npc.save(dry)
        print(f"    {'LIST_NPC.STB':26s} row {HOUND} {HOUND_NAME} (from {HOUND_TEMPLATE})")
    stl = oro.Stl(P(ours, NPC_STL_REL))
    if not stl.has(HOUND_STRID):
        stl.append(HOUND_STRID, HOUND, HOUND_NAME)
        stl.save(dry)
        print(f"    {'LIST_NPC_S.STL':26s} +1 key {HOUND_STRID}")
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    if HOUND >= len(chr_.chars) or chr_.chars[HOUND] is None:
        if HOUND >= len(chr_.chars):
            chr_.chars.extend([None] * (HOUND + 1 - len(chr_.chars)))
        chr_.chars[HOUND] = copy.deepcopy(chr_.chars[HOUND_TEMPLATE])
        chr_.save(dry)
        print(f"    {'LIST_NPC.CHR':26s} entry {HOUND} = {HOUND_TEMPLATE}'s model")

    sk = oro.Stb(P(ours, SKILL_STB_REL))
    if sk.rows <= LEAP:
        sk.grow_to(LEAP + 1)
    cur = sk.get(LEAP, 0).decode("latin-1").strip()
    if cur and cur != LEAP_NAME:
        raise SystemExit(f"our LIST_SKILL row {LEAP} is {cur!r}")
    wrote = []
    for row, name, src, build in ((LEAP, LEAP_NAME, LEAP_SRC, leap_row),
                                  (BREATH, BREATH_NAME, BREATH_SRC, breath_row)):
        if sk.rows <= row:
            sk.grow_to(row + 1)
        cur = sk.get(row, 0).decode("latin-1").strip()
        if cur and cur != name:
            raise SystemExit(f"our LIST_SKILL row {row} is {cur!r}")
        want = build(sk)
        if sk.d[row] != want:
            sk.d[row] = want
            wrote.append(f"{row} {name} (from {src})")
    if wrote:
        sk.save(dry)
        print(f"    {'LIST_SKILL.STB':26s} rows " + ", ".join(wrote))


def stage2(ours, src, dry):
    print("stage 2 -- the monsters")
    ids = sorted(MONSTERS)
    s_npc, d_npc = oro.Stb(P(src, NPC_STB_REL)), oro.Stb(P(ours, NPC_STB_REL))
    if d_npc.rows <= max(ids):
        raise SystemExit("our LIST_NPC is shorter than tsuki's ids")
    written, fresh = [], []
    for i in ids:
        if s_npc.get(i, 0) != b"Cerberus":
            raise SystemExit(f"tsuki LIST_NPC row {i} is {s_npc.get(i, 0)!r}")
        cur = d_npc.get(i, 0).decode("latin-1").strip()
        if cur and cur != MONSTERS[i][0]:
            raise SystemExit(f"our LIST_NPC row {i} is {cur!r}")
        if not cur:
            fresh.append(i)
        want = npc_row(s_npc, d_npc, i)
        if d_npc.d[i] != want:
            d_npc.d[i] = want
            written.append(i)
    if written:
        d_npc.save(dry)
    print(f"    {'LIST_NPC.STB':26s} rows {written or 'already in place'}")

    stl = oro.Stl(P(ours, NPC_STL_REL))
    names = [i for i in ids if not stl.has(MONSTERS[i][1])]
    for i in names:
        stl.append(MONSTERS[i][1], i, MONSTERS[i][0])
    if names:
        stl.save(dry)
    print(f"    {'LIST_NPC_S.STL':26s} +{len(names)} keys")

    stage2_extras(ours, dry)
    changed = write_if_changed(P(ours, SKEL_REL), cerberus_skeleton(src), dry)
    print(f"    {'RUNA_WOLF3_BONE.ZMD':26s} p_mouth {'written' if changed else 'in place'}")
    line_rows, added = ai_lines(ours, dry)
    print(f"    {'AI_s.STB':26s} +{added} lines, rows {sorted(line_rows.values())}")
    fai = oro.Stb(P(ours, AI_STB_REL))
    ai_rows = []
    for row, fname in sorted(AI_FILES.items()):
        cell = rf"3DDATA\AI\{fname}".encode("latin-1")
        if fai.rows <= row:
            fai.grow_to(row + 1)
        if fai.get(row, 0) != cell:
            if fai.get(row, 0).strip():
                raise SystemExit(f"our FILE_AI row {row} is {fai.get(row, 0)!r}")
            fai.set(row, 0, cell)
            ai_rows.append(row)
    if ai_rows:
        fai.save(dry)
    print(f"    {'FILE_AI.STB':26s} rows {ai_rows or 'already in place'}")
    for row, blob in sorted(ai_blobs(line_rows).items()):
        fname = AI_FILES[row]
        changed = write_if_changed(os.path.join(P(ours, AI_DIR_REL), fname), blob, dry)
        print(f"    {fname:26s} {'written' if changed else 'in place'}")

    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    cleared = [i for i in fresh if i < len(chr_.chars) and chr_.chars[i] is not None]
    for i in cleared:
        chr_.chars[i] = None
    if cleared:
        chr_.save(dry)
        print(f"    {'LIST_NPC.CHR':26s} cleared orphan entries {cleared}")
    oro.import_characters(ids, ours, src, dry, "Cerberus")
    if not dry or os.path.isfile(P(ours, SKEL_REL)):
        write_if_changed(P(ours, SKEL_REL), cerberus_skeleton(src), dry)


# ------------------------------------------------------------------- stage 3
def register_trigger():
    """`Cerberus-Register`: the conditions are the whole check, there is no reward.

    COND_013 selects the gatekeeper (a server-wide lookup by NPC number), COND_011
    requires its event value 0 to be OPEN_VALUE (the controller sets it while
    registration is open), COND_003 asks level >= MIN_LEVEL and money >= FEE. The
    game server recognises the trigger by name once it has passed and adds the
    player to the draw (the fee is taken at the draw, from those drawn only).
    Layouts from retail PVP10.QSD (COND_011 / COND_013) and import-hebarn.py
    (COND_003)."""
    c_npc = struct.pack("<Iii", 12, 13, GATEKEEPER)
    c_open = struct.pack("<IiBxhiB3x", 20, 11, 0, 0, OPEN_VALUE, OP_EQ)
    abil = [(AT_LEVEL, MIN_LEVEL, OP_GE), (AT_MONEY, FEE, OP_GE)]
    pl = struct.pack("<i", len(abil)) + b"".join(struct.pack("<iiB3x", *a) for a in abil)
    c_abil = struct.pack("<Ii", 8 + len(pl), 3) + pl
    return fate.qsd_build_trigger(REGISTER_TRIGGER, [c_npc, c_open, c_abil], [])


def gatekeeper_row(n):
    row = list(n.d[GATEKEEPER_TEMPLATE])
    row[0] = GATEKEEPER_NAME.encode("latin-1")
    row[oro.NPC_STRID_COL] = GATEKEEPER_STRID.encode()
    for c in oro.NPC_SELL_TAB_COLS:
        row[c] = b""
    return row


def event_row(e):
    return [GATEKEEPER_NAME.encode("latin-1"), e.get(EVENT_ROW - 1, 1), b"NPC",
            rf"3Ddata\Event\{GATEKEEPER_CON}".encode()]


def gatekeeper_placement(ours):
    """A MOB record at the temple door, on the template NPC's own record."""
    tmpl = None
    d = P(ours, TEMPLATE_MAP_REL)
    for name in ifo_files(d):
        buf, bounds = oro.read_ifo(os.path.join(d, name))
        for o in oro.read_lump(buf, bounds, oro.LUMP_MOB)[0] or []:
            if struct.unpack_from("<i", o["fixed"], 8)[0] == GATEKEEPER_TEMPLATE:
                tmpl = o
    if tmpl is None:
        raise SystemExit(f"NPC {GATEKEEPER_TEMPLATE} is not placed in {TEMPLATE_MAP_REL}")
    fname, pos, map_xy = chunk_of_world(*GATEKEEPER_POS)
    z = ground(P(ours, LP02_REL), *GATEKEEPER_POS)
    b = bytearray(fixed_with(tmpl["fixed"], map_xy=map_xy, pos=(pos[0], pos[1], z)))
    struct.pack_into("<i", b, 8, GATEKEEPER)
    struct.pack_into("<4f", b, POS_OFF - 16, *GATEKEEPER_ROT)
    obj = dict(tmpl)
    obj["fixed"] = bytes(b)
    obj["extra"] = struct.pack("<i", 0) + oro.put_bstr(GATEKEEPER_CON.encode("latin-1"))
    obj["obj_id"] = GATEKEEPER
    return fname, obj


def toll_cmd(ours, write):
    cmd = [QUEST_EDITOR, "con-toll", ours, GATEKEEPER_CON, TOLL_KEY, REGISTER_TRIGGER,
           "--open-when", str(OPEN_VALUE)]
    for k, v in DIALOG.items():
        cmd += [k, v]
    return cmd + (["--write"] if write else [])


def stage3(ours, src, dry):
    print("stage 3 -- the gatekeeper and the register trigger")
    qp = P(ours, QSD_REL)
    blob = open(qp, "rb").read()
    want = register_trigger()
    have = fate.qsd_trigger_bytes(blob, REGISTER_TRIGGER)
    if have is None:
        qdir = P(ours, r"3DDATA\QUESTDATA")
        for f in os.listdir(qdir):
            if f.lower().endswith(".qsd") and f.lower() != os.path.basename(qp).lower():
                if fate.qsd_has_trigger(open(os.path.join(qdir, f), "rb").read(), REGISTER_TRIGGER):
                    raise SystemExit(f"{REGISTER_TRIGGER} already exists in {f} (names are global)")
        out = fate.qsd_append_pattern(blob, QSD_PATTERN, [want])
    elif have != want:
        out = blob.replace(have, want, 1)
    else:
        out = blob
    ok, _ = fate.qsd_parse_ok(out)
    if not ok or fate.qsd_trigger_bytes(out, REGISTER_TRIGGER) != want:
        raise SystemExit("register trigger did not round-trip")
    if out != blob:
        write_if_changed(qp, out, dry)
    print(f"    {'QP401.QSD':26s} {REGISTER_TRIGGER}: NPC {GATEKEEPER} value {OPEN_VALUE}, "
          f"level >= {MIN_LEVEL}, zuly >= {FEE} " + ("already in place" if out == blob else "written"))

    n = oro.Stb(P(ours, NPC_STB_REL))
    if n.rows <= GATEKEEPER:
        n.grow_to(GATEKEEPER + 1)
    cur = n.get(GATEKEEPER, 0).decode("latin-1").strip()
    if cur and cur != GATEKEEPER_NAME:
        raise SystemExit(f"our LIST_NPC row {GATEKEEPER} is {cur!r}")
    row = gatekeeper_row(n)
    if n.d[GATEKEEPER] != row:
        n.d[GATEKEEPER] = row
        n.save(dry)
        print(f"    {'LIST_NPC.STB':26s} row {GATEKEEPER} {GATEKEEPER_NAME!r} (from {GATEKEEPER_TEMPLATE})")
    stl = oro.Stl(P(ours, NPC_STL_REL))
    if not stl.has(GATEKEEPER_STRID):
        stl.append(GATEKEEPER_STRID, GATEKEEPER, GATEKEEPER_NAME)
        stl.save(dry)
        print(f"    {'LIST_NPC_S.STL':26s} +1 key {GATEKEEPER_STRID}")
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    if GATEKEEPER >= len(chr_.chars) or chr_.chars[GATEKEEPER] is None:
        if GATEKEEPER >= len(chr_.chars):
            chr_.chars.extend([None] * (GATEKEEPER + 1 - len(chr_.chars)))
        chr_.chars[GATEKEEPER] = copy.deepcopy(chr_.chars[GATEKEEPER_TEMPLATE])
        chr_.save(dry)
        print(f"    {'LIST_NPC.CHR':26s} entry {GATEKEEPER} = {GATEKEEPER_TEMPLATE}'s model")
    e = oro.Stb(P(ours, EVENT_STB_REL))
    erow = event_row(e)
    if e.d[EVENT_ROW] != erow:
        if any(x.strip() for x in e.d[EVENT_ROW]):
            raise SystemExit(f"our LIST_EVENT row {EVENT_ROW} is {e.get(EVENT_ROW, 0)!r}")
        e.d[EVENT_ROW] = erow
        e.save(dry)
        print(f"    {'LIST_EVENT.STB':26s} row {EVENT_ROW} -> {GATEKEEPER_CON}")

    if not os.path.isfile(QUEST_EDITOR):
        raise SystemExit(f"{QUEST_EDITOR} missing: build it (cargo build --release -p quest-editor)")
    r = subprocess.run(toll_cmd(ours, not dry), capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"quest-editor con-toll failed:\n{r.stdout}\n{r.stderr}")
    for line in r.stdout.splitlines():
        if line.strip().startswith(("CREATE", "REWRITE", "UNCHANGED", "UPSERT")):
            print(f"    {'dialog':26s} {line.strip()[:110]}")

    fname, want_obj = gatekeeper_placement(ours)
    pth = find_ci(P(ours, LP02_REL), fname)
    buf, bounds = oro.read_ifo(pth)
    mobs, trailing = oro.read_lump(buf, bounds, oro.LUMP_MOB)
    if mobs is None:
        raise SystemExit(f"{pth}: no MOB lump")
    keep = [o for o in mobs if struct.unpack_from("<i", o["fixed"], 8)[0] != GATEKEEPER]
    lump = oro.build_object_lump(keep + [want_obj], trailing)
    off, end = oro.lump_block(bounds, oro.LUMP_MOB)
    if buf[off:end] == lump:
        print(f"    {'LP02/' + fname:26s} gatekeeper already placed")
    else:
        write_if_changed(pth, oro.build_ifo(bounds, buf, {oro.LUMP_MOB: lump}), dry)
        print(f"    {'LP02/' + fname:26s} + {GATEKEEPER_NAME} at {GATEKEEPER_POS} m")
    sweep_event_baks(ours, dry)


def sweep_event_baks(ours, dry):
    """quest-editor leaves `<file>.bak` beside what it writes; move them out."""
    ev = P(ours, r"3DDATA\EVENT")
    for f in sorted(os.listdir(ev)):
        if f.lower().endswith(".bak") and not dry:
            dest = os.path.join(BUILD_DIR, "bak", "3DDATA", "EVENT", f)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            if os.path.isfile(dest):
                os.remove(os.path.join(ev, f))
            else:
                shutil.move(os.path.join(ev, f), dest)


# ------------------------------------------------------------------- stage 4
dropkit = load("add_karkia_drops", "add-karkia-drops.py")   # build_row / encode
ITEM_STB = {T_CAP: "LIST_CAP", T_BODY: "LIST_BODY", T_ARMS: "LIST_ARMS", T_FOOT: "LIST_FOOT",
            T_BACK: "LIST_BACK", T_WEAPON: "LIST_WEAPON", T_GEM: "LIST_JEMITEM"}
DROP_STB_REL = r"3DDATA\STB\ITEM_DROP.STB"


def drop_table():
    """(common, groups) for ITEM_DROP row 49. All 30 common slots are filled, so a
    kill always rolls an item (col 20 = 100: no zone fallback, no money); low
    slots win more often (Get_DropITEM), so the weapon and wing redirects sit first.
    The lv160-170 weapons and lv150-170 wings have no other source."""
    common = [("redirect", 1), ("redirect", 2), ("redirect", 3), ("redirect", 4)] * 3
    common += [(T_BACK, 1017),                      # Skyborn Preceptor 150
               (T_WEAPON, 377),                     # Dual Steam Shockers 170
               (T_WEAPON, 73)]                      # Tribal Crossbow 156
    common += [(T_GEM, n) for n in (307, 317, 367, 377)]   # Garnet, Ruby, Diamond, Pink Opal [7]
    common += [(T_GEM, n) for n in (336, 346, 356, 366)]   # Topaz, Emerald, Peridot, Diamond [6]
    common += [(T_BODY, n) for n in (40, 70, 100, 130)]    # the four lv165 bodies
    common += [(T_ARMS, n) for n in (40, 70, 100)]         # lv165 gloves
    groups = {
        # Simplex Saber 163, Bone Club 168, Military Crossbow 168, Sword 166, Nail Hatchet 166
        1: [(T_WEAPON, n) for n in (22, 51, 74, 120, 149)],
        # Keil Cutter 170, Ferrum Bow 167, Ancient Gun 165, Ancient Launcher 166, Marvel Staff 166
        2: [(T_WEAPON, n) for n in (181, 221, 250, 278, 321)],
        # Vision Wand 160, Blaze Wand 167, Bastard & Brave Gun 160, Glove Knuckle 165,
        # Mythril Dual Wield 170
        3: [(T_WEAPON, n) for n in (349, 350, 376, 420, 448)],
        # Holy Coffin 160, Sandalphon Wings 165, Deneb Blade 170, Moonlit Magic Circle 152
        4: [(T_BACK, n) for n in (1069, 1070, 1071, 1087)],
    }
    return common, groups


def all_loot():
    """Every (type, no) Cerberus can give, table and death drops."""
    common, groups = drop_table()
    out = [e for e in common if e[0] != "redirect"] + [e for g in groups.values() for e in g]
    out += [(T_WEAPON, n) for g in UNIQUES for n in g] + [(T_BACK, n) for n in BACK_DROP]
    out += [(T_GEM, n) for n in GEM5_DROP + GEM6_DROP]
    return out


def item_names(ours):
    """{(type, no): name}; exits on a blank row (a drop would hand out nothing)."""
    out, stbs = {}, {}
    for t, no in all_loot():
        stb = stbs.setdefault(t, oro.Stb(P(ours, rf"3DDATA\STB\{ITEM_STB[t]}.STB")))
        if not stb.occupied(no):
            raise SystemExit(f"{ITEM_STB[t]} row {no} is blank -- a drop would hand out nothing")
        out[(t, no)] = stb.get(no, 0).decode("latin-1").strip()
    return out


def drop_row_cells(cols):
    row = [b""] * cols
    row[0] = DROP_LABEL.encode()
    for slot, v in dropkit.build_row(*drop_table()).items():
        row[1 + slot] = str(v).encode()
    return row


def simulate(cells, n=300000, seed=7):
    """Get_DropITEM at level parity with col 20 = 100 (add-oro-drops.simulate's model)."""
    import random
    rnd, counts = random.Random(seed), collections.Counter()
    kinds = {T_WEAPON: "weapon lv160-170", T_BACK: "wings / back shield", T_GEM: "gem [6]/[7]",
             T_BODY: "armour lv165", T_ARMS: "armour lv165"}
    for _ in range(n):
        drop_var = int((100 + 100 - (1 + rnd.randrange(100)) - 16 * 3.5 - 10) * 0.38)
        if drop_var <= 0:
            counts["nothing"] += 1
            continue
        idx = rnd.randrange(30) if drop_var > 30 else rnd.randrange(drop_var)
        v = cells.get(idx, 0)
        if 0 < v <= 4:
            v = cells.get(26 + v * 5 + rnd.randrange(5), 0)
        if v <= 0:
            counts["nothing"] += 1
            continue
        t = v // 100000 if v >= 100000 else v // 1000
        counts[kinds.get(t, "?")] += 1
    return {k: c / n for k, c in counts.most_common()}


def stage4(ours, src, dry):
    print("stage 4 -- the loot table")
    names = item_names(ours)
    common, groups = drop_table()
    listed = [e for e in common if e[0] != "redirect"] + [e for g in groups.values() for e in g]
    print("    table: " + ", ".join(names[e] for e in listed))
    for k, v in simulate(dropkit.build_row(common, groups)).items():
        print(f"      {k:22s} {v * 100:5.1f}% of kills")
    print("    death drops, every kill: one of " + ", ".join(names[(T_WEAPON, n)] for g in UNIQUES for n in g))
    print("      + one of " + ", ".join(names[(T_BACK, n)] for n in BACK_DROP))
    print("      + one of " + ", ".join(names[(T_GEM, n)] for n in GEM5_DROP))
    print("      + one of " + ", ".join(names[(T_GEM, n)] for n in GEM6_DROP))
    d = oro.Stb(P(ours, DROP_STB_REL))
    want = drop_row_cells(d.cols)
    if d.d[DROP_TABLE] == want:
        print(f"    {'ITEM_DROP.STB':26s} row {DROP_TABLE} already in place")
        return
    if any(x.strip() for x in d.d[DROP_TABLE]) and d.get(DROP_TABLE, 0) != DROP_LABEL.encode():
        raise SystemExit(f"ITEM_DROP row {DROP_TABLE} holds a table we did not author: {d.get(DROP_TABLE, 0)!r}")
    d.d[DROP_TABLE] = want
    d.save(dry)
    print(f"    {'ITEM_DROP.STB':26s} row {DROP_TABLE} written")


# --------------------------------------------------------------------- verify
def verify(ours, src):
    bad = 0

    def check(ok, what):
        nonlocal bad
        bad += not ok
        print(f"    {'ok  ' if ok else 'FAIL'} {what}")

    print("verify")
    s_map, d_map = P(src, MAPS_REL), P(ours, MAPS_REL)
    same = os.path.isdir(d_map)
    for name in ifo_files(s_map) if same else []:
        p = os.path.join(d_map, name.upper())
        same &= os.path.isfile(p) and open(p, "rb").read() == lair_ifo(ours, s_map, name)
    check(same, "lair .IFO files (no gate, no spawn point, no dead box)")
    check(os.path.isfile(os.path.join(d_map, ZON_NAME)), ZON_NAME)
    lits = True
    for chunk_name in [f[:-4] for f in ifo_files(s_map)]:
        for lit_name in ("OBJECTLIGHTMAPDATA.LIT", "BUILDINGLIGHTMAPDATA.LIT"):
            p = os.path.join(d_map, chunk_name, "LIGHTMAP", lit_name)
            lits &= os.path.isfile(p) and open(p, "rb").read() == lair_lit(s_map, chunk_name, lit_name)[0]
    check(lits, "lair .lit files (no entry for a dropped record)")
    check(not any(f.lower().endswith(".txt") for f in os.listdir(d_map)), "no stray .txt")
    z = oro.Stb(P(ours, ZONE_STB_REL))
    check(z.d[ZONE] == zone_row(oro.Stb(P(src, ZONE_STB_REL)), z), f"LIST_ZONE row {ZONE}")
    check(oro.Stl(P(ours, ZONE_STL_REL)).has(ZONE_STL_KEY), ZONE_STL_KEY)
    w = oro.Stb(P(ours, WARP_STB_REL))
    check(w.get(RETIRED_WARP, 0) != RETIRED_WARP_LABEL, f"WARP row {RETIRED_WARP} no longer ours")
    lair_gates = []
    for name in ifo_files(d_map) if os.path.isdir(d_map) else []:
        b, bd = oro.read_ifo(os.path.join(d_map, name))
        lair_gates += oro.read_lump(b, bd, oro.LUMP_WARP)[0] or []
    check(not lair_gates, "no gate out of the lair")
    ev = zon.parse(open(find_ci(P(ours, LP02_REL), LP02_ZON), "rb").read()).lump(zon.EVENTS)
    check([e for e in ev if e.name == LANDING.encode()] == [landing_event(ours)], f"{LP02_ZON} {LANDING}")
    lair_ev = zon.parse(open(os.path.join(d_map, ZON_NAME), "rb").read()).lump(zon.EVENTS) \
        if os.path.isfile(os.path.join(d_map, ZON_NAME)) else []
    check({e.name for e in lair_ev} >= {b"start", b"restore"}, "lair start/restore points")

    npc, s_npc = oro.Stb(P(ours, NPC_STB_REL)), oro.Stb(P(src, NPC_STB_REL))
    check(all(npc.d[i] == npc_row(s_npc, npc, i) for i in MONSTERS), f"LIST_NPC {sorted(MONSTERS)}")
    stl = oro.Stl(P(ours, NPC_STL_REL))
    check(all(stl.has(v[1]) for v in MONSTERS.values()), "LIST_NPC_S keys")
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    check(all(i < len(chr_.chars) and chr_.chars[i] for i in MONSTERS), "CHR entries")
    fai = oro.Stb(P(ours, AI_STB_REL))
    ais = oro.Stb(P(ours, AI_STR_REL))
    rows = {}
    for key, text in LINES.items():
        hit = [r for r in range(ais.rows) if ais.get(r, 1) == text.encode("latin-1")]
        rows[key] = hit[0] if hit else None
    check(None not in rows.values(), "AI_s.STB lines")
    if None not in rows.values():
        for row, blob in sorted(ai_blobs(rows).items()):
            p = os.path.join(P(ours, AI_DIR_REL), AI_FILES[row])
            check(fai.get(row, 0) == rf"3DDATA\AI\{AI_FILES[row]}".encode()
                  and os.path.isfile(p) and open(p, "rb").read() == blob, f"FILE_AI {row} + {AI_FILES[row]}")
    ex = load("balance_trend_exclude", "balance-trend-exclude.py")
    check(all(ex.excluded(i) for i in list(MONSTERS) + [HOUND]),
          "balance-trend-exclude.py lists the three ids")
    check(npc.rows > HOUND and npc.d[HOUND] == hound_row(npc), f"LIST_NPC {HOUND} {HOUND_NAME}")
    check(HOUND < len(chr_.chars) and chr_.chars[HOUND] == chr_.chars[HOUND_TEMPLATE], f"CHR {HOUND}")
    check(stl.has(HOUND_STRID), HOUND_STRID)
    sk = oro.Stb(P(ours, SKILL_STB_REL))
    check(sk.rows > LEAP and sk.d[LEAP] == leap_row(sk), f"LIST_SKILL {LEAP} {LEAP_NAME}")
    check(sk.rows > BREATH and sk.d[BREATH] == breath_row(sk), f"LIST_SKILL {BREATH} {BREATH_NAME}")
    sp = P(ours, SKEL_REL)
    check(os.path.isfile(sp) and open(sp, "rb").read() == cerberus_skeleton(src),
          "Cerberus skeleton has p_mouth")
    check(fate.qsd_trigger_bytes(open(P(ours, QSD_REL), "rb").read(), REGISTER_TRIGGER)
          == register_trigger(), f"QP401.QSD {REGISTER_TRIGGER}")
    check(npc.rows > GATEKEEPER and npc.d[GATEKEEPER] == gatekeeper_row(npc),
          f"LIST_NPC {GATEKEEPER} {GATEKEEPER_NAME}")
    check(GATEKEEPER < len(chr_.chars) and chr_.chars[GATEKEEPER] == chr_.chars[GATEKEEPER_TEMPLATE],
          f"CHR {GATEKEEPER}")
    check(stl.has(GATEKEEPER_STRID), GATEKEEPER_STRID)
    e = oro.Stb(P(ours, EVENT_STB_REL))
    check(e.d[EVENT_ROW] == event_row(e), f"LIST_EVENT {EVENT_ROW}")
    r = subprocess.run(toll_cmd(ours, False), capture_output=True, text=True)
    check(r.returncode == 0 and "UNCHANGED toll gate" in r.stdout, f"{GATEKEEPER_CON} (register dialog)")
    fname, want = gatekeeper_placement(ours)
    b2, bd = oro.read_ifo(find_ci(P(ours, LP02_REL), fname))
    mobs = oro.read_lump(b2, bd, oro.LUMP_MOB)[0] or []
    check([(o["name"], o["fixed"], o["extra"]) for o in mobs
           if struct.unpack_from("<i", o["fixed"], 8)[0] == GATEKEEPER]
          == [(want["name"], want["fixed"], want["extra"])], "gatekeeper placed at the temple door")
    d = oro.Stb(P(ours, DROP_STB_REL))
    check(d.d[DROP_TABLE] == drop_row_cells(d.cols), f"ITEM_DROP row {DROP_TABLE} (Cerberus)")
    item_names(ours)                       # exits on a blank item row
    print("verify " + ("OK" if not bad else f"FAILED ({bad})"))
    return 1 if bad else 0


# ------------------------------------------------------------------- selftest
def selftest(ours, src):
    print("selftest -- every writer must reproduce its input byte for byte")
    s_map = P(src, MAPS_REL)
    for name in ifo_files(s_map) + [ZON_NAME]:
        p = os.path.join(s_map, name)
        buf, bounds = oro.read_ifo(p)
        assert oro.build_ifo(bounds, buf, {}) == buf, name
        if name.lower().endswith(".ifo"):
            for lt in (oro.LUMP_MOB, oro.LUMP_REGEN, oro.LUMP_WARP, oro.LUMP_EVENT_OBJECT):
                objs, trailing = oro.read_lump(buf, bounds, lt)
                if objs is None:
                    continue
                off, end = oro.lump_block(bounds, lt)
                assert oro.build_object_lump(objs, trailing) == buf[off:end], (name, lt)
    zp = find_ci(P(ours, LP02_REL), LP02_ZON)
    assert zon.build(zon.parse(open(zp, "rb").read())) == open(zp, "rb").read(), LP02_ZON
    print(f"    the lair's .IFO/.ZON and our {LP02_ZON} round-trip")
    for rel in (ZONE_STB_REL, NPC_STB_REL, WARP_STB_REL, AI_STB_REL, AI_STR_REL):
        p = P(ours, rel)
        assert oro.Stb(p).to_bytes() == open(p, "rb").read(), rel
    for rel in (ZONE_STL_REL, NPC_STL_REL):
        p = P(ours, rel)
        assert oro.Stl(p).to_bytes() == open(p, "rb").read(), rel
    p = P(ours, NPC_CHR_REL)
    assert oro.Chr(p).to_bytes() == open(p, "rb").read(), "LIST_NPC.CHR"
    print("    LIST_ZONE / LIST_NPC / WARP / FILE_AI / AI_s / two STLs / CHR round-trip")
    print("    selftest OK")


# ---------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--stage", type=int, choices=(1, 2, 3, 4), action="append")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--root", default=ROOT)
    ap.add_argument("--source", default=DEFAULT_SRC)
    args = ap.parse_args()
    ours = os.path.join(args.root, "data")
    src = args.source
    if not os.path.isdir(P(src, MAPS_REL)):
        raise SystemExit(f"source is not a tsuki client with the Cerberus Lair: {src}")
    if args.selftest:
        selftest(ours, src)
        return 0
    if args.verify:
        return verify(ours, src)
    if not args.stage:
        ap.error("give --stage N (1-4), --verify or --selftest")
    for st in sorted(set(args.stage)):
        {1: stage1, 2: stage2, 3: stage3, 4: stage4}[st](ours, src, args.dry_run)
        print()
    if args.dry_run:
        print("dry run: nothing written")
    else:
        sweep_baks(ours)
        print("done. Next: audit-lightmap-index.py --zone 49, audit-ai-skill-refs.py,")
        print("rebake the VFS, restart the servers.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
