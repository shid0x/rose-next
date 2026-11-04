#!/usr/bin/env python3
"""Import maps from China Rose (梦幻骑士, "Dream Knight"): scenery first, spawns later.

    python scripts/import-china-rose.py --map HA --dry-run   preview
    python scripts/import-china-rose.py --map HA             install, then bake the walk grid
    python scripts/import-china-rose.py --verify             re-derive every installed map
    python scripts/import-china-rose.py --selftest           every codec round-trips the source

China Rose is a dead 2009 Chinese ROSE fork: its client C++ source and its installed
data (`C:\\Users\\Thomas\\Desktop\\china rose`, VFS unpacked to `extracted\\`). Its
world is the custom map series tsuki carries part of (its warp points number the
maps M001-M029). Shadow Canyon, the Mysterious Cavern and the Starlight Lounge came
from tsuki's copies (scripts/import-valley002.py); China Rose is the original, so
this script takes maps from it directly. The method, and every trap China Rose
data has: doc/importing-maps.md. One spec per map in MAPS; an install is
idempotent, --dry-run previews, --verify re-derives the whole state, --selftest
proves every reader/writer round-trips the source byte for byte first.

What an install does, for one map
---------------------------------
  1. The map folder, from China Rose's `3DDATA\\MAPS\\<name>` to our planet folder:
     HIM, TIL, ZON, lightmaps and .lit as shipped; every .IFO rewritten through
     rosefmt.ifo (spawn points held back for the monster stage, dead warp
     records dropped, morph objects re-pointed at our rows; any record in a lump
     the spec does not expect stops the install); the minimap halved to our scale
     and its Chinese labels repainted in English. No .MOV: they are retired, the
     walk grid replaced them.
  2. The art: the decoration table (with the spec's collision fixes) and everything it names (meshes, textures,
     effects and their particle chain), the new tile textures, the files of new
     morph rows. Copied only where we have no file of that name (rosedata.copy_files):
     a retail file China Rose re-encoded stays ours.
  3. The tables: LIST_MORPH_OBJECT / LIST_SKY rows (a row identical to one of ours
     is shared, a new one goes to a free row), the LIST_ZONE row, its LIST_ZONE_S name.
  4. The walk grid: scripts/nav-bake.py <zone>, which writes <FOLDER>.lnav beside
     the map (the server's walkability, heights and A*) and build/nav/<FOLDER>.png.

After it: `add-dds-mipmaps.py --subdir <dir>` for the folders it lists (HA: 34 of its
~270 new textures had no chain), and `--subdir <map folder> --lightmaps`; rebake the
VFS; restart the servers (LIST_ZONE, the maps' event points and the grid are read
at startup).

HA -> zone 158, Haian Coast (surveyed 2026-11-04)
-------------------------------------------------
China Rose LIST_ZONE row 27, 海岸场景 "coast scene" (STL key LZON027, no text in any
of its five languages), the series' M027. tsuki carries it as row 158
`ELDEON\\HA` "Haian"; it keeps tsuki's number and folder, as the other series maps did.
  * **A walled cove.** Sea and beach to the north with a lighthouse islet, sandstone
    ruins with pools in the middle, a camp of huts and towers in the west, jungle
    with streams and wooden bridges in the south; cliffs of 40-60 m and waterfalls
    all round (terrain -11 .. 64 m). No way out on foot.
  * **8 x 8 chunks, 28_28 .. 35_35, of which 42 (x 29-34, y 28-34) are the map.**
    The other 22 are a ring of flat ground (x 28 and 35, y 35; the north corners
    carry the cliffs on, up to 40 m) with no object and no lightmap, for the far
    terrain: the client falls back to `3DDATA\\TERRAIN\\default_light.dds` there.
    Kept, it is the original's (tsuki dropped it).
  * 782 decorations (77 of LIST_DECO_Haian's 125 objects), 178 invisible walls
    (lump 11; the client builds every one from LIST_DECO_SPECIAL object 2, whose
    mesh is byte-identical to China Rose's), 78 morph objects (water ripples,
    waterfalls), 780 ocean rectangles. No NPC, building, sound or event object.
  * **Start and revive in the west**, (4888, 5331) m, under the minimap's west star.
    GM way in: `/mm 158 489 533` (10 m units). No gate from our world yet.
  * **Held back: 285 spawn points** (REGEN, chunks 30-34 x 29-34), China Rose's
    LIST_NPC 130-139 -- the monster stage. The walk grid seeds from the ZON's
    event points without them; re-bake once they are in.
  * **Dropped: the east gate's trigger and portal** (34_31). Its WARP record names
    warp row 38, China Rose's "M027 -> M028" to YJ (遗迹场景 "ruins scene", CR zone
    28), a map we do not have; **our row 38 is Anima Lake -> Junon Polis**, so the
    record as shipped would have sent anyone walking into the gate there. Its
    effect `_warp_gate03.eft` (a different file of that name is ours) would show a
    live portal that goes nowhere. The stone platform under them is a decoration
    (LIST_DECO_Haian 51) and stays, and so do the ZON's `WARP-M027-M028` /
    `WARP-M028-M027` points, ready for a gate.
  * **China Rose's copy is the original, tsuki's an editor re-save of it**: heights
    and placements agree to 0.02 cm, the lightmaps byte for byte, but tsuki's drops
    the ring, the spawns and the gate, and writes +-FLT_MAX HIM culling bounds (so
    no terrain patch is ever height-culled: harmless, and true of our canyon, cave
    and lounge too). China Rose's bounds are exact (rosefmt.chunk.compute_him_bounds
    reproduces all 64).
  * **Lightmaps clean**: audit-lightmap-index.py on the source, 779 entries, nothing
    found -- no .lit repair, unlike the canyon.
  * **Bridges**: two wooden bridge models (LIST_DECO_Haian 49 x5, 50 x3; the deck is
    the collision-4 root part, the rails collision 12) placed at 10.6-17.4x, 40-60 m
    long. Each deck is one surface with two hairline joints across it, 0.05-0.1 cm in
    the model = 0.6-1.7 cm in the world: inside the avatar's 15 cm foot sphere, and
    ProbeObjectFloor_Avatar backs a missed ray. Not stitched (the canyon's rope
    bridges had 3-20 cm joints). Walk them in the first test.
  * Zone row: China Rose's cols 0-35 are ours column for column (its IO_STB.h
    ZONE_* macros); its 36-41 (world-map index, PK NPC, navmesh flag, a scale, the
    navmesh path) are dropped. Changed: the name, the ZON and minimap paths (our
    folder), the music, the sky row. **Music**: China Rose's HA has none (nor YJ,
    Yulin, the volcano maps: the series' unfinished end) and tsuki's `BGM_MGHA27.ogg`
    is in no dump. The user's pick (2026-11-04): `BGM_FCSL.ogg`, China Rose's
    翡翠森林 "Emerald Forest" theme (its zone 4, PK011; Ogg Vorbis, 103 s, loops).
    Music is not in data/: the file is copied by hand from China Rose's
    `SOUND\\BGM\\` into the launch folder's `sound\\bgm\\`, like every track of ours.
  * Sky: China Rose's row 0 = our 13 (the Starlight Lounge's, from tsuki), cell for
    cell; the two `MAPOBJ\\Sky\\day01/night01.dds` differ only outside the dome cap's
    octagon, which the sky mesh never samples. Shared.
  * Morph rows: 1-3 (water ripples) are ours, identical; 42 `pokpo01` is our 47
    (the canyon brought it); 43 `pokpo02`, a second waterfall, goes to our free 50
    (our 42/43 are cart abilities).
  * Tiles: 31 of the 46 are new. T007/T031 are ours re-encoded; T008 is a
    brighter version of ours, which every Junon field uses -- ours stays, and no
    HA tile uses it anyway.
  * Minimap: 1024 px at 128 px a chunk (China Rose's CMinimapDLG.cpp
    MINIMAP_RESOLUTION_PER_MAP; ours is 64) -> 512 px. The picture is 8 x 8 chunks
    from LIST_ZONE cols 9/10 (29, 29) with a one-chunk border, so the map's row
    y=28 (the sea) sits in the border, as in China Rose. Three labels name the
    series' neighbours, repainted: west 镜花雪原 "Mirror-Flower Snowfield" ->
    "Mirage Snowfield" (the canyon's name for it: Snowfield001, CR zone 12), east
    遗忘之都 "City of Oblivion" -> "Forgotten City" (the gate's YJ), south 栖雨林地
    "rain-roost woodland" -> "Rainforest" (the notch in the south cliff; Yulin 雨林 is
    CR zone 29, though no ZON links the two).
  * **One tree model had its collision on the wrong part** (first test, 2026-11-04:
    "this one tree you can walk through", at (4803, 4995) m). LIST_DECO_Haian 28,
    `dtree_0731b` (31 placed): the trunk, part 1 `dtree_0731b02` (257 triangles, the
    ground to 6.3 m in the model, ~20 m placed), is collision 0, and the
    collision-12 part is `dtree_0731b03`, a 6-triangle card floating 1.7-4.7 m up.
    Object 81 is the same model with the two levels swapped -- China Rose's authors
    fixed it as a new object (2 placed) and left 28 alone. collision_fixes makes 28's
    parts byte for byte 81's. A scan of every placed part that stands on the ground,
    over 2.5 m tall, with collision 0 found nothing else of the kind: leaves over a
    colliding trunk, flat cards (a door, a backdrop) and the jungle undergrowth, which
    stays walk-through as Eldeon's ferns do.
  * **The hanging vines drew a pale outline that came and went with the camera**
    (second test, 2026-11-04). Not the texture's colour: `Pk011_veg_tree_014_A.dds`
    (object 28's part 2, the vine card, and three other trees') is green under its
    clear texels at every mip level. It is the engine. A material with texture alpha
    on a fogged object is alpha-BLENDED with an alpha test of 32
    (zz_material_colormap.cpp, "do small alpha test for fogged objects") and goes to
    the transparent pass, sorted far to near **part by part**, each part by its own
    centre (zz_scene_octree.cpp compare_farther_than). The canopy's centre is ~20 m up,
    the vines' ~10 m: as the camera moves their order flips, and when the vines draw
    first their half-transparent edge blends with the sky already in the frame, writes
    depth and hides the canopy drawn after. Retail trees mostly carry their leaves on
    one alpha part. This texture's alpha is a soft gradient (256 levels), so the edge
    is wide. cutout_textures makes it a hard cut-out at the material's own reference
    (128), each mip level cut at the threshold that keeps mip 0's coverage (the strands
    do not thin out with distance), colour averaged over the visible texels: measured
    with bilinear sampling, the half-transparent share of the vines' visible pixels
    falls from 36% to 19% magnified 6x and from 47% to 31% at mip 1, coverage 22% ->
    20% (the faintest wisps go). A mitigation, not a cure: the next test showed the
    same lines round the canopies, whose texture is already a hard cut-out (0/239).
    The cure is in the engine, for every map: two-pass alpha parts (src/engine/CLAUDE.md,
    "Transparency Antialiasing"; `[VIDEO] FOLIAGE_TWO_PASS`, `/alphasplit`).

Backups: build/china-rose/ (orig/ for every file this script overwrote, bak/ for
the table codecs' .bak files).
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)

import numpy as np  # noqa: E402
from PIL import Image, ImageDraw, ImageFont  # noqa: E402
import rosedata as oro  # noqa: E402  the shared data layer
from rosedata import texture  # noqa: E402  texconv, the hard cut-out
from rosefmt import chunk, dds, ifo, lit, lnav, zon  # noqa: E402
from rosefmt.zsc import TAG_COLLISION, Zsc, decode_props, serialize_object  # noqa: E402

# --------------------------------------------------------------------- config
DEFAULT_SRC = r"C:\Users\Thomas\Desktop\china rose\extracted"
BUILD_DIR = os.path.join(ROOT, "build", "china-rose")

ZONE_STB_REL = r"3DDATA\STB\LIST_ZONE.STB"
ZONE_STL_REL = r"3DDATA\STB\LIST_ZONE_S.STL"
MORPH_STB_REL = r"3DDATA\STB\LIST_MORPH_OBJECT.STB"
MORPH_FILE_COLS = (1, 2, 3)
SKY_STB_REL = r"3DDATA\STB\LIST_SKY.STB"
SKY_FILE_COLS = (0, 1, 2)
# LIST_ZONE: China Rose's cols 0-35 are ours (its IO_STB.h:597-648); 36-41 are its own
ZONE_NAME_COL, ZONE_FILE_COL, ZONE_MUSIC_DAY_COL, ZONE_MUSIC_NIGHT_COL = 0, 1, 5, 6
ZONE_SKY_COL, ZONE_MINIMAP_COL, ZONE_MINIMAP_X_COL, ZONE_MINIMAP_Y_COL = 7, 8, 9, 10
ZONE_DECO_COL, ZONE_CNST_COL, ZONE_STL_COL = 11, 12, 26
BAK_TRACKED = [ZONE_STB_REL, ZONE_STL_REL, MORPH_STB_REL, SKY_STB_REL]

MINIMAP_NAME = "MINIMAP.DDS"
SOURCE_MINIMAP_PX = 128             # China Rose's CMinimapDLG.cpp MINIMAP_RESOLUTION_PER_MAP
MINIMAP_PX = 64                     # ours (src/client/interface/dlgs/cminimapdlg.cpp)
MINIMAP_FONT = (r"C:\Windows\Fonts\arialbd.ttf", 11)
MINIMAP_TEXT, MINIMAP_OUTLINE, MINIMAP_SHADOW = (255, 255, 255), (35, 28, 20), (20, 16, 12)
LIT_NAMES = ("OBJECTLIGHTMAPDATA.LIT", "BUILDINGLIGHTMAPDATA.LIT")
ORIGIN_M, CHUNK_M = 5200.0, 160.0

# lumps a map's records may sit in; a record anywhere else stops the install
PLACED_LUMPS = {ifo.OBJECT, ifo.COLLISION, ifo.MORPH}

HA = dict(
    key="HA", src_zone=27, zone=158, name="Haian Coast", stl_key="LZON158",
    src_dir=r"3DDATA\MAPS\HA", dst_dir=r"3DDATA\Maps\ELDEON\HA", zon="HA.zon",
    deco=r"3DDATA\MAPOBJ\LIST_DECO_Haian.ZSC", cnst=r"3DDATA\MAPOBJ\LIST_CNST_FK.ZSC",
    music=rb"Sound\BGM\BGM_FCSL.ogg",                # CR's Emerald Forest theme (HA ships none)
    # China Rose row -> ours: 1-3 identical, 42 pokpo01 = our 47 (the canyon's), 43 pokpo02 new
    morph_rows={1: 1, 2: 2, 3: 3, 42: 47, 43: 50},
    sky_rows={0: 13},                                # cell for cell (see the docstring)
    # deco object -> {part: (mesh it must be, collision level)}: 28's trunk had none,
    # a floating card had 12; the result must equal object 81, the authors' fixed copy
    collision_fixes={28: {1: ("dtree_0731b02.zms", 12), 2: ("dtree_0731b03.zms", 0)}},
    collision_twins={28: 81},
    # textures made hard cut-outs at their material's alpha reference (see the docstring:
    # the hanging vines' pale outline)
    cutout_textures={r"3Ddata\mapobj\HOUSE\haian\HA_dds\Pk011_veg_tree_014_A.dds": 128},
    # spawn points: kept out until the monster stage
    held_lumps=(ifo.REGEN,),
    # the dead east gate: lump -> what each record must be, per chunk
    dead={"34_31.IFO": {ifo.WARP: [38], ifo.EFFECT: [b"3DData\\Effect\\_warp_gate03.eft"]}},
    # The minimap's labels on China Rose's 1024 px picture (text box, erased there) and
    # ours, drawn centred on the old text at half size.
    labels=(
        ((277, 429, 364, 447), "Mirage Snowfield"),  # 镜花雪原, west, over the start
        ((780, 408, 868, 426), "Forgotten City"),    # 遗忘之都, east, the gate to YJ
        ((476, 784, 563, 803), "Rainforest"),        # 栖雨林地, south, the cliff notch
    ),
    gm_way_in="/mm 158 489 533",
)
MAPS = {"HA": HA}


# -------------------------------------------------------------------- helpers
def P(root, rel):
    return os.path.join(root, rel.replace("\\", "/"))


def txt(cell):
    return cell.decode("latin-1").strip()


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
    """Move the table codecs' .bak files out of data/ -- pack.ps1 refuses to bake them."""
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


