#!/usr/bin/env python3
"""The Karkia catacombs, milestone 1: the two crypt zones and the keeper.

Design and decisions: doc/karkia-catacombs-implementation-brief.md. The server side is
src/sho_gameserver/src/catacombs.*; **the constants here and there must agree** (zone
numbers, the keeper's row, the trigger's name, the hall's position, the toll).

    python scripts/import-catacombs.py --dry-run
    python scripts/import-catacombs.py
    python scripts/import-catacombs.py --verify
    python scripts/import-catacombs.py --restore     # tables and files back, zone folders removed

What it writes
--------------
* **Two zones**, 90 "Catacombs of Reminiscence" and 91 "Catacombs of Sorrow": the same
  flat 2 x 2-chunk layout (320 m square, the maze's 150 m in the middle), built with
  the map generator's zone builder (`mapgen.zone.build_zone`), each with its own floor
  tile and music. Only one is open on a given day; the controller picks it.
  - LIST_ZONE rows copied from the Tower of Despair (136: underground, Karkia, the
    dungeon sky) and overridden; LIST_ZONE_S keys LZON_KCATA1 / LZON_KCATA2.
  - The ZON's one texture and one tile row follow the Abandoned Church (86), the other
    flat indoor Karkia zone.
  - `start` and `restore` are the middle of the entrance hall, the 2 x 2 cells in the
    maze's south-west corner. The boss room is the 4 x 4 in the north-east corner.
* **The floor tiles** (3DDATA/TERRAIN/TILES/KARKIA/KCATACOMB/): Jrose's catacomb floor
  textures are one 15 m cell each. Reminiscence's carry trims along the sides laid out
  for Jrose's walls, Sorrow's a dark vignette, and ours is terrain whose tile repeats
  every 10 m under walls that move daily. So each tile is a 128 px cut from the
  texture's plain middle, mirrored 2 x 2: seamless by construction.
* **The keepers**, one in front of each of the four Columbariums of the Desolate
  Cemetery (zone 87): [Crypt Keeper] Osric, Maren, Aldous and Edda (LIST_NPC 4020-4023,
  the plague doctor's look). The first stands where Jrose put its Reminiscence entrance,
  the third at its Sorrow entrance. The Cemetery had no NPC at all. Every entrance leads
  to the crypt of the day, and the crypt casts a player out to the entrance used.
  - They share one dialog, written by `quest-editor con-toll`: one option, open only
    while the keeper's event value 0 is 1 (the controller sets all four during an
    opening).
  - The trigger `Catacomb-Enter` only checks that value. **It takes nothing**: the
    controller does the rest in classUSER::Do_QuestTRIGGER's hook, because the toll is
    payable once a day (a database record), the destination changes daily, and a QSD
    would take the hearts before any C++ could refuse. A player who lacks the toll is
    told so by a whisper from the keeper.
* **The walk grids** of 90, 91 and 87 (the Cemetery gained an NPC, a bake input).

* **Milestone 2, the maze itself** (2026-11-10): each crypt's own decoration table
  (LIST_DECO_KCATACOMB1/2.ZSC: a wall, a pillar, two floor medallions, built by
  catacomb_art.py from Jrose's catacomb set -- read its docstring for why the wall is
  our own mesh) and, in the map files, the outer wall, a pillar at every grid corner
  and the medallions. **No interior wall is in the map files** since milestone 3
  (2026-11-10): the game server builds each day's maze and sends its walls as zone
  objects (src/sho_gameserver/src/catacomb_maze.cpp, CCatacombs::BuildMaze), and
  blocks the same cells on the walk grid. `--check-mask` proves those cells are the
  ones the bake blocked when the walls were map objects (milestone 2's grids, kept
  in build/catacombs/m2-reference).

* **The monsters** (2026-11-10, catacomb_monsters.py -- its docstring is the record):
  six rows at levels 235-240 (LIST_NPC 2550-2555, copies of Spire Village's Alpha
  roster), their AI files (the Alpha files with a 30 m leash, 12 m aggro and no
  summons: FILE_AI 2269-2274), and 32 regen points per crypt, in the middle of every
  other cell, none near the hall or the boss room. Their drop table is 912, written
  with the zone rows 90/91 by `add-karkia-drops.py` (run it after this script).

* **The boss** (2026-11-10, catacomb_boss.py -- its docstring is the record): the
  Crypt Necromancer, LIST_NPC 2556, with its two attacks to dodge (LIST_SKILL 7205
  Grave Burst, 7206 Soul Lances), three clips cut from the Necromancer's own and a
  small AI file (FILE_AI 2275). No regen point: the controller spawns it, runs its
  attacks and its phases. Its drop table is 913 (`add-karkia-drops.py`).
* **The boss pool** (2026-11-12, catacomb_pool.py -- its docstring is the record): four
  more bodies from the Jrose dump, LIST_NPC 2557-2560 (Ferdinando, Argentia,
  Gladiomurus, the Ikaness Greater Assassin) with two weapons (LIST_WEAPON 29, 30).
  Needs the Jrose dump on disk for the first run.
* **Argentia's fight** (2026-11-12, catacomb_argentia.py): the skill rows the controller
  hits and animates with (LIST_SKILL 7207-7212) and the tile-eruption effect
  (FILE_EFFECT 2902).
* **Ferdinando's fight** (2026-11-12, catacomb_ferdinando.py -- its docstring is the
  record): SlopRose's iai katana clips retargeted onto his skeleton (CHR slots 2, 6-9,
  12-19), the kit's effects in blood (FILE_EFFECT 2903-2916) and his rows (LIST_SKILL
  7213-7224). Needs the SlopRose overrides and EVO-REFORGED's retargeter on disk.
* **Gladiomurus's fight** (2026-11-13, catacomb_gladiomurus.py -- its docstring is the
  record): her own skill clips and SlopRose's rapier / two-handed clips on her
  skeleton (CHR slots 6-9, 12-17), the kit in gold with the sword wall (FILE_EFFECT
  2917-2925) and her rows (LIST_SKILL 7225-7234). catacomb_kit.py is what the two
  fights share.
* **The Assassin's fight** (2026-11-13, catacomb_assassin.py -- its docstring is the
  record): SlopRose's iai clips on his skeleton (CHR slots 6-9, 12-17), the kit in
  violet (FILE_EFFECT 2926-2937) and his rows (LIST_SKILL 7235-7246).

Deploy: restart the servers (LIST_ZONE, LIST_NPC, the QSD, the IFOs and the grids are
read at start), re-bake the client data (scripts/pack.ps1).
"""
import argparse
import copy
import json
import math
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import rosedata as oro  # noqa: E402
from rosefmt import ifo, lnav, qsd, zon  # noqa: E402
from rosefmt.container import Region  # noqa: E402
from mapgen import lighting  # noqa: E402  (the vendored texconv's path)
from mapgen.zone import EventPoint, Tile, ZoneParams, build_zone  # noqa: E402
import catacomb_art as art  # noqa: E402
import catacomb_maze as maze  # noqa: E402
import catacomb_monsters as mon  # noqa: E402
import catacomb_boss as boss  # noqa: E402
import catacomb_pool as pool  # noqa: E402
import catacomb_argentia as argentia  # noqa: E402
import catacomb_ferdinando as ferdinando  # noqa: E402
import catacomb_gladiomurus as gladiomurus  # noqa: E402
import catacomb_assassin as assassin  # noqa: E402

