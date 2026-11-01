"""Import the Karkia planet (9 zones) from the Jrose client.

Karkia is Jrose's late-era undead planet -- a cemetery, an overrun village, a
burned forest, a tower and a memory-realm cluster. `doc/karkia-survey.md` is the
evidence that it is importable; `doc/karkia-roadmap.md` is the plan this script
implements. Read the roadmap before changing anything here.

Staged the same way as `scripts/import-oro.py`, and for the same reason -- each
stage is independently testable in game and independently revertible:

    --stage 1   terrain, art, zone rows, zone names. No NPCs, no monsters, no
                gates: the copied .IFOs get their MOB/REGEN/WARP/EVENT_OBJECT
                lumps emptied on the way in (count = 0, lump table untouched),
                so later stages just refill them.
    --stage 2   the internal warp gates: WARP.STB rows and the gate objects put
                back into the .IFO WARP lumps. The way *in* is a separate
                script, scripts/add-karkia-travel.py.
    --stage 3   monsters: 38 LIST_NPC rows at their native ids, names, AI rows
                and .aip files, character models/skeletons/motions/effects with
                the usual index remap, mob-weapon presentation, and the spawn
                lumps. Stats are copied as Jrose wrote them; re-deriving them
                for our curve is stage 4.

Stages 4-6 (the balance pass, drops, NPCs) are planned in the roadmap and not
written yet.

Every stage is idempotent -- re-running detects what is already in place and does
nothing. --dry-run previews. --selftest proves every writer round-trips
byte-identically before anything is touched.

Why this is a *port* and not a copy
-----------------------------------
The container formats are unchanged (all 9 .ZON, 111 .IFO and 18 .ZSC parse with
the readers already in import-oro.py, and not one referenced asset is missing),
which is what makes stage 1 cheap. Five things still do not travel:

  * **Karkia's .ZON files are the stripped late-Jrose variant**: a 28-byte block 0
    and *no* LUMP_ECONOMY. The client does not care -- ReadZoneINFO reads exactly
    28 bytes -- but the server does. CZoneFILE::ReadECONOMY is the only caller of
    CEconomy::Load, so with no lump 4 it never runs, and CEconomy::Init then
    computes `m_iTownITEM[nP] = m_nTown_CONSUM[nP] * 100` off uninitialised heap.
    Not fatal (m_btItemRATE is clamped to 45..65) but the zone's shop prices would
    be driven by garbage. Every one of our own 55 zones has a lump 4, so this
    script splices one in -- see zon_with_economy().

  * **WARP.STB 170 and 172 are live Oro gates** here (TOWN->ODE01 and
    ODRP01->ODE01). Karkia uses those ids for Cemetery->Church and
    Cemetery->SpireVil. Nothing in stage 1 writes WARP.STB, but stage 2 must
    renumber rather than copy, or Muris breaks silently.

  * **LIST_ZONE_S.STL key LZON086 is our zone 82** (Gates of Muris), and Karkia's
    zone 86 asks for exactly that key. Fresh keys are allocated here (LZON091+,
    the first free block above our highest) and the English names are authored --
    the source strings are Japanese and our STL is the modern ITST01 dialect with
    five language blocks against Jrose's legacy I_NUM.

  * **Our .dds files are the better copies.** 331 of the textures Karkia shares
    with us differ only in that ours carry mip chains (mips=9 against mips=0) --
    we ran add-dds-mipmaps.py over them. copy_new() never overwrites, which makes
    that safe by construction; the *new* textures want the same treatment, so the
    run ends by listing them.

  * `LIST_SKY` row 17 does not exist here (we have 13 rows). Four Karkia zones
    reference it. It is copied along with the two LUNAR\\Sky02 textures.

Deliberately not done in stage 1: the entrance. Karkia has no gate from anywhere
in the game, and its nine zones split into two disconnected clusters plus an
isolated Tower -- that is stage 2's Wayfinder NPC. Reach the zones with a GM warp
until then.

After running: rebake the client VFS and restart the servers (they cache STBs at
startup).

File formats this script writes are documented at their reader/writer functions,
or in import-oro.py where they are shared.
"""
import argparse
import collections
import importlib.util
import io
import os
import re
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# --------------------------------------------------------------------- config
DEFAULT_SRC = r"C:\Users\Thomas\Desktop\Testclients\Jrose"

MAPS_REL = r"3DDATA\MAPS\KARKIA"
KARKIA_ART_REL = r"3DDATA\KARKIA"
ZONE_STB_REL = r"3DDATA\STB\LIST_ZONE.STB"
ZONE_STL_REL = r"3DDATA\STB\LIST_ZONE_S.STL"
SKY_STB_REL = r"3DDATA\STB\LIST_SKY.STB"

# zone row -> (map folder, English name, fresh STL key)
#
# The names are authored, not translated cell-for-cell: LIST_ZONE col 0 carries an
# editor label ("K-Karkia's Memories (boss)") while LIST_ZONE_S.STL carries the
# in-game one, and it is the STL that draws above the minimap. Rows 135 and 136
# share one name in the source (both 絶望の塔); they are split here because the
# minimap would otherwise show the same string in two different places.
ZONES = [
    (86,  "KCHURCH",       "The Abandoned Church", "LZON091"),
    (87,  "KCEMETERY",     "The Desolate Cemetery", "LZON092"),
    (88,  "KSPIREVIL",     "Spire Village",         "LZON093"),
    (131, "KBURNEDFOREST", "The Burned Forest",     "LZON094"),
    (133, "KMEMORIES",     "Memories of Karkia",    "LZON095"),
    (134, "KMEMORIESBOSS", "Infinite Prison",       "LZON096"),
    (135, "KTOWERPLACE",   "Foot of the Tower",     "LZON097"),
    (136, "KTOWER",        "Tower of Despair",      "LZON098"),
    (144, "KFLOWERGARDEN", "Garden of Karkia",      "LZON099"),
]
MAX_ZONE_ROW = max(r for r, _, _, _ in ZONES)

# Our LIST_ZONE is 36 columns to Jrose's 37; cols 0-35 align by meaning and col 36
# (their world-map number) is theirs alone. Two are overridden rather than copied:
# col 0 is the editor label and col 26 is the STL key that collides.
ZONE_COPY_COLS = 36
ZONE_NAME_COL, ZONE_STL_COL = 0, 26

# Four Karkia zones point at LIST_SKY row 17 (a Lunar sky variant); the other five
# use row 1, which is already byte-identical to ours.
SKY_ROW = 17

# --- stage 2a: the internal warp gates ------------------------------------
#
# Karkia places 11 gate objects carrying 10 distinct warp ids. Eight of those ids
# are free in our WARP.STB; **170 and 172 are live Oro gates** (TOWN->ODE01 and
# ODRP01->ODE01), so copying them verbatim would silently redirect Muris. They are
# remapped to 191/192, which keeps every Karkia gate in one contiguous 173-192
# band -- our table's last occupied row is 172, so that whole band is free.
#
# Everything else about a gate is read from the source at run time rather than
# restated here: the destination zone and the destination event-position name come
# out of Jrose's WARP.STB, and the placements come out of the source .IFOs. Only
# the id remap and the English name are authored.
GATE_REMAP = {170: 191, 172: 192}
GATE_NAMES = {
    170: "KCemetery -> KChurch (The Desolate Cemetery -> The Abandoned Church)",
    172: "KCemetery -> KSpireVil (The Desolate Cemetery -> Spire Village)",
    173: "KSpireVil -> KCemetery (Spire Village -> The Desolate Cemetery)",
    178: "KCemetery -> KBurnedForest (The Desolate Cemetery -> The Burned Forest)",
    179: "KBurnedForest -> KCemetery (The Burned Forest -> The Desolate Cemetery)",
    180: "KMemoriesBoss -> KMemories (Infinite Prison -> Memories of Karkia)",
    185: "KSpireVil -> KTowerPlace (Spire Village -> Foot of the Tower)",
    186: "KTowerPlace -> KSpireVil (Foot of the Tower -> Spire Village)",
    189: "KMemories -> KFlowerGarden (Memories of Karkia -> Garden of Karkia)",
    190: "KFlowerGarden -> KMemories (Garden of Karkia -> Memories of Karkia)",
}
WARP_STB_REL = r"3DDATA\STB\WARP.STB"
WARP_DEST_ZONE_COL, WARP_DEST_EVENT_COL = 1, 2

# --- stage 3: the monsters -------------------------------------------------
#
# 31 spawned plus 7 that exist only as AI summons, at their **native ids**. All 38
# are free in our LIST_NPC (our last occupied row is 2265), and keeping the source
# numbering is what makes the .aip summon references resolve with no binary
# patching -- ks_2699 summons 2685 and 2686 by id.
#
# The names are authored. Jrose's LIST_NPC_S.STL is the legacy I_NUM dialect with
# no language blocks and Japanese text, so there is nothing to copy; these are
# translations, cross-checked against the .aip filenames where those name the
# creature (kak_spider -> Murilo, kak_neggolem -> Neg Golem, kak_deadeye -> Evil
# Eye, kak_melt -> Melt Qualle).
#
# Two deliberate choices worth knowing:
#   * The "D=" prefix is CANON and the "=" is load-bearing. Jrose's own
#     player-facing rows spell it that way (`D=シード`, `Ｄ＝エラー`), and a
#     translated line of dialog quotes it directly: "So every monster with 'D='
#     in its name is a victim of the Devil Pest." These names were first authored
#     with a hyphen, which silently broke that line -- the player was told to look
#     for a prefix no monster had. Corrected 2026-09-08; do not "tidy" it back to
#     a hyphen.
#   * 2701 and 2702 both come out "D=Victim". They are separate rows at different
#     levels whose only distinguishing text is the *editor* label (2701 is
#     "transformed villager", 2702 "maddened infected"), and the in-game name is
#     the same in the source. Kept faithful rather than invented; our own data
#     already ships three zones called "The Golden Ring", and 186 names in
#     LIST_NPC are shared by more than one row (Candle Ghost x32).
#   * The Spire Village roster is the Cemetery roster again +14 levels with an
#     alpha suffix, so it gets " Alpha". That suffix is **also canon**: the
#     source rows carry only dev placeholders (`KSヴィクティムf`), but the
#     dialog names the creatures directly -- `D=ヴィクティムα`, `蘇った防疫団員α`
#     -- so " Alpha" is what the quest text tells the player to hunt. Re-theming
#     the set would desynchronise those lines; the open roadmap question is the
#     shared *art*, not the names.
#   * A few are transliteration guesses with no established English: Murilo,
#     Orgeid, Eugeid, Melt Qualle, Woodnoid.
KARKIA_MONSTERS = {
    # AI-summon only
    2685: "Hebarn Officer Pazugenti",
    2686: "Hebarn Officer Scylla Mira",
    2687: "Corroded Golem",
    2688: "Revived Veteran Warrior",
    2689: "Ghost Seed",                     # shares STL key LNPC2731 with 2731
    2705: "D=Pollinosis",
    2731: "Ghost Seed",
    # Spire Village (zone 88)
    2690: "D=Victim Alpha",
    2691: "D=Victim Alpha",
    2692: "Revived Quarantine Officer Alpha",
    2693: "D=Ghoul Ein Alpha",
    2694: "D=Ghoul Eine Alpha",
    2695: "Deadly Wolf Alpha",
    2696: "Murilo Alpha",
    2697: "Evil Eye Alpha",
    2698: "D=Error Alpha",
    2699: "Deadly Drake Alpha",
    # The Desolate Cemetery (zone 87)
    2701: "D=Victim",
    2702: "D=Victim",
    2703: "Revived Quarantine Officer",
    2704: "D=Seed",
    2712: "D=Ghoul Ein",
    2713: "D=Ghoul Eine",
    2714: "Deadly Wolf",
    2715: "Murilo",
    2716: "Evil Eye",
    2717: "D=Error",
    2719: "Woodnoid",
    2720: "Dark Tower",
    2721: "Element Battler",
    2722: "Neg Golem",
    2723: "Orgeid",
    2724: "Eugeid",
    2725: "Evil Fairy",
    2727: "Zorn Gargoyle",
    2728: "Elgar Gargoyle",
    2729: "Deadly Drake",
    2730: "Melt Qualle",
    # --- stage 7b: the flashback zones -------------------------------------
    #
    # Memories (133) and the Garden (144) are Karkia *before* the fall, and
    # their monsters are ordinary wildlife rather than the undead of the
    # present day. Jrose laid out 53 regen points across the flashback cluster
    # and never assigned a monster to any of them -- their own files carry id 1
    # in every slot -- so which creatures live here is authored, not restored.
    # The dialog names them, which is what settles it:
    #
    #   Pormello's ecology quests  -> Melitta, Melan Melitta, Calaplasinos,
    #                                 and the Melitta swarm's queen
    #   Nitraria's leaf collection -> Mukuroji, Nigaki, Beeberu
    #
    # Ids 2520-2560 are entirely empty on our side, so these keep their native
    # numbering like the rest of the roster.
    #
    # Names are transliterations, kept identical to the ones already shipped in
    # the translated dialog -- consistency with the text a player reads matters
    # more than a prettier rendering. Mukuroji, Nigaki and Beeberu are Japanese
    # plant names (soapberry, bitterwood), which is why Nitraria harvests
    # leaves from them.
    2527: "Calaplasinos",
    2528: "Melitta",
    2529: "Melan Melitta",
    2530: "Basilissa Melitta",
    2539: "Mukuroji",
    2547: "Nigaki",
    2549: "Beeberu",
}
NPC_STB_REL = r"3DDATA\STB\LIST_NPC.STB"
NPC_STL_REL = r"3DDATA\STB\LIST_NPC_S.STL"
AI_STB_REL = r"3DDATA\STB\FILE_AI.STB"
WEAPON_STB_REL = r"3DDATA\STB\LIST_WEAPON.STB"
NPC_CHR_REL = r"3DDATA\NPC\LIST_NPC.CHR"
PART_NPC_ZSC_REL = r"3DDATA\NPC\PART_NPC.ZSC"
NPC_RANGE_COL = 26                 # bare-hand attack range, in cm
MELEE_RANGE_CM = 800               # see the bullet-effect note in stage3()
EFFECT_STB_REL = r"3DDATA\STB\LIST_EFFECT.STB"
FILE_EFFECT_STB_REL = r"3DDATA\STB\FILE_EFFECT.STB"
FILE_SOUND_STB_REL = r"3DDATA\STB\FILE_SOUND.STB"
HITSOUND_STB_REL = r"3DDATA\STB\LIST_HITSOUND.STB"
EFFECT_DIR = r"3DDATA\EFFECT"      # FILE_EFFECT names its .eft files bare

# --- stage 3h: attack presentation ----------------------------------------
#
# Two *different* effect tables are in play, and getting them the wrong way round
# makes correct data look broken and broken data look fine:
#
#   Hitted(iEffectIDX) -> EFFECT_HITTED_NORMAL(I) = g_TblEFFECT.get_int32(I, 9),
#     and g_TblEFFECT is LIST_EFFECT.STB. So the impact effect is a **LIST_EFFECT**
#     row whose col 9 is the FILE_EFFECT index. Fed by LIST_WEAPON 38/39.
#   ShowEffectOnCharByIndex -> Add_EffectWithIDX, bounds-checked against
#     CEffectLIST, which cgame.cpp builds from **FILE_EFFECT.STB**. So the death
#     effect (LIST_NPC 34) is a FILE_EFFECT row.
#
# The trap that produced the reported bug: cobjchar_actionframe.cpp case 21/31
# falls back to the NPC's own columns (33 hand-hit effect, 31 attack sound) *only
# when the monster has no right-hand weapon*. A monster that equips a blank weapon
# row therefore gets neither the weapon's presentation nor the NPC fallback --
# damage numbers appear, and nothing else does.
# ---------------------------------------------------------------- stage 6: NPCs
# 32 conversation NPCs across five zones. Jrose places them in the .IFO LUMP_MOB
# (a fixed placement carrying an AI row and a .CON name), not the REGEN lump.
#
# Nine sit at low ids that our table already reaches. Only 1074 is a live NPC of
# ours ([Wounded Traveler] Seth) -- the other eight are rows with no name, which
# the server skips at spawn (`if (!NPC_NAME(iObjID)) continue`). All nine are
# remapped anyway, into the band just past Karkia's own, so this stage writes no
# row that existed before it. The placements are remapped with them.
NPC_ID_REMAP = {1047: 4138, 1048: 4139, 1074: 4140, 1076: 4141, 1077: 4142,
                1078: 4143, 1079: 4144, 1080: 4145, 1190: 4146}

