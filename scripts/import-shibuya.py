#!/usr/bin/env python3
"""Import SHIBUYA and its live house, Jrose's SECONDWALL collab maps, as zones 126/127.

Jrose zone 126 "SW Shibuya" is a Tokyo street scene -- shops, office blocks, a
"109"-style tower, Christmas trees -- and 127 "Live House SEVEN" is the club the
survivors hole up in. Both come from a 2017 event built around the band
SECONDWALL (YUKA, APG, RYO, YU-SUKE, SHOHEI appear as NPCs): the Deaders, an
undead plague, have overrun SHIBUYA, and the band slipped into the Seven Hearts
through the "Gap Between the Twin Walls" to find help. The first import took
the walk, the people and their story, not the event. The tribute (2026-09-29)
brings the Deaders themselves into the streets, a level 60-80 zone with two
level-100 bosses, and the band's requests as quests of our own (quest-editor
packs, not Jrose's QSDs). Still no dungeon.

Staged like scripts/import-skaaj.py; every stage is idempotent, --dry-run
previews, --verify re-derives the state, --selftest proves every writer
round-trips byte-identically before anything is touched:

    --stage 1   both map folders, terrain tile, object tables and their art,
                the two zone rows and names, the warp pair between them, BGM.
                The copied .IFOs get their MOB and REGEN lumps emptied and
                their warp gates re-pointed at our WARP.STB rows.
    --stage 2   the townsfolk: LIST_NPC rows at their native ids, English
                names, character models (and the Christmas trees' sparkle),
                LIST_EVENT rows, the .CON files, shops, and the placements.
    --stage 3   dialog and travel: English text for every reachable line,
                every quest branch hidden, the SECONDWALL mascot's "go back"
                wired to Junon Polis, and Jones (Junon Polis) given a "take
                me to SHIBUYA" option.
    --stage 4   the Deaders: twelve LIST_NPC rows at their Jrose ids with OUR
                stats (DEADERS), their models, their AI rebuilt from the Jrose
                files with our edits (DEADER_AI), the police zombies' guns
                (import-item.py) and the skill kit (import-monster-skills.py,
                whose SHIBUYA notes explain every power and status). No spawn
                points yet: those are the tribute's Phase 2.

The Deaders (stage 4)
---------------------
  * **Jrose's field rows are placeholders** -- level 1, HP 100, which its event
    rescaled -- and its level-235 copies belong to the dungeon. So every stat is
    ours, from our own table's level medians (the DEADERS comment).
  * **The Rot Tracker is a rare spawn**: any field zombie or police zombie that
    dies can call it (SummonMasterDist, 5 m away). Jrose's rule ANDs three rolls
    into 0.2% and guards with a level window that never matches; ours is one 5%
    roll and "no ally 21-45 levels above me within 60 m", i.e. no boss already
    near. Its own despawn rule held an age of -1200 s, which made it vanish the
    moment a player stepped out of 40 m; it now lingers for at least 120 s.
  * **Casts we cannot run are stripped, and events left empty are dropped** (a
    pattern's events are first-match-wins, so an empty event would starve the
    next): 3691 (a "leave N HP" formula we lack), 3656 (a heal on cast target 3,
    unimplemented), 6141 (damage cast on the caster itself), 3780 (our Karkia
    Stun, far too strong here). Four more are re-pointed at our tail
    (CAST_REMAP) because their ids are other skills here.
  * **Bare-handed rows get a hit effect** (403, the dog 453): Jrose left them
    blank, which lands a hit with no visible impact. Jrose's quest triggers in
    the death-event column are blanked -- the quest-editor would read one as a
    quest to chain onto.
  * **Drops are blank until Phase 2**, which also has to move the legacy tables
    squatting on drop-table ids 126/127 (a drop table and a zone share ids).
  * EXP is provisional; re-run rebalance-exp-rewards.py once the monsters spawn.

Why this is a port and not a copy
---------------------------------
  * **Zones 126/127 and LZON126/127 are free here**, so both keep their Jrose
    numbers (below the gameserver's zone ceiling of 250). Zone 128, "SHIBUYA
    782", is an instanced dungeon (Jrose's shared `dungeon.zon` plus
    SW109.STB) and zone 36 a separate outdoor concert stage; neither is
    imported.

  * **Shibuya's 108 regen points are map-editor templates**: named
    02m/10m/15m and every slot is NPC 1, the Mini-Jelly Bean, at caps of up to
    30. The event's monsters lived in the dungeon. The REGEN lumps are emptied.

  * **Jrose's warp rows 176/177 are Oro's Golden Ring gates here.** The pair
    lands on 182/183 (free) and the one gate object in each map is
    re-pointed; the destination event names (WARP_126127 / WARP_127126) are in
    the source .ZONs and copy over unchanged.

  * **Shibuya ships no lightmaps at all** -- no per-chunk folder. The objects
    load without one (LoadLightMapINFO returns false quietly) and the terrain
    falls back to 3DDATA\\TERRAIN\\default_light.dds, which we ship. The live
    house has its lightmaps and they are copied.

  * **The .ZONs are the stripped late-Jrose variant with no LUMP_ECONOMY**;
    spliced in exactly as import-karkia.py does it.

  * **A MOB placement names its dialog without an extension** ("EM02-089"),
    and the gameserver matches it against the LIST_EVENT cell's basename
    exactly, so it is rewritten to "EM02-089.con" -- the Skaaj canon_con rule.

  * **Three live-house NPCs are dropped**: Kaijiro (4054, a shield crafter
    working from Jrose materials), Kuuichiro (4083, Jrose master armour
    upgrades) and Nicola (4100, Jrose saint-item trades). Nothing they do
    exists here.

  * **Every quest branch is hidden, and every quest click is a no-op.** The
    whole cast is a 30-quest chain (Q5456-Q5487) we do not ship. As with
    Skaaj, "a trigger we do not ship never passes" is not a safe assumption --
    some Jrose checks are inverted -- so every node with a check function is
    re-pointed at TA_Hidden (defined in the QEX1 appendix to return 0) except
    the one branch per NPC we choose to show, whose check is cleared. Every
    AT_* click function in each file is redefined in the appendix (which runs
    after the main blob, so the later definition wins): services get our own
    bodies, the mascot's "go back" fires our warp trigger, and everything else
    does nothing -- no Jrose trigger name is ever sent to the server.

  * **What each NPC shows** (the SHOW table): the five band members and Kanna
    tell their intro -- the outbreak, the band's escape into the Seven Hearts,
    a request to thin out the Deaders, translated faithfully even though no
    Deader spawns here (it is an easter egg, and the story is theirs).
    Shimasaburo keeps his services (shop, repair, refine) and his account of
    the day the Deaders came; Sarasa keeps the storage; Juri her stall. Their
    Jrose shop tabs sold Jrose goods, so Shimasaburo gets Junon Polis' Guns
    tab (guns and ammunition -- "need bullets?") plus potions, and Juri food
    plus potions.

After running: add-dds-mipmaps.py over the folders stage 1 lists (Jrose ships
flat textures), then `add-dds-mipmaps.py --subdir 3DDATA/MAPS/JUNON/SW_LIVE
--lightmaps` for the live house's 13 object-lightmap atlases, which the default
pass skips -- without it each one logs `slow texture create` (~11 ms) on entry,
the same scoped opt-in Karkia, Oro and Skaaj got. Then
`fix-coplanar-object-overlaps.py --zone 127 --only D48`: the neon over the
live-house door (SWL_Neon01) is authored flush with the room shell's wall and
partly 1-2 cm inside a partition, so it flickered; one record moves 3.5 cm
into the room. (The zone's other 13 coplanar pairs are left as shipped.)
Rebake the VFS, restart the servers, ship client and data together (QEX1).

Usage:
    python scripts/import-shibuya.py --selftest
    python scripts/import-shibuya.py --stage 1 --stage 2 --stage 3 --dry-run
    python scripts/import-shibuya.py --stage 1 --stage 2 --stage 3
    python scripts/import-shibuya.py --verify
"""
import argparse
import importlib.util
import os
import re
import shutil
import struct
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

# --------------------------------------------------------------------- config
DEFAULT_SRC = r"C:\Users\Thomas\Desktop\Testclients\Jrose"
BUILD_DIR = os.path.join(ROOT, "build", "shibuya")

# Our zone row == Jrose's. maps: the map folder; zon: its .ZON; zsc: the
# decoration and building tables; bgm: what the row's BGM columns name.
ZONES = {
    126: dict(name="SHIBUYA", stl="LZON126",
              maps=r"3DDATA\MAPS\JUNON\SW_SBY", zon="SW_SBY.ZON",
              zsc=[r"3DDATA\JUNON\LIST_DECO_SW_SBY.ZSC", r"3DDATA\JUNON\LIST_CNST_SW_SBY.ZSC"],
              bgm=r"Sound\BGM\geb_Desert.ogg", ifos=49),
    127: dict(name="Live House SEVEN", stl="LZON127",
              maps=r"3DDATA\MAPS\JUNON\SW_LIVE", zon="SW_LIVE.ZON",
              zsc=[r"3DDATA\JUNON\LIST_DECO_SW_Live.ZSC", r"3DDATA\JUNON\LIST_CNST_SW_Live.ZSC"],
              bgm=r"Sound\BGM\SW_Endroll.ogg", ifos=1),
}
ZONE_STB_REL = r"3DDATA\STB\LIST_ZONE.STB"
ZONE_STL_REL = r"3DDATA\STB\LIST_ZONE_S.STL"
ZONE_COPY_COLS = 36                # Jrose has a 37th, its world-map number
ZONE_NAME_COL, ZONE_STL_COL = 0, 26
BGM_DEPLOY_DIR = r"C:\Users\Thomas\Desktop\ROSEProject\Sound\BGM"

