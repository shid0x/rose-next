#!/usr/bin/env python3
"""Import the Cave of Ulverick, RoseZA's spider dungeon, as our open zone 34.

The cave sits under Desert of the Dead (zone 29): its mouth is the "Cave of
Ulverick" arrow painted on JD04's minimap, east of the map at (5857, 5098).
RoseZA ran it as a party instance; we have no instances, so it comes in as a
plain open zone. Only RoseZA ships it whole -- its 9 chunks with .MOV grids and
full lightmaps; 667 and tsuki carry a different terrain cut without spawns,
titanRose the same IFOs without lightmap textures. Nothing of the instance
is kept: no puzzle and no boss phase existed, only bookkeeping.

Staged like scripts/import-shibuya.py; every stage is idempotent, --dry-run
previews, --verify re-derives the state, --selftest proves every writer
round-trips byte-identically before anything is touched:

    --stage 1   the map folder (REGEN emptied, the exit gate re-pointed), the
                decoration table LIST_DECO_JZC.ZSC and its art, morph object 46,
                the zone row and name, the way out (the cave's gate, re-pointed
                at WARP row 100, and its landing point in JD04.ZON), the BGM.
    --stage 2   the monsters: Ulverick's Minion (531) and the three bosses --
                Leader (532), General (533), King (534) -- at RoseZA's row
                numbers with OUR stats (MONSTERS), their models, and their AI
                rebuilt from RoseZA's with the instance events cut.
    --stage 3   the spawns: the 55 minion points binned into camps, and one
                boss alone in each of the cave's three rooms (BOSS_POINTS).
    --stage 4   the loot: one table per monster (DROP_TABLES) and zone row 34,
                which mirrors the Minion's.
    --stage 5   the way in: [Bone Warden] Ossian at the cave mouth, who takes a
                toll of bones (GATEKEEPER, TOLL) and sends you in. Needs
                bin/release/quest-editor.exe (con-toll) for the dialog.

The instance, and what replaced it
----------------------------------
  * **RoseZA's five identical zone rows (90-94) were five party slots**, each
    with its own copy of the three bosses (LIST_NPC 532-534, 580-594) and its
    own AI files (uv_*01..05). NPC Lithia (1156, Desert of the Dead) held each
    slot's state in her variables 1-5; quest triggers JZC_Gate01-05 checked a
    slot was free, warped the party in, spawned the Leader and started a 2.4 h
    quest timer (1790); the Leader's death spawned the General, his the King.
    We keep one row, one set, and no trigger: col 22 (join trigger JZC_enter,
    which kicked anyone above level 150 back out) is blank.
  * **Three rooms, three bosses, no chain** (2026-10-05, after the first test:
    the minimap shows three clear rooms). Each boss is a lone respawning point
    -- one body, the Oro kings' pattern -- in its own room, measured off the
    minimap (2.5 m a pixel, origin (4800, 5600) m): the King (lv151, 30 min) in
    the round middle room, the hardest to reach; the General (lv145, 20 min) in
    the big north-west room, on RoseZA's own Leader spawn, which is exactly that
    room's centre; the Leader (lv135, 10 min) in the small south-east room. RoseZA chained them
    through quest triggers (the Leader's death spawned the General, his the
    King); a chain in an open zone can be stalled or stolen, so each stands on
    its own. Every point spawns on its first tick since 2026-10-05: the zone
    clock (CDXHPC) starts at 0 and CRegenPOINT used to wait one full interval
    first, so the King appeared 30 minutes after a server boot.
    The first import (2026-10-04) took the King only and put him on a spot the
    survey read out of UV_King01's reward 8 -- in solid rock west of the
    corridor; he never showed.
  * **Every AI event gated on AICOND17 ("select NPC N") is cut.** That condition
    looks the NPC up across the whole server (`g_pZoneLIST->Get_LocalNPC`), and
    every event behind it is instance bookkeeping: the King's idle event "N_1"
    selects Lithia and **kills itself** (AIACT23) whenever her slot variable is 0
    -- which, with no instance to set it, is always; "30MinCheck" and the death
    event select NPC 1791 (blank here) to fire UV_TimeOver_n / UV_Defeated_n;
    three events gated on Judy (1201) variable 0 == 310 drop item 12:417, a
    RoseZA event item. **Speech (AIACT28) is cut too**: the Leader's spawn line
    is type 2, a server-wide announcement, and its text is a row number into
    the AI string table (`pSTB->get_cstr(iStrID - 1)`, cai_file.cpp), which is
    another lineage's here -- every respawn would have announced someone
    else's line, or "unknown", to the whole server. Everything else -- casts,
    cooldown variables (AIACT35),
    the minion's 3% Poison Weaver summon below 15% HP -- runs unchanged, and
    every opcode left is inside our cond 0-31 / act 0-38.
  * **The bosses' casts on motion 6 move to 8** (BOSS_REMOTION). The spider
    skeleton holds casting_01, a clip with no action frame, in slots 6, 7 and
    8, and action_skill01 in 9; a monster casts on slot nMotion and releases on
    nMotion + 1, so motion 6 released on an event-less clip and its payload
    folded in silently ~3 s late (the audit's "payload" class). The minion and
    the bosses' 3595 already cast on 8. The three bosses share one kit. 3594 (a self-heal) and 3060 are RoseZA's
    rows byte for byte; 3595-3597 are ours, patched for the Devourer import.

Why this is a port and not a copy
---------------------------------
  * **Zone 34**: free, LZON034 free, ITEM_DROP row 34 "(Un-Used)", and Evo
    numbered the cave 34-36. RoseZA's 90-94 hold Oro here. Our row 34 carried
    three stray revive cells (22/570/520, Desert of the Dead's own) and they are
    kept: dying in the cave revives you where dying in the desert does. RoseZA
    revived into Anima Lake.
  * **The decoration table is new to us** -- LIST_DECO_JZC.ZSC, identical in all
    four dumps that have it. Of the 160 files the cave places, 157 were here.
    Morph object 46 (domsky-night, a glowing dome) is a blank row in our
    LIST_MORPH_OBJECT and is copied with its texture.
  * **WARP rows**: RoseZA's exit is row 80, our Ramesses -> Pyramid gate. It
    lands on free row 100 (cave -> JD04, onto WARP-JZC01-JD04, copied from
    RoseZA's JD04.ZON). The cave's own JZC_Warpout box beside its exit is kept
    as RoseZA shipped it: its trigger is in no QSD, and the client checks a
    trigger locally before sending (QF_checkQuestCondition), so walking into it
    does nothing.
  * **The way in is a toll** (stage 5, 2026-10-05). RoseZA's entrance was an
    event box (JZC_Gate01) whose quest warped the party in. The first import
    made it a free walk-in gate (WARP row 99) at the box's position -- and the
    box sat 15 m under the desert floor (z -1010 cm, ground 500): RoseZA's
    4.2-scale box reached the surface, our 1.5-scale gate did not, so nobody
    could get in. It is removed (stage 1 deletes the gate and blanks row 99).
    In its place [Bone Warden] Ossian (LIST_NPC 1158, Harry's ghost model)
    stands at the mouth and asks a toll of bone -- 10 Animal Leg Bone and 10
    Animal Backbone, which Desert of the Dead's own monsters drop: you clear
    the desert to open the cave. One QSD trigger is the price and the door
    (QP401.QSD `Ulverick-EnterCave`: COND_004 >= 10 of each in the bag,
    REWD_001 op 0 takes them, REWD_007 sends the speaker -- not the party --
    to the cave's `start`), laid out like retail's 2051-19 (QP201.QSD), whose
    entities re-encode to the same fields. A trigger that fails is silent on
    both sides, so the dialog (`quest-editor con-toll`) asks first: "let me
    in" is answered by "lay them down?" or "you lack the bones", gated on
    QF_checkQuestCondition of that same trigger. COND_004 counts the first
    stack it finds, as retail does.
  * **Stats are ours**, from our spawned monsters' level medians (MONSTERS),
    the King with the house boss factors (x10 HP/EXP, x1.35 ATK, x1.2 DEF/RES).
    RoseZA's scale is ~2x ours (its Poison Weaver: HP 72, EXP 12792; ours 35,
    570). EXP is only seeded: rebalance-exp-rewards.py owns the column.
    Both ids are in balance-trend-exclude.py (levels 128/151 sit inside the
    60-199 window every balance pass fits).
  * **The loot is authored** (stage 4): RoseZA's tables 819/822 are empty in
    every dump we own, the drops lived on its server. Same shapes and rates
    as Oro/Karkia/SHIBUYA, and nothing new to the economy except the Jrose
    back items (957-1061), which had no source anywhere and cap at level 150
    -- doc/project-drops.md wanted "a pass of their own aimed at the 100-150
    band", and this cave is that band. The Minion (lv128, players ~118-137)
    pays the insect/spider materials, Elixir/Hime/Enthiric, a crystal and
    Orihalcon, medium waters, the lv129 weapon tier our Eldeon tables already
    use, and a bucket of lv120-135 back items. The King (lv151) pays four
    lv142-150 back items directly, the lv145 weapons, and the lv145 armour
    sets, which until now dropped only off lv170+ monsters, out of reach of a
    lv145 player. Simulated at parity (add-oro-drops.simulate, 600k kills):
    Minion 39% materials, 7% use, 1 in 30 gear, 1 in 32 a back item, 15%
    zuly; King ~38% gear (the house boss rate) plus ~20% a back item, no zuly.
  * **Camps, not a carpet** (stage 3). The instance packed 55 points of cap 5
    into the cave, ~275 bodies for one party. They are binned on Karkia's 60 m
    grid, keeping the authored point nearest each bin's centre: 19 camps of 5
    on Karkia's camp values (count 1 per slot, cap 5, 20 s, tacticPoint 100).

After running: `add-dds-mipmaps.py` over the folders stage 1 lists, then
`audit-lightmap-index.py --zone 34` and `audit-ai-skill-refs.py` (the casts),
rebake the VFS, restart the servers. Backups: build/ulverick/.
"""
import argparse
import collections
import copy
import importlib.util
import math
import os
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DATA_ROOT = os.path.join(ROOT, "data")
sys.path.insert(0, HERE)