# our id -> English name. Authored from the bracketed Japanese role titles, which
# are legible even when the names are not: [神父] is a priest, [教会の衛士] a church
# guard, [スピール戦士] a Spire warrior. Four of the Church names come from Jrose's
# own LIST_EVENT rows rather than LIST_NPC, which give a proper name where the NPC
# table only has a placeholder ("Kシャーマン" is [巫女] ネモ, the shrine maiden).
# ASCII only: Stb.set encodes latin-1.
KARKIA_NPCS = {
    # The Abandoned Church (86)
    4138: "[Priest] Fritz",              4139: "[Archaeologist] Garnia",
    4140: "[Church Guard] Fumo",         4141: "[Church Guard] Ragi",
    4142: "[Shrine Maiden] Nemo",        4143: "[Plague Doctor] Jenner",
    4144: "[Priest] Brown",              4145: "[Church Guard] Kashi",
    4146: "[Explorer] Petri",            4094: "[Mage of Dreams] Ragia",
    # Spire Village (88)
    4016: "[Spire Captain] Blago",       4017: "[Spire Warrior] Sulfa",
    4018: "[Spire Warrior] Dinos",       4019: "[Spire Warrior] Gelt",
    4085: "[Field Medic] Emil",
    # Memories of Karkia (133) -- placed now, reachable when we write that quest
    4088: "[Storagekeeper] Ash",         4089: "[General Store] Ginias",
    4090: "[Naturalist] Pormello",       4091: "[Lovelorn] Miranda",
    4092: "[At Prayer] Beluga",          4093: "[Gatewarden] Lowe",
    4095: "[Mage of Mists] Magia",       4096: "[Mage of the Endless] Nagia",
    4097: "[High Priest] Bordeaux",      4108: "[Starsteel Armourer] Astraea",
    # Foot of the Tower (135)
    4101: "[Gate Registrar] Lombert",    4102: "[Warrant Officer] Holk",
    4103: "[Master Smith] Belfa",        4104: "[Parel Caravan] Orentark",
    4105: "[Artificer] Physalis",
    # Garden of Karkia (144)
    4136: "[Planet Surveyor] Nitraria",  4137: "[Planet Surveyor] Steinia",
}
NPC_STRID_PREFIX = "LKNPC"         # our own STL keys; Jrose's are its own numbering
EVENT_STB_REL = r"3DDATA\STB\LIST_EVENT.STB"
EVENT_FILE_COL = 3                 # EVENT_FILENAME; 0 is the editor name
EVENT_DIR = r"3DDATA\EVENT"

PRESENTATION_COLS = {38: "LIST_EFFECT", 39: "LIST_EFFECT", 40: "FILE_SOUND",
                     41: "FILE_SOUND", 42: "LIST_HITSOUND"}
# Repairs, applied only to weapon rows no pre-existing monster equips.
#   1137: blank in both tables. Its two users are the melee Revived Quarantine
#         Officers, so it is filled from 1131 -- the D-Ghoul's row, humanoid
#         undead melee, which is the closest match we already own and which our
#         own Shaman family shares.
#   1156: col 38 names LIST_EFFECT row 631, which we do not have. The row is
#         imported instead of cleared, because clearing it would also change the
#         *server's* mind about the Evil Fairy: UsesProjectileAttackPresentation()
#         is `bullet_effect > 0`, so a zero there silently converts a projectile
#         attacker into a melee one.
PRESENTATION_DONOR = {1137: 1131}
PRESENTATION_EFFECT_ROWS = [631]

# --- stage 3i: the monsters' skills ---------------------------------------
#
# The Karkia AI casts 23 skill ids through AIACT_24. Diffed row by row against
# ours, 12 are byte-identical and 5 differ only in magnitude -- monster-skill rows
# in the high range of LIST_SKILL are inherited and nobody edited them. Six need
# attention and none needs authoring. See doc/karkia-roadmap.md §6.
#
# Ported into their own row numbers, which are blank here. Cols 0-85 align by
# meaning; **col 86 does not** -- ours is the STL key and theirs is
# AVAILABLE_STATUS -- so it is left alone.
#
# Col 0 is *not* copied either: theirs is a Japanese editor label, and copying the
# cp932 bytes into our UTF-8 table yields mojibake ("aX^i5bj"). The server reads
# SKILL_NAME from col 0 while the client shows the STL name, so an ASCII label is
# both correct and readable. These names are authored.
#
# The last five were missed by the original survey, which walked only the 31
# *spawned* monsters' AI files -- the seven AI-summoned-only monsters have AI rows
# of their own and brought five more skills with them. Walking each monster's
# LIST_NPC col 16 rather than a hand-listed set of filenames is what found them.
SKILL_PORTS = {                    # source row -> (our row, ASCII label)
    3613: (3613, "Karkia Stun"),          # ant stun, 5 s, AoE r3000 power 400
    3616: (3616, "Questarua Burst"),      # type 7, r3000 power 100
    3627: (3627, "Kleitos Silence"),      # type 13, range 3000
    3685: (3686, "Karkia Ward"),          # self DEF+MR buff; 3685 is our GM Blessing
    3711: (3711, "Ferdinand Stun"),       # AoE r4000 power 200, 4 s
    3771: (3771, "Duke Vlad Counter"),    # AoE r5000 power 10
    3779: (3779, "KS Defence Down"),      # AoE r1800 power 300, 60 s
    3780: (3780, "KS Stun"),              # AoE r1500 power 250, 5 s
    3781: (3781, "KS Poison"),            # single target, power 350, 30 s
}
# Cast by the AI but already ours under another number, so the .aip is re-pointed
# rather than the row being duplicated:
#   716 is Jrose's Champion Berserk rank 1 of 5; ours is a 20-rank family and 361
#       is the matching rank position -- the least invented choice, and stage 4
#       can raise it.
#   846 is their Tornado rank 1 of 2 at power 500. Our Tornado family only runs
#       ranks 6-10, and rank 10 (row 1090) is power 501 -- matched on power rather
#       than on rank, since the rank numbering does not correspond.
SKILL_REPOINT = {716: 361, 846: 1090}

# Skills the AI is stopped from ever casting, by zeroing the percentage on the
# AICOND_07 that gates their event. `Get_RANDOM(100) < 0` is never true, so the
# event is permanently dead -- which is how the source data itself disables things
# (both Hebarn Officers ship a 0% self-heal), so it needs no new mechanism and is a
# single byte to put back.
#
# 1090 Tornado, on the Revived Quarantine Officer (2703) and its Alpha (2692).
#   Reported in game as damage with no animation and no visible cast, and a run of
#   "blank hits" ending in a death. The rates say the report is not bad luck:
#   kak_livingdead.aip has a damaged-rate of *100%*, so 2703 rolled this on 10% of
#   every hit it took. Adding the missing casting/skill anim pair (a86dbbff) fixed
#   the monster's animation but evidently not the skill's own presentation.
#
#   The likely reason it is this skill and not the others: 1090 is one of only two
#   *re-pointed* skills (SKILL_REPOINT), i.e. one of OUR player skills handed to a
#   monster, rather than a row ported from Jrose. Its sibling 361 Berserk is a
#   self-buff and shows its own effect, so it looks fine; an AoE damage skill has to
#   present a hit on a target it was never authored to reach. Left as a known
#   unknown rather than chased -- removing it costs one AI action on one monster.
#
#   Explained and brought back 2026-11-01 by fix-quarantine-officer-skills.py: the
#   release clip was a plain swing (event 21 only: nothing presents an area hit), and
#   1090 is the player's power 501 / 14 m. That script gives both officers the
#   player's own Tornado clips on slots 8/9 (retargeted) and a monster row of their
#   own (7202), and the player's Berserk clips on 6/7. It stays disabled HERE on
#   purpose -- the event this stage writes still names 1090 on slots 6/7 -- so re-run
#   that script after this stage (and after a CHR stage, which puts back 6/7).
#
# NOT disabled, for contrast: 3613 Karkia Stun on Murilo (2715/2696) was also never
# seen in game, but that one is fully explained without a defect -- 35% damaged-rate
# x a 10% roll x "target has no harmful status" is ~3.5% of hits taken, and its
# motion pair (8/9) is verified present on both casters. It is a ported row, not a
# re-pointed player skill. Add 3613 here if it turns out to misbehave too.
AIP_DISABLE_SKILLS = {
    1090: "Tornado: damage with no visible cast, 10% of every hit taken on 2703",
}
SKILL_STB_REL = r"3DDATA\STB\LIST_SKILL.STB"
SKILL_COPY_COLS = 86               # 0-85; 86 is our STL key, theirs is not
AIACT_USE_SKILL = 25               # AIACT_24 as stored (file ids are 1-based)
AIACT24_SKILL_OFF = 10             # dwSize(4) Type(4) btTarget(1) pad(1) nSkill
AIACT24_MOTION_OFF = 12            # ...then nMotion

# A monster's skill animation does NOT come from the skill table. CObjMOB stores
# the .aip record's nMotion and hands out GetANI_Casting() == nMotion and
# GetANI_Skill() == nMotion + 1 (cobjnpc.h), indexing MOB_ANI_* in datatype.h:
#   0 STOP  1 MOVE  2 ATTACK  3 HIT  4 DIE  5 RUN
#   6 CASTION01  7 SKILL_ACTION01  8 CASTION02  9 SKILL_ACTION02  10 ETC
# So the only meaningful nMotion values are 6 and 8 -- they name a casting slot,
# and the skill lands on that slot's action anim. Both halves must exist in
# LIST_NPC.CHR or the presentation is silently skipped: CCharMODEL::GetMOTION
# returns NULL for an unmapped type and Chg_CurMOTION(NULL) is a no-op, so the
# monster keeps whatever clip it was already playing while the skill still fires
# and still deals damage. That is the "took damage, saw no animation" report.
#
# Auditing every AIACT_24 across all 38 monsters found exactly one model at
# fault, and it is a Jrose defect, not an import one -- their CHR carries the
# same six anims. The sordmaster/warrior1 set has no casting or skill clip
# anywhere in either dump, so the two slots are filled from the six that exist:
# warring (2.17 s, the alert pose) telegraphs the cast, attack (1.97 s) releases
# it. Note warring doubles as this model's idle, so a cast begun from a standing
# idle will not visibly restart -- the skill frame always changes, which is the
# half that was missing.
CHR_MOTION_FILL = {
    # npc -> {anim type: motion pool path}
    2692: {6: r"3Ddata\MOTION\NPC\warrior1\warrior1_warring_01.ZMO",
           7: r"3Ddata\MOTION\NPC\warrior1\warrior1_attack_01.ZMO"},
    2703: {6: r"3Ddata\MOTION\NPC\warrior1\warrior1_warring_01.ZMO",
           7: r"3Ddata\MOTION\NPC\warrior1\warrior1_attack_01.ZMO"},
    # Mukuroji (stage 7b) is the same defect on a different model. It uses
    # Pig1, whose CHR carries only the six basic clips in *both* dumps, and its
    # .aip casts skill 3050 on nMotion **8** -- so the missing pair is 8/9
    # (CASTION02 / SKILL_ACTION02), not 6/7. Filled the same way: warnnimg
    # (1.70 s, the alert pose) telegraphs, attack (1.90 s) releases.
    2539: {8: r"3Ddata\MOTION\NPC\pig01\pig01_warnnimg.ZMO",
           9: r"3Ddata\MOTION\NPC\pig01\pig01_attack.ZMO"},
}
# Slots that EXIST but hold the wrong clip. Orgeid (2723, kcemetery) casts 3575
# on nMotion 8, so the client plays slot 8 to cast and slot 9 to release -- and
# Jrose's CHR (copied verbatim by this stage) fills 8 AND 9 with casting_01.ZMO,
# a clip with no action frames, while the real release clip status_01.ZMO (frames
# 24/34, the projectile launch) sits unused in slot 7. The bolt never fired: the
# server's damage landed on a later melee frame with no visual, and the queued
# status payload timed out (found by the 2026-09-14 "projectile skill with no
# launch frame" survey; Mukuroji was the other spawned case). Revived Veteran
# Warrior (2688, unspawned) has the identical shape for KS Poison. Applied by
# chr_override_motions, idempotent, and also by scripts/fix-chr-skill-slots.py
# without a full --stage 3.
CHR_MOTION_OVERRIDE = {
    2723: {9: r"3Ddata\MOTION\NPC\orphe01\status_01.ZMO"},
    2688: {9: r"3Ddata\MOTION\NPC\Mummy_Female02\Mummy_Female03_status_skill01.ZMO"},
}
MOB_ANI_MAX = 11                   # MAX_MOB_ANI; the client indexes with no bound check

# Fraction of a zone's regen points to KEEP. 1.0 (or absent) ships Jrose's layout.
#
# Karkia is authored as a carpet where our zones are clumps: one monster per regen
# point, points ~5 m apart, against our 5-10 per point 20-45 m apart.
#
# **Empty on purpose since 2026-09-12.** KSPIREVIL used to sit here at 0.25, sized
# against a 200 m bodies-and-mesh-draws model that counted a point as one monster.
# It is four (see regen_set_tacticpoint), so that model -- and every density number
# in doc/karkia-roadmap.md -- was 4x low, which is why a 75% cut still played as a
# wall. Both main maps now go through SPAWN_CAMPS below, which merges neighbouring
# points instead of deleting them and so cannot lose a species. Thinning stays
# available for a zone whose density genuinely *is* its point count.
SPAWN_THINNING = {}

# Camp consolidation, for the two main maps: folder -> (grid cell in cm, camp size).
#
# These two zones are a *carpet* -- one regen point per monster, points ~5 m
# apart, 847 of them in the Cemetery and 1,111 in Spire Village. Thinning alone
# is the wrong lever for them, for two reasons found on 2026-09-12:
#
# 1. **Every point is single-species.** All five basic slots and both tactics
#    slots of every one of those 1,958 points hold the *same* npc id. So the
#    escalation machine buys nothing, and the only thing that makes the zone
#    varied is which points happen to be near you -- which means deleting points
#    deletes species. thin_regen is greedy on nearest-neighbour distance and
#    completely species-blind: at 15% keep it drops Evil Fairy, Orgeid and D=Seed
#    out of the Cemetery entirely.
#
# 2. **Each point holds four monsters, not one.** See regen_set_tacticpoint --
#    tacticPoint 1 puts the very first tick in the top branch of the table.
#
# So instead of deleting points we *merge* them. Bin the zone on a grid, keep one
# record per bin, and give it the species of everything that bin absorbed: the
# result is one camp where there were six or seven singles, holding a mix rather
# than a clone. Population falls because the bin's 6-7 points become one point of
# five bodies; diversity *rises* because 85% of the camps come out multi-species
# where every source point was single-species.
#
# Measured against the Jrose source with CRegenPOINT::Proc's real branch table:
#
#              camps  bodies   was   per 100m cell   vs JG07   camp gap   species
#   KCEMETERY    113     565  3,388       8.8         0.27x     38.8 m     21/21
#   KSPIREVIL     77     385  1,109      12.4         0.39x     37.8 m     10/10
#
# JG07, our densest field zone, is 32.2 bodies per cell. The ~38 m median gap
# between camps sits well outside the 13-22 m aggro radii the Karkia .aip files
# declare, so a pull is one camp of five.
#
# KSPIREVIL used to carry SPAWN_THINNING 0.25 (1,111 -> 278 points). That is gone:
# consolidation supersedes it and running both would cut twice. thin_regen itself
# stays -- it is still the right lever for a zone whose density really is its
# point count.
SPAWN_CAMPS = {"KCEMETERY": (6000, 5), "KSPIREVIL": (6000, 5)}

# What a consolidated camp gets written with. interval paces the escalation as
# well as the respawn -- a camp needs `size` ticks to fill, so 20 s means a
# cleared camp is back in ~100 s. Jrose ran these maps at 20-565 s; JG07 runs
# 5-12 s; Eldeon EZ01 runs 30-40 s.
CAMP_INTERVAL = 20
CAMP_RANGE = 12          # metres; five bodies need more room than Jrose's 5 m
CAMP_TACTIC_POINT = 100  # the house value -- see regen_set_tacticpoint

# Which monsters a zone's regen points actually spawn, as
#   folder -> (basic [(npc, count), ...], tactics [(npc, count), ...])
#
# Jrose laid out 53 regen points across these three zones and put **id 1 in
# every slot** -- their own files, not our import -- so nothing here is being
# restored. Every point carries five basic slots and two tactics slots with a
# concurrent cap of seven, and the tactics list is the escalation set: it comes
# out once the point's tactic points build up, which makes it the right home for
# an elite. Unused slots are written as id 0, which the loader skips.
#
# This lives in stage 3 for the same reason SPAWN_THINNING does: the stage
# rebuilds every REGEN lump from Jrose's source on each run, so an
# after-the-fact edit would be silently undone by the next --stage 3.
#
# The rosters come from what the NPCs ask for, which is the only authority there
# is now that the source slots are empty:
#   Memories  -- Pormello's ecology quests name Melitta, Melan Melitta and
#                Calaplasinos, and a swarm with a queen behind it.
#   Garden    -- Nitraria collects leaves from Mukuroji, Nigaki and Beeberu.
#   Burned Forest is NOT a flashback zone: its gates run to and from the
#                Cemetery, so it is present-day Karkia and gets present-day
#                monsters. The Church's own timber request names Woodnoids and
#                Dark Towers, which is exactly a burned forest's roster.
# Concurrent-alive cap (CRegenPOINT m_iLimitCNT) to force onto every regen point
# in a zone. None means "leave Jrose's".
#
# This is the lever these three zones need, and SPAWN_THINNING is not. Karkia's
# main zones are built as **many points at cap 1** -- the Cemetery is 847 points
# holding 847 monsters -- while the flashback zones are **few points at cap
# 10-15**: 23 points holding 265 in the Burned Forest. Each point is therefore a
# nest that refills up to fifteen bodies into a 10 m radius on a 60 s timer, and
# the maps are small enough (Memories' whole population fits inside one 200 m
# view) that there is nowhere to stand that is not inside one.
#
# Measured as monsters per 100 m cell against JG07, our densest field zone:
# Burned Forest and Memories both sat at 0.66x and the Garden at 0.61x, which
# looks reasonable until you notice Memories spends all of it in five cells
# where JG07 spreads over twenty-six. Capping at 5 lands them near 0.3x, which
# is where Spire Village ended up after its 75% thin -- the density already
# signed off as "much better, still an invasion feeling".
SPAWN_CAP = {"KMEMORIES": 5, "KFLOWERGARDEN": 5, "KBURNEDFOREST": 5}