# Jrose warp row -> (our row, label). Jrose's 176/177 are Oro gates here.
WARP_STB_REL = r"3DDATA\STB\WARP.STB"
WARPS = {
    176: (182, "SW_SBY -> SW_LIVE (SHIBUYA -> Live House SEVEN)"),
    177: (183, "SW_LIVE -> SW_SBY (Live House SEVEN -> SHIBUYA)"),
}

NPC_STB_REL = r"3DDATA\STB\LIST_NPC.STB"
NPC_STL_REL = r"3DDATA\STB\LIST_NPC_S.STL"
NPC_CHR_REL = r"3DDATA\NPC\LIST_NPC.CHR"
EVENT_STB_REL = r"3DDATA\STB\LIST_EVENT.STB"
EVENT_FILE_COL = 3
EVENT_DIR_REL = r"3DDATA\EVENT"
LTB_REL = r"3DDATA\EVENT\ulngtb_con.ltb"
QSD_REL = r"3DDATA\QUESTDATA\QP401.QSD"
QSD_TEMPLATE_REL = r"3DDATA\QUESTDATA\PVP10.QSD"
QSD_TEMPLATE_TRIGGER = "PvP10-061"
QUEST_EDITOR = os.path.join(ROOT, "bin", "release", "quest-editor.exe")

# Jrose id -> English name. ASCII only: Stb.set encodes latin-1. The role
# titles are ours; Jrose used "[SW]" and "[SHIBUYA]", which mean nothing here.
NPCS = {
    1226: "Christmas Tree",
    1227: "Christmas Tree",
    4044: "[SECONDWALL] YUKA",
    4045: "[SECONDWALL] APG",
    4046: "[SECONDWALL] RYO",
    4047: "[SECONDWALL] YU-SUKE",
    4048: "[SECONDWALL] SHOHEI",
    4049: "[Mechanic] Shimasaburo",
    4050: "[Survivor] Sarasa",
    4051: "[Shrine Maiden] Kanna",
    4052: "[Shopkeeper] Juri",
    4053: "[SECONDWALL] Mascot",
}
DROP_NPCS = {4054, 4083, 4100}     # crafting/trade on Jrose items (docstring)
NPC_STRID_PREFIX = "LSBNPC"
NPC_PVP_NOPVP = b"0"               # townsfolk: normal cursor, not the attack one
# Shop tabs (LIST_NPC cols 21-24), ours. 490 = Junon Polis Guns (guns + ammo,
# Mildun's), 471 = Potion/Return Scroll, 494 = Food (Harin's tavern).
SHOPS = {4049: (490, 471, 0, 0), 4052: (494, 471, 0, 0)}

# ---- dialog
CON_FILES = [f"EM02-{n:03d}.CON" for n in range(80, 90)]
# (menu, item) nodes whose check is cleared: the one branch each NPC shows.
# Every other node with a check function is pointed at TA_Hidden.
SHOW = {
    "EM02-080.CON": [(0, 1)],          # YUKA: the outbreak, the escape
    "EM02-081.CON": [(0, 1)],          # APG: a safe place for survivors
    "EM02-082.CON": [(0, 1)],          # RYO: the city that was
    "EM02-083.CON": [(0, 1)],          # YU-SUKE: the shelter
    "EM02-084.CON": [(0, 1)],          # SHOHEI: the show that never was
    "EM02-085.CON": [(0, 1), (2, 5)],  # Shimasaburo: services + the day it began
    "EM02-086.CON": [(0, 1)],          # Sarasa: storage
    "EM02-087.CON": [(0, 1)],          # Kanna: the shrine maiden
    "EM02-088.CON": [(0, 1)],          # Juri: her stall
    "EM02-089.CON": [],                # the mascot: ungated already
}
NODE_CHECK_OFF = 12
APPENDIX_BEGIN = "-- SHIBUYA BEGIN\n"
APPENDIX_END = "-- SHIBUYA END\n"
APPENDIX_HEAD = ("-- rose-next: the SECONDWALL quest chain is not shipped; its "
                 "branches are gated on this.\nfunction TA_Hidden() return 0 end\n")
RETURN_TRIGGER = "Shibuya-TravelToJunon"
# Click functions with a body of our own. Every other AT_* a file names is
# redefined to do nothing.
CLICK_BODY = {
    "AT_store": "GF_openStore(QF_getEventOwner(E), 0)",
    "AT_repair": "GF_repair(QF_getEventOwner(E))",
    "AT_upgrade": "GF_openUpgrade(QF_getEventOwner(E))",
    "AT_openBank2": "GF_openBank(QF_getEventOwner(E))",
    "AT_SW_MASCOT_02": f'QF_doQuestTrigger("{RETURN_TRIGGER}")',
}
AT_NAME = re.compile(rb"(?<![A-Za-z0-9_])AT_[A-Za-z0-9_]+")

REWD_007 = 0x01000000 | 7
TRAVEL_TRIGGER = "Shibuya-TravelToShibuya"
# (pattern, trigger, zone) -- a REWD_007 to the zone's own `start`.
TRIGGERS = [
    ("ShibuyaTravel", TRAVEL_TRIGGER, 126),
    ("ShibuyaReturnJunon", RETURN_TRIGGER, 2),
]
TRAVEL_HOSTS = [(1104, "[Historian] Jones")]
TRAVEL_KEY = "shibuya"
TRAVEL_TEXT = {
    "--hook": "They say there's a strange city called SHIBUYA. Can you get me there?",
    "--confirm": "SHIBUYA... a city of another world, reached through the Gap Between "
                 "the Twin Walls. I hear it has fallen on dark days. Shall I send you?",
    "--accept": "Yes, take me to SHIBUYA.",
    "--decline": "Not just yet.",
}

# str_id -> English, written into ulngtb_con.ltb at the source id (every one is
# free on our side; the importer refuses a row another tool owns). Only the
# lines the SHOW branches reach; structural and hidden nodes stay empty.
STRINGS = {
    # EM02-089 the SECONDWALL mascot, in SHIBUYA
    27628: "Ah... Do you want to go back to your own world...?",
    27629: "I'll go back.",
    27630: "I'll stay a while longer.",
    # EM02-080 YUKA
    27505: "Hello. I suppose this is the first time you've seen me like this. "
           "Coming back to SHIBUYA has brought my memories back clearly.",
    27506: "Your memories? Why was only your soul drifting through the Seven Hearts?",
    27507: "You may not believe this, but Deaders appeared in SHIBUYA.",
    27508: "Deaders... You mean the undead?",
    27509: "Yes. The Deaders roamed the streets, attacking anyone still alive... "
           "That was when I happened on the entrance to the \"Gap Between the Twin "
           "Walls\", and went to the Seven Hearts to look for help.",
    27510: "So that's what happened...",
    27511: "And that's where I met you... Please, won't you help this world?",
    27512: "Then the first thing is to make this area safe.",
    27513: "There seem to be a lot of the Deaders we call \"Business Zombies\" around "
           "here. Please be careful. And thank you.",
    27514: "Leave it to me!",
    # EM02-081 APG
    27532: "Ah! You made it to this world! I'm APG. ...It's in a terrible state, isn't it?",
    27533: "Yes, it gave me quite a shock.",
    27534: "YUKA may already have told you everything, but may I ask you a favour too?",
    27535: "What is it?",
    27536: "More than anything we want the world back the way it was, when it was "
           "peaceful. For that, the survivors first need somewhere they can live safely.",
    27537: "I see.",
    27538: "So I'd like you to deal with the \"Honey Zombies\". They're uncannily good "
           "at sensing people, so they're the biggest danger to anyone trying to "
           "escape here.",
    27539: "Understood.",
    # EM02-082 RYO
    27557: "Welcome to SHIBUYA. Have you seen what it's like outside? This was a city "
           "that grew up differently from the Seven Hearts. And then, all of a "
           "sudden, this...",
    27558: "I wonder what caused it...",
    27559: "I don't know. But there's no use moping. We have to think about what we "
           "can do now.",
    27560: "True. This place seems fairly safe, so it could serve as a shelter.",
    27561: "There may be other survivors out there too. I'd like to make this a place "
           "people can live. ...I know. I'll go and look for survivors, so could you "
           "take care of the \"Undead Dogs\" around here?",
    27562: "Leave it to me!",
    # EM02-083 YU-SUKE (Jrose's node names say RYO; the file is his)
    27580: "Oh, hey. You made it here safely. This building was solidly built to begin "
           "with, so we've been able to use it as a shelter.",
    27581: "It's dangerous outside, after all.",
    27582: "Solid or not, every attack weakens it somewhere, so we have to step up "
           "the patrols and keep patching it.",
    27583: "That's true...",
    27584: "And our supplies are running low. If only we could get our hands on some "
           "repair materials...",
    27585: "Then I'll go and gather some!",
    27586: "Really!? I think the \"Cunning Ghouls\" probably carry the kind of thing we "
           "need for repairs. I'll go and patrol the area, so I'm counting on you!",
    27587: "Leave it to me!",
    # EM02-084 SHOHEI
    27603: "Fancy meeting you here. We had a show booked here, and we'd just arrived "
           "early to set up. Then, out of nowhere, all this happened...",
    27604: "So it came out of nowhere.",
    27605: "I suppose we should be grateful just to be alive. Now we have to work out "
           "how to break out of this.",
    27606: "If we knew the cause, we might be able to stop it getting any worse.",
    27607: "The cause, huh... Nothing comes to mind so far...",
    27608: "There's no point rushing. Let's think about what we can do right now.",
    27609: "Right. They say we've still got food to spare, but it might be worth "
           "looking for more anyway. The trouble is, we never know where the Deaders "
           "will come from, so there's never much time to search.",
    27610: "Then I'll shore up the defences!",
    27611: "Would you? Then please focus on the \"Sly Ghouls\". Maybe they kept a "
           "little of their wits -- they move in tricky ways, and they're dangerous.",
    27612: "Leave it to me!",
    # EM02-085 Shimasaburo
    27685: "In a mess like this you can't let go of your weapon, eh. If you need ammo, "
           "I'll sort you out. What'll it be?",
    27686: "Show me your wares.",
    27687: "I'd like something repaired.",
    27688: "I'd like something refined.",
    27689: "Nothing right now.",
    27691: "I'd like to know what happened.",
    27709: "What happened, huh. The day the Deaders showed up was a peaceful day like "
           "any other. Nothing on the news about an accident at some drug company, "
           "nothing like that.",
    27710: "I see...",
    27711: "If there'd been some big disaster, there'd have been evacuation warnings, "
           "people directing everyone out. They just appeared, no warning at all, so "
           "nobody could do a thing before it came to this. And that was about a "
           "month ago now.",
    27712: "A month...",
    27713: "Yeah. Might not sound like long, but one month and this whole area's in "
           "the state you see. You'd have a harder time finding someone alive. That's "
           "about all I know. Not much use, I reckon, but it'll have to do.",
    27714: "No, thank you very much!",
    # EM02-086 Sarasa
    28129: "Welcome. Not that there's anything here I can offer a guest, mind you. But "
           "if you're heading out, I can look after your things. Want to leave anything?",
    28130: "Use the storage.",
    28131: "No, thanks.",
    # EM02-087 Kanna
    27640: "How do you do. My name is Kanna. I served as a shrine maiden.",
    27641: "How do you do. What's a shrine maiden?",
    27642: "One who serves the gods, in this world.",
    27643: "I see! Wait, \"this world\"... you can tell I came from another one?",
    27644: "Yes. I sense a blessing on you unlike anything I have ever felt.",
    27645: "You can tell that?",
    27646: "Yes... I know I shouldn't ask this of you, but won't you please help us?",
    27647: "Of course!",
    27648: "I've got something to look into right now. Later!",
    27649: "Thank you. First, your strength must be shown to the others, not only to "
           "me. Please forgive the rudeness of testing you when it is we who are "
           "asking for help.",
    27650: "Leave it to me. What should I do?",
    27651: "Then, to begin, please defeat some eighty Business Zombies. That should "
           "show everyone your strength beyond any doubt.",
    27652: "Understood!",
    # EM02-088 Juri
    28197: "Things being how they are, I can't offer any luxuries, but want to have a look?",
    28198: "Show me.",
    28199: "I'm fine for now.",
}
LTB_KEY = "SHIBUYA-{}"