def zone_folder(spec):
    return os.path.basename(spec["dst_dir"])


# --------------------------------------------------------------- map files
def map_files(s_map):
    """(sub, name) of every source map file we ship, in walk order (no .MOV: retired)."""
    for base, _, names in os.walk(s_map):
        sub = os.path.relpath(base, s_map)
        for name in sorted(names):
            if not name.lower().endswith(".mov"):
                yield sub, name


def map_ifo(spec, s_map, name):
    """A China Rose .IFO as we ship it, and what was taken out: {lump: records}."""
    raw = open(os.path.join(s_map, name), "rb").read()
    m = ifo.parse(raw)
    out = {}
    dead = spec["dead"].get(name.upper(), {})
    for lt, lname in ifo.LUMP_NAMES.items():
        recs = m.lump(lt)
        if lt in (ifo.MAPINFO, ifo.WATER, ifo.OCEAN) or not recs or lt in PLACED_LUMPS:
            continue
        if lt in spec["held_lumps"]:
            out[lt] = list(recs)
            recs.clear()
            continue
        if lt in dead:
            got = [r.warp_id if lt == ifo.WARP else r.eft if lt == ifo.EFFECT else r.obj_id for r in recs]
            if got != dead[lt]:
                raise SystemExit(f"{name}: {lname} holds {got}, the spec expects {dead[lt]}")
            out[lt] = list(recs)
            recs.clear()
            continue
        raise SystemExit(f"{name}: {len(recs)} unexpected record(s) in lump {lname}")
    for lt in dead:
        if lt not in out:
            raise SystemExit(f"{name}: the spec's dead {ifo.LUMP_NAMES[lt]} records are not in the source")
    changed = bool(out)
    for r in m.lump(ifo.MORPH) or []:
        if r.obj_id not in spec["morph_rows"]:
            raise SystemExit(f"{name}: morph object {r.obj_id} has no row mapping")
        if spec["morph_rows"][r.obj_id] != r.obj_id:
            r.obj_id = spec["morph_rows"][r.obj_id]
            changed = True
    return (ifo.build(m) if changed else raw), out


