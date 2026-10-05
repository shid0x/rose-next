"""Import an equipment item from another ROSE data set into ours, as a new ID.

Usage:
    python scripts/import-item.py --type back --source C:\\path\\to\\data --source-row 803 --icon 8451
    python scripts/import-item.py --type weapon --source ... --source-row 873 --dry-run

Two modes:
    default     copy the source row's stats, with --def/--res/--atk/--req-level to
                temper them. Needs the source schema to line up with ours.
    --art-only  take the model (plus optionally the icon and ground-drop model) and
                nothing else; every stat column is cloned from --template-row, one of
                OUR existing rows. Use it whenever the source's game semantics are not
                wanted. It is also the mode to reach for with an unfamiliar dump: no
                source cell is ever read, so a foreign schema cannot misalign and a
                foreign ability id cannot reach the *unbounded* write at
                `m_iAddValue[nType] += nValue` in cuserdata.cpp (client and server
                both). That array is int[AT_MAX] with m_nPassiveRate/m_btRecoverHP/
                m_iDropRATE sitting right behind it, so an out-of-range id from a
                modern affix system corrupts character state instead of erroring.

What it does (all appends -- existing rows/objects are never modified):
    1. <type>.STB   append one row (new ID = current row count) with the source stats
    2. <type>.ZSC   append the source's model object; meshes/materials already present
                    in our ZSC are reused, missing ones are appended
    3. <type>_S.STL append key <PREFIX><id> with name/desc taken from the source STL
    4. copies any mesh/texture files referenced by the appended ZSC entries from the
       source data dir into ours (same relative paths)

Helmet/armour/gauntlet/boots are sex-split: the client loads a separate model
table per sex (io_basic.cpp, m_pMD_CharPARTS[sex][part] -> LIST_mBODY.ZSC /
LIST_wBODY.ZSC and friends) and indexes **both** by the same item number. So a
new armour ID needs one object appended to each, and the invariant that must
hold afterwards is:

    STB rows == male ZSC objects == female ZSC objects

It holds exactly in both our data and RoseZA's today. This script checks it
before appending -- if a table is already out of step, something else corrupted
it and adding another row would only bury the evidence.

Structure facts this relies on (verified against the live client/server loaders):
    - Item visuals come from the type's ZSC object[item_no]; the STB "model file"
      column 1 (.txt path) is vestigial and never read by the game.
    - Item STB tables share a column prefix (ITEM_* macros in rose/io/stb.h):
      col 5 price, 7 weight, 8 quality, 9 icon, 10 field model, 29 durability,
      31 defence, 32 resistance. The STL key is always the LAST column, and evo-era
      sources carry extra columns after ours which are dropped.
    - STBDATA::load (src/common/src/io/stb.cpp) reads (rows-1)x(cols-1) u16-length
      cells at `offset`; the header before `offset` holds col titles + row names that
      STB editors read, so the row-name list gets a matching new entry.
    - Icon indices (STB col 9) point into OUR ITEM1.TSI. The atlases of different
      data sets share geometry but NOT art -- the same index is different art in
      each -- so always pass --icon with an index added by add-item-icon.py.
    - Field model (STB col 10) indexes our LIST_FieldITEM.ZSC (ground-drop mesh);
      pass --copy-field-model to port the source's drop model too (recommended), or
      --field-model N to reuse one of ours.

After running: restart servers + client, spawn with /item <type>:<new id> (GM 2048).
"""
import argparse, collections, io, os, re, shutil, struct, sys

OURS = "data"
FIELD_ZSC_REL = r"3DDATA\ITEM\LIST_FieldITEM.ZSC"

# Upper bound for an ability id written into a bonus column. The real limit is
# AT_MAX in src/common/shared/datatype.h (~105-109 depending on the FRAROSE
# block); 105 is the conservative floor of that range. Anything at or above it
# runs off the end of CUserDATA::m_iAddValue on both client and server, which
# have no bounds check -- see the --art-only note above.
AT_MAX_GUARD = 105

# AT_ATK, so an overwritten ATK bonus can be named in the warning below.
AT_ATK = 18

# name -> (item type number, STB, model ZSCs, STL, STL key prefix, our data-col count)
#
# "model ZSCs" is a tuple because armour is sex-split; it is empty for types with
# no avatar model at all. Every entry is appended in lockstep and must end up
# holding the new item number as its object index.
# The col count is a tripwire: if our table ever gains a column the row we build
# would be the wrong width, and appending it would silently shift every field.
# The prefix must be the one that table's *own* keys already use -- read it off
# the file, never infer it from the table name. LIST_JEWEL's keys are LJEW; this
# entry said LJEM (which is LIST_JEMITEM's prefix), so an imported jewel would
# have been given a key nothing looks up and shipped with no name at all.
TYPES = {
    "weapon":   (8, r"3DDATA\STB\LIST_WEAPON.STB",   (r"3DDATA\WEAPON\LIST_WEAPON.ZSC",),
                 r"3DDATA\STB\LIST_WEAPON_S.STL",   "LWEA", 46),
    "subwpn":   (9, r"3DDATA\STB\LIST_SUBWPN.STB",   (r"3DDATA\WEAPON\LIST_SUBWPN.ZSC",),
                 r"3DDATA\STB\LIST_SUBWPN_S.STL",   "LSUB", 36),
    "back":     (6, r"3DDATA\STB\LIST_BACK.STB",     (r"3DDATA\AVATAR\LIST_BACK.ZSC",),
                 r"3DDATA\STB\LIST_BACK_S.STL",     "LBAC", 35),
    "faceitem": (1, r"3DDATA\STB\LIST_FACEITEM.STB", (r"3DDATA\AVATAR\LIST_FACEIEM.ZSC",),
                 r"3DDATA\STB\LIST_FACEITEM_S.STL", "LFAC", 34),
    # Armour: one model table per sex, both indexed by the item number.
    "cap":      (2, r"3DDATA\STB\LIST_CAP.STB",      (r"3DDATA\AVATAR\LIST_MCAP.ZSC",
                                                      r"3DDATA\AVATAR\LIST_WCAP.ZSC"),
                 r"3DDATA\STB\LIST_CAP_S.STL",      "LCAP", 37),
    "body":     (3, r"3DDATA\STB\LIST_BODY.STB",     (r"3DDATA\AVATAR\LIST_MBODY.ZSC",
                                                      r"3DDATA\AVATAR\LIST_WBODY.ZSC"),
                 r"3DDATA\STB\LIST_BODY_S.STL",     "LBOD", 36),
    "arms":     (4, r"3DDATA\STB\LIST_ARMS.STB",     (r"3DDATA\AVATAR\LIST_MARMS.ZSC",
                                                      r"3DDATA\AVATAR\LIST_WARMS.ZSC"),
                 r"3DDATA\STB\LIST_ARMS_S.STL",     "LARM", 35),
    "foot":     (5, r"3DDATA\STB\LIST_FOOT.STB",     (r"3DDATA\AVATAR\LIST_MFOOT.ZSC",
                                                      r"3DDATA\AVATAR\LIST_WFOOT.ZSC"),
                 r"3DDATA\STB\LIST_FOOT_S.STL",     "LFOO", 36),
    # No avatar model -- the model step is skipped entirely.
    "useitem":  (10, r"3DDATA\STB\LIST_USEITEM.STB",  (),
                 r"3DDATA\STB\LIST_USEITEM_S.STL",  "LUSE", 28),
    "jewel":    (7, r"3DDATA\STB\LIST_JEWEL.STB",     (),
                 r"3DDATA\STB\LIST_JEWEL_S.STL",    "LJEW", 35),
    "natural":  (12, r"3DDATA\STB\LIST_NATURAL.STB",  (),
                 r"3DDATA\STB\LIST_NATURAL_S.STL",  "LNAT", 19),
}

