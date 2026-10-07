"""Import the 667 build's Oro planet -- maps, monsters, NPCs, gates. No quests.

Replaces the RoseZA-era Oro that scripts/remove-oro.py took out. Run that first;
this script refuses to start while the old rows are still occupied.

WHY TWO SOURCES

The 667 dump (`SRC_667`) is the authority for everything it has: the ten zone
rows, the map folders, the object tables, LIST_NPC rows 0-42, the NPC and zone
names (English, in-table), the warp rows and the placements. It ships NO `.aip`,
no `FILE_AI.STB`, no readable dialogs (`.CXE` only), no `ulngtb_con.ltb` and no
`.MOV`. So RoseZA (`SRC_ZA`) supplies AI files by *filename* -- 667's AI-type
column indexes a table it does not ship, and its low numbers (47, 91) do not line
up with our FILE_AI rows -- plus the one dialog we lacked (EM71-034) and the two
motions 667 forgot (asperCastRange.ZMO, icanes05/casting_01.ZMO). Dialogs, dialog
text and the 38 Oro `.aip` files already under data/ are reused, not re-copied.

WHAT IS AUTHORED HERE, AND WHY

  * `AI_DONOR`: ten species have no AI in any dump we own (Golden Scarab
    2279-2281, Ikaness 2290-2296). The Ikaness borrow our retail Ikaness AIs
    (rows 424-430: same skeletons, same casting motions); the Scarabs borrow the
    Scorpio line, which casts no skill -- their model has no casting animation
    (anim types 0-5 only). chr_anim_audit proves every cast animates.
  * The summon closure. Nineteen Oro monsters `SummonMaster` Shadow Ghosts
    2236/2237, and Ghost Seed 706's ghost_m5.aip `Change_CHAR`s into 2230-2239;
    no map spawns them and they were blank here. Every id reachable through the
    six monster-carrying AI actions (audit-ai-monster-refs.py's table) is walked
    transitively from the roster and imported when a dump has it. Ids no dump has
    (Lucky Pig 3001-3003) are left to that audit to strip.
  * `EXCLUDE_MONSTERS`: the Cactus 2181 is a 667 "interactive object" NPC (their
    cols 52-57), a system we do not have. Its 147 regen points are removed.
  * `DROP_NPC_PLACEMENTS`: three TOWN placements (Daih'vyd, Roen, Battlemaster
    Amber) have neither a row nor a dialog anywhere -- event/arena NPCs.
  * `GATE_REMAP`: 667's new warp rows 178-180 collide with live Karkia gates.
  * Spawn camps: every Oro field map gets Karkia's 60 m camps of five
    (`CAMP_ZONES`, `consolidate_camps`). Until 2026-10-08 only the two
    single-species carpets (ODFS01, ODGR01) were merged, and only their count-1
    points: alpha testers found Oro overcrowded, and it measured 0.47-1.01x
    JG07 against Karkia's 0.27-0.43x. Boss points are never merged, and are
    capped at one alive (`BOSS_MIN_INTERVAL`). scripts/fix-oro-spawn-density.py
    replays just this step onto data/ without re-running stage 3.
  * Drop columns 18-20 come from the RoseZA row (667 moved drops to its own
    cols 88-102), so tables 773-851 stay reserved and, as today, nothing drops
    until the drop pass; new species get fresh free table ids at NPC_DROP_ITEM
    100 for the same reason.
  * Zone join triggers (cols 22-24) are blank: 667's `ZONE_PlanetOrlo` hooks
    are quest-side, and we import no quests.

Stats are 667's, deliberately -- levels run to 255 against our 240 cap and HP is
~3x RoseZA's. scripts/rebalance-oro-667.py is the pass that follows.

Idempotent; --dry-run; --selftest; --verify; --revert (put back every table it
overwrote from build/oro-667/import-pre/ and delete every file it created).
Never leaves a .bak under data/ (pack.ps1 refuses them).

    python scripts/import-oro-667.py --selftest
    python scripts/import-oro-667.py --dry-run
    python scripts/import-oro-667.py              # stages 1 2 3 4
    python scripts/import-oro-667.py --verify
"""
import argparse
import collections
import importlib.util
import json
import os
import re
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC_667 = r"C:\Users\Thomas\Desktop\Testclients\667\extracted data"
SRC_ZA = r"C:\Users\Thomas\Desktop\Testclients\RoseZA test client\data"

# (row, folder). Names, keys and revive events are read from the 667 tables.
ZONES = [(14, "COLOSSEUM"), (71, "TOWN"), (72, "OROIP"), (73, "ODP01"),
         (78, "ODD04"), (79, "ODD05"), (80, "ODOS01"), (81, "ODRP01"),
         (83, "ODGR01"), (85, "ODFS01")]
ZONE_ROWS = {r for r, _ in ZONES}
FOLDER_OF = dict(ZONES)
MAPS_REL = r"3DDATA\MAPS\ORO"
ZONE_COPY_COLS = 31
ZONE_TRIGGER_COLS = (22, 23, 24)
ZONE_REVIVE_COL, ZONE_START_COL, ZONE_STL_COL = 3, 2, 26
REVIVE_CANDIDATES = ("restore", "restor", "respawn", "start")
ORO_ZSC_TABLES = [r"3DDATA\ORO\LIST_DECO_ODT.ZSC", r"3DDATA\ORO\LIST_CNST_ODT.ZSC",
                  r"3DDATA\ORO\LIST_DECO_ODD.ZSC", r"3DDATA\ORO\LIST_CNST_ODD.ZSC"]
SHARED_ZSC_TABLES = [r"3DDATA\JUNON\LIST_DECO_JZP.ZSC", r"3DDATA\JUNON\LIST_CNST_JG.ZSC"]
EDITOR_TABLES = [r"3DDATA\STB\LIST_CNST_ODT.STB", r"3DDATA\STB\LIST_TERRAIN_OBJECT_ODT.STB",
                 r"3DDATA\STB\LIST_CNST_ODD.STB", r"3DDATA\STB\LIST_TERRAIN_OBJECT_ODD.STB"]
ZONETYPE_RETARGET = {"TOWN": (15, 17)}      # folder -> (667 type, ours); see import-oro.py
ZONETYPE_STB_REL = r"3DDATA\TERRAIN\TILES\ZONETYPEINFO.STB"

GATE_REMAP = {178: 187, 179: 188, 180: 193}  # 178-180 are live Karkia gates here

EXCLUDE_MONSTERS = {2181}
DROP_NPC_PLACEMENTS = {1496, 1497, 1992}
CON_ALIAS = {f"EM71-034-{i:02d}": "EM71-034" for i in range(1, 7)}  # 667's own LIST_EVENT does this
AI_DONOR = {2279: 2237, 2280: 2238, 2281: 2240,          # Golden Scarab <- Scorpio line
            2290: 424, 2291: 425, 2292: 426, 2293: 427,  # Ikaness <- retail Ikaness
            2294: 428, 2295: 429, 2296: 430}
NPC_BLANK_COLS = (38, 41)          # 667 death-event target/trigger: quest-side
# 667 stores attack speed (game col 14) in a different unit: 3300 where RoseZA
# and our retail rows hold 40-110. Copied verbatim it read as a 33x attack rate.
# Rows both dumps have take RoseZA's value; the 19 new species take a sibling's.
ATK_SPEED_COL = 14
# NPC_TYPE (game col 27) is drawn straight as a sprite index into
# TARGETMARK.TSI beside the focused monster's HP bar (CNameBox::DrawTargetMark).
# 667 added composite codes -- 42 Leader-Guard, 45 Leader-Ranger, 72 King-Guard,
# 75 King-Ranger ... -- that fall off the sheet, so those monsters showed no mark.
# The units digit is the role, the tens digit the rank (4 leader / 5 captain /
# 6 sub-boss / 7 king / 8 elite). Kings fold to the crown (11) and elites to the
# skull (16); leaders, captains and sub-bosses keep their role, since Oro's sit
# at normal strength on our stats (folding kings to the role put a bow on the
# Terrasaurus King -- fix-monster-marks.py). The server only tests this col
# against 900.
NPC_TYPE_COL = 27
NPC_TYPE_COMPOSITE_MIN, NPC_TYPE_NPC_MIN = 40, 900
NPC_TYPE_RANK_FOLD = {7: 11, 8: 16}   # tens digit -> single mark; others fold to the role
# Bare-handed monsters present their hit through NPC_HAND_HIT_EFFECT (game col 33,
# a LIST_EFFECT row). 667 left it blank on the Golden Scarabs and the Hungry Desert
# Scavenger, which lands no visible impact; 403 is the most common retail value.
HAND_HIT_COL, HAND_HIT_DEFAULT = 33, b"403"
ATK_SPEED_DONOR = {2274: 2257, 2275: 2258, 2276: 2259,         # Armastyx <- Mastyx
                   2279: 2251, 2280: 2252, 2281: 2254,         # Scarab <- Scorpio
                   2284: 2251, 2285: 2252,                     # Darkened Scorpio
                   2286: 2205, 2287: 2204,                     # Darkened Asper
                   2290: 1576, 2291: 1577, 2292: 1578, 2293: 1579,
                   2294: 1580, 2295: 1581, 2296: 1582}         # Ikaness <- retail
NPC_DROP_COLS = (18, 19, 20)
NEW_SPECIES_DROP_ITEM = b"100"     # always the (empty) table, never the zone fallback
DROP_TABLE_FIRST_FREE = 852
# Camp consolidation: folder -> (grid cell in cm, camp size). Karkia's values
# (import-karkia.py SPAWN_CAMPS / CAMP_*), on every Oro map that spawns monsters
# except Muris, whose outskirts already sit at Karkia's density (0.25x).
#
# Steady-state population, CRegenPOINT::Proc simulated to fill, measured per
# occupied 100 m cell against JG07 (32.2, our densest field zone):
#
#                       before 2026-10-08                after
#              bodies  per cell  vs JG07  gap    bodies  per cell  vs JG07  gap
#   ODD04         685     20.8    0.64x  12 m       304     10.9    0.34x  36 m
#   ODD05         681     28.4    0.88x  13 m       228     10.4    0.32x  39 m
#   ODRP01        526     19.5    0.60x  26 m       216      8.6    0.27x  45 m
#   ODGR01        829     15.1    0.47x  19 m       599     10.9    0.34x  37 m
#   ODFS01      1,786     32.5    1.01x  13 m       579     11.8    0.37x  36 m
#
# Karkia sits at 0.27-0.43x with a ~38 m gap; "gap" is the median distance to
# the nearest other spawn point. The old pass merged only the carpet (one
# species, count 1 in every slot) and passed the RoseZA-style nests through:
# cap 10, counts of 6-7, a 5 s tick and a 20-30 m spread, ~12 m apart. In
# ODFS01 those nests held 1,240 of the 1,786 bodies the "merged" map carried.
CAMP_ZONES = {"ODD04": (6000, 5), "ODD05": (6000, 5), "ODRP01": (6000, 5),
              "ODGR01": (6000, 5), "ODFS01": (6000, 5)}