# ---- stage 4: the Deaders (the SHIBUYA tribute, 2026-09-29)
# id -> (English name, level, HP col, ATK, HIT, DEF, RES, AVOID, EXP col). Jrose's
# own rows are level-1/HP-100 placeholders (its event scaled them) or level-235
# dungeon copies, so every number here is ours: our LIST_NPC medians at level
# 60/68/76/100 (monsters with EXP, interpolated), times a per-species factor --
# the dog is frail and evasive, the ghouls hit harder, the police are softer, the
# puppet is a summon worth 30% EXP, the Rot Tracker 5x HP/EXP and x1.3 ATK, Deader
# Rex 10x HP/EXP and x1.35 ATK, x1.2 DEF/RES (the house boss factor). Max HP is
# level x the HP column (cobjnpc.cpp). EXP is provisional: rebalance-exp-rewards.py
# prices monsters that spawn, so it is re-run once the Phase 2 spawns exist.
DEADERS = {
    4060: ("Business Zombie",     60,  29, 241, 154, 171, 122,  89,   67),
    4061: ("Honey Zombie",        63,  29, 267, 174, 184, 129,  93,   70),
    4062: ("Undead Dog",          66,  24, 264, 163, 198, 136, 122,   73),
    4063: ("Cunning Ghoul",       70,  32, 327, 189, 212, 144, 103,   78),
    1947: ("Police Zombie",       72,  30, 312, 178, 196, 148, 106,   80),
    4064: ("Sly Ghoul",           74,  35, 344, 185, 223, 152, 119,   82),
    1948: ("Policewoman Zombie",  75,  33, 313, 188, 203, 154, 110,   84),
    4065: ("Cool Dead",           77,  37, 319, 193, 232, 159, 113,   88),
    4066: ("Cutie Dead",          78,  37, 324, 194, 235, 162, 114,   92),
    1833: ("Slave Puppet",        85,  27, 310, 206, 261, 185, 126,   35),
    4069: ("Rot Tracker",        100, 205, 586, 253, 348, 256, 151,  845),
    4068: ("Deader Rex",         100, 410, 609, 253, 379, 280, 151, 1690),
}
DEADER_STAT_COLS = (7, 8, 9, 10, 11, 12, 13, 17)   # level, HP .. AVOID, EXP
DEADER_STRID_PREFIX = "LSBMOB"
# Bare-handed rows Jrose left without a hit effect, which lands a hit with no
# visible impact (the 667 Scarab lesson): our prisoners' 403 on the same
# skeletons, our wolves' 453 on the dog. The police hit with their guns' bullet.
DEADER_HAND_HIT = {4062: 453}
DEADER_HAND_HIT_DEFAULT = 403
NPC_HAND_HIT_COL, NPC_DEAD_EVENT_COL = 33, 41
NPC_DROP_COLS = (18, 19, 20)       # table, money, rate: Phase 2 authors the drops

AI_STB_REL = r"3DDATA\STB\FILE_AI.STB"
AI_DIR_REL = r"3DDATA\AI"
# FILE_AI row -> file, at Jrose's row number (all free here). Every file is
# rebuilt from the pristine source with the edits below on each run.
DEADER_AI = {246: "sw_sby01.aip", 247: "sw_sby02.aip", 358: "sw109_boss02.aip",
             359: "sw109_mannequin.aip", 407: "zombie_police_sby.aip"}
# Casts moved to our tail rows (their ids are other skills here) and casts we
# cannot run; import-monster-skills.py's SHIBUYA notes give the reasons.
CAST_REMAP = {869: 7015, 2072: 7016, 2111: 7017, 3687: 7018}
CAST_STRIP = {3691, 3656, 6141, 3780}
SHIBUYA_SKILLS = (3722, 3723, 3724, 3726, 3744, 3760, 3782, 3783, 3687,
                  2067, 2072, 2466, 2111, 869, 2990)
# The Rot Tracker as a rare spawn. Jrose's death rule ANDs three rolls (10%,
# 10%, 20% = 0.2%) and guards on "no ally 100-500 levels below me within 40 m",
# which never matches. Ours: one 5% roll, and no ally 21-45 levels above the
# dying mob within 60 m -- the field is 60-78 and the bosses 100, so that is
# "no boss (or boss's puppet) nearby", one Rot Tracker per area at most.
ROT_TRACKER, ROT_TRACKER_CHANCE = 4069, 5
ROT_TRACKER_GUARD = dict(dist=60, lo=-45, hi=-21)
# Its own despawn rule ("no enemy within 40 m and older than N s -> suicide") holds
# N = -1200, which our COND_30 reads as always old enough: it vanished the moment
# a player stepped back. Two minutes first.
ROT_TRACKER_MIN_AGE = 120

WEAPON_STB_REL = r"3DDATA\STB\LIST_WEAPON.STB"
IMPORT_ITEM = os.path.join(HERE, "import-item.py")
IMPORT_SKILLS = os.path.join(HERE, "import-monster-skills.py")
# The police guns: Jrose player pistols, held by the police as weapons. Stats
# from our Bubble Gun (231): the same bullet (LIST_EFFECT 175) and sounds (82/78)
# as the Jrose rows, and a monster's weapon only presents its hits.
POLICE_GUNS = {1947: (1924, "Police Revolver"), 1948: (1300, "Police Derringer")}
POLICE_GUN_TEMPLATE = 231

# Every file this importer saves through a writer that leaves `<file>.bak`
# beside it; the .baks are moved to build/ at the end (pack.rs would bake them).
BAK_TRACKED = [ZONE_STB_REL, ZONE_STL_REL, WARP_STB_REL, NPC_STB_REL, NPC_STL_REL,
               EVENT_STB_REL, NPC_CHR_REL, r"3DDATA\NPC\PART_NPC.ZSC", QSD_REL,
               AI_STB_REL]


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
kk = load("import_karkia", "import-karkia.py")        # copy_new, economy splice
fate = load("import_oro_fate", "import-oro-fate.py")  # QSD + .CON codecs
tk = load("translate_karkia", "translate-karkia-dialog.py")   # .LTB codec
travel = load("add_oro_travel", "add-oro-travel.py")  # .ZON event positions
sk = load("import_skaaj", "import-skaaj.py")          # .CON node patching


def P(root, rel):
    return os.path.join(root, rel.replace("\\", "/"))