BUILD_DIR = os.path.join(ROOT, "build", "catacombs")
JROSE = r"C:\Users\Thomas\Desktop\Testclients\Jrose"

ZONE_STB_REL = r"3DDATA\STB\LIST_ZONE.STB"
ZONE_STL_REL = r"3DDATA\STB\LIST_ZONE_S.STL"
NPC_STB_REL = r"3DDATA\STB\LIST_NPC.STB"
NPC_STL_REL = r"3DDATA\STB\LIST_NPC_S.STL"
NPC_CHR_REL = r"3DDATA\NPC\LIST_NPC.CHR"
EVENT_STB_REL = r"3DDATA\STB\LIST_EVENT.STB"
QSD_REL = r"3DDATA\QUESTDATA\QP401.QSD"
LTB_REL = r"3DDATA\EVENT\ulngtb_con.ltb"
CEMETERY_REL = r"3DDATA\MAPS\KARKIA\KCEMETERY"
CHURCH_ZON_REL = r"3DDATA\MAPS\KARKIA\KCHURCH\KCHURCH.ZON"
TILE_DIR_REL = r"3DDATA\TERRAIN\TILES\KARKIA\KCATACOMB"

# ------------------------------------------------------------------ the layout
# catacombs.cpp carries the same numbers.
CHUNK_X0, CHUNK_Y0, CHUNKS = 31, 31, 2          # slots 31..32: the Church's block
CENTRE_CM = (CHUNK_X0 + CHUNKS / 2.0) * 16000   # 512000: both axes
CELL_CM, GRID = 1500, 10                        # 10 x 10 cells of 15 m
MAZE_SW_CM = CENTRE_CM - CELL_CM * GRID / 2.0   # 504500
HALL_CELLS = 2                                  # the hall: cells 0..1 on both axes (catacomb_maze.HALL)
HALL_CM = MAZE_SW_CM + CELL_CM * HALL_CELLS / 2.0   # 506000: start and revive

TEMPLATE_ZONE = 136                             # Tower of Despair
CEMETERY_ZONE = 87
(C_NAME, C_ZON, C_START, C_REVIVE, C_UNDERGROUND, C_BGM_DAY, C_BGM_NIGHT, C_SKY, C_MINIMAP,
 C_MM_X, C_MM_Y) = range(11)
C_DECO, C_CNST = 11, 12
C_PVP, C_JOIN, C_KILL, C_DEAD, C_STL, C_WEATHER = 18, 22, 23, 24, 26, 27
C_OCEAN, C_WATER = 34, 35
C_REVIVE_ZONE, C_REVIVE_X, C_REVIVE_Y = 31, 32, 33

ZONES = (
    # zone, folder, name, STL key, music, floor texture source (Jrose), its plain middle
    dict(zone=90, folder="KCATACOMB1", name="Catacombs of Reminiscence", key="LZON_KCATA1",
         bgm=r"Sound\BGM\Ramesses.ogg", tile="FLOOR_REMINISCENCE.DDS",
         src=r"3Ddata\KARKIA\KCatacomb\FLOOR\FLOOR_C_01.DDS", box=(48, 44, 176, 172)),
    dict(zone=91, folder="KCATACOMB2", name="Catacombs of Sorrow", key="LZON_KCATA2",
         bgm=r"Sound\BGM\KChurch.ogg", tile="FLOOR_SORROW.DDS",
         src=r"3Ddata\KARKIA\KCatacomb2\FLOOR\FLOOR_A_01.DDS", box=(56, 56, 184, 184)),
)