# Seconds between regen ticks (CRegenPOINT m_iInterval). None means "leave
# Jrose's", which for these three zones is 120 -- five to twenty times slower
# than retail Junon's 5-25s. The interval is not just respawn speed: a point
# advances *one step* of its escalation per tick (see SPAWN_ROSTER below), so
# at 120s the second monster species in a nest is two minutes behind the first
# and the fifth is ten.
SPAWN_INTERVAL = {"KMEMORIES": 15, "KFLOWERGARDEN": 15, "KBURNEDFOREST": 15}

# The per-slot counts below are 1 **on purpose, and they must stay well under
# SPAWN_CAP.** CRegenPOINT::Proc is not a spawn list, it is an escalation state
# machine: each tick it picks one or two slots -- never all five -- and it
# returns early doing nothing at all while liveCNT >= limitCNT. Shipping count 5
# against a cap of 5 meant slot 0 alone filled the point exactly on the first
# tick, so slots 1-4 were unreachable until a player cleared the nest by hand.
# In game that read as "the zone only has Woodnoids", then Dark Towers after you
# killed them, then Evil Fairies -- one species at a time, forever.
#
# Retail is the model: JG01's points run counts of 1-2 against caps of 2-13. At
# count 1 and cap 5 a point fills to four species in five ticks and holds the
# same five bodies it held before, so this costs no density.
#
# A repeated id is how a roster weights a species (Memories wants twice as much
# Melitta as Calaplasinos); repeat the slot, do not raise the count.
SPAWN_ROSTER = {
    "KMEMORIES": ([(2528, 1), (2528, 1), (2529, 1), (2529, 1), (2527, 1)],
                  [(2530, 1), (2527, 1)]),
    "KFLOWERGARDEN": ([(2539, 1), (2547, 1), (2549, 1), (2539, 1), (2547, 1)],
                      [(2549, 1), (2539, 1)]),
    "KBURNEDFOREST": ([(2719, 1), (2720, 1), (2725, 1), (2727, 1), (2728, 1)],
                      [(2720, 1), (2725, 1)]),
}

# Where a synthetic LUMP_ECONOMY comes from. Any of our zones would do -- 50 of our
# 55 carry the identical 74-byte block -- but a populated Junon field zone gives
# sane non-zero town figures rather than the all-zero ones some Oro zones carry.
ECONOMY_TEMPLATE_REL = r"3DDATA\MAPS\JUNON\JG01\JG01.ZON"

# .ZON lump ids (client io_terrain.h ZONE_LUMP_TYPE); note these are a *different*
# enum from the .IFO lump ids, which import-oro.py owns.
ZON_LUMP_INFO, ZON_LUMP_EVENT, ZON_LUMP_TILE = 0, 1, 2
ZON_LUMP_BRUSHES, ZON_LUMP_ECONOMY = 3, 4

# CEconomy::Load reads three ints then MAX_PRICE_TYPE - MIN_PRICE_TYPE of them
# (datatype.h: 1 and 11), after three pascal strings. Anything past that is
# editor slack the reader never touches.
ECONOMY_MIN_INTS = 3 + (11 - 1)


def load_oro():
    """The shared data layer (scripts/rosedata): codecs, table schema, importer helpers."""
    import rosedata
    return rosedata


oro = load_oro()

# Which stage refills each entity lump stage 1 emptied. --verify reports all four
# and only flags the ones whose stage has not run. (Below load_oro() because it
# names import-oro's lump enum rather than restating the numbers.)
LUMP_STAGE = {oro.LUMP_WARP: "gates", oro.LUMP_REGEN: "spawns",
              oro.LUMP_MOB: "npcs", oro.LUMP_EVENT_OBJECT: "events"}


# ------------------------------------------------------------------- helpers
# import-oro.py's ABS_ASSET_RE matches only .ptl/.dds/.tga/.zms, so an .eft that
# animates a mesh -- and names the .zmo that drives it -- loses the motion. That
# is not theoretical: JGTFOODSHOP_NIGHT01.EFT names JGTFOODSHOP_NIGHT01.ZMO, and
# the client pops a modal "open error" box for it on entering Spire Village. 14
# .zmo across the Karkia effect chain were missed that way.
#
# .eft is included so the walk is transitive through nested effects, and .mrp
# (morph targets) because an animated-building .eft names one of those too.
EFFECT_ASSET_RE = re.compile(
    rb"3DDATA[\\/][0-9A-Za-z_\\/. -]+?\.(?:ptl|dds|tga|zms|zmo|eft|mrp)", re.I)
# A .ptl names its textures bare; an .eft names its .mrp bare, relative to itself.
BARE_TEXTURE_RE = re.compile(rb"[0-9A-Za-z_][0-9A-Za-z_-]*\.(?:dds|tga)", re.I)
BARE_SIBLING_RE = re.compile(rb"[0-9A-Za-z_][0-9A-Za-z_.-]*\.(?:mrp|zmo)", re.I)
PARTICLE_TEXTURE_DIR = r"3DDATA\EFFECT\PARTICLES\TEXTURE"


def effect_chain(seeds, src_index):
    """.eft -> .ptl/.mrp/.zmo/.zms -> particle texture, transitively.

    Same shape as import-oro.py's version, with the wider extension set above and
    bare sibling names resolved against the referring file's own directory. Both
    formats are length-prefixed binary; the paths are recovered by pattern rather
    than with a full parser because these fields are all we need and the files are
    a few hundred bytes each.
    """
    out, queue, seen = set(), list(seeds), set()
    while queue:
        rel = queue.pop()
        k = key_of(rel)
        if k in seen:
            continue
        seen.add(k)
        out.add(rel)
        p = src_index.get(k)
        if p is None:
            continue
        blob = open(p, "rb").read()
        for hit in EFFECT_ASSET_RE.finditer(blob):
            queue.append(hit.group().decode("latin-1"))
        # A bare name is a path *reconstruction*, not a reference: we are guessing
        # the directory. Only take it if the guess resolves, because a bare name
        # that does not is usually one already covered by an absolute path
        # elsewhere in the same file (an .eft names both `_pumpkin_01.zmo` and
        # `3DDATA\EFFECT\EFFECTMESH\_PUMPKIN_01.ZMO`), and reporting the failed
        # guess as "missing from source" would be a lie about a file we have.
        # Absolute references stay unguarded, so a genuinely absent one is still
        # reported.
        parent = os.path.dirname(k)
        bare = []
        if k.endswith(".ptl"):
            bare = [os.path.join(PARTICLE_TEXTURE_DIR, h.group().decode("latin-1"))
                    for h in BARE_TEXTURE_RE.finditer(blob)]
        elif k.endswith(".eft"):
            bare = [os.path.join(parent, h.group().decode("latin-1"))
                    for h in BARE_SIBLING_RE.finditer(blob)]
        queue += [b for b in bare if key_of(b) in src_index]
    return out


def key_of(rel):
    """Data-relative lookup key: backslashes, separator runs collapsed, lowercase.

    Collapsing runs is not tidiness. Several Jrose .eft and .ptl files embed a
    C-escaped path *literally* -- the bytes on disk really are
    `3DData\\\\Effect\\\\Particles\\\\_fire_02.dds` -- and a lookup that takes
    them at face value reports 19 files that are present in both trees as
    missing from the source. `zz_slash_converter` (engine/include/zz_string.h)
    performs exactly this collapse before every VFS open, which is why the engine
    resolves them and why matching it is the right fix rather than special-casing
    the offending files.
    """
    if isinstance(rel, bytes):
        rel = rel.decode("latin-1")
    out = []
    for ch in rel:
        if ch in "\\/":
            if out and out[-1] == "\\":
                continue
            out.append("\\")
        else:
            out.append(ch)
    return "".join(out).lstrip("\\").lower()


def index_tree(root):
    """Case-insensitive map of data-relative path -> real path.

    Windows would resolve the case for us, but relying on that hides a genuinely
    missing file behind a lookup that happens to work on one machine. Building the
    index means "not in source" is reported rather than discovered at bake time.
    """
    idx = {}
    for base, _, names in os.walk(root):
        rel = os.path.relpath(base, root)
        if rel == ".":
            rel = ""
        for n in names:
            idx[key_of(os.path.join(rel, n))] = os.path.join(base, n)
    return idx


def dest_of(ours, rel):
    """Our tree is uppercase throughout; keep it that way."""
    return os.path.join(ours, key_of(rel).upper().replace("\\", "/"))


def copy_new(rels, src_index, ours, dry, label):
    """Copy data-relative paths we do not already have. Never overwrites.

    Not overwriting is a correctness property here, not an optimisation: 331 of
    the textures Karkia shares with us are the same art with our mip chains added,
    and Jrose's copies would be a downgrade.
    """
    new, total, missing, new_dds = 0, 0, [], []
    for rel in sorted({key_of(r) for r in rels}):
        s = src_index.get(rel)
        if s is None:
            missing.append(rel)
            continue
        d = dest_of(ours, rel)
        if os.path.isfile(d):
            continue
        new += 1
        total += os.path.getsize(s)
        if rel.endswith(".dds"):
            new_dds.append(rel)
        if not dry:
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copyfile(s, d)
    print(f"    {label:26s} {new:5d} new files  {total / 1048576:7.2f} MB"
          + (f"   ({len(missing)} NOT IN SOURCE)" if missing else ""))
    for m in missing[:8]:
        print(f"        missing from source: {m}")
    return new, total, missing, new_dds


# -------------------------------------------------------------------- ZON I/O
# Same container as an .IFO -- i32 lump_count, lump_count x (i32 type, i32 absolute
# offset), then the blocks contiguously in table order -- but a different lump
# enum. import-oro.py's read_ifo/build_ifo work on it unchanged.
def zon_lumps(path):
    return oro.read_ifo(path)


def economy_template(ours):
    """The raw LUMP_ECONOMY block of one of our zones, checked for shape.

    Copied verbatim rather than synthesised: the block is three pascal strings
    followed by 13 ints, and taking a real one means the layout cannot drift from
    what CZoneFILE::ReadECONOMY actually reads.
    """
    p = os.path.join(ours, ECONOMY_TEMPLATE_REL.replace("\\", "/"))
    if not os.path.isfile(p):
        raise SystemExit(f"economy template missing: {p}")
    buf, bounds = zon_lumps(p)
    off, end = oro.lump_block(bounds, ZON_LUMP_ECONOMY)
    if off is None:
        raise SystemExit(f"{p}: no LUMP_ECONOMY to use as a template")
    blk = buf[off:end]
    parse_economy(blk, p)                      # refuse a template we cannot read
    return blk


def parse_economy(blk, where):
    """Walk a LUMP_ECONOMY block exactly as CZoneFILE::ReadECONOMY does."""
    o = 0
    for _ in range(3):                         # zone name, BGM, background model
        if o >= len(blk):
            raise SystemExit(f"{where}: economy block truncated in the strings")
        n = blk[o]; o += 1 + n
        if o > len(blk):
            raise SystemExit(f"{where}: economy string runs past the block")
        if _ == 0:
            o += 4                             # i32 m_iIsDungeon follows the name
    if (len(blk) - o) < 4 * ECONOMY_MIN_INTS:
        raise SystemExit(f"{where}: economy block has {(len(blk) - o) // 4} ints, "
                         f"needs {ECONOMY_MIN_INTS}")
    return struct.unpack_from(f"<{ECONOMY_MIN_INTS}i", blk, o)


def zon_with_economy(path, template):
    """Return the .ZON bytes with a LUMP_ECONOMY appended, or None if it has one.

    Appended last so the lump table stays in ascending offset order, which is what
    read_ifo asserts and what both the client and server loaders assume when they
    seek per lump.
    """
    buf, bounds = zon_lumps(path)
    if oro.lump_block(bounds, ZON_LUMP_ECONOMY)[0] is not None:
        return None
    blocks = [buf[off:end] for _, off, end in bounds] + [template]
    types = [t for t, _, _ in bounds] + [ZON_LUMP_ECONOMY]
    out = [struct.pack("<i", len(types))]
    cur = 4 + 8 * len(types)
    for t, blk in zip(types, blocks):
        out.append(struct.pack("<ii", t, cur))
        cur += len(blk)
    out.extend(blocks)
    return b"".join(out)


# ------------------------------------------------------------------ selftest
def selftest(ours, src, src_index):
    """Every writer must reproduce its input byte-for-byte with no edits."""
    ok = True

    def check(label, name, same):
        nonlocal ok
        ok = ok and same
        print(f"    {label:34s} {'OK' if same else 'FAIL'}   {name}")

    for rel in (ZONE_STB_REL, SKY_STB_REL, WARP_STB_REL,
                NPC_STB_REL, AI_STB_REL, WEAPON_STB_REL):
        p = os.path.join(ours, rel.replace("\\", "/"))
        check("STB round-trip", os.path.basename(p),
              oro.Stb(p).to_bytes() == open(p, "rb").read())

    for rel in (ZONE_STL_REL, NPC_STL_REL):
        p = os.path.join(ours, rel.replace("\\", "/"))
        check("STL round-trip", os.path.basename(p),
              oro.Stl(p).to_bytes() == open(p, "rb").read())

    p = os.path.join(ours, NPC_CHR_REL.replace("\\", "/"))
    check("CHR round-trip", os.path.basename(p),
          oro.Chr(p).to_bytes() == open(p, "rb").read())
    p = os.path.join(ours, PART_NPC_ZSC_REL.replace("\\", "/"))
    check("ZSC round-trip", os.path.basename(p),
          oro.Zsc(p).to_bytes() == open(p, "rb").read())

    # Karkia's own containers, plus a couple of ours, through the shared readers.
    probes = []
    for _, folder, _, _ in ZONES:
        d = os.path.join(src, MAPS_REL.replace("\\", "/"), folder)
        if os.path.isdir(d):
            probes += [os.path.join(d, f) for f in sorted(os.listdir(d))
                       if f.lower().endswith(".ifo")][:4]
    mine = os.path.join(ours, r"3DDATA\MAPS\ELDEON\EJT01".replace("\\", "/"))
    if os.path.isdir(mine):
        probes += [os.path.join(mine, f) for f in sorted(os.listdir(mine))
                   if f.lower().endswith(".ifo")][:3]
    cont_ok = lump_ok = True
    for p in probes:
        buf, bounds = oro.read_ifo(p)
        cont_ok = cont_ok and oro.build_ifo(bounds, buf, {}) == buf
        for lt in oro.LUMPS_STAGE1_EMPTY:
            if oro.lump_block(bounds, lt)[0] is None:
                continue
            off, end = oro.lump_block(bounds, lt)
            objs, trailing = oro.read_lump(buf, bounds, lt)
            if oro.build_object_lump(objs, trailing) != buf[off:end]:
                lump_ok = False
                print(f"        lump {lt} round-trip FAILED in {p}")
    check("IFO container rebuild", f"{len(probes)} files", cont_ok)
    check("IFO lump decode round-trip", "MOB/REGEN/WARP/EVENT_OBJECT", lump_ok)

    # The three REGEN field writers. Each must leave the record's length alone
    # (the lump is not repacked around them) and must not disturb the other two
    # fields -- they share one offset walk over two variable-length mob lists,
    # so an off-by-one here writes a plausible number into the wrong field.
    field_ok, probed = True, 0
    for p in probes:
        buf, bounds = oro.read_ifo(p)
        if oro.lump_block(bounds, oro.LUMP_REGEN)[0] is None:
            continue
        objs, _tail = oro.read_lump(buf, bounds, oro.LUMP_REGEN)
        for obj in objs:
            extra = obj["extra"]
            _iv0, _cap0, rng0, tp0 = regen_get_params(extra)
            got = regen_set_interval(regen_set_cap(extra, 5), 15)
            if len(got) != len(extra) or regen_get_params(got) != (15, 5, rng0, tp0):
                field_ok = False
            # All four trailing fields at once: each writer must land on its own
            # i32 and leave the other three alone.
            got = regen_set_tacticpoint(
                regen_set_range(regen_set_interval(regen_set_cap(extra, 5), 20), 12), 100)
            if len(got) != len(extra) or regen_get_params(got) != (20, 5, 12, 100):
                field_ok = False
            # A no-op rewrite of the roster must reproduce the record exactly.
            basic, tactics = regen_roster_of(extra)
            if regen_set_roster(extra, basic, tactics) != extra:
                field_ok = False
            probed += 1
    check("REGEN field writers", f"{probed} points", field_ok)

    # Camp consolidation on the real Cemetery carpet: it must lose no species,
    # leave every record the length it was, put every survivor on a sane cap and
    # tacticPoint, and reproduce itself exactly on a second run.
    camp_ok, camp_note = True, "no source"
    csrc = os.path.join(src, "3DDATA", "MAPS", "KARKIA", "KCEMETERY")
    if os.path.isdir(csrc):
        per_file = {}
        for fn in sorted(os.listdir(csrc)):
            if not fn.upper().endswith(".IFO"):
                continue
            cbuf, cbounds = oro.read_ifo(os.path.join(csrc, fn))
            if oro.lump_block(cbounds, oro.LUMP_REGEN)[0] is None:
                continue
            objs, _t = oro.read_lump(cbuf, cbounds, oro.LUMP_REGEN)
            if objs:
                per_file[("KCEMETERY", fn)] = objs

        def species(pf):
            out = set()
            for objs in pf.values():
                for o in objs:
                    b, t = regen_roster_of(o["extra"])
                    out |= {n for n, _c in b + t if n >= 1}
            return out

        before_sp = species(per_file)
        lens = {(k, i): len(o["extra"])
                for k, v in per_file.items() for i, o in enumerate(v)}
        kept, absorbed, camps = consolidate_camps(per_file, 6000, 5)
        after_sp = species(kept)
        if after_sp != before_sp:
            camp_ok = False
            camp_note = f"species lost: {sorted(before_sp - after_sp)}"
        for objs in kept.values():
            for o in objs:
                iv, cap, rng, tac = regen_get_params(o["extra"])
                if (iv, cap, rng, tac) != (CAMP_INTERVAL, 5, CAMP_RANGE,
                                           CAMP_TACTIC_POINT):
                    camp_ok = False
                b, t = regen_roster_of(o["extra"])
                if any(c != 1 for n, c in b + t if n >= 1):
                    camp_ok = False   # a per-slot count near the cap strands slots
        if len(set(lens.values())) and any(
                len(o["extra"]) not in set(lens.values())
                for objs in kept.values() for o in objs):
            camp_ok = False
        # Deterministic: the same input must give byte-identical output twice.
        again, _a2, c2 = consolidate_camps(per_file, 6000, 5)
        if c2 != camps or [o["extra"] for k in sorted(again) for o in again[k]] !=                 [o["extra"] for k in sorted(kept) for o in kept[k]]:
            camp_ok = False
            camp_note = "not deterministic"
        if camp_ok:
            camp_note = (f"{absorbed} points -> {camps} camps, "
                         f"{len(after_sp)} species kept")
    check("REGEN camp consolidation", camp_note, camp_ok)

    # The economy splice: it must add exactly one lump, leave every other block
    # byte-identical, and produce something ReadECONOMY can walk.
    try:
        tmpl = economy_template(ours)
    except SystemExit as e:
        check("economy template", str(e), False)
        return ok
    check("economy template parses", f"{len(tmpl)} bytes", True)

    splice_ok = True
    for _, folder, _, _ in ZONES:
        d = os.path.join(src, MAPS_REL.replace("\\", "/"), folder)
        if not os.path.isdir(d):
            continue
        zon = [f for f in os.listdir(d) if f.lower().endswith(".zon")]
        if not zon:
            continue
        p = os.path.join(d, zon[0])
        buf, bounds = zon_lumps(p)
        blob = zon_with_economy(p, tmpl)
        if blob is None:
            splice_ok = False
            print(f"        {zon[0]} already has a LUMP_ECONOMY -- unexpected")
            continue
        tmp = io.BytesIO(blob)
        nb, nbounds = _bounds_from_bytes(blob)
        if [t for t, _, _ in nbounds] != [t for t, _, _ in bounds] + [ZON_LUMP_ECONOMY]:
            splice_ok = False
            print(f"        {zon[0]} lump table wrong after splice")
            continue
        for (t, o1, e1), (t2, o2, e2) in zip(bounds, nbounds):
            if buf[o1:e1] != nb[o2:e2]:
                splice_ok = False
                print(f"        {zon[0]} lump {t} changed during splice")
        eo, ee = oro.lump_block(nbounds, ZON_LUMP_ECONOMY)
        parse_economy(nb[eo:ee], zon[0])
    check("ZON economy splice", f"{len(ZONES)} zones", splice_ok)
    return ok