CAMP_INTERVAL, CAMP_RANGE, CAMP_TACTIC_POINT = 20, 12, 100
# A single-species point that respawns slower than this is a boss: never merged,
# and held at one alive. 667 authored the Fearsome Terrasaurus King's three
# points at cap 5 with a 30-50 minute tick, so an unattended point stacked a
# king every half hour, up to five.
BOSS_MIN_INTERVAL = 600

# Verified 2026-09-13 by reading both headers: 667's first 44 reader columns are
# ours under reworded labels. Positional copy is only valid while that holds.
NPC_HEADERS_667 = [
    "NPC", "Name", "Mon Filename Assigned", "Walking Speed", "Running Speed",
    "Size of Monster", "Right Weapon No.", "Left Weapon No.", "Level", "HP", "ATK",
    "Accuracy", "DEF", "MDEF", "Flee", "ATK Speed", "Normal/Magic", "AI Type", "EXP",
    "NPC Minimap Icon", "Summon Gauge Cost", "Store Currency", "Trade Tab1",
    "Trade Tab2", "Trade Tab3", "Trade Tab 4", "Target Type",
    "Bare Hands ATK Range(cm)", "Character Type", "Texture Type", "Face Icon",
    "General Sound Effect", "Attacking Sound Effect", "Attacked Sound Effect",
    "Bare Hands Attacking Effect", "Dying Effect", "Dying Sound Effect",
    "Editor's Data", "10:Editor Appearance\n11: Regen List", "Death Event Target",
    "Glow Effect", "Description", "Death Event Trigger", "HP Gauge Bar"]
ZONE_HEADERS_667 = [
    "zone type", "Region Name", "Zone Files (including the path)",
    "Name starting position", "Location name resurrected", "Terrain",
    "Background Music (Day)", "Background Music (Night)", "Background Image",
    "Minimap Image location", "Zone Start Map Location", "Zone Start Map Location",
    "Objects Table", "Building Table", "Day Cycle", "Morning Hour", "Day Hour",
    "Evening Hour", "Night Hour", "PVP Allowance", "Planet Number",
    "Sound Table Call", "Camera Type", "Zone Transfer Trigger", "Killing Trigger",
    "Death Trigger", "Size of Area", "Name", "Weather", "PartyExpA", "PartyExpB"]

# audit-ai-monster-refs.py's table: every one carries WORD monster id at offset 8.
G = 0x0B000000
MONSTER_ACTS = {0x0A | G, 0x0B | G, 0x13 | G, 0x15 | G, 0x25 | G, 0x26 | G}

BUILD_DIR = os.path.join("build", "oro-667")
PRE_DIR = "import-pre"
MANIFEST = "import-manifest.json"

ROLE_SUFFIX = re.compile(r"\s*\((?:[A-Za-z\- ]+)\)\s*$")
LEVEL_PREFIX = re.compile(r"^\(\d+\)\s*")
CODE_PREFIX = re.compile(r"^\([A-Z0-9_]+\)\s*")


def load(name, fname, argv_extra=()):
    spec = importlib.util.spec_from_file_location(name, os.path.join(HERE, fname))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, [fname, *argv_extra]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    return mod


oro = load("import_oro", "import-oro.py")
kk = load("import_karkia", "import-karkia.py")
rdr = load("rose_data_reader", "rose-data-reader.py")
oro.backup = lambda path: None      # our undo is build/oro-667/, never a .bak


def P(root, rel):
    return os.path.join(root, rel.replace("\\", "/"))


def zon_in(dst_maps, src_maps, folder):
    """The zone's .ZON: ours once stage 1 has run, the 667 copy in a dry run."""
    for d in (os.path.join(dst_maps, folder), os.path.join(src_maps, folder)):
        if os.path.isdir(d):
            zon = [f for f in os.listdir(d) if f.lower().endswith(".zon")]
            if zon:
                return os.path.join(d, zon[0])
    raise SystemExit(f"{folder}: no .ZON in ours or the source")


def num(stb, r, c):
    v = stb.get(r, c).strip()
    return int(v) if v.lstrip(b"-").isdigit() else 0


def row_blank(stb, i):
    """No name and no model file. `occupied()` is too strict: our rows 6/7 carry
    stray cells (an AI number, a range) and nothing else -- spawning them is the
    blank-row crash class, so they must be imported over."""
    return i >= stb.rows or not (stb.get(i, 0).strip() or stb.get(i, 1).strip())


def clean_name(raw):
    s = raw.decode("latin-1").strip()
    s = LEVEL_PREFIX.sub("", s)
    s = ROLE_SUFFIX.sub("", s)
    return s.strip()


def clean_zone_name(raw):
    return CODE_PREFIX.sub("", raw.decode("latin-1").strip()).strip()


def stl_english(stl):
    """key -> block-1 (English) text, falling back to block 0."""
    out = {}
    for i, (k, _) in enumerate(stl.keys):
        txt = stl.langs[1][i][0] if len(stl.langs) > 1 else b""
        out[k.decode("latin-1")] = (txt or stl.langs[0][i][0]).decode("latin-1")
    return out


class Snapshot:
    """Pre-write copies of every existing file a stage overwrites, plus the list
    of files it creates, so --revert can undo the import exactly."""

    def __init__(self, root, ours, dry):
        self.ours, self.dry = ours, dry
        self.dir = os.path.join(root, BUILD_DIR, PRE_DIR)
        self.mpath = os.path.join(root, BUILD_DIR, MANIFEST)
        self.man = {"overwritten": [], "created": []}
        if os.path.exists(self.mpath):
            self.man = json.load(open(self.mpath, encoding="utf-8"))

    def keep(self, rel):
        """Snapshot a file we are about to overwrite (first time only)."""
        rel = rel.replace("\\", "/")
        p = os.path.join(self.ours, rel)
        if self.dry or not os.path.isfile(p):
            return
        d = os.path.join(self.dir, rel)
        if os.path.exists(d):
            return
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copyfile(p, d)
        if rel not in self.man["overwritten"]:
            self.man["overwritten"].append(rel)
        self.save()

    def created(self, rels):
        if self.dry:
            return
        for rel in rels:
            rel = rel.replace("\\", "/")
            if rel not in self.man["created"]:
                self.man["created"].append(rel)
        self.save()

    def save(self):
        os.makedirs(os.path.dirname(self.mpath), exist_ok=True)
        with open(self.mpath, "w", encoding="utf-8") as fh:
            json.dump(self.man, fh)


def write(snap, rel, blob):
    snap.keep(rel)
    if snap.dry:
        return
    p = P(snap.ours, rel)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p, "wb") as fh:
        fh.write(blob)


def tree_files(ours):
    out = set()
    for base, _d, files in os.walk(ours):
        for f in files:
            out.add(os.path.relpath(os.path.join(base, f), ours).replace("\\", "/"))
    return out


# ------------------------------------------------------------- preflight
def selftest(ours, s667, sza, idx):
    ok = True

    def check(label, same):
        nonlocal ok
        ok = ok and same
        print(f"    {label:52s} {'OK' if same else 'FAIL'}")

    a = rdr.Stb(P(s667, r"3DDATA\STB\LIST_NPC.STB"), "latin-1")
    got = [x.decode("latin-1") for x in a.colnames[:44]]
    check("667 LIST_NPC header fingerprint (cols 0-43)", got == NPC_HEADERS_667)
    if got != NPC_HEADERS_667:
        for i, (x, y) in enumerate(zip(got, NPC_HEADERS_667)):
            if x != y:
                print(f"        col {i}: {x!r} != {y!r}")
    z = rdr.Stb(P(s667, r"3DDATA\STB\LIST_ZONE.STB"), "latin-1")
    got = [x.decode("latin-1") for x in z.colnames[:31]]
    check("667 LIST_ZONE header fingerprint (cols 0-30)", got == ZONE_HEADERS_667)
    ours_npc = rdr.Stb(P(ours, r"3DDATA\STB\LIST_NPC.STB"), "latin-1")
    check("our LIST_NPC has 44 game columns", ours_npc.cols == 44)

    for rel in (r"3DDATA\STB\LIST_ZONE.STB", r"3DDATA\STB\LIST_NPC.STB", r"3DDATA\STB\WARP.STB",
                r"3DDATA\STB\FILE_AI.STB", r"3DDATA\STB\LIST_WEAPON.STB",
                r"3DDATA\STB\LIST_EVENT.STB", r"3DDATA\STB\LIST_SKY.STB", ZONETYPE_STB_REL):
        p = P(ours, rel)
        check(f"STB round-trip {os.path.basename(p)}", oro.Stb(p).to_bytes() == open(p, "rb").read())
    for rel in (r"3DDATA\STB\LIST_ZONE.STB", r"3DDATA\STB\LIST_NPC.STB", r"3DDATA\STB\WARP.STB"):
        p = P(s667, rel)
        check(f"667 STB round-trip {os.path.basename(p)}", oro.Stb(p).to_bytes() == open(p, "rb").read())
    for root, label in ((ours, "our"), (s667, "667")):
        for rel in (r"3DDATA\STB\LIST_NPC_S.STL", r"3DDATA\STB\LIST_ZONE_S.STL"):
            p = P(root, rel)
            check(f"{label} STL round-trip {os.path.basename(p)}",
                  oro.Stl(p).to_bytes() == open(p, "rb").read())
        p = P(root, r"3DDATA\NPC\LIST_NPC.CHR")
        check(f"{label} CHR round-trip", oro.Chr(p).to_bytes() == open(p, "rb").read())
        p = P(root, r"3DDATA\NPC\PART_NPC.ZSC")
        check(f"{label} PART_NPC.ZSC round-trip", oro.Zsc(p).to_bytes() == open(p, "rb").read())
    p = P(ours, r"3DDATA\EVENT\ulngtb_con.ltb")
    check("our ulngtb_con.ltb round-trip", oro.Ltb(p).to_bytes() == open(p, "rb").read())
    p = P(sza, r"3DDATA\EVENT\ulngtb_con.ltb")
    check("ZA ulngtb_con.ltb round-trip", oro.Ltb(p).to_bytes() == open(p, "rb").read())

    n_ifo = bad = 0
    for _, folder in ZONES:
        d = P(s667, f"{MAPS_REL}\\{folder}")
        for f in os.listdir(d):
            p = os.path.join(d, f)
            if f.lower().endswith(".ifo"):
                buf, bounds = oro.read_ifo(p)
                repl = {}
                for t, off, end in bounds:
                    if t in (oro.LUMP_MOB, oro.LUMP_REGEN, oro.LUMP_WARP, oro.LUMP_EVENT_OBJECT,
                             oro.LUMP_EFFECT, oro.LUMP_SOUND, oro.LUMP_OBJECT):
                        objs, tr = oro.read_lump(buf, bounds, t)
                        repl[t] = oro.build_object_lump(objs, tr)
                n_ifo += 1
                if oro.build_ifo(bounds, buf, repl) != buf:
                    bad += 1
            elif f.lower().endswith(".zon"):
                buf, bounds = kk.zon_lumps(p)
                eo, ee = oro.lump_block(bounds, kk.ZON_LUMP_ECONOMY)
                if eo is None:
                    bad += 1
                    print(f"        {f}: no LUMP_ECONOMY")
                else:
                    kk.parse_economy(buf[eo:ee], p)
    check(f"667 IFO round-trip ({n_ifo} files) + .ZON economy", bad == 0)
    for rel in (r"3DDATA\MOTION\NPC\asper\asperCastRange.ZMO",
                r"3DDATA\MOTION\NPC\icanes05\casting_01.ZMO",
                r"3DDATA\AI\ghost_at_s5.aip", r"3DDATA\AI\ghost_m5.aip",
                r"3DDATA\EVENT\EM71-034.CON"):
        check(f"source chain has {os.path.basename(rel)}", kk.key_of(rel) in idx)
    return ok