# ------------------------------------------------------------------ the keeper
KEEPER_TEMPLATE = 4143                          # [Plague Doctor] Jenner: row and model
# One keeper per Columbarium of the Cemetery (LIST_DECO_KARKIA 174, with door 186 or the
# open 187): the map has four, and each is an entrance (the user, 2026-11-10). Each
# keeper is his own LIST_NPC row, because the server finds an NPC by its number
# (Get_LocalNPC) and each one's event value 0 is what his dialog reads. Rows 4020-4023
# were blank, in the Karkia NPC band (4016-4146).
#   (row, name, the Columbarium's pivot in world m, its yaw in degrees)
# Jrose's two entrances are the first (Reminiscence, 4806.28 / 4786.83) and the third
# (Sorrow, 5312.03 / 5030.19): both 5.5 m in front of the pivot, which gives the front
# as (sin yaw, -cos yaw).
KEEPERS = (
    (4020, "[Crypt Keeper] Osric", (4800.84, 4786.83), 90),
    (4021, "[Crypt Keeper] Maren", (5365.20, 5258.30), 270),
    (4022, "[Crypt Keeper] Aldous", (5312.00, 5024.70), 180),
    (4023, "[Crypt Keeper] Edda", (5667.70, 4727.10), 270),     # the open door, in the cliff
)
# Where a keeper stands and where the crypt casts players out, in metres in front of
# the pivot and to its left. The first keeper's spot was validated in game.
KEEPER_FRONT, KEEPER_LEFT = 8.66, 3.17
EXIT_FRONT = 11.16
SPOT_STEP, SPOT_REACH = 0.5, 4.0                # a blocked spot moves out along the front
EVENT_ROW = 141                                 # blank; 140 is the Cerberus gatekeeper
KEEPER_CON = "EM87-001.con"
TOLL_KEY = "kcat"
QSD_PATTERN, ENTER_TRIGGER = "Catacombs", "Catacomb-Enter"
OPEN_VALUE = 1                                  # his event value 0 during an opening
OP_EQ = 0
QUEST_EDITOR = os.path.join(ROOT, "bin", "release", "quest-editor.exe")
DIALOG = {
    "--greet": "The dead below do not rest, traveller. The crypt opens on every even hour, "
               "and seals again before the next.",
    "--ask": "Let me into the catacombs.",
    "--bye": "Mind the door.",
    "--offer": "The price is five Black Hearts, once a day. It buys you this opening: leave "
               "and come back as you like until the crypt seals. If you have already paid "
               "for this opening, pass freely.",
    "--lack": "The crypt is sealed.",
    "--accept": "Open the door.",
    "--decline": "Not yet.",
    "--later": "I will come back.",
    "--closed": "The crypt is sealed. It opens again on the next even hour.",
}

TRACKED = [ZONE_STB_REL, ZONE_STL_REL, NPC_STB_REL, NPC_STL_REL, NPC_CHR_REL, EVENT_STB_REL,
           mon.AI_STB_REL, boss.SKILL_STB_REL, boss.SKILL_STL_REL, *pool.TRACKED, *argentia.TRACKED,
           QSD_REL, LTB_REL, CEMETERY_REL + r"\KCemetery.lnav"] + [
    CEMETERY_REL + "\\" + name for name in ("30_35.IFO", "33_32.IFO", "33_33.IFO", "35_35.IFO")]


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


def write_if_changed(path, blob, dry):
    if os.path.isfile(path) and open(path, "rb").read() == blob:
        return False
    if not dry:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as fh:
            fh.write(blob)
    return True


def keep_originals(ours):
    """Snapshot every existing file this script changes, once, before the first write."""
    for rel in TRACKED:
        src = find_ci(ours, rel)
        dst = os.path.join(BUILD_DIR, "orig", rel.replace("\\", "/"))
        if os.path.isfile(src) and not os.path.isfile(dst):
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            shutil.copyfile(src, dst)


def sweep_baks(ours):
    """pack.ps1 refuses to bake while a .bak sits under data/."""
    moved = 0
    for rel in TRACKED + [r"3DDATA\EVENT\x", r"3DDATA\AI\x"]:
        folder = os.path.dirname(find_ci(ours, rel))
        for f in os.listdir(folder) if os.path.isdir(folder) else []:
            if f.lower().endswith(".bak"):
                dest = os.path.join(BUILD_DIR, "bak", os.path.relpath(folder, ours), f)
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                if os.path.isfile(dest):
                    os.remove(os.path.join(folder, f))
                else:
                    shutil.move(os.path.join(folder, f), dest)
                moved += 1
    if moved:
        print(f"    {'.bak files':26s} {moved} moved to build/catacombs/bak")


# ---------------------------------------------------------------------- floor tiles
def tile_blob(z):
    """The zone's floor tile: the source's plain middle, mirrored 2 x 2, DXT1 with a
    full mip chain (the Church's tile is 256 px DXT1, 9 levels)."""
    from PIL import Image, ImageOps
    src = os.path.join(JROSE, z["src"])
    if not os.path.isfile(src):
        raise SystemExit(f"{src} missing: the Jrose dump supplies the floor art")
    im = Image.open(src)
    im.load()
    crop = im.convert("RGB").crop(z["box"])
    w, h = crop.size
    t = Image.new("RGB", (2 * w, 2 * h))
    t.paste(crop, (0, 0))
    t.paste(ImageOps.mirror(crop), (w, 0))
    t.paste(ImageOps.flip(crop), (0, h))
    t.paste(ImageOps.flip(ImageOps.mirror(crop)), (w, h))
    with tempfile.TemporaryDirectory() as tmp:
        t.save(os.path.join(tmp, "tile.png"))
        cmd = [os.path.abspath(lighting.TEXCONV), "-nologo", "-y", "-dx9", "-m", "0", "-nowic",
               "-if", "BOX", "-f", "DXT1", "-o", tmp, os.path.join(tmp, "tile.png")]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit("texconv failed: %s" % (r.stdout or r.stderr)[-400:])
        return open(os.path.join(tmp, "tile.dds"), "rb").read()