def _bounds_from_bytes(blob):
    """read_ifo, but on an in-memory blob (used to verify a splice before writing)."""
    n, = struct.unpack_from("<i", blob, 0)
    tab = [struct.unpack_from("<ii", blob, 4 + 8 * i) for i in range(n)]
    bounds = [(t, off, tab[i + 1][1] if i + 1 < n else len(blob))
              for i, (t, off) in enumerate(tab)]
    return blob, bounds


# ------------------------------------------------------- stage 1: empty zones
def stage1(ours, src, src_index, dry):
    print("stage 1 -- terrain, art, zone rows, zone names")

    src_maps = os.path.join(src, MAPS_REL.replace("\\", "/"))
    dst_maps = os.path.join(ours, MAPS_REL.replace("\\", "/"))
    template = economy_template(ours)

    # --- 1a. maps, with the entity lumps emptied and an economy lump spliced in
    copied = rewritten = spliced = total = 0
    for row, folder, _, _ in ZONES:
        s = os.path.join(src_maps, folder)
        d = os.path.join(dst_maps, folder)
        if not os.path.isdir(s):
            raise SystemExit(f"source map folder missing: {s}")
        # Recursive: every chunk has a <x>_<y>/LIGHTMAP subdirectory the client
        # loads separately (CTERRAIN::LoadLightMapINFO), so a flat copy loses
        # lighting without erroring.
        for base, _, names in os.walk(s):
            sub = os.path.relpath(base, s)
            dbase = d if sub == "." else os.path.join(d, sub)
            for name in sorted(names):
                sp, dp = os.path.join(base, name), os.path.join(dbase, name)
                if os.path.isfile(dp):
                    continue
                low = name.lower()
                if low.endswith(".ifo"):
                    buf, bounds = oro.read_ifo(sp)
                    repl = {}
                    for lt in oro.LUMPS_STAGE1_EMPTY:
                        off, end = oro.lump_block(bounds, lt)
                        if off is None or buf[off:off + 4] == b"\0\0\0\0":
                            continue
                        _, trailing = oro.read_lump(buf, bounds, lt)
                        repl[lt] = oro.build_object_lump([], trailing)
                    blob = oro.build_ifo(bounds, buf, repl) if repl else buf
                    rewritten += 1 if repl else 0
                elif low.endswith(".zon"):
                    blob = zon_with_economy(sp, template)
                    if blob is None:
                        blob = open(sp, "rb").read()
                    else:
                        spliced += 1
                else:
                    blob = None
                if not dry:
                    os.makedirs(dbase, exist_ok=True)
                    if blob is None:
                        shutil.copyfile(sp, dp)
                    else:
                        with open(dp, "wb") as fh:
                            fh.write(blob)
                copied += 1
                total += os.path.getsize(sp)
    print(f"    {'map files':26s} {copied:5d} new files  {total / 1048576:7.2f} MB")
    print(f"    {'':26s}       {rewritten} .IFO with entity lumps emptied, "
          f"{spliced} .ZON given a LUMP_ECONOMY")

    # --- 1b. terrain tiles named by each .ZON's tile lump
    tiles = set()
    for _, folder, _, _ in ZONES:
        d = os.path.join(src_maps, folder)
        zon = [f for f in os.listdir(d) if f.lower().endswith(".zon")][0]
        tiles |= {t for t in oro.zon_tiles(os.path.join(d, zon))
                  if "\\" in t or "/" in t}
    new_dds = []
    _, _, _, tile_dds = copy_new(tiles, src_index, ours, dry, "terrain tiles")
    new_dds += tile_dds

    # --- 1c. the object tables and every file they name
    zsc_rels = []
    art = set()
    src_art = os.path.join(src, KARKIA_ART_REL.replace("\\", "/"))
    if not os.path.isdir(src_art):
        raise SystemExit(f"source art folder missing: {src_art}")
    for name in sorted(os.listdir(src_art)):
        if not name.lower().endswith(".zsc"):
            continue
        rel = f"{KARKIA_ART_REL}\\{name}"
        zsc_rels.append(rel)
        art |= oro.zsc_asset_refs(oro.Zsc(os.path.join(src_art, name)))
    # ...plus the files the .IFO records name inline. The EFFECT and SOUND lumps
    # each carry a filename in the record rather than an index into a table, so
    # they are invisible to anything that only walks the ZSC tables.
    for _, folder, _, _ in ZONES:
        d = os.path.join(src_maps, folder)
        for name in sorted(os.listdir(d)):
            if not name.lower().endswith(".ifo"):
                continue
            buf, bounds = oro.read_ifo(os.path.join(d, name))
            for lt in (oro.LUMP_EFFECT, oro.LUMP_SOUND):
                objs, _ = oro.read_lump(buf, bounds, lt)
                for o in objs or []:
                    n = o["extra"][0]
                    art.add(o["extra"][1:1 + n].decode("latin-1"))
    art = {a for a in art if "\\" in a or "/" in a}
    art |= effect_chain({a for a in art if a.lower().endswith(".eft")}, src_index)
    copy_new(zsc_rels, src_index, ours, dry, "object tables")
    _, _, _, art_dds = copy_new(art, src_index, ours, dry, "deco/cnst art")
    new_dds += art_dds

    # --- 1d. the sky
    src_sky = oro.Stb(os.path.join(src, SKY_STB_REL.replace("\\", "/")))
    our_sky = oro.Stb(os.path.join(ours, SKY_STB_REL.replace("\\", "/")))
    added = our_sky.grow_to(SKY_ROW + 1)
    want = [src_sky.get(SKY_ROW, c) for c in range(our_sky.cols)]
    if our_sky.d[SKY_ROW] != want:
        for c in range(our_sky.cols):
            our_sky.set(SKY_ROW, c, src_sky.get(SKY_ROW, c))
        our_sky.save(dry)
        print(f"    {'LIST_SKY.STB':26s} +{added} rows, row {SKY_ROW} written")
    else:
        print(f"    {'LIST_SKY.STB':26s} row {SKY_ROW} already present")
    sky = {our_sky.get(SKY_ROW, c).decode("latin-1") for c in range(5)}
    _, _, _, sky_dds = copy_new({x for x in sky if "\\" in x or "/" in x},
                                src_index, ours, dry, "sky assets")
    new_dds += sky_dds

    # --- 1e. zone rows
    src_zone = oro.Stb(os.path.join(src, ZONE_STB_REL.replace("\\", "/")))
    zstb = oro.Stb(os.path.join(ours, ZONE_STB_REL.replace("\\", "/")))
    grown = zstb.grow_to(MAX_ZONE_ROW + 1,
                         labels={r: n for r, _, n, _ in ZONES})
    changed = 0
    for row, folder, name, stl_key in ZONES:
        if not src_zone.occupied(row):
            raise SystemExit(f"source LIST_ZONE row {row} ({folder}) is empty")
        before = list(zstb.d[row])
        for c in range(ZONE_COPY_COLS):
            zstb.set(row, c, src_zone.get(row, c))
        zstb.set(row, ZONE_NAME_COL, name)      # theirs is a Japanese editor label
        zstb.set(row, ZONE_STL_COL, stl_key)    # theirs collides with our zone 82
        if zstb.d[row] != before:
            changed += 1
    print(f"    {'LIST_ZONE.STB':26s} +{grown} rows (now {zstb.rows}), "
          f"{changed} Karkia rows written")
    zstb.save(dry)

    # --- 1f. zone names
    zstl = oro.Stl(os.path.join(ours, ZONE_STL_REL.replace("\\", "/")))
    n = 0
    for _, _, name, key in ZONES:
        if zstl.has(key):
            continue
        zstl.append(key, int(key[4:]), name)
        n += 1
    print(f"    {'LIST_ZONE_S.STL':26s} +{n} keys (now {len(zstl.keys)})")
    if n:
        zstl.save(dry)

    # --- 1g. what still wants doing by hand
    if new_dds:
        print(f"\n    NOTE: {len(new_dds)} new .dds have no mip chain (Jrose ships them")
        print("          flat). Run scripts/add-dds-mipmaps.py over them -- our copies of")
        print("          the shared textures already have chains, which is why nothing")
        print("          here overwrites an existing file.")
    print("    NOTE: BGM lives outside data/ -- copy Sound/BGM/KChurch.ogg, KField.ogg")
    print("          and Boss02.ogg into the deployed game dir if you want music (a")
    print("          missing track is silence, not an error).")
    print("    NOTE: zone 134's minimap is 'NOMAP' in the source, so its minimap panel")
    print("          draws empty. That is Jrose's authoring, not a broken import.")
    print("    NOTE: Karkia keeps its native zone numbers (86-144), which leaves blank")
    print("          LIST_ZONE rows in the gaps. The gameserver skips those on")
    print("          Is_FileExist; the worldserver's Init only tests ZONE_FILE for NULL,")
    print("          not for empty, so each blank row logs one 'zone file open error' at")
    print("          startup and carries on. We already have ~26 such rows -- this adds")
    print("          more noise, not a new failure. Native numbers are kept because the")
    print("          .ZON event names and the revive-zone columns encode them.")
    print("    NOTE: .bak files sit next to every table touched. pack.rs filters only")
    print("          *hidden* entries, so delete them before baking or they go in the .vfs.")


# ---------------------------------------------------------------- AIP I/O
# AI_FILE_HEADER: i32 pattern_count, i32 second, i32 second_of_attack_move,
# i32 title_len, then title_len bytes. Per pattern: char[32] name, i32 events.
# Per event: char[32] name, i32 conds, conds x (u32 size, u32 type, size-8 bytes),
# then i32 acts and the same shape again. Every record is self-sizing, so the walk
# never has to know a record's layout -- only the ones it wants to touch.
def aip_walk_skill_actions(blob):
    """Yield the absolute offset of every AIACT_24 record's nSkill field."""
    o = 0
    npat, _sec, _sec2, ntitle = struct.unpack_from("<iiii", blob, o)
    o += 16 + ntitle
    for _ in range(npat):
        o += 32
        nev, = struct.unpack_from("<i", blob, o); o += 4
        for _ in range(nev):
            o += 32
            ncond, = struct.unpack_from("<i", blob, o); o += 4
            for _ in range(ncond):
                size, = struct.unpack_from("<I", blob, o)
                if size < 8 or o + size > len(blob):
                    raise ValueError(f"bad condition size {size} at {o}")
                o += size
            nact, = struct.unpack_from("<i", blob, o); o += 4
            for _ in range(nact):
                size, typ = struct.unpack_from("<II", blob, o)
                if size < 8 or o + size > len(blob):
                    raise ValueError(f"bad action size {size} at {o}")
                if (typ & 0xFFFF) == AIACT_USE_SKILL:
                    yield o + AIACT24_SKILL_OFF
                o += size


def aip_repoint(path, remap, dry):
    """Rewrite the skill id of every AIACT_24 record named in `remap`.

    Returns [(old, new)] for what changed. Idempotent: a record already carrying
    the new id is not in `remap` and is left alone.
    """
    blob = bytearray(open(path, "rb").read())
    changed = []
    for off in aip_walk_skill_actions(blob):
        cur, = struct.unpack_from("<h", blob, off)
        if cur in remap:
            struct.pack_into("<h", blob, off, remap[cur])
            changed.append((cur, remap[cur]))
    if changed and not dry:
        oro.backup(path)
        with open(path, "wb") as fh:
            fh.write(blob)
        # Re-read and re-walk: a bad offset would corrupt the record silently.
        check = open(path, "rb").read()
        got = {struct.unpack_from("<h", check, o)[0]
               for o in aip_walk_skill_actions(check)}
        if got & set(remap):
            raise SystemExit(f"VERIFY FAILED: {path} still casts {got & set(remap)}")
    return changed


AICOND_RANDOM_PCT = 8              # AICOND_07 as stored; its body is one BYTE cPercent


def aip_walk_events(blob):
    """Yield (pattern, event, [(cond_type, off, size)], [(act_type, off, size)])."""
    o = 0
    npat, _sec, _rate, ntitle = struct.unpack_from("<iiii", blob, o)
    o += 16 + ntitle
    for pi in range(npat):
        o += 32
        nev, = struct.unpack_from("<i", blob, o); o += 4
        for ei in range(nev):
            o += 32
            nc, = struct.unpack_from("<i", blob, o); o += 4
            conds = []
            for _ in range(nc):
                size, typ = struct.unpack_from("<II", blob, o)
                if size < 8 or o + size > len(blob):
                    raise ValueError(f"bad condition size {size} at {o}")
                conds.append((typ & 0xFFFF, o, size)); o += size
            na, = struct.unpack_from("<i", blob, o); o += 4
            acts = []
            for _ in range(na):
                size, typ = struct.unpack_from("<II", blob, o)
                if size < 8 or o + size > len(blob):
                    raise ValueError(f"bad action size {size} at {o}")
                acts.append((typ & 0xFFFF, o, size)); o += size
            yield pi, ei, conds, acts


def aip_disable(path, skills, dry):
    """Zero the AICOND_07 percentage gating every event that casts one of `skills`.

    Returns [(skill, pattern, event)] for what changed. Idempotent: a percentage
    already at 0 is left alone and not reported.

    Refuses an event that carries any action besides the cast, because killing the
    event would silently take those with it. All four events this currently targets
    hold exactly one action, so the guard costs nothing today and stops the next use
    of this config from doing more than it says.
    """
    blob = bytearray(open(path, "rb").read())
    changed = []
    for pi, ei, conds, acts in aip_walk_events(blob):
        casts = [struct.unpack_from("<h", blob, off + AIACT24_SKILL_OFF)[0]
                 for t, off, _s in acts if t == AIACT_USE_SKILL]
        hit = [s for s in casts if s in skills]
        if not hit:
            continue
        if len(acts) != 1:
            raise SystemExit(
                f"{os.path.basename(path)} pattern {pi} event {ei} casts {hit} but "
                f"has {len(acts)} actions -- disabling it would drop the others; "
                f"remove the AIACT_24 record instead of gating the event")
        pct = [off for t, off, _s in conds if t == AICOND_RANDOM_PCT]
        if not pct:
            raise SystemExit(
                f"{os.path.basename(path)} pattern {pi} event {ei} casts {hit} but "
                f"has no AICOND_07 to zero -- it would fire unconditionally")
        for off in pct:
            if blob[off + 8] == 0:
                continue                      # already disabled
            blob[off + 8] = 0
            changed.append((hit[0], pi, ei))
    if changed and not dry:
        oro.backup(path)
        with open(path, "wb") as fh:
            fh.write(blob)
        check = bytearray(open(path, "rb").read())
        for pi, ei, conds, acts in aip_walk_events(check):
            casts = [struct.unpack_from("<h", check, off + AIACT24_SKILL_OFF)[0]
                     for t, off, _s in acts if t == AIACT_USE_SKILL]
            if any(s in skills for s in casts):
                for t, off, _s in conds:
                    if t == AICOND_RANDOM_PCT and check[off + 8] != 0:
                        raise SystemExit(f"VERIFY FAILED: {path} still rolls "
                                         f"{check[off + 8]}% for {casts}")
    return changed