# ---------------------------------------------------------------- STB
def stb_read(path):
    with open(path, "rb") as fh:
        d = fh.read()
    f = io.BytesIO(d)
    f.read(4)
    offset, rows, cols = struct.unpack("<III", f.read(12))
    f.seek(offset)
    def pstr():
        n, = struct.unpack("<H", f.read(2))
        return f.read(n)
    data = [[pstr() for _ in range(cols - 1)] for _ in range(rows - 1)]
    return d, offset, rows, cols, data

def stb_append_row(path, row_cells, dry):
    d, offset, rows, cols, _ = stb_read(path)
    assert len(row_cells) == cols - 1, (len(row_cells), cols - 1)
    new_id = rows - 1
    row_name = str(new_id).encode("ascii")
    name_block = struct.pack("<H", len(row_name)) + row_name
    cells = b"".join(struct.pack("<H", len(c)) + c for c in row_cells)
    out = (d[0:4]
           + struct.pack("<III", offset + len(name_block), rows + 1, cols)
           + d[16:offset] + name_block + d[offset:] + cells)
    if not dry:
        with open(path, "wb") as fh:
            fh.write(out)
    return new_id

def stb_write_row(path, row, row_cells, dry):
    """Overwrite an existing (blank) row's cells in place.

    The counterpart to stb_append_row, for --target-row. Same rebuild as
    stb_set_cell: the header, its column titles and every row name are preserved
    verbatim, and the table's row count does not change.
    """
    d, offset, rows, cols, data = stb_read(path)
    if not 0 <= row < rows - 1:
        sys.exit("stb_write_row: row %d out of range (%d rows)" % (row, rows - 1))
    if len(row_cells) != cols - 1:
        sys.exit("stb_write_row: %d cells for %d columns" % (len(row_cells), cols - 1))
    data[row] = list(row_cells)
    body = b"".join(struct.pack("<H", len(c)) + c for r in data for c in r)
    if not dry:
        with open(path, "wb") as fh:
            fh.write(d[:offset] + body)
    return row

def stb_set_cell(path, row, col, value, dry):
    """Overwrite one cell in place. Rebuilds the data section; the header (column
    titles + row names) is preserved verbatim, so editors still read the file."""
    d, offset, rows, cols, data = stb_read(path)
    if not 0 <= row < rows - 1 or not 0 <= col < cols - 1:
        sys.exit("stb_set_cell: row %d col %d out of range (%dx%d)"
                 % (row, col, rows - 1, cols - 1))
    if isinstance(value, str):
        value = value.encode("euc-kr")
    data[row][col] = value
    body = b"".join(struct.pack("<H", len(c)) + c
                    for r in data for c in r)
    out = d[:offset] + body
    if not dry:
        with open(path, "wb") as fh:
            fh.write(out)
    return out


# ---------------------------------------------------------------- ZSC
class Zsc:
    def __init__(self, path):
        with open(path, "rb") as fh:
            self.d = fh.read()
        f = self.f = io.BytesIO(self.d)
        self.meshes, self.materials, self.effects, self.objects = [], [], [], []
        n = self.u16()
        for _ in range(n):
            self.meshes.append(self.cstr())
        self.mesh_end = f.tell()
        n = self.u16()
        for _ in range(n):
            self.materials.append((self.cstr(), f.read(36)))
        self.mat_end = f.tell()
        n = self.u16()
        for _ in range(n):
            self.effects.append(self.cstr())
        self.objcnt_pos = f.tell()
        n = self.u16()
        for _ in range(n):
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
        # some third-party editors leave junk after the last object; the game
        # only reads the declared object count, so ignore it (never copy it)
        self.obj_end = f.tell()
        self.trailing = len(self.d) - self.obj_end
        if self.trailing:
            print("NOTE: %s has %d trailing bytes after the last object (ignored)" % (path, self.trailing))

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

def norm(p):
    return p.decode("ascii", "replace").replace("\\", "/").lower()

def zms_extent(path):
    """(vertex count, largest bbox extent) from a ZMS header, or None.

    Both come out of the header, which is read eagerly regardless of lazy
    geometry loading, so this is cheap.

    Reads 8 KB rather than the header's nominal size because the **bone table
    sits between the bounding box and the vertex count** and is variable length
    (2 bytes per bone). A 64-byte read covers a rigid mesh and overruns on a
    skinned one, and since the parse is wrapped in a bare except that failure
    returned None -- which callers read as "cannot tell", so the placeholder
    guard silently stopped evaluating exactly the more complex meshes.
    """
    try:
        with open(path, "rb") as fh:
            d = fh.read(8192)
    except OSError:
        return None
    try:
        i = d.index(b"\x00") + 1
        if not d[:i - 1].startswith(b"ZMS"):
            return None
        j = i + 4                                   # skip the format flags
        bmin = struct.unpack_from("<3f", d, j)
        bmax = struct.unpack_from("<3f", d, j + 12)
        j += 24
        nbone, = struct.unpack_from("<H", d, j)
        j += 2 + nbone * 2
        nvert, = struct.unpack_from("<H", d, j)
    except Exception:
        return None
    return nvert, max(bmax[k] - bmin[k] for k in range(3))

def object_is_placeholder(source, src_zsc, obj_idx):
    """True when every part of a source object is degenerate geometry.

    Some source models are stand-ins for a system we do not have. Jrose's
    `mant_dammy_m.zms` -- "dammy" is dummy -- is 3 vertices in a bounding box
    0.0014 units across, and 76 of their back rows use it: their mantles are
    drawn elsewhere (those textures live under `3Ddata/NPC/MANT/`, away from the
    avatar art) and the back slot only holds a placeholder. Imported faithfully,
    such an item equips and shows nothing, which reads as a broken import rather
    than a faithful one.

    Judged per *object*, not per mesh: plenty of good models have a tiny second
    part (a strap, a charm), so a small mesh alone means nothing. Only an object
    with no visible geometry at all is a placeholder.
    """
    _cyl, parts, _dummies, _bb = src_zsc.objects[obj_idx]
    if not parts:
        return False                                # empty is handled separately
    for mid, _tid, _props in parts:
        rel = norm(src_zsc.meshes[mid]).replace("/", os.sep)
        if rel.lower().startswith("3ddata"):
            rel = rel.split(os.sep, 1)[1]
        info = zms_extent(os.path.join(source, "3DDATA", rel))
        if info is None:
            return False                            # unreadable: not our call to make
        nvert, extent = info
        if nvert > 8 and extent >= 0.05:
            return False
    return True

# Paths inside .eft/.ptl are length-prefixed rather than NUL-terminated, and the
# prefix width varies, so match the path shape directly instead of parsing the
# container. Missing one is not fatal -- the engine's contract is that an absent
# asset logs once and renders without it -- whereas a mis-parsed length would be.
#
# Anchored on the data root and non-greedy, so it stops at the first extension:
# these blobs put a bare name and the full path back to back
# ("flying_ef.ptl" then "3DDATA\EFFECT\PARTICLES\flying_ef.ptl"), and searching
# for the extension first lands on the name and misses the path behind it.
# Note .ptl files write their separators **doubled** ("3DData\\effect\\..."),
# which is why the result is collapsed below rather than used as-is.
_ASSET_RE = re.compile(
    rb"3[Dd][Dd][Aa][Tt][Aa][\\/][ -~]{0,200}?\.(?:[Pp][Tt][Ll]|[Dd][Dd][Ss]|[Tt][Gg][Aa]"
    rb"|[Zz][Mm][Ss]|[Zz][Mm][Oo]|[Ee][Ff][Tt])")

def _collapse_seps(rel):
    while b"\\\\" in rel:
        rel = rel.replace(b"\\\\", b"\\")
    return rel

def effect_dependencies(source, eft_rel):
    """Every file an .eft pulls in: itself, its particle files, their textures.

    Returns ZSC-style backslash paths, so copy_assets can take them unchanged.
    """
    out, seen, queue = [], set(), [eft_rel]
    while queue:
        rel = queue.pop(0)
        key = norm(rel)
        if key in seen:
            continue
        seen.add(key)
        out.append(rel)
        rel_nodata = rel.decode("ascii", "replace").replace("/", "\\")
        if rel_nodata.lower().startswith("3ddata"):
            rel_nodata = rel_nodata.split("\\", 1)[1]
        path = os.path.join(source, "3DDATA", rel_nodata)
        if not os.path.exists(path):
            continue
        with open(path, "rb") as fh:
            blob = fh.read()
        for m in _ASSET_RE.finditer(blob):
            queue.append(_collapse_seps(m.group()))
    return out