def tiles(ours, dry):
    for z in ZONES:
        path = os.path.join(P(ours, TILE_DIR_REL), z["tile"])
        changed = write_if_changed(path, tile_blob(z), dry)
        print(f"    {z['tile']:26s} " + ("written" if changed else "in place"))


# ---------------------------------------------------------------------- the art
def art_stage(ours, dry):
    for z in ZONES:
        files = art.art_files(z["zone"])
        changed = sum(write_if_changed(P(ours, rel), blob, dry) for rel, blob in files.items())
        print(f"    {art.SETS[z['zone']]['zsc']:26s} + {len(files) - 1} models and textures, {changed} written")


# ---------------------------------------------------------------------- the zones
def tile_texture(z):
    return ("3Ddata/TERRAIN/Tiles/Karkia/KCatacomb/" + z["tile"]).encode("latin-1")


class ZonTemplate:
    """What `mapgen.zone.make_zon` copies from a template ZON: the Church's tile row and
    economy, with the crypt's own texture."""

    def __init__(self, church, z):
        self.l = {zon.TEXTURES: [tile_texture(z)], zon.TILES: church.lump(zon.TILES),
                  zon.ECONOMY: church.lump(zon.ECONOMY)}

    def lump(self, t):
        return self.l[t]


NO_WALLS = [False] * maze.EDGES                 # the map files hold no interior wall: the server sends the day's


def zone_files(ours, z):
    church = zon.parse(open(find_ci(ours, CHURCH_ZON_REL), "rb").read())
    if len(church.lump(zon.TILES)) != 1 or len(church.lump(zon.TEXTURES)) != 1:
        raise SystemExit("the Church's ZON no longer has one texture and one tile row")
    p = ZoneParams(folder=z["folder"], x0=CHUNK_X0, y0=CHUNK_Y0, width=CHUNKS, height=CHUNKS,
                   ground_cm=0.0, tile=Tile(brush=0, tile_set=0, tile_index=0, tile_id=0),
                   events=[EventPoint("start", HALL_CM, HALL_CM, 0.0),
                           EventPoint("restore", HALL_CM, HALL_CM, 0.0)],
                   zone_type=1, start_chunk=(CHUNK_X0, CHUNK_Y0),
                   objects=art.records_by_chunk(art.placements(NO_WALLS, MAZE_SW_CM, CELL_CM)),
                   regen=mon.regen_by_chunk(z["zone"], MAZE_SW_CM, CELL_CM))
    return build_zone(p, ZonTemplate(church, z))


def zone_dir_rel(z):
    return r"3DDATA\MAPS\KARKIA" + "\\" + z["folder"]


def zone_row(zstb, z):
    row = [zstb.get(TEMPLATE_ZONE, c) for c in range(zstb.cols)]
    over = {
        C_NAME: z["name"],
        C_ZON: zone_dir_rel(z) + "\\" + z["folder"] + ".ZON",
        C_START: "start", C_REVIVE: "restore", C_UNDERGROUND: "1",
        C_BGM_DAY: z["bgm"], C_BGM_NIGHT: z["bgm"],
        C_MINIMAP: "NOMAP",                      # an empty panel; blank would keep the last zone's
        C_MM_X: str(CHUNK_X0), C_MM_Y: str(64 - (CHUNK_Y0 + CHUNKS - 1)),
        # each crypt's own decoration table (catacomb_art.py); no building in either
        C_DECO: art.zsc_rel(z["zone"]), C_CNST: r"3DDATA\KARKIA\LIST_CNST_KARKIA.ZSC",
        C_PVP: "",                               # PvE until M5
        C_OCEAN: "0", C_WATER: "25",
        C_JOIN: "", C_KILL: "", C_DEAD: "",      # the template's are QSD names: never inherit
        C_STL: z["key"], C_WEATHER: "",
        C_REVIVE_ZONE: "", C_REVIVE_X: "", C_REVIVE_Y: "",
    }
    for c, v in over.items():
        row[c] = v.encode("latin-1")
    return row


def zones(ours, dry):
    zstb = oro.Stb(P(ours, ZONE_STB_REL))
    zstl = oro.Stl(P(ours, ZONE_STL_REL))
    dirty_stb = dirty_stl = False
    for z in ZONES:
        n = z["zone"]
        cur = zstb.get(n, C_NAME).decode("latin-1").strip()
        if cur and cur != z["name"]:
            raise SystemExit(f"LIST_ZONE row {n} is {cur!r}: not free")
        files = zone_files(ours, z)
        zdir = P(ours, zone_dir_rel(z))
        changed = sum(write_if_changed(os.path.join(zdir, rel.replace("/", os.sep)), blob, dry)
                      for rel, blob in files.items())
        print(f"    zone {n} {z['folder']:14s} {len(files)} files, {changed} written")
        row = zone_row(zstb, z)
        if [zstb.get(n, c) for c in range(zstb.cols)] != row:
            for c in range(zstb.cols):
                zstb.set(n, c, row[c])
            dirty_stb = True
            print(f"    {'LIST_ZONE.STB':26s} row {n} {z['name']}")
        if not zstl.has(z["key"]):
            zstl.append(z["key"], n, z["name"])
            dirty_stl = True
            print(f"    {'LIST_ZONE_S.STL':26s} +key {z['key']}")
    if dirty_stb:
        zstb.save(dry, keep_backup=False)
    if dirty_stl:
        zstl.save(dry, keep_backup=False)