REGEN_POS_OFF = 2 + 2 + 4 + 4 + 4 + 4 + 16   # into the 60-byte fixed object header


def regen_set_roster(extra, basic, tactics):
    """Rewrite a REGEN record's two mob lists in place.

    Layout (CRegenPOINT::Load): a pascal-string point name, then two lists, each
    an i32 count followed by that many (pascal-string mob name, i32 npc,
    i32 count) entries, then interval / limitCNT / range / tacticPoint.

    Only the two i32s per entry are touched and the names are left exactly as
    they are -- every one of them is empty in these files -- so the record's
    length never changes and the surrounding lump needs no repacking. A slot
    beyond the end of the roster is written as npc 0, which `regen_mob_ids` and
    the server both skip.
    """
    out = bytearray(extra)
    o = 1 + out[0]                                   # past the point name
    for wanted in (basic, tactics):
        cnt, = struct.unpack_from("<i", out, o)
        o += 4
        for slot in range(max(0, cnt)):
            o += 1 + out[o]                          # past the mob name
            npc, num = wanted[slot] if slot < len(wanted) else (0, 0)
            struct.pack_into("<ii", out, o, npc, num)
            o += 8
    if len(out) != len(extra):
        raise SystemExit("regen record changed length")
    return bytes(out)


def regen_tail_offset(extra):
    """Byte offset of a REGEN record's four trailing i32s.

    They are interval / limitCNT / range / tacticPoint, in that order, sitting
    after the point name and the two mob lists (CRegenPOINT::Load).
    """
    o = 1 + extra[0]
    for _ in range(2):
        cnt, = struct.unpack_from("<i", extra, o)
        o += 4
        for _ in range(max(0, cnt)):
            o += 1 + extra[o]
            o += 8
    return o


def regen_get_params(extra):
    """(interval, limitCNT, range, tacticPoint) as the record currently holds them."""
    return struct.unpack_from("<4i", extra, regen_tail_offset(extra))


def regen_roster_of(extra):
    """(basic, tactics) as [(npc, count), ...], slots kept in file order.

    oro.regen_mob_ids flattens both lists and drops empty slots; this keeps the
    slot structure, which is what the escalation state machine indexes by.
    """
    o = 1 + extra[0]
    lists = []
    for _ in range(2):
        cnt, = struct.unpack_from("<i", extra, o)
        o += 4
        slots = []
        for _ in range(max(0, cnt)):
            o += 1 + extra[o]
            slots.append(struct.unpack_from("<ii", extra, o))
            o += 8
        lists.append(slots)
    return lists[0], lists[1]


def regen_set_cap(extra, cap):
    """Force a REGEN record's concurrent-alive cap (m_iLimitCNT).

    A single field write, so the record's length is unchanged.
    """
    out = bytearray(extra)
    struct.pack_into("<i", out, regen_tail_offset(out) + 4, cap)
    return bytes(out)


def regen_set_interval(extra, seconds):
    """Force a REGEN record's tick interval (m_iInterval), in seconds.

    First of the four trailing i32s. The server multiplies it by 1000 on load;
    the file holds seconds.
    """
    out = bytearray(extra)
    struct.pack_into("<i", out, regen_tail_offset(out), seconds)
    return bytes(out)


def regen_set_range(extra, metres):
    """Force a REGEN record's spawn radius (m_iRange). Third trailing i32.

    The file holds metres; CRegenPOINT::Load multiplies by 100 into cm. This is
    how wide a point scatters the bodies it spawns, so it wants to grow with the
    concurrent cap or a camp of five stacks on one spot.
    """
    out = bytearray(extra)
    struct.pack_into("<i", out, regen_tail_offset(out) + 8, metres)
    return bytes(out)


def regen_set_tacticpoint(extra, points):
    """Force a REGEN record's tactics divisor (m_iTacticsPOINT). Fourth i32.

    **This is the field that decides whether limitCNT means anything.**
    CRegenPOINT::Proc picks its escalation branch from

        iVar = ((limitCNT*2 - liveCNT) * curTactics * 50) / (limitCNT * tacticPoint)

    and CZoneTHREAD::RegenCharacter has no cap check of its own -- the only gate
    is Proc's early return while liveCNT >= limitCNT. So tacticPoint scales the
    whole table: at 100 an empty point starts at iVar == curTactics == 1, walks
    the branches from the bottom and fills to exactly limitCNT. At 1 the very
    first tick computes iVar = 100, lands in the top branch, and spawns
    basic[4] + tactics[0]+1 + tactics[1] = four bodies into a cap of one.

    Jrose shipped KCEMETERY and KSPIREVIL at tacticPoint 1 -- the only two zones
    in the whole dataset that do; every other zone we ship uses 50-200 -- which
    is why those maps held 3,388 and 1,109 monsters against a summed limitCNT of
    847 and 278. Every density figure anyone measured from the tables was 4x low.
    """
    out = bytearray(extra)
    struct.pack_into("<i", out, regen_tail_offset(out) + 12, points)
    return bytes(out)


def rotate(seq, by):
    """Roster order rotated left, for giving neighbouring points different leads."""
    if not seq:
        return seq
    by %= len(seq)
    return seq[by:] + seq[:by]


def apply_spawn_roster(per_file, folder):
    """Point every regen record in one zone at its roster, cap it, and time it.

    Each point gets the same roster **rotated by its own index**. Slot 0 is the
    one CRegenPOINT::Proc always spawns first and leans on hardest, so an
    unrotated zone leads with a single species everywhere at once -- every nest
    in lockstep, which is what 23 identical points in the Burned Forest did.
    Rotating spreads the lead across the roster the way retail's hand-authored
    points do, without changing what any point eventually holds.

    Ordering is `sorted()` rather than dict order so a re-run reproduces the
    same assignment; this stage rebuilds the lumps from source every time.
    """
    roster = SPAWN_ROSTER.get(folder.upper())
    cap = SPAWN_CAP.get(folder.upper())
    interval = SPAWN_INTERVAL.get(folder.upper())
    if not roster and cap is None and interval is None:
        return 0
    basic, tactics = roster if roster else ([], [])
    n = 0
    for _key, objs in sorted(per_file.items()):
        for obj in objs:
            if roster:
                obj["extra"] = regen_set_roster(
                    obj["extra"], rotate(basic, n), tactics)
            if cap is not None:
                obj["extra"] = regen_set_cap(obj["extra"], cap)
            if interval is not None:
                obj["extra"] = regen_set_interval(obj["extra"], interval)
            n += 1
    return n