def dds_size(path):
    h = dds.parse_header(path)
    return h.width, h.height, h.mip_count


def find_name(folder, want):
    """A file's real name in a folder, case-insensitively (dumps are case-mixed)."""
    hit = [f for f in os.listdir(folder) if f.upper() == want.upper()]
    if len(hit) != 1:
        raise SystemExit(f"{folder}: {len(hit)} files named {want}")
    return hit[0]


def erase_labels(a, boxes):
    """The labels painted out of an (h, w, 3) float picture, in place: the white text,
    its outline and its down-right shadow are masked inside each box and filled from
    the pixels around them (repeated 4-neighbour averaging)."""
    pad = 4
    for x0, y0, x1, y1 in boxes:
        sub = a[y0 - pad:y1 + pad + 1, x0 - pad:x1 + pad + 1]
        white = sub.min(axis=2) > 150
        mask = white.copy()
        for dy in range(-2, 4):
            for dx in range(-2, 4):
                mask |= np.roll(np.roll(white, dy, 0), dx, 1)
        mask[[0, -1], :] = False
        mask[:, [0, -1]] = False
        for _ in range(400):
            nb = (np.roll(sub, 1, 0) + np.roll(sub, -1, 0) + np.roll(sub, 1, 1) + np.roll(sub, -1, 1)) / 4
            sub[mask] = nb[mask]


