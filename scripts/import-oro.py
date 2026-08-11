"""Import the Oro planet (12 zones) from the RoseZA / Narose test client.

RoseZA ships the *evo-era* Oro -- Muris, the Golden Ring, the Wasteland -- which is
a completely different planet from the retail Oro that QQ-iROSE and ruff carry
(Heliopolis / Paradise of Ra / Geb Desert). The two share only the folder name
ODRP01. This script imports the RoseZA one, at RoseZA's own zone numbers 71-82,
because quest warps store zone numbers as literals (QP401/QP402 alone contain 46
REWD_007 warps hardcoded to 71-82) and renumbering would mean rewriting all of them.

Staged, because each stage is independently testable in-game and independently
revertible:

    --stage 1   terrain, art, zone rows, zone names.  No NPCs, no monsters, no
                gates: the copied .IFOs get their MOB/REGEN/WARP/EVENT_OBJECT
                lumps emptied on the way in (count = 0, lump table untouched), so
                later stages just refill them.
    --stage 2   the 24 internal warp gates (WARP.STB rows + IFO lump 10).
    --stage 3   monsters: LIST_NPC rows, AI rows + .aip files, character models,
                and the spawn lump.

Every stage is idempotent -- re-running detects what is already in place and does
nothing. --dry-run previews. --selftest proves every writer round-trips
byte-identically before anything is touched.

Why this is a *port* and not a copy
-----------------------------------
The container formats are unchanged between the evo and classic eras (all 173 Oro
.IFOs parse with our reader, ZON lumps and ZSC tables likewise), and LIST_NPC.STB
is column-aligned on cols 0-42, so entity rows copy straight across. But four
things do not travel:

  * `LIST_ZONE.STB` diverges after col 30. Ours puts revive zone/x/y at 31-33;
    RoseZA leaves those blank and keeps a revive zone at col 36. Only cols 0-30
    are copied; 31-33 are left blank (nothing in our code reads them -- revive
    resolves through the `restore` event position in the .ZON).
  * Three zones have no event position named `restore`: TOWN calls it `restor`
    (a typo in the original data), ODRP01 and ODE01 call it `respawn`. RoseZA's
    own zone rows say `restore` for all twelve, so copying col 3 verbatim would
    give three zones a revive point that resolves to NULL -- the same failure mode
    as the Xita->Shady warp disconnect. ZONE_REVIVE_POS is fixed per zone here.
  * `PART_NPC.ZSC` / skeleton / motion indices overlap between the two data sets
    (Oro needs models 260-744, skeletons 49-254, motions 343-1527 against our
    707/233/1409), so character models are appended with a remap, never by index.
  * Item ids mean different things in the two data sets (weapon 22 is "Simplex
    Saber" for us and "Panther Blade" for them), which is why nothing here touches
    shops, drops or quest rewards. Those are stages 4-6 and are authoring work.

Not done here, deliberately: the entrance. Oro's canonical way in is the quest
trigger JZP43_73 in QP101.QSD, fired from the Junon Pyramid B3 (zone 43) which we
do not have. Reach the zones with a GM warp until that is decided.

After running: rebake the client VFS, and restart the servers for stages 1-3
(they cache STBs at startup).

File formats this script writes are documented at their reader/writer functions.
"""
import argparse, io, os, re, shutil, struct, sys

NUL = b"\x00"
ABS_ASSET_RE = re.compile(rb"3DDATA[\\/][0-9A-Za-z_\\/. -]+?\.(?:ptl|dds|tga|zms)", re.I)
BARE_TEXTURE_RE = re.compile(rb"[0-9A-Za-z_][0-9A-Za-z_-]*\.(?:dds|tga)", re.I)

# --------------------------------------------------------------------- config
DEFAULT_SRC = r"C:\Users\Thomas\Desktop\Testclients\RoseZA test client\data"

# zone row -> (folder, clean name, revive event position that actually exists)
ZONES = [
    (71, "TOWN",   "Desert City of Muris",   "restor"),
    (72, "OROIP",  "Portal Room",            "restore"),
    (73, "ODP01",  "Orlean Portal Temple",   "restore"),
    (74, "ODC01",  "Wreckage of the Blindi", "restore"),
    (75, "ODD01",  "The Golden Ring",        "restore"),
    (76, "ODD02",  "The Golden Ring",        "restore"),
    (77, "ODD03",  "The Golden Ring",        "restore"),
    (78, "ODD04",  "The Wasteland",          "restore"),
    (79, "ODD05",  "The Wasteland",          "restore"),
    (80, "ODOS01", "Ancient Oasis Shrine",   "restore"),
    (81, "ODRP01", "Wasteland Ruins Path",   "respawn"),
    (82, "ODE01",  "Gates of Muris",         "respawn"),
]
ZONE_STRING_NAMES = {              # STL key -> English name (from RoseZA lang block 1)
    "LZON080": "Desert City of Muris",
    "LZON081": "Portal Room",
    "LZON082": "Orlean Portal Temple",
    "LZON083": "Wreckage of the Blindi",
    "LZON084": "The Golden Ring",
    "LZON085": "The Wasteland",
    "LZON086": "Gates of Muris",
    "LZON089": "Ancient Oasis Shrine",
    "LZON090": "Wasteland Ruins Path",
}

MAX_ZONE_ROW = 82
ZONE_OBJ_TABLE_COL, ZONE_CNST_TABLE_COL = 11, 12
ZONE_COPY_COLS = 31                # cols 0..30 copy; 31+ diverge, see docstring

# object tables the Oro zones reference (LIST_CNST_JG.ZSC we already have)
ZSC_TABLES = [
    r"3DDATA\ORO\LIST_DECO_ODT.ZSC",
    r"3DDATA\ORO\LIST_CNST_ODT.ZSC",
    r"3DDATA\ORO\LIST_DECO_ODD.ZSC",
    r"3DDATA\ORO\LIST_CNST_ODD.ZSC",
    r"3DDATA\JUNON\LIST_DECO_JZP.ZSC",
]

MAPS_REL = r"3DDATA\MAPS\ORO"
SKY_STB_REL = r"3DDATA\STB\LIST_SKY.STB"
PARTICLE_TEXTURE_DIR = r"3DDATA\EFFECT\PARTICLES\TEXTURE"

# ZONE_BG_IMAGE (col 7) is a LIST_SKY.STB row, and every Oro zone points at row 10,
# which is blank in ours -- the map editor reports "Map load failed during loading
# sky" and the client gets no sky mesh. The zone rows name three tables, not two:
# object (11), cnst (12) and this one.
ZONE_SKY_COL = 7
ORO_SKY_ROW = 10

# Zone join/kill/dead triggers. RoseZA hangs its own PvP hooks on Muris and the
# Gates of Muris (PvP71-Enter, PvP82-Enter/Kill/Death) which live in QSDs we do not
# have; 27 of our own zones use PvP1301-340, so the Oro rows take that and leave
# kill/dead blank. An unresolved trigger is inert, but a name no QSD defines is a
# landmine for whoever adds Oro quests later.
ZONE_TRIGGER_COLS = (22, 23, 24)
ZONE_TRIGGERS = (b"PvP1301-340", b"", b"")
NPC_CHR_REL = r"3DDATA\NPC\LIST_NPC.CHR"
PART_NPC_ZSC_REL = r"3DDATA\NPC\PART_NPC.ZSC"

LUMP_OBJECT, LUMP_MOB, LUMP_REGEN = 1, 2, 8
LUMP_SOUND, LUMP_EFFECT = 4, 5
LUMP_WARP, LUMP_COLLISION, LUMP_EVENT_OBJECT = 10, 11, 12
# lumps stage 1 empties; stages 2 and 3 refill WARP and REGEN/MOB
LUMPS_STAGE1_EMPTY = (LUMP_MOB, LUMP_REGEN, LUMP_WARP, LUMP_EVENT_OBJECT)