def consolidate_camps(per_file, cell_cm, size):
    """Merge a carpet of single-monster points into fewer mixed-species camps.

    Takes {(folder, name): [objects]} for one whole zone and returns the same
    shape. Zone-wide rather than per-file for the reason thin_regen is: the
    positions share one coordinate space (the server adds a single constant bias
    at load), so a chunk-local decision would leave every seam between chunks as
    dense as it started.

    Bin on a `cell_cm` grid, keep one record per bin, and give that record the
    species of everything the bin absorbed. The survivor is the point nearest the
    bin centroid rather than the centroid itself -- an authored position is
    guaranteed to be somewhere a monster already stood, where a computed centroid
    can land in a chasm between two clusters or inside a building.

    Slot assignment, given that every source point is single-species:
      basic   = the bin's distinct species, most locally-common first, up to 5.
                Fewer than 5 distinct repeats the most common to fill, which is
                how a roster weights a species -- never by raising the count.
      tactics = the spill (species 6 and 7) when a bin holds more than 5 distinct,
                so consolidation cannot lose a species even locally; otherwise the
                bin's rarest, which is what the tactics list is for -- it only
                comes out in the top branches, after sustained clearing.

    Every slot count stays 1. It has to: Proc returns early while
    liveCNT >= limitCNT, so a count anywhere near the cap means slot 0 fills the
    camp on tick one and the rest are unreachable until a player clears it by hand.

    Points that are not part of the carpet are passed through untouched --
    anything already on a non-1 tacticPoint was authored deliberately (Spire
    Village's lone Deadly Drake Alpha point, tacticPoint 100 / interval 900 s /
    its own point name). Deterministic throughout so a re-run reproduces it.
    """
    carpet, kept = [], {k: [] for k in per_file}
    for key, objs in sorted(per_file.items()):
        for i, o in enumerate(objs):
            _iv, _cap, _rng, tac = regen_get_params(o["extra"])
            if tac == 1:
                x, y, _z = struct.unpack_from("<fff", o["fixed"], REGEN_POS_OFF)
                basic, tactics = regen_roster_of(o["extra"])
                ids = [n for n, _c in basic + tactics if n >= 1]
                if ids:
                    common = collections.Counter(ids).most_common(1)[0][0]
                    carpet.append((key, i, x, y, common))
                    continue
            kept[key].append(o)          # not carpet: pass through verbatim

    bins = collections.defaultdict(list)
    for entry in carpet:
        _key, _i, x, y, _sp = entry
        bins[(int(x // cell_cm), int(y // cell_cm))].append(entry)

    camps = 0
    for _cell, members in sorted(bins.items()):
        cx = sum(e[2] for e in members) / len(members)
        cy = sum(e[3] for e in members) / len(members)
        key, idx, _x, _y, _sp = min(
            members, key=lambda e: ((e[2] - cx) ** 2 + (e[3] - cy) ** 2, e[0], e[1]))

        ranked = [sp for sp, _n in
                  collections.Counter(e[4] for e in members).most_common()]
        basic = ranked[:size]
        while len(basic) < 5:
            basic.append(ranked[0])
        basic = basic[:5]
        tactics = ranked[5:7] if len(ranked) > 5 else [ranked[-1]]

        obj = dict(per_file[key][idx])
        obj["extra"] = regen_set_roster(obj["extra"],
                                        [(n, 1) for n in basic],
                                        [(n, 1) for n in tactics])
        obj["extra"] = regen_set_cap(obj["extra"], size)
        obj["extra"] = regen_set_interval(obj["extra"], CAMP_INTERVAL)
        obj["extra"] = regen_set_range(obj["extra"], CAMP_RANGE)
        obj["extra"] = regen_set_tacticpoint(obj["extra"], CAMP_TACTIC_POINT)
        kept[key].append(obj)
        camps += 1

    return kept, len(carpet), camps


def thin_regen(per_file, keep):
    """Drop regen points until `keep` of them remain, most crowded first.

    Takes {(folder, name): [objects]} for one whole zone and returns the same
    shape with points removed. Zone-wide rather than per-file on purpose: the
    positions share one coordinate space (the server adds a single constant bias
    at load), so a chunk-local decision would thin each chunk's interior while
    leaving the seams between chunks as dense as ever.

    Greedy by nearest-neighbour distance: repeatedly remove whichever surviving
    point sits closest to another survivor. That opens walkable corridors and
    preserves the outline of the layout, where taking every Nth point would thin
    uniformly and keep the "no gaps anywhere" property that is the actual problem.
    Deterministic -- ties break on file order -- so a re-run reproduces it exactly.
    """
    pts = []
    for key, objs in sorted(per_file.items()):
        for i, o in enumerate(objs):
            x, y, _z = struct.unpack_from("<fff", o["fixed"], REGEN_POS_OFF)
            pts.append([key, i, x, y, True])

    target = max(1, int(round(len(pts) * keep)))
    alive = len(pts)
    if alive <= target:
        return per_file, 0

    def nearest(idx):
        x, y = pts[idx][2], pts[idx][3]
        best = None
        for j, q in enumerate(pts):
            if j == idx or not q[4]:
                continue
            d = (q[2] - x) ** 2 + (q[3] - y) ** 2
            if best is None or d < best:
                best = d
        return best if best is not None else float("inf")

    # Recomputing every distance after each removal is O(n^3) and Spire Village has
    # 1111 points, so keep a cached nearest-distance per point and refresh lazily:
    # a removal can only ever *increase* a survivor's nearest distance, so a cached
    # value is a lower bound and is safe to re-check on pop.
    import heapq
    heap = [(nearest(i), i) for i in range(len(pts)) if pts[i][4]]
    heapq.heapify(heap)
    while alive > target and heap:
        d, i = heapq.heappop(heap)
        if not pts[i][4]:
            continue
        cur = nearest(i)
        if cur > d:                      # stale lower bound -- re-insert and retry
            heapq.heappush(heap, (cur, i))
            continue
        pts[i][4] = False
        alive -= 1

    out, removed = {}, 0
    for key, objs in per_file.items():
        drop = {p[1] for p in pts if p[0] == key and not p[4]}
        out[key] = [o for i, o in enumerate(objs) if i not in drop]
        removed += len(drop)
    return out, removed


def aip_skill_motions(ours, npc_stb, ai_stb, ids):
    """{npc: {(skill, nMotion)}} for every AIACT_24 the given monsters can run."""
    out = {}
    for i in ids:
        a = npc_stb.get(i, oro.NPC_AI_COL).strip()
        if not a.isdigit() or not int(a):
            continue
        p = dest_of(ours, ai_stb.get(int(a), 0).decode("latin-1").strip())
        if not os.path.isfile(p):
            continue
        blob = open(p, "rb").read()
        for off in aip_walk_skill_actions(blob):
            sk, = struct.unpack_from("<h", blob, off)
            mo, = struct.unpack_from("<h", blob,
                                     off - AIACT24_SKILL_OFF + AIACT24_MOTION_OFF)
            out.setdefault(i, set()).add((sk, mo))
    return out


def chr_anim_audit(chr_, wanted):
    """[(npc, skill, motion, missing_types)] for pairs the CHR cannot animate.

    `wanted` is what aip_skill_motions returned. An anim type above MAX_MOB_ANI is
    ignored: our own CHR carries 112 entries of 0xCDCD MSVC heap fill, which both
    loaders read as a negative int16 and skip.
    """
    bad = []
    for npc in sorted(wanted):
        entry = chr_.chars[npc] if npc < len(chr_.chars) else None
        have = {t for t, _ in entry["anims"] if t < MOB_ANI_MAX} if entry else set()
        for skill, motion in sorted(wanted[npc]):
            missing = [t for t in (motion, motion + 1) if t not in have]
            if missing or motion not in (6, 8):
                bad.append((npc, skill, motion, missing))
    return bad


def chr_fill_motions(chr_, dry):
    """Give the monsters in CHR_MOTION_FILL the casting/skill anims they lack.

    Matched against the motion pool by path rather than by index, so a re-import
    that renumbers the pool cannot silently point a monster at another model's
    clip. Idempotent: an anim type already present is never rewritten.
    """
    def norm(b):
        return b.decode("latin-1").replace("/", "\\").lower()

    pool = {norm(m): i for i, m in enumerate(chr_.motions)}
    added = []
    for npc, fills in sorted(CHR_MOTION_FILL.items()):
        entry = chr_.chars[npc] if npc < len(chr_.chars) else None
        if entry is None:
            raise SystemExit(f"CHR_MOTION_FILL: npc {npc} has no CHR entry -- "
                             f"run --stage 3 first")
        have = {t for t, _ in entry["anims"]}
        for typ, path in sorted(fills.items()):
            if typ in have:
                continue
            idx = pool.get(path.replace("/", "\\").lower())
            if idx is None:
                raise SystemExit(f"CHR_MOTION_FILL: {path} is not in the motion "
                                 f"pool -- it must already be interned by a model")
            entry["anims"].append((typ, idx))
            added.append(f"{npc} type {typ} -> {os.path.basename(path)}")
    if added and not dry:
        chr_.save(dry)
    return added


def chr_override_motions(chr_):
    """Re-point the CHR_MOTION_OVERRIDE slots. Mutates only; returns what changed.

    Same path-matched pool lookup as chr_fill_motions. Idempotent: a slot already
    pointing at the wanted clip is left alone, so a re-run reports nothing.
    """
    def norm(b):
        return b.decode("latin-1").replace("/", "\\").lower()

    pool = {norm(m): i for i, m in enumerate(chr_.motions)}
    changed = []
    for npc, overrides in sorted(CHR_MOTION_OVERRIDE.items()):
        entry = chr_.chars[npc] if npc < len(chr_.chars) else None
        if entry is None:
            raise SystemExit(f"CHR_MOTION_OVERRIDE: npc {npc} has no CHR entry -- "
                             f"run --stage 3 first")
        for typ, path in sorted(overrides.items()):
            idx = pool.get(path.replace("/", "\\").lower())
            if idx is None:
                raise SystemExit(f"CHR_MOTION_OVERRIDE: {path} is not in the motion "
                                 f"pool -- it must already be interned by a model")
            anims = entry["anims"]
            slot = next((k for k, (t, _) in enumerate(anims) if t == typ), None)
            if slot is None:
                anims.append((typ, idx))
                changed.append(f"{npc} type {typ} added -> {os.path.basename(path)}")
            elif anims[slot][1] != idx:
                anims[slot] = (typ, idx)
                changed.append(f"{npc} type {typ} -> {os.path.basename(path)}")
    return changed


# ------------------------------------------------- stage 2a: the warp gates
def collect_gates(src, src_maps):
    """{source warp id: (dest zone, dest event name, [(folder, ifo), ...])}.

    Read rather than restated: the destination pair comes from Jrose's WARP.STB and
    the placements from the source .IFOs, so a gate this script does not know about
    cannot go missing silently.
    """
    src_warp = oro.Stb(os.path.join(src, WARP_STB_REL.replace("\\", "/")))
    placed = {}
    for _, folder, _, _ in ZONES:
        d = os.path.join(src_maps, folder)
        for name in sorted(os.listdir(d)):
            if not name.lower().endswith(".ifo"):
                continue
            buf, bounds = oro.read_ifo(os.path.join(d, name))
            if oro.lump_block(bounds, oro.LUMP_WARP)[0] is None:
                continue
            objs, _ = oro.read_lump(buf, bounds, oro.LUMP_WARP)
            for o in objs:
                placed.setdefault(o["warp_id"], []).append((folder, name))
    out = {}
    for wid, where in sorted(placed.items()):
        dest = src_warp.get(wid, WARP_DEST_ZONE_COL).strip()
        dest = int(dest) if dest.isdigit() else -1
        event = src_warp.get(wid, WARP_DEST_EVENT_COL).strip()
        out[wid] = (dest, event, where)
    return out


def stage2(ours, src, src_index, dry):
    print("stage 2a -- the internal warp gates")

    src_maps = os.path.join(src, MAPS_REL.replace("\\", "/"))
    dst_maps = os.path.join(ours, MAPS_REL.replace("\\", "/"))
    folder_of = {row: folder for row, folder, _, _ in ZONES}
    gates = collect_gates(src, src_maps)

    ours_zone_rows = {row for row, _, _, _ in ZONES}
    for wid, (dest, event, where) in gates.items():
        if dest not in ours_zone_rows:
            raise SystemExit(f"warp {wid} -> zone {dest}, which is not a Karkia zone "
                             f"we imported (placed in {sorted({f for f, _ in where})})")

    # --- 2a-i. every destination event position must resolve, byte for byte.
    # This is the check that matters: the server hashes the name and looks it up in
    # the destination zone, and a miss is what produces an IS_HACKING disconnect
    # rather than a failed warp.
    bad = []
    for wid, (dest, event, _) in sorted(gates.items()):
        d = os.path.join(dst_maps, folder_of[dest])
        zon = [f for f in os.listdir(d) if f.lower().endswith(".zon")][0]
        names = [n for n, _ in oro.zon_events(os.path.join(d, zon))]
        if event not in names:
            bad.append((wid, dest, event, f"not among {len(names)} events in {zon}"))
    if bad:
        for wid, dest, event, why in bad:
            print(f"    !! warp {wid} -> zone {dest} event {event!r}: {why}")
        raise SystemExit("destination event positions missing -- refusing to write")
    print(f"    {'event positions':26s} all {len(gates)} resolve byte-for-byte")

    # --- 2a-ii. WARP.STB rows
    our_warp = oro.Stb(os.path.join(ours, WARP_STB_REL.replace("\\", "/")))
    written, remapped = 0, []
    for wid, (dest, event, _) in sorted(gates.items()):
        ours_id = GATE_REMAP.get(wid, wid)
        if ours_id >= our_warp.rows:
            raise SystemExit(f"warp id {ours_id} beyond our WARP.STB ({our_warp.rows} rows)")
        before = list(our_warp.d[ours_id])
        if wid not in GATE_REMAP and any(x.strip() for x in before) \
                and our_warp.get(ours_id, 0) != GATE_NAMES[wid].encode("latin-1"):
            raise SystemExit(f"warp {ours_id} is occupied by "
                             f"{our_warp.get(ours_id, 0).decode('latin-1')!r} -- "
                             f"add it to GATE_REMAP rather than overwriting it")
        our_warp.set(ours_id, 0, GATE_NAMES[wid])
        our_warp.set(ours_id, WARP_DEST_ZONE_COL, str(dest))
        our_warp.set(ours_id, WARP_DEST_EVENT_COL, event)
        if our_warp.d[ours_id] != before:
            written += 1
        if wid != ours_id:
            remapped.append(f"{wid}->{ours_id}")
    print(f"    {'WARP.STB':26s} {written} rows written"
          + (f", remapped {', '.join(remapped)} (live Oro gates)" if remapped else ""))
    our_warp.save(dry)

    # --- 2a-iii. put the gate objects back into our .IFO copies
    files, placements = 0, 0
    for _, folder, _, _ in ZONES:
        s, d = os.path.join(src_maps, folder), os.path.join(dst_maps, folder)
        for name in sorted(os.listdir(s)):
            if not name.lower().endswith(".ifo"):
                continue
            sbuf, sbounds = oro.read_ifo(os.path.join(s, name))
            if oro.lump_block(sbounds, oro.LUMP_WARP)[0] is None:
                continue
            sobjs, _ = oro.read_lump(sbuf, sbounds, oro.LUMP_WARP)
            if not sobjs:
                continue
            dp = os.path.join(d, name)
            dbuf, dbounds = oro.read_ifo(dp)
            if oro.lump_block(dbounds, oro.LUMP_WARP)[0] is None:
                raise SystemExit(f"{dp}: no WARP lump to fill (run --stage 1 first)")
            have, dtrail = oro.read_lump(dbuf, dbounds, oro.LUMP_WARP)
            if len(have) == len(sobjs):
                continue                              # already restored
            keep = []
            for o in sobjs:
                fixed = bytearray(o["fixed"])
                struct.pack_into("<h", fixed, 0, GATE_REMAP.get(o["warp_id"], o["warp_id"]))
                keep.append(dict(o, fixed=bytes(fixed)))
            blob = oro.build_ifo(dbounds, dbuf,
                                 {oro.LUMP_WARP: oro.build_object_lump(keep, dtrail)})
            files += 1
            placements += len(keep)
            if not dry:
                with open(dp, "wb") as fh:
                    fh.write(blob)
    print(f"    {'gate objects':26s} {placements} placements into {files} .IFO")

    print("\n    Karkia is now walkable within each cluster. Still no way IN: that is")
    print("    stage 2b (the Wayfinder NPC), so reach it with a GM warp for now.")
    print("    NOTE: gate 185 is placed twice in KSPIREVIL\\34_33.IFO -- two objects on")
    print("          one spot, as Jrose authored it. Not a duplicate to clean up.")


# --------------------------------------------------------- stage 3: monsters
def stage3(ours, src, src_index, dry):
    print("stage 3 -- monsters, at their native ids and with Jrose's own stats")

    src_maps = os.path.join(src, MAPS_REL.replace("\\", "/"))
    dst_maps = os.path.join(ours, MAPS_REL.replace("\\", "/"))
    ids = sorted(KARKIA_MONSTERS)

    def S(rel):
        return oro.Stb(os.path.join(src, rel.replace("\\", "/")))

    def O(rel):
        return oro.Stb(os.path.join(ours, rel.replace("\\", "/")))

    def num(stb, r, c):
        """int of a cell -- import-oro's Stb works in bytes and has no accessor."""
        v = stb.get(r, c).strip()
        return int(v) if v.isdigit() else 0

    # --- 3a. cross-check the roster against what the spawn lumps actually name.
    # The AI-summoned seven appear in no REGEN lump, so the two sets differ by
    # design -- but a *spawned* id we do not know about would be a hole.
    spawned, regen_src = set(), {}
    for _, folder, _, _ in ZONES:
        d = os.path.join(src_maps, folder)
        for name in sorted(os.listdir(d)):
            if not name.lower().endswith(".ifo"):
                continue
            buf, bounds = oro.read_ifo(os.path.join(d, name))
            off, end = oro.lump_block(bounds, oro.LUMP_REGEN)
            if off is None or buf[off:off + 4] == b"\0\0\0\0":
                continue
            regen_src[(folder, name)] = buf[off:end]
            objs, _ = oro.read_lump(buf, bounds, oro.LUMP_REGEN)
            for o in objs:
                spawned.update(oro.regen_mob_ids(o["extra"]))
    src_npc, our_npc = S(NPC_STB_REL), O(NPC_STB_REL)
    # A spawned id we neither import nor already own would be a hole. One we
    # already own is fine and expected: KBurnedForest, KMemories and KFlowerGarden
    # spawn *only* monster id 1, which is our own level-2 Mini-Jelly Bean. Jrose
    # laid out their regen points and never assigned a real monster, so those
    # three zones come in as Jrose ships them -- visibly unfinished rather than
    # quietly empty. Populating them is a later pass (roadmap §4, decision 4).
    borrowed = sorted(i for i in spawned - set(ids) if our_npc.occupied(i))
    unknown = sorted(spawned - set(ids) - set(borrowed))
    if unknown:
        raise SystemExit(f"spawn lumps reference monsters that are neither imported "
                         f"nor already ours: {unknown}")
    summon_only = sorted(set(ids) - spawned)
    print(f"    {'roster':26s} {len(ids)} monsters "
          f"({len(spawned & set(ids))} spawned, {len(summon_only)} AI-summoned only: "
          f"{summon_only})")
    if borrowed:
        names = ", ".join(f"{i} ({our_npc.get(i, 0).decode('latin-1')})"
                          for i in borrowed)
        # Jrose left id 1 in every slot of the three zones SPAWN_ROSTER covers,
        # so this is what the *source* holds, not what we ship -- the roster is
        # applied further down, after this scope is taken.
        print(f"    {'placeholder spawns':26s} source has {names} in "
              f"{len(SPAWN_ROSTER)} zones; SPAWN_ROSTER replaces them")
    if max(ids) >= our_npc.rows:
        raise SystemExit(f"monster id {max(ids)} beyond LIST_NPC.STB ({our_npc.rows})")
    written, kept = 0, []
    for i in ids:
        if our_npc.occupied(i):
            kept.append(i)
            continue
        for c in range(oro.NPC_COPY_COLS):
            our_npc.set(i, c, src_npc.get(i, c))
        our_npc.set(i, 0, KARKIA_MONSTERS[i])       # theirs is a Japanese label
        our_npc.set(i, oro.NPC_PVP_COL, oro.DEFAULT_PVP_STATE)
        written += 1
    print(f"    {'LIST_NPC.STB':26s} {written} rows written"
          + (f", {len(kept)} already ours {kept}" if kept else ""))

    # --- 3c. names. 2689 and 2731 share one key in the source (both Ghost Seed),
    # so the key is appended once and both rows point at it.
    our_stl = oro.Stl(os.path.join(ours, NPC_STL_REL.replace("\\", "/")))
    nnames = 0
    for i in ids:
        key = our_npc.get(i, oro.NPC_STRID_COL).decode("latin-1").strip()
        if not key or our_stl.has(key):
            continue
        our_stl.append(key, i, KARKIA_MONSTERS[i])
        nnames += 1
    print(f"    {'LIST_NPC_S.STL':26s} +{nnames} keys (now {len(our_stl.keys)})")

    # --- 3d. AI rows and their .aip files
    src_ai, our_ai = S(AI_STB_REL), O(AI_STB_REL)
    need_ai = sorted({int(our_npc.get(i, oro.NPC_AI_COL))
                      for i in ids
                      if our_npc.get(i, oro.NPC_AI_COL).strip().isdigit()} - {0})
    grew = our_ai.grow_to(max(need_ai) + 1) if need_ai else 0
    aips, ai_written, clobbered = set(), 0, []
    for a in need_ai:
        f = src_ai.get(a, 0)
        if not f.strip():
            raise SystemExit(f"AI row {a} is blank in the source FILE_AI.STB")
        aips.add(f.decode("latin-1"))
        if our_ai.get(a, 0) != f:
            if our_ai.get(a, 0).strip():
                clobbered.append(f"{a}: {our_ai.get(a, 0).decode('latin-1')} -> "
                                 f"{f.decode('latin-1')}")
            our_ai.set(a, 0, f)
            ai_written += 1
    for c in clobbered:
        print(f"    !! AI row overwritten -- {c}")
    print(f"    {'FILE_AI.STB':26s} +{grew} rows (now {our_ai.rows}), "
          f"{ai_written} written, {len(need_ai)} AI types")
    copy_new(aips, src_index, ours, dry, ".aip files")

    # --- 3e. models, skeletons and motions, appended with a full index remap.
    # import-oro.py owns this; nothing about it is Oro-specific.
    our_npc.save(dry)
    our_ai.save(dry)
    if nnames:
        our_stl.save(dry)
    oro.import_characters(ids, ours, src, dry, "monster")

    # ...but its asset sweep takes only skeletons and motions, not the character
    # *effect* pool, so an effect a monster carries would be interned into the CHR
    # and never copied. Karkia needs four, one of them Karkia-specific
    # (kakia_fairy_01.eft on the Evil Fairy). Collected here rather than by
    # patching import-oro, which is a shipped importer.
    src_chr = oro.Chr(os.path.join(src, NPC_CHR_REL.replace("\\", "/")))
    eff = set()
    for i in ids:
        c = src_chr.chars[i]
        if not c:
            continue
        for _, e in c["effects"]:
            if e < len(src_chr.effects):
                eff.add(src_chr.effects[e].decode("latin-1"))
    eff = {e for e in eff if "\\" in e or "/" in e}
    copy_new(eff | effect_chain(eff, src_index), src_index, ours, dry,
             "character effects")

    # --- 3f. mob-weapon presentation. LIST_NPC cols 5/6 are weapon *model* rows,
    # and an empty WEAPON_BULLET_EFFECT there means a ranged monster fires no
    # projectile and lands no visible hit -- the same defect
    # scripts/fix-mob-bullet-effects.py exists to repair.
    src_wpn, our_wpn = S(WEAPON_STB_REL), O(WEAPON_STB_REL)
    weapons = set()
    for i in ids:
        for col in (oro.NPC_R_WEAPON_COL, oro.NPC_L_WEAPON_COL):
            v = our_npc.get(i, col).strip()
            if v.isdigit() and int(v):
                weapons.add(int(v))
    users = {}
    for i in ids:
        for col in (oro.NPC_R_WEAPON_COL, oro.NPC_L_WEAPON_COL):
            v = our_npc.get(i, col).strip()
            if v.isdigit() and int(v):
                users.setdefault(int(v), []).append(i)
    wfixed, wempty = [], []
    for w in sorted(weapons):
        if w >= our_wpn.rows:
            raise SystemExit(f"monster weapon row {w} beyond LIST_WEAPON.STB "
                             f"({our_wpn.rows} rows)")
        if any(our_wpn.get(w, c).strip() for c in oro.WEAPON_PRESENTATION_COLS):
            continue                                # already presents something
        got = [(c, src_wpn.get(w, c)) for c in oro.WEAPON_PRESENTATION_COLS
               if src_wpn.get(w, c).strip()]
        if not got:
            wempty.append(w)
            continue
        for c, v in got:
            our_wpn.set(w, c, v)
        wfixed.append(w)
    print(f"    {'LIST_WEAPON.STB':26s} {len(wfixed)} of {len(weapons)} mob weapons "
          f"given attack presentation {wfixed}")
    # An empty WEAPON_BULLET_EFFECT (col 38) is only a defect for a *ranged* user:
    # UsesProjectileAttackPresentation() is `weapon > 0 && bullet_effect > 0`, so an
    # empty col 38 simply selects the melee hit frame, which is correct for a melee
    # monster and invisible for a bow/gun one (see fix-mob-bullet-effects.py).
    # Karkia's melee users sit at 90-350 cm and its ranged ones at 1100-1800, so
    # 800 falls in the empty band between the two clusters.
    #
    # That reasoning covers col 38 and *only* col 38. A melee monster still needs
    # 39/40/42, and a row blank in all five leaves it with no hit effect and no
    # sound at all -- which is the bug this comment originally waved through.
    # Step 3h below is what checks and repairs that.
    for w in wempty:
        ranged = [i for i in users.get(w, [])
                  if int(our_npc.get(i, NPC_RANGE_COL).strip() or 0) > MELEE_RANGE_CM]
        if ranged:
            print(f"    !! weapon row {w} has no bullet effect in either table and "
                  f"is used by RANGED {ranged} -- they will fire nothing visible")
    if wfixed:
        our_wpn.save(dry)

    # --- 3h. attack presentation: validate every index against the table that
    # actually consumes it, and repair the Karkia-exclusive weapon rows. This runs
    # unconditionally rather than only for newly-written rows, so re-running fixes
    # data an earlier version of this script already wrote.
    le, fe = O(EFFECT_STB_REL), O(FILE_EFFECT_STB_REL)
    fs, hs = O(FILE_SOUND_STB_REL), O(HITSOUND_STB_REL)
    tables = {"LIST_EFFECT": le, "FILE_EFFECT": fe,
              "FILE_SOUND": fs, "LIST_HITSOUND": hs}

    def resolves(tbl, v):
        return v == 0 or (v < tbl.rows and tbl.occupied(v))

    # 3h-i. import the LIST_EFFECT rows our weapons name but we do not have,
    # together with any FILE_EFFECT row they point at and its asset.
    src_le, src_fe = S(EFFECT_STB_REL), S(FILE_EFFECT_STB_REL)
    eff_written, fe_written, eff_assets = [], [], set()
    for r in PRESENTATION_EFFECT_ROWS:
        if r < le.rows and le.occupied(r):
            continue
        if r >= src_le.rows or not src_le.occupied(r):
            raise SystemExit(f"LIST_EFFECT row {r} is not in the source either")
        le.grow_to(r + 1)
        for c in range(min(le.cols, src_le.cols)):
            le.set(r, c, src_le.get(r, c))
        eff_written.append(r)
        for c in range(le.cols):          # every cell that names a FILE_EFFECT row
            v = num(le, r, c)
            if not v or v >= src_fe.rows or not src_fe.occupied(v):
                continue
            if v < fe.rows and fe.occupied(v):
                continue
            fe.grow_to(v + 1)
            for c2 in range(min(fe.cols, src_fe.cols)):
                fe.set(v, c2, src_fe.get(v, c2))
            fe_written.append(v)
            eff_assets.add(src_fe.get(v, 0).decode("latin-1").strip())  # bare name
    if eff_written:
        le.save(dry)
    if fe_written:
        fe.save(dry)
    print(f"    {'LIST_EFFECT.STB':26s} +{len(eff_written)} rows {eff_written}, "
          f"FILE_EFFECT +{len(fe_written)} rows {fe_written}")
    # FILE_EFFECT names its files bare (`questarua_gem.eft`), and the client hands
    # that straight to the VFS -- they live flat in 3DDATA\EFFECT\, so the prefix
    # is added here rather than the name being dropped for having no separator.
    if eff_assets:
        chain = {os.path.join(EFFECT_DIR, a) if not ("\\" in a or "/" in a) else a
                 for a in eff_assets if a}
        copy_new(chain | effect_chain(chain, src_index), src_index, ours, dry,
                 "effect assets")

    # 3h-ii. repair the weapon rows, but only ones no pre-existing monster equips
    ours_only = {}
    for w in sorted(weapons):
        holders = [r for r in range(our_npc.rows)
                   if our_npc.occupied(r)
                   and w in (num(our_npc, r, oro.NPC_R_WEAPON_COL),
                             num(our_npc, r, oro.NPC_L_WEAPON_COL))]
        ours_only[w] = all(h in KARKIA_MONSTERS for h in holders)
    repaired, shared_bad = [], []
    for w in sorted(weapons):
        vals = {c: num(our_wpn, w, c) for c in PRESENTATION_COLS}
        melee = [i for i in users.get(w, [])
                 if int(our_npc.get(i, NPC_RANGE_COL).strip() or 0) <= MELEE_RANGE_CM]
        dangling = [c for c, t in PRESENTATION_COLS.items()
                    if not resolves(tables[t], vals[c])]
        # "Silent" means the *melee* path presents nothing. A row carrying a
        # bullet effect is never silent whatever its owner's range: the server's
        # UsesProjectileAttackPresentation() is `bullet_effect > 0`, so it takes
        # the projectile path and cols 39/40 are never consulted.
        silent = bool(melee) and not vals[38] and not vals[39] and not vals[40]
        if not dangling and not silent:
            continue
        if not ours_only[w]:
            shared_bad.append((w, dangling, silent))
            continue
        donor = PRESENTATION_DONOR.get(w)
        if silent and donor:
            for c in (39, 40, 42):
                our_wpn.set(w, c, our_wpn.get(donor, c))
            repaired.append(f"{w} melee presentation from {donor}")
        for c in dangling:
            if resolves(tables[PRESENTATION_COLS[c]], num(our_wpn, w, c)):
                continue                     # an import above fixed it
            repaired.append(f"{w} col {c}={vals[c]} still dangles")
    if repaired:
        our_wpn.save(dry)
    print(f"    {'attack presentation':26s} "
          f"{len([r for r in repaired if 'from' in r])} weapon rows repaired"
          + (f": {repaired}" if repaired else ""))
    for w, dangling, silent in shared_bad:
        print(f"    {'':26s} weapon {w} is imperfect (dangling={dangling} "
              f"silent={silent}) but is shared with pre-existing monsters -- left alone")

    # --- 3i. the monsters' skills. Three port into their own (blank) row numbers,
    # one ports to a different row because its own is occupied, and two re-point at
    # skills we already own. Nothing is authored.
    src_skill, our_skill = S(SKILL_STB_REL), O(SKILL_STB_REL)
    ported, skill_assets = [], set()
    fe_before = len(fe_written)
    for src_row, (dst_row, label) in sorted(SKILL_PORTS.items()):
        if src_row >= src_skill.rows or not src_skill.occupied(src_row):
            raise SystemExit(f"LIST_SKILL row {src_row} is not in the source")
        # A row we already ported is identified by its skill number (col 1), not by
        # its label -- an earlier version of this script copied the Japanese one,
        # and matching on the label would call our own work someone else's row.
        if our_skill.occupied(dst_row):
            if num(our_skill, dst_row, 1) != num(src_skill, src_row, 1):
                raise SystemExit(f"LIST_SKILL row {dst_row} is occupied by "
                                 f"{our_skill.get(dst_row, 0).decode('latin-1')!r} -- "
                                 f"point SKILL_PORTS at a free row instead")
            if our_skill.get(dst_row, 0).decode("latin-1") == label:
                continue                                  # already ported
            our_skill.set(dst_row, 0, label)              # relabel in place
            ported.append(f"{dst_row} relabelled")
            continue
        for c in range(SKILL_COPY_COLS):
            our_skill.set(dst_row, c, src_skill.get(src_row, c))
        our_skill.set(dst_row, 0, label)      # theirs is a Japanese editor label
        ported.append(f"{src_row}->{dst_row}" if src_row != dst_row else str(src_row))
        # every effect/sound the ported row names must exist on our side
        for c in (56, 59, 62, 65, 74, 77, 80, 83):
            v = num(src_skill, src_row, c)
            if not v or v >= src_fe.rows or not src_fe.occupied(v):
                continue
            if v < fe.rows and fe.occupied(v):
                continue
            fe.grow_to(v + 1)
            for c2 in range(min(fe.cols, src_fe.cols)):
                fe.set(v, c2, src_fe.get(v, c2))
            fe_written.append(v)
            skill_assets.add(src_fe.get(v, 0).decode("latin-1").strip())
    if ported:
        our_skill.save(dry)
        if fe_written:
            fe.save(dry)
    print(f"    {'LIST_SKILL.STB':26s} {len(ported)} rows ported {ported}"
          + (f", FILE_EFFECT +{len(fe_written) - fe_before} rows "
             f"{fe_written[fe_before:]}" if len(fe_written) > fe_before else ""))
    if skill_assets:
        chain = {os.path.join(EFFECT_DIR, a) if not ("\\" in a or "/" in a) else a
                 for a in skill_assets if a}
        copy_new(chain | effect_chain(chain, src_index), src_index, ours, dry,
                 "skill effect assets")

    remap = dict(SKILL_REPOINT)
    remap.update({src: dst for src, (dst, _) in SKILL_PORTS.items() if src != dst})
    patched = collections.Counter()
    for a in sorted(aips):
        p = dest_of(ours, a)
        if not os.path.isfile(p):
            continue
        for old, new in aip_repoint(p, remap, dry):
            patched[f"{old}->{new}"] += 1
    print(f"    {'.aip skill re-points':26s} {sum(patched.values())} records "
          f"{dict(patched) if patched else '(already done)'}")

    # Skills withdrawn after in-game testing -- see AIP_DISABLE_SKILLS. Done here,
    # after the re-points, because the ids it names are OUR numbering.
    killed = collections.Counter()
    for a in sorted(aips):
        p = dest_of(ours, a)
        if not os.path.isfile(p):
            continue
        for sk, _pi, _ei in aip_disable(p, AIP_DISABLE_SKILLS, dry):
            killed[sk] += 1
    print(f"    {'.aip skills disabled':26s} {sum(killed.values())} events "
          + (f"{dict(killed)}" if killed else "(already done)"))

    # --- 3j. skill animations. Every AIACT_24 names a casting slot; both halves
    # of the pair have to exist in the CHR or the cast presents nothing at all.
    # Audited across all 38 monsters rather than fixed where a report landed --
    # the Tornado report was one of four faults on the same model, and the other
    # three (its Berserk, and both on the Alpha) would not have been noticed.
    our_chr = oro.Chr(os.path.join(ours, NPC_CHR_REL.replace("\\", "/")))
    added = chr_fill_motions(our_chr, dry)
    print(f"    {'CHR skill animations':26s} {len(added)} filled "
          f"{added if added else '(already done)'}")
    overridden = chr_override_motions(our_chr)
    print(f"    {'CHR slot overrides':26s} {len(overridden)} re-pointed "
          f"{overridden if overridden else '(already done)'}")
    if overridden and not dry:
        our_chr.save(dry)
    if not dry:
        after = oro.Chr(os.path.join(ours, NPC_CHR_REL.replace("\\", "/")))
        still = chr_anim_audit(after, aip_skill_motions(ours, our_npc, our_ai, ids))
        if still:
            raise SystemExit(f"VERIFY FAILED: {len(still)} skill actions still have "
                             f"no animation: {still}")

    # --- 3k. spawn thinning. Applied here, at the point the lump is written,
    # rather than as a separate script: this stage rebuilds every REGEN lump from
    # Jrose's source on each run, so an after-the-fact edit would be silently
    # undone by the next --stage 3. Doing it here makes it idempotent and makes
    # SPAWN_THINNING the single record of what the zone actually ships.
    # --- 3l. spawn rosters. Jrose shipped these three zones with id 1 in every
    # slot, so the monsters they hold are chosen here. Same placement as the
    # thinning below and for the same reason: this stage rebuilds the lumps.
    rostered = {}
    for folder in sorted({f for _r, f, _n, _k in ZONES}):
        if folder.upper() not in SPAWN_ROSTER and folder.upper() not in SPAWN_CAP:
            continue
        files_here = {k: v for k, v in regen_src.items() if k[0] == folder}
        per_file = {}
        trailing = {}
        for key, blob in files_here.items():
            objs, tail = oro.parse_object_lump(blob, 0, len(blob),
                                               oro.LUMP_REGEN, exact=False)
            per_file[key] = objs
            trailing[key] = tail
        n = apply_spawn_roster(per_file, folder)
        for key, objs in per_file.items():
            regen_src[key] = oro.build_object_lump(objs, trailing.get(key, b""))
        basic, tactics = SPAWN_ROSTER.get(folder.upper(), ([], []))
        rostered[folder] = (n, sorted({m for m, _c in basic + tactics if m}),
                            SPAWN_CAP.get(folder.upper()),
                            SPAWN_INTERVAL.get(folder.upper()))
    for folder, (n, mobs, cap, iv) in sorted(rostered.items()):
        capped = f", cap {cap}" if cap is not None else ""
        timed = f", {iv}s tick" if iv is not None else ""
        print(f"    {'spawn roster':26s} {folder}: {n} points{capped}{timed} -> {mobs}")

    thinned = {}
    for folder in {f for _r, f, _n, _k in ZONES}:
        keep = SPAWN_THINNING.get(folder.upper(), 1.0)
        files_here = {k: v for k, v in regen_src.items() if k[0] == folder}
        if keep >= 1.0 or not files_here:
            continue
        per_file = {}
        for key, blob in files_here.items():
            objs, trailing = oro.parse_object_lump(blob, 0, len(blob),
                                                   oro.LUMP_REGEN, exact=False)
            per_file[key] = objs
            thinned.setdefault("trailing", {})[key] = trailing
        before = sum(len(v) for v in per_file.values())
        kept, removed = thin_regen(per_file, keep)
        for key, objs in kept.items():
            regen_src[key] = oro.build_object_lump(
                objs, thinned["trailing"].get(key, b""))
        print(f"    {'spawn thinning':26s} {folder}: {before} -> {before - removed} "
              f"points (keep {keep:.0%})")

    # --- 3m. camp consolidation, for the two main maps. Same placement and same
    # reason as the thinning above: this stage rebuilds every REGEN lump from
    # Jrose's source on each run, so an .IFO edited by hand or in the map editor
    # is silently reverted by the next --stage 3.
    for folder in sorted({f for _r, f, _n, _k in ZONES}):
        spec = SPAWN_CAMPS.get(folder.upper())
        files_here = {k: v for k, v in regen_src.items() if k[0] == folder}
        if not spec or not files_here:
            continue
        cell_cm, size = spec
        per_file, trailing = {}, {}
        for key, blob in files_here.items():
            objs, tail = oro.parse_object_lump(blob, 0, len(blob),
                                               oro.LUMP_REGEN, exact=False)
            per_file[key] = objs
            trailing[key] = tail
        kept, absorbed, camps = consolidate_camps(per_file, cell_cm, size)
        for key, objs in kept.items():
            regen_src[key] = oro.build_object_lump(objs, trailing.get(key, b""))
        passthru = sum(len(v) for v in kept.values()) - camps
        print(f"    {'spawn camps':26s} {folder}: {absorbed} points -> {camps} camps "
              f"x{size} = {camps * size} bodies "
              f"({cell_cm // 100} m grid, {passthru} point(s) passed through)")

    # --- 3g. the spawn lumps, last, so a half-written run leaves no live spawns
    # pointing at rows that do not exist yet.
    files, points = 0, 0
    for (folder, name), blob in sorted(regen_src.items()):
        dp = os.path.join(dst_maps, folder, name)
        if not os.path.isfile(dp):
            raise SystemExit(f"{dp}: run --stage 1 first")
        dbuf, dbounds = oro.read_ifo(dp)
        doff, dend = oro.lump_block(dbounds, oro.LUMP_REGEN)
        if doff is None:
            raise SystemExit(f"{dp}: no REGEN lump to fill")
        if dbuf[doff:dend] == blob:
            continue
        n, = struct.unpack_from("<i", blob, 0)
        out = oro.build_ifo(dbounds, dbuf, {oro.LUMP_REGEN: blob})
        files += 1
        points += n
        if not dry:
            with open(dp, "wb") as fh:
                fh.write(out)
            vbuf, vbounds = oro.read_ifo(dp)
            voff, vend = oro.lump_block(vbounds, oro.LUMP_REGEN)
            if vbuf[voff:vend] != blob:
                raise SystemExit(f"VERIFY FAILED: {dp} REGEN lump mismatch")
    print(f"    {'IFO regen lumps':26s} {points} spawn points into {files} files")

    print("\n    Stats are Jrose's, deliberately -- see the roadmap. On our curve a")
    print("    level-213 trash mob takes ~4,200 swings and kills you in three hits,")
    print("    so play one session to see the fights, then run the stage-4 pass.")
    print("    Zones 131/133/144 ship id 1 in every source slot -- Jrose's own")
    print("    authoring, not a broken import. SPAWN_ROSTER replaces it.")


# ------------------------------------------------------- stage 6: the NPCs
def mob_con_name(extra):
    """The .CON a LUMP_MOB placement names: int AI, then a pascal string.

    Read the way zonefile.cpp reads it. Note the reference is stored WITH the
    extension for the EM86 family and WITHOUT it for the rest, while LIST_EVENT
    always holds a full path -- so every comparison here is on the stem.
    """
    ln = extra[4]
    return extra[5:5 + ln].split(b"\0")[0].decode("latin-1", "replace")


def stem(p):
    if isinstance(p, bytes):
        p = p.decode("latin-1", "replace")
    return os.path.splitext(os.path.basename(p.replace("\\", "/")))[0].upper()


def stage6(ours, src, src_index, dry):
    """NPC rows, models, dialog registrations and the placements themselves."""
    print("\nstage 6 -- the NPCs")
    S = lambda rel: oro.Stb(os.path.join(src, rel.replace("\\", "/")))
    O = lambda rel: oro.Stb(os.path.join(ours, rel.replace("\\", "/")))
    src_maps = os.path.join(src, MAPS_REL.replace("\\", "/"))
    dst_maps = os.path.join(ours, MAPS_REL.replace("\\", "/"))

    # --- 6a. collect the placements, remapping the nine colliding ids
    placements, cons = {}, {}
    for row, folder, label, _k in ZONES:
        d = os.path.join(src_maps, folder)
        if not os.path.isdir(d):
            continue
        for name in sorted(os.listdir(d)):
            if not name.lower().endswith(".ifo"):
                continue
            buf, bounds = oro.read_ifo(os.path.join(d, name))
            objs, trailing = oro.read_lump(buf, bounds, oro.LUMP_MOB)
            if not objs:
                continue
            for o in objs:
                o["obj_id"] = NPC_ID_REMAP.get(o["obj_id"], o["obj_id"])
                fixed = bytearray(o["fixed"])       # parse_object_lump hands back bytes
                struct.pack_into("<i", fixed, 8, o["obj_id"])
                o["fixed"] = bytes(fixed)
                cons[o["obj_id"]] = mob_con_name(o["extra"])
            placements[(folder, name)] = (objs, trailing)
    ids = sorted({o["obj_id"] for objs, _t in placements.values() for o in objs})
    unknown = [i for i in ids if i not in KARKIA_NPCS]
    if unknown:
        raise SystemExit(f"placements name NPCs with no English name: {unknown}")
    print(f"    {'placements':26s} {sum(len(o) for o, _t in placements.values())} "
          f"across {len(placements)} .IFO files, {len(ids)} distinct NPCs")

    # --- 6b. LIST_NPC rows. Copied from the source at the source id, written at
    # ours, with an English name and a fresh STL key of our own numbering.
    src_npc, our_npc = S(NPC_STB_REL), O(NPC_STB_REL)
    back = {v: k for k, v in NPC_ID_REMAP.items()}
    our_npc.grow_to(max(ids) + 1)
    written, kept = [], []
    for i in ids:
        s = back.get(i, i)
        if our_npc.occupied(i) and our_npc.get(i, 0).decode("latin-1") == KARKIA_NPCS[i]:
            kept.append(i)
            continue
        if s >= src_npc.rows or not src_npc.occupied(s):
            raise SystemExit(f"NPC {s} is not in the source LIST_NPC")
        for c in range(min(our_npc.cols, src_npc.cols)):
            our_npc.set(i, c, src_npc.get(s, c))
        our_npc.set(i, 0, KARKIA_NPCS[i])
        our_npc.set(i, oro.NPC_STRID_COL, f"{NPC_STRID_PREFIX}{i}")
        written.append(i)
    print(f"    {'LIST_NPC.STB':26s} {len(written)} rows written, "
          f"{len(kept)} already ours")

    our_stl = oro.Stl(os.path.join(ours, NPC_STL_REL.replace("\\", "/")))
    nnames = 0
    for i in ids:
        key = our_npc.get(i, oro.NPC_STRID_COL).decode("latin-1").strip()
        if key and not our_stl.has(key):
            our_stl.append(key, i, KARKIA_NPCS[i])
            nnames += 1
    print(f"    {'LIST_NPC_S.STL':26s} +{nnames} keys (now {len(our_stl.keys)})")

    # --- 6c. dialog. Every .CON exists and is registered in Jrose's LIST_EVENT,
    # and none of the 32 offers a quest (checked with quest-editor con-triggers),
    # so they come in verbatim: no dangling trigger can follow them. The text is
    # Japanese until it is overridden through the QEX1 appendix -- a working
    # bank/shop NPC that speaks the wrong language beats a mute one.
    src_ev, our_ev = S(EVENT_STB_REL), O(EVENT_STB_REL)
    have = {stem(our_ev.get(r, EVENT_FILE_COL)): r for r in range(our_ev.rows)
            if our_ev.get(r, EVENT_FILE_COL).strip()}
    src_by_stem = {stem(src_ev.get(r, EVENT_FILE_COL)): r for r in range(src_ev.rows)
                   if src_ev.get(r, EVENT_FILE_COL).strip()}
    free = (r for r in range(1, our_ev.rows)
            if not our_ev.get(r, EVENT_FILE_COL).strip())
    ev_rows, con_files, ev_written = {}, set(), []
    for i in ids:
        st = stem(cons.get(i, ""))
        if not st:
            continue
        if st in have:
            ev_rows[i] = have[st]
            continue
        s = src_by_stem.get(st)
        if s is None:
            raise SystemExit(f"NPC {i}: .CON {st} is in no LIST_EVENT row")
        r = next(free)
        for c in range(min(our_ev.cols, src_ev.cols)):
            our_ev.set(r, c, src_ev.get(s, c))
        our_ev.set(r, 0, KARKIA_NPCS[i])          # editor label, ours is readable
        have[st] = ev_rows[i] = r
        ev_written.append(r)
        con_files.add(src_ev.get(s, EVENT_FILE_COL).decode("latin-1").strip())
    print(f"    {'LIST_EVENT.STB':26s} +{len(ev_written)} rows "
          f"{ev_written[:6]}{'...' if len(ev_written) > 6 else ''}")
    copy_new(con_files, src_index, ours, dry, ".CON dialogs")

    our_npc.save(dry)
    our_ev.save(dry)
    if nnames:
        our_stl.save(dry)

    # --- 6d. models, skeletons and motions, with the full index remap. The rows
    # were written under OUR ids, so import_characters is asked for those and the
    # source entry is fetched from the pre-remap id.
    oro.import_characters([back.get(i, i) for i in ids], ours, src, dry, "NPC")

    # A CHR entry is addressed by NPC id and import_characters copies index to
    # index, so the nine remapped NPCs land at their *Jrose* ids and have to be
    # moved to ours. All nine of those slots were empty on our side beforehand --
    # our own 1074 [Wounded Traveler] Seth has a LIST_NPC row but no CHR entry --
    # so the move carries nothing off with it and the vacated slot goes back to
    # None. Idempotent: a slot already moved is None and is skipped.
    moved = []
    if not dry:
        our_chr = oro.Chr(os.path.join(ours, NPC_CHR_REL.replace("\\", "/")))
        if max(ids) >= len(our_chr.chars):
            our_chr.chars.extend([None] * (max(ids) + 1 - len(our_chr.chars)))
        for s, d in sorted(NPC_ID_REMAP.items()):
            if our_chr.chars[d] is not None:
                continue                                  # already done
            if our_chr.chars[s] is None:
                raise SystemExit(f"CHR entry {s} missing -- cannot place NPC {d}")
            our_chr.chars[d], our_chr.chars[s] = our_chr.chars[s], None
            moved.append(f"{s}->{d}")
        if moved:
            our_chr.save(dry)
    print(f"    {'CHR entries remapped':26s} {len(moved)} "
          f"{moved if moved else '(none needed)' if not dry else '(dry run)'}")

    # --- 6e. normalise the .CON reference each placement carries.
    #
    # zonefile.cpp resolves an NPC's dialog with
    #     _stricmp(std::filesystem::path(EVENT_FILENAME(row)).filename(), szName)
    # which is an EXACT basename compare, extension included. Jrose stores the
    # reference with ".con" for the EM86 family and WITHOUT it for the other 19,
    # and their server evidently tolerated that; ours cannot, so those 19 resolved
    # to nQuestIDX = 0 and had no dialog at all -- reported in game as "I don't
    # think I can talk to them", which is not the Japanese text at all.
    #
    # Rewritten to exactly what our LIST_EVENT row holds, so the compare cannot
    # miss. The record is `int AI` + one pascal string and nothing else.
    canon, fixed_refs = {}, []
    for i, r in ev_rows.items():
        canon[i] = os.path.basename(
            our_ev.get(r, EVENT_FILE_COL).decode("latin-1").replace("\\", "/"))
    for objs, _t in placements.values():
        for o in objs:
            want = canon.get(o["obj_id"])
            if not want:
                continue
            cur = mob_con_name(o["extra"])
            if cur == want:
                continue
            o["extra"] = o["extra"][:4] + oro.put_bstr(want.encode("latin-1"))
            fixed_refs.append(f"{o['obj_id']}:{cur}->{want}")
    print(f"    {'.CON references fixed':26s} {len(fixed_refs)} "
          + (f"(e.g. {fixed_refs[0]})" if fixed_refs else "(all already exact)"))

    # --- 6f. the placements themselves, last, so a half-written run leaves no NPC
    # pointing at a row or a model that is not there yet.
    files, n = 0, 0
    for (folder, name), (objs, trailing) in sorted(placements.items()):
        dp = os.path.join(dst_maps, folder, name)
        if not os.path.isfile(dp):
            raise SystemExit(f"{dp}: run --stage 1 first")
        dbuf, dbounds = oro.read_ifo(dp)
        doff, dend = oro.lump_block(dbounds, oro.LUMP_MOB)
        if doff is None:
            raise SystemExit(f"{dp}: no MOB lump to fill")
        blob = oro.build_object_lump(objs, trailing)
        if dbuf[doff:dend] == blob:
            continue
        out = oro.build_ifo(dbounds, dbuf, {oro.LUMP_MOB: blob})
        files += 1
        n += len(objs)
        if not dry:
            with open(dp, "wb") as fh:
                fh.write(out)
            vbuf, vbounds = oro.read_ifo(dp)
            voff, vend = oro.lump_block(vbounds, oro.LUMP_MOB)
            if vbuf[voff:vend] != blob:
                raise SystemExit(f"VERIFY FAILED: {dp} MOB lump mismatch")
    print(f"    {'IFO mob lumps':26s} {n} NPCs into {files} files")
    print("\n    They speak Japanese until stage 6c overrides the text through the")
    print("    QEX1 appendix, and the Church still has no way back out -- that is")
    print("    one travel option on one of these, now that they exist.")


def verify(ours):
    """Re-derive the result from what is on disk, after the fact."""
    print("verify -- reading back what is in data/")
    dst_maps = os.path.join(ours, MAPS_REL.replace("\\", "/"))
    bad = 0
    for row, folder, name, key in ZONES:
        d = os.path.join(dst_maps, folder)
        if not os.path.isdir(d):
            print(f"    !! zone {row} {folder}: map folder missing")
            bad += 1
            continue
        hims = [f for f in os.listdir(d) if f.lower().endswith(".him")]
        zon = [f for f in os.listdir(d) if f.lower().endswith(".zon")]
        if not zon:
            print(f"    !! zone {row} {folder}: no .ZON")
            bad += 1
            continue
        p = os.path.join(d, zon[0])
        buf, bounds = zon_lumps(p)
        types = [t for t, _, _ in bounds]
        eo, ee = oro.lump_block(bounds, ZON_LUMP_ECONOMY)
        econ = "missing"
        if eo is not None:
            parse_economy(buf[eo:ee], p)
            econ = f"{ee - eo}B ok"
        names = [n for n, _ in oro.zon_events(p)]
        # Every entity lump stage 1 emptied is counted and reported by name; a
        # lump is only *flagged* if the stage that refills it has not run yet.
        # Reporting rather than asserting-empty is deliberate: holding a lump to
        # the stage-1 expectation forever means every later stage's own output
        # shows up as a fault, which happened twice before this was generalised.
        counts = dict.fromkeys(LUMP_STAGE.values(), 0)
        for f in sorted(os.listdir(d)):
            if not f.lower().endswith(".ifo"):
                continue
            b2, bd2 = oro.read_ifo(os.path.join(d, f))
            for lt in oro.LUMPS_STAGE1_EMPTY:
                off, _ = oro.lump_block(bd2, lt)
                if off is None:
                    continue
                counts[LUMP_STAGE[lt]] += struct.unpack_from("<i", b2, off)[0]
        # "npcs" left this list when stage 6 started filling LUMP_MOB. Only
        # LUMP_EVENT_OBJECT still has no stage that refills it.
        early = [k for k in ("events",) if counts[k]]
        flag = ""
        if eo is None or early:
            flag = f"   <-- CHECK {early}" if early else "   <-- CHECK"
            bad += 1
        shown = " ".join(f"{k}={counts[k]}" for k in ("gates", "spawns", "npcs", "events"))
        print(f"    zone {row:3d} {folder:14s} {len(hims):3d} chunks  "
              f"economy={econ}  events={len(names)}  {shown}{flag}")

    zstb = oro.Stb(os.path.join(ours, ZONE_STB_REL.replace("\\", "/")))
    zstl = oro.Stl(os.path.join(ours, ZONE_STL_REL.replace("\\", "/")))
    for row, folder, name, key in ZONES:
        got_name = zstb.get(row, ZONE_NAME_COL).decode("latin-1")
        got_key = zstb.get(row, ZONE_STL_COL).decode("latin-1")
        zon_path = zstb.get(row, 1).decode("latin-1")
        on_disk = os.path.isfile(os.path.join(ours, zon_path.replace("\\", "/")))
        problems = []
        if got_name != name:
            problems.append(f"name={got_name!r}")
        if got_key != key:
            problems.append(f"key={got_key!r}")
        if not zstl.has(key):
            problems.append("no STL entry")
        if not on_disk:
            problems.append(f"ZONE_FILE not on disk: {zon_path}")
        if problems:
            bad += 1
            print(f"    !! zone {row}: {'; '.join(problems)}")
    # --- stage 2a: gates. Count placements, resolve every destination, and prove
    # the two live Oro gates we remapped around are still pointing at Muris.
    warp = oro.Stb(os.path.join(ours, WARP_STB_REL.replace("\\", "/")))
    zon_events_of, placed = {}, {}
    for row, folder, _, _ in ZONES:
        d = os.path.join(dst_maps, folder)
        if not os.path.isdir(d):
            continue
        zon = [f for f in os.listdir(d) if f.lower().endswith(".zon")][0]
        # zon_events yields the names as bytes; decode so the comparison below is
        # like-for-like. (The stage-2 writer compares bytes to bytes and was fine;
        # only this reader mixed the two, and reported every gate unresolved.)
        zon_events_of[row] = {n.decode("latin-1")
                              for n, _ in oro.zon_events(os.path.join(d, zon))}
        for f in sorted(os.listdir(d)):
            if not f.lower().endswith(".ifo"):
                continue
            b2, bd2 = oro.read_ifo(os.path.join(d, f))
            if oro.lump_block(bd2, oro.LUMP_WARP)[0] is None:
                continue
            objs, _ = oro.read_lump(b2, bd2, oro.LUMP_WARP)
            for o in objs:
                placed.setdefault(o["warp_id"], []).append(folder)
    if placed:
        for wid in sorted(placed):
            dest = warp.get(wid, WARP_DEST_ZONE_COL).strip()
            dest = int(dest) if dest.isdigit() else -1
            event = warp.get(wid, WARP_DEST_EVENT_COL).decode("latin-1").strip()
            ok = dest in zon_events_of and event in zon_events_of[dest]
            if not ok:
                bad += 1
            print("    gate %-4d x%-2d from %-14s -> zone %-4d event %-14s %s"
                  % (wid, len(placed[wid]), ",".join(sorted(set(placed[wid])))[:14],
                     dest, event, "ok" if ok else "<-- UNRESOLVED"))
        for wid, want in ((170, "82"), (172, "82")):
            got = warp.get(wid, WARP_DEST_ZONE_COL).decode("latin-1").strip()
            name = warp.get(wid, 0).decode("latin-1")
            if got != want or "ODE01" not in name:
                bad += 1
                print(f"    !! Oro gate {wid} was overwritten: zone {got!r} {name!r}")
        print("    Oro gates 170/172 still -> zone 82 (Gates of Muris)")
    else:
        print("    gates: none placed yet (stage 2 not run)")

    # --- stage 3: monsters. Rows, names, AI files and CHR entries, plus the
    # spawn population each zone actually carries.
    npc = oro.Stb(os.path.join(ours, NPC_STB_REL.replace("\\", "/")))
    nstl = oro.Stl(os.path.join(ours, NPC_STL_REL.replace("\\", "/")))
    ai = oro.Stb(os.path.join(ours, AI_STB_REL.replace("\\", "/")))
    chr_ = oro.Chr(os.path.join(ours, NPC_CHR_REL.replace("\\", "/")))
    ids = sorted(KARKIA_MONSTERS)
    missing_row = [i for i in ids if not npc.occupied(i)]
    if missing_row:
        print(f"    monsters: none imported yet ({len(missing_row)} of {len(ids)} rows "
              f"empty) -- stage 3 not run")
    else:
        wrong = [(i, npc.get(i, 0).decode("latin-1"))
                 for i in ids if npc.get(i, 0).decode("latin-1") != KARKIA_MONSTERS[i]]
        nokey = [i for i in ids
                 if not nstl.has(npc.get(i, oro.NPC_STRID_COL).decode("latin-1").strip())]
        nochr = [i for i in ids
                 if i >= len(chr_.chars) or chr_.chars[i] is None]
        aip_missing = []
        for i in ids:
            a = npc.get(i, oro.NPC_AI_COL).strip()
            if not a.isdigit() or not int(a):
                continue
            f = ai.get(int(a), 0).decode("latin-1").strip()
            if not f or not os.path.isfile(dest_of(ours, f)):
                aip_missing.append((i, a.decode("latin-1"), f))
        for label, items in (("name mismatch", wrong), ("no STL key", nokey),
                             ("no CHR entry", nochr), ("missing .aip", aip_missing)):
            if items:
                bad += 1
                print(f"    !! monsters {label}: {items[:6]}")
        lv = sorted(npc.get(i, 7).decode("latin-1").strip() or "0" for i in ids)
        print(f"    monsters {len(ids)} rows, {len(nstl.keys)} STL keys total, "
              f"CHR {len(chr_.chars)} entries, levels {lv[0]}-{lv[-1]}, all .aip present")
        # Every skill the AI can cast must resolve to a casting/skill anim pair,
        # or it fires invisibly (see CHR_MOTION_FILL).
        acts = aip_skill_motions(ours, npc, ai, ids)
        silent = chr_anim_audit(chr_, acts)
        if silent:
            bad += 1
            print(f"    !! {len(silent)} skill actions have no animation: "
                  f"{[(n, s, m) for n, s, m, _ in silent[:6]]}")
        else:
            print(f"    skill animations {sum(len(v) for v in acts.values())} "
                  f"AIACT_24 records across {len(acts)} monsters, all animate")

    sky = oro.Stb(os.path.join(ours, SKY_STB_REL.replace("\\", "/")))
    if sky.rows <= SKY_ROW or not sky.occupied(SKY_ROW):
        bad += 1
        print(f"    !! LIST_SKY row {SKY_ROW} missing (rows={sky.rows})")
    else:
        print(f"    LIST_SKY row {SKY_ROW} = "
              f"{sky.get(SKY_ROW, 0).decode('latin-1')}")
    print(f"    LIST_ZONE rows={zstb.rows}  LIST_ZONE_S keys={len(zstl.keys)}")
    print(f"\n    {'ALL CHECKS PASSED' if not bad else f'{bad} PROBLEM(S)'}")
    return bad == 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--stage", type=int, choices=(1, 2, 3, 6), action="append",
                    help="stage to run (repeatable); omit to run them all")
    ap.add_argument("--dry-run", action="store_true", help="preview without writing")
    ap.add_argument("--selftest", action="store_true",
                    help="prove every writer is byte-faithful, then exit")
    ap.add_argument("--verify", action="store_true",
                    help="read back what is in data/ and check it, then exit")
    ap.add_argument("--root", default=".", help="repo root (default: cwd)")
    ap.add_argument("--source", default=DEFAULT_SRC, help="Jrose client root")
    args = ap.parse_args()

    ours = os.path.join(args.root, "data")
    src = args.source
    if not os.path.isdir(ours):
        raise SystemExit(f"not found: {ours} (run from the repo root or pass --root)")
    if args.verify:
        return 0 if verify(ours) else 1
    if not os.path.isdir(src):
        raise SystemExit(f"source client root not found: {src}")

    print(f"source: {src}")
    print(f"target: {os.path.abspath(ours)}\n")
    print("indexing the source tree...")
    src_index = index_tree(src)
    print(f"    {len(src_index)} files\n")

    print("self-test (every writer must round-trip byte-identically):")
    if not selftest(ours, src, src_index):
        raise SystemExit("self-test FAILED -- refusing to write")
    if args.selftest:
        return 0

    print()
    for s in sorted(set(args.stage or (1, 2, 3, 6))):
        {1: stage1, 2: stage2, 3: stage3, 6: stage6}[s](ours, src, src_index, args.dry_run)
        print()

    if args.dry_run:
        print("dry run: nothing written")
    else:
        print("done -- .bak backups alongside every table touched.")
        print("Next: --verify, then rebake the client VFS and restart the servers.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