def S(src, rel):
    return oro.Stb(P(src, rel))


def O(ours, rel):
    return oro.Stb(P(ours, rel))


def ifo_files(folder):
    return sorted(f for f in os.listdir(folder) if f.lower().endswith(".ifo"))


def zon_path(root, zone):
    z = ZONES[zone]
    return os.path.join(P(root, z["maps"]), z["zon"])


def landing(ours, src, zone):
    """(x, y) of a zone's `start` event -- from the source .ZON before stage 1."""
    p = zon_path(ours, zone) if zone in ZONES else None
    if p is None:
        zstb = O(ours, ZONE_STB_REL)
        p = P(ours, zstb.get(zone, 1).decode("latin-1").strip())
    elif not os.path.isfile(p):
        p = zon_path(src, zone)
    pos = travel.zon_event_positions(p)
    if "start" not in pos:
        raise SystemExit(f"zone {zone}: no 'start' event (have {sorted(pos)})")
    x, y = pos["start"]
    return int(round(x)), int(round(y))


def all_qsd_triggers(ours):
    """{trigger name: file} across every QSD we ship -- names are global."""
    d = P(ours, r"3DDATA\QUESTDATA")
    out = {}
    for f in sorted(os.listdir(d)):
        if f.lower().endswith(".qsd"):
            for name, _ents, _raw in fate.qsd_walk(open(os.path.join(d, f), "rb").read()):
                out.setdefault(name, f)
    return out


def remap_warp(fixed):
    """The fixed record with its i16 warp id moved to our WARP.STB row."""
    wid, = struct.unpack_from("<h", fixed, 0)
    if wid not in WARPS:
        raise SystemExit(f"warp gate names Jrose warp row {wid}, which has no mapping")
    return struct.pack("<h", WARPS[wid][0]) + fixed[2:]


def rewrite_ifo(path):
    """A source .IFO as we ship it: MOB/REGEN emptied (stage 2 refills MOB),
    warp gates re-pointed. Returns the bytes."""
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
            o["fixed"] = remap_warp(o["fixed"])
        repl[oro.LUMP_WARP] = oro.build_object_lump(objs, trailing)
    return oro.build_ifo(bounds, buf, repl) if repl else buf


def sweep_baks(ours):
    """Move the writers' .bak files out of data/ -- pack.rs bakes anything.

    `quest-editor con-warp` leaves its own beside the host's .CON and the LTB,
    so every .bak in the event folder goes too.
    """
    moved = 0
    ev_dir = P(ours, EVENT_DIR_REL)
    extra = [f"{EVENT_DIR_REL}\\{f[:-4]}" for f in sorted(os.listdir(ev_dir))
             if f.lower().endswith(".bak")]
    # import-item.py (the police guns) names its backups <file>.import-<id>.bak
    for d in (r"3DDATA\STB", r"3DDATA\WEAPON"):
        extra += [f"{d}\\{f[:-4]}" for f in sorted(os.listdir(P(ours, d)))
                  if re.search(r"\.import-\d+\.bak$", f, re.I)]
    for rel in BAK_TRACKED + extra:
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


# ------------------------------------------------------------------- stage 1
def stage1(ours, src, src_index, dry):
    print("stage 1 -- maps, art, zone rows, warps")
    template = kk.economy_template(ours)
    new_dds = []
    for zone, z in sorted(ZONES.items()):
        s_map, d_map = P(src, z["maps"]), P(ours, z["maps"])
        if not os.path.isdir(s_map):
            raise SystemExit(f"source map folder missing: {s_map}")
        copied = rewritten = spliced = 0
        for base, _, names in os.walk(s_map):
            sub = os.path.relpath(base, s_map)
            dbase = d_map if sub == "." else os.path.join(d_map, sub)
            for name in sorted(names):
                sp, dp = os.path.join(base, name), os.path.join(dbase, name)
                if os.path.isfile(dp):
                    continue
                low = name.lower()
                blob = None
                if low.endswith(".ifo"):
                    blob = rewrite_ifo(sp)
                    rewritten += blob != open(sp, "rb").read()
                elif low.endswith(".zon"):
                    blob = kk.zon_with_economy(sp, template)
                    spliced += blob is not None
                elif low.endswith(".dds"):
                    new_dds.append(os.path.relpath(dp, ours))
                if not dry:
                    os.makedirs(dbase, exist_ok=True)
                    if blob is None:
                        shutil.copyfile(sp, dp)
                    else:
                        with open(dp, "wb") as fh:
                            fh.write(blob)
                copied += 1
        print(f"    {z['maps'].split(chr(92))[-1]:26s} {copied:5d} new  ({rewritten} .IFO rewritten, "
              f"{spliced} .ZON given a LUMP_ECONOMY)")

        tiles = {t for t in oro.zon_tiles(os.path.join(s_map, z["zon"])) if "\\" in t or "/" in t}
        new_dds += kk.copy_new(tiles, src_index, ours, dry, "  terrain tiles")[3]

        art = set()
        for rel in z["zsc"]:
            art |= oro.zsc_asset_refs(oro.Zsc(src_index[kk.key_of(rel)]))
        art = {a for a in art if "\\" in a or "/" in a}
        art |= kk.effect_chain({a for a in art if a.lower().endswith(".eft")}, src_index)
        kk.copy_new(z["zsc"], src_index, ours, dry, "  object tables")
        new_dds += kk.copy_new(art, src_index, ours, dry, "  decoration art")[3]

    # zone rows and names
    src_zone, zstb = S(src, ZONE_STB_REL), O(ours, ZONE_STB_REL)
    zstl = oro.Stl(P(ours, ZONE_STL_REL))
    zstb.grow_to(max(ZONES) + 1)
    changed = stl_changed = False
    for zone, z in sorted(ZONES.items()):
        if not src_zone.occupied(zone):
            raise SystemExit(f"source LIST_ZONE row {zone} is empty")
        if zstb.occupied(zone) and zstb.get(zone, ZONE_NAME_COL) != z["name"].encode():
            raise SystemExit(f"our LIST_ZONE row {zone} is occupied by {zstb.get(zone, 0)!r}")
        before = list(zstb.d[zone])
        for c in range(min(ZONE_COPY_COLS, zstb.cols)):
            zstb.set(zone, c, src_zone.get(zone, c))
        zstb.set(zone, ZONE_NAME_COL, z["name"])
        zstb.set(zone, ZONE_STL_COL, z["stl"])
        row_changed = zstb.d[zone] != before
        changed |= row_changed
        print(f"    {'LIST_ZONE.STB':26s} row {zone} {z['name']!r} "
              f"{'written' if row_changed else 'already in place'}")
        if not zstl.has(z["stl"]):
            zstl.append(z["stl"], int(z["stl"][4:]), z["name"])
            stl_changed = True
            print(f"    {'LIST_ZONE_S.STL':26s} +1 key {z['stl']}")
    if changed:
        zstb.save(dry)
    if stl_changed:
        zstl.save(dry)

    # the warp pair
    src_w, wstb = S(src, WARP_STB_REL), O(ours, WARP_STB_REL)
    wstb.grow_to(max(r for r, _ in WARPS.values()) + 1)
    wrote = []
    for srow, (row, label) in sorted(WARPS.items()):
        want = [label.encode("latin-1"), src_w.get(srow, 1), src_w.get(srow, 2)]
        cur = [wstb.get(row, c) for c in range(3)]
        if cur == want:
            continue
        if wstb.occupied(row):
            raise SystemExit(f"our WARP.STB row {row} is occupied by {cur[0]!r}")
        for c, v in enumerate(want):
            wstb.set(row, c, v)
        wrote.append(row)
    if wrote:
        wstb.save(dry)
    print(f"    {'WARP.STB':26s} rows {wrote or 'already in place'}")

    # BGM lives outside data/, in the deployed game folder
    for zone, z in sorted(ZONES.items()):
        bgm_s = P(src, z["bgm"])
        bgm_d = os.path.join(BGM_DEPLOY_DIR, os.path.basename(bgm_s))
        if not (os.path.isdir(BGM_DEPLOY_DIR) and os.path.isfile(bgm_s)):
            print(f"    NOTE: copy {z['bgm']} into the deployed game's Sound\\BGM by hand")
        elif os.path.isfile(bgm_d):
            print(f"    {'BGM':26s} {os.path.basename(bgm_s)} already deployed")
        else:
            if not dry:
                shutil.copyfile(bgm_s, bgm_d)
            print(f"    {'BGM':26s} {os.path.basename(bgm_s)} -> {BGM_DEPLOY_DIR}")

    if new_dds:
        dirs = sorted({os.path.dirname(d).replace("\\", "/") for d in new_dds})
        print(f"\n    NOTE: {len(new_dds)} new .dds, probably without mip chains. Run")
        print("          scripts/add-dds-mipmaps.py --subdir <dir> for each of:")
        for d in dirs:
            print(f"            {d}")


# ------------------------------------------------------------------- stage 2
def placements_from_source(src):
    """{zone: {ifo name: (objs, trailing)}} of the source MOB lumps, canonicalised."""
    out = {}
    for zone, z in sorted(ZONES.items()):
        s_map = P(src, z["maps"])
        per = {}
        for name in ifo_files(s_map):
            buf, bounds = oro.read_ifo(os.path.join(s_map, name))
            objs, trailing = oro.read_lump(buf, bounds, oro.LUMP_MOB)
            keep = []
            for o in objs or []:
                if o["obj_id"] in DROP_NPCS:
                    continue
                con = sk.mob_con_name(o["extra"])
                if con:
                    o["extra"] = o["extra"][:4] + oro.put_bstr(
                        sk.canon_con(con).encode("latin-1"))
                keep.append(o)
            if keep:
                per[name] = (keep, trailing)
        out[zone] = per
    return out