NPC_AI_COL, NPC_STRID_COL, NPC_PVP_COL = 16, 40, 43
NPC_R_WEAPON_COL, NPC_L_WEAPON_COL = 5, 6
NPC_COPY_COLS = 43                 # 0..42 align; 43 is our own PVP-state column

# LIST_WEAPON columns that decide how a monster's attack *presents*. A blank
# WEAPON_BULLET_EFFECT on a ranged monster means the client's Get_BulletNO()
# skips Add_BULLET and the server's projectile path picks MeleeHitFrame, which
# nothing on a bow/gun motion consumes -- the monster then deals damage with no
# projectile, no hit and no sound. Cols 31-43 are structurally aligned between
# the two data sets even though the stat columns above them were rebalanced.
WEAPON_PRESENTATION_COLS = (38, 39, 40, 41, 42)


# ---------------------------------------------------------------- primitives
def read_pstr(buf, o):
    """7-bit varint length + bytes (STL flavour)."""
    b = buf[o]; o += 1
    if b & 0x80:
        b2 = buf[o]; o += 1
        n = (b2 << 7) | (b - 0x80)
    else:
        n = b
    return buf[o:o + n], o + n


def write_pstr(out, s):
    n = len(s)
    if n < 0x80:
        out.write(bytes([n]))
    elif n < 0x4000:
        out.write(bytes([(n & 0x7F) | 0x80, n >> 7]))
    else:
        raise ValueError(f"string too long for a pascal string: {n}")
    out.write(s)


# -------------------------------------------------------------------- STB I/O
# STB1 layout (see src/lib_util/src/classstb.cpp and src/common/src/io/stb.cpp):
#     "STB1" | u32 data_offset | u32 raw_rows | u32 raw_cols
#     ...header region (row height, column widths, column names, row names)...
#     at data_offset: (raw_rows-1) x (raw_cols-1) u16-length strings
# Game indices drop the header row and root column, so data[r][c] IS get_int32(r,c).
# The header region is treated opaquely: row labels are the last thing in it, so
# appending rows means inserting label strings immediately before data_offset.
class Stb:
    def __init__(self, path):
        self.path = path
        raw = open(path, "rb").read()
        if raw[:4] != b"STB1":
            raise SystemExit(f"{path}: not an STB1 file")
        self.data_offset, raw_rows, raw_cols = struct.unpack_from("<III", raw, 4)
        self.rows, self.cols = raw_rows - 1, raw_cols - 1
        self.header = raw[16:self.data_offset]
        f = io.BytesIO(raw)
        f.seek(self.data_offset)

        def cell():
            n, = struct.unpack("<H", f.read(2))
            return f.read(n)

        self.d = [[cell() for _ in range(self.cols)] for _ in range(self.rows)]
        self.tail = f.read()          # editors sometimes leave junk; preserved

    def get(self, r, c):
        return self.d[r][c] if r < self.rows and c < self.cols else b""

    def set(self, r, c, v):
        if isinstance(v, str):
            v = v.encode("latin-1")
        self.d[r][c] = v

    def occupied(self, r):
        return r < self.rows and any(x.strip() for x in self.d[r])

    def grow_to(self, game_rows, labels=None):
        """Extend to `game_rows`, appending blank rows and their row labels."""
        add = game_rows - self.rows
        if add <= 0:
            return 0
        extra = io.BytesIO()
        for i in range(add):
            lbl = b""
            if labels:
                lbl = labels.get(self.rows + i, b"")
                if isinstance(lbl, str):
                    lbl = lbl.encode("latin-1")
            extra.write(struct.pack("<H", len(lbl)))
            extra.write(lbl)
        self.header += extra.getvalue()
        self.d.extend([[b"" for _ in range(self.cols)] for _ in range(add)])
        self.rows = game_rows
        return add

    def to_bytes(self):
        out = io.BytesIO()
        out.write(b"STB1")
        out.write(struct.pack("<III", 16 + len(self.header), self.rows + 1, self.cols + 1))
        out.write(self.header)
        for row in self.d:
            for cell in row:
                out.write(struct.pack("<H", len(cell)))
                out.write(cell)
        out.write(self.tail)
        return out.getvalue()

    def save(self, dry):
        blob = self.to_bytes()
        if dry:
            return
        backup(self.path)
        with open(self.path, "wb") as fh:
            fh.write(blob)


# -------------------------------------------------------------------- STL I/O
# ITST01/NRST01/QEST01: pstr format tag | u32 key_count | key_count x (pstr, u32)
# | u32 lang_count | lang_count x u32 block_offset. Each block: key_count x u32
# entry offsets, then the entries. ITST01 entries are (name, desc); NRST01 is name
# only; QEST01 adds two more strings. The client ignores the per-entry offset
# table and reads sequentially, so entries must stay in key order.
STL_FIELDS = {b"NRST01": 1, b"ITST01": 2, b"QEST01": 4}


class Stl:
    def __init__(self, path):
        self.path = path
        buf = open(path, "rb").read()
        self.fmt, o = read_pstr(buf, 0)
        if self.fmt not in STL_FIELDS:
            raise SystemExit(f"{path}: unknown STL format {self.fmt!r}")
        nfields = STL_FIELDS[self.fmt]
        count, = struct.unpack_from("<I", buf, o); o += 4
        self.keys = []
        for _ in range(count):
            k, o = read_pstr(buf, o)
            i, = struct.unpack_from("<I", buf, o); o += 4
            self.keys.append((k, i))
        nlang, = struct.unpack_from("<I", buf, o); o += 4
        lang_off = struct.unpack_from(f"<{nlang}I", buf, o)
        self.langs = []
        for base in lang_off:
            entry_off = struct.unpack_from(f"<{count}I", buf, base)
            rows = []
            for eo in entry_off:
                fields, p = [], eo
                for _ in range(nfields):
                    s, p = read_pstr(buf, p)
                    fields.append(s)
                rows.append(fields)
            self.langs.append(rows)

    def has(self, key):
        k = key.encode("latin-1") if isinstance(key, str) else key
        return any(x == k for x, _ in self.keys)

    def append(self, key, idx, name):
        """Append one entry with the same English text in every language block."""
        k = key.encode("latin-1")
        n = name.encode("latin-1")
        self.keys.append((k, idx))
        nfields = STL_FIELDS[self.fmt]
        for rows in self.langs:
            rows.append([n] + [b""] * (nfields - 1))

    def to_bytes(self):
        out = io.BytesIO()
        write_pstr(out, self.fmt)
        out.write(struct.pack("<I", len(self.keys)))
        for k, i in self.keys:
            write_pstr(out, k)
            out.write(struct.pack("<I", i))
        out.write(struct.pack("<I", len(self.langs)))
        lang_off_pos = out.tell()
        out.write(b"\0" * (4 * len(self.langs)))
        lang_offsets = []
        for rows in self.langs:
            lang_offsets.append(out.tell())
            entry_off_pos = out.tell()
            out.write(b"\0" * (4 * len(self.keys)))
            entry_offsets = []
            for fields in rows:
                entry_offsets.append(out.tell())
                for s in fields:
                    write_pstr(out, s)
            end = out.tell()
            out.seek(entry_off_pos)
            out.write(struct.pack(f"<{len(self.keys)}I", *entry_offsets))
            out.seek(end)
        end = out.tell()
        out.seek(lang_off_pos)
        out.write(struct.pack(f"<{len(self.langs)}I", *lang_offsets))
        out.seek(end)
        return out.getvalue()

    def save(self, dry):
        blob = self.to_bytes()
        if dry:
            return
        backup(self.path)
        with open(self.path, "wb") as fh:
            fh.write(blob)