# --------------------------------------------------------------------- config
DEFAULT_SRC = r"C:\Users\Thomas\Desktop\Testclients\RoseZA test client\data"
BUILD_DIR = os.path.join(ROOT, "build", "ulverick")

ZONE = 34
SRC_ZONE = 90
ZONE_NAME = "Cave of Ulverick"
ZONE_STL_KEY = "LZON034"
ZONE_STB_REL = r"3DDATA\STB\LIST_ZONE.STB"
ZONE_STL_REL = r"3DDATA\STB\LIST_ZONE_S.STL"
ZONE_COPY_COLS = 31                # 0..30 from RoseZA; 31-33 revive stay ours
ZONE_NAME_COL, ZONE_JOIN_COL, ZONE_STL_COL = 0, 22, 26
ZONE_REVIVE = ("22", "570", "520")  # Desert of the Dead's own (cols 31-33)
MAPS_REL = r"3DDATA\MAPS\JUNON\JZC01"
ZON_NAME = "JZCAVE.ZON"
DECO_ZSC_REL = r"3DDATA\JUNON\LIST_DECO_JZC.ZSC"
# Flames sit low on the cave's torches and braziers (2026-10-05 test). Each flame is
# a dummy point on its model (`_fire_01.eft` torch, `_fire_02.eft` brazier) and the
# .eft places its particles at the dummy itself (offset 0), so the dummy's height is
# the flame's. RoseZA authored them that way -- the same objects in Goblin Cave
# (LIST_DECO_JZ) and Karkia match byte for byte -- and the cave's table is its own,
# so lifting them here touches nothing else. cm, added to RoseZA's z on every run.
# Left alone: the campfire (411) and the cauldron (412), whose fire belongs low.
FLAME_LIFT = {
    **{o: 50 for o in (301, 304, 307, 310, 312, 428)},     # wall torches
    **{o: 50 for o in (315, 401)},                          # gate braziers, fire stands
}
FLAME_EFFECTS = (b"_fire_01.eft", b"_fire_02.eft")
ZSC_TAG_POS = 1
BGM_SRC = os.path.join(os.path.dirname(DEFAULT_SRC), "Sound", "BGM", "BGM_Fedion.ogg")
BGM_DEPLOY_DIR = r"C:\Users\Thomas\Desktop\ROSEProject\Sound\BGM"

MORPH_STB_REL = r"3DDATA\STB\LIST_MORPH_OBJECT.STB"
MORPH_ROWS = (46,)
MORPH_FILE_COLS = (1, 2, 3)

WARP_STB_REL = r"3DDATA\STB\WARP.STB"
WARP_OUT, SRC_WARP_OUT = 100, 80
WARP_ROWS = {
    WARP_OUT: ("JZC01 -> JD04 (Cave of Ulverick -> Desert of the Dead)", "29", "WARP-JZC01-JD04"),
}
# the free walk-in gate of the first import, removed by stage 1 (docstring)
RETIRED_WARP_IN = 99
RETIRED_WARP_LABEL = b"JD04 -> JZC01 (Desert of the Dead -> Cave of Ulverick)"
CAVE_START = "start"
JD04_REL = r"3DDATA\MAPS\JUNON\JD04"
JD04_GATE_IFO = "36_33.IFO"        # the cave mouth's chunk
JD04_LANDING = "WARP-JZC01-JD04"

NPC_STB_REL = r"3DDATA\STB\LIST_NPC.STB"
NPC_STL_REL = r"3DDATA\STB\LIST_NPC_S.STL"
NPC_CHR_REL = r"3DDATA\NPC\LIST_NPC.CHR"
NPC_STRID_PREFIX = "LUVMOB"
NPC_EXP_COL, NPC_DEAD_EVENT_COL, NPC_HAND_HIT_COL = 17, 41, 33
NPC_DROP_COLS = (18, 19, 20)       # table, money %, drop rate
# (name, level, HP, ATK, HIT, DEF, RES, AVOID, attack speed, EXP). Our spawned
# monsters' medians: level 124-132 field rows (HP column under 110) and 147-155;
# levels 135 and 145 interpolate between the two (their own samples are 5-9 rows
# and noisy). Max HP is level x the HP column (cobjnpc.cpp). The bosses climb in
# factors: Leader x5 HP/EXP, x1.25 ATK, x1.1 DEF/RES, x1.05 HIT/AVOID; General x7,
# x1.3, x1.15, x1.08; the King the house boss factors x10, x1.35, x1.2, x1.1.
# EXP is only seeded: rebalance-exp-rewards.py owns the column.
MONSTERS = {
    531: ("Ulverick's Minion",   128,  55, 565, 330, 430, 420, 190, 105, 520),
    532: ("Ulverick's Leader",   135, 250, 765, 384, 497, 465, 215, 105, 2075),
    533: ("Ulverick's General",  145, 294, 881, 449, 554, 491, 244, 100, 4459),
    534: ("King Ulverick",       151, 380, 969, 492, 601, 516, 263, 100, 6370),
}
STAT_COLS = (7, 8, 9, 10, 11, 12, 13, 14, 17)
BOSSES = (532, 533, 534)
DROP_RATE, MONEY_FIELD, MONEY_BOSS = 80, 15, 0
MONSTER_DROPS = {531: 819, 532: 820, 533: 821, 534: 822}    # stage 4 authors the tables

AI_STB_REL = r"3DDATA\STB\FILE_AI.STB"
AI_DIR_REL = r"3DDATA\AI"
MONSTER_AI = {454: "uv_minion01.aip", 455: "uv_leader01.aip", 456: "uv_general01.aip",
              457: "uv_king01.aip"}
BOSS_AI = {"uv_leader01.aip", "uv_general01.aip", "uv_king01.aip"}
AI_SELECT_NPC = 18                 # raw type of AICOND17 (raw = number + 1)
AI_CAST = 25                       # raw type of AIACT24
AI_SAY = 29                        # raw type of AIACT28
CAST_SKILL_OFF, CAST_MOTION_OFF = 10, 12
BOSS_REMOTION = {6: 8}

# ---- stage 3
CAMP_CELL = 6000                   # Karkia's 60 m grid, cm
CAMP = dict(cap=5, interval=20, range=12, tactic=100)
CAMP_BASIC, CAMP_TACTICS = [531] * 5, [531] * 2
# one boss alone in each room (world metres, the room's centre off the minimap);
# camps within `clear` m are dropped so the boss is not buried in minions
BOSS_POINTS = (
    dict(name="UV-KING", npc=534, world=(5231.0, 5236.0), interval=1800, range=1, clear=22),
    dict(name="UV-GENERAL", npc=533, world=(5076.0, 5345.0), interval=1200, range=1, clear=22),
    dict(name="UV-LEADER", npc=532, world=(5370.0, 5038.0), interval=600, range=1, clear=22),
)
CHUNK_CM, GRID_CM, ORIGIN_CM = 16000, 250, 520000
REGEN_POS_OFF = 2 + 2 + 4 + 4 + 4 + 4 + 16
MAP_XY_OFF = 2 + 2 + 4 + 4