def fill_lump(ours, zone, per_file, lump, dry, label):
    d_map = P(ours, ZONES[zone]["maps"])
    files = n = 0
    for name, (objs, trailing) in sorted(per_file.items()):
        dp = os.path.join(d_map, name)
        if not os.path.isfile(dp):
            if dry:
                files += 1
                n += len(objs)
                continue                       # stage 1 not applied yet
            raise SystemExit(f"{dp}: run --stage 1 first")
        dbuf, dbounds = oro.read_ifo(dp)
        doff, dend = oro.lump_block(dbounds, lump)
        if doff is None:
            raise SystemExit(f"{dp}: no lump {lump} to fill")
        blob = oro.build_object_lump(objs, trailing)
        if dbuf[doff:dend] == blob:
            continue
        out = oro.build_ifo(dbounds, dbuf, {lump: blob})
        files += 1
        n += len(objs)
        if not dry:
            with open(dp, "wb") as fh:
                fh.write(out)
            vbuf, vbounds = oro.read_ifo(dp)
            voff, vend = oro.lump_block(vbounds, lump)
            if vbuf[voff:vend] != blob:
                raise SystemExit(f"VERIFY FAILED: {dp} lump {lump} mismatch")
    print(f"    {label:26s} {n} records into {files} files"
          + ("" if files else " (already in place)"))


def stage2(ours, src, src_index, dry):
    print("stage 2 -- the townsfolk")
    placements = placements_from_source(src)
    ids = sorted({o["obj_id"] for per in placements.values()
                  for objs, _ in per.values() for o in objs})
    unknown = [i for i in ids if i not in NPCS]
    if unknown:
        raise SystemExit(f"placements name NPCs with no English name: {unknown}")
    print(f"    {'placements':26s} "
          f"{sum(len(o) for per in placements.values() for o, _ in per.values())} "
          f"of {len(ids)} NPCs ({len(DROP_NPCS)} dropped: {sorted(DROP_NPCS)})")

    # 2a. LIST_NPC rows at their native ids
    src_npc, our_npc = S(src, NPC_STB_REL), O(ours, NPC_STB_REL)
    our_npc.grow_to(max(ids) + 1)
    written, kept = [], []
    for i in ids:
        # Judged by name, not by occupied(): a nameless row is a free row, and
        # the copy overwrites every column anyway (the Oro/Skaaj trap).
        cur = our_npc.get(i, 0).decode("latin-1").strip()
        if cur == NPCS[i]:
            kept.append(i)
            continue
        if cur:
            raise SystemExit(f"our LIST_NPC row {i} is occupied by {cur!r}")
        if not src_npc.get(i, 0).strip():
            raise SystemExit(f"NPC {i} is not in the source LIST_NPC")
        for c in range(min(our_npc.cols, src_npc.cols)):
            our_npc.set(i, c, src_npc.get(i, c))
        our_npc.set(i, 0, NPCS[i])
        our_npc.set(i, oro.NPC_STRID_COL, f"{NPC_STRID_PREFIX}{i}")
        our_npc.set(i, oro.NPC_PVP_COL, NPC_PVP_NOPVP)
        for c, tab in zip(oro.NPC_SELL_TAB_COLS, SHOPS.get(i, (0, 0, 0, 0))):
            our_npc.set(i, c, str(tab) if tab else b"")
        written.append(i)
    print(f"    {'LIST_NPC.STB':26s} {len(written)} rows written, {len(kept)} already ours")

    our_stl = oro.Stl(P(ours, NPC_STL_REL))
    nnames = 0
    for i in ids:
        key = f"{NPC_STRID_PREFIX}{i}"
        if not our_stl.has(key):
            our_stl.append(key, i, NPCS[i])
            nnames += 1
    print(f"    {'LIST_NPC_S.STL':26s} +{nnames} keys (now {len(our_stl.keys)})")

    # 2b. dialog registrations: the source cell verbatim, labelled in English
    src_ev, our_ev = S(src, EVENT_STB_REL), O(ours, EVENT_STB_REL)
    base_of = lambda cell: os.path.basename(cell.replace("\\", "/")).lower()
    have = {base_of(our_ev.get(r, EVENT_FILE_COL).decode("latin-1").strip()): r
            for r in range(our_ev.rows) if our_ev.get(r, EVENT_FILE_COL).strip()}
    src_by_base = {base_of(src_ev.get(r, EVENT_FILE_COL).decode("latin-1").strip()): r
                   for r in range(src_ev.rows) if src_ev.get(r, EVENT_FILE_COL).strip()}
    free = (r for r in range(1, our_ev.rows) if not our_ev.get(r, EVENT_FILE_COL).strip())
    cons = {}
    for per in placements.values():
        for objs, _ in per.values():
            for o in objs:
                con = sk.mob_con_name(o["extra"])
                if con:
                    cons[o["obj_id"]] = con
    ev_written = []
    for i, con in sorted(cons.items()):
        if con.lower() in have:
            continue
        s = src_by_base.get(con.lower())
        if s is None:
            raise SystemExit(f"NPC {i}: {con} is in no source LIST_EVENT row")
        r = next(free)
        for c in range(min(our_ev.cols, src_ev.cols)):
            our_ev.set(r, c, src_ev.get(s, c))
        our_ev.set(r, 0, NPCS[i])
        have[con.lower()] = r
        ev_written.append(r)
    print(f"    {'LIST_EVENT.STB':26s} +{len(ev_written)} rows {ev_written}")
    if sorted(c.upper() for c in cons.values()) != sorted(CON_FILES):
        raise SystemExit(f"placed dialogs {sorted(cons.values())} != CON_FILES")
    # copied verbatim here; stage 3 rewrites them from the source with our edits
    kk.copy_new({f"{EVENT_DIR_REL}\\{c}" for c in CON_FILES}, src_index, ours, dry,
                ".CON dialogs")

    our_npc.save(dry)
    our_ev.save(dry)
    if nnames:
        our_stl.save(dry)

    # 2c. models. A CHR slot left behind at a row that was blank a moment ago is
    # an orphan; import_characters keeps whatever it finds, so clear it first.
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    cleared = [i for i in written if i < len(chr_.chars) and chr_.chars[i] is not None]
    for i in cleared:
        chr_.chars[i] = None
    if cleared:
        chr_.save(dry)
        print(f"    {'LIST_NPC.CHR':26s} cleared orphan entries {cleared}"
              + (" (dry run: import_characters below still sees them)" if dry else ""))
    oro.import_characters(ids, ours, src, dry, "NPC")
    # import_characters interns the bone effects' paths in the CHR but copies only
    # meshes, materials, skeletons and motions -- the Christmas trees' sparkle
    # (.eft and everything it names) has to be brought here.
    src_chr = oro.Chr(P(src, NPC_CHR_REL))
    efts = {src_chr.effects[e].decode("latin-1") for i in ids
            for _, e in src_chr.chars[i]["effects"] if e < len(src_chr.effects)}
    efts = {e for e in efts if "\\" in e or "/" in e}
    kk.copy_new(efts | kk.effect_chain(efts, src_index), src_index, ours, dry,
                "NPC bone effects")

    # 2d. the placements, last
    for zone, per in sorted(placements.items()):
        fill_lump(ours, zone, per, oro.LUMP_MOB, dry, f"IFO mob lumps ({zone})")


# ------------------------------------------------------------------- stage 3
def con_nodes(blob):
    """Every (menu, item, check) in a .CON."""
    _, menu_num = fate.con_menu_offsets(blob)
    for mi in range(menu_num):
        for j in range(sk.con_menu(blob, mi)[3]):
            yield mi, j, fate.con_get_item(blob, mi, j)[2]


def appendix_upsert(appendix, body):
    s = appendix.decode("latin-1")
    start = s.find(APPENDIX_BEGIN)
    if start >= 0:
        end = s.find(APPENDIX_END, start)
        if end >= 0:
            s = s[:start] + s[end + len(APPENDIX_END):]
    if s and not s.endswith("\n"):
        s += "\n"
    return (s + APPENDIX_BEGIN + body + APPENDIX_END).encode("latin-1")


def build_con(src_path, name):
    """Our version of one dialog, derived from the pristine source."""
    blob = open(src_path, "rb").read()
    show = set(SHOW[name])
    for mi, j, chk in list(con_nodes(blob)):
        if (mi, j) in show:
            if chk:
                blob = sk.con_set_check(blob, mi, j, "")
        elif chk:
            blob = sk.con_set_check(blob, mi, j, "TA_Hidden")
    head, lua, appendix = fate.con_split(blob)
    clicks = sorted({m.decode("latin-1") for m in AT_NAME.findall(lua)})
    body = APPENDIX_HEAD + "".join(
        f"function {c}(E) {CLICK_BODY.get(c, '')} end\n".replace("(E)  end", "(E) end")
        for c in clicks)
    return fate.con_join(head, lua, appendix_upsert(appendix, body)), clicks