# -------------------------------------------------------------------- IFO I/O
# i32 lump_count | lump_count x (i32 type, i32 absolute_offset) | blocks laid out
# contiguously in table order. Object record shared by every object lump:
#   u8 name_len + name | i16 warp_id | i16 event_id | i32 obj_type | i32 obj_id
#   | i32 map_x | i32 map_y | 16B quat | 12B pos | 12B scale
# Per-lump extras: MOB adds i32 AI + pascal .CON name; REGEN adds a point name,
# two mob lists and four i32s; EVENT_OBJECT adds two pascal strings. Undecoded
# lumps are copied byte-for-byte, so they cannot be corrupted.
OBJ_FIXED = 2 + 2 + 4 + 4 + 4 + 4 + 16 + 12 + 12


class IfoReader:
    def __init__(self, buf, o=0):
        self.b, self.o = buf, o

    def i32(self):
        v, = struct.unpack_from("<i", self.b, self.o); self.o += 4; return v

    def u8(self):
        v = self.b[self.o]; self.o += 1; return v

    def take(self, n):
        s = self.b[self.o:self.o + n]
        if len(s) != n:
            raise ValueError("truncated .IFO")
        self.o += n
        return s

    def bstr(self):
        """u8 length + bytes -- what ReadByte + Read/Seek does."""
        return self.take(self.u8())

    def vstr(self):
        """7-bit varint length + bytes -- what CFileSystem::ReadPascalString does.

        Only LUMP_TERRAIN_EVENT_OBJECT uses this; the object name, the MOB lump's
        .CON filename and every string inside a REGEN record are plain u8. Reading
        an event-object trigger as u8 silently under-consumes the lump.
        """
        b = self.u8()
        if b & 0x80:
            b = ((self.u8() << 7) | (b - 0x80))
        return self.take(b)


def put_bstr(s):
    if len(s) > 255:
        raise ValueError("name too long")
    return bytes([len(s)]) + s


def parse_object_lump(buf, start, end, lump_type, exact=True):
    """Decode an object lump. Returns (objects, trailing_bytes).

    Every lump but the last consumes its block exactly, and `exact` makes a
    shortfall an error there -- that is how a misread field gets caught rather
    than silently absorbed. The *final* lump's block runs to EOF and RoseZA's map
    editor leaves junk after it (up to ~900 bytes, sometimes recognisable regen
    text), so its remainder is captured and re-emitted verbatim instead. The game
    reads the record count and stops, so those bytes are dead either way.
    """
    r = IfoReader(buf, start)
    count = r.i32()
    objs = []
    for _ in range(count):
        name = r.bstr()
        fixed = r.take(OBJ_FIXED)
        warp_id, event_id = struct.unpack_from("<hh", fixed, 0)
        obj_id, = struct.unpack_from("<i", fixed, 8)
        raw_start = r.o
        if lump_type == LUMP_MOB:
            r.take(4)                       # i32 AI
            r.bstr()                        # .CON file
        elif lump_type == LUMP_EVENT_OBJECT:
            r.vstr(); r.vstr()          # trigger name, .CON file
        elif lump_type == LUMP_EFFECT:
            r.bstr()                        # .eft file
        elif lump_type == LUMP_SOUND:
            r.bstr(); r.i32(); r.i32()      # .wav file, range (cm), interval (sec)
        elif lump_type == LUMP_REGEN:
            r.bstr()                        # regen point name
            for _ in range(2):              # basic list, then tactics list
                for _ in range(r.i32()):
                    r.bstr(); r.i32(); r.i32()
            r.take(16)                      # interval, limit, range, tactics point
        objs.append(dict(name=name, fixed=fixed, extra=buf[raw_start:r.o],
                         warp_id=warp_id, event_id=event_id, obj_id=obj_id))
    if r.o > end or (exact and r.o != end):
        raise ValueError(f"lump {lump_type}: parsed to {r.o}, block ends at {end}")
    return objs, buf[r.o:end]


def build_object_lump(objs, trailing=b""):
    out = [struct.pack("<i", len(objs))]
    for o in objs:
        out.append(put_bstr(o["name"]))
        out.append(o["fixed"])
        out.append(o["extra"])
    out.append(trailing)
    return b"".join(out)


def read_ifo(path):
    buf = open(path, "rb").read()
    r = IfoReader(buf)
    n = r.i32()
    lumps = [(r.i32(), r.i32()) for _ in range(n)]
    header_end = r.o
    offsets = [o for _, o in lumps]
    if offsets != sorted(offsets):
        raise ValueError(f"{path}: lump offsets are not in table order")
    if offsets and offsets[0] != header_end:
        raise ValueError(f"{path}: first lump at {offsets[0]}, header ends at {header_end}")
    bounds = [(t, off, lumps[i + 1][1] if i + 1 < n else len(buf))
              for i, (t, off) in enumerate(lumps)]
    return buf, bounds


def build_ifo(bounds, buf, replacements):
    """replacements: {lump_type: raw block bytes}. Lump table is preserved."""
    blocks = [replacements.get(t, buf[off:end]) for t, off, end in bounds]
    n = len(bounds)
    out = [struct.pack("<i", n)]
    cur = 4 + 8 * n
    for (t, _, _), blk in zip(bounds, blocks):
        out.append(struct.pack("<ii", t, cur))
        cur += len(blk)
    out.extend(blocks)
    return b"".join(out)


def lump_block(bounds, lump_type):
    for t, off, end in bounds:
        if t == lump_type:
            return off, end
    return None, None


def read_lump(buf, bounds, lump_type):
    """(objects, trailing) for a lump, or (None, None) if the file has no such lump."""
    off, end = lump_block(bounds, lump_type)
    if off is None:
        return None, None
    return parse_object_lump(buf, off, end, lump_type,
                             exact=(lump_type != bounds[-1][0]))


# -------------------------------------------------------------------- ZSC I/O
# u16 mesh_count + NUL-terminated paths | u16 material_count + (path, 36B flags)
# | u16 effect_count + paths | u16 object_count + objects. Each object:
#   12B cylinder | u16 part_count | parts | u16 dummy_count | dummies | 24B bbox
# A part is (u16 mesh, u16 material, proplist); a dummy is (4B, proplist); a
# proplist is (u8 type, u8 len, bytes)* terminated by a type byte of 0. An object
# with zero parts ends right there -- no dummies, no bbox (CMODEL::Load returns
# early), which is why the part count is tested before reading further.
class Zsc:
    def __init__(self, path):
        self.path = path
        self.d = open(path, "rb").read()
        f = self.f = io.BytesIO(self.d)
        self.meshes, self.materials, self.effects, self.objects = [], [], [], []
        for _ in range(self.u16()):
            self.meshes.append(self.cstr())
        self.mesh_end = f.tell()
        for _ in range(self.u16()):
            self.materials.append((self.cstr(), f.read(36)))
        self.mat_end = f.tell()
        for _ in range(self.u16()):
            self.effects.append(self.cstr())
        self.objcnt_pos = f.tell()
        for _ in range(self.u16()):
            cyl = f.read(12)
            nparts = self.u16()
            parts, dummies, bb = [], [], b""
            if nparts > 0:
                for _ in range(nparts):
                    parts.append((self.u16(), self.u16(), self.props()))
                for _ in range(self.u16()):
                    dummies.append((f.read(4), self.props()))
                bb = f.read(24)
            self.objects.append((cyl, parts, dummies, bb))
        self.obj_end = f.tell()

    def u16(self):
        return struct.unpack("<H", self.f.read(2))[0]

    def cstr(self):
        out = b""
        while True:
            c = self.f.read(1)
            if c in (b"\x00", b""):
                break
            out += c
        return out

    def props(self):
        start = self.f.tell()
        while True:
            t = self.f.read(1)[0]
            if t == 0:
                break
            self.f.read(self.f.read(1)[0])
        return self.d[start:self.f.tell()]

    def object_bytes(self, cyl, parts, dummies, bb):
        out = [cyl, struct.pack("<H", len(parts))]
        for mid, tid, props in parts:
            out.append(struct.pack("<HH", mid, tid))
            out.append(props)
        if parts:
            out.append(struct.pack("<H", len(dummies)))
            for a, props in dummies:
                out += [a, props]
            out.append(bb)
        return b"".join(out)

    def to_bytes(self, extra_meshes=(), extra_mats=(), extra_objects=()):
        out = [struct.pack("<H", len(self.meshes) + len(extra_meshes)),
               self.d[2:self.mesh_end]]
        out += [m + b"\x00" for m in extra_meshes]
        out += [struct.pack("<H", len(self.materials) + len(extra_mats)),
                self.d[self.mesh_end + 2:self.mat_end]]
        out += [p + b"\x00" + flags for p, flags in extra_mats]
        out += [self.d[self.mat_end:self.objcnt_pos],
                struct.pack("<H", len(self.objects) + len(extra_objects)),
                self.d[self.objcnt_pos + 2:self.obj_end]]
        out += list(extra_objects)
        return b"".join(out)