# ---------------------------------------------------------------------- the keeper
def keeper_strid(row_no):
    return "LCATNPC%d" % row_no


def keeper_row(n, row_no, name):
    row = list(n.d[KEEPER_TEMPLATE])
    row[0] = name.encode("latin-1")
    row[oro.NPC_STRID_COL] = keeper_strid(row_no).encode()
    for c in oro.NPC_SELL_TAB_COLS:
        row[c] = b""
    return row


def npc_rows(ours, dry):
    n = oro.Stb(P(ours, NPC_STB_REL))
    stl = oro.Stl(P(ours, NPC_STL_REL))
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    dirty = {"stb": False, "stl": False, "chr": False}
    for row_no, name, _pivot, _yaw in KEEPERS:
        cur = n.get(row_no, 0).decode("latin-1").strip()
        if cur and cur != name:
            raise SystemExit(f"LIST_NPC row {row_no} is {cur!r}: not free")
        if row_no < len(chr_.chars) and chr_.chars[row_no] is not None and not cur:
            raise SystemExit(f"LIST_NPC row {row_no} is blank but has a CHR entry: not free")
        if n.d[row_no] != keeper_row(n, row_no, name):
            n.d[row_no] = keeper_row(n, row_no, name)
            dirty["stb"] = True
            print(f"    {'LIST_NPC.STB':26s} row {row_no} {name}")
        if not stl.has(keeper_strid(row_no)):
            stl.append(keeper_strid(row_no), row_no, name)
            dirty["stl"] = True
            print(f"    {'LIST_NPC_S.STL':26s} +key {keeper_strid(row_no)}")
        want = copy.deepcopy(chr_.chars[KEEPER_TEMPLATE])
        if chr_.chars[row_no] != want:
            chr_.chars[row_no] = want
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


def triggers():
    """[(name, conds, rewds)]. COND_013 selects the first keeper (a server-wide lookup by
    NPC number; the controller keeps every keeper's value the same), COND_011 needs his
    event value 0 to be OPEN_VALUE. No reward: the
    controller takes the toll and warps (the docstring says why). Layouts as in
    import-wisdom-escort.py."""
    c_npc = struct.pack("<Iii", 12, 13, KEEPERS[0][0])
    c_open = struct.pack("<IiBxhiB3x", 20, 11, 0, 0, OPEN_VALUE, OP_EQ)
    return [(ENTER_TRIGGER, [c_npc, c_open], [])]


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
        raise SystemExit("the Catacombs trigger did not round-trip")
    write_if_changed(qp, out, dry)
    print(f"    {'QP401.QSD':26s} {ENTER_TRIGGER} " + ("in place" if out == blob else "written"))


def event_row(e):
    return [b"[Crypt Keeper]", e.get(EVENT_ROW - 1, 1), b"NPC",   # every keeper shares the dialog
            rf"3Ddata\Event\{KEEPER_CON}".encode()]


def dialog(ours, dry):
    e = oro.Stb(P(ours, EVENT_STB_REL))
    erow = event_row(e)
    if e.d[EVENT_ROW] != erow:
        if any(x.strip() for x in e.d[EVENT_ROW]) and e.d[EVENT_ROW][3] != erow[3]:   # ours: the label may change
            raise SystemExit(f"LIST_EVENT row {EVENT_ROW} is {e.get(EVENT_ROW, 0)!r}: not free")
        e.d[EVENT_ROW] = erow
        e.save(dry, keep_backup=False)
        print(f"    {'LIST_EVENT.STB':26s} row {EVENT_ROW} -> {KEEPER_CON}")
    if not os.path.isfile(QUEST_EDITOR):
        raise SystemExit(f"{QUEST_EDITOR} missing: build it (cargo build --release -p quest-editor)")
    cmd = [QUEST_EDITOR, "con-toll", ours, KEEPER_CON, TOLL_KEY, ENTER_TRIGGER,
           "--open-when", str(OPEN_VALUE)]
    for k, v in DIALOG.items():
        cmd += [k, v]
    r = subprocess.run(cmd + ([] if dry else ["--write"]), capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"quest-editor con-toll failed:\n{r.stdout}\n{r.stderr}")
    for line in r.stdout.splitlines():
        if line.strip().startswith(("CREATE", "REWRITE", "UNCHANGED", "UPSERT")):
            print(f"    {'dialog':26s} {line.strip()[:110]}")


_nav = {}