def stage3(ours, src, src_index, dry):
    print("stage 3 -- dialog and travel")

    # 3a. warp triggers, into QP401.QSD
    qsd_path = P(ours, QSD_REL)
    blob = open(qsd_path, "rb").read()
    tmpl = fate.qsd_find_entity(open(P(ours, QSD_TEMPLATE_REL), "rb").read(),
                                QSD_TEMPLATE_TRIGGER, REWD_007)
    if tmpl is None:
        raise SystemExit(f"no REWD_007 template in {QSD_TEMPLATE_TRIGGER}")
    everywhere = all_qsd_triggers(ours)
    wrote = 0
    for pattern, trigger, zone in TRIGGERS:
        if fate.qsd_has_trigger(blob, trigger):
            continue
        if trigger in everywhere:
            raise SystemExit(f"trigger {trigger} already exists in {everywhere[trigger]}")
        x, y = landing(ours, src, zone)
        # STR_REWD_007: int zone; int x; int y; BYTE party option (0: only me)
        rew = fate.qsd_patch(tmpl, (0, "<iii", (zone, x, y)), (12, "<B", (0,)))
        out = fate.qsd_append_pattern(blob, pattern,
                                      [fate.qsd_build_trigger(trigger, [], [rew])])
        ok, consumed = fate.qsd_parse_ok(out)
        if not ok or not fate.qsd_has_trigger(out, trigger):
            raise SystemExit(f"rebuilt QSD does not re-parse ({consumed}/{len(out)})")
        blob = out
        wrote += 1
        print(f"    {trigger:26s} -> zone {zone} at ({x}, {y})")
    if wrote:
        fate.write_file(qsd_path, blob, dry)
    print(f"    {'QP401.QSD':26s} +{wrote} triggers")

    # 3b. English text, at the source string ids. Before 3d: con-warp appends
    # its rows at the end of the table (the translate-first rule).
    ltb_path = P(ours, LTB_REL)
    ltb = tk.Ltb(ltb_path)
    orig_rows = len(ltb.rows)
    ltb.grow_to(max(STRINGS) + 1)
    changed = 0
    for sid, text in sorted(STRINGS.items()):
        key, cur = ltb.text(sid, 0), ltb.text(sid, 1)
        if cur == text and key == LTB_KEY.format(sid):
            continue
        if (key or cur) and not key.startswith("SHIBUYA-"):
            raise SystemExit(f"ulngtb_con.ltb row {sid} is taken by {key!r}")
        ltb.set_all_langs(sid, LTB_KEY.format(sid), text)
        changed += 1
    if changed:
        bak = os.path.join(BUILD_DIR, "ulngtb_con.ltb.orig")
        if not dry:
            os.makedirs(BUILD_DIR, exist_ok=True)
            if not os.path.isfile(bak):
                shutil.copyfile(ltb_path, bak)
            with open(ltb_path, "wb") as fh:
                fh.write(ltb.to_bytes())
            back = tk.Ltb(ltb_path)
            bad = [s for s, t in STRINGS.items() if back.text(s, 1) != t]
            if bad:
                raise SystemExit(f"VERIFY FAILED: LTB rows {bad[:8]}")
    print(f"    {'ulngtb_con.ltb':26s} {changed} of {len(STRINGS)} strings written "
          f"({orig_rows} -> {len(ltb.rows)} rows)")

    # 3c. the dialogs themselves, rebuilt from the pristine source each run
    n = nclicks = 0
    for name in CON_FILES:
        sp = os.path.join(P(src, EVENT_DIR_REL), name)
        if not os.path.isfile(sp):
            raise SystemExit(f"source dialog missing: {sp}")
        blob, clicks = build_con(sp, name)
        nclicks += len(clicks)
        dp = os.path.join(P(ours, EVENT_DIR_REL), name)
        if sk.write_if_changed(dp, blob, dry, backup=False):
            n += 1
    print(f"    {'.CON dialogs':26s} {n} of {len(CON_FILES)} rewritten "
          f"({nclicks} click functions overridden)")

    # 3d. the way in
    if not os.path.isfile(QUEST_EDITOR):
        print(f"    NOTE: {QUEST_EDITOR} not built; run by hand:")
        for npc, _ in TRAVEL_HOSTS:
            print(f"          quest-editor con-warp data {npc} {TRAVEL_KEY} {TRAVEL_TRIGGER} --write")
        return
    for npc, who in TRAVEL_HOSTS:
        cmd = [QUEST_EDITOR, "con-warp", ours, str(npc), TRAVEL_KEY, TRAVEL_TRIGGER]
        for flag, text in TRAVEL_TEXT.items():
            cmd += [flag, text]
        if dry:
            print(f"    {who:26s} (dry run: con-warp needs the trigger written first)")
            continue
        cmd.append("--write")
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit(f"con-warp {npc} failed:\n{r.stdout}\n{r.stderr}")
        line = next((l for l in r.stdout.splitlines() if "warp option" in l), r.stdout.strip())
        print(f"    {who:26s} {line.strip()[:90]}")


# ------------------------------------------------------------------- stage 4
mon = load("audit_ai_monster_refs", "audit-ai-monster-refs.py")   # .aip codec

AI_CHANCE, AI_COUNT, AI_AGE = 8, 28, 31      # raw type index = condition + 1
AI_CAST, AI_SUMMON_DIST = 25, 38             # raw type index = action + 1


def ai_type(entry):
    return struct.unpack_from("<I", entry, 4)[0] & 0xFF


def build_aip(src_path, ai_row):
    """Our version of one Deader AI file, from the pristine source. Returns
    (bytes, [edit notes])."""
    header, title, pats, tail = mon.parse_aip(open(src_path, "rb").read())
    notes = []
    out = []
    for pname, evs in pats:
        new_evs = []
        for ename, conds, acts in evs:
            kept = []
            for a in acts:
                if ai_type(a) == AI_CAST:
                    skill, = struct.unpack_from("<h", a, 10)
                    if skill in CAST_STRIP:
                        notes.append(f"strip cast {skill}")
                        continue
                    if skill in CAST_REMAP:
                        a = bytearray(a)
                        struct.pack_into("<h", a, 10, CAST_REMAP[skill])
                        a = bytes(a)
                        notes.append(f"cast {skill} -> {CAST_REMAP[skill]}")
                kept.append(a)
            if acts and not kept:
                # An event left with no action still matches, and a pattern's
                # events are first-match-wins: it would starve the ones after it.
                continue
            summons_rt = any(ai_type(a) == AI_SUMMON_DIST
                             and struct.unpack_from("<H", a, 8)[0] == ROT_TRACKER for a in kept)
            if summons_rt:
                chance = next(c for c in conds if ai_type(c) == AI_CHANCE)
                count = next(c for c in conds if ai_type(c) == AI_COUNT)
                chance = chance[:8] + bytes([ROT_TRACKER_CHANCE]) + chance[9:]
                # AICOND27: int dist (m), BYTE allied, pad, short lo, short hi,
                # WORD num, BYTE op -- "fewer than 1 ally with self-other in [lo,hi]"
                count = bytearray(count)
                g = ROT_TRACKER_GUARD
                struct.pack_into("<iB", count, 8, g["dist"], 1)
                struct.pack_into("<hhHB", count, 14, g["lo"], g["hi"], 1, 3)
                conds = [chance, bytes(count)]
                notes.append(f"Rot Tracker rule: {ROT_TRACKER_CHANCE}%, none within {g['dist']} m")
            new_conds = []
            for c in conds:
                if ai_type(c) == AI_AGE and ai_row == 247:
                    c = c[:8] + struct.pack("<i", ROT_TRACKER_MIN_AGE) + c[12:]
                    notes.append(f"despawn age -> {ROT_TRACKER_MIN_AGE} s")
                new_conds.append(c)
            new_evs.append((ename, new_conds, kept))
        out.append((pname, new_evs))
    return mon.build_aip(header, title, out, tail), notes


def weapon_row_named(ours, name):
    w = O(ours, WEAPON_STB_REL)
    for r in range(w.rows):
        if w.get(r, 0).decode("latin-1").strip() == name:
            return r
    return None


def import_police_guns(ours, src, dry):
    """{npc id: our weapon row}. Each gun is imported once (found by name after)."""
    out, planned = {}, 0
    for npc, (src_row, name) in sorted(POLICE_GUNS.items()):
        have = weapon_row_named(ours, name)
        if have is not None:
            out[npc] = have
            print(f"    {name:26s} weapon row {have} (already ours)")
            continue
        cmd = [sys.executable, IMPORT_ITEM, "--type", "weapon", "--source", src,
               "--source-row", str(src_row), "--art-only",
               "--template-row", str(POLICE_GUN_TEMPLATE), "--name", name,
               "--desc", "A Deader's sidearm from SHIBUYA."]
        if dry:
            cmd.append("--dry-run")
        r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit(f"import-item {name} failed:\n{r.stdout}\n{r.stderr}")
        got = weapon_row_named(ours, name)
        if got is None and dry:
            got = O(ours, WEAPON_STB_REL).rows + planned
            planned += 1
        if got is None:
            raise SystemExit(f"import-item wrote {name}, but no row carries the name")
        out[npc] = got
        print(f"    {name:26s} weapon row {got} from Jrose {src_row}"
              + (" (dry run)" if dry else ""))
    return out