def minimap_bytes(spec, s_map):
    """China Rose's minimap at our scale, labelled in English, as DDS bytes.

    China Rose draws 128 px a chunk and our client 64 (both place the picture's
    top-left one chunk before LIST_ZONE cols 9/10), so the picture is halved. The
    labels are erased at full size, the picture box-halved, the English drawn at
    the halved size, and texconv encodes DXT1 with a full chain (add-dds-mipmaps.py's
    -nowic -if BOX)."""
    sp = os.path.join(s_map, find_name(s_map, MINIMAP_NAME))
    sw, sh, _ = dds_size(sp)
    if sw % SOURCE_MINIMAP_PX or sh % SOURCE_MINIMAP_PX:
        raise SystemExit(f"{sp} is {sw}x{sh}: not whole chunks at {SOURCE_MINIMAP_PX} px")
    w, h = sw * MINIMAP_PX // SOURCE_MINIMAP_PX, sh * MINIMAP_PX // SOURCE_MINIMAP_PX
    with tempfile.TemporaryDirectory() as tmp:
        src_copy = os.path.join(tmp, "src.dds")          # texconv writes beside a path it cannot parse
        shutil.copyfile(sp, src_copy)
        png = texture.texconv(["-ft", "png", "-m", "1", "-o", tmp, src_copy], os.path.join(tmp, "src.png"))
        a = np.asarray(Image.open(png).convert("RGB")).astype(np.float32)
        erase_labels(a, [box for box, _ in spec["labels"]])
        im = Image.fromarray(np.clip(np.rint(a), 0, 255).astype(np.uint8)).resize((w, h), Image.BOX)
        draw = ImageDraw.Draw(im)
        font = ImageFont.truetype(*MINIMAP_FONT)
        k = MINIMAP_PX / SOURCE_MINIMAP_PX
        for (x0, y0, x1, y1), text in spec["labels"]:
            x, y = (x0 + x1) / 2 * k, (y0 + y1) / 2 * k
            draw.text((x + 1, y + 1), text, font=font, anchor="mm", fill=MINIMAP_SHADOW)
            draw.text((x, y), text, font=font, anchor="mm", fill=MINIMAP_TEXT,
                      stroke_width=1, stroke_fill=MINIMAP_OUTLINE)
        os.makedirs(os.path.join(tmp, "out"))
        label_png = os.path.join(tmp, "MINIMAP.png")
        im.save(label_png)
        pow2 = w & (w - 1) == 0 and h & (h - 1) == 0       # DirectXTex's BOX takes powers of two only
        out = texture.texconv(["-dx9", "-m", "0", "-nowic", "-if", "BOX" if pow2 else "LINEAR", "-f", "BC1_UNORM",
                       "-o", os.path.join(tmp, "out"), label_png], os.path.join(tmp, "out", "MINIMAP.dds"))
        return open(out, "rb").read()