def assert_removed(root, ours):
    """remove-oro.py's manifest is the proof it ran; the rows it blanked are
    exactly the ones stage 1 refills, so they cannot be the test."""
    if not os.path.exists(os.path.join(root, "build", "oro-removal", "manifest.json")):
        raise SystemExit("remove-oro.py has not run (no build/oro-removal/manifest.json)")
    zone = oro.Stb(P(ours, r"3DDATA\STB\LIST_ZONE.STB"))
    stale = [r for r in (74, 75, 76, 77, 82) if zone.occupied(r)]
    if stale:
        raise SystemExit(f"RoseZA-era zone rows still occupied: {stale}")


# --------------------------------------------------------- stage 1: zones
def stage1(ours, s667, sza, idx, snap):
    dry = snap.dry
    print("stage 1 -- terrain, art, object tables, zone rows, zone names")
    src_maps, dst_maps = P(s667, MAPS_REL), P(ours, MAPS_REL)
    template = kk.economy_template(ours)

    # --- 1a. map folders. Everything but the .MOV we kept; entity lumps emptied.
    copied = rewritten = spliced = 0
    total = 0
    created = []
    for _, folder in ZONES:
        s, d = os.path.join(src_maps, folder), os.path.join(dst_maps, folder)
        if not os.path.isdir(s):
            raise SystemExit(f"source map folder missing: {s}")
        for base, _dirs, names in os.walk(s):
            sub = os.path.relpath(base, s)
            dbase = d if sub == "." else os.path.join(d, sub)
            for name in sorted(names):
                sp, dp = os.path.join(base, name), os.path.join(dbase, name)
                if os.path.isfile(dp):
                    continue
                low = name.lower()
                blob = None
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
                    blob = kk.zon_with_economy(sp, template)
                    if blob is None:
                        blob = open(sp, "rb").read()
                    else:
                        spliced += 1
                if not dry:
                    os.makedirs(dbase, exist_ok=True)
                    if blob is None:
                        shutil.copyfile(sp, dp)
                    else:
                        with open(dp, "wb") as fh:
                            fh.write(blob)
                created.append(os.path.relpath(dp, ours))
                copied += 1
                total += os.path.getsize(sp)
    snap.created(created)
    print(f"    {'map files':26s} {copied:5d} new files  {total / 1048576:7.2f} MB   "
          f"({rewritten} .IFO lumps emptied, {spliced} .ZON economy spliced)")

    # --- 1b. tiles
    tiles = set()
    for _, folder in ZONES:
        d = os.path.join(src_maps, folder)
        zon = [f for f in os.listdir(d) if f.lower().endswith(".zon")][0]
        tiles |= {t for t in oro.zon_tiles(os.path.join(d, zon)) if "\\" in t or "/" in t}
    kk.copy_new(tiles, idx, ours, dry, "terrain tiles")

    # --- 1c. object tables (667's, overwriting ours) and everything they name
    for rel in ORO_ZSC_TABLES + EDITOR_TABLES:
        sp = P(s667, rel)
        if not os.path.isfile(sp):
            raise SystemExit(f"source table missing: {sp}")
        blob = open(sp, "rb").read()
        dp = P(ours, rel)
        if os.path.isfile(dp) and open(dp, "rb").read() == blob:
            continue
        write(snap, rel, blob)
    print(f"    {'object/editor tables':26s} {len(ORO_ZSC_TABLES) + len(EDITOR_TABLES)} "
          f"written from 667 (JZP/JG tables are ours already)")
    art = set()
    for rel in ORO_ZSC_TABLES + SHARED_ZSC_TABLES:
        src = P(s667, rel) if os.path.isfile(P(s667, rel)) else P(ours, rel)
        art |= oro.zsc_asset_refs(oro.Zsc(src))
    for _, folder in ZONES:
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
    art |= kk.effect_chain({a for a in art if a.lower().endswith(".eft")}, idx)
    kk.copy_new(art, idx, ours, dry, "deco/cnst art")

    # --- 1d. sky: 667 row 10 is byte-identical to ours; only assert it
    sky = oro.Stb(P(ours, r"3DDATA\STB\LIST_SKY.STB"))
    s667_sky = oro.Stb(P(s667, r"3DDATA\STB\LIST_SKY.STB"))
    if [sky.get(10, c) for c in range(3)] != [s667_sky.get(10, c) for c in range(3)]:
        raise SystemExit("LIST_SKY row 10 differs from 667 -- inspect before overwriting")
    print(f"    {'LIST_SKY.STB':26s} row 10 already matches 667")

    # --- 1e. zone types
    zt = oro.Stb(P(ours, ZONETYPE_STB_REL))
    for _, folder in ZONES:
        p = zon_in(dst_maps, src_maps, folder)
        zon = os.path.basename(p)
        cur = oro.zon_zone_type(p)
        was, now = ZONETYPE_RETARGET.get(folder, (cur, cur))
        if cur == was and was != now and not dry:
            oro.zon_zone_type(p, now)
            cur = now
        if cur not in (was, now):
            raise SystemExit(f"{zon}: ZoneType {cur}, expected {was} or {now}")
        if not (zt.rows > cur and zt.occupied(cur)):
            raise SystemExit(f"{zon}: ZoneType {cur} has no ZONETYPEINFO row")
    print(f"    {'.ZON zone types':26s} all rows present; TOWN retargeted "
          f"{ZONETYPE_RETARGET['TOWN'][0]}->{ZONETYPE_RETARGET['TOWN'][1]}")

    # --- 1f. zone rows
    src_zone = oro.Stb(P(s667, r"3DDATA\STB\LIST_ZONE.STB"))
    zstb = oro.Stb(P(ours, r"3DDATA\STB\LIST_ZONE.STB"))
    if zstb.rows <= max(ZONE_ROWS):
        raise SystemExit("our LIST_ZONE is shorter than expected")
    changed = 0
    for row, folder in ZONES:
        if not src_zone.occupied(row):
            raise SystemExit(f"667 LIST_ZONE row {row} ({folder}) is empty")
        events = {n.decode("latin-1") for n, _ in oro.zon_events(zon_in(dst_maps, src_maps, folder))}
        before = list(zstb.d[row])
        for c in range(zstb.cols):
            zstb.set(row, c, src_zone.get(row, c) if c < ZONE_COPY_COLS else b"")
        zstb.set(row, 0, clean_zone_name(src_zone.get(row, 0)))
        for col in (ZONE_START_COL, ZONE_REVIVE_COL):
            want = src_zone.get(row, col).decode("latin-1")
            if want not in events:
                alt = next((c for c in REVIVE_CANDIDATES if c in events), None)
                if alt is None:
                    raise SystemExit(f"{folder}: no revive event among {sorted(events)}")
                zstb.set(row, col, alt)
        for c in ZONE_TRIGGER_COLS:
            zstb.set(row, c, b"")
        if zstb.d[row] != before:
            changed += 1
    print(f"    {'LIST_ZONE.STB':26s} {changed} of {len(ZONES)} rows written "
          f"(cols 0-30 from 667, triggers blank)")
    write(snap, r"3DDATA\STB\LIST_ZONE.STB", zstb.to_bytes())

    # --- 1g. zone names: same keys as before, text refreshed from 667
    src_stl = oro.Stl(P(s667, r"3DDATA\STB\LIST_ZONE_S.STL"))
    eng = stl_english(src_stl)
    zstl = oro.Stl(P(ours, r"3DDATA\STB\LIST_ZONE_S.STL"))
    added = renamed = 0
    for row, folder in ZONES:
        key = zstb.get(row, ZONE_STL_COL).decode("latin-1").strip()
        text = eng.get(key) or zstb.get(row, 0).decode("latin-1")
        if zstl.has(key):
            if zstl.name(key, 0) != text:
                zstl.set_name(key, text)
                renamed += 1
        else:
            zstl.append(key, int(key[4:]), text)
            added += 1
    print(f"    {'LIST_ZONE_S.STL':26s} +{added} keys, {renamed} renamed (now {len(zstl.keys)})")
    write(snap, r"3DDATA\STB\LIST_ZONE_S.STL", zstl.to_bytes())