# A ZSC part property is (u8 tag, u8 len, bytes) terminated by a zero tag. Tags
# SWITCH_ANI..SWITCH_ANI+MAX_MESH_ANI_TYPE-1 (8..28) and SWITCH_CNST_ANI (30) hold
# *file paths* -- .zmo motions for animated scenery such as the Muris carts. See
# CFixedPART::Load and CCharPART::Load in src/client/io_model.cpp. Collecting only
# meshes and materials leaves those behind, and the client pops a modal "open error"
# box for each one during map load, then dies.
SWITCH_ANI, MAX_MESH_ANI_TYPE = 8, 21
SWITCH_CNST_ANI = SWITCH_ANI + MAX_MESH_ANI_TYPE + 1                # 30
PROP_PATH_TAGS = set(range(SWITCH_ANI, SWITCH_ANI + MAX_MESH_ANI_TYPE)) | {SWITCH_CNST_ANI}


def prop_paths(props):
    out, o = [], 0
    while o < len(props):
        tag = props[o]
        o += 1
        if tag == 0:
            break
        ln = props[o]
        o += 1
        val, o = props[o:o + ln], o + ln
        if tag in PROP_PATH_TAGS and ln:
            out.append(val.rstrip(NUL).decode("latin-1"))
    return out


def zsc_asset_refs(z):
    """Every file a ZSC table names: meshes, materials, effects, part motions.

    The effect list doubles as a *light* list -- entries not starting with "3D" are
    light names rather than files, and the client filters on exactly that prefix.
    """
    out = {m.decode("latin-1") for m in z.meshes}
    out |= {p.decode("latin-1") for p, _ in z.materials}
    out |= {e.decode("latin-1") for e in z.effects if e[:2].upper() == b"3D"}
    for cyl, parts, dummies, bb in z.objects:
        for _, _, props in parts:
            out |= set(prop_paths(props))
        for _, props in dummies:
            out |= set(prop_paths(props))
    return {x for x in out if "\\" in x or "/" in x}


def effect_chain(efts, src):
    """.eft -> .PTL -> particle texture, transitively.

    Both are length-prefixed binary; the paths are recovered by pattern rather than
    with a full parser because these two fields are all we need and the files are a
    few hundred bytes each. A .PTL names its textures bare, so those resolve against
    the shared particle texture directory.
    """
    out, queue, seen = set(), list(efts), set()
    while queue:
        rel = queue.pop()
        if rel.lower() in seen:
            continue
        seen.add(rel.lower())
        out.add(rel)
        p = rel_path(src, rel)
        if not os.path.isfile(p):
            continue
        blob = open(p, "rb").read()
        for hit in ABS_ASSET_RE.finditer(blob):
            queue.append(hit.group().decode("latin-1"))
        if rel.lower().endswith(".ptl"):
            for hit in BARE_TEXTURE_RE.finditer(blob):
                queue.append(os.path.join(PARTICLE_TEXTURE_DIR,
                                          hit.group().decode("latin-1")))
    return out


# -------------------------------------------------------------------- CHR I/O
# u16 n + NUL-terminated skeleton paths | same for motions | same for effects
# | u16 char_count, then per character: u8 active (0 = hole, nothing follows),
#   u16 skeleton, NUL-terminated name, u16 n + n x u16 PART_NPC.ZSC model index,
#   u16 n + n x (u16 anim type, u16 motion index),
#   u16 n + n x (u16 anim type, u16 effect index).
# NOTE: the file carries ~2 KB past the declared character count -- dead entries
# the client never reads. It is preserved verbatim; dropping it is untested.
class Chr:
    def __init__(self, path):
        self.path = path
        self.d = open(path, "rb").read()
        f = self.f = io.BytesIO(self.d)
        self.skeletons = [self.cstr() for _ in range(self.u16())]
        self.motions = [self.cstr() for _ in range(self.u16())]
        self.effects = [self.cstr() for _ in range(self.u16())]
        self.chars = []
        for _ in range(self.u16()):
            if not f.read(1)[0]:
                self.chars.append(None)
                continue
            skel = self.u16()
            name = self.cstr()
            models = [self.u16() for _ in range(self.u16())]
            anims = [(self.u16(), self.u16()) for _ in range(self.u16())]
            effs = [(self.u16(), self.u16()) for _ in range(self.u16())]
            self.chars.append(dict(skel=skel, name=name, models=models,
                                   anims=anims, effects=effs))
        self.tail = f.read()

    def u16(self):
        return struct.unpack("<H", self.f.read(2))[0]

    def cstr(self):
        out = b""
        while True:
            c = self.f.read(1)
            if c in (b"\x00", b""):
                break
            out += c
        return out

    def to_bytes(self):
        out = io.BytesIO()

        def strlist(items):
            out.write(struct.pack("<H", len(items)))
            for s in items:
                out.write(s + b"\x00")

        strlist(self.skeletons)
        strlist(self.motions)
        strlist(self.effects)
        out.write(struct.pack("<H", len(self.chars)))
        for c in self.chars:
            if c is None:
                out.write(b"\x00")
                continue
            out.write(b"\x01")
            out.write(struct.pack("<H", c["skel"]))
            out.write(c["name"] + b"\x00")
            out.write(struct.pack("<H", len(c["models"])))
            for m in c["models"]:
                out.write(struct.pack("<H", m))
            out.write(struct.pack("<H", len(c["anims"])))
            for t, a in c["anims"]:
                out.write(struct.pack("<HH", t, a))
            out.write(struct.pack("<H", len(c["effects"])))
            for t, e in c["effects"]:
                out.write(struct.pack("<HH", t, e))
        out.write(self.tail)
        return out.getvalue()

    def save(self, dry):
        blob = self.to_bytes()
        if dry:
            return
        backup(self.path)
        with open(self.path, "wb") as fh:
            fh.write(blob)


# ------------------------------------------------------------------- helpers
def backup(path):
    if not os.path.exists(path + ".bak"):
        shutil.copyfile(path, path + ".bak")


def norm(p):
    if isinstance(p, bytes):
        p = p.decode("latin-1")
    return p.replace("\\", "/").lower()


def rel_path(root, rel):
    return os.path.join(root, rel.replace("\\", "/"))