def minimap_covers(spec, src, s_map):
    """The chunks our minimap shows (with its border), from the halved picture and cols 9/10."""
    s_z = oro.Stb(P(src, ZONE_STB_REL))
    x0, y0 = int(txt(s_z.get(spec["src_zone"], ZONE_MINIMAP_X_COL))), int(txt(s_z.get(spec["src_zone"], ZONE_MINIMAP_Y_COL)))
    sw, sh, _ = dds_size(os.path.join(s_map, find_name(s_map, MINIMAP_NAME)))
    nx, ny = sw // SOURCE_MINIMAP_PX - 2, sh // SOURCE_MINIMAP_PX - 2
    return (x0 - 1, x0 + nx), (y0 - 1, y0 + ny)


# ----------------------------------------------------------------- tables
def set_collision(props, level):
    """A part's property list with its collision tag set to `level` (the tag is
    added before the end if absent); scripts/set-deco-collision.py's edit."""
    out, o, done = bytearray(), 0, False
    while o < len(props):
        tag = props[o]
        if tag == 0:
            break
        ln = props[o + 1]
        val = props[o + 2:o + 2 + ln]
        if tag == TAG_COLLISION and ln >= 2:
            val = struct.pack("<h", level) + val[2:]
            done = True
        out += bytes([tag, ln]) + val
        o += 2 + ln
    if not done:
        out += bytes([TAG_COLLISION, 2]) + struct.pack("<h", level)
    out += b"\x00"
    return bytes(out)


def deco_table(spec, src):
    """China Rose's decoration table as we ship it: collision_fixes applied, every
    other object byte for byte the source's, each fixed object equal part for part
    to its collision twin."""
    path = P(src, spec["deco"])
    raw = open(path, "rb").read()
    fixes = spec.get("collision_fixes", {})
    if not fixes:
        return raw
    z = Zsc(path)
    before = [serialize_object(o) for o in z.objects]
    for oid, parts_fix in fixes.items():
        cyl, parts, dummies, bb = z.objects[oid]
        parts = list(parts)
        for pi, (mesh, level) in parts_fix.items():
            m, t, props = parts[pi]
            if os.path.basename(z.meshes[m].decode("latin-1")).lower() != mesh.lower():
                raise SystemExit(f"{spec['deco']} object {oid} part {pi} is not {mesh}")
            parts[pi] = (m, t, set_collision(props, level))
        z.objects[oid] = (cyl, parts, dummies, bb)
    data = z.rebuild()
    z2 = Zsc(data)
    if len(z2.objects) != len(before) or z2.meshes != z.meshes or z2.materials != z.materials:
        raise SystemExit(f"{spec['deco']}: the rebuild changed the table's lists")
    for i, o in enumerate(z2.objects):
        if i not in fixes and serialize_object(o) != before[i]:
            raise SystemExit(f"{spec['deco']}: object {i} changed")
    for oid, twin in spec.get("collision_twins", {}).items():
        if [p[2] for p in z2.objects[oid][1]] != [p[2] for p in z2.objects[twin][1]]:
            raise SystemExit(f"{spec['deco']}: object {oid}'s parts are not object {twin}'s")
    return data


def zone_row(spec, s_z, d_z):
    r = spec["src_zone"]
    row = [s_z.get(r, c) for c in range(d_z.cols)]
    row[ZONE_NAME_COL] = spec["name"].encode()
    row[ZONE_FILE_COL] = (spec["dst_dir"] + "\\" + spec["zon"]).encode()
    row[ZONE_MUSIC_DAY_COL] = row[ZONE_MUSIC_NIGHT_COL] = spec["music"]
    row[ZONE_SKY_COL] = str(spec["sky_rows"][int(txt(s_z.get(r, ZONE_SKY_COL)))]).encode()
    row[ZONE_MINIMAP_COL] = (spec["dst_dir"] + "\\minimap.dds").encode()
    row[ZONE_STL_COL] = spec["stl_key"].encode()
    return row


def table_rows(src, ours, rel, rows):
    """{our row: wanted cells} for a row-for-row copy out of a China Rose table."""
    s, d = oro.Stb(P(src, rel)), oro.Stb(P(ours, rel))
    if s.cols != d.cols:
        raise SystemExit(f"{rel}: China Rose has {s.cols} columns, ours {d.cols}")
    return {dst: [s.get(sr, c) for c in range(d.cols)] for sr, dst in rows.items()}


def put_rows(ours, rel, want, label, dry):
    d = oro.Stb(P(ours, rel))
    wrote, shared = [], []
    for r, cells in sorted(want.items()):
        if r < d.rows and d.d[r] == cells:
            shared.append(r)
            continue
        if r >= d.rows or d.occupied(r):
            raise SystemExit(f"our {label} row {r} is {d.get(r, 0)!r}, not China Rose's")
        d.d[r] = cells
        wrote.append(r)
    if wrote:
        d.save(dry)
    print(f"    {label:26s} rows written {wrote or 'none'}, shared {shared or 'none'}")