# --------------------------------------------------------- stage 2: gates
def stage2(ours, s667, sza, idx, snap):
    dry = snap.dry
    print("stage 2 -- warp gates")
    src_maps, dst_maps = P(s667, MAPS_REL), P(ours, MAPS_REL)
    src_warp = oro.Stb(P(s667, r"3DDATA\STB\WARP.STB"))
    our_warp = oro.Stb(P(ours, r"3DDATA\STB\WARP.STB"))

    placed = {}
    for _, folder in ZONES:
        d = os.path.join(src_maps, folder)
        for name in sorted(os.listdir(d)):
            if not name.lower().endswith(".ifo"):
                continue
            buf, bounds = oro.read_ifo(os.path.join(d, name))
            objs, _ = oro.read_lump(buf, bounds, oro.LUMP_WARP)
            for o in objs or []:
                placed.setdefault(o["warp_id"], []).append((folder, name))
    usable, skipped = {}, {}
    for wid, where in sorted(placed.items()):
        dest = num(src_warp, wid, 1)
        (usable if dest in ZONE_ROWS else skipped)[wid] = (dest, where)
    for wid, (dest, where) in skipped.items():
        print(f"    skip gate {wid:4d} -> zone {dest} (not in the 667 zone set): "
              f"{sorted({f for f, _ in where})}")

    bad = []
    for wid, (dest, _) in sorted(usable.items()):
        want = src_warp.get(wid, 2)
        if want not in [n for n, _ in oro.zon_events(zon_in(dst_maps, src_maps, FOLDER_OF[dest]))]:
            bad.append((wid, dest, want))
    if bad:
        for b in bad:
            print(f"    !! gate {b[0]} -> zone {b[1]} event {b[2]!r} missing")
        raise SystemExit("destination event positions missing -- refusing to write")
    print(f"    {'event positions':26s} all {len(usable)} resolve byte-for-byte")

    written, remapped = 0, []
    for wid, (dest, _) in sorted(usable.items()):
        oid = GATE_REMAP.get(wid, wid)
        if oid >= our_warp.rows:
            raise SystemExit(f"warp id {oid} beyond our WARP.STB")
        before = list(our_warp.d[oid])
        if any(x.strip() for x in before) and our_warp.get(oid, 0) != src_warp.get(wid, 0):
            raise SystemExit(f"WARP.STB row {oid} is occupied by "
                             f"{our_warp.get(oid, 0).decode('latin-1')!r} -- extend GATE_REMAP")
        for c in range(min(our_warp.cols, src_warp.cols)):
            our_warp.set(oid, c, src_warp.get(wid, c))
        if our_warp.d[oid] != before:
            written += 1
        if oid != wid:
            remapped.append(f"{wid}->{oid}")
    print(f"    {'WARP.STB':26s} {written} rows written, {len(usable)} gates"
          + (f", remapped {remapped}" if remapped else ""))
    write(snap, r"3DDATA\STB\WARP.STB", our_warp.to_bytes())

    files = gates = 0
    for _, folder in ZONES:
        s, d = os.path.join(src_maps, folder), os.path.join(dst_maps, folder)
        for name in sorted(os.listdir(s)):
            if not name.lower().endswith(".ifo"):
                continue
            sbuf, sbounds = oro.read_ifo(os.path.join(s, name))
            sobjs, _ = oro.read_lump(sbuf, sbounds, oro.LUMP_WARP)
            keep = []
            for o in sobjs or []:
                if o["warp_id"] not in usable:
                    continue
                fixed = bytearray(o["fixed"])
                struct.pack_into("<h", fixed, 0, GATE_REMAP.get(o["warp_id"], o["warp_id"]))
                keep.append(dict(o, fixed=bytes(fixed)))
            if not keep:
                continue
            dp = os.path.join(d, name)
            if not os.path.isfile(dp):
                if dry:
                    files += 1
                    gates += len(keep)
                    continue
                raise SystemExit(f"{dp}: run --stage 1 first")
            dbuf, dbounds = oro.read_ifo(dp)
            have, dtrail = oro.read_lump(dbuf, dbounds, oro.LUMP_WARP)
            blob = oro.build_object_lump(keep, dtrail)
            doff, dend = oro.lump_block(dbounds, oro.LUMP_WARP)
            if dbuf[doff:dend] == blob:
                continue
            files += 1
            gates += len(keep)
            if not dry:
                with open(dp, "wb") as fh:
                    fh.write(oro.build_ifo(dbounds, dbuf, {oro.LUMP_WARP: blob}))
    print(f"    {'IFO warp lumps':26s} {gates} gate objects into {files} files")


def fix_attack_speed(our_npc, za_npc, ids):
    """Attack speed from RoseZA's row, else from ATK_SPEED_DONOR (ours by then).

    Runs on every id, written or kept, so a re-run repairs rows an earlier
    version copied verbatim. Returns [(id, old, new)] for what it changed.
    """
    changed = []
    for i in ids:
        if not our_npc.occupied(i):
            continue
        if za_npc.occupied(i) and za_npc.get(i, ATK_SPEED_COL).strip():
            want = za_npc.get(i, ATK_SPEED_COL).strip()
        elif i in ATK_SPEED_DONOR:
            want = our_npc.get(ATK_SPEED_DONOR[i], ATK_SPEED_COL).strip()
            if not want:
                raise SystemExit(f"ATK_SPEED_DONOR {i} -> {ATK_SPEED_DONOR[i]}: donor has no value")
        else:
            continue
        cur = our_npc.get(i, ATK_SPEED_COL).strip()
        if cur != want:
            changed.append((i, cur.decode("latin-1"), want.decode("latin-1")))
            our_npc.set(i, ATK_SPEED_COL, want)
    return changed


