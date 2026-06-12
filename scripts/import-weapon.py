"""Import a weapon from another ROSE data set into ours, appended as a new weapon ID.

Usage:
    python scripts/import-weapon.py --source C:\\path\\to\\other\\data --source-row 873 --icon 1436
    python scripts/import-weapon.py --source ... --source-row 873 --dry-run

What it does (all appends -- existing rows/objects are never modified):
    1. LIST_WEAPON.STB   append one row (new ID = current row count) with the source stats
    2. LIST_WEAPON.ZSC   append the source's model object; meshes/materials already present
                         in our ZSC are reused, missing ones are appended
    3. LIST_WEAPON_S.STL append key LWEA<id> with name/desc taken from the source STL
    4. copies any mesh/texture files referenced by the appended ZSC entries from the
       source data dir into ours (same relative paths)

Structure facts this relies on (verified against the live client/server loaders):
    - Weapon visuals come from LIST_WEAPON.ZSC object[item_no]; the STB "model file"
      column 1 (.txt path) is vestigial and never read by the game.
    - Our LIST_WEAPON.STB has 46 data columns, key in col 45. Evo-era sources have 49;
      their 3 extra columns (45-47) are dropped. Columns 0-44 align 1:1.
    - STBDATA::load (src/common/src/io/stb.cpp) reads (rows-1)x(cols-1) u16-length
      cells at `offset`; the header before `offset` holds col titles + row names that
      STB editors read, so the row-name list gets a matching new entry.
    - Icon indices (STB col 9) point into OUR ITEM1.TSI atlas (50 sheets x 169 cells,
      512x512 DXT5, 40x40 cells); source icon indices map to different art, so pass
      --icon explicitly (defaults to keeping the source value, with a warning).
    - Field model (STB col 10) indexes our LIST_FieldITEM.ZSC (ground-drop mesh);
      pass --copy-field-model to port the source's drop model too (recommended), or
      --field-model N to reuse one of ours (749 = dual sword drop).

After running: restart servers + client, spawn with /item 8:<new id> (GM level 2048).
"""
import argparse, io, os, shutil, struct, sys

OURS = "data"
STB_REL = r"3DDATA\STB\LIST_WEAPON.STB"
ZSC_REL = r"3DDATA\WEAPON\LIST_WEAPON.ZSC"
STL_REL = r"3DDATA\STB\LIST_WEAPON_S.STL"
FIELD_ZSC_REL = r"3DDATA\ITEM\LIST_FieldITEM.ZSC"

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

def zsc_append_object(ours_path, src_zsc, src_obj_idx, dry):
    ours = Zsc(ours_path)
    cyl, sparts, sdummies, sbb = src_zsc.objects[src_obj_idx]
    if not sparts:
        sys.exit("source ZSC object %d has no parts (no model)" % src_obj_idx)

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
    obj.append(struct.pack("<H", len(sdummies)))
    for a, props in sdummies:
        obj += [a, props]
    obj.append(sbb)

    out = [struct.pack("<H", len(ours.meshes) + len(new_meshes)),
           ours.d[2:ours.mesh_end]]
    out += [m + b"\x00" for m in new_meshes]
    out += [struct.pack("<H", len(ours.materials) + len(new_mats)),
            ours.d[ours.mesh_end + 2:ours.mat_end]]
    out += [p + b"\x00" + flags for p, flags in new_mats]
    out += [ours.d[ours.mat_end:ours.objcnt_pos],
            struct.pack("<H", len(ours.objects) + 1),
            ours.d[ours.objcnt_pos + 2:ours.obj_end],
            b"".join(obj)]
    if not dry:
        with open(ours_path, "wb") as fh:
            fh.write(b"".join(out))
    files_needed = [m for m in new_meshes] + [p for p, _ in new_mats]
    return len(ours.objects), files_needed

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
        print("copied: %s" % rel)