def map_assets(spec, src):
    """Every data-relative file a map needs besides its map folder, by kind."""
    deco = oro.Zsc(P(src, spec["deco"]))
    art = {a for a in oro.zsc_asset_refs(deco) if "\\" in a or "/" in a}
    art |= oro.effect_chain({a for a in art if a.lower().endswith(".eft")}, src)
    tiles = {t for t in oro.zon_tiles(P(src, spec["src_dir"] + "\\" + spec["zon"])) if "\\" in t or "/" in t}
    want = table_rows(src, src, MORPH_STB_REL, {r: r for r in spec["morph_rows"]})
    morph = {txt(cells[c]) for cells in want.values() for c in MORPH_FILE_COLS if txt(cells[c])}
    want = table_rows(src, src, SKY_STB_REL, {r: r for r in spec["sky_rows"]})
    sky = {txt(cells[c]) for cells in want.values() for c in SKY_FILE_COLS if txt(cells[c])}
    return {"object tables": {spec["deco"], spec["cnst"]}, "decoration art": art,
            "terrain tiles": tiles, "morph art": morph, "sky": sky}


# ---------------------------------------------------------------- walk grid
def lnav_path(spec, ours):
    return os.path.join(P(ours, spec["dst_dir"]), zone_folder(spec) + ".lnav")


def lnav_stale(spec, ours):
    """Inputs the grid was baked from that changed since ([] = fresh), or None if absent;
    the server's own test (nav_grid.cpp Grid::Load)."""
    path = lnav_path(spec, ours)
    if not os.path.isfile(path):
        return None
    nav = lnav.parse(open(path, "rb").read())
    zdir, stale = P(ours, spec["dst_dir"]), []
    for name, crc in nav.inputs:
        n = name.decode("latin-1")
        f = os.path.join(ours if "/" in n else zdir, n)
        if not os.path.isfile(f) or zlib.crc32(open(f, "rb").read()) != crc:
            stale.append(n)
    return stale


def bake(spec):
    print(f"  walk grid: scripts/nav-bake.py {spec['zone']}")
    r = subprocess.run([sys.executable, os.path.join(HERE, "nav-bake.py"), str(spec["zone"])],
                       capture_output=True, text=True, encoding="utf-8", errors="replace")
    for line in r.stdout.splitlines():
        print("    | " + line)
    if r.returncode:
        raise SystemExit("nav-bake failed:\n" + r.stderr[-2000:])


# ------------------------------------------------------------------- install
def install(spec, ours, src, dry, do_bake):
    print(f"{spec['key']} -> zone {spec['zone']}, {spec['name']}")
    s_map, d_map = P(src, spec["src_dir"]), P(ours, spec["dst_dir"])
    s_z, d_z = oro.Stb(P(src, ZONE_STB_REL)), oro.Stb(P(ours, ZONE_STB_REL))
    src_zon = txt(s_z.get(spec["src_zone"], ZONE_FILE_COL)).lower()
    if src_zon != (spec["src_dir"] + "\\" + spec["zon"]).lower():
        raise SystemExit(f"China Rose LIST_ZONE row {spec['src_zone']} is {src_zon!r}")

    copied = rewritten = 0
    taken = {}
    for sub, name in map_files(s_map):
        sp = os.path.join(s_map, sub, name)
        dp = os.path.normpath(os.path.join(d_map, sub, name))
        if name.lower().endswith(".ifo"):
            blob, out = map_ifo(spec, s_map, name)
            for lt, recs in out.items():
                taken[lt] = taken.get(lt, 0) + len(recs)
            rewritten += write_if_changed(dp, blob, dry)
            continue
        if sub == "." and name.upper() == MINIMAP_NAME:
            if write_if_changed(dp, minimap_bytes(spec, s_map), dry):
                print(f"    {name:26s} halved to our {MINIMAP_PX} px a chunk, labels in English")
            continue
        if os.path.isfile(dp):
            if open(dp, "rb").read() != open(sp, "rb").read() and not name.lower().endswith(".dds"):
                raise SystemExit(f"{dp} differs from China Rose's: not ours to overwrite")
            continue                   # a .dds may carry the mip chain add-dds-mipmaps gave it
        if not dry:
            os.makedirs(os.path.dirname(dp), exist_ok=True)
            shutil.copyfile(sp, dp)
        copied += 1
    print(f"    {zone_folder(spec):26s} {copied:5d} files copied, {rewritten} .IFO written")
    for lt, n in sorted(taken.items()):
        why = "held for the monster stage" if lt in spec["held_lumps"] else "dead, dropped"
        print(f"    {'':26s} {n:5d} {ifo.LUMP_NAMES[lt]} record(s) {why}")

    new_dds = []
    for label, rels in map_assets(spec, src).items():
        rels = rels - {spec["deco"]}                       # written below, with its fixes
        new_dds += [r for r in rels if r.lower().endswith(".dds") and not os.path.isfile(P(ours, r))]
        oro.copy_files(rels, src, ours, dry, label)
    fixed = sorted(spec.get("collision_fixes", {}))
    if write_if_changed(P(ours, spec["deco"]), deco_table(spec, src), dry):
        print(f"    {os.path.basename(spec['deco']):26s} written" +
              (f", collision fixed on object(s) {fixed}" if fixed else ""))
    else:
        print(f"    {os.path.basename(spec['deco']):26s} already in place")
    for rel, ref in spec.get("cutout_textures", {}).items():
        state = "written" if write_if_changed(P(ours, rel), texture.cutout_dds(P(src, rel), ref), dry) else "already in place"
        print(f"    {os.path.basename(rel):26s} hard cut-out at {ref}, full chain: {state}")
    have_cnst = P(ours, spec["cnst"])
    if open(have_cnst, "rb").read() != open(P(src, spec["cnst"]), "rb").read():
        raise SystemExit(f"our {spec['cnst']} is not China Rose's: the map's buildings would change")

    put_rows(ours, MORPH_STB_REL, table_rows(src, ours, MORPH_STB_REL, spec["morph_rows"]),
             "LIST_MORPH_OBJECT.STB", dry)
    put_rows(ours, SKY_STB_REL, table_rows(src, ours, SKY_STB_REL, spec["sky_rows"]), "LIST_SKY.STB", dry)

    zone, name = spec["zone"], spec["name"]
    grown = d_z.grow_to(zone + 1, labels={zone: name})
    cur = txt(d_z.get(zone, ZONE_NAME_COL))
    if cur and cur != name:
        raise SystemExit(f"our LIST_ZONE row {zone} is {cur!r}")
    want = zone_row(spec, s_z, d_z)
    if d_z.d[zone] != want or grown:
        d_z.d[zone] = want
        d_z.save(dry)
        print(f"    {'LIST_ZONE.STB':26s} row {zone} written" + (f" (+{grown} rows)" if grown else ""))
    else:
        print(f"    {'LIST_ZONE.STB':26s} row {zone} already in place")
    stl = oro.Stl(P(ours, ZONE_STL_REL))
    if not stl.has(spec["stl_key"]):
        stl.append(spec["stl_key"], zone, name)
        stl.save(dry)
        print(f"    {'LIST_ZONE_S.STL':26s} +1 key {spec['stl_key']}")
    elif stl.name(spec["stl_key"]) != name:
        stl.set_name(spec["stl_key"], name)
        stl.save(dry)
        print(f"    {'LIST_ZONE_S.STL':26s} {spec['stl_key']} renamed {name}")
    else:
        print(f"    {'LIST_ZONE_S.STL':26s} {spec['stl_key']} already in place")

    if not dry:
        sweep_baks(ours)
        if do_bake:
            bake(spec)
    if new_dds:
        dirs = sorted({os.path.dirname(d).replace("\\", "/") for d in new_dds})
        print(f"\n    {len(new_dds)} new .dds outside the map folder; give them mip chains with")
        print("    scripts/add-dds-mipmaps.py --subdir <dir> for each of:")
        for d in dirs:
            print(f"        {d}")
    print(f"    and --subdir {spec['dst_dir'].replace(chr(92), '/')} --lightmaps")
    print(f"    GM way in: {spec['gm_way_in']}")


