"""ZSC model tables (``LIST_*.ZSC``, ``PART_*.ZSC``): meshes, materials, objects.

Layout::

    u16 mesh_count + NUL-terminated paths
    u16 material_count + (NUL-terminated path, 36B flags)
    u16 effect_count + NUL-terminated paths
    u16 object_count + objects

Each object: ``12B cylinder | u16 part_count | parts | u16 dummy_count | dummies
| 24B bbox``. A part is ``(u16 mesh, u16 material, proplist)``; a dummy is
``(4B, proplist)``; a proplist is ``(u8 tag, u8 len, bytes)*`` ended by a zero
tag. **An object with zero parts ends right after its count** -- no dummies, no
bbox (``CMODEL::Load`` returns early) -- so the part count is tested before
reading on, and ``serialize_object`` writes the same shape back.

Property tags ``SWITCH_ANI .. SWITCH_ANI+MAX_MESH_ANI_TYPE-1`` (8..28) and
``SWITCH_CNST_ANI`` (30) hold *file paths*: ``.zmo`` motions for animated
scenery (the Muris carts). Collecting only meshes and materials leaves those
behind, and the client pops a modal "open error" box per file during map load.
``asset_refs`` includes them. The effect list doubles as a *light* list: entries
not starting with ``3D`` are light names, not files.

Weapon visuals are ``LIST_WEAPON.ZSC`` indexed by item number (1:1 with STB
rows); the STB "model file" column is vestigial. The cached bbox is **wrong in
every ZSC in the game** (Y and Z left in mesh units) -- the client culls from
real mesh bounds; see doc/zsc-bounding-boxes.md.

Third-party editors leave junk after the last object (``trailing`` bytes). The
game reads the declared count and stops; ``to_bytes`` drops the junk, so the
round-trip check compares against the file up to ``obj_end``.
"""
import io
import struct

from .binio import read_file

NUL = b"\x00"
SWITCH_ANI, MAX_MESH_ANI_TYPE = 8, 21
SWITCH_CNST_ANI = SWITCH_ANI + MAX_MESH_ANI_TYPE + 1                # 30
PROP_PATH_TAGS = set(range(SWITCH_ANI, SWITCH_ANI + MAX_MESH_ANI_TYPE)) | {SWITCH_CNST_ANI}

# A dummy point whose effect the client places with its rotation and scale as
# well as its position (client io_model.h, POINT_EFFECT_ORIENTED). Plain points
# (0) have only ever been placed by position.
POINT_EFFECT_ORIENTED = 3


class Zsc:
    def __init__(self, path):
        self.path = path if not isinstance(path, (bytes, bytearray)) else None
        self.d = read_file(path)
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
        self.trailing = len(self.d) - self.obj_end

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

    # ------------------------------------------------------------ writing
    @staticmethod
    def object_bytes(cyl, parts, dummies, bb):
        return serialize_object((cyl, parts, dummies, bb))

    def objects_blob(self):
        """The object section rebuilt from ``self.objects`` (no count)."""
        return b"".join(serialize_object(o) for o in self.objects)

    def to_bytes(self, extra_meshes=(), extra_mats=(), extra_objects=()):
        """The file with extra records appended to each list. Existing records
        are spliced from the original bytes, so they cannot change."""
        out = [struct.pack("<H", len(self.meshes) + len(extra_meshes)),
               self.d[2:self.mesh_end]]
        out += [m + NUL for m in extra_meshes]
        out += [struct.pack("<H", len(self.materials) + len(extra_mats)),
                self.d[self.mesh_end + 2:self.mat_end]]
        out += [p + NUL + flags for p, flags in extra_mats]
        out += [self.d[self.mat_end:self.objcnt_pos],
                struct.pack("<H", len(self.objects) + len(extra_objects)),
                self.d[self.objcnt_pos + 2:self.obj_end]]
        out += list(extra_objects)
        return b"".join(out)

    def rebuild(self, effects=None):
        """The file with the object section rebuilt from ``self.objects`` --
        for writing *at* an index (``import-item.py --target-row``). Meshes and
        materials are spliced from the original bytes, and so are the effects
        unless ``effects`` gives the list to write instead (paths as bytes; a
        dummy names its effect by index, so keep the order to re-point one)."""
        head = self.d[:self.objcnt_pos] if effects is None else (
            self.d[:self.mat_end] + struct.pack("<H", len(effects))
            + b"".join(e + NUL for e in effects))
        return head + struct.pack("<H", len(self.objects)) + self.objects_blob()

    def without(self, mesh_idx=(), mat_idx=()):
        """The file with these mesh and material records removed, every object's
        part indices remapped past the gap. A removed record must be referenced by
        no object left (empty its objects first). Effects are spliced as they are."""
        drop_m, drop_t = set(mesh_idx), set(mat_idx)
        for i, (_c, parts, _d, _b) in enumerate(self.objects):
            for m, t, _p in parts:
                if m in drop_m or t in drop_t:
                    raise ValueError("object %d still uses a removed record" % i)
        mmap, tmap = {}, {}
        for old in range(len(self.meshes)):
            if old not in drop_m:
                mmap[old] = len(mmap)
        for old in range(len(self.materials)):
            if old not in drop_t:
                tmap[old] = len(tmap)
        meshes = [m for i, m in enumerate(self.meshes) if i not in drop_m]
        mats = [x for i, x in enumerate(self.materials) if i not in drop_t]
        objects = [(c, [(mmap[m], tmap[t], p) for m, t, p in parts], d, b)
                   for c, parts, d, b in self.objects]
        out = [struct.pack("<H", len(meshes))] + [m + NUL for m in meshes]
        out += [struct.pack("<H", len(mats))] + [p + NUL + flags for p, flags in mats]
        out += [self.d[self.mat_end:self.objcnt_pos], struct.pack("<H", len(objects))]
        out += [serialize_object(o) for o in objects]
        return b"".join(out)

    def rebuild_with(self, extra_meshes=(), extra_mats=()):
        """``rebuild()`` plus records appended to the mesh and material lists --
        for replacing an object in place with one that needs paths the table
        does not hold yet. ``extra_mats`` are ``(path, 36-byte flags)`` pairs,
        and the objects in ``self.objects`` index them past the existing ones."""
        out = [struct.pack("<H", len(self.meshes) + len(extra_meshes)),
               self.d[2:self.mesh_end]]
        out += [m + NUL for m in extra_meshes]
        out += [struct.pack("<H", len(self.materials) + len(extra_mats)),
                self.d[self.mesh_end + 2:self.mat_end]]
        out += [p + NUL + flags for p, flags in extra_mats]
        out += [self.d[self.mat_end:self.objcnt_pos],
                struct.pack("<H", len(self.objects)),
                self.objects_blob()]
        return b"".join(out)