def zsc_serialize_object(o):
    """One object record, exactly as Zsc.__init__ parsed it.

    A zero-part object ends after its count -- no dummy list and no bounding box
    follow -- which is what the game writes for an unused row and what the
    parser expects to read back.
    """
    cyl, parts, dummies, bb = o
    out = [cyl, struct.pack("<H", len(parts))]
    if not parts:
        return b"".join(out)
    for mid, tid, props in parts:
        out.append(struct.pack("<HH", mid, tid))
        out.append(props)
    out.append(struct.pack("<H", len(dummies)))
    for a, props in dummies:
        out += [a, props]
    out.append(bb)
    return b"".join(out)


def zsc_selftest(paths):
    """Re-serialising every object must reproduce the section byte for byte.

    Writing at an index rebuilds the whole object section instead of splicing
    onto the end, so this is the guarantee that untouched objects survive it.
    Proved across all 57 ZSCs we ship before --target-row was allowed near them.
    """
    bad = []
    for path in paths:
        z = Zsc(path)
        original = z.d[z.objcnt_pos + 2:z.obj_end]
        if b"".join(zsc_serialize_object(o) for o in z.objects) != original:
            bad.append(path)
    return bad


# A dummy point whose effect the client places with its rotation and scale as well
# as its position (client io_model.h, POINT_EFFECT_ORIENTED). Plain points (0)
# have only ever been placed by position, and two shipped back items rely on that.
POINT_EFFECT_ORIENTED = 3

def weapon_effect_dummy(source, row):
    """(effect path, point type, props) for a Jrose LIST_WEAPONEFFECT placement.

    Jrose attaches a cosmetic effect to an equipped item through its item
    tables (LIST_BACK col 51 -> LIST_WEAPONEFFECT row), not through the model;
    its particle-only back items carry a 4-vertex placeholder mesh and nothing
    else. Our client has no such table, but it does spawn effects from a
    model's ZSC dummy points, so the placement becomes one.

    Column meanings come from TRose.exe, not from the STB header (every column
    title there is the editor's "put a title here" placeholder). The three
    readers of the table (0x45f680, 0x4621c0, 0x462340) all do the same thing:

        col 2          FILE_EFFECT row of the .eft
        cols 4,5,6,7   x, y, z, w * 0.01 -> CEffect::Rotation(quat) -> setQuaternion
        cols 9,10,11   position, as is   -> CEffect::Transform -> setPosition
        cols 13,14,15  scale * 0.01      -> CEffect::Scale -> setScale

    then link the effect to the part's model node -- what our client does with a
    dummy whose parent is part 1. Cols 3, 8 and 12 are never read. The quaternion
    is passed through un-normalised, as Jrose does; quat::to_matrix uses the
    1-2(yy+zz) form, so (0,0,0,0.01) is the identity and a short quaternion
    shrinks the effect a little as well as turning it. Rotation is written in
    the ZSC's own order, w x y z (CPointPART::Load's ReadFloat4).
    """
    stb_dir = os.path.join(source, "3DDATA", "STB")
    _, _, _, _, wfx = stb_read(os.path.join(stb_dir, "LIST_WEAPONEFFECT.STB"))
    _, _, _, _, files = stb_read(os.path.join(stb_dir, "FILE_EFFECT.STB"))
    if not 0 <= row < len(wfx):
        sys.exit("LIST_WEAPONEFFECT has no row %d" % row)
    cell = lambda c: int(wfx[row][c] or b"0")
    eft_id = cell(2)
    if not 0 < eft_id < len(files) or not files[eft_id][1]:
        sys.exit("LIST_WEAPONEFFECT row %d names FILE_EFFECT row %d, which is empty"
                 % (row, eft_id))
    path = files[eft_id][1]
    pos = (float(cell(9)), float(cell(10)), float(cell(11)))
    rot = tuple(cell(c) * 0.01 for c in (7, 4, 5, 6))
    scale = tuple(cell(c) * 0.01 for c in (13, 14, 15))
    props = (struct.pack("<BB3f", 1, 12, *pos) + struct.pack("<BB4f", 2, 16, *rot)
             + struct.pack("<BB3f", 3, 12, *scale) + struct.pack("<BBh", 7, 2, 1) + b"\x00")
    print("attached effect: LIST_WEAPONEFFECT %d -> %s, pos %s, rot(wxyz) %s, scale %s"
          % (row, path.decode("ascii", "replace"), pos,
             tuple(round(v, 2) for v in rot), tuple(round(v, 2) for v in scale)))
    return path, POINT_EFFECT_ORIENTED, props