# ---------------------------------------------------------------- main
def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--source", required=True, help="path to the source data directory")
    ap.add_argument("--source-row", type=int, required=True, help="weapon row in the source LIST_WEAPON.STB")
    ap.add_argument("--name", help="override item name (default: from source STL)")
    ap.add_argument("--desc", help="override item description (default: from source STL)")
    ap.add_argument("--icon", type=int, help="icon index in OUR ITEM1.TSI (default: keep source value, likely wrong art)")
    ap.add_argument("--field-model", type=int, help="ground-drop model index in OUR LIST_FieldITEM.ZSC")
    ap.add_argument("--copy-field-model", action="store_true",
                    help="port the source's ground-drop model object into our LIST_FieldITEM.ZSC")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    if not os.path.isdir(os.path.join(OURS, "3DDATA")):
        sys.exit("run from the repo root (data/3DDATA not found)")

    src_stb = os.path.join(args.source, STB_REL)
    _, _, srows, scols, sdata = stb_read(src_stb)
    if args.source_row >= len(sdata):
        sys.exit("source row %d out of range (%d rows)" % (args.source_row, len(sdata)))
    src = sdata[args.source_row]
    if not src[0]:
        sys.exit("source row %d has an empty name (unused row?)" % args.source_row)
    if scols - 1 not in (46, 49):
        print("WARNING: source has %d data cols (expected 46 or 49); cols 0-44 assumed classic layout" % (scols - 1))

    _, _, orows, ocols, odata = stb_read(os.path.join(OURS, STB_REL))
    assert ocols - 1 == 46, "our LIST_WEAPON.STB layout changed (cols=%d)" % (ocols - 1)
    new_id = orows - 1
    if new_id > 2047:
        sys.exit("new ID %d exceeds the 11-bit item number limit (2047)" % new_id)
    new_key = ("LWEA%d" % new_id).encode("ascii")

    # name/desc from source STL via the source row's key (last col)
    name = desc = b""
    src_key = src[-1]
    if src_key:
        try:
            skeys, slangs = stl_read(os.path.join(args.source, STL_REL))
            ki = [i for i, (k, _) in enumerate(skeys) if k == src_key]
            if ki:
                name, desc = slangs[0][ki[0]]
        except FileNotFoundError:
            pass
    stb_name = src[0].decode("euc-kr", "replace")
    if name and stb_name.split()[:1] != name.decode("utf-8", "replace").split()[:1]:
        print("WARNING: source STL name %r differs from STB name %r -- source data may be "
              "inconsistent; pass --name/--desc to override" % (name.decode("utf-8", "replace"), stb_name))
    if args.name:
        name = args.name.encode("utf-8")
    if args.desc:
        desc = args.desc.encode("utf-8")
    if not name:
        name = src[0]

    # build our 46-col row: source cols 0-44 + our key
    row = list(src[0:45]) + [new_key]
    row[1] = b""  # vestigial model-path column; the game reads the ZSC instead
    if args.icon is not None:
        row[9] = str(args.icon).encode("ascii")
    else:
        print("WARNING: keeping source icon index %s -- points at different art in our atlas, pass --icon" % src[9].decode())
    if not args.copy_field_model:
        field_count = len(Zsc(os.path.join(OURS, FIELD_ZSC_REL)).objects)
        if args.field_model is not None:
            row[10] = str(args.field_model).encode("ascii")
        fm = int(row[10] or b"0")
        if fm >= field_count:
            sys.exit("field model %d out of range (our LIST_FieldITEM.ZSC has %d objects); "
                     "pass --field-model or --copy-field-model" % (fm, field_count))

    print("importing %r as ID %d (key %s, name %r)" % (src[0].decode("euc-kr", "replace"), new_id, new_key.decode(), name.decode("utf-8", "replace")))

    # backups
    if not args.dry_run:
        rels = [STB_REL, ZSC_REL, STL_REL] + ([FIELD_ZSC_REL] if args.copy_field_model else [])
        for rel in rels:
            p = os.path.join(OURS, rel)
            bak = p + ".import-%d.bak" % new_id
            if not os.path.exists(bak):
                shutil.copy2(p, bak)

    src_zsc = Zsc(os.path.join(args.source, ZSC_REL))
    if args.source_row >= len(src_zsc.objects):
        sys.exit("source ZSC has no object %d" % args.source_row)
    obj_id, files_needed = zsc_append_object(os.path.join(OURS, ZSC_REL), src_zsc, args.source_row, args.dry_run)
    assert obj_id == new_id, "STB/ZSC index drift: row %d vs object %d" % (new_id, obj_id)
    copy_assets(files_needed, args.source, args.dry_run)

    if args.copy_field_model:
        src_fm = int(src[10] or b"0")
        src_field = Zsc(os.path.join(args.source, FIELD_ZSC_REL))
        if src_fm <= 0 or src_fm >= len(src_field.objects):
            sys.exit("source field model %d out of range (%d objects)" % (src_fm, len(src_field.objects)))
        field_id, ffiles = zsc_append_object(os.path.join(OURS, FIELD_ZSC_REL), src_field, src_fm, args.dry_run)
        copy_assets(ffiles, args.source, args.dry_run)
        row[10] = str(field_id).encode("ascii")
        print("field model: ported source object %d as our %d" % (src_fm, field_id))

    stb_append_row(os.path.join(OURS, STB_REL), row, args.dry_run)
    stl_append(os.path.join(OURS, STL_REL), new_key, new_id, name, desc, args.dry_run)

    # verify
    if not args.dry_run:
        _, _, vrows, _, vdata = stb_read(os.path.join(OURS, STB_REL))
        assert vrows - 1 == new_id + 1 and vdata[new_id][45] == new_key
        vz = Zsc(os.path.join(OURS, ZSC_REL))
        assert len(vz.objects) == new_id + 1 and vz.objects[new_id][1]
        vkeys, vlangs = stl_read(os.path.join(OURS, STL_REL))
        assert vkeys[-1][0] == new_key and vlangs[0][-1][0] == name
        print("verified: STB row, ZSC object and STL entry all present")
        print("\nDONE - restart servers, then spawn with: /item 8:%d" % new_id)
    else:
        print("\nDRY RUN - no files written")

if __name__ == "__main__":
    main()