def serialize_object(o):
    """One object record, exactly as ``Zsc.__init__`` parsed it."""
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


# Part / dummy property tags (client io_model.cpp CFixedPART::Load / CCharPART::Load)
TAG_POS, TAG_ROT, TAG_SCALE, TAG_AXIS_ROT, TAG_BONE, TAG_DUMMY, TAG_PARENT = 1, 2, 3, 4, 5, 6, 7
TAG_COLLISION = 29


def decode_props(props):
    """A proplist as a dict: ``pos`` (3f), ``rot`` as (x, y, z, w) -- **the file
    stores w, x, y, z** -- ``scale`` (3f), ``parent`` (1-based in the file, -1 =
    root here), ``collision`` (i16), ``bone``/``dummy`` (i16), and ``raw``
    {tag: bytes} for every other tag (the motion paths among them)."""
    out = {"pos": (0.0, 0.0, 0.0), "rot": (0.0, 0.0, 0.0, 1.0), "scale": (1.0, 1.0, 1.0),
           "parent": -1, "collision": 0, "raw": {}}
    o = 0
    while o < len(props):
        tag = props[o]
        o += 1
        if tag == 0:
            break
        ln = props[o]
        o += 1
        val = props[o:o + ln]
        o += ln
        try:
            if tag == TAG_POS and ln >= 12:
                out["pos"] = struct.unpack_from("<3f", val)
            elif tag == TAG_ROT and ln >= 16:
                w, x, y, z = struct.unpack_from("<4f", val)
                out["rot"] = (x, y, z, w)
            elif tag == TAG_SCALE and ln >= 12:
                out["scale"] = struct.unpack_from("<3f", val)
            elif tag == TAG_PARENT and ln >= 2:
                out["parent"] = struct.unpack_from("<h", val)[0] - 1
            elif tag == TAG_COLLISION and ln >= 2:
                out["collision"] = struct.unpack_from("<h", val)[0]
            elif tag == TAG_BONE and ln >= 2:
                out["bone"] = struct.unpack_from("<h", val)[0]
            elif tag == TAG_DUMMY and ln >= 2:
                out["dummy"] = struct.unpack_from("<h", val)[0]
            else:
                out["raw"][tag] = val
        except struct.error:
            out["raw"][tag] = val
    return out


def material_flags(flags):
    """The 36-byte material record after the texture path (the editor's ZSC.cs):
    nine i16 -- skin, alpha, two_sided, alpha_test, alpha_ref, zwrite, ztest,
    blend (0 none, 1 lighten, 2 normal), specular -- then f32, i16 and 3f kept
    raw as ``extra``. 96% of materials are alpha-tested and the flags are
    unreliable as a whole (doc: Transparency Antialiasing)."""
    f = struct.unpack_from("<9h", flags, 0)
    return {"skin": f[0], "alpha": f[1], "two_sided": f[2], "alpha_test": f[3],
            "alpha_ref": f[4], "zwrite": f[5], "ztest": f[6], "blend": f[7],
            "specular": f[8], "extra": bytes(flags[18:36])}


def prop_paths(props):
    """File paths held in a proplist (part motions)."""
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


def asset_refs(z):
    """Every file a ZSC table names: meshes, materials, effects, part motions."""
    out = {m.decode("latin-1") for m in z.meshes}
    out |= {p.decode("latin-1") for p, _ in z.materials}
    out |= {e.decode("latin-1") for e in z.effects if e[:2].upper() == b"3D"}
    for cyl, parts, dummies, bb in z.objects:
        for _, _, props in parts:
            out |= set(prop_paths(props))
        for _, props in dummies:
            out |= set(prop_paths(props))
    return {x for x in out if "\\" in x or "/" in x}


def selftest(paths):
    """Re-serialising every object must reproduce the section byte for byte."""
    bad = []
    for path in paths:
        z = Zsc(path)
        if z.objects_blob() != z.d[z.objcnt_pos + 2:z.obj_end]:
            bad.append(path)
    return bad