def copy_files(rels, src_root, dst_root, dry, label):
    """Copy data-relative paths that we do not already have. Returns (n, bytes)."""
    n, total, missing = 0, 0, []
    for rel in sorted(set(rels)):
        s = rel_path(src_root, rel)
        d = rel_path(dst_root, rel)
        if not os.path.isfile(s):
            missing.append(rel)
            continue
        if os.path.isfile(d):
            continue
        n += 1
        total += os.path.getsize(s)
        if not dry:
            os.makedirs(os.path.dirname(d), exist_ok=True)
            shutil.copyfile(s, d)
    print(f"    {label:26s} {n:5d} new files  {total / 1048576:7.2f} MB"
          + (f"   ({len(missing)} not in source)" if missing else ""))
    for m in missing[:5]:
        print(f"        missing from source: {m}")
    return n, total


# ------------------------------------------------------------------ selftest
def selftest(ours, src):
    """Every writer must reproduce its input byte-for-byte with no edits."""
    ok = True

    def check(label, path, blob):
        nonlocal ok
        same = blob == open(path, "rb").read()
        ok = ok and same
        print(f"    {label:34s} {'OK' if same else 'FAIL'}   {os.path.basename(path)}")

    for rel in (r"3DDATA\STB\LIST_ZONE.STB", r"3DDATA\STB\LIST_NPC.STB",
                r"3DDATA\STB\FILE_AI.STB", r"3DDATA\STB\WARP.STB"):
        p = rel_path(ours, rel)
        check("STB round-trip", p, Stb(p).to_bytes())

    for rel in (r"3DDATA\STB\LIST_ZONE_S.STL", r"3DDATA\STB\LIST_NPC_S.STL"):
        p = rel_path(ours, rel)
        check("STL round-trip", p, Stl(p).to_bytes())

    p = rel_path(ours, NPC_CHR_REL)
    check("CHR round-trip", p, Chr(p).to_bytes())

    p = rel_path(ours, PART_NPC_ZSC_REL)
    check("ZSC round-trip", p, Zsc(p).to_bytes())

    # IFO: container rebuild and per-lump decode, over a real Oro map and one of ours
    probes = []
    src_maps = rel_path(src, MAPS_REL)
    for zone in ("TOWN", "ODD01", "ODRP01"):
        d = os.path.join(src_maps, zone)
        if os.path.isdir(d):
            probes += [os.path.join(d, f) for f in sorted(os.listdir(d))
                       if f.lower().endswith(".ifo")][:4]
    ours_maps = rel_path(ours, r"3DDATA\MAPS\ELDEON\EJT01")
    if os.path.isdir(ours_maps):
        probes += [os.path.join(ours_maps, f) for f in sorted(os.listdir(ours_maps))
                   if f.lower().endswith(".ifo")][:3]
    cont_ok = lump_ok = True
    for p in probes:
        buf, bounds = read_ifo(p)
        cont_ok = cont_ok and build_ifo(bounds, buf, {}) == buf
        for lt in (LUMP_OBJECT, LUMP_MOB, LUMP_SOUND, LUMP_EFFECT, LUMP_REGEN,
                   LUMP_WARP, LUMP_COLLISION, LUMP_EVENT_OBJECT):
            off, end = lump_block(bounds, lt)
            if off is None:
                continue
            objs, trailing = read_lump(buf, bounds, lt)
            if build_object_lump(objs, trailing) != buf[off:end]:
                lump_ok = False
                print(f"        lump {lt} round-trip FAILED in {p}")
    ok = ok and cont_ok and lump_ok
    print(f"    {'IFO container rebuild':34s} {'OK' if cont_ok else 'FAIL'}   "
          f"({len(probes)} files)")
    print(f"    {'IFO lump decode round-trip':34s} {'OK' if lump_ok else 'FAIL'}")
    return ok


# ------------------------------------------------------- stage 1: empty zones
def stage1(ours, src, dry):
    print("stage 1 -- terrain, art, zone rows, zone names")

    src_maps, dst_maps = rel_path(src, MAPS_REL), rel_path(ours, MAPS_REL)
    src_zone = Stb(rel_path(src, r"3DDATA\STB\LIST_ZONE.STB"))

    # --- 1a. maps, with the entity lumps emptied
    copied = rewritten = 0
    total = 0
    for row, folder, _, _ in ZONES:
        s, d = os.path.join(src_maps, folder), os.path.join(dst_maps, folder)
        if not os.path.isdir(s):
            raise SystemExit(f"source map folder missing: {s}")
        # recursive: each map has a <map>/LIGHTMAP subdirectory the client loads
        # separately (CTERRAIN::LoadLightMapINFO), so a flat copy loses lighting
        for base, _, names in os.walk(s):
            sub = os.path.relpath(base, s)
            dbase = d if sub == "." else os.path.join(d, sub)
            for name in sorted(names):
                sp, dp = os.path.join(base, name), os.path.join(dbase, name)
                if os.path.isfile(dp):
                    continue
                if name.lower().endswith(".ifo"):
                    buf, bounds = read_ifo(sp)
                    repl = {}
                    for lt in LUMPS_STAGE1_EMPTY:
                        off, end = lump_block(bounds, lt)
                        if off is None or buf[off:off + 4] == b"\0\0\0\0":
                            continue
                        _, trailing = read_lump(buf, bounds, lt)
                        repl[lt] = build_object_lump([], trailing)
                    blob = build_ifo(bounds, buf, repl) if repl else buf
                    rewritten += 1 if repl else 0
                    if not dry:
                        os.makedirs(dbase, exist_ok=True)
                        with open(dp, "wb") as fh:
                            fh.write(blob)
                else:
                    if not dry:
                        os.makedirs(dbase, exist_ok=True)
                        shutil.copyfile(sp, dp)
                copied += 1
                total += os.path.getsize(sp)
    print(f"    {'map files':26s} {copied:5d} new files  {total / 1048576:7.2f} MB"
          f"   ({rewritten} .IFO with lumps emptied)")

    # --- 1b. terrain tiles referenced by the .ZON tile lump
    tiles = set()
    for _, folder, _, _ in ZONES:
        d = os.path.join(src_maps, folder)
        zon = [f for f in os.listdir(d) if f.lower().endswith(".zon")][0]
        tiles |= set(zon_tiles(os.path.join(d, zon)))
    tiles = {t for t in tiles if "\\" in t or "/" in t}
    copy_files(tiles, src, ours, dry, "terrain tiles")

    # --- 1c. object tables and every file they reference
    art = set()
    for rel in ZSC_TABLES:
        p = rel_path(src, rel)
        if not os.path.isfile(p):
            raise SystemExit(f"source object table missing: {p}")
        art |= zsc_asset_refs(Zsc(p))
    # ...plus the files the .IFO records name inline. The EFFECT and SOUND lumps
    # each carry a filename in the record itself rather than an index into a table,
    # so they are invisible to any collection that only walks the ZSC tables.
    for _, folder, _, _ in ZONES:
        d = os.path.join(src_maps, folder)
        for name in sorted(os.listdir(d)):
            if not name.lower().endswith(".ifo"):
                continue
            buf, bounds = read_ifo(os.path.join(d, name))
            for lt in (LUMP_EFFECT, LUMP_SOUND):
                objs, _ = read_lump(buf, bounds, lt)
                for o in objs or []:
                    n = o["extra"][0]
                    art.add(o["extra"][1:1 + n].decode("latin-1"))
    art = {a for a in art if "\\" in a or "/" in a}
    efts = {a for a in art if a.lower().endswith(".eft")}
    art |= effect_chain(efts, src)
    copy_files(ZSC_TABLES, src, ours, dry, "object tables")
    copy_files(art, src, ours, dry, "deco/cnst art")

    # --- 1d. the sky
    src_sky = Stb(rel_path(src, SKY_STB_REL))
    our_sky = Stb(rel_path(ours, SKY_STB_REL))
    our_sky.grow_to(ORO_SKY_ROW + 1)
    if our_sky.d[ORO_SKY_ROW] != src_sky.d[ORO_SKY_ROW][:our_sky.cols]:
        for c in range(min(our_sky.cols, src_sky.cols)):
            our_sky.set(ORO_SKY_ROW, c, src_sky.get(ORO_SKY_ROW, c))
        our_sky.save(dry)
        print(f"    {'LIST_SKY.STB':26s} row {ORO_SKY_ROW} written "
              f"({our_sky.get(ORO_SKY_ROW, 0).decode('latin-1')})")
    else:
        print(f"    {'LIST_SKY.STB':26s} row {ORO_SKY_ROW} already present")
    sky = {our_sky.get(ORO_SKY_ROW, c).decode("latin-1") for c in range(5)}
    copy_files({x for x in sky if "\\" in x or "/" in x}, src, ours, dry, "sky assets")

    # --- 1e. zone rows
    zstb = Stb(rel_path(ours, r"3DDATA\STB\LIST_ZONE.STB"))
    added = zstb.grow_to(MAX_ZONE_ROW + 1,
                         labels={r: n for r, _, n, _ in ZONES})
    changed = 0
    for row, folder, name, revive in ZONES:
        before = list(zstb.d[row])
        for c in range(ZONE_COPY_COLS):
            zstb.set(row, c, src_zone.get(row, c))
        zstb.set(row, 0, name)          # drop RoseZA's "(CODE) " prefix
        zstb.set(row, 3, revive)        # the event position that actually exists
        for c, v in zip(ZONE_TRIGGER_COLS, ZONE_TRIGGERS):
            zstb.set(row, c, v)         # RoseZA's PvP hooks are in QSDs we lack
        if zstb.d[row] != before:
            changed += 1
    print(f"    {'LIST_ZONE.STB':26s} +{added} rows (now {zstb.rows}), "
          f"{changed} Oro rows written")
    zstb.save(dry)

    # --- 1f. zone names
    zstl = Stl(rel_path(ours, r"3DDATA\STB\LIST_ZONE_S.STL"))
    n = 0
    for key in sorted(ZONE_STRING_NAMES):
        if zstl.has(key):
            continue
        zstl.append(key, int(key[4:]), ZONE_STRING_NAMES[key])
        n += 1
    print(f"    {'LIST_ZONE_S.STL':26s} +{n} keys (now {len(zstl.keys)})")
    if n:
        zstl.save(dry)

    print("    NOTE: BGM lives outside data/ -- copy Sound/BGM/Orlo_Crash_Site.ogg,")
    print("          Orlo_Golden_Ring.ogg and Orlo_Portal.ogg into the deployed game")
    print("          dir if you want music (a missing track is silence, not an error).")