def zsc_build_append(ours_path, src_zsc, src_obj_idx, source=None, copy_effects=False,
                     target=None, extra_dummies=()):
    """Return (object_index, assets_needed, new_file_bytes) -- writes nothing.

    `extra_dummies` are (effect path, point type, props) points added after the
    source object's own -- weapon_effect_dummy() builds them. Their effect is
    always copied, as --copy-effects would.

    `target` writes the object **at that index** instead of appending, for
    --target-row. The row's existing object must be empty; the caller checks
    that. Everything about remapping meshes, materials and effects is identical
    either way -- only the final assembly differs.

    Callers write only once *every* table for this item has been built, so a
    failure part-way cannot leave the STB and the per-sex ZSCs at different
    lengths. That is not hypothetical: importing the Crystal Soldier cap
    appended the male model, then hit the empty female object and exited,
    leaving LIST_MCAP.ZSC one object longer than LIST_CAP.STB.
    """
    ours = Zsc(ours_path)
    cyl, sparts, sdummies, sbb = src_zsc.objects[src_obj_idx]
    if not sparts:
        # Legitimate and common in evo-era data -- 381 of RoseZA's 2415 named
        # caps have no female model. Emit an empty object so the index still
        # lines up; the item is simply invisible on that sex.
        print("WARNING: source object %d in %s has no model -- appending an empty object "
              "(the item will not show on this sex)"
              % (src_obj_idx, os.path.basename(ours_path)))
        empty = cyl + struct.pack("<H", 0)
        if target is None:
            out = [ours.d[:ours.objcnt_pos],
                   struct.pack("<H", len(ours.objects) + 1),
                   ours.d[ours.objcnt_pos + 2:ours.obj_end],
                   empty]
            return len(ours.objects), [], b"".join(out)
        body = [empty if i == target else zsc_serialize_object(o)
                for i, o in enumerate(ours.objects)]
        out = [ours.d[:ours.objcnt_pos],
               struct.pack("<H", len(ours.objects)),
               b"".join(body)]
        return target, [], b"".join(out)

    our_mesh_idx = {norm(m): i for i, m in enumerate(ours.meshes)}
    our_mat_idx = {norm(p): i for i, (p, _) in enumerate(ours.materials)}
    new_meshes, new_mats = [], []
    mesh_map, mat_map = {}, {}
    for mid, tid, _ in sparts:
        if mid not in mesh_map:
            key = norm(src_zsc.meshes[mid])
            if key in our_mesh_idx:
                mesh_map[mid] = our_mesh_idx[key]
            else:
                mesh_map[mid] = len(ours.meshes) + len(new_meshes)
                new_meshes.append(src_zsc.meshes[mid])
        if tid not in mat_map:
            key = norm(src_zsc.materials[tid][0])
            if key in our_mat_idx:
                mat_map[tid] = our_mat_idx[key]
            else:
                mat_map[tid] = len(ours.materials) + len(new_mats)
                new_mats.append(src_zsc.materials[tid])

    obj = [cyl, struct.pack("<H", len(sparts))]
    for mid, tid, props in sparts:
        obj.append(struct.pack("<HH", mesh_map[mid], mat_map[tid]))
        obj.append(props)

    # Dummy points carry an index into the ZSC's *effect* list, and that index
    # must be remapped exactly like a mesh or material index -- copying it
    # verbatim writes a source-table index into our table.
    #
    # This is a hard crash, not a cosmetic bug. CMODEL<CCharPART>::Load does
    #
    #     m_pDummyPoints[nP].m_uiEftKEY = (nListIDX >= 0) ? pEftKEY[nListIDX] : 0;
    #
    # (src/client/IO_Model.h) with no bounds check, and pEftKEY is NULL when the
    # table declares no effects -- which LIST_BACK.ZSC does. Importing Jrose's
    # Phoenix Wings, whose dummy references its table's effect 0, therefore took
    # the client down inside CGame::Load_BasicDATA with a null read, before the
    # title screen and before anything reached error.txt.
    #
    # Effects are a separate asset class with their own dependency tree (.EFT ->
    # particles -> textures), so we reuse one only when our table already holds
    # the same path, and otherwise drop the dummy point. Dropping costs a
    # cosmetic attachment; emitting a dangling index costs the whole client.
    our_eft_idx = {norm(e): i for i, e in enumerate(ours.effects)}
    new_effects, effect_files = [], []
    kept = []
    for a, props in sdummies:
        list_idx, eff_type = struct.unpack("<hh", a)
        if list_idx < 0:                      # no effect on this point -- always safe
            kept.append((a, props))
            continue
        src_path = (src_zsc.effects[list_idx] if list_idx < len(src_zsc.effects) else None)
        if src_path is not None and norm(src_path) in our_eft_idx:
            kept.append((struct.pack("<hh", our_eft_idx[norm(src_path)], eff_type), props))
            continue
        if src_path is not None and copy_effects:
            idx = len(ours.effects) + len(new_effects)
            our_eft_idx[norm(src_path)] = idx
            new_effects.append(src_path)
            effect_files.append(src_path)
            kept.append((struct.pack("<hh", idx, eff_type), props))
            continue
        print("WARNING: dropping a dummy point whose effect %s is not in our %s "
              "(our table declares %d effect(s)); the model imports, its attached "
              "effect does not. Pass --copy-effects to port it."
              % (src_path.decode("ascii", "replace") if src_path else "index %d" % list_idx,
                 os.path.basename(ours_path), len(ours.effects)))
    for src_path, eff_type, props in extra_dummies:
        if norm(src_path) in our_eft_idx:
            idx = our_eft_idx[norm(src_path)]
        else:
            idx = len(ours.effects) + len(new_effects)
            our_eft_idx[norm(src_path)] = idx
            new_effects.append(src_path)
            effect_files.append(src_path)
        kept.append((struct.pack("<hh", idx, eff_type), props))
    obj.append(struct.pack("<H", len(kept)))
    for a, props in kept:
        obj += [a, props]
    obj.append(sbb)

    out = [struct.pack("<H", len(ours.meshes) + len(new_meshes)),
           ours.d[2:ours.mesh_end]]
    out += [m + b"\x00" for m in new_meshes]
    out += [struct.pack("<H", len(ours.materials) + len(new_mats)),
            ours.d[ours.mesh_end + 2:ours.mat_end]]
    out += [p + b"\x00" + flags for p, flags in new_mats]
    # effect list: count, then the existing strings, then anything we appended
    out += [struct.pack("<H", len(ours.effects) + len(new_effects)),
            ours.d[ours.mat_end + 2:ours.objcnt_pos]]
    out += [e + b"\x00" for e in new_effects]
    obj_bytes = b"".join(obj)
    if target is None:
        out += [struct.pack("<H", len(ours.objects) + 1),
                ours.d[ours.objcnt_pos + 2:ours.obj_end],
                obj_bytes]
        index = len(ours.objects)
    else:
        body = [obj_bytes if i == target else zsc_serialize_object(o)
                for i, o in enumerate(ours.objects)]
        out += [struct.pack("<H", len(ours.objects)), b"".join(body)]
        index = target
    files_needed = [m for m in new_meshes] + [p for p, _ in new_mats]
    for eft in effect_files:
        files_needed += effect_dependencies(source, eft)
    return index, files_needed, b"".join(out)

# ---------------------------------------------------------------- STL
def read_varint(f):
    n = 0; shift = 0
    while True:
        b = f.read(1)[0]
        n |= (b & 0x7F) << shift
        if b < 0x80:
            return n
        shift += 7

def write_varint(n):
    out = b""
    while n > 0x7F:
        out += bytes([(n & 0x7F) | 0x80])
        n >>= 7
    return out + bytes([n])

def vstr_read(f):
    return f.read(read_varint(f))

def vstr_write(b):
    return write_varint(len(b)) + b

def stl_read(path):
    with open(path, "rb") as fh:
        f = io.BytesIO(fh.read())
    typ = vstr_read(f)
    assert typ == b"ITST01", typ
    keycount, = struct.unpack("<I", f.read(4))
    keys = []
    for _ in range(keycount):
        k = vstr_read(f)
        idx, = struct.unpack("<I", f.read(4))
        keys.append((k, idx))
    langcount, = struct.unpack("<I", f.read(4))
    langpos = struct.unpack("<%dI" % langcount, f.read(4 * langcount))
    langs = []
    for lp in langpos:
        f.seek(lp)
        offsets = struct.unpack("<%dI" % keycount, f.read(4 * keycount))
        entries = []
        for o in offsets:
            f.seek(o)
            entries.append((vstr_read(f), vstr_read(f)))
        langs.append(entries)
    return keys, langs

def stl_append(path, new_key, new_idx, name, desc, dry):
    keys, langs = stl_read(path)
    if any(k == new_key for k, _ in keys):
        sys.exit("STL key %s already exists" % new_key.decode())
    keys.append((new_key, new_idx))
    for entries in langs:
        entries.append((name, desc))
    stl_write(path, keys, langs, dry)

def stl_write(path, keys, langs, dry):
    """Serialise a whole ITST01 table. Every per-language entry is written at a
    fresh offset, so this is also how entries get *removed* -- the offset table
    is rebuilt rather than patched."""
    keycount, langcount = len(keys), len(langs)

    header = vstr_write(b"ITST01") + struct.pack("<I", keycount)
    for k, idx in keys:
        header += vstr_write(k) + struct.pack("<I", idx)
    header += struct.pack("<I", langcount)
    langpos_at = len(header)
    header += b"\x00" * (4 * langcount)

    body = b""
    lang_positions = []
    for entries in langs:
        lang_positions.append(len(header) + len(body))
        base = len(header) + len(body) + 4 * keycount
        offsets, blob = [], b""
        for n, ds in entries:
            offsets.append(base + len(blob))
            blob += vstr_write(n) + vstr_write(ds)
        body += struct.pack("<%dI" % keycount, *offsets) + blob

    out = bytearray(header + body)
    out[langpos_at:langpos_at + 4 * langcount] = struct.pack("<%dI" % langcount, *lang_positions)
    if not dry:
        with open(path, "wb") as fh:
            fh.write(bytes(out))

# ---------------------------------------------------------------- assets
def copy_assets(files_needed, source, dry):
    for f in files_needed:
        rel = f.decode("ascii", "replace").replace("/", "\\")
        # ZSC paths start with '3Ddata\...' -- strip the data-root component
        rel_nodata = rel.split("\\", 1)[1] if rel.lower().startswith("3ddata") else rel
        srcf = os.path.join(source, "3DDATA", rel_nodata)
        dstf = os.path.join(OURS, "3DDATA", rel_nodata)
        if os.path.exists(dstf):
            print("asset exists: %s" % rel)
            continue
        if not os.path.exists(srcf):
            print("WARNING: source asset missing: %s" % srcf)
            continue
        if not dry:
            os.makedirs(os.path.dirname(dstf), exist_ok=True)
            shutil.copy2(srcf, dstf)
        print("%s: %s" % ("would copy" if dry else "copied", rel))