def fix_presentation(our_npc, ids):
    """Fold 667's composite type codes to one mark (NPC_TYPE_RANK_FOLD), and give bare-handed
    monsters a hand-hit effect. Oro rows only (>= 2100); idempotent."""
    types, hits = [], []
    for i in ids:
        if i < 2100 or not our_npc.occupied(i):
            continue
        t = num(our_npc, i, NPC_TYPE_COL)
        if NPC_TYPE_COMPOSITE_MIN <= t < NPC_TYPE_NPC_MIN and t % 10:
            folded = NPC_TYPE_RANK_FOLD.get(t // 10, t % 10)
            our_npc.set(i, NPC_TYPE_COL, str(folded))
            types.append((i, t, folded))
        armed = any(num(our_npc, i, c) for c in (oro.NPC_R_WEAPON_COL, oro.NPC_L_WEAPON_COL))
        if (t < NPC_TYPE_NPC_MIN and not armed and not our_npc.get(i, HAND_HIT_COL).strip()
                and our_npc.get(i, 7).strip()):
            our_npc.set(i, HAND_HIT_COL, HAND_HIT_DEFAULT)
            hits.append(i)
    return types, hits


# ------------------------------------------------------ stage 3: monsters
def aip_monster_refs(blob):
    """Monster ids named by the six monster-carrying AI actions in one .aip."""
    ids = set()
    o = 0
    npat = struct.unpack_from("<i", blob, o)[0]; o += 12
    ntitle = struct.unpack_from("<i", blob, o)[0]; o += 4 + ntitle
    for _ in range(npat):
        o += 32
        nev = struct.unpack_from("<i", blob, o)[0]; o += 4
        for _ in range(nev):
            o += 32
            for _ in range(2):
                n = struct.unpack_from("<i", blob, o)[0]; o += 4
                for _ in range(n):
                    sz, typ = struct.unpack_from("<Ii", blob, o)
                    if typ in MONSTER_ACTS:
                        ids.add(struct.unpack_from("<H", blob, o + 8)[0])
                    o += sz
    return ids


def resolve_ai(our_ai, src_ai_za, ai_type, byname):
    """(our FILE_AI row, filename or None). 667's AI type indexes RoseZA's table.

    Same row if it already holds that file or is blank; else the row that holds
    it; else appended. Returns (None, None) for a blank source row.
    """
    if not ai_type or ai_type >= src_ai_za.rows:
        return None, None
    f = src_ai_za.get(ai_type, 0)
    if not f.strip():
        return None, None
    k = kk.key_of(f)
    cur = our_ai.get(ai_type, 0) if ai_type < our_ai.rows else b""
    if cur.strip() and kk.key_of(cur) == k:
        return ai_type, f
    if not cur.strip():
        our_ai.grow_to(ai_type + 1)
        our_ai.set(ai_type, 0, f)
        byname[k] = ai_type
        return ai_type, f
    if k in byname:
        return byname[k], f
    r = our_ai.rows
    our_ai.grow_to(r + 1)
    our_ai.set(r, 0, f)
    byname[k] = r
    return r, f


def point_kind(o):
    """'boss', 'lone', 'camp' or 'empty' for one regen record.

    A boss is one species on a slow tick (BOSS_MIN_INTERVAL); a lone point is one
    species at cap 1 on a fast one -- the Golden Ring's Venomous Hooded Asper
    rares (63 s). Both have the same slot shape as a carpet point (one species,
    count 1 in all seven slots), so the cap and the interval are what tell them
    apart, and a boss folded into a 20-second camp of five would be the worst
    thing this pass could do. Everything else with a roster is camp material.
    """
    basic, tactics = kk.regen_roster_of(o["extra"])
    species = {n for n, c in basic + tactics if n >= 1 and c >= 1}
    if not species:
        return "empty"
    iv, cap, _rng, _tac = kk.regen_get_params(o["extra"])
    if len(species) == 1 and iv >= BOSS_MIN_INTERVAL:
        return "boss"
    if len(species) == 1 and cap <= 1:
        return "lone"
    return "camp"


def cap_bosses(per_file):
    """Hold every boss point at one alive. Returns (per_file, points changed)."""
    out, fixed = {}, 0
    for key, objs in per_file.items():
        out[key] = []
        for o in objs:
            if point_kind(o) == "boss" and kk.regen_get_params(o["extra"])[1] != 1:
                o = dict(o, extra=kk.regen_set_cap(o["extra"], 1))
                fixed += 1
            out[key].append(o)
    return out, fixed


def consolidate_camps(per_file, cell_cm, size):
    """Karkia's consolidate_camps, generalised to mixed-species nests.

    Karkia's carpet points each hold one species, so a bin's roster ranks species
    by how many points carried them. Oro's RoseZA-style nests hold two or more
    at counts up to 7, so here a species weighs the sum of its slot counts over
    the bin. Bosses and lone points pass through verbatim (point_kind). Camps
    are Karkia's: every slot count 1, cap `size`, 20 s, 12 m, tacticPoint 100,
    placed on the authored point nearest the bin centroid. Deterministic, so a
    re-run reproduces it.
    """
    pool, kept = [], {k: [] for k in per_file}
    for key, objs in sorted(per_file.items()):
        for i, o in enumerate(objs):
            if point_kind(o) != "camp":
                kept[key].append(o)
                continue
            basic, tactics = kk.regen_roster_of(o["extra"])
            weight = collections.Counter()
            for n, c in basic + tactics:
                if n >= 1 and c >= 1:
                    weight[n] += c
            x, y, _z = struct.unpack_from("<fff", o["fixed"], kk.REGEN_POS_OFF)
            pool.append((key, i, x, y, weight))
    bins = collections.defaultdict(list)
    for e in pool:
        bins[(int(e[2] // cell_cm), int(e[3] // cell_cm))].append(e)
    camps = 0
    for _cell, members in sorted(bins.items()):
        cx = sum(e[2] for e in members) / len(members)
        cy = sum(e[3] for e in members) / len(members)
        key, i, _x, _y, _w = min(members, key=lambda e: ((e[2] - cx) ** 2 + (e[3] - cy) ** 2, e[0], e[1]))
        weight = collections.Counter()
        for e in members:
            weight.update(e[4])
        ranked = [sp for sp, _n in weight.most_common()]
        basic = ranked[:size]
        while len(basic) < 5:
            basic.append(ranked[0])
        basic = basic[:5]
        tactics = ranked[5:7] if len(ranked) > 5 else [ranked[-1]]
        obj = dict(per_file[key][i])
        e = kk.regen_set_roster(obj["extra"], [(n, 1) for n in basic], [(n, 1) for n in tactics])
        e = kk.regen_set_cap(e, size)
        e = kk.regen_set_interval(e, CAMP_INTERVAL)
        e = kk.regen_set_range(e, CAMP_RANGE)
        obj["extra"] = kk.regen_set_tacticpoint(e, CAMP_TACTIC_POINT)
        kept[key].append(obj)
        camps += 1
    return kept, len(pool), camps


def report_spawn_sanity(final, log=print):
    """Every slot count must stay under its point's cap (the escalation trap),
    and every boss must be held at one alive. Returns the number of problems on
    a camp map or a boss point; elsewhere a hot slot is only reported (Muris
    ships six single-species points with a count of 5 at cap 5 -- the whole nest
    pops at once, which costs it no species)."""
    hot, kinds = [], collections.Counter()
    for key, objs in final.items():
        for o in objs:
            kind = point_kind(o)
            kinds[kind] += 1
            _iv, cap, _rng, tac = kk.regen_get_params(o["extra"])
            if kind == "boss":
                if cap != 1:
                    hot.append((key[0], key[1], "boss cap", cap))
                continue
            if kind == "lone":
                continue                    # cap 1, slot 0 fills it, by design
            b, t = kk.regen_roster_of(o["extra"])
            slots = [(n, c) for n, c in b + t if n >= 1 and c >= 1]
            if any(c >= cap for _n, c in slots) or tac != 100:
                hot.append((key[0], key[1], (cap, tac), slots))
    fatal = [h for h in hot if h[0] in CAMP_ZONES or h[2] == "boss cap"]
    log(f"    {'regen sanity':26s} {sum(kinds.values())} points: {kinds['boss']} boss, "
        f"{kinds['lone']} lone, {kinds['camp']} mixed; {len(fatal)} problems, "
        f"{len(hot) - len(fatal)} hot slots reported outside the camp maps")
    for h in (fatal or hot)[:6]:
        log(f"    {'':26s} {h}")
    return len(fatal)


def read_spawn_source(s667):
    """{(folder, file): (regen objects, lump trailing bytes)} from 667's maps,
    excluded species stripped. The input to plan_spawns and stage 3's roster."""
    regen_src = {}
    for _, folder in ZONES:
        d = os.path.join(P(s667, MAPS_REL), folder)
        for name in sorted(os.listdir(d)):
            if not name.lower().endswith(".ifo"):
                continue
            buf, bounds = oro.read_ifo(os.path.join(d, name))
            off, _end = oro.lump_block(bounds, oro.LUMP_REGEN)
            if off is None or buf[off:off + 4] == b"\0\0\0\0":
                continue
            objs, trailing = oro.read_lump(buf, bounds, oro.LUMP_REGEN)
            objs, _dropped = strip_species(objs, EXCLUDE_MONSTERS)
            regen_src[(folder, name)] = (objs, trailing)
    return regen_src


def plan_spawns(regen_src, log=print):
    """{(folder, file): regen objects} as Oro should ship them: bosses held at
    one alive everywhere, every CAMP_ZONES map merged into camps."""
    per_folder = collections.defaultdict(dict)
    for (folder, name), (objs, _trailing) in regen_src.items():
        per_folder[folder][(folder, name)] = objs
    final = {}
    for folder, per_file in sorted(per_folder.items()):
        per_file, capped = cap_bosses(per_file)
        if capped:
            log(f"    {'boss points':26s} {folder}: {capped} held at one alive")
        spec = CAMP_ZONES.get(folder)
        if spec:
            per_file, absorbed, camps = consolidate_camps(per_file, *spec)
            log(f"    {'spawn camps':26s} {folder}: {absorbed} points -> {camps} camps "
                f"x{spec[1]} = {camps * spec[1]} bodies ({spec[0] // 100} m grid, "
                f"{sum(len(v) for v in per_file.values()) - camps} passed through)")
        final.update(per_file)
    return final


def regen_blob(regen_src, final, key):
    return oro.build_object_lump(final[key], regen_src[key][1])


def strip_species(objs, excluded):
    """Drop excluded ids from every roster; drop points left with no roster."""
    out, dropped = [], 0
    for o in objs:
        basic, tactics = kk.regen_roster_of(o["extra"])
        b2 = [(n, c) for n, c in basic if n not in excluded]
        t2 = [(n, c) for n, c in tactics if n not in excluded]
        if (b2, t2) == (basic, tactics):
            out.append(o)
            continue
        if not [n for n, c in b2 + t2 if n >= 1 and c >= 1]:
            dropped += 1
            continue
        o = dict(o, extra=kk.regen_set_roster(o["extra"], b2 or [(0, 0)], t2 or [(0, 0)]))
        out.append(o)
    return out, dropped


def stage3(ours, s667, sza, idx, snap):
    dry = snap.dry
    print("stage 3 -- monsters")
    src_maps, dst_maps = P(s667, MAPS_REL), P(ours, MAPS_REL)
    S = lambda rel, root=s667: oro.Stb(P(root, rel))
    O = lambda rel: oro.Stb(P(ours, rel))

    # --- 3a. roster from the spawn lumps
    regen_src, spawned = read_spawn_source(s667), set()
    for objs, _trailing in regen_src.values():
        for o in objs:
            spawned.update(oro.regen_mob_ids(o["extra"]))
    spawned -= EXCLUDE_MONSTERS
    src_npc, our_npc = S(r"3DDATA\STB\LIST_NPC.STB"), O(r"3DDATA\STB\LIST_NPC.STB")
    za_npc = S(r"3DDATA\STB\LIST_NPC.STB", sza)
    src_ai_za, our_ai = S(r"3DDATA\STB\FILE_AI.STB", sza), O(r"3DDATA\STB\FILE_AI.STB")
    byname = {kk.key_of(our_ai.get(r, 0)): r for r in range(our_ai.rows) if our_ai.get(r, 0).strip()}
    za_index = kk.index_tree(sza)

    # --- 3b. the summon closure, walked through RoseZA's .aip by AI filename
    def ai_type_of(i):
        if i in AI_DONOR:
            return None
        return num(src_npc, i, oro.NPC_AI_COL) if not row_blank(src_npc, i) else num(our_npc, i, oro.NPC_AI_COL)

    def aip_of(i):
        if i in AI_DONOR:
            f = our_ai.get(AI_DONOR[i], 0)
            return P(ours, f.decode("latin-1")) if f.strip() else None
        t = ai_type_of(i)
        if not row_blank(src_npc, i):
            if not t or t >= src_ai_za.rows or not src_ai_za.get(t, 0).strip():
                return None
            return za_index.get(kk.key_of(src_ai_za.get(t, 0)))
        f = our_ai.get(t, 0) if t and t < our_ai.rows else b""
        return P(ours, f.decode("latin-1")) if f.strip() else None

    roster = set(spawned)
    queue, seen, absent, borrowed = sorted(spawned), set(), set(), set()
    while queue:
        i = queue.pop()
        if i in seen:
            continue
        seen.add(i)
        p = aip_of(i)
        if not p or not os.path.isfile(p):
            continue
        for m in aip_monster_refs(open(p, "rb").read()):
            if m in seen or m in EXCLUDE_MONSTERS:
                continue
            if not row_blank(our_npc, m) and row_blank(src_npc, m):
                borrowed.add(m)
                queue.append(m)          # ours, but its own AI may reach further
            elif not row_blank(src_npc, m):
                roster.add(m)
                queue.append(m)
            elif not row_blank(our_npc, m):
                borrowed.add(m)
                queue.append(m)
            else:
                absent.add(m)
    ids = sorted(roster)
    summon_only = sorted(roster - spawned)
    print(f"    {'roster':26s} {len(ids)} monsters ({len(spawned)} spawned, "
          f"{len(summon_only)} summon-only: {summon_only})")
    print(f"    {'summon closure':26s} {len(borrowed)} already ours "
          f"{sorted(borrowed)[:12]}{'...' if len(borrowed) > 12 else ''}; "
          f"{len(absent)} in no dump {sorted(absent)} (left for audit-ai-monster-refs)")
    for i in ids:
        if i >= our_npc.rows:
            raise SystemExit(f"monster id {i} beyond LIST_NPC.STB ({our_npc.rows})")

    # --- 3c. rows
    eng = stl_english(oro.Stl(P(s667, r"3DDATA\STB\LIST_NPC_S.STL")))
    d_stb = O(r"3DDATA\STB\ITEM_DROP.STB")
    referrers = {num(our_npc, r, 18) for r in range(our_npc.rows) if our_npc.occupied(r)}
    free_tables = (r for r in range(DROP_TABLE_FIRST_FREE, d_stb.rows)
                   if not any(d_stb.get(r, c).strip() for c in range(d_stb.cols))
                   and r not in referrers)
    written, kept, new_tables, ai_rows = 0, [], {}, {}
    for i in ids:
        if not row_blank(our_npc, i):
            kept.append(i)
            continue
        if row_blank(src_npc, i):
            raise SystemExit(f"monster {i} is blank in 667 too")
        for c in range(our_npc.cols):
            our_npc.set(i, c, src_npc.get(i, c) if c < oro.NPC_COPY_COLS else b"")
        key = src_npc.get(i, oro.NPC_STRID_COL).decode("latin-1").strip()
        our_npc.set(i, 0, eng.get(key) or clean_name(src_npc.get(i, 0)))
        our_npc.set(i, oro.NPC_PVP_COL, oro.DEFAULT_PVP_STATE)
        for c in NPC_BLANK_COLS:
            our_npc.set(i, c, b"")
        if za_npc.occupied(i) and za_npc.get(i, 18).strip():
            for c in NPC_DROP_COLS:
                our_npc.set(i, c, za_npc.get(i, c))
        else:
            t = next(free_tables)
            new_tables[i] = t
            our_npc.set(i, 18, str(t))
            our_npc.set(i, 19, b"0")
            our_npc.set(i, 20, NEW_SPECIES_DROP_ITEM)
        if i in AI_DONOR:
            row = AI_DONOR[i]
            if not our_ai.get(row, 0).strip():
                raise SystemExit(f"AI_DONOR {i} -> {row}: our FILE_AI row is blank")
        else:
            row, f = resolve_ai(our_ai, src_ai_za, num(src_npc, i, oro.NPC_AI_COL), byname)
        our_npc.set(i, oro.NPC_AI_COL, str(row) if row is not None else b"")
        ai_rows[i] = row
        written += 1
    print(f"    {'LIST_NPC.STB':26s} {written} rows written, {len(kept)} already ours "
          f"{kept if kept else ''}; {len(new_tables)} new species given drop tables "
          f"{sorted(new_tables.values())[:4]}..")
    print(f"    {'AI donors':26s} {AI_DONOR}")

    # --- 3d. names
    our_stl = oro.Stl(P(ours, r"3DDATA\STB\LIST_NPC_S.STL"))
    added = renamed = 0
    for i in ids:
        key = our_npc.get(i, oro.NPC_STRID_COL).decode("latin-1").strip()
        if not key:
            continue
        text = our_npc.get(i, 0).decode("latin-1")
        if our_stl.has(key):
            if our_stl.name(key, 0) != text:
                our_stl.set_name(key, text)
                renamed += 1
        else:
            our_stl.append(key, i, text)
            added += 1
    print(f"    {'LIST_NPC_S.STL':26s} +{added} keys, {renamed} renamed (now {len(our_stl.keys)})")

    # --- 3e. AI files
    aips = {our_ai.get(r, 0).decode("latin-1") for r in set(ai_rows.values()) if r is not None}
    kk.copy_new(aips, idx, ours, dry, ".aip files")
    print(f"    {'FILE_AI.STB':26s} now {our_ai.rows} rows")
    fixed = fix_attack_speed(our_npc, za_npc, ids)
    print(f"    {'attack speed':26s} {len(fixed)} rows re-based on RoseZA/donor units "
          f"{fixed[:4]}{'...' if len(fixed) > 4 else ''}")
    types, hits = fix_presentation(our_npc, ids)
    print(f"    {'target mark / hand hit':26s} {len(types)} type codes folded {types[:5]}"
          f"{'...' if len(types) > 5 else ''}; hand-hit effect set on {hits}")

    write(snap, r"3DDATA\STB\LIST_NPC.STB", our_npc.to_bytes())
    write(snap, r"3DDATA\STB\FILE_AI.STB", our_ai.to_bytes())
    write(snap, r"3DDATA\STB\LIST_NPC_S.STL", our_stl.to_bytes())

    # --- 3f. models. import_characters copies from ONE source; the two motions
    # 667 lacks are fetched from RoseZA afterwards by the same paths.
    snap.keep(r"3DDATA\NPC\LIST_NPC.CHR")
    snap.keep(r"3DDATA\NPC\PART_NPC.ZSC")
    oro.import_characters(ids, ours, s667, dry, "monster")
    fill_missing_motions(ours, ids, s667, idx, dry)

    # --- 3g. weapon presentation (same check as import-oro stage 3f)
    src_wpn, our_wpn = S(r"3DDATA\STB\LIST_WEAPON.STB"), O(r"3DDATA\STB\LIST_WEAPON.STB")
    za_wpn = S(r"3DDATA\STB\LIST_WEAPON.STB", sza)
    weapons = set()
    for i in ids:
        for col in (oro.NPC_R_WEAPON_COL, oro.NPC_L_WEAPON_COL):
            v = num(our_npc, i, col)
            if v:
                weapons.add(v)
    wfixed = []
    for w in sorted(weapons):
        if w >= our_wpn.rows:
            raise SystemExit(f"monster weapon row {w} beyond LIST_WEAPON.STB")
        if any(our_wpn.get(w, c).strip() for c in oro.WEAPON_PRESENTATION_COLS):
            continue
        for donor in (za_wpn, src_wpn):
            got = [(c, donor.get(w, c)) for c in oro.WEAPON_PRESENTATION_COLS if donor.get(w, c).strip()]
            if got:
                break
        if not got:
            print(f"    !! weapon {w} has no presentation data in any source")
            continue
        for c, v in got:
            our_wpn.set(w, c, v)
        if not our_wpn.get(w, 0).strip():
            our_wpn.set(w, 0, donor.get(w, 0))
        wfixed.append(w)
    print(f"    {'LIST_WEAPON.STB':26s} {len(wfixed)} of {len(weapons)} monster weapons repaired {wfixed}")
    if wfixed:
        write(snap, r"3DDATA\STB\LIST_WEAPON.STB", our_wpn.to_bytes())

    # --- 3h. every skill the AI casts must animate on the model
    if not dry:
        chr_ = oro.Chr(P(ours, r"3DDATA\NPC\LIST_NPC.CHR"))
        acts = kk.aip_skill_motions(ours, our_npc, our_ai, ids)
        silent = kk.chr_anim_audit(chr_, acts)
        # Fatal only where the pairing is OURS (the donor AIs). A 667 monster
        # whose own AI casts on slot 2 or 7 (the Devourers, Flying Ball) is how
        # RoseZA shipped and played it; report, do not refuse.
        mine = [x for x in silent if x[0] in AI_DONOR]
        if mine:
            raise SystemExit(f"donor AI casts do not animate on the target: {mine}")
        print(f"    {'skill animations':26s} {sum(len(v) for v in acts.values())} casts "
              f"across {len(acts)} monsters; donors clean, "
              f"{len(silent)} pre-existing oddities: {[(n, s, m) for n, s, m, _ in silent]}")

    # --- 3i. spawn lumps: camps everywhere but Muris, bosses held at one alive
    final = plan_spawns(regen_src)
    report_spawn_sanity(final)
    files = points = 0
    for (folder, name), objs in sorted(final.items()):
        dp = os.path.join(dst_maps, folder, name)
        blob = oro.build_object_lump(objs, regen_src[(folder, name)][1])
        if not os.path.isfile(dp):
            if dry:
                files += 1
                points += len(objs)
                continue
            raise SystemExit(f"{dp}: run --stage 1 first")
        dbuf, dbounds = oro.read_ifo(dp)
        doff, dend = oro.lump_block(dbounds, oro.LUMP_REGEN)
        if dbuf[doff:dend] == blob:
            continue
        files += 1
        points += len(objs)
        if not dry:
            with open(dp, "wb") as fh:
                fh.write(oro.build_ifo(dbounds, dbuf, {oro.LUMP_REGEN: blob}))
    print(f"    {'IFO regen lumps':26s} {points} spawn points into {files} files")


def fill_missing_motions(ours, ids, s667, idx, dry):
    """Every skeleton/motion/effect the SOURCE CHR names for `ids`, through the
    667 -> RoseZA chain. import_characters copies from 667 alone and reports the
    two motions it lacks as missing; this pass finds them in RoseZA. Read from
    the source CHR rather than ours so a dry run sees the same set."""
    chr_ = oro.Chr(P(s667, r"3DDATA\NPC\LIST_NPC.CHR"))
    want = set()
    for i in ids:
        c = chr_.chars[i] if i < len(chr_.chars) else None
        if not c:
            continue
        if c["skel"] < len(chr_.skeletons):
            want.add(chr_.skeletons[c["skel"]].decode("latin-1"))
        for _t, a in c["anims"]:
            if a < len(chr_.motions):
                want.add(chr_.motions[a].decode("latin-1"))
        for _t, e in c["effects"]:
            if e < len(chr_.effects):
                want.add(chr_.effects[e].decode("latin-1"))
    want = {w for w in want if "\\" in w or "/" in w}
    kk.copy_new(want | kk.effect_chain({w for w in want if w.lower().endswith(".eft")}, idx),
                idx, ours, dry, "motions/effects (chain)")


# ---------------------------------------------------------- stage 4: NPCs
def stage4(ours, s667, sza, idx, snap):
    dry = snap.dry
    print("stage 4 -- NPCs, placements, dialog registrations")
    src_maps, dst_maps = P(s667, MAPS_REL), P(ours, MAPS_REL)
    S = lambda rel, root=s667: oro.Stb(P(root, rel))
    O = lambda rel: oro.Stb(P(ours, rel))

    placements, cons = {}, {}
    for _, folder in ZONES:
        d = os.path.join(src_maps, folder)
        for name in sorted(os.listdir(d)):
            if not name.lower().endswith(".ifo"):
                continue
            buf, bounds = oro.read_ifo(os.path.join(d, name))
            objs, trailing = oro.read_lump(buf, bounds, oro.LUMP_MOB)
            if not objs:
                continue
            keep = [o for o in objs if o["obj_id"] not in DROP_NPC_PLACEMENTS]
            for o in keep:
                cons[o["obj_id"]] = kk.mob_con_name(o["extra"])
            placements[(folder, name)] = (keep, trailing)
    ids = sorted({o["obj_id"] for objs, _t in placements.values() for o in objs})
    print(f"    {'placements':26s} {sum(len(o) for o, _t in placements.values())} across "
          f"{len(placements)} files, {len(ids)} NPCs (dropped {sorted(DROP_NPC_PLACEMENTS)})")

    src_npc, our_npc = S(r"3DDATA\STB\LIST_NPC.STB"), O(r"3DDATA\STB\LIST_NPC.STB")
    src_ai_za, our_ai = S(r"3DDATA\STB\FILE_AI.STB", sza), O(r"3DDATA\STB\FILE_AI.STB")
    byname = {kk.key_of(our_ai.get(r, 0)): r for r in range(our_ai.rows) if our_ai.get(r, 0).strip()}
    our_sell = O(r"3DDATA\STB\LIST_SELL.STB")
    eng = stl_english(oro.Stl(P(s667, r"3DDATA\STB\LIST_NPC_S.STL")))
    written, kept, dropped_tabs = 0, [], []
    for i in ids:
        if our_npc.occupied(i):
            kept.append(i)
            continue
        if not src_npc.occupied(i):
            raise SystemExit(f"NPC {i} placed but not in 667 LIST_NPC")
        for c in range(oro.NPC_COPY_COLS):
            our_npc.set(i, c, src_npc.get(i, c))
        key = src_npc.get(i, oro.NPC_STRID_COL).decode("latin-1").strip()
        our_npc.set(i, 0, eng.get(key) or clean_name(src_npc.get(i, 0)))
        is_npc = our_npc.get(i, oro.NPC_TYPE_COL) == oro.NPC_TYPE_NPC
        our_npc.set(i, oro.NPC_PVP_COL, oro.NPC_PVP_STATE if is_npc else oro.DEFAULT_PVP_STATE)
        for c in NPC_BLANK_COLS:
            our_npc.set(i, c, b"")
        row, _f = resolve_ai(our_ai, src_ai_za, num(src_npc, i, oro.NPC_AI_COL), byname)
        our_npc.set(i, oro.NPC_AI_COL, str(row) if row is not None else b"")
        for c in oro.NPC_SELL_TAB_COLS:
            t = num(our_npc, i, c)
            if t and (t >= our_sell.rows or not our_sell.occupied(t)):
                dropped_tabs.append((i, t))
                our_npc.set(i, c, b"")
        written += 1
    print(f"    {'LIST_NPC.STB':26s} {written} rows written, {len(kept)} already ours")
    print(f"    {'shop tabs dropped':26s} {len(dropped_tabs)} (no stock in our LIST_SELL) "
          f"-- stock-muris-shops.py re-points Huzam/Azim")
    za_npc = S(r"3DDATA\STB\LIST_NPC.STB", sza)
    fixed = fix_attack_speed(our_npc, za_npc, ids)
    print(f"    {'attack speed':26s} {len(fixed)} NPC rows re-based")
    # The NPCs' idle .aip files (RoseZA's NPC_xxxx.aip). Stage 3 copies the
    # monsters'; an earlier version of this stage resolved the rows and never
    # copied the files, and the client greets a missing one with a modal box.
    npc_aips = set()
    for i in ids:
        a = num(our_npc, i, oro.NPC_AI_COL)
        if a and a < our_ai.rows and our_ai.get(a, 0).strip():
            npc_aips.add(our_ai.get(a, 0).decode("latin-1"))
    kk.copy_new(npc_aips, idx, ours, dry, "NPC .aip files")

    our_stl = oro.Stl(P(ours, r"3DDATA\STB\LIST_NPC_S.STL"))
    added = renamed = 0
    for i in ids:
        key = our_npc.get(i, oro.NPC_STRID_COL).decode("latin-1").strip()
        if not key:
            continue
        text = our_npc.get(i, 0).decode("latin-1")
        if our_stl.has(key):
            if our_stl.name(key, 0) != text:
                our_stl.set_name(key, text)
                renamed += 1
        else:
            our_stl.append(key, i, text)
            added += 1
    print(f"    {'LIST_NPC_S.STL':26s} +{added} keys, {renamed} renamed")

    # --- dialogs: our LIST_EVENT already holds the 43 RoseZA-era rows; add what
    # is missing from RoseZA (EM71-034), then normalise every placement's ref
    # to the exact basename our row holds (zonefile.cpp compares full basenames).
    src_ev, our_ev = S(r"3DDATA\STB\LIST_EVENT.STB", sza), O(r"3DDATA\STB\LIST_EVENT.STB")
    have = {kk.stem(our_ev.get(r, oro.EVENT_FILE_COL)): r for r in range(our_ev.rows)
            if our_ev.get(r, oro.EVENT_FILE_COL).strip()}
    za_by_stem = {kk.stem(src_ev.get(r, oro.EVENT_FILE_COL)): r for r in range(src_ev.rows)
                  if src_ev.get(r, oro.EVENT_FILE_COL).strip()}
    free = (r for r in range(1, our_ev.rows) if not any(our_ev.get(r, c).strip() for c in range(our_ev.cols)))
    ev_rows, new_cons, ev_written, mute = {}, set(), [], []
    for i in ids:
        st = kk.stem(cons.get(i, ""))
        st = CON_ALIAS.get(st, st)
        if not st or st == "EMPTY":
            continue
        if st in have:
            ev_rows[i] = have[st]
            continue
        s = za_by_stem.get(st)
        if s is None:
            mute.append((i, st))
            continue
        r = next(free)
        for c in range(min(our_ev.cols, src_ev.cols)):
            our_ev.set(r, c, src_ev.get(s, c))
        have[st] = ev_rows[i] = r
        ev_written.append(r)
        new_cons.add(src_ev.get(s, oro.EVENT_FILE_COL).decode("latin-1").strip())
    print(f"    {'LIST_EVENT.STB':26s} +{len(ev_written)} rows {ev_written} for {sorted(new_cons)}")
    for i, st in mute:
        print(f"    {'':26s} NPC {i} {our_npc.get(i, 0).decode('latin-1')!r}: dialog {st} exists "
              f"in no readable dump -- placed mute")
    kk.copy_new(new_cons, idx, ours, dry, ".CON dialogs")

    src_ltb = oro.Ltb(P(sza, oro.CON_LTB_REL))
    our_ltb = oro.Ltb(P(ours, oro.CON_LTB_REL))
    need = set()
    for con in new_cons:
        p = idx.get(kk.key_of(con))
        if p:
            need |= set(oro.con_str_ids(p))
    need = sorted(i for i in need if i < len(src_ltb.rows))
    grew = our_ltb.grow_to(max(need) + 1) if need else 0
    filled = 0
    for i in need:
        if any(our_ltb.rows[i]):
            continue
        for c in range(min(our_ltb.cols, src_ltb.cols)):
            our_ltb.rows[i][c] = src_ltb.rows[i][c]
        filled += 1
    print(f"    {'ulngtb_con.ltb':26s} +{grew} rows, {filled} of {len(need)} string ids filled")
    if grew or filled:
        write(snap, oro.CON_LTB_REL, our_ltb.to_bytes())

    write(snap, r"3DDATA\STB\LIST_NPC.STB", our_npc.to_bytes())
    write(snap, r"3DDATA\STB\FILE_AI.STB", our_ai.to_bytes())
    write(snap, r"3DDATA\STB\LIST_NPC_S.STL", our_stl.to_bytes())
    write(snap, r"3DDATA\STB\LIST_EVENT.STB", our_ev.to_bytes())

    snap.keep(r"3DDATA\NPC\LIST_NPC.CHR")
    snap.keep(r"3DDATA\NPC\PART_NPC.ZSC")
    oro.import_characters(ids, ours, s667, dry, "NPC")
    fill_missing_motions(ours, ids, s667, idx, dry)

    canon = {i: os.path.basename(our_ev.get(r, oro.EVENT_FILE_COL).decode("latin-1").replace("\\", "/"))
             for i, r in ev_rows.items()}
    fixed_refs = 0
    for objs, _t in placements.values():
        for o in objs:
            want = canon.get(o["obj_id"])
            if not want or kk.mob_con_name(o["extra"]) == want:
                continue
            o["extra"] = o["extra"][:4] + oro.put_bstr(want.encode("latin-1"))
            fixed_refs += 1
    print(f"    {'.CON references':26s} {fixed_refs} normalised to the LIST_EVENT basename")

    files = n = 0
    for (folder, name), (objs, trailing) in sorted(placements.items()):
        dp = os.path.join(dst_maps, folder, name)
        blob = oro.build_object_lump(objs, trailing)
        if not os.path.isfile(dp):
            if dry:
                files += 1
                n += len(objs)
                continue
            raise SystemExit(f"{dp}: run --stage 1 first")
        dbuf, dbounds = oro.read_ifo(dp)
        doff, dend = oro.lump_block(dbounds, oro.LUMP_MOB)
        if dbuf[doff:dend] == blob:
            continue
        files += 1
        n += len(objs)
        if not dry:
            with open(dp, "wb") as fh:
                fh.write(oro.build_ifo(dbounds, dbuf, {oro.LUMP_MOB: blob}))
    print(f"    {'IFO mob lumps':26s} {n} NPCs into {files} files")


# --------------------------------------------------------------- verify
def verify(ours, sza):
    print("verify -- reading back data/")
    bad = 0
    dst_maps = P(ours, MAPS_REL)
    zstb = oro.Stb(P(ours, r"3DDATA\STB\LIST_ZONE.STB"))
    zstl = oro.Stl(P(ours, r"3DDATA\STB\LIST_ZONE_S.STL"))
    zt = oro.Stb(P(ours, ZONETYPE_STB_REL))
    events_of = {}
    for row, folder in ZONES:
        d = os.path.join(dst_maps, folder)
        probs = []
        if not os.path.isdir(d):
            probs.append("folder missing")
        else:
            zon = [f for f in os.listdir(d) if f.lower().endswith(".zon")]
            if not zon:
                probs.append("no .ZON")
            else:
                p = os.path.join(d, zon[0])
                buf, bounds = kk.zon_lumps(p)
                eo, ee = oro.lump_block(bounds, kk.ZON_LUMP_ECONOMY)
                if eo is None:
                    probs.append("no economy lump")
                else:
                    kk.parse_economy(buf[eo:ee], p)
                zty = oro.zon_zone_type(p)
                if not zt.occupied(zty):
                    probs.append(f"ZoneType {zty} unknown")
                events_of[row] = {n.decode("latin-1") for n, _ in oro.zon_events(p)}
                for col in (ZONE_START_COL, ZONE_REVIVE_COL):
                    if zstb.get(row, col).decode("latin-1") not in events_of[row]:
                        probs.append(f"col {col} event {zstb.get(row, col)!r} missing")
        if not zstb.occupied(row):
            probs.append("LIST_ZONE row blank")
        key = zstb.get(row, ZONE_STL_COL).decode("latin-1")
        if not zstl.has(key):
            probs.append(f"no STL key {key}")
        counts = {}
        if os.path.isdir(d):
            for f in os.listdir(d):
                if not f.lower().endswith(".ifo"):
                    continue
                b2, bd2 = oro.read_ifo(os.path.join(d, f))
                for lt, lab in ((oro.LUMP_WARP, "gates"), (oro.LUMP_REGEN, "spawns"), (oro.LUMP_MOB, "npcs")):
                    off, _ = oro.lump_block(bd2, lt)
                    if off is not None:
                        counts[lab] = counts.get(lab, 0) + struct.unpack_from("<i", b2, off)[0]
        movs = sum(1 for f in os.listdir(d) if f.lower().endswith(".mov")) if os.path.isdir(d) else 0
        if probs:
            bad += 1
        print(f"    zone {row:3d} {folder:10s} {zstb.get(row, 0).decode('latin-1'):26s} "
              f"{key} mov={movs:2d} {counts}{'   <-- ' + '; '.join(probs) if probs else ''}")

    warp = oro.Stb(P(ours, r"3DDATA\STB\WARP.STB"))
    placed = {}
    for row, folder in ZONES:
        d = os.path.join(dst_maps, folder)
        if not os.path.isdir(d):
            continue
        for f in os.listdir(d):
            if not f.lower().endswith(".ifo"):
                continue
            b2, bd2 = oro.read_ifo(os.path.join(d, f))
            objs, _ = oro.read_lump(b2, bd2, oro.LUMP_WARP)
            for o in objs or []:
                placed.setdefault(o["warp_id"], set()).add(folder)
    for wid in sorted(placed):
        dest, ev = num(warp, wid, 1), warp.get(wid, 2).decode("latin-1")
        ok = dest in events_of and ev in events_of[dest]
        if not ok:
            bad += 1
        print(f"    gate {wid:3d} from {','.join(sorted(placed[wid])):10s} -> zone {dest:3d} "
              f"{ev:26s} {'ok' if ok else '<-- UNRESOLVED'}")
    for wid in (178, 179, 180):
        if not warp.get(wid, 0).startswith(b"K"):
            bad += 1
            print(f"    !! Karkia gate {wid} overwritten: {warp.get(wid, 0)}")

    npc = oro.Stb(P(ours, r"3DDATA\STB\LIST_NPC.STB"))
    nstl = oro.Stl(P(ours, r"3DDATA\STB\LIST_NPC_S.STL"))
    ai = oro.Stb(P(ours, r"3DDATA\STB\FILE_AI.STB"))
    chr_ = oro.Chr(P(ours, r"3DDATA\NPC\LIST_NPC.CHR"))
    spawned, placed_npcs, con_refs = set(), set(), {}
    for row, folder in ZONES:
        d = os.path.join(dst_maps, folder)
        if not os.path.isdir(d):
            continue
        for f in os.listdir(d):
            if not f.lower().endswith(".ifo"):
                continue
            b2, bd2 = oro.read_ifo(os.path.join(d, f))
            objs, _ = oro.read_lump(b2, bd2, oro.LUMP_REGEN)
            for o in objs or []:
                spawned.update(oro.regen_mob_ids(o["extra"]))
            objs, _ = oro.read_lump(b2, bd2, oro.LUMP_MOB)
            for o in objs or []:
                placed_npcs.add(o["obj_id"])
                con_refs[o["obj_id"]] = kk.mob_con_name(o["extra"])
    if not spawned:
        print("    spawns: none (stage 3 not run)")
    else:
        # closure through OUR .aip files now
        seen, queue, absent = set(), sorted(spawned), set()
        while queue:
            i = queue.pop()
            if i in seen:
                continue
            seen.add(i)
            a = num(npc, i, oro.NPC_AI_COL)
            f = ai.get(a, 0).decode("latin-1").strip() if a and a < ai.rows else ""
            p = kk.dest_of(ours, f) if f else ""
            if f and not os.path.isfile(p):
                bad += 1
                print(f"    !! monster {i}: .aip missing {f}")
                continue
            if not f:
                continue
            for m in aip_monster_refs(open(p, "rb").read()):
                if npc.occupied(m):
                    queue.append(m)
                else:
                    absent.add((i, m))
        blank = [i for i in spawned if not npc.occupied(i)]
        nochr = [i for i in seen if i < len(chr_.chars) and chr_.chars[i] is None and npc.get(i, 1).strip()]
        nokey = [i for i in seen if npc.get(i, oro.NPC_STRID_COL).strip()
                 and not nstl.has(npc.get(i, oro.NPC_STRID_COL).decode("latin-1").strip())]
        for label, items in (("spawned but blank", blank), ("no CHR entry", nochr), ("no STL key", nokey)):
            if items:
                bad += 1
                print(f"    !! {label}: {items[:8]}")
        lv = sorted(num(npc, i, 7) for i in spawned)
        print(f"    monsters: {len(spawned)} spawned, closure {len(seen)} rows, levels {lv[0]}-{lv[-1]}")
        if absent:
            print(f"    summon refs to rows no dump has: {sorted(absent)} "
                  f"(run audit-ai-monster-refs.py)")
        acts = kk.aip_skill_motions(ours, npc, ai, sorted(seen))
        silent = kk.chr_anim_audit(chr_, acts)
        mine = [x for x in silent if x[0] in AI_DONOR]
        if mine:
            bad += 1
            print(f"    !! donor AI casts do not animate: {mine}")
        print(f"    skill animations: {sum(len(v) for v in acts.values())} casts; donors clean, "
              f"{len(silent)} pre-existing oddities {[(n, s, m) for n, s, m, _ in silent][:6]}")
    if placed_npcs:
        ev = oro.Stb(P(ours, r"3DDATA\STB\LIST_EVENT.STB"))
        names = {os.path.basename(ev.get(r, oro.EVENT_FILE_COL).decode("latin-1").replace("\\", "/")).lower()
                 for r in range(ev.rows) if ev.get(r, oro.EVENT_FILE_COL).strip()}
        unres = [(i, c) for i, c in con_refs.items() if c and c.lower() != "empty" and c.lower() not in names]
        nofile = [(i, c) for i, c in con_refs.items()
                  if c and c.lower() != "empty" and not os.path.isfile(P(ours, f"3DDATA\\EVENT\\{c}"))]
        print(f"    NPCs: {len(placed_npcs)} placed, {len(unres)} dialog refs not in LIST_EVENT "
              f"{unres[:4]}, {len(nofile)} .CON files absent {nofile[:4]}")
        missing = [i for i in placed_npcs if not npc.occupied(i)]
        if missing:
            bad += 1
            print(f"    !! placed NPCs with no row: {missing}")
    print(f"\n    {'ALL CHECKS PASSED' if not bad else f'{bad} PROBLEM(S)'}")
    return bad == 0


# --------------------------------------------------------------- revert
def revert(root, ours, dry):
    mpath = os.path.join(root, BUILD_DIR, MANIFEST)
    if not os.path.exists(mpath):
        sys.exit("no import manifest -- nothing to revert")
    man = json.load(open(mpath, encoding="utf-8"))
    pre = os.path.join(root, BUILD_DIR, PRE_DIR)
    n = 0
    for rel in man["created"]:
        p = os.path.join(ours, rel)
        if os.path.isfile(p):
            n += 1
            if not dry:
                os.remove(p)
    for rel in man["overwritten"]:
        s = os.path.join(pre, rel)
        if os.path.isfile(s) and not dry:
            shutil.copyfile(s, os.path.join(ours, rel))
    print(f"  {n} created files removed, {len(man['overwritten'])} tables restored")
    if not dry:
        os.remove(mpath)
        shutil.rmtree(pre, ignore_errors=True)
        for _, folder in ZONES:
            d = os.path.join(P(ours, MAPS_REL), folder)
            for base, dirs, files in os.walk(d, topdown=False):
                if not files and not dirs and base != d:
                    os.rmdir(base)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--stage", type=int, choices=(1, 2, 3, 4), action="append")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--revert", action="store_true")
    ap.add_argument("--root", default=os.path.dirname(HERE))
    ap.add_argument("--source", default=SRC_667)
    ap.add_argument("--fallback", default=SRC_ZA)
    a = ap.parse_args()
    ours = os.path.join(a.root, "data")
    s667 = os.path.join(a.source, "3DDATA") if os.path.isdir(os.path.join(a.source, "3DDATA")) else a.source
    s667 = os.path.dirname(s667) if os.path.basename(s667).upper() == "3DDATA" else s667
    sza = a.fallback
    if a.verify:
        return 0 if verify(ours, sza) else 1
    if a.revert:
        revert(a.root, ours, a.dry_run)
        return 0
    for d in (ours, s667, sza):
        if not os.path.isdir(d):
            raise SystemExit(f"not found: {d}")
    print(f"source: {s667}\nfallback: {sza}\ntarget: {os.path.abspath(ours)}\n")
    print("indexing both source trees...")
    idx = kk.index_tree(sza)
    idx.update(kk.index_tree(s667))        # 667 wins where both have a file
    print(f"    {len(idx)} files\n")
    print("self-test:")
    if not selftest(ours, s667, sza, idx):
        raise SystemExit("self-test FAILED -- refusing to write")
    if a.selftest:
        return 0
    assert_removed(a.root, ours)
    snap = Snapshot(a.root, ours, a.dry_run)
    files_before = tree_files(ours) if not a.dry_run else set()
    print()
    for s in sorted(set(a.stage or (1, 2, 3, 4))):
        {1: stage1, 2: stage2, 3: stage3, 4: stage4}[s](ours, s667, sza, idx, snap)
        print()
    if not a.dry_run:
        snap.created(sorted(tree_files(ours) - files_before))
    print("dry run: nothing written" if a.dry_run else
          f"done. undo: --revert (manifest in {os.path.join(a.root, BUILD_DIR)}). Next: --verify.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