def zon_tiles(path):
    """LUMP_ZONE_TILE (2): i32 count, then count x (u8 len + path)."""
    d = open(path, "rb").read()
    cnt, = struct.unpack_from("<i", d, 0)
    for i in range(cnt):
        t, off = struct.unpack_from("<ii", d, 4 + 8 * i)
        if t != 2:
            continue
        o = off
        n, = struct.unpack_from("<i", d, o); o += 4
        out = []
        for _ in range(n):
            ln = d[o]; o += 1
            out.append(d[o:o + ln].rstrip(b"\0").decode("latin-1")); o += ln
        return out
    return []


# ------------------------------------------------------------ stage 2: warps
def stage2(ours, src, dry):
    print("stage 2 -- internal warp gates")

    src_warp = Stb(rel_path(src, r"3DDATA\STB\WARP.STB"))
    our_warp = Stb(rel_path(ours, r"3DDATA\STB\WARP.STB"))
    imported_zones = {row for row, _, _, _ in ZONES}
    folder_of = {row: folder for row, folder, _, _ in ZONES}

    src_maps, dst_maps = rel_path(src, MAPS_REL), rel_path(ours, MAPS_REL)

    # every warp id actually placed as a gate inside an Oro map
    placed = {}
    for row, folder, _, _ in ZONES:
        d = os.path.join(src_maps, folder)
        for name in sorted(os.listdir(d)):
            if not name.lower().endswith(".ifo"):
                continue
            buf, bounds = read_ifo(os.path.join(d, name))
            off, end = lump_block(bounds, LUMP_WARP)
            if off is None:
                continue
            objs, _ = read_lump(buf, bounds, LUMP_WARP)
            for o in objs:
                placed.setdefault(o["warp_id"], []).append((folder, name))

    # a gate is only safe if its destination zone is one we actually imported
    usable, skipped = {}, {}
    for wid, where in sorted(placed.items()):
        dest = src_warp.get(wid, 1).strip()
        dest = int(dest) if dest.isdigit() else -1
        (usable if dest in imported_zones else skipped)[wid] = (dest, where)
    for wid, (dest, where) in skipped.items():
        print(f"    skip warp {wid:4d} -> zone {dest} (not imported): "
              f"{sorted({f for f, _ in where})}")

    # --- 2a. WARP.STB rows
    rowchanges = 0
    for wid, (dest, _) in sorted(usable.items()):
        if wid >= our_warp.rows:
            raise SystemExit(f"warp id {wid} beyond our WARP.STB ({our_warp.rows} rows)")
        before = list(our_warp.d[wid])
        for c in range(min(our_warp.cols, src_warp.cols)):
            our_warp.set(wid, c, src_warp.get(wid, c))
        if our_warp.d[wid] != before:
            rowchanges += 1
            if any(x.strip() for x in before):
                print(f"    warp {wid:4d} overwrote dead row "
                      f"{before[0].decode('latin-1')!r} -> "
                      f"{our_warp.get(wid, 0).decode('latin-1')!r}")
    print(f"    {'WARP.STB':26s} {rowchanges} rows written ({len(usable)} gates usable)")

    # --- 2b. verify every destination event position exists, byte for byte
    bad = []
    for wid, (dest, _) in sorted(usable.items()):
        want = src_warp.get(wid, 2)
        d = os.path.join(dst_maps, folder_of[dest])
        if not os.path.isdir(d):
            bad.append((wid, dest, want, "zone folder not imported")); continue
        zon = [f for f in os.listdir(d) if f.lower().endswith(".zon")][0]
        names = [n for n, _ in zon_events(os.path.join(d, zon))]
        if want not in names:
            bad.append((wid, dest, want, f"not in {zon} ({len(names)} events)"))
    if bad:
        for wid, dest, want, why in bad:
            print(f"    !! warp {wid} -> zone {dest} event {want!r}: {why}")
        raise SystemExit("destination event positions missing -- refusing to write "
                         "(this is what makes the server drop the client as IS_HACKING)")
    print(f"    {'event positions':26s} all {len(usable)} resolve byte-for-byte")
    our_warp.save(dry)

    # --- 2c. put the gate objects back into our .IFO copies
    files, gates = 0, 0
    for row, folder, _, _ in ZONES:
        s, d = os.path.join(src_maps, folder), os.path.join(dst_maps, folder)
        if not os.path.isdir(d):
            raise SystemExit(f"{d}: run --stage 1 first")
        for name in sorted(os.listdir(s)):
            if not name.lower().endswith(".ifo"):
                continue
            sbuf, sbounds = read_ifo(os.path.join(s, name))
            soff, send = lump_block(sbounds, LUMP_WARP)
            if soff is None:
                continue
            sobjs, _ = read_lump(sbuf, sbounds, LUMP_WARP)
            keep = [o for o in sobjs if o["warp_id"] in usable]
            if not keep:
                continue
            dp = os.path.join(d, name)
            dbuf, dbounds = read_ifo(dp)
            doff, dend = lump_block(dbounds, LUMP_WARP)
            if doff is None:
                raise SystemExit(f"{dp}: no WARP lump to fill")
            have, dtrail = read_lump(dbuf, dbounds, LUMP_WARP)
            if len(have) == len(keep):
                continue                       # already restored
            blob = build_ifo(dbounds, dbuf,
                             {LUMP_WARP: build_object_lump(keep, dtrail)})
            files += 1
            gates += len(keep)
            if not dry:
                with open(dp, "wb") as fh:
                    fh.write(blob)
            # verify through the same decoder
            if not dry:
                vbuf, vbounds = read_ifo(dp)
                voff, vend = lump_block(vbounds, LUMP_WARP)
                vobjs, _ = read_lump(vbuf, vbounds, LUMP_WARP)
                got = {o["warp_id"] for o in vobjs}
                if got != {o["warp_id"] for o in keep}:
                    raise SystemExit(f"VERIFY FAILED: {dp} warp ids {sorted(got)}")
    print(f"    {'IFO warp lumps':26s} {gates} gates into {files} files")