# -------------------------------------------------------------- verify etc.
def verify(ours, src):
    bad = 0

    def check(ok, what):
        nonlocal bad
        bad += not ok
        print(f"    {'ok  ' if ok else 'FAIL'} {what}")

    z_ours = oro.Stb(P(ours, ZONE_STB_REL))
    for spec in MAPS.values():
        zone = spec["zone"]
        if not (z_ours.rows > zone and txt(z_ours.get(zone, ZONE_NAME_COL)) == spec["name"]):
            print(f"verify {spec['key']}: not installed")
            continue
        print(f"verify {spec['key']} (zone {zone}, {spec['name']})")
        s_map, d_map = P(src, spec["src_dir"]), P(ours, spec["dst_dir"])
        same, n = True, 0
        for sub, name in map_files(s_map):
            dp = os.path.normpath(os.path.join(d_map, sub, name))
            n += 1
            if name.lower().endswith(".ifo"):
                want = map_ifo(spec, s_map, name)[0]
            elif sub == "." and name.upper() == MINIMAP_NAME:
                want = minimap_bytes(spec, s_map)
            elif name.lower().endswith(".dds"):
                same &= os.path.isfile(dp)     # add-dds-mipmaps.py gives them a chain
                continue
            else:
                want = open(os.path.join(s_map, sub, name), "rb").read()
            same &= os.path.isfile(dp) and open(dp, "rb").read() == want
        check(same, f"map folder as built ({n} files: spawns held, dead gate dropped, morphs re-pointed, "
                    f"minimap at our scale)")
        movs = [f for f in os.listdir(d_map) if f.lower().endswith(".mov")] if os.path.isdir(d_map) else []
        check(not movs, f"no .MOV ({len(movs)})")
        left, morph_ids = {}, set()
        for name in sorted(f for f in os.listdir(d_map) if f.lower().endswith(".ifo")):
            m = ifo.parse(open(os.path.join(d_map, name), "rb").read())
            for lt, lname in ifo.LUMP_NAMES.items():
                recs = m.lump(lt)
                if isinstance(recs, list) and recs and lt not in PLACED_LUMPS:
                    left[lname] = left.get(lname, 0) + len(recs)
            morph_ids |= {r.obj_id for r in m.lump(ifo.MORPH) or []}
        check(not left, f"only decorations, walls and morphs placed ({left or 'nothing else'})")
        check(morph_ids <= set(spec["morph_rows"].values()), f"morph ids {sorted(morph_ids)} are the mapped rows")
        zp = os.path.join(d_map, find_name(d_map, spec["zon"])) if os.path.isdir(d_map) else None
        ev = {e.name for e in zon.parse(open(zp, "rb").read()).lump(zon.EVENTS)} if zp else set()
        check({b"start", b"restore"} <= ev, "start / restore")
        (cx0, cx1), (cy0, cy1) = minimap_covers(spec, src, s_map)
        sx, sy = [e for e in zon.parse(open(zp, "rb").read()).lump(zon.EVENTS) if e.name == b"start"][0].x, \
            [e for e in zon.parse(open(zp, "rb").read()).lump(zon.EVENTS) if e.name == b"start"][0].y
        chx, chy = int((sx / 100 + ORIGIN_M) // CHUNK_M), 64 - int((sy / 100 + ORIGIN_M) // CHUNK_M)
        check(cx0 < chx < cx1 and cy0 < chy < cy1,
              f"the start's chunk {chx}_{chy} is inside the minimap ({cx0 + 1}-{cx1 - 1} x {cy0 + 1}-{cy1 - 1})")

        missing = []
        for rels in map_assets(spec, src).values():
            missing += [r for r in rels if os.path.isfile(P(src, r)) and not os.path.isfile(P(ours, r))]
        check(not missing, f"every asset the map names is here ({len(missing)} missing)")
        fixed = sorted(spec.get("collision_fixes", {}))
        check(open(P(ours, spec["deco"]), "rb").read() == deco_table(spec, src),
              f"{os.path.basename(spec['deco'])} is China Rose's"
              + (f", collision fixed on object(s) {fixed} (= their twins "
                 f"{[spec['collision_twins'][o] for o in fixed]})" if fixed else ""))
        check(open(P(ours, spec["cnst"]), "rb").read() == open(P(src, spec["cnst"]), "rb").read(),
              f"{os.path.basename(spec['cnst'])} is China Rose's")
        for rel, ref in spec.get("cutout_textures", {}).items():
            p = P(ours, rel)
            check(os.path.isfile(p) and open(p, "rb").read() == texture.cutout_dds(P(src, rel), ref),
                  f"{os.path.basename(rel)} is a hard cut-out at {ref} with its full chain")
        for rel, rows, label in ((MORPH_STB_REL, spec["morph_rows"], "LIST_MORPH_OBJECT"),
                                 (SKY_STB_REL, spec["sky_rows"], "LIST_SKY")):
            d = oro.Stb(P(ours, rel))
            want = table_rows(src, ours, rel, rows)
            check(all(d.d[r] == cells for r, cells in want.items()), f"{label} rows {sorted(want)}")
        check(z_ours.d[zone] == zone_row(spec, oro.Stb(P(src, ZONE_STB_REL)), z_ours), f"LIST_ZONE row {zone}")
        stl = oro.Stl(P(ours, ZONE_STL_REL))
        check(stl.has(spec["stl_key"]) and stl.name(spec["stl_key"]) == spec["name"],
              f"{spec['stl_key']} = {spec['name']}")
        r = subprocess.run([sys.executable, os.path.join(HERE, "audit-lightmap-index.py"), "--zone", str(zone)],
                           capture_output=True, text=True, encoding="utf-8", errors="replace")
        last = r.stdout.strip().splitlines()[-1] if r.stdout.strip() else r.stderr.strip()
        check(r.returncode == 0 and "0 CRASH, 0 RISK, 0 VISUAL" in last, f"audit-lightmap-index --zone {zone}: {last}")
        stale = lnav_stale(spec, ours)
        check(stale == [], "walk grid " + ("missing: run scripts/nav-bake.py %d" % zone if stale is None else
                                           f"stale ({', '.join(stale[:4])}): re-bake" if stale else
                                           f"{zone_folder(spec)}.lnav matches every map file"))
    baks = [rel for rel in BAK_TRACKED if os.path.isfile(P(ours, rel) + ".bak")]
    check(not baks, "no .bak left under data/")
    print("verify " + ("OK" if not bad else f"FAILED ({bad})"))
    return 1 if bad else 0


def selftest(ours, src):
    print("selftest -- every reader/writer must reproduce its input byte for byte")
    for spec in MAPS.values():
        s_map = P(src, spec["src_dir"])
        n = 0
        for sub, name in map_files(s_map):
            p = os.path.join(s_map, sub, name)
            blob = open(p, "rb").read()
            low = name.lower()
            if low.endswith(".ifo"):
                assert ifo.build(ifo.parse(blob)) == blob, name
            elif name.upper() in LIT_NAMES:
                assert lit.build_lit(lit.parse_lit(blob)) == blob, (sub, name)
            elif low.endswith(".zon"):
                assert zon.build(zon.parse(blob)) == blob, name
            elif low.endswith(".him"):
                assert chunk.build_him(chunk.parse_him(blob)) == blob, name
            elif low.endswith(".til"):
                assert chunk.build_til(chunk.parse_til(blob)) == blob, name
            else:
                continue
            n += 1
        z = oro.Zsc(P(src, spec["deco"]))
        assert z.rebuild_with() == z.d[:z.obj_end], spec["deco"]
        print(f"    {spec['key']}: {n} .IFO / .HIM / .TIL / .lit / .ZON files and "
              f"{os.path.basename(spec['deco'])} round-trip")
    for rel in (ZONE_STB_REL, MORPH_STB_REL, SKY_STB_REL):
        for root in (ours, src):
            p = P(root, rel)
            assert oro.Stb(p).to_bytes() == open(p, "rb").read(), p
    p = P(ours, ZONE_STL_REL)
    assert oro.Stl(p).to_bytes() == open(p, "rb").read(), ZONE_STL_REL
    print("    LIST_ZONE / LIST_MORPH_OBJECT / LIST_SKY (both sides) and LIST_ZONE_S round-trip")
    print("    selftest OK")


# ---------------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--map", choices=sorted(MAPS), action="append", help="install this map (repeatable)")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--no-bake", action="store_true", help="skip the walk grid (nav-bake.py)")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--selftest", action="store_true")
    ap.add_argument("--root", default=ROOT)
    ap.add_argument("--source", default=DEFAULT_SRC, help="China Rose's unpacked data")
    args = ap.parse_args()
    ours = os.path.join(args.root, "data")
    src = args.source
    for spec in MAPS.values():
        if not os.path.isdir(P(src, spec["src_dir"])):
            raise SystemExit(f"not China Rose's unpacked data (no {spec['src_dir']}): {src}")
    if args.selftest:
        selftest(ours, src)
        return 0
    if args.verify:
        return verify(ours, src)
    if not args.map:
        ap.error("give --map, --verify or --selftest")
    for key in args.map:
        install(MAPS[key], ours, src, args.dry_run, not args.no_bake)
        print()
    if args.dry_run:
        print("dry run: nothing written")
    else:
        print("done. Next: add-dds-mipmaps.py on the folders above, --verify, rebake the VFS,")
        print("restart the servers.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