# ---------------------------------------------------------------- icon
def copy_source_icon(source, src_index, label, dry):
    """Crop one sprite out of the source data set's ITEM1.TSI into ours.

    Icon indices are NOT portable: both atlases have the same 40x40 cell
    geometry but completely different art, so reusing the source's number gives
    a plausible-looking wrong icon. This ports the actual pixels and returns the
    index in *our* atlas.
    """
    import importlib.util
    here = os.path.dirname(os.path.abspath(__file__))
    spec = importlib.util.spec_from_file_location(
        "add_item_icon", os.path.join(here, "add-item-icon.py"))
    ico = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(ico)
    from PIL import Image

    src_res = os.path.join(source, "3DDATA", "CONTROL", "RES")
    src_tsi = os.path.join(src_res, "ITEM1.TSI")
    if not os.path.exists(src_tsi):
        sys.exit("source has no ITEM1.TSI at %s" % src_tsi)
    textures, blocks = ico.tsi_read(src_tsi, strict=False)   # foreign atlas, read-only
    flat = []
    for (sheet, _), (cnt, raw) in zip(textures, blocks):
        for i in range(cnt):
            flat.append((sheet, raw[i * 54:(i + 1) * 54]))
    if not 0 <= src_index < len(flat):
        sys.exit("source icon index %d out of range (source atlas has %d sprites)"
                 % (src_index, len(flat)))
    sheet, ent = flat[src_index]
    _, x1, y1, x2, y2 = struct.unpack_from("<hiiii", ent, 0)

    path = os.path.join(src_res, sheet)
    if not os.path.exists(path):                       # source dirs vary in case
        for alt in os.listdir(src_res):
            if alt.lower() == sheet.lower():
                path = os.path.join(src_res, alt)
                break
    # Crop a fixed cell rather than trusting x2/y2: the two rect conventions
    # (x..x+40 and x..x+39) are mixed *within* RoseZA's own ITEM1.TSI -- exactly
    # half its sprites use each -- so honouring the stored far edge silently
    # shaves a pixel off half the icons.
    cell = ico.CELL
    art = Image.open(path).convert("RGBA").crop((x1, y1, x1 + cell, y1 + cell))
    print("icon: source %d (%s %s) -> " % (src_index, sheet, (x1, y1)), end="")
    return ico.add_icon(art, label, dry)


# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--type", default="weapon", choices=sorted(TYPES),
                    help="item type to import (default: weapon)")
    # not required=True: --selftest needs neither, and argparse would reject the
    # run before main() could dispatch to it
    ap.add_argument("--source", help="path to the source data directory")
    ap.add_argument("--source-row", type=int,
                    help="row in the source table for this type")
    ap.add_argument("--name", help="override item name (default: from source STL)")
    ap.add_argument("--desc", help="override item description (default: from source STL)")
    ap.add_argument("--art-only", action="store_true",
                    help="take ONLY the model (and, with the flags below, the icon and "
                         "ground-drop model) from the source; clone every stat column from "
                         "--template-row instead of the source row. Use this whenever the "
                         "source's game semantics are not wanted -- it is also the safe mode "
                         "for a foreign schema, since no source cell is ever read and a "
                         "foreign ability id therefore cannot reach the unbounded "
                         "m_iAddValue[] write on client and server. Requires --name.")
    ap.add_argument("--template-row", type=int,
                    help="row in OUR table whose stat columns the new item copies "
                         "(required by --art-only)")
    ap.add_argument("--icon", type=int, help="icon index in OUR ITEM1.TSI (default: keep source value, likely wrong art)")
    ap.add_argument("--copy-icon", action="store_true",
                    help="port the source's inventory icon into our ITEM1.TSI and use it "
                         "(recommended; the same index is different art in each atlas)")
    ap.add_argument("--def", dest="defence", type=int,
                    help="override DEFENCE (col 31). Evo-era gear is scaled ~4x ours -- "
                         "importing source stats verbatim trivialises our existing content")
    ap.add_argument("--res", type=int, help="override RESISTENCE (col 32)")
    ap.add_argument("--price", type=int,
                    help="override BASE_PRICE (col 5). Mostly for materials under "
                         "--art-only, where every other column comes from one template "
                         "and the price is what tells two materials apart")
    ap.add_argument("--req-level", type=int,
                    help="override the required character level (NEED_DATA pair with type 31)")
    ap.add_argument("--atk", type=int,
                    help="weapon only: override ATTACK_POWER (col 35)")
    ap.add_argument("--atk-speed", type=int,
                    help="weapon only: override ATTACK_SPEED (col 36). Lower is FASTER "
                         "(attack_speed = 1500/(value+5)), and evo-era values run ~6 higher "
                         "than ours, so copying them makes a weapon noticeably slower")
    ap.add_argument("--req-stat", metavar="ID:VALUE",
                    help="set the NON-level requirement pair, e.g. 10:320 for STR 320 "
                         "(ability ids: 10 STR, 11 DEX, 12 INT, 13 CON, 14 CHARM, 15 SENSE). "
                         "Without this the requirement comes from the template or the source "
                         "row, which decouples it from --req-level -- that is how a batch of "
                         "level-170 shields ended up demanding endgame STR.")
    ap.add_argument("--bonus", metavar="ID:VALUE", action="append", default=[],
                    help="set a bonus stat pair (cols 24/25 then 27/28). Repeatable, max 2. "
                         "e.g. --bonus 20:25 for +25 HIT, --bonus 26:18 for +18 CRITICAL. "
                         "Refused for ids >= AT_MAX, which would corrupt adjacent character "
                         "state through the unbounded m_iAddValue[] write.")
    ap.add_argument("--field-model", type=int, help="ground-drop model index in OUR LIST_FieldITEM.ZSC")
    ap.add_argument("--copy-field-model", action="store_true",
                    help="port the source's ground-drop model object into our LIST_FieldITEM.ZSC")
    ap.add_argument("--copy-effects", action="store_true",
                    help="port cosmetic effects the model carries on its ZSC dummy points "
                         "(a wing trail, say): appends the effect to our table's effect list "
                         "and copies the .eft plus the particle files and textures it pulls "
                         "in. Without this such a dummy point is dropped, since a dangling "
                         "effect index crashes the client on load.")
    ap.add_argument("--attach-effect-row", type=int,
                    help="back only: add a dummy point carrying the effect of this row of "
                         "the source's LIST_WEAPONEFFECT.STB (Jrose col 51 of LIST_BACK), "
                         "placed with its rotation and scale. For Jrose's particle-only back "
                         "items, whose model is a placeholder; implies --allow-placeholder "
                         "and copies the effect's files. Needs the matching client "
                         "(POINT_EFFECT_ORIENTED) for the rotation and scale.")
    ap.add_argument("--allow-placeholder", action="store_true",
                    help="import even if the source model is degenerate geometry (it will "
                         "equip and show nothing); normally such a row is refused")
    ap.add_argument("--target-row", type=int,
                    help="write into this existing blank row instead of appending. "
                         "The point is the 999 ceiling: a packed item code is "
                         "type*1000+no in drop cells (Get_DropITEM), in QSD rewards "
                         "(tagBaseITEM::Init) and in recipe inputs "
                         "(PRODUCT_NEED_ITEM_NO), so an item numbered above 999 can be "
                         "neither dropped, granted by a quest, nor used in a craft. "
                         "Appending walks straight past that once a table is near it -- "
                         "LIST_NATURAL is 981 rows, leaving 19 usable slots -- while the "
                         "table itself has hundreds of blank rows lower down. Refuses a "
                         "row that already carries a name.")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--selftest", action="store_true",
                    help="prove that re-serialising every ZSC we ship reproduces its "
                         "object section byte for byte -- the guarantee --target-row "
                         "rests on, since writing at an index rebuilds that section")
    args = ap.parse_args()

    if args.selftest:
        import glob as _glob
        paths = sorted(_glob.glob(os.path.join(OURS, "3DDATA", "**", "*.ZSC"),
                                  recursive=True))
        bad = zsc_selftest(paths)
        names = ", ".join(os.path.basename(b) for b in bad)
        print("%d ZSC file(s) scanned, %d did not round-trip%s"
              % (len(paths), len(bad), (": " + names) if bad else ""))
        return 1 if bad else 0

    for req in ("source", "source_row"):
        if getattr(args, req) is None:
            ap.error("the following arguments are required: --%s" % req.replace("_", "-"))

    if not os.path.isdir(os.path.join(OURS, "3DDATA")):
        sys.exit("run from the repo root (data/3DDATA not found)")

    ITEM_TYPE, STB_REL, ZSC_RELS, STL_REL, KEY_PREFIX, EXPECT_COLS = TYPES[args.type]

    src_stb = os.path.join(args.source, STB_REL)
    _, _, srows, scols, sdata = stb_read(src_stb)
    if args.source_row >= len(sdata):
        sys.exit("source row %d out of range (%d rows)" % (args.source_row, len(sdata)))
    src = sdata[args.source_row]
    if not src[0] and not args.name and not args.art_only:
        # An empty STB name usually means an unused row -- but not always: QQ-iROSE
        # leaves column 0 blank throughout and keeps names only in the STL, so
        # requiring one there would reject its entire table. --name settles it.
        sys.exit("source row %d has an empty name (unused row?) -- pass --name if the "
                 "source keeps names only in its STL" % args.source_row)
    _, _, orows, ocols, odata = stb_read(os.path.join(OURS, STB_REL))
    if ocols - 1 != EXPECT_COLS:
        sys.exit("our %s has %d data cols, expected %d -- update TYPES before importing"
                 % (os.path.basename(STB_REL), ocols - 1, EXPECT_COLS))
    if not args.art_only:
        # Only the stat-copying path cares that the columns line up; --art-only
        # never reads a source cell, so a foreign schema is irrelevant to it.
        if scols - 1 < ocols - 1:
            sys.exit("source table is narrower (%d) than ours (%d); columns would not line up"
                     % (scols - 1, ocols - 1))
        if scols != ocols:
            print("note: source has %d data cols to our %d; the extra trailing columns are dropped"
                  % (scols - 1, ocols - 1))
    if args.target_row is not None:
        new_id = args.target_row
        # A model table is indexed 1:1 with the STB, so writing in place is only
        # safe when that row's object is genuinely unused -- otherwise we would
        # overwrite another item's art. Every blank row in our tables does have an
        # empty object (checked across all 595 blank LIST_CAP rows), but check
        # rather than trust: the cost of being wrong is a silently reskinned item.
        for rel in ZSC_RELS:
            z = Zsc(os.path.join(OURS, rel))
            if new_id >= len(z.objects):
                sys.exit("--target-row %d is past the end of %s (%d objects); the model "
                         "table would need padding first"
                         % (new_id, os.path.basename(rel), len(z.objects)))
            if z.objects[new_id][1]:
                sys.exit("--target-row %d already carries %d model part(s) in %s -- that "
                         "is another item's art, even though the STB row reads blank. "
                         "Pick a row that is empty in both."
                         % (new_id, len(z.objects[new_id][1]), os.path.basename(rel)))
        if not 0 <= new_id < orows - 1:
            sys.exit("--target-row %d out of range (%d rows)" % (new_id, orows - 1))
        if odata[new_id][0].strip():
            sys.exit("--target-row %d is occupied by %r -- refusing to overwrite"
                     % (new_id, odata[new_id][0].decode("latin-1", "replace")))
    else:
        new_id = orows - 1
    if new_id > 2047:
        sys.exit("new ID %d exceeds the 11-bit item number limit (2047)" % new_id)
    if new_id > 999:
        print("WARNING: id %d is above 999, so this item can never be dropped, granted "
              "by a quest, or used as a recipe input -- see --target-row" % new_id)

    # Every model table must already be in step with the STB, or the new object
    # would not land on the new item number. Checked on both sides: a source
    # table that is short means the row we are copying has no model there.
    # The invariant is against the STB's ROW COUNT, not against new_id. Those are
    # the same number when appending (new_id == orows - 1) and are not when
    # --target-row aims at a blank row in the middle, where comparing to new_id
    # reports every healthy table as corrupt.
    stb_rows = orows - 1
    for rel in ZSC_RELS:
        have = len(Zsc(os.path.join(OURS, rel)).objects)
        if have != stb_rows:
            sys.exit("our %s holds %d objects but %s has %d rows -- the tables are already "
                     "out of step, fix that before importing"
                     % (os.path.basename(rel), have, os.path.basename(STB_REL), stb_rows))
        scount = len(Zsc(os.path.join(args.source, rel)).objects)
        if args.source_row >= scount:
            sys.exit("source %s has only %d objects, no object %d for this row"
                     % (os.path.basename(rel), scount, args.source_row))
    new_key = ("%s%d" % (KEY_PREFIX, new_id)).encode("ascii")

    # name/desc from source STL via the source row's key (last col).
    # --art-only never touches the source STL: its text is ours to write, and
    # skipping the read also sidesteps foreign STL dialects entirely (Jrose
    # writes the legacy `I_NUM` header, which stl_read() below rejects).
    name = desc = b""
    src_key = src[-1] if not args.art_only else b""
    if src_key:
        try:
            skeys, slangs = stl_read(os.path.join(args.source, STL_REL))
            ki = [i for i, (k, _) in enumerate(skeys) if k == src_key]
            if ki:
                # Block 0 is Korean in every data set seen so far; block 1 is the
                # English text. Reading block 0 silently imports Korean names.
                lang = 1 if len(slangs) > 1 and slangs[1][ki[0]][0] else 0
                name, desc = slangs[lang][ki[0]]
        except FileNotFoundError:
            pass
        stb_name = src[0].decode("euc-kr", "replace")
        if stb_name and name and stb_name.split()[:1] != name.decode("utf-8", "replace").split()[:1]:
            print("WARNING: source STL name %r differs from STB name %r -- source data may be "
                  "inconsistent; pass --name/--desc to override" % (name.decode("utf-8", "replace"), stb_name))
    if args.name:
        name = args.name.encode("utf-8")
    if args.desc:
        desc = args.desc.encode("utf-8")
    if not name:
        name = src[0]

    if args.art_only:
        # Take the model and nothing else. Every stat column is cloned from one
        # of OUR existing rows, so the new item can only ever hold values this
        # build already handles -- which is the whole point: a foreign row's
        # bonus/requirement ability ids (cols 24/27 and 19/21) are fed straight
        # into `m_iAddValue[nType] += nValue` on both client and server with no
        # bounds check (src/client/common/cuserdata.cpp, and the gameserver's
        # copy of it). m_iAddValue is int[AT_MAX] followed in CUserDATA by
        # m_nPassiveRate, m_btRecoverHP and m_iDropRATE, so an out-of-range id
        # silently corrupts adjacent character state rather than crashing.
        # Jrose's modern affix ids (174/175/184/185/195) are exactly that.
        if args.template_row is None:
            sys.exit("--art-only needs --template-row: the stat columns are cloned from one "
                     "of our own rows, so pick an existing %s row to base it on"
                     % os.path.basename(STB_REL))
        if not 0 <= args.template_row < len(odata):
            sys.exit("--template-row %d out of range (our %s has %d rows)"
                     % (args.template_row, os.path.basename(STB_REL), len(odata)))
        if not args.name:
            sys.exit("--art-only needs --name: no name is read from the source")
        tmpl = odata[args.template_row]
        print("art-only: model from source row %d, stats cloned from our row %d (%r)"
              % (args.source_row, args.template_row,
                 tmpl[0].decode("euc-kr", "replace")))
        row = list(tmpl[0:ocols - 2]) + [new_key]
    else:
        # build our row: source cols 0..n-2 + our key in the last column
        row = list(src[0:ocols - 2]) + [new_key]
        # The column before the STL key is the STR_ITEMPREFIX id the client
        # prepends to the name (1-8 rare grades, 9 "[Costume]", 10 "[Event]") and
        # colours it by. A cash-shop donor carries its shop tag there: the lv210
        # weapon tier shipped as "[Costume] Arcidian Sword" (alpha #2). Our
        # imports are ordinary items, so start them with no prefix; a rare grade
        # is a deliberate edit afterwards. Only the tables the client reads the
        # column from (CItem::GetItemRareType); subwpn's last-but-one column is
        # something else.
        if args.type in ("weapon", "cap", "body", "arms", "foot"):
            row[ocols - 3] = b""
        # Weapon col 34 selects the animation set (1 = 1H sword, 5 = 2H sword,
        # 8 = 2H axe ...). Sources author it freely -- QQ put a 1H sword on the
        # 2H-sword set, so it was held two-handed here. Use whatever set every
        # weapon of the same type in our own table already uses.
        if args.type == "weapon":
            same = collections.Counter(
                o[34].strip() for o in odata[1:]
                if o[0].strip() and o[4].strip() == row[4].strip() and o[34].strip())
            if same:
                ours = same.most_common(1)[0][0]
                if row[34].strip() != ours:
                    print("motion type %s from the source -> %s, the set our other type-%s "
                          "weapons use" % (row[34].decode() or "(blank)", ours.decode(),
                                           row[4].decode()))
                    row[34] = ours
    row[1] = b""  # vestigial model-path column; the game reads the ZSC instead
    if args.name:
        # Col 0 is what the *server* reports (ITEM_NAME is get_cstr(I, 0) there);
        # the client reads the STL. Override both or they disagree.
        row[0] = args.name.encode("euc-kr", "replace")
    if args.copy_icon and args.icon is not None:
        sys.exit("pass either --copy-icon or --icon, not both")
    if args.copy_icon:
        row[9] = str(copy_source_icon(args.source, int(src[9] or b"0"),
                                      "%s%d" % (KEY_PREFIX.lower(), new_id),
                                      args.dry_run)).encode("ascii")
    elif args.icon is not None:
        row[9] = str(args.icon).encode("ascii")
    elif args.art_only:
        # The template's icon is a real index in our own atlas, so this is a
        # valid placeholder rather than the wrong-art hazard below.
        print("note: no --copy-icon/--icon, reusing the template row's icon %s "
              "(valid, but shared with that item)" % row[9].decode())
    else:
        print("WARNING: keeping source icon index %s -- points at different art in our atlas, "
              "pass --copy-icon or --icon" % src[9].decode())

    # Stat overrides. Worth using on anything from an evo-era source: their gear
    # is scaled roughly 4x ours (their entry lv200 plate set totals 1915 DEF to
    # our Jabberwock's 491), so importing verbatim drops our existing monsters to
    # the damage floor.
    if args.price is not None:
        # Column 5 (ITEM_BASE_PRICE). Worth its own flag for materials: --art-only
        # clones every stat from one template, and for a material the price *is*
        # most of what distinguishes it -- Graphistone is 50,000z where a Black
        # Iron Gear is 100z.
        print("PRICE: %s -> %d" % (row[5].decode() or "0", args.price))
        row[5] = str(args.price).encode("ascii")
    if args.defence is not None:
        print("DEFENCE: source %s -> %d" % (row[31].decode() or "0", args.defence))
        row[31] = str(args.defence).encode("ascii")
    if args.res is not None:
        print("RESISTENCE: source %s -> %d" % (row[32].decode() or "0", args.res))
        row[32] = str(args.res).encode("ascii")
    if args.atk is not None or args.atk_speed is not None:
        if args.type != "weapon":
            sys.exit("--atk/--atk-speed only apply to --type weapon")
        if args.atk is not None:
            print("ATTACK_POWER: source %s -> %d" % (row[35].decode() or "0", args.atk))
            row[35] = str(args.atk).encode("ascii")
        if args.atk_speed is not None:
            print("ATTACK_SPEED: source %s -> %d (attack_speed %d -> %d)"
                  % (row[36].decode() or "0", args.atk_speed,
                     1500 // (int(row[36] or b"0") + 5), 1500 // (args.atk_speed + 5)))
            row[36] = str(args.atk_speed).encode("ascii")
    if args.req_level is not None:
        # Requirements are (type, value) pairs at cols 19/20 and 21/22; type 31
        # is character level. Retarget the existing pair if there is one so we
        # don't leave two conflicting level requirements behind.
        slot = None
        for c in (19, 21):
            if row[c].strip() == b"31":
                slot = c
                break
        if slot is None:
            for c in (19, 21):
                if not row[c].strip():
                    slot = c
                    row[c] = b"31"
                    break
        if slot is None:
            sys.exit("both NEED_DATA slots are used by non-level requirements; "
                     "cannot set --req-level without dropping one")
        print("required level: source %s -> %d" % (row[slot + 1].decode() or "0", args.req_level))
        row[slot + 1] = str(args.req_level).encode("ascii")

    if args.req_stat:
        # The other half of the requirement pair. Kept separate from --req-level
        # because they occupy the same two slots and a caller usually wants both:
        # setting only the level leaves the stat gate at whatever the template
        # had, which is how level-170 shields inherited an endgame STR bar.
        try:
            rid, rval = (int(x) for x in args.req_stat.split(":", 1))
        except ValueError:
            sys.exit("--req-stat wants ID:VALUE, e.g. 10:320")
        if rid == 31:
            sys.exit("--req-stat is for the non-level requirement; use --req-level for 31")
        slot = None
        for c in (19, 21):                       # reuse a slot already holding this id
            if row[c].strip() == str(rid).encode("ascii"):
                slot = c
                break
        if slot is None:                         # else a slot not holding the level
            for c in (19, 21):
                if row[c].strip() != b"31":
                    slot = c
                    row[c] = str(rid).encode("ascii")
                    break
        if slot is None:
            sys.exit("both requirement slots hold a level requirement; cannot set --req-stat")
        print("required stat: %s -> %d:%d" % (row[slot + 1].decode() or "0", rid, rval))
        row[slot + 1] = str(rval).encode("ascii")

    if args.bonus:
        # Bonus pairs live at cols 24/25 and 27/28 and are ability ids fed into
        # the unbounded m_iAddValue[nType] += nValue write on both client and
        # server, so the id is range-checked here rather than trusted.
        if len(args.bonus) > 2:
            sys.exit("at most 2 --bonus pairs (cols 24/25 and 27/28)")
        for n, spec in enumerate(args.bonus):
            try:
                bid, bval = (int(x) for x in spec.split(":", 1))
            except ValueError:
                sys.exit("--bonus wants ID:VALUE, e.g. 20:25")
            if not 0 < bid < AT_MAX_GUARD:
                sys.exit("--bonus id %d is outside 1..%d; an out-of-range ability id writes "
                         "past m_iAddValue[] and corrupts adjacent character state"
                         % (bid, AT_MAX_GUARD - 1))
            col = 24 if n == 0 else 27
            old_id = row[col].decode().strip()
            old_val = row[col + 1].decode().strip()
            print("bonus %d: %s:%s -> %d:%d"
                  % (n + 1, old_id or "-", old_val or "-", bid, bval))
            # Slot 1 is where every one of our level-230 flagships keeps its ATK
            # bonus (+10 on most types, +30 on staff/wand/katar, +90 on dual), and
            # in --art-only the template's slots are already populated -- so the
            # first --bonus overwrites it. That happened silently across two Jrose
            # weapon batches and left five items weaker than the lower-level
            # weapon they were scaled up from; see
            # scripts/fix-weapon-atk-bonus-loss.py. There are only two slots, so
            # this cannot be fixed by relocating; say loudly what is being lost.
            if old_id.isdigit() and int(old_id) != bid and int(old_id) != 0:
                print("  WARNING: this overwrites the template's %s bonus (%s:%s). "
                      "There are only two bonus slots, so it cannot be kept as well -- "
                      "if it is an ATK bonus (id 18), fold it into --atk or the item "
                      "lands below its own template."
                      % ("ATK" if int(old_id) == AT_ATK else "id %s" % old_id,
                         old_id, old_val or "0"))
            row[col] = str(bid).encode("ascii")
            row[col + 1] = str(bval).encode("ascii")

    if not args.copy_field_model:
        field_count = len(Zsc(os.path.join(OURS, FIELD_ZSC_REL)).objects)
        if args.field_model is not None:
            row[10] = str(args.field_model).encode("ascii")
        fm = int(row[10] or b"0")
        if fm >= field_count:
            sys.exit("field model %d out of range (our LIST_FieldITEM.ZSC has %d objects); "
                     "pass --field-model or --copy-field-model" % (fm, field_count))

    # In --art-only the source name is never read, and blindly decoding it as
    # euc-kr would print mojibake for a cp932 source (Jrose) -- so say which row
    # the model came from instead of guessing at its text.
    what = ("source row %d" % args.source_row if args.art_only
            else repr(src[0].decode("euc-kr", "replace")))
    print("importing %s as ID %d (key %s, name %r)"
          % (what, new_id, new_key.decode(), name.decode("utf-8", "replace")))

    # backups
    if not args.dry_run:
        rels = ([STB_REL, STL_REL] + list(ZSC_RELS)
                + ([FIELD_ZSC_REL] if args.copy_field_model else []))
        for rel in rels:
            p = os.path.join(OURS, rel)
            bak = p + ".import-%d.bak" % new_id
            if not os.path.exists(bak):
                shutil.copy2(p, bak)

    # Sex-split armour appends to both tables and they must stay index-aligned.
    # Build every table before writing any of them, so an error in the second
    # cannot leave the first already extended.
    pending = []
    empty_models = set()
    zsc_counts_before = {rel: len(Zsc(os.path.join(OURS, rel)).objects)
                         for rel in ZSC_RELS}
    extra_dummies = ()
    if args.attach_effect_row is not None:
        if args.type != "back":
            sys.exit("--attach-effect-row is for back items only")
        extra_dummies = (weapon_effect_dummy(args.source, args.attach_effect_row),)
    for rel in ZSC_RELS:
        src_zsc = Zsc(os.path.join(args.source, rel))
        if not src_zsc.objects[args.source_row][1]:
            empty_models.add(rel)
        elif object_is_placeholder(args.source, src_zsc, args.source_row) \
                and not args.allow_placeholder and not extra_dummies:
            sys.exit("source object %d in %s is a placeholder -- every part is degenerate "
                     "geometry (a few vertices in a near-zero bounding box), so the item "
                     "would equip and show nothing. The source most likely draws this "
                     "through a system we do not have. Pick a different row, or pass "
                     "--allow-placeholder if you really mean it."
                     % (args.source_row, os.path.basename(rel)))
        obj_id, files_needed, blob = zsc_build_append(
            os.path.join(OURS, rel), src_zsc, args.source_row,
            args.source, args.copy_effects, target=args.target_row,
            extra_dummies=extra_dummies)
        if obj_id != new_id:
            sys.exit("STB/ZSC index drift in %s: row %d vs object %d"
                     % (os.path.basename(rel), new_id, obj_id))
        pending.append((rel, files_needed, blob))
    for rel, files_needed, blob in pending:
        if not args.dry_run:
            with open(os.path.join(OURS, rel), "wb") as fh:
                fh.write(blob)
        print("model: %s object %d -> our %d" % (os.path.basename(rel), args.source_row, new_id))
        copy_assets(files_needed, args.source, args.dry_run)

    if args.copy_field_model:
        src_fm = int(src[10] or b"0")
        src_field = Zsc(os.path.join(args.source, FIELD_ZSC_REL))
        usable = 0 < src_fm < len(src_field.objects)
        # Index 0 is the "no ground-drop model" convention and plenty of cosmetic
        # rows use it. In --art-only the template already supplied a field model
        # that is valid in OUR ZSC, so degrade to that rather than failing a whole
        # import over a drop mesh. In stat-copy mode there is no such fallback --
        # row[10] would be a source index meaning nothing here -- so it stays fatal.
        if not usable and not args.art_only:
            sys.exit("source field model %d out of range (%d objects)"
                     % (src_fm, len(src_field.objects)))
        if not usable:
            print("note: source row has no ground-drop model (%d); keeping the "
                  "template's field model %s" % (src_fm, row[10].decode() or "0"))
        else:
            field_path = os.path.join(OURS, FIELD_ZSC_REL)
            field_id, ffiles, fblob = zsc_build_append(field_path, src_field, src_fm)
            if not args.dry_run:
                with open(field_path, "wb") as fh:
                    fh.write(fblob)
            copy_assets(ffiles, args.source, args.dry_run)
            row[10] = str(field_id).encode("ascii")
            print("field model: ported source object %d as our %d" % (src_fm, field_id))

    if args.target_row is not None:
        stb_write_row(os.path.join(OURS, STB_REL), new_id, row, args.dry_run)
    else:
        stb_append_row(os.path.join(OURS, STB_REL), row, args.dry_run)
    stl_append(os.path.join(OURS, STL_REL), new_key, new_id, name, desc, args.dry_run)

    # verify
    if not args.dry_run:
        _, _, vrows, vcols, vdata = stb_read(os.path.join(OURS, STB_REL))
        # An append lands on the last row and grows the table by one; an in-place
        # --target-row write does neither, so the row count is the wrong invariant
        # for it -- assert that the table did NOT move instead.
        if args.target_row is None:
            assert vrows - 1 == new_id + 1, (
                "table has %d rows, expected %d" % (vrows - 1, new_id + 1))
        else:
            assert vrows == orows, (
                "in-place write changed the row count: %d -> %d" % (orows, vrows))
        assert vdata[new_id][vcols - 2] == new_key
        for rel in ZSC_RELS:
            vz = Zsc(os.path.join(OURS, rel))
            # The object count is the invariant that actually matters; a model
            # may legitimately be empty (the source has no art for that sex), in
            # which case the placeholder we appended has no parts by design.
            if args.target_row is None:
                assert len(vz.objects) == new_id + 1, (
                    "%s ended with %d objects, expected %d"
                    % (rel, len(vz.objects), new_id + 1))
            else:
                # in place: the table must NOT have grown, and the row we aimed
                # at is the one that changed
                assert len(vz.objects) == zsc_counts_before[rel], (
                    "%s changed size on an in-place write: %d -> %d"
                    % (rel, zsc_counts_before[rel], len(vz.objects)))
            if rel not in empty_models:
                assert vz.objects[new_id][1], "%s object %d has no parts" % (rel, new_id)
            # Every index we wrote must be in range for the list it points into.
            # The client bounds-checks none of them: a mesh/material index runs
            # off the end of its array and a dummy point's effect index is read
            # straight out of pEftKEY, which is NULL when the table declares no
            # effects -- a null deref inside CGame::Load_BasicDATA, i.e. a client
            # that dies at the title screen having written nothing to error.txt.
            # Scanned over the whole table, so it also catches earlier damage.
            for oi, (_cyl, vparts, vdummies, _bb) in enumerate(vz.objects):
                for mid, tid, _p in vparts:
                    assert mid < len(vz.meshes) and tid < len(vz.materials), (
                        "%s object %d references mesh %d / material %d of %d / %d"
                        % (rel, oi, mid, tid, len(vz.meshes), len(vz.materials)))
                for a, _p in vdummies:
                    eidx = struct.unpack("<h", a[:2])[0]
                    assert eidx < len(vz.effects), (
                        "%s object %d has a dummy point referencing effect %d but the "
                        "table declares %d -- this crashes the client on load"
                        % (rel, oi, eidx, len(vz.effects)))
        vkeys, vlangs = stl_read(os.path.join(OURS, STL_REL))
        assert vkeys[-1][0] == new_key and vlangs[0][-1][0] == name
        print("verified: STB row, ZSC object and STL entry all present")
        print("\nDONE - restart servers, then spawn with: /item %d:%d" % (ITEM_TYPE, new_id))
    else:
        print("\nDRY RUN - no files written")

if __name__ == "__main__":
    main()