def zon_events(path):
    """LUMP_EVENT_OBJECT (1) of a .ZON: i32 count, then 3 floats + u8 len + name."""
    d = open(path, "rb").read()
    cnt, = struct.unpack_from("<i", d, 0)
    for i in range(cnt):
        t, off = struct.unpack_from("<ii", d, 4 + 8 * i)
        if t != 1:
            continue
        o = off
        n, = struct.unpack_from("<i", d, o); o += 4
        out = []
        for _ in range(n):
            pos = struct.unpack_from("<3f", d, o); o += 12
            ln = d[o]; o += 1
            out.append((d[o:o + ln], pos)); o += ln
        return out
    return []


# --------------------------------------------------------- stage 3: monsters
LEVEL_PREFIX = re.compile(r"^\(\d+\)\s*")
DEFAULT_PVP_STATE = b"3"           # PvpState::All -- what 731 of our 743 monsters use


def clean_npc_name(raw):
    """RoseZA prefixes monster names with their level: '(201) Baby Desert Asper'."""
    return LEVEL_PREFIX.sub("", raw.decode("latin-1")).strip()


class StringPool:
    """Maps a source index to ours, interning on demand.

    Lazily, deliberately: interning every source string would append paths for
    skeletons and motions no imported monster references, and the asset copy only
    covers what is referenced -- so the table would carry entries pointing at files
    that are not there. Resolving on first use keeps the two in step by
    construction, and `used` is exactly the set of files to copy.
    """

    def __init__(self, our_list, src_list):
        self.ours, self.src = our_list, src_list
        self.have = {norm(s): i for i, s in enumerate(our_list)}
        self.used = set()

    def __getitem__(self, src_idx):
        s = self.src[src_idx]
        self.used.add(s.decode("latin-1"))
        k = norm(s)
        if k not in self.have:
            self.have[k] = len(self.ours)
            self.ours.append(s)
        return self.have[k]


def regen_mob_ids(extra):
    """The two mob lists inside a REGEN record's per-object extra bytes."""
    o = 0
    n = extra[o]
    o += 1 + n                                     # regen point name
    ids = []
    for _ in range(2):                             # basic list, then tactics list
        cnt, = struct.unpack_from("<i", extra, o)
        o += 4
        for _ in range(cnt):
            ln = extra[o]
            o += 1 + ln
            idx, c = struct.unpack_from("<ii", extra, o)
            o += 8
            if idx > 0 and c > 0:
                ids.append(idx)
    return ids