# ---- stage 4
DROP_STB_REL = r"3DDATA\STB\ITEM_DROP.STB"
ITEM_STB = {2: "LIST_CAP", 3: "LIST_BODY", 4: "LIST_ARMS", 5: "LIST_FOOT", 6: "LIST_BACK",
            8: "LIST_WEAPON", 9: "LIST_SUBWPN", 10: "LIST_USEITEM", 12: "LIST_NATURAL"}


def _field(mats, uses, *buckets):
    """add-karkia-drops' field shape: materials and use items in the common slots
    (cheapest first: low slots roll most often), then a redirect per bucket."""
    common = [(12, m) for m in mats] + [(10, u) for u in uses]
    common += [("redirect", g + 1) for g in range(len(buckets))]
    return common, {g + 1: list(b) for g, b in enumerate(buckets)}


DROP_TABLES = {
    819: ("ULVERICK Minion (lv128)", _field(
        # Thick Insect Shell, Sticky Liquid, Elixir, Hime, Spider Web, Blue Crystal,
        # Enthiric, Orihalcon; Vital / Spiritual Water (M)
        [172, 178, 66, 67, 278, 162, 69, 16], [11, 30],
        # lv129: Righteous Sword, Giant Sword, Lynx Bow, Ikaness Staff, Phantasma Knuckle
        [(8, 18), (8, 116), (8, 217), (8, 316), (8, 416)],
        # Battle Insect Wings 120, Assassin's Guard 125, Skyborn Adjutant 126,
        # Buster Blade 130, Metatron Wings 135
        [(6, 992), (6, 977), (6, 1021), (6, 972), (6, 963)])),
    # The Leader: a mini-boss. Four lv132-138 back items direct (~4.5% each), the
    # lv135-136 weapons behind two redirects, then materials and a water.
    820: ("BOSS Ulverick's Leader (lv135)", (
        # Wings of the Awakened 132, Righteous Bulwark 132, Skull Brand 135,
        # Skyborn Pathfinder 138
        [(6, 1023), (6, 1028), (6, 974), (6, 1019), ("redirect", 1), ("redirect", 2),
         # Spider Web, Orihalcon, Enthiric, Hime, Blue Crystal; Vital Water (M)
         (12, 278), (12, 16), (12, 69), (12, 67), (12, 162), (10, 11)],
        # lv135: Black Shamshir, Shion's Guardian, Bifenith Bow, Oracle Staff,
        # Bloody Hook Knuckle
        {1: [(8, 19), (8, 117), (8, 218), (8, 317), (8, 417)],
         # lv131-136: Nirvana Hammer, Adamantium Axe, Steam Shocker, Glorious Dual
         # Wield, Longinus Spear
         2: [(8, 48), (8, 146), (8, 247), (8, 445), (8, 177)]})),
    # The General: four lv140-145 back items, the lv145-147 weapons the King does
    # not carry, and the lv145 gloves, boots and caps (until now only off lv170+
    # monsters; the King pays the body armour).
    821: ("BOSS Ulverick's General (lv145)", (
        # Fallen Gabriel Wings 140, Dreamdrift Butterfly 140, Ignis Wings 145,
        # Dragonic Wings 145; White Hearts, Otherworldly Metal, Vital Water (L)
        [(6, 962), (6, 995), (6, 964), (6, 1018),
         ("redirect", 1), ("redirect", 2), ("redirect", 3), ("redirect", 1),
         (12, 157), (12, 18), (10, 12)],
        # Doom Hammer 146, Titan Axe 146, Fury Spantun 147, Hawk Knuckle 145,
        # Demise Dual Weapon 146
        {1: [(8, 49), (8, 147), (8, 179), (8, 418), (8, 446)],
         # Durable / Fairy / Pirate / Fisher Gloves, Durable Boots
         2: [(4, 38), (4, 68), (4, 98), (4, 128), (5, 38)],
         # Fairy Shoes, Pirate Sandals, Fisherman Sandals, Fairy Bonnet, Fishing Hat
         3: [(5, 68), (5, 98), (5, 128), (2, 68), (2, 128)]})),
    822: ("BOSS King Ulverick (lv151)", (
        # Wings of Black Heaven 142, Draco Felix 145, Raging Sacred Cross 148,
        # Aetherforge Wings 150; each bucket twice; White Hearts, Otherworldly
        # Metal, Vital Water (L)
        [(6, 1020), (6, 986), (6, 1034), (6, 970),
         ("redirect", 1), ("redirect", 2), ("redirect", 1), ("redirect", 2),
         (12, 157), (12, 18), (10, 12)],
        # lv145: Death Bringer, Sacred Hander, Zephyr Long Bow, Shok Impact Gun, Twinkle Staff
        {1: [(8, 20), (8, 118), (8, 219), (8, 248), (8, 318)],
         # lv145: Durable Armor, Fairy Vest, Pirate Armor, Fisherman Suit, Durable Band
         2: [(3, 38), (3, 68), (3, 98), (3, 128), (2, 38)]})),
}
# the 20% zone roll (col 20 = 80) lands on the row numbered after the zone
ZONE_DROP_MIRROR = 819
ZONE_DROP_LABELS = (b"", b"(Un-Used)")   # what row 34 may hold before us

# ---- stage 5: the gatekeeper
GATEKEEPER = 1158                  # free (two stray zero cells, no CHR, no key)
GATEKEEPER_NAME = "[Bone Warden] Ossian"
GATEKEEPER_STRID = "LUVNPC1158"
GATEKEEPER_TEMPLATE = 1157         # [Ghost] Harry, Desert of the Dead: row + model
# in front of the cave mouth, beside where the way out lands you (world metres)
GATEKEEPER_POS = (5845.0, 5092.0)
EVENT_STB_REL = r"3DDATA\STB\LIST_EVENT.STB"
EVENT_ROW = 129
EVENT_DIR_REL = r"3DDATA\EVENT"
GATEKEEPER_CON = "EM29-005.con"
TOLL_KEY = "ulv"
QSD_REL = r"3DDATA\QUESTDATA\QP401.QSD"
TOLL_PATTERN, TOLL_TRIGGER = "Ulverick-Toll", "Ulverick-EnterCave"
TOLL = ((12, 191, 10), (12, 192, 10))    # Animal Leg Bone, Animal Backbone
TOLL_WHERE = 12                    # COND_004 iWhere: past the equip slots = the bag
OP_GE, OP_TAKE = 2, 0              # Check_QuestOP >=; REWD_001 op 0 = take
QUEST_EDITOR = os.path.join(ROOT, "bin", "release", "quest-editor.exe")
TOLL_TEXT = {
    "--greet": "Beneath my feet the Cave of Ulverick sleeps. Its spiders do not.",
    "--ask": "I want to enter the cave.",
    "--bye": "Farewell.",
    "--offer": "The dead of this desert ask a toll of bone: ten Animal Leg Bones and ten "
               "Animal Backbones. You carry them. Lay them down, and the way opens.",
    "--lack": "The dead of this desert ask a toll of bone: ten Animal Leg Bones and ten "
              "Animal Backbones. Hunt the beasts of the desert, and come back when you "
              "carry them.",
    "--accept": "Lay down the bones and enter.",
    "--decline": "Not yet.",
    "--later": "I will come back with the bones.",
}

# every file a writer here may leave a .bak beside (pack.rs would bake it)
BAK_TRACKED = [ZONE_STB_REL, ZONE_STL_REL, WARP_STB_REL, NPC_STB_REL, NPC_STL_REL,
               NPC_CHR_REL, r"3DDATA\NPC\PART_NPC.ZSC", AI_STB_REL, MORPH_STB_REL,
               DROP_STB_REL, EVENT_STB_REL, QSD_REL, r"3DDATA\EVENT\ulngtb_con.ltb",
               rf"{EVENT_DIR_REL}\{GATEKEEPER_CON}"]


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


oro = load("import_oro", "import-oro.py")             # STB/STL/CHR/ZSC/IFO codecs
kk = load("import_karkia", "import-karkia.py")        # copy_new, effect_chain, index
mon = load("audit_ai_monster_refs", "audit-ai-monster-refs.py")   # .aip codec
fate = load("import_oro_fate", "import-oro-fate.py")  # QSD codec
from mapgen import prefab, zon  # noqa: E402


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