def stage4(ours, src, src_index, dry):
    print("stage 4 -- the Deaders (monsters, AI, skills, weapons)")
    ids = sorted(DEADERS)
    src_npc, our_npc = S(src, NPC_STB_REL), O(ours, NPC_STB_REL)
    our_npc.grow_to(max(ids) + 1)

    guns = import_police_guns(ours, src, dry)

    # 4a. LIST_NPC rows at their native ids, our numbers on the source's shape
    written, kept = [], []
    for i in ids:
        name, *stats = DEADERS[i]
        cur = our_npc.get(i, 0).decode("latin-1").strip()
        if cur and cur != name:
            raise SystemExit(f"our LIST_NPC row {i} is occupied by {cur!r}")
        before = list(our_npc.d[i])
        for c in range(oro.NPC_COPY_COLS):
            our_npc.set(i, c, src_npc.get(i, c))
        our_npc.set(i, 0, name)
        for c, v in zip(DEADER_STAT_COLS, stats):
            our_npc.set(i, c, str(v))
        for c in NPC_DROP_COLS + oro.NPC_SELL_TAB_COLS:
            our_npc.set(i, c, b"")
        our_npc.set(i, NPC_DEAD_EVENT_COL, b"")      # Jrose quest triggers we do not ship
        our_npc.set(i, oro.NPC_STRID_COL, f"{DEADER_STRID_PREFIX}{i}")
        our_npc.set(i, oro.NPC_PVP_COL, oro.DEFAULT_PVP_STATE)
        if i in guns:
            our_npc.set(i, oro.NPC_R_WEAPON_COL, str(guns[i]))
        elif not our_npc.get(i, NPC_HAND_HIT_COL).strip():
            our_npc.set(i, NPC_HAND_HIT_COL, str(DEADER_HAND_HIT.get(i, DEADER_HAND_HIT_DEFAULT)))
        ai = int(src_npc.get(i, oro.NPC_AI_COL) or 0)
        if ai not in DEADER_AI:
            raise SystemExit(f"{name} ({i}) uses Jrose AI row {ai}, which DEADER_AI does not cover")
        (kept if our_npc.d[i] == before else written).append(i)
    print(f"    {'LIST_NPC.STB':26s} {len(written)} rows written, {len(kept)} already ours")

    our_stl = oro.Stl(P(ours, NPC_STL_REL))
    nnames = 0
    for i in ids:
        key = f"{DEADER_STRID_PREFIX}{i}"
        if not our_stl.has(key):
            our_stl.append(key, i, DEADERS[i][0])
            nnames += 1
    print(f"    {'LIST_NPC_S.STL':26s} +{nnames} keys")

    # 4b. AI rows, and the files rebuilt from source with our edits
    src_ai, our_ai = S(src, AI_STB_REL), O(ours, AI_STB_REL)
    our_ai.grow_to(max(DEADER_AI) + 1)
    ai_rows = 0
    for row, fname in sorted(DEADER_AI.items()):
        cell = src_ai.get(row, 0)
        if os.path.basename(cell.decode("latin-1").replace("\\", "/")).lower() != fname.lower():
            raise SystemExit(f"source FILE_AI row {row} is {cell!r}, expected {fname}")
        if our_ai.get(row, 0) != cell:
            if our_ai.get(row, 0).strip():
                raise SystemExit(f"our FILE_AI row {row} is {our_ai.get(row, 0)!r}")
            our_ai.set(row, 0, cell)
            ai_rows += 1
    print(f"    {'FILE_AI.STB':26s} +{ai_rows} rows {sorted(DEADER_AI)}")
    n_ai = 0
    for row, fname in sorted(DEADER_AI.items()):
        blob, notes = build_aip(os.path.join(P(src, AI_DIR_REL), fname), row)
        if sk.write_if_changed(os.path.join(P(ours, AI_DIR_REL), fname), blob, dry, backup=False):
            n_ai += 1
        summary = ", ".join(sorted(set(notes))) or "verbatim"
        print(f"    {fname:26s} {summary}")
    print(f"    {'.aip files':26s} {n_ai} of {len(DEADER_AI)} (re)written")

    our_npc.save(dry)
    our_ai.save(dry)
    if nnames:
        our_stl.save(dry)

    # 4c. models (clearing orphan CHR slots first, as stage 2 does) + bone effects
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    cleared = [i for i in written if i < len(chr_.chars) and chr_.chars[i] is not None]
    for i in cleared:
        chr_.chars[i] = None
    if cleared:
        chr_.save(dry)
        print(f"    {'LIST_NPC.CHR':26s} cleared orphan entries {cleared}")
    oro.import_characters(ids, ours, src, dry, "monster")
    src_chr = oro.Chr(P(src, NPC_CHR_REL))
    efts = {src_chr.effects[e].decode("latin-1") for i in ids
            for _, e in src_chr.chars[i]["effects"] if e < len(src_chr.effects)}
    efts = {e for e in efts if "\\" in e or "/" in e}
    if efts:
        kk.copy_new(efts | kk.effect_chain(efts, src_index), src_index, ours, dry,
                    "monster bone effects")

    # 4d. the skill kit
    cmd = [sys.executable, IMPORT_SKILLS, "--skills", ",".join(map(str, SHIBUYA_SKILLS))]
    if dry:
        cmd.append("--dry-run")
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"import-monster-skills failed:\n{r.stdout}\n{r.stderr}")
    wrote = [l for l in r.stdout.splitlines() if l.startswith("   LIST_SKILL ") and " type " in l]
    print(f"    {'skill kit':26s} {len(SHIBUYA_SKILLS)} skills, "
          f"{len(wrote)} to write" + (" (dry run)" if dry else ""))