def stage3(ours, src, dry):
    print("stage 3 -- monsters")

    src_maps, dst_maps = rel_path(src, MAPS_REL), rel_path(ours, MAPS_REL)

    # --- 3a. which monsters do the spawn points actually reference?
    mob_ids, regen_src = set(), {}
    for row, folder, _, _ in ZONES:
        d = os.path.join(src_maps, folder)
        for name in sorted(os.listdir(d)):
            if not name.lower().endswith(".ifo"):
                continue
            buf, bounds = read_ifo(os.path.join(d, name))
            off, end = lump_block(bounds, LUMP_REGEN)
            if off is None or buf[off:off + 4] == b"\0\0\0\0":
                continue
            regen_src[(folder, name)] = buf[off:end]
            objs, _ = read_lump(buf, bounds, LUMP_REGEN)
            for o in objs:
                for idx in regen_mob_ids(o["extra"]):
                    mob_ids.add(idx)
    mob_ids = sorted(mob_ids)
    print(f"    {'spawn-referenced monsters':26s} {len(mob_ids)} ids "
          f"({mob_ids[0]}..{mob_ids[-1]})")

    src_npc = Stb(rel_path(src, r"3DDATA\STB\LIST_NPC.STB"))
    our_npc = Stb(rel_path(ours, r"3DDATA\STB\LIST_NPC.STB"))
    if max(mob_ids) >= our_npc.rows:
        raise SystemExit(f"monster id {max(mob_ids)} beyond LIST_NPC.STB ({our_npc.rows})")

    # --- 3b. LIST_NPC rows
    written, kept = 0, []
    for i in mob_ids:
        if our_npc.occupied(i):
            kept.append(i)
            continue
        for c in range(NPC_COPY_COLS):
            our_npc.set(i, c, src_npc.get(i, c))
        our_npc.set(i, 0, clean_npc_name(src_npc.get(i, 0)))
        our_npc.set(i, NPC_PVP_COL, DEFAULT_PVP_STATE)
        written += 1
    print(f"    {'LIST_NPC.STB':26s} {written} rows written, "
          f"{len(kept)} already ours {kept if kept else ''}")

    # --- 3c. monster names (RoseZA language block 1 is English)
    src_stl = Stl(rel_path(src, r"3DDATA\STB\LIST_NPC_S.STL"))
    src_names = {}
    for i, (k, _) in enumerate(src_stl.keys):
        txt = src_stl.langs[1][i][0] if len(src_stl.langs) > 1 else b""
        src_names[k] = txt or src_stl.langs[0][i][0]
    our_stl = Stl(rel_path(ours, r"3DDATA\STB\LIST_NPC_S.STL"))
    nnames = 0
    for i in mob_ids:
        key = our_npc.get(i, NPC_STRID_COL).decode("latin-1").strip()
        if not key or our_stl.has(key):
            continue
        name = src_names.get(key.encode("latin-1"), b"")
        text = name.decode("latin-1") if name else our_npc.get(i, 0).decode("latin-1")
        our_stl.append(key, i, text)
        nnames += 1
    print(f"    {'LIST_NPC_S.STL':26s} +{nnames} keys (now {len(our_stl.keys)})")

    # --- 3d. AI rows and their .aip files
    src_ai = Stb(rel_path(src, r"3DDATA\STB\FILE_AI.STB"))
    our_ai = Stb(rel_path(ours, r"3DDATA\STB\FILE_AI.STB"))
    need_ai = sorted({int(our_npc.get(i, NPC_AI_COL))
                      for i in mob_ids
                      if our_npc.get(i, NPC_AI_COL).strip().isdigit()} - {0})
    grew = our_ai.grow_to(max(need_ai) + 1) if need_ai else 0
    aips, ai_written = set(), 0
    for a in need_ai:
        f = src_ai.get(a, 0)
        if not f.strip():
            raise SystemExit(f"AI row {a} is blank in the source FILE_AI.STB")
        aips.add(f.decode("latin-1"))
        if our_ai.get(a, 0) != f:
            if our_ai.get(a, 0).strip():
                print(f"    AI row {a} overwritten: {our_ai.get(a, 0)!r} -> {f!r}")
            our_ai.set(a, 0, f)
            ai_written += 1
    print(f"    {'FILE_AI.STB':26s} +{grew} rows (now {our_ai.rows}), "
          f"{ai_written} rows written, {len(need_ai)} AI types")
    copy_files(aips, src, ours, dry, ".aip files")

    # --- 3e. character models, appended with a full index remap
    src_chr = Chr(rel_path(src, NPC_CHR_REL))
    our_chr = Chr(rel_path(ours, NPC_CHR_REL))
    src_zsc = Zsc(rel_path(src, PART_NPC_ZSC_REL))
    our_zsc = Zsc(rel_path(ours, PART_NPC_ZSC_REL))

    skel_map = StringPool(our_chr.skeletons, src_chr.skeletons)
    motion_map = StringPool(our_chr.motions, src_chr.motions)
    effect_map = StringPool(our_chr.effects, src_chr.effects)

    mesh_idx = {norm(m): i for i, m in enumerate(our_zsc.meshes)}
    mat_idx = {norm(p): i for i, (p, _) in enumerate(our_zsc.materials)}
    new_meshes, new_mats, new_objects = [], [], []
    model_map, art = {}, set()

    def map_model(sidx):
        if sidx in model_map:
            return model_map[sidx]
        cyl, sparts, sdummies, sbb = src_zsc.objects[sidx]
        parts = []
        for mid, tid, props in sparts:
            mk = norm(src_zsc.meshes[mid])
            if mk not in mesh_idx:
                mesh_idx[mk] = len(our_zsc.meshes) + len(new_meshes)
                new_meshes.append(src_zsc.meshes[mid])
            tk = norm(src_zsc.materials[tid][0])
            if tk not in mat_idx:
                mat_idx[tk] = len(our_zsc.materials) + len(new_mats)
                new_mats.append(src_zsc.materials[tid])
            art.add(src_zsc.meshes[mid].decode("latin-1"))
            art.add(src_zsc.materials[tid][0].decode("latin-1"))
            parts.append((mesh_idx[mk], mat_idx[tk], props))
        model_map[sidx] = len(our_zsc.objects) + len(new_objects)
        new_objects.append(our_zsc.object_bytes(cyl, parts, sdummies, sbb))
        return model_map[sidx]

    if max(mob_ids) >= len(our_chr.chars):
        our_chr.chars.extend([None] * (max(mob_ids) + 1 - len(our_chr.chars)))
    chr_written = 0
    for i in mob_ids:
        if our_chr.chars[i] is not None:
            continue
        c = src_chr.chars[i]
        if c is None:
            raise SystemExit(f"monster {i} has no LIST_NPC.CHR entry in the source")
        our_chr.chars[i] = dict(
            skel=skel_map[c["skel"]],
            name=c["name"],
            models=[map_model(m) for m in c["models"]],
            anims=[(t, motion_map[a]) for t, a in c["anims"]],
            effects=[(t, effect_map[e]) for t, e in c["effects"]],
        )
        chr_written += 1

    art |= skel_map.used | motion_map.used
    print(f"    {'LIST_NPC.CHR':26s} {chr_written} entries (now {len(our_chr.chars)}), "
          f"skel {len(our_chr.skeletons)} motion {len(our_chr.motions)}")
    print(f"    {'PART_NPC.ZSC':26s} +{len(new_objects)} models, "
          f"+{len(new_meshes)} meshes, +{len(new_mats)} materials")
    art = {a for a in art if "\\" in a or "/" in a}
    copy_files(art, src, ours, dry, "character art")

    # --- 3f. mob-weapon presentation rows
    src_wpn = Stb(rel_path(src, r"3DDATA\STB\LIST_WEAPON.STB"))
    our_wpn = Stb(rel_path(ours, r"3DDATA\STB\LIST_WEAPON.STB"))
    weapons = set()
    for i in mob_ids:
        for col in (NPC_R_WEAPON_COL, NPC_L_WEAPON_COL):
            v = our_npc.get(i, col).strip()
            if v.isdigit() and int(v):
                weapons.add(int(v))
    wfixed = []
    for w in sorted(weapons):
        if w >= our_wpn.rows:
            raise SystemExit(f"monster weapon row {w} beyond LIST_WEAPON.STB "
                             f"({our_wpn.rows} rows)")
        if any(our_wpn.get(w, c).strip() for c in WEAPON_PRESENTATION_COLS):
            continue                              # already presents something
        got = [(c, src_wpn.get(w, c)) for c in WEAPON_PRESENTATION_COLS
               if src_wpn.get(w, c).strip()]
        if not got:
            print(f"    !! weapon {w} has no presentation data in the source either")
            continue
        for c, v in got:
            our_wpn.set(w, c, v)
        if not our_wpn.get(w, 0).strip():
            our_wpn.set(w, 0, src_wpn.get(w, 0))
        wfixed.append((w, [c for c, _ in got]))
    print(f"    {'LIST_WEAPON.STB':26s} {len(wfixed)} of {len(weapons)} monster weapons "
          f"given attack presentation {[w for w, _ in wfixed]}")
    if wfixed:
        our_wpn.save(dry)

    # --- 3g. tables first, then the spawn lumps
    our_npc.save(dry)
    our_ai.save(dry)
    if nnames:
        our_stl.save(dry)
    our_chr.save(dry)
    if new_objects and not dry:
        p = rel_path(ours, PART_NPC_ZSC_REL)
        backup(p)
        with open(p, "wb") as fh:
            fh.write(our_zsc.to_bytes(new_meshes, new_mats, new_objects))

    files, points = 0, 0
    for (folder, name), blob in sorted(regen_src.items()):
        dp = os.path.join(dst_maps, folder, name)
        if not os.path.isfile(dp):
            raise SystemExit(f"{dp}: run --stage 1 first")
        dbuf, dbounds = read_ifo(dp)
        doff, dend = lump_block(dbounds, LUMP_REGEN)
        if doff is None:
            raise SystemExit(f"{dp}: no REGEN lump to fill")
        if dbuf[doff:dend] == blob:
            continue
        n, = struct.unpack_from("<i", blob, 0)
        out = build_ifo(dbounds, dbuf, {LUMP_REGEN: blob})
        files += 1
        points += n
        if not dry:
            with open(dp, "wb") as fh:
                fh.write(out)
            vbuf, vbounds = read_ifo(dp)
            voff, vend = lump_block(vbounds, LUMP_REGEN)
            if vbuf[voff:vend] != blob:
                raise SystemExit(f"VERIFY FAILED: {dp} REGEN lump mismatch")
    print(f"    {'IFO regen lumps':26s} {points} spawn points into {files} files")


# -------------------------------------------------------------------- driver
def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--stage", type=int, choices=(1, 2, 3), action="append",
                    help="stage to run (repeatable); omit to run 1, 2 and 3")
    ap.add_argument("--dry-run", action="store_true", help="preview without writing")
    ap.add_argument("--selftest", action="store_true",
                    help="prove every writer is byte-faithful, then exit")
    ap.add_argument("--root", default=".", help="repo root (default: cwd)")
    ap.add_argument("--source", default=DEFAULT_SRC, help="RoseZA client data dir")
    args = ap.parse_args()

    ours = os.path.join(args.root, "data")
    src = args.source
    if not os.path.isdir(ours):
        raise SystemExit(f"not found: {ours} (run from the repo root or pass --root)")
    if not os.path.isdir(src):
        raise SystemExit(f"source data dir not found: {src}")

    print("self-test (every writer must round-trip byte-identically):")
    if not selftest(ours, src):
        raise SystemExit("self-test FAILED -- refusing to write")
    if args.selftest:
        return 0

    stages = sorted(set(args.stage or (1, 2, 3)))
    print()
    for s in stages:
        {1: stage1, 2: stage2, 3: stage3}[s](ours, src, args.dry_run)
        print()

    if args.dry_run:
        print("dry run: nothing written")
    else:
        print("done -- .bak backups alongside every table touched.")
        print("Next: rebake the client VFS and restart the servers (they cache STBs).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