def fixed_with(fixed, warp_id=None, obj_type=None, obj_id=None, map_xy=None, pos=None, scale=None):
    """A 60-byte object header with some fields replaced:
    i16 warp, i16 event, i32 type, i32 id, i32 map_x, i32 map_y, f32 quat[4], f32 pos[3], f32 scale[3]."""
    b = bytearray(fixed)
    if warp_id is not None:
        struct.pack_into("<hh", b, 0, warp_id, 0)
    if obj_type is not None:
        struct.pack_into("<i", b, 4, obj_type)
    if obj_id is not None:
        struct.pack_into("<i", b, 8, obj_id)
    if map_xy is not None:
        struct.pack_into("<ii", b, MAP_XY_OFF, *map_xy)
    if pos is not None:
        struct.pack_into("<3f", b, REGEN_POS_OFF, *pos)
    if scale is not None:
        struct.pack_into("<3f", b, REGEN_POS_OFF + 12, *scale)
    return bytes(b)


def chunk_of_world(wx_m, wy_m):
    """(ifo name, local pos cm, (map_x, map_y)) for a world position in metres --
    mapgen/decorate.records_by_chunk's arithmetic."""
    wx, wy = wx_m * 100, wy_m * 100
    cx, cy = int(wx // CHUNK_CM), int(wy // CHUNK_CM)
    lx, ly = wx - cx * CHUNK_CM, wy - cy * CHUNK_CM
    return (f"{cx}_{64 - cy}.IFO", (wx - ORIGIN_CM, wy - ORIGIN_CM, 0.0),
            (int(lx // GRID_CM), 63 - int(ly // GRID_CM)))


# ------------------------------------------------------------------- stage 1
def cave_ifo(path):
    """A source cave .IFO as we ship it: REGEN emptied (stage 3 refills it), the
    exit gate re-pointed at our WARP row."""
    buf, bounds = oro.read_ifo(path)
    repl = {}
    for lt in (oro.LUMP_MOB, oro.LUMP_REGEN):
        off, _ = oro.lump_block(bounds, lt)
        if off is None or buf[off:off + 4] == b"\0\0\0\0":
            continue
        _, trailing = oro.read_lump(buf, bounds, lt)
        repl[lt] = oro.build_object_lump([], trailing)
    objs, trailing = oro.read_lump(buf, bounds, oro.LUMP_WARP)
    if objs:
        for o in objs:
            wid, = struct.unpack_from("<h", o["fixed"], 0)
            if wid != SRC_WARP_OUT:
                raise SystemExit(f"{path}: gate names RoseZA warp row {wid}, which has no mapping")
            o["fixed"] = fixed_with(o["fixed"], warp_id=WARP_OUT)
        repl[oro.LUMP_WARP] = oro.build_object_lump(objs, trailing)
    return oro.build_ifo(bounds, buf, repl) if repl else buf


def stage1(ours, src, src_index, dry):
    print("stage 1 -- map, art, zone row, warps")
    s_map, d_map = P(src, MAPS_REL), P(ours, MAPS_REL)
    copied = rewritten = 0
    new_dds = []
    for base, _, names in os.walk(s_map):
        sub = os.path.relpath(base, s_map)
        dbase = d_map if sub == "." else os.path.join(d_map, sub)
        for name in sorted(names):
            sp, dp = os.path.join(base, name), os.path.join(dbase, name)
            if os.path.isfile(dp):
                continue
            blob = None
            if name.lower().endswith(".ifo"):
                blob = cave_ifo(sp)
                rewritten += blob != open(sp, "rb").read()
            elif name.lower().endswith(".dds"):
                new_dds.append(os.path.relpath(dp, ours))
            if not dry:
                os.makedirs(dbase, exist_ok=True)
                if blob is None:
                    shutil.copyfile(sp, dp)
                else:
                    with open(dp, "wb") as fh:
                        fh.write(blob)
            copied += 1
    print(f"    {'JZC01':26s} {copied:5d} new  ({rewritten} .IFO rewritten)")
    if kk.zon_with_economy(os.path.join(s_map, ZON_NAME), kk.economy_template(ours)) is not None:
        raise SystemExit(f"{ZON_NAME} lacks a LUMP_ECONOMY; splice it as import-shibuya.py does")

    tiles = {t for t in oro.zon_tiles(os.path.join(s_map, ZON_NAME)) if "\\" in t or "/" in t}
    new_dds += kk.copy_new(tiles, src_index, ours, dry, "terrain tiles")[3]
    art = oro.zsc_asset_refs(oro.Zsc(src_index[kk.key_of(DECO_ZSC_REL)]))
    art = {a for a in art if "\\" in a or "/" in a}
    art |= kk.effect_chain({a for a in art if a.lower().endswith(".eft")}, src_index)
    kk.copy_new([DECO_ZSC_REL], src_index, ours, dry, "object table")
    lifted, n = deco_zsc_with_flames_lifted(src_index)
    if os.path.isfile(P(ours, DECO_ZSC_REL)) or not dry:
        changed = write_if_changed(P(ours, DECO_ZSC_REL), lifted, dry)
        print(f"    {'flames lifted':26s} {n} dummies on {len(FLAME_LIFT)} objects "
              + ("written" if changed else "already in place"))
    new_dds += kk.copy_new(art, src_index, ours, dry, "decoration art")[3]

    # morph object 46 and its files
    s_mo, d_mo = oro.Stb(P(src, MORPH_STB_REL)), oro.Stb(P(ours, MORPH_STB_REL))
    files, wrote = set(), []
    for r in MORPH_ROWS:
        want = [s_mo.get(r, c) for c in range(d_mo.cols)]
        files |= {want[c].decode("latin-1") for c in MORPH_FILE_COLS if want[c].strip()}
        if d_mo.d[r] == want:
            continue
        if d_mo.occupied(r):
            raise SystemExit(f"our LIST_MORPH_OBJECT row {r} is {d_mo.get(r, 0)!r}")
        d_mo.d[r] = want
        wrote.append(r)
    if wrote:
        d_mo.save(dry)
    print(f"    {'LIST_MORPH_OBJECT.STB':26s} rows {wrote or 'already in place'}")
    new_dds += kk.copy_new(files, src_index, ours, dry, "morph art")[3]

    # the zone row and its name
    s_z, d_z = oro.Stb(P(src, ZONE_STB_REL)), oro.Stb(P(ours, ZONE_STB_REL))
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
        stl.append(ZONE_STL_KEY, int(ZONE_STL_KEY[4:]), ZONE_NAME)
        stl.save(dry)
        print(f"    {'LIST_ZONE_S.STL':26s} +1 key {ZONE_STL_KEY}")

    # the way out
    w = oro.Stb(P(ours, WARP_STB_REL))
    wrote = []
    for row, cells in sorted(WARP_ROWS.items()):
        want = [c.encode("latin-1") for c in cells]
        cur = [w.get(row, c) for c in range(3)]
        if cur == want:
            continue
        if any(x.strip() for x in cur):
            raise SystemExit(f"our WARP.STB row {row} is {cur[0]!r}")
        for c, v in enumerate(want):
            w.set(row, c, v)
        wrote.append(row)
    # the first import's free gate goes: its row, and its object at the mouth
    if w.get(RETIRED_WARP_IN, 0) == RETIRED_WARP_LABEL:
        for c in range(w.cols):
            w.set(RETIRED_WARP_IN, c, b"")
        wrote.append(f"-{RETIRED_WARP_IN}")
    if wrote:
        w.save(dry)
    print(f"    {'WARP.STB':26s} rows {wrote or 'already in place'}")
    p = find_ci(P(ours, JD04_REL), JD04_GATE_IFO)
    buf, bounds = oro.read_ifo(p)
    warps, trailing = oro.read_lump(buf, bounds, oro.LUMP_WARP)
    keep = [o for o in warps or [] if struct.unpack_from("<h", o["fixed"], 0)[0] != RETIRED_WARP_IN]
    if warps is not None and len(keep) != len(warps):
        write_if_changed(p, oro.build_ifo(bounds, buf, {oro.LUMP_WARP: oro.build_object_lump(keep, trailing)}), dry)
        print(f"    {'JD04/' + JD04_GATE_IFO:26s} - free gate {RETIRED_WARP_IN} (the toll replaces it)")

    # its landing point in JD04.ZON
    zp = find_ci(P(ours, JD04_REL), "JD04.ZON")
    ours_zon = zon.parse(open(zp, "rb").read())
    events = ours_zon.lump(zon.EVENTS)
    if any(e.name == JD04_LANDING.encode() for e in events):
        print(f"    {'JD04.ZON':26s} {JD04_LANDING} already in place")
    else:
        ref = [e for e in zon.parse(open(find_ci(P(src, JD04_REL), "JD04.ZON"), "rb").read()).lump(zon.EVENTS)
               if e.name == JD04_LANDING.encode()]
        if len(ref) != 1:
            raise SystemExit(f"source JD04.ZON: {len(ref)} events named {JD04_LANDING}")
        events.append(ref[0])
        write_if_changed(zp, zon.build(ours_zon), dry)
        print(f"    {'JD04.ZON':26s} + {JD04_LANDING} at ({ref[0].x / 100 + 5200:.1f}, "
              f"{ref[0].y / 100 + 5200:.1f}) m")

    # BGM lives outside data/, in the deployed game folder
    bgm_d = os.path.join(BGM_DEPLOY_DIR, os.path.basename(BGM_SRC))
    if not (os.path.isdir(BGM_DEPLOY_DIR) and os.path.isfile(BGM_SRC)):
        print(f"    NOTE: copy {BGM_SRC} into the deployed game's Sound\\BGM by hand")
    elif os.path.isfile(bgm_d):
        print(f"    {'BGM':26s} {os.path.basename(BGM_SRC)} already deployed")
    else:
        if not dry:
            shutil.copyfile(BGM_SRC, bgm_d)
        print(f"    {'BGM':26s} {os.path.basename(BGM_SRC)} -> {BGM_DEPLOY_DIR}")

    if new_dds:
        dirs = sorted({os.path.dirname(d).replace("\\", "/") for d in new_dds})
        print(f"\n    NOTE: {len(new_dds)} new .dds, maybe without mip chains. Check with")
        print("          scripts/add-dds-mipmaps.py --dry-run --subdir <dir> for each of:")
        for d in dirs:
            print(f"            {d}")


def deco_zsc_with_flames_lifted(src_index):
    """RoseZA's LIST_DECO_JZC.ZSC with the FLAME_LIFT dummies raised: (bytes, count)."""
    z = oro.Zsc(src_index[kk.key_of(DECO_ZSC_REL)])
    body, n = [], 0
    for i, (cyl, parts, dummies, bb) in enumerate(z.objects):
        if i in FLAME_LIFT:
            new = []
            for a, props in dummies:
                idx, _typ = struct.unpack("<hh", a)
                eft = z.effects[idx].lower() if 0 <= idx < len(z.effects) else b""
                if eft.endswith(FLAME_EFFECTS):
                    props, hit = lift_pos_tag(props, FLAME_LIFT[i])
                    n += hit
                new.append((a, props))
            dummies = new
        body.append(z.object_bytes(cyl, parts, dummies, bb))
    blob = z.d[:z.objcnt_pos + 2] + b"".join(body)
    if len(blob) != len(z.d):
        raise SystemExit("flame lift changed the table's size")
    return blob, n


def lift_pos_tag(props, dz):
    """A dummy's property bytes with its position tag's z raised by dz cm."""
    b, o = bytearray(props), 0
    while o < len(b) and b[o]:
        tag, ln = b[o], b[o + 1]
        if tag == ZSC_TAG_POS and ln == 12:
            x, y, zz = struct.unpack_from("<3f", b, o + 2)
            struct.pack_into("<3f", b, o + 2, x, y, zz + dz)
            return bytes(b), 1
        o += 2 + ln
    return props, 0


def zone_row(s_z, d_z):
    row = [s_z.get(SRC_ZONE, c) for c in range(ZONE_COPY_COLS)]
    row += list(d_z.d[ZONE][ZONE_COPY_COLS:])
    row[ZONE_NAME_COL] = ZONE_NAME.encode()
    row[ZONE_JOIN_COL] = b""
    row[ZONE_STL_COL] = ZONE_STL_KEY.encode()
    for c, v in zip((31, 32, 33), ZONE_REVIVE):
        row[c] = v.encode()
    return row


# ------------------------------------------------------------------- stage 2
def ai_type(entry):
    return struct.unpack_from("<I", entry, 4)[0] & 0xFF


def build_aip(src_path, remotion):
    """Our version of one cave AI file, from the pristine source: (bytes, notes)."""
    header, title, pats, tail = mon.parse_aip(open(src_path, "rb").read())
    notes = collections.Counter()
    out = []
    for pname, evs in pats:
        kept = []
        for ename, conds, acts in evs:
            if any(ai_type(c) == AI_SELECT_NPC for c in conds):
                notes["cut instance event"] += 1
                continue
            new_acts = []
            for a in acts:
                if ai_type(a) == AI_SAY:
                    notes["cut speech"] += 1
                    continue
                if ai_type(a) == AI_CAST:
                    motion, = struct.unpack_from("<h", a, CAST_MOTION_OFF)
                    if motion in remotion:
                        a = a[:CAST_MOTION_OFF] + struct.pack("<h", remotion[motion]) + a[CAST_MOTION_OFF + 2:]
                        notes[f"cast motion {motion} -> {remotion[motion]}"] += 1
                new_acts.append(a)
            if acts and not new_acts:
                # an event left with no action still matches, and a pattern's
                # events are first-match-wins: it would starve the ones after it
                continue
            kept.append((ename, conds, new_acts))
        out.append((pname, kept))
    return mon.build_aip(header, title, out, tail), notes


def npc_row(s_npc, d_npc, i, before):
    name, *stats = MONSTERS[i]
    row = [s_npc.get(i, c) for c in range(oro.NPC_COPY_COLS)] + list(before[oro.NPC_COPY_COLS:])
    row[0] = name.encode()
    for c, v in zip(STAT_COLS, stats):
        row[c] = str(v).encode()
    if before[0].strip():
        row[NPC_EXP_COL] = before[NPC_EXP_COL]     # rebalance-exp-rewards.py owns it
    for c in oro.NPC_SELL_TAB_COLS:
        row[c] = b""
    money = MONEY_BOSS if i in BOSSES else MONEY_FIELD
    for c, v in zip(NPC_DROP_COLS, (MONSTER_DROPS[i], money, DROP_RATE)):
        row[c] = str(v).encode()
    row[NPC_DEAD_EVENT_COL] = b""
    row[oro.NPC_STRID_COL] = f"{NPC_STRID_PREFIX}{i}".encode()
    row[oro.NPC_PVP_COL] = oro.DEFAULT_PVP_STATE
    ai_src = int(s_npc.get(i, oro.NPC_AI_COL) or 0)
    if ai_src not in MONSTER_AI:
        raise SystemExit(f"{name} ({i}) uses RoseZA AI row {ai_src}, which MONSTER_AI does not cover")
    return row


def stage2(ours, src, src_index, dry):
    print("stage 2 -- the monsters")
    ids = sorted(MONSTERS)
    s_npc, d_npc = oro.Stb(P(src, NPC_STB_REL)), oro.Stb(P(ours, NPC_STB_REL))
    written, fresh = [], []
    for i in ids:
        cur = d_npc.get(i, 0).decode("latin-1").strip()
        if cur and cur != MONSTERS[i][0]:
            raise SystemExit(f"our LIST_NPC row {i} is {cur!r}")
        if not cur:
            fresh.append(i)
        before = list(d_npc.d[i])
        d_npc.d[i] = npc_row(s_npc, d_npc, i, before)
        if d_npc.d[i] != before:
            written.append(i)
    print(f"    {'LIST_NPC.STB':26s} rows {written or 'already in place'}")

    stl = oro.Stl(P(ours, NPC_STL_REL))
    names = [i for i in ids if not stl.has(f"{NPC_STRID_PREFIX}{i}")]
    for i in names:
        stl.append(f"{NPC_STRID_PREFIX}{i}", i, MONSTERS[i][0])
    print(f"    {'LIST_NPC_S.STL':26s} +{len(names)} keys")

    s_ai, d_ai = oro.Stb(P(src, AI_STB_REL)), oro.Stb(P(ours, AI_STB_REL))
    ai_rows = []
    for row, fname in sorted(MONSTER_AI.items()):
        cell = s_ai.get(row, 0)
        if os.path.basename(cell.decode("latin-1").replace("\\", "/")).lower() != fname.lower():
            raise SystemExit(f"source FILE_AI row {row} is {cell!r}, expected {fname}")
        if d_ai.get(row, 0) != cell:
            if d_ai.get(row, 0).strip():
                raise SystemExit(f"our FILE_AI row {row} is {d_ai.get(row, 0)!r}")
            d_ai.set(row, 0, cell)
            ai_rows.append(row)
    print(f"    {'FILE_AI.STB':26s} rows {ai_rows or 'already in place'}")
    for row, fname in sorted(MONSTER_AI.items()):
        remotion = BOSS_REMOTION if fname in BOSS_AI else {}
        blob, notes = build_aip(os.path.join(P(src, AI_DIR_REL), fname), remotion)
        changed = write_if_changed(os.path.join(P(ours, AI_DIR_REL), fname), blob, dry)
        summary = ", ".join(f"{k} x{v}" for k, v in sorted(notes.items())) or "verbatim"
        print(f"    {fname:26s} {'written' if changed else 'in place'}: {summary}")

    d_npc.save(dry)
    d_ai.save(dry)
    if names:
        stl.save(dry)

    # models: clear orphan CHR slots on rows created now, then import
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    cleared = [i for i in fresh if i < len(chr_.chars) and chr_.chars[i] is not None]
    for i in cleared:
        chr_.chars[i] = None
    if cleared:
        chr_.save(dry)
        print(f"    {'LIST_NPC.CHR':26s} cleared orphan entries {cleared}")
    oro.import_characters(ids, ours, src, dry, "monster")


# ------------------------------------------------------------------- stage 3
def regen_extra(point_name, basic, tactics, interval, cap, rng, tactic):
    """CRegenPOINT::Load: point name, basic and tactics lists of (mob name, npc,
    count), interval (s), limitCNT, range (m), tacticPoint."""
    out = oro.put_bstr(point_name.encode("latin-1"))
    for lst in (basic, tactics):
        out += struct.pack("<i", len(lst))
        for npc in lst:
            out += oro.put_bstr(b"") + struct.pack("<ii", npc, 1)
    return out + struct.pack("<4i", interval, cap, rng, tactic)


def plan_spawns(src):
    """{ifo name: (objs, trailing)} for every cave chunk: the camps and the King."""
    s_map = P(src, MAPS_REL)
    per, pts = {}, []
    for name in ifo_files(s_map):
        buf, bounds = oro.read_ifo(os.path.join(s_map, name))
        objs, trailing = oro.read_lump(buf, bounds, oro.LUMP_REGEN)
        if objs is None:
            continue
        per[name] = ([], trailing)
        for o in objs:
            x, y, _z = struct.unpack_from("<3f", o["fixed"], REGEN_POS_OFF)
            pts.append((name, x, y, o))
    if not pts:
        raise SystemExit("the source cave has no regen points")

    bosses = []
    for b in BOSS_POINTS:
        bname, bpos, bmap = chunk_of_world(*b["world"])
        if bname not in per:
            raise SystemExit(f"{b['name']}'s chunk {bname} has no REGEN lump")
        tmpl = min(pts, key=lambda p: (p[0] != bname, p[0]))[3]
        obj = dict(tmpl)
        obj["fixed"] = fixed_with(tmpl["fixed"], map_xy=bmap, pos=bpos)
        obj["extra"] = regen_extra(b["name"], [b["npc"]], [], b["interval"], 1, b["range"],
                                   CAMP["tactic"])
        per[bname][0].append(obj)
        bosses.append((bpos, b["clear"], f"{b['name']} in {bname}"))

    near_boss = lambda p: any(math.hypot(p[1] - pos[0], p[2] - pos[1]) < clear * 100
                              for pos, clear, _ in bosses)
    bins = collections.defaultdict(list)
    for p in pts:
        if not near_boss(p):
            bins[(int(p[1] // CAMP_CELL), int(p[2] // CAMP_CELL))].append(p)
    n = 0
    for _cell, members in sorted(bins.items()):
        cx = sum(p[1] for p in members) / len(members)
        cy = sum(p[2] for p in members) / len(members)
        name, _x, _y, o = min(members, key=lambda p: ((p[1] - cx) ** 2 + (p[2] - cy) ** 2, p[0]))
        n += 1
        camp = dict(o)
        camp["extra"] = regen_extra(f"UV-{n:02d}", CAMP_BASIC, CAMP_TACTICS, CAMP["interval"],
                                    CAMP["cap"], CAMP["range"], CAMP["tactic"])
        per[name][0].append(camp)
    return per, n, len(pts), ", ".join(label for _, _, label in bosses)


def fill_regen(ours, per, dry):
    d_map = P(ours, MAPS_REL)
    files = recs = 0
    for name, (objs, trailing) in sorted(per.items()):
        dp = os.path.join(d_map, name)
        if not os.path.isfile(dp):
            if dry:
                files += 1
                recs += len(objs)
                continue
            raise SystemExit(f"{dp}: run --stage 1 first")
        buf, bounds = oro.read_ifo(dp)
        off, end = oro.lump_block(bounds, oro.LUMP_REGEN)
        blob = oro.build_object_lump(objs, trailing)
        if buf[off:end] == blob:
            continue
        files += 1
        recs += len(objs)
        if not dry:
            with open(dp, "wb") as fh:
                fh.write(oro.build_ifo(bounds, buf, {oro.LUMP_REGEN: blob}))
    return files, recs


def stage3(ours, src, src_index, dry):
    print("stage 3 -- spawns")
    per, camps, authored, bosses = plan_spawns(src)
    print(f"    {'camps':26s} {camps} camps of {CAMP['cap']} from {authored} authored points")
    print(f"    {'bosses':26s} {bosses}")
    files, recs = fill_regen(ours, per, dry)
    print(f"    {'IFO regen lumps':26s} {recs} records into {files} files"
          + ("" if files else " (already in place)"))


# ------------------------------------------------------------------- stage 4
dropkit = load("add_karkia_drops", "add-karkia-drops.py")   # build_row / encode


def plan_drops():
    rows = {r: (label, dropkit.build_row(common, groups))
            for r, (label, (common, groups)) in DROP_TABLES.items()}
    rows[ZONE] = (f"{ZONE_NAME} zone roll (mirrors {ZONE_DROP_MIRROR})",
                  dict(rows[ZONE_DROP_MIRROR][1]))
    return rows


def drop_row_cells(cols, label, cells):
    row = [b""] * cols
    row[0] = label.encode("latin-1")
    for slot, v in cells.items():
        row[1 + slot] = str(v).encode("latin-1")
    return row


def item_names(ours):
    """{(type, no): name} for every item the tables name; exits on a blank row."""
    out, stbs = {}, {}
    for _label, (common, groups) in DROP_TABLES.values():
        for t, no in [e for e in common if e[0] != "redirect"] + [e for g in groups.values() for e in g]:
            stb = stbs.setdefault(t, oro.Stb(P(ours, rf"3DDATA\STB\{ITEM_STB[t]}.STB")))
            if not stb.occupied(no):
                raise SystemExit(f"{ITEM_STB[t]} row {no} is blank -- a drop would hand out nothing")
            out[(t, no)] = stb.get(no, 0).decode("latin-1").strip() or f"{ITEM_STB[t]} {no}"
    return out


def stage4(ours, src, src_index, dry):
    print("stage 4 -- loot")
    names = item_names(ours)
    for label, (common, groups) in DROP_TABLES.values():
        listed = [e for e in common if e[0] != "redirect"] + [e for g in groups.values() for e in g]
        print(f"    {label}: " + ", ".join(names[e] for e in listed))
    d = oro.Stb(P(ours, DROP_STB_REL))
    wrote = []
    for r, (label, cells) in sorted(plan_drops().items()):
        want = drop_row_cells(d.cols, label, cells)
        if d.d[r] == want:
            continue
        ours_label = d.get(r, 0) == label.encode("latin-1")
        free = not any(x.strip() for x in d.d[r][1:]) and d.get(r, 0) in ZONE_DROP_LABELS
        if not (free or ours_label):
            raise SystemExit(f"ITEM_DROP row {r} holds a table we did not author: {d.get(r, 0)!r}")
        d.d[r] = want
        wrote.append(r)
    if wrote:
        d.save(dry)
    print(f"    {'ITEM_DROP.STB':26s} rows {wrote or 'already in place'}")


# ------------------------------------------------------------------- stage 5
def toll_trigger():
    """The toll as one QSD trigger: conditions = the price, rewards = take it, warp.
    Entity layouts are io_quest.h's, checked against retail 2051-19 (QP201.QSD)."""
    items = [(t * 1000 + no, TOLL_WHERE, n, OP_GE) for t, no, n in TOLL]
    pl = struct.pack("<i", len(items)) + b"".join(struct.pack("<IiiB3x", *it) for it in items)
    cond = struct.pack("<Ii", 8 + len(pl), 4) + pl
    take = [struct.pack("<IiIBxhB3x", 20, 0x01000001, t * 1000 + no, OP_TAKE, n, 0) for t, no, n in TOLL]
    x, y = cave_start_cm()
    warp = struct.pack("<IiiiiB3x", 24, 0x01000007, ZONE, x, y, 0)
    return fate.qsd_build_trigger(TOLL_TRIGGER, [cond], take + [warp])


def cave_start_cm():
    ev = zon.parse(open(find_ci(P(DATA_ROOT, MAPS_REL), ZON_NAME), "rb").read()).lump(zon.EVENTS)
    hit = [e for e in ev if e.name == CAVE_START.encode()]
    if len(hit) != 1:
        raise SystemExit(f"{ZON_NAME}: {len(hit)} '{CAVE_START}' events")
    return int(round(hit[0].x + ORIGIN_CM)), int(round(hit[0].y + ORIGIN_CM))


def gatekeeper_row(n):
    row = list(n.d[GATEKEEPER_TEMPLATE])
    row[0] = GATEKEEPER_NAME.encode("latin-1")
    row[oro.NPC_STRID_COL] = GATEKEEPER_STRID.encode()
    return row


def event_row(e):
    return [GATEKEEPER_NAME.encode("latin-1"), e.get(EVENT_ROW - 1, 1), b"NPC",
            rf"3Ddata\Event\{GATEKEEPER_CON}".encode()]


def gatekeeper_placement(ours):
    """A MOB record for the cave mouth, on Harry's own record's shape."""
    tmpl = None
    for name in ifo_files(P(ours, JD04_REL)):
        buf, bounds = oro.read_ifo(os.path.join(P(ours, JD04_REL), name))
        for o in oro.read_lump(buf, bounds, oro.LUMP_MOB)[0] or []:
            if struct.unpack_from("<i", o["fixed"], 8)[0] == GATEKEEPER_TEMPLATE:
                tmpl = o
    if tmpl is None:
        raise SystemExit(f"NPC {GATEKEEPER_TEMPLATE} is not placed in JD04")
    fname, pos, map_xy = chunk_of_world(*GATEKEEPER_POS)
    if fname.lower() != JD04_GATE_IFO.lower():
        raise SystemExit(f"GATEKEEPER_POS lands in {fname}, not {JD04_GATE_IFO}")
    field, (x0, y0) = prefab._zone_field(P(ours, JD04_REL))
    z = prefab._height(field, x0, y0, GATEKEEPER_POS[0] * 100, GATEKEEPER_POS[1] * 100)
    obj = dict(tmpl)
    obj["fixed"] = fixed_with(tmpl["fixed"], obj_id=GATEKEEPER, map_xy=map_xy, pos=(pos[0], pos[1], z))
    obj["extra"] = struct.pack("<i", 0) + oro.put_bstr(GATEKEEPER_CON.encode("latin-1"))
    obj["obj_id"] = GATEKEEPER
    return obj


def stage5(ours, src, src_index, dry):
    print("stage 5 -- the gatekeeper and his toll")
    # 5a. the toll trigger
    qp = P(ours, QSD_REL)
    blob = open(qp, "rb").read()
    want = toll_trigger()
    have = fate.qsd_trigger_bytes(blob, TOLL_TRIGGER)
    if have is None:
        qdir = P(ours, r"3DDATA\QUESTDATA")
        for f in os.listdir(qdir):
            if f.lower().endswith(".qsd") and f.lower() != os.path.basename(qp).lower():
                if fate.qsd_has_trigger(open(os.path.join(qdir, f), "rb").read(), TOLL_TRIGGER):
                    raise SystemExit(f"{TOLL_TRIGGER} already exists in {f} (names are global)")
        out = fate.qsd_append_pattern(blob, TOLL_PATTERN, [want])
    elif have != want:
        out = blob.replace(have, want, 1)
    else:
        out = blob
    ok, _ = fate.qsd_parse_ok(out)
    if not ok or fate.qsd_trigger_bytes(out, TOLL_TRIGGER) != want:
        raise SystemExit("toll trigger did not round-trip")
    if out != blob and not dry:
        oro.backup(qp)
        with open(qp, "wb") as fh:
            fh.write(out)
    x, y = cave_start_cm()
    print(f"    {'QP401.QSD':26s} {TOLL_TRIGGER}: "
          + " + ".join(f"{n} x {t}:{no}" for t, no, n in TOLL)
          + f" -> zone {ZONE} ({x}, {y}) "
          + ("already in place" if out == blob else "written"))

    # 5b. the NPC: row, name, model, event row
    n = oro.Stb(P(ours, NPC_STB_REL))
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

    # 5c. the dialog
    if not os.path.isfile(QUEST_EDITOR):
        raise SystemExit(f"{QUEST_EDITOR} missing: build it (cargo build --release -p quest-editor)")
    cmd = [QUEST_EDITOR, "con-toll", ours, GATEKEEPER_CON, TOLL_KEY, TOLL_TRIGGER]
    for k, v in TOLL_TEXT.items():
        cmd += [k, v]
    if not dry:
        cmd.append("--write")
    r = subprocess.run(cmd, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"quest-editor con-toll failed:\n{r.stdout}\n{r.stderr}")
    for line in r.stdout.splitlines():
        if line.strip().startswith(("CREATE", "REWRITE", "UNCHANGED", "UPSERT")):
            print(f"    {'dialog':26s} {line.strip()[:110]}")

    # 5d. the placement at the cave mouth
    p = find_ci(P(ours, JD04_REL), JD04_GATE_IFO)
    buf, bounds = oro.read_ifo(p)
    mobs, trailing = oro.read_lump(buf, bounds, oro.LUMP_MOB)
    if mobs is None:
        raise SystemExit(f"{p}: no MOB lump")
    want_obj = gatekeeper_placement(ours)
    keep = [o for o in mobs if struct.unpack_from("<i", o["fixed"], 8)[0] != GATEKEEPER]
    new = keep + [want_obj]
    blob = oro.build_object_lump(new, trailing)
    off, end = oro.lump_block(bounds, oro.LUMP_MOB)
    if buf[off:end] == blob:
        print(f"    {'JD04/' + JD04_GATE_IFO:26s} gatekeeper already placed")
    else:
        write_if_changed(p, oro.build_ifo(bounds, buf, {oro.LUMP_MOB: blob}), dry)
        print(f"    {'JD04/' + JD04_GATE_IFO:26s} + {GATEKEEPER_NAME} at {GATEKEEPER_POS} m")
    sweep_event_baks(ours, dry)


def sweep_event_baks(ours, dry):
    """quest-editor leaves `<file>.bak` beside what it writes; move them out."""
    ev = P(ours, EVENT_DIR_REL)
    for f in sorted(os.listdir(ev)):
        if f.lower().endswith(".bak") and not dry:
            dest = os.path.join(BUILD_DIR, "bak", "3DDATA", "EVENT", f)
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            if os.path.isfile(dest):
                os.remove(os.path.join(ev, f))
            else:
                shutil.move(os.path.join(ev, f), dest)


# --------------------------------------------------------------------- verify
def verify(ours, src):
    bad = 0

    def check(ok, what):
        nonlocal bad
        bad += not ok
        print(f"    {'ok  ' if ok else 'FAIL'} {what}")

    print("verify")
    s_map, d_map = P(src, MAPS_REL), P(ours, MAPS_REL)
    check(os.path.isdir(d_map) and all(os.path.isfile(os.path.join(d_map, f)) for f in ifo_files(s_map)),
          "cave map folder")
    zp = P(ours, DECO_ZSC_REL)
    check(os.path.isfile(zp) and open(zp, "rb").read()
          == deco_zsc_with_flames_lifted(kk.index_tree(src))[0], "LIST_DECO_JZC.ZSC (flames lifted)")
    z = oro.Stb(P(ours, ZONE_STB_REL))
    check(z.d[ZONE] == zone_row(oro.Stb(P(src, ZONE_STB_REL)), z), f"LIST_ZONE row {ZONE}")
    check(oro.Stl(P(ours, ZONE_STL_REL)).has(ZONE_STL_KEY), ZONE_STL_KEY)
    w = oro.Stb(P(ours, WARP_STB_REL))
    check(all([w.get(r, c) for c in range(3)] == [x.encode() for x in v] for r, v in WARP_ROWS.items()),
          f"WARP rows {sorted(WARP_ROWS)}")
    buf, bounds = oro.read_ifo(find_ci(P(ours, JD04_REL), JD04_GATE_IFO))
    warps, _ = oro.read_lump(buf, bounds, oro.LUMP_WARP)
    check(not any(struct.unpack_from("<h", o["fixed"], 0)[0] == RETIRED_WARP_IN for o in warps or [])
          and w.get(RETIRED_WARP_IN, 0) != RETIRED_WARP_LABEL, "no free gate into the cave")
    ev = zon.parse(open(find_ci(P(ours, JD04_REL), "JD04.ZON"), "rb").read()).lump(zon.EVENTS)
    check(any(e.name == JD04_LANDING.encode() for e in ev), f"JD04.ZON {JD04_LANDING}")
    cave_ev = zon.parse(open(os.path.join(d_map, ZON_NAME), "rb").read()).lump(zon.EVENTS) \
        if os.path.isfile(os.path.join(d_map, ZON_NAME)) else []
    check(any(e.name == CAVE_START.encode() for e in cave_ev), "cave landing point")
    exits = []
    for name in ifo_files(d_map) if os.path.isdir(d_map) else []:
        b, bd = oro.read_ifo(os.path.join(d_map, name))
        exits += [struct.unpack_from("<h", o["fixed"], 0)[0] for o in (oro.read_lump(b, bd, oro.LUMP_WARP)[0] or [])]
    check(exits == [WARP_OUT], f"cave exit gate -> {WARP_OUT} (have {exits})")
    mo_s, mo_d = oro.Stb(P(src, MORPH_STB_REL)), oro.Stb(P(ours, MORPH_STB_REL))
    check(all(mo_d.d[r] == [mo_s.get(r, c) for c in range(mo_d.cols)] for r in MORPH_ROWS), "morph rows")

    npc = oro.Stb(P(ours, NPC_STB_REL))
    s_npc = oro.Stb(P(src, NPC_STB_REL))
    check(all(npc.d[i] == npc_row(s_npc, npc, i, npc.d[i]) for i in MONSTERS), f"LIST_NPC {sorted(MONSTERS)}")
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    check(all(i < len(chr_.chars) and chr_.chars[i] for i in MONSTERS), "CHR entries")
    ai = oro.Stb(P(ours, AI_STB_REL))
    for row, fname in sorted(MONSTER_AI.items()):
        remotion = BOSS_REMOTION if fname in BOSS_AI else {}
        want, _ = build_aip(os.path.join(P(src, AI_DIR_REL), fname), remotion)
        p = os.path.join(P(ours, AI_DIR_REL), fname)
        check(ai.get(row, 0).strip() != b"" and os.path.isfile(p) and open(p, "rb").read() == want,
              f"FILE_AI {row} + {fname}")

    per, _, _, _ = plan_spawns(src)
    same = True
    for name, (objs, trailing) in per.items():
        p = os.path.join(d_map, name)
        if not os.path.isfile(p):
            same = False
            break
        b, bd = oro.read_ifo(p)
        off, end = oro.lump_block(bd, oro.LUMP_REGEN)
        same &= b[off:end] == oro.build_object_lump(objs, trailing)
    check(same, "regen lumps (camps + bosses)")
    d = oro.Stb(P(ours, DROP_STB_REL))
    check(all(d.d[r] == drop_row_cells(d.cols, label, cells) for r, (label, cells) in plan_drops().items()),
          f"ITEM_DROP rows {sorted(plan_drops())}")
    check(fate.qsd_trigger_bytes(open(P(ours, QSD_REL), "rb").read(), TOLL_TRIGGER) == toll_trigger(),
          f"QP401.QSD {TOLL_TRIGGER}")
    check(npc.d[GATEKEEPER] == gatekeeper_row(npc), f"LIST_NPC {GATEKEEPER} {GATEKEEPER_NAME}")
    check(GATEKEEPER < len(chr_.chars) and chr_.chars[GATEKEEPER] == chr_.chars[GATEKEEPER_TEMPLATE],
          f"CHR {GATEKEEPER}")
    check(oro.Stl(P(ours, NPC_STL_REL)).has(GATEKEEPER_STRID), GATEKEEPER_STRID)
    e = oro.Stb(P(ours, EVENT_STB_REL))
    check(e.d[EVENT_ROW] == event_row(e), f"LIST_EVENT {EVENT_ROW}")
    # the .CON's Lua tail is XOR-coded: ask the builder whether a rebuild would change it
    r = subprocess.run([QUEST_EDITOR, "con-toll", ours, GATEKEEPER_CON, TOLL_KEY, TOLL_TRIGGER]
                       + [x for kv in TOLL_TEXT.items() for x in kv], capture_output=True, text=True)
    check(r.returncode == 0 and "UNCHANGED toll gate" in r.stdout, f"{GATEKEEPER_CON} (toll dialog)")
    b, bd = oro.read_ifo(find_ci(P(ours, JD04_REL), JD04_GATE_IFO))
    mobs = oro.read_lump(b, bd, oro.LUMP_MOB)[0] or []
    want = gatekeeper_placement(ours)
    check([(o["name"], o["fixed"], o["extra"]) for o in mobs
           if struct.unpack_from("<i", o["fixed"], 8)[0] == GATEKEEPER]
          == [(want["name"], want["fixed"], want["extra"])], "gatekeeper placed at the cave mouth")
    print("verify " + ("OK" if not bad else f"FAILED ({bad})"))
    return 1 if bad else 0


# ------------------------------------------------------------------- selftest
def selftest(ours, src):
    print("selftest -- every writer must reproduce its input byte for byte")
    s_map = P(src, MAPS_REL)
    n = 0
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
        n += 1
    p = find_ci(P(ours, JD04_REL), JD04_GATE_IFO)
    buf, bounds = oro.read_ifo(p)
    objs, trailing = oro.read_lump(buf, bounds, oro.LUMP_WARP)
    off, end = oro.lump_block(bounds, oro.LUMP_WARP)
    assert oro.build_ifo(bounds, buf, {}) == buf and oro.build_object_lump(objs, trailing) == buf[off:end]
    zp = find_ci(P(ours, JD04_REL), "JD04.ZON")
    assert zon.build(zon.parse(open(zp, "rb").read())) == open(zp, "rb").read(), "JD04.ZON"
    print(f"    {n} cave .IFO/.ZON containers, our JD04 {JD04_GATE_IFO} and JD04.ZON round-trip")
    for rel in (ZONE_STB_REL, NPC_STB_REL, WARP_STB_REL, AI_STB_REL, MORPH_STB_REL, DROP_STB_REL):
        p = P(ours, rel)
        assert oro.Stb(p).to_bytes() == open(p, "rb").read(), rel
    for rel in (ZONE_STL_REL, NPC_STL_REL):
        p = P(ours, rel)
        assert oro.Stl(p).to_bytes() == open(p, "rb").read(), rel
    p = P(ours, NPC_CHR_REL)
    assert oro.Chr(p).to_bytes() == open(p, "rb").read(), "LIST_NPC.CHR"
    print("    LIST_ZONE / LIST_NPC / WARP / FILE_AI / MORPH / ITEM_DROP / two STLs / CHR round-trip")
    for fname in MONSTER_AI.values():
        blob = open(os.path.join(P(src, AI_DIR_REL), fname), "rb").read()
        assert mon.build_aip(*mon.parse_aip(blob)) == blob, fname
    print(f"    {len(MONSTER_AI)} .aip files round-trip")
    print("    selftest OK")


# ---------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--stage", type=int, choices=(1, 2, 3, 4, 5), action="append")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--root", default=ROOT)
    ap.add_argument("--source", default=DEFAULT_SRC)
    args = ap.parse_args()
    ours = os.path.join(args.root, "data")
    src = args.source
    if not os.path.isdir(P(src, MAPS_REL)):
        raise SystemExit(f"source is not a RoseZA client with JZC01: {src}")
    if args.selftest:
        selftest(ours, src)
        return 0
    if args.verify:
        return verify(ours, src)
    if not args.stage:
        ap.error("give --stage N (1-5), --verify or --selftest")
    src_index = kk.index_tree(src)
    for st in sorted(set(args.stage)):
        {1: stage1, 2: stage2, 3: stage3, 4: stage4, 5: stage5}[st](ours, src, src_index, args.dry_run)
        print()
    if args.dry_run:
        print("dry run: nothing written")
    else:
        sweep_baks(ours)
        print("done. Next: add-dds-mipmaps.py (see the stage 1 note), audit-lightmap-index.py")
        print("--zone 34, audit-ai-skill-refs.py, rebake the VFS, restart the servers.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