# -------------------------------------------------------------------- verify
def verify(ours, src):
    print("verify")
    bad = []

    def check(cond, what):
        print(f"    {'ok ' if cond else 'BAD'} {what}")
        if not cond:
            bad.append(what)

    idx = kk.index_tree(ours)
    zstb, zstl = O(ours, ZONE_STB_REL), oro.Stl(P(ours, ZONE_STL_REL))
    for zone, z in sorted(ZONES.items()):
        check(zstb.get(zone, 0) == z["name"].encode(), f"LIST_ZONE row {zone} is {z['name']}")
        check(zstl.has(z["stl"]), f"STL key {z['stl']}")
        d_map = P(ours, z["maps"])
        ifos = ifo_files(d_map) if os.path.isdir(d_map) else []
        zon = zon_path(ours, zone)
        check(len(ifos) == z["ifos"] and os.path.isfile(zon),
              f"{z['maps']}: {len(ifos)}/{z['ifos']} .IFO + {z['zon']}")
        if os.path.isfile(zon):
            _, bounds = oro.read_ifo(zon)
            check(oro.lump_block(bounds, kk.ZON_LUMP_ECONOMY)[0] is not None,
                  f"{z['zon']} has LUMP_ECONOMY")
            tiles = oro.zon_tiles(zon)
            missing = [t for t in tiles if kk.key_of(t) not in idx]
            check(not missing, f"{z['zon']} tiles present ({len(tiles)}, {len(missing)} missing)")
        for rel in z["zsc"]:
            if not os.path.isfile(P(ours, rel)):
                check(False, rel)
                continue
            refs = oro.zsc_asset_refs(oro.Zsc(P(ours, rel)))
            missing = [r for r in refs if kk.key_of(r) not in idx]
            check(not missing, f"{rel.split(chr(92))[-1]}: {len(refs)} refs, {len(missing)} missing")
        regen = warps = 0
        warp_ids = set()
        for name in ifos:
            buf, bounds = oro.read_ifo(os.path.join(d_map, name))
            regen += len(oro.read_lump(buf, bounds, oro.LUMP_REGEN)[0] or [])
            for o in oro.read_lump(buf, bounds, oro.LUMP_WARP)[0] or []:
                warps += 1
                warp_ids.add(o["warp_id"])
        check(regen == 0, f"zone {zone}: {regen} regen points (want 0)")
        want_ids = {row for row, _ in WARPS.values()}
        check(warps == 1 and warp_ids <= want_ids, f"zone {zone}: warp gate -> {sorted(warp_ids)}")

    wstb = O(ours, WARP_STB_REL)
    for srow, (row, label) in sorted(WARPS.items()):
        check(wstb.get(row, 0) == label.encode(), f"WARP.STB row {row} ({label.split(' (')[0]})")

    npc, stl = O(ours, NPC_STB_REL), oro.Stl(P(ours, NPC_STL_REL))
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    rows = [i for i in NPCS if npc.get(i, 0).decode("latin-1") == NPCS[i]]
    keys = [i for i in NPCS if stl.has(f"{NPC_STRID_PREFIX}{i}")]
    chrs = [i for i in NPCS if i < len(chr_.chars) and chr_.chars[i] is not None]
    check(len(rows) == len(NPCS), f"{len(rows)}/{len(NPCS)} LIST_NPC rows")
    check(len(keys) == len(NPCS), f"{len(keys)}/{len(NPCS)} STL keys")
    check(len(chrs) == len(NPCS), f"{len(chrs)}/{len(NPCS)} CHR entries")
    tabs_ok = all([int(npc.get(i, c) or 0) for c in oro.NPC_SELL_TAB_COLS]
                  == list(SHOPS.get(i, (0, 0, 0, 0))) for i in NPCS)
    check(tabs_ok, "shop tabs are ours (Shimasaburo, Juri) or blank")

    ev = O(ours, EVENT_STB_REL)
    reg = {os.path.basename(ev.get(r, EVENT_FILE_COL).decode("latin-1").replace("\\", "/")).lower()
           for r in range(ev.rows) if ev.get(r, EVENT_FILE_COL).strip()}
    want = {sk.canon_con(f).lower() for f in CON_FILES}
    check(want <= reg, f"LIST_EVENT registers {len(want & reg)}/{len(want)} dialogs")

    placed, refs_ok = 0, True
    for zone, z in sorted(ZONES.items()):
        d_map = P(ours, z["maps"])
        for name in (ifo_files(d_map) if os.path.isdir(d_map) else []):
            buf, bounds = oro.read_ifo(os.path.join(d_map, name))
            for o in oro.read_lump(buf, bounds, oro.LUMP_MOB)[0] or []:
                placed += 1
                con = sk.mob_con_name(o["extra"]).lower()
                refs_ok &= o["obj_id"] in NPCS and (not con or con in reg)
    want_placed = sum(len(o) for per in placements_from_source(src).values()
                      for o, _ in per.values())
    check(placed == want_placed, f"{placed}/{want_placed} NPC placements")
    check(refs_ok, "every placement is one of ours and its .CON is registered")

    qsd = open(P(ours, QSD_REL), "rb").read()
    have = [t for _, t, _ in TRIGGERS if fate.qsd_has_trigger(qsd, t)]
    check(len(have) == len(TRIGGERS), f"QP401.QSD holds {len(have)}/{len(TRIGGERS)} triggers")
    ltb = tk.Ltb(P(ours, LTB_REL))
    n = sum(1 for s, t in STRINGS.items() if ltb.text(s, 1) == t)
    check(n == len(STRINGS), f"ulngtb_con.ltb: {n}/{len(STRINGS)} English strings")

    ok_files, leaks = 0, []
    for name in CON_FILES:
        dp = os.path.join(P(ours, EVENT_DIR_REL), name)
        if not os.path.isfile(dp):
            continue
        blob = open(dp, "rb").read()
        want_blob, _ = build_con(os.path.join(P(src, EVENT_DIR_REL), name), name)
        ok_files += blob == want_blob
        show = set(SHOW[name])
        for mi, j, chk in con_nodes(blob):
            if chk not in ("", "TA_Hidden") or ((mi, j) in show and chk):
                leaks.append(f"{name}:{mi}/{j}={chk}")
    check(ok_files == len(CON_FILES), f"{ok_files}/{len(CON_FILES)} dialogs match the build")
    check(not leaks, f"no node gated on a Jrose check ({len(leaks)} leak) {leaks[:3]}")

    ev_dir = P(ours, EVENT_DIR_REL)
    carriers = []
    for f in sorted(os.listdir(ev_dir)):
        if f.lower().endswith(".con"):
            _, _, appendix = fate.con_split(open(os.path.join(ev_dir, f), "rb").read())
            if TRAVEL_TRIGGER.encode() in appendix:
                carriers.append(f)
    check(len(carriers) >= len(TRAVEL_HOSTS),
          f"{len(carriers)} dialog(s) offer {TRAVEL_TRIGGER} {carriers}")

    # stage 4
    npc = O(ours, NPC_STB_REL)
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    stats_ok = [i for i, (name, *st) in DEADERS.items()
                if npc.get(i, 0).decode("latin-1") == name
                and [int(npc.get(i, c) or 0) for c in DEADER_STAT_COLS] == st
                and not npc.get(i, NPC_DEAD_EVENT_COL).strip()
                and int(npc.get(i, oro.NPC_AI_COL) or 0) in DEADER_AI]
    check(len(stats_ok) == len(DEADERS), f"{len(stats_ok)}/{len(DEADERS)} Deader rows (name, stats, AI, no Jrose trigger)")
    check(all(stl.has(f"{DEADER_STRID_PREFIX}{i}") for i in DEADERS), "Deader STL keys")
    check(all(i < len(chr_.chars) and chr_.chars[i] is not None for i in DEADERS), "Deader CHR entries")
    guns_ok = all(int(npc.get(i, oro.NPC_R_WEAPON_COL) or 0) == weapon_row_named(ours, name)
                  for i, (_, name) in POLICE_GUNS.items())
    check(guns_ok, "police hold the imported guns")
    ai = O(ours, AI_STB_REL)
    ai_ok = 0
    for row, fname in sorted(DEADER_AI.items()):
        dp = os.path.join(P(ours, AI_DIR_REL), fname)
        want, _ = build_aip(os.path.join(P(src, AI_DIR_REL), fname), row)
        ai_ok += (ai.get(row, 0) == S(src, AI_STB_REL).get(row, 0)
                  and os.path.isfile(dp) and open(dp, "rb").read() == want)
    check(ai_ok == len(DEADER_AI), f"{ai_ok}/{len(DEADER_AI)} AI rows + files match the build")
    r = subprocess.run([sys.executable, IMPORT_SKILLS, "--verify",
                        "--skills", ",".join(map(str, SHIBUYA_SKILLS))],
                       cwd=ROOT, capture_output=True, text=True)
    check(r.returncode == 0, f"skill kit ({len(SHIBUYA_SKILLS)} rows) verifies")
    print("    " + ("ALL OK" if not bad else f"{len(bad)} problem(s)"))
    return 0 if not bad else 1


# ------------------------------------------------------------------ selftest
def selftest(ours, src):
    print("selftest -- every writer must reproduce its input byte for byte")
    n = 0
    for zone, z in sorted(ZONES.items()):
        s_map = P(src, z["maps"])
        for name in ifo_files(s_map) + [z["zon"]]:
            p = os.path.join(s_map, name)
            buf, bounds = oro.read_ifo(p)
            assert oro.build_ifo(bounds, buf, {}) == buf, name
            if name.lower().endswith(".ifo"):
                for lt in (oro.LUMP_MOB, oro.LUMP_REGEN, oro.LUMP_WARP):
                    objs, trailing = oro.read_lump(buf, bounds, lt)
                    if objs is None:
                        continue
                    off, end = oro.lump_block(bounds, lt)
                    assert oro.build_object_lump(objs, trailing) == buf[off:end], (name, lt)
            n += 1
    print(f"    {n} .IFO + .ZON containers round-trip")
    for rel in (ZONE_STB_REL, NPC_STB_REL, EVENT_STB_REL, WARP_STB_REL):
        p = P(ours, rel)
        assert oro.Stb(p).to_bytes() == open(p, "rb").read(), rel
    for rel in (ZONE_STL_REL, NPC_STL_REL):
        p = P(ours, rel)
        assert oro.Stl(p).to_bytes() == open(p, "rb").read(), rel
    p = P(ours, NPC_CHR_REL)
    assert oro.Chr(p).to_bytes() == open(p, "rb").read(), "LIST_NPC.CHR"
    print("    LIST_ZONE / LIST_NPC / LIST_EVENT / WARP / two STLs / CHR round-trip")
    ltb = tk.Ltb(P(ours, LTB_REL))
    tmp = os.path.join(BUILD_DIR, "selftest.ltb")
    os.makedirs(BUILD_DIR, exist_ok=True)
    with open(tmp, "wb") as fh:
        fh.write(ltb.to_bytes())
    back = tk.Ltb(tmp)
    os.remove(tmp)
    assert len(back.rows) == len(ltb.rows) and back.cols == ltb.cols
    assert all(back.rows[r] == ltb.rows[r] for r in range(len(ltb.rows)))
    print(f"    ulngtb_con.ltb rebuilds to the same {len(ltb.rows)} rows")
    for name in CON_FILES:
        blob = open(os.path.join(P(src, EVENT_DIR_REL), name), "rb").read()
        head, lua, appendix = fate.con_split(blob)
        assert fate.con_join(head, lua, appendix) == blob, name
        assert lua.startswith(b"\x1bLua"), f"{name}: Lua blob did not decode"
        nodes = {(mi, j) for mi, j, _ in con_nodes(blob)}
        assert set(SHOW[name]) <= nodes, f"{name}: SHOW names a missing node"
    print(f"    {len(CON_FILES)} .CON dialogs split/join byte-identically, Lua decodes")
    for rel in (QSD_REL, QSD_TEMPLATE_REL):
        ok, _ = fate.qsd_parse_ok(open(P(ours, rel), "rb").read())
        assert ok, rel
    print("    QP401 / PVP10 QSDs parse exactly")
    for row, fname in sorted(DEADER_AI.items()):
        blob = open(os.path.join(P(src, AI_DIR_REL), fname), "rb").read()
        assert mon.build_aip(*mon.parse_aip(blob)) == blob, fname
    p = P(ours, AI_STB_REL)
    assert oro.Stb(p).to_bytes() == open(p, "rb").read(), "FILE_AI"
    print(f"    {len(DEADER_AI)} Deader .aip files and FILE_AI round-trip")
    for zone in (126, 2):
        x, y = landing(ours, src, zone)
        assert 100_000 < x < 10_000_000 and 100_000 < y < 10_000_000, (zone, x, y)
        print(f"    landing spot of zone {zone} = ({x}, {y})")
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
    if not os.path.isdir(P(src, ZONES[126]["maps"])):
        raise SystemExit(f"source is not a Jrose client with SHIBUYA: {src}")
    if args.selftest:
        selftest(ours, src)
        return 0
    if args.verify:
        return verify(ours, src)
    if not args.stage:
        ap.error("give --stage N (1-4), --verify or --selftest")
    src_index = kk.index_tree(src)
    for st in sorted(set(args.stage)):
        {1: stage1, 2: stage2, 3: stage3, 4: stage4}[st](ours, src, src_index, args.dry_run)
        print()
    if args.dry_run:
        print("dry run: nothing written")
    else:
        sweep_baks(ours)
        print("done. Next: add-dds-mipmaps.py (see the stage 1 note), rebake the VFS,")
        print("restart the servers, deploy client + data together.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