def ground(ours, x_m, y_m):
    """(terrain height cm, walkable) under a world point of the Cemetery, from its walk
    grid (vertices 2.5 m apart, row 0 = south; 1 m cells)."""
    if ours not in _nav:
        _nav[ours] = lnav.parse(open(find_ci(ours, CEMETERY_REL + r"\KCemetery.lnav"), "rb").read())
    sx, sy = int(x_m // 160), int(y_m // 160)
    t = _nav[ours].tiles[(sx, sy)]
    h = t.heights
    fx, fy = (x_m - sx * 160) / 2.5, (y_m - sy * 160) / 2.5
    ix, iy = int(fx), int(fy)
    tx, ty = fx - ix, fy - iy
    top = h[iy + 1][ix] * (1 - tx) + h[iy + 1][ix + 1] * tx
    bot = h[iy][ix] * (1 - tx) + h[iy][ix + 1] * tx
    cell = t.simple[int(y_m - sy * 160)][int(x_m - sx * 160)]
    return float(bot * (1 - ty) + top * ty), bool(cell)


def f32(*vals):
    """As the file stores them, so a record read back compares equal."""
    return tuple(struct.unpack("<f", struct.pack("<f", v))[0] for v in vals)


def spot(ours, pivot, yaw, front, left):
    """A walkable point `front` m ahead of a Columbarium and `left` m to its left, moved
    out along the front when the building's (or the cliff's) footprint covers it."""
    th = math.radians(yaw)
    fx, fy = math.sin(th), -math.cos(th)
    lx, ly = math.cos(th), math.sin(th)
    out = 0.0
    while out <= SPOT_REACH:
        x = round(pivot[0] + (front + out) * fx + left * lx, 1)
        y = round(pivot[1] + (front + out) * fy + left * ly, 1)
        if ground(ours, x, y)[1]:
            return x, y
        out += SPOT_STEP
    raise SystemExit(f"no walkable ground in front of the Columbarium at {pivot}")


def keeper_spots(ours):
    """[(row, name, keeper (x, y) m, exit (x, y) m, yaw)]: what catacombs.cpp's KEEPERS
    table carries (`--emit` prints it)."""
    return [(row_no, name, spot(ours, pivot, yaw, KEEPER_FRONT, KEEPER_LEFT),
             spot(ours, pivot, yaw, EXIT_FRONT, 0.0), yaw) for row_no, name, pivot, yaw in KEEPERS]


def keeper_record(ours, row_no, pos, yaw):
    x_cm, y_cm = pos[0] * 100, pos[1] * 100
    sx, sy = int(x_cm // 16000), int(y_cm // 16000)
    z_cm, _ = ground(ours, *pos)
    half = math.radians(yaw) / 2.0                  # he faces the way the door does
    return f"{sx}_{64 - sy}.IFO", ifo.Mob(
        name=b"", warp_id=0, event_id=0, obj_type=3, obj_id=row_no,
        map_x=int((x_cm - sx * 16000) // 250), map_y=63 - int((y_cm - sy * 16000) // 250),
        rot=f32(0.0, 0.0, math.sin(half), math.cos(half)), pos=f32(x_cm - 520000, y_cm - 520000, z_cm),
        scale=(1.0, 1.0, 1.0), ai=0, con_file=KEEPER_CON.encode("latin-1"))


def placement(ours, dry):
    moved = False
    for row_no, name, pos, _exit, yaw in keeper_spots(ours):
        ifo_name, want = keeper_record(ours, row_no, pos, yaw)
        if CEMETERY_REL + "\\" + ifo_name not in TRACKED:
            raise SystemExit(f"{name} lands in {ifo_name}, which TRACKED does not snapshot")
        path = find_ci(ours, CEMETERY_REL + "\\" + ifo_name)
        f = ifo.parse(open(path, "rb").read())
        mobs = f.lumps(ifo.MOB)
        if len(mobs) > 1:
            raise SystemExit(f"{ifo_name}: {len(mobs)} MOB lumps")
        if not mobs:
            # the Cemetery had no NPC anywhere: its chunks have no MOB lump yet
            f.entries.append((ifo.MOB, len(f.regions)))
            f.regions.append(Region(ifo.MOB, []))
            mobs = f.lumps(ifo.MOB)
        mobs[0][:] = [m for m in mobs[0] if m.obj_id != row_no] + [want]
        changed = write_if_changed(path, ifo.build(f), dry)
        moved = moved or changed
        print(f"    {ifo_name:26s} {name} at {pos} m, z {want.pos[2] / 100:.1f} m "
              + ("written" if changed else "in place"))
    return moved


def emit(ours):
    """The KEEPERS table of catacombs.cpp, world cm."""
    for row_no, name, pos, ex, _yaw in keeper_spots(ours):
        print('    {%d, "%s", %.0f.f, %.0f.f, %.0f.f, %.0f.f},' % (
            row_no, name.split("] ")[1], pos[0] * 100, pos[1] * 100, ex[0] * 100, ex[1] * 100))
    return 0


def bake(ours, extra):
    rows = [str(z["zone"]) for z in ZONES] + [str(n) for n in extra]
    r = subprocess.run([sys.executable, os.path.join(HERE, "nav-bake.py"), *rows, "--data", ours],
                       capture_output=True, text=True)
    tail = [ln for ln in (r.stdout + r.stderr).splitlines() if ln.strip()][-12:]
    for ln in tail:
        print("    " + ln[:150])
    if r.returncode != 0:
        raise SystemExit("nav-bake.py failed")


# ---------------------------------------------------------------------- the wall mask
def ground_walk(path):
    """A baked grid's walkable ground over the crypt's two chunks: [y][x] booleans, 1 m
    cells from the south-west chunk's corner."""
    import numpy as np
    nav = lnav.parse(open(path, "rb").read())
    side = CHUNKS * 160
    walk = np.zeros((side, side), dtype=bool)
    for (sx, sy), t in nav.tiles.items():
        w = (t.simple > 0).copy()
        first = 0
        for k, ci in enumerate(np.flatnonzero(t.complex_mask.reshape(-1))):
            n = int(t.cells[k]["n_spans"])
            spans = t.spans[first:first + n]
            first += n
            w.reshape(-1)[ci] = any((int(sp["flags"]) & 7) == 7 for sp in spans)   # WALK | REACHED | TERRAIN
        oy, ox = (sy - CHUNK_Y0) * 160, (sx - CHUNK_X0) * 160
        walk[oy:oy + 160, ox:ox + 160] = w
    return walk


def check_mask(ours):
    """Milestone 3's acceptance: over the maze, today's grid (no interior wall) minus the
    server's wall mask for day 0 must be milestone 2's grid, baked with day 0's walls as
    map objects. One kind of difference is expected and allowed: a cell beside a pillar
    that is open now and was blocked then. In Reminiscence the bake's body test reached
    one cell past the pillar from a wall's end hidden inside it (42 cells on day 0);
    nothing stands there, so open is the truer answer. Sorrow matches cell for cell."""
    import numpy as np
    bad = 0
    off = int((MAZE_SW_CM - CHUNK_X0 * 16000) // 100)
    side = maze.MASK_SIDE
    for z in ZONES:
        ref_path = os.path.join(BUILD_DIR, "m2-reference", z["folder"] + ".lnav")
        if not os.path.isfile(ref_path):
            raise SystemExit(f"{ref_path} is missing: the milestone-2 grid is the reference")
        ref = ground_walk(ref_path)[off:off + side, off:off + side]
        now = ground_walk(os.path.join(P(ours, zone_dir_rel(z)), z["folder"] + ".lnav"))[off:off + side, off:off + side]
        mask = np.array([list(r) for r in maze.block_mask(maze.generate(maze.seed_for(z["zone"], 0)))], dtype=bool)
        got = now & ~mask
        diff = got != ref
        print(f"    zone {z['zone']}: {int(now.sum())} open cells, {int(mask.sum())} under the mask, "
              f"{int(ref.sum())} walkable in the reference; {int(diff.sum())} cells differ "
              f"({int((got & ~ref).sum())} open only now, {int((ref & ~got).sum())} open only then)")
        wrong = []
        for y, x in zip(*np.nonzero(diff)):
            by_pillar = max(abs(x - round(x / 15) * 15), abs(y - round(y / 15) * 15)) <= 3
            if not (got[y, x] and by_pillar):
                wrong.append((int(x), int(y)))
        print(f"        {int(diff.sum()) - len(wrong)} beside a pillar and open now (allowed), {len(wrong)} others")
        for x, y in wrong[:8]:
            print(f"        cell ({x}, {y}): now {'open' if got[y, x] else 'blocked'}")
        bad += len(wrong)
    print("OK" if not bad else f"{bad} cell(s) differ where none may")
    return 0 if not bad else 1


# ---------------------------------------------------------------------- verify / restore
def verify(ours):
    bad = []

    def check(ok, what):
        print(f"    {'ok  ' if ok else 'FAIL'} {what}")
        if not ok:
            bad.append(what)

    zstb = oro.Stb(P(ours, ZONE_STB_REL))
    zstl = oro.Stl(P(ours, ZONE_STL_REL))
    for z in ZONES:
        n = z["zone"]
        check([zstb.get(n, c) for c in range(zstb.cols)] == zone_row(zstb, z), f"LIST_ZONE {n} {z['name']}")
        check(zstl.has(z["key"]), f"LIST_ZONE_S {z['key']}")
        zdir = P(ours, zone_dir_rel(z))
        files = zone_files(ours, z)
        same = all(os.path.isfile(os.path.join(zdir, rel.replace("/", os.sep)))
                   and open(os.path.join(zdir, rel.replace("/", os.sep)), "rb").read() == blob
                   for rel, blob in files.items())
        check(same, f"{z['folder']}: {len(files)} map files match the builder")
        afiles = art.art_files(n)
        check(all(os.path.isfile(P(ours, rel)) and open(P(ours, rel), "rb").read() == blob
                  for rel, blob in afiles.items()), f"{art.SETS[n]['zsc']} and its {len(afiles) - 1} files match the builder")
        tile = os.path.join(P(ours, TILE_DIR_REL), z["tile"])
        check(os.path.isfile(tile), f"floor tile {z['tile']}")
        check(os.path.isfile(os.path.join(zdir, z["folder"] + ".lnav")), f"{z['folder']}.lnav baked")
    n = oro.Stb(P(ours, NPC_STB_REL))
    chr_ = oro.Chr(P(ours, NPC_CHR_REL))
    nstl = oro.Stl(P(ours, NPC_STL_REL))
    for row_no, name, _pivot, _yaw in KEEPERS:
        check(n.d[row_no] == keeper_row(n, row_no, name), f"LIST_NPC {row_no} {name}")
        check(nstl.has(keeper_strid(row_no)), f"LIST_NPC_S {keeper_strid(row_no)}")
        check(chr_.chars[row_no] == chr_.chars[KEEPER_TEMPLATE], f"LIST_NPC.CHR {row_no}")
    mon.verify(ours, check)
    boss.verify(ours, check)
    pool.verify(ours, check)
    argentia.verify(ours, check)
    ferdinando.verify(ours, check)
    gladiomurus.verify(ours, check)
    assassin.verify(ours, check)
    blob = open(P(ours, QSD_REL), "rb").read()
    for name, c, r in triggers():
        check(qsd.trigger_bytes(blob, name) == qsd.build_trigger(name, c, r), f"QP401 {name}")
    e = oro.Stb(P(ours, EVENT_STB_REL))
    check(e.d[EVENT_ROW] == event_row(e), f"LIST_EVENT {EVENT_ROW}")
    check(os.path.isfile(find_ci(ours, r"3DDATA\EVENT" + "\\" + KEEPER_CON)), KEEPER_CON)
    for row_no, name, pos, _exit, yaw in keeper_spots(ours):
        ifo_name, want = keeper_record(ours, row_no, pos, yaw)
        f = ifo.parse(open(find_ci(ours, CEMETERY_REL + "\\" + ifo_name), "rb").read())
        placed = [m for m in (f.lump(ifo.MOB) or []) if m.obj_id == row_no]
        check(placed == [want], f"{ifo_name} places {row_no} {name} once")
    leftovers = []
    for rel in TRACKED + [r"3DDATA\EVENT\x", r"3DDATA\AI\x"]:
        folder = os.path.dirname(find_ci(ours, rel))
        leftovers += [f for f in os.listdir(folder) if f.lower().endswith(".bak")]
    check(not leftovers, "no .bak left beside the files" + (f": {leftovers[:3]}" if leftovers else ""))
    print(f"{'OK' if not bad else f'{len(bad)} problem(s)'}")
    return 0 if not bad else 1


def restore(ours):
    """The tracked files back from build/catacombs/orig, the new files and folders removed.
    Refuses nothing: run it only to undo this script (other scripts' later edits to the
    same tables go with it)."""
    orig = os.path.join(BUILD_DIR, "orig")
    if not os.path.isdir(orig):
        raise SystemExit("nothing to restore: build/catacombs/orig is missing")
    for rel in TRACKED:
        src = os.path.join(orig, rel.replace("\\", "/"))
        if os.path.isfile(src):
            shutil.copyfile(src, find_ci(ours, rel))
            print(f"    restored {rel}")
    for z in ZONES:
        zdir = P(ours, zone_dir_rel(z))
        if os.path.isdir(zdir):
            shutil.rmtree(zdir)
            print(f"    removed {zone_dir_rel(z)}")
    for z in ZONES:
        for rel in [art.zsc_rel(z["zone"])]:
            if os.path.isfile(P(ours, rel)):
                os.remove(P(ours, rel))
                print(f"    removed {rel}")
    adir = P(ours, art.ART_DIR_REL)
    if os.path.isdir(adir):
        shutil.rmtree(adir)
        print(f"    removed {art.ART_DIR_REL}")
    tdir = P(ours, TILE_DIR_REL)
    if os.path.isdir(tdir):
        shutil.rmtree(tdir)
        print(f"    removed {TILE_DIR_REL}")
    mon.remove_files(ours)
    boss.remove_files(ours)
    argentia.remove_files(ours)
    assassin.remove_files(ours)
    gladiomurus.remove_files(ours)
    ferdinando.remove_files(ours)
    con = find_ci(ours, r"3DDATA\EVENT" + "\\" + KEEPER_CON)
    if os.path.isfile(con):
        os.remove(con)
        print(f"    removed {KEEPER_CON}")
    shutil.move(orig, os.path.join(BUILD_DIR, "orig-restored"))
    print("restored; restart the servers and re-bake")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--ours", default=os.path.join(ROOT, "data"))
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--restore", action="store_true")
    ap.add_argument("--no-bake", action="store_true", help="skip the walk grids")
    ap.add_argument("--emit", action="store_true", help="print catacombs.cpp's KEEPERS table")
    ap.add_argument("--check-mask", action="store_true",
                    help="the server's wall mask against milestone 2's baked walls")
    args = ap.parse_args()
    if args.verify:
        return verify(args.ours)
    if args.restore:
        return restore(args.ours)
    if args.emit:
        return emit(args.ours)
    if args.check_mask:
        return check_mask(args.ours)
    dry = args.dry_run
    if not dry:
        keep_originals(args.ours)
    print("the floor tiles")
    tiles(args.ours, dry)
    print("the walls, pillars and medallions")
    art_stage(args.ours, dry)
    print("the monsters")
    mon.ai_files(args.ours, dry)
    mon.npc_rows(args.ours, dry)
    print("the boss")
    boss.apply(args.ours, dry)
    print("the boss pool")
    if pool.apply(args.ours, dry) and not dry:
        pool.finish(args.ours)
    argentia.apply(args.ours, dry)
    ferdinando.apply(args.ours, dry)
    gladiomurus.apply(args.ours, dry)
    assassin.apply(args.ours, dry)
    print("the zones")
    zones(args.ours, dry)
    print("the keepers")
    npc_rows(args.ours, dry)
    trigger(args.ours, dry)
    dialog(args.ours, dry)
    moved = placement(args.ours, dry)
    if not dry:
        sweep_baks(args.ours)
        if not args.no_bake:
            print("the walk grids")
            # the Cemetery only when its IFO changed: an NPC is a bake input
            bake(args.ours, [CEMETERY_ZONE] if moved else [])
    with open(os.path.join(BUILD_DIR, "layout.json") if not dry else os.devnull, "w") as fh:
        json.dump({"centre_cm": CENTRE_CM, "maze_sw_cm": MAZE_SW_CM, "cell_cm": CELL_CM,
                   "grid": GRID, "hall_cm": HALL_CM}, fh, indent=1)
    print("dry run, nothing written" if dry else
          "done -- run add-karkia-drops.py (tables 912 and 913, zone rows 90/91), then --verify")
    return 0


if __name__ == "__main__":
    sys.exit(main())
