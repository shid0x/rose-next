"""The Karkia catacombs' art and layout: wall, pillar and medallion models, each crypt's
decoration table, and where the pieces stand. Used by scripts/import-catacombs.py.

The art is Jrose's catacomb set (3Ddata/KARKIA/KCatacomb, KCatacomb2), built for 15 m
cells as 1 m models that its client scales by 15. We keep that: the models here are
1 m "unit" models and every placement carries a scale of 15.

Why the wall is our own mesh
----------------------------
Jrose's straight wall ("wall C") is **one-sided**: its faces look one way only, because
in Jrose a wall backs onto a solid cell of rock. Ours stands on the edge between two
corridors and is seen from both. So each crypt's wall model is built here from wall C:

* the piece is moved so that its deepest faces (the backs of the burial niches, 33.3 cm
  from Jrose's cell centre) lie on our edge line, and a second copy is turned half a
  turn about that line: two faces back to back, 23 cm between their surfaces (3.5 m at
  scale 15), each one's niches staying in its own half;
* the column at the piece's end is dropped: a pillar stands at every grid corner
  instead, and hides the joints;
* a cap closes the top, since the piece has none.

One model, one object, so a wall is one placement: one IFO record now, one zone object
when the walls become daily (milestone 3). The collision is the faces' own (flag 12,
as in Jrose).

The pillar is a plain box wearing the column strip of the same texture, a little wider
than the wall and a little taller, sized so that no face of one lies in a face of the
other (PILLAR_HALF).
The medallions are Jrose's round floor pieces, without collision, laid just above the
ground in the entrance hall and the boss room.
"""
import math
import os
import struct
import subprocess
import tempfile

import numpy as np

from rosefmt import dds, ifo, zms
from rosefmt.zsc import Zsc, serialize_object
from mapgen import lighting

import catacomb_maze as maze

JROSE = r"C:\Users\Thomas\Desktop\Testclients\Jrose"
ART_DIR_REL = r"3DDATA\KARKIA\KCATACOMB"
SCALE = 15.0                       # unit models -> 15 m cells

# Unit-model measures, metres (a cell is 1 m).
EDGE_PLANE = 0.333                 # wall C's deepest faces (Reminiscence's niche backs): our edge line
COLUMN_FROM = 0.419                # triangles wholly past this x are the end column
WALL_TOP = 1.5
CAP_HALF = 0.132                   # the cap reaches a little past the cornices
# 3.9 m square at scale 15. **Chosen against the wall's own face planes**: a pillar face
# in the plane of a wall face flickers (the first cut, 0.14, put the pillar's sides at
# 0.36 from the wall's middle, exactly the end face of the last burial niche: seen in
# game 2026-11-10). The wall has faces parallel to it at 0.117 (surface), 0.147 (ledges)
# and 0.167 (cornice, base); Sorrow's panel at 0.138; and faces across it at 0.34-0.36
# from its middle and closer in. 0.13 puts the pillar's faces at 0.13 and 0.37: 15 cm or
# more (at scale 15) from every one. Re-check with the planes if this ever changes.
PILLAR_HALF = 0.13
PILLAR_TOP = 1.53                  # above the wall's cap: no shared plane
# The wall texture's layout (both sets): the face on the left, the column strip on the
# right, a small plain patch under it.
COLUMN_UV = (0.70, 0.03, 0.96, 0.74)    # u0, v0 (top), u1, v1 (foot)
PATCH_UV = (0.70, 0.80, 0.93, 0.88)
MEDALLION_Z_CM = 8.0               # over the flat ground: no z-fight, feet barely sink

OBJ_WALL, OBJ_PILLAR, OBJ_MEDAL_HALL, OBJ_MEDAL_BOSS = 1, 2, 3, 4

SETS = {
    90: dict(prefix="KC1", zsc="LIST_DECO_KCATACOMB1.ZSC", src_zsc="KCATACOMB0101.ZSC",
             # (source mesh, source texture, our name, collides)
             wall=[(r"3Ddata\KARKIA\KCatacomb\WALL\WALL_C_1_1.ZMS", r"3Ddata\KARKIA\KCatacomb\WALL\WALL_010.DDS",
                    "WALL", True)],
             medal_mesh=r"3Ddata\KARKIA\KCatacomb\FLOOR\FLOOR_E_1_1.ZMS",
             medal_hall=r"3Ddata\KARKIA\KCatacomb\FLOOR\FLOOR_E_01.DDS",
             medal_boss=r"3Ddata\KARKIA\KCatacomb\FLOOR\FLOOR_E_04.DDS"),
    91: dict(prefix="KC2", zsc="LIST_DECO_KCATACOMB2.ZSC", src_zsc="KCATACOMB0201.ZSC",
             # Sorrow's wall C is two parts: the wall, and a carved panel in front of it
             wall=[(r"3Ddata\KARKIA\KCatacomb2\WALL\WALL_C_1_1_L00_P01.ZMS",
                    r"3Ddata\KARKIA\KCatacomb2\WALL\WALL_010_2.DDS", "WALL", True),
                   (r"3Ddata\KARKIA\KCatacomb2\WALL\WALL_C_1_1_L00.ZMS",
                    r"3Ddata\KARKIA\KCatacomb2\WALL\WALL_010_3.DDS", "PANEL", False)],
             medal_mesh=r"3Ddata\KARKIA\KCatacomb2\FLOOR\FLOOR_E_1_1.ZMS",
             medal_hall=r"3Ddata\KARKIA\KCatacomb2\FLOOR\FLOOR_E_01.DDS",
             medal_boss=r"3Ddata\KARKIA\KCatacomb2\FLOOR\FLOOR_E_04.DDS"),
}


def jpath(rel):
    p = os.path.join(JROSE, rel)
    if not os.path.isfile(p):
        raise SystemExit(f"{p} missing: the Jrose dump supplies the catacomb art")
    return p


# ---------------------------------------------------------------------- meshes
def _finish(m, pos, uv0, uv1, faces):
    """A parsed Jrose mesh re-used as the container for new geometry: same version,
    same vertex format (position + two UV sets, no normals), no strip."""
    pos = np.ascontiguousarray(pos, dtype="<f4").reshape(-1, 3)
    m.positions = pos
    m.uvs = [np.ascontiguousarray(uv0, dtype="<f4").reshape(-1, 2),
             np.ascontiguousarray(uv1, dtype="<f4").reshape(-1, 2)]
    m.faces = np.ascontiguousarray(faces, dtype="<u2").reshape(-1, 3)
    m.bbox_min = tuple(float(v) for v in pos.min(0))
    m.bbox_max = tuple(float(v) for v in pos.max(0))
    m.strip = np.zeros(0, "<u2")       # the source's strip indexes the source's faces
    m.matids = []
    return zms.build(m)


def _quad(corners, uv_box, flip_v=False):
    """Two triangles over four corners given counter-clockwise seen from outside (the
    winding Jrose's faces have), with a texture box stretched over them: corner 0 at
    (u0, v1), 1 at (u1, v1), 2 at (u1, v0), 3 at (u0, v0)."""
    u0, v0, u1, v1 = uv_box
    uv = [(u0, v1), (u1, v1), (u1, v0), (u0, v0)]
    return list(corners), uv, [(0, 1, 2), (0, 2, 3)]


def wall_mesh(src_rel, with_cap):
    """Wall C made two-sided about our edge line (see the module docstring)."""
    m = zms.parse(jpath(src_rel))
    if m.format != 0x182 or len(m.uvs) != 2:
        raise SystemExit(f"{src_rel}: not the position + 2 UV mesh this was written for")
    pos = np.asarray(m.positions, dtype=np.float64)
    faces = np.asarray(m.faces, dtype=np.int64)
    keep = faces[~(pos[faces][:, :, 0] >= COLUMN_FROM).all(axis=1)]
    used = np.unique(keep)
    remap = -np.ones(len(pos), dtype=np.int64)
    remap[used] = np.arange(len(used))
    a = pos[used] - (0.0, EDGE_PLANE, 0.0)
    if a[:, 1].min() < -1e-3:
        raise SystemExit(f"{src_rel}: geometry behind the edge plane ({a[:, 1].min():.3f} m)")
    b = a * (-1.0, -1.0, 1.0)                      # half a turn about the vertical: winding kept
    f = remap[keep]
    out_pos = [a, b]
    out_uv0 = [m.uvs[0][used], m.uvs[0][used]]
    out_uv1 = [m.uvs[1][used], m.uvs[1][used]]
    out_f = [f, f + len(a)]
    if with_cap:
        corners, uv, tris = _quad([(-0.5, -CAP_HALF, WALL_TOP), (0.5, -CAP_HALF, WALL_TOP),
                                   (0.5, CAP_HALF, WALL_TOP), (-0.5, CAP_HALF, WALL_TOP)], PATCH_UV)
        out_pos.append(np.array(corners))
        out_uv0.append(np.array(uv))
        out_uv1.append(np.array(uv))
        out_f.append(np.array(tris) + 2 * len(a))
    return _finish(m, np.concatenate(out_pos), np.concatenate(out_uv0), np.concatenate(out_uv1),
                   np.concatenate(out_f))


def pillar_mesh(template_rel):
    """A box: four sides wearing the column strip, a top wearing the plain patch."""
    m = zms.parse(jpath(template_rel))
    h, t = PILLAR_HALF, PILLAR_TOP
    pos, uv, faces = [], [], []
    side = [(h, -h, 0.0), (h, h, 0.0), (h, h, t), (h, -h, t)]         # the +x side, outward
    for k in range(4):
        c, s = math.cos(k * math.pi / 2), math.sin(k * math.pi / 2)
        corners, quv, tris = _quad([(x * c - y * s, x * s + y * c, z) for x, y, z in side], COLUMN_UV)
        faces += [tuple(i + len(pos) for i in tri) for tri in tris]
        pos += corners
        uv += quv
    corners, quv, tris = _quad([(-h, -h, t), (h, -h, t), (h, h, t), (-h, h, t)], PATCH_UV)
    faces += [tuple(i + len(pos) for i in tri) for tri in tris]
    pos += corners
    uv += quv
    return _finish(m, np.array(pos), np.array(uv), np.array(uv), np.array(faces))


def outward(blob):
    """Sanity: every triangle's normal points away from the model's vertical axis or
    straight up (a wall face, a pillar side, a cap), never back in. Returns the count
    of triangles that fail."""
    m = zms.parse(blob)
    tri = m.triangles_cm()
    n = np.cross(tri[:, 1] - tri[:, 0], tri[:, 2] - tri[:, 0])
    cen = tri.mean(axis=1)
    side = np.abs(n[:, 1]) > np.abs(n[:, 2])
    wrong_y = side & (np.sign(n[:, 1]) != np.sign(cen[:, 1])) & (np.abs(n[:, 1]) > np.abs(n[:, 0]))
    return int(wrong_y.sum())


# ---------------------------------------------------------------------- textures
def texture_with_mips(src_rel):
    """The source texture in its own format with a full mip chain: Jrose ships these
    with none, and the client would build the chain at every load."""
    src = jpath(src_rel)
    fmt = dds.info(src)[3]
    with tempfile.TemporaryDirectory() as tmp:
        cmd = [os.path.abspath(lighting.TEXCONV), "-nologo", "-y", "-dx9", "-m", "0", "-nowic",
               "-if", "BOX", "-f", fmt, "-o", tmp, src]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode != 0:
            raise SystemExit("texconv failed on %s: %s" % (src_rel, (r.stdout or r.stderr)[-300:]))
        out = [f for f in os.listdir(tmp) if f.lower().endswith(".dds")]
        return open(os.path.join(tmp, out[0]), "rb").read()


# ---------------------------------------------------------------------- the table
def props(parent=None, collision=0):
    """A part's property list: identity transform, its parent (stored 1-based, 0 =
    none), and a collision flag when it collides -- the layout of Jrose's own parts."""
    out = bytes((1, 12)) + struct.pack("<3f", 0.0, 0.0, 0.0)
    out += bytes((2, 16)) + struct.pack("<4f", 1.0, 0.0, 0.0, 0.0)          # w, x, y, z
    out += bytes((3, 12)) + struct.pack("<3f", 1.0, 1.0, 1.0)
    out += bytes((7, 2)) + struct.pack("<h", 0 if parent is None else parent + 1)
    if collision:
        out += bytes((29, 2)) + struct.pack("<h", collision)
    return out + b"\0"


def art_files(zone):
    """{data-relative path: bytes} of a crypt's models, textures and decoration table."""
    s = SETS[zone]
    pre = s["prefix"]
    src = Zsc(os.path.join(JROSE, "3Ddata", "Maps", "Karkia", "KCatacomb", s["src_zsc"]))
    flags = {p.decode("latin-1").replace("/", "\\").lower(): f for p, f in src.materials}

    def flag_of(tex_rel):
        return flags[tex_rel.lower()]

    files, meshes, mats, objects = {}, [], [], []

    def rel(name):
        return ART_DIR_REL + "\\" + name

    def add(mesh_name, blob, tex_name, tex_src):
        if rel(mesh_name) not in files:
            files[rel(mesh_name)] = blob
        if rel(tex_name) not in files:
            files[rel(tex_name)] = texture_with_mips(tex_src)
        meshes.append(rel(mesh_name).encode("latin-1"))
        mats.append((rel(tex_name).encode("latin-1"), flag_of(tex_src)))
        return len(meshes) - 1

    empty = (b"\0" * 12, [], [], b"")
    bb = src.objects[113][3]
    # 1: the wall. The colliding part is the root; Sorrow's carved panel hangs from it.
    parts = []
    for mesh_src, tex_src, name, collides in s["wall"]:
        blob = wall_mesh(mesh_src, with_cap=collides)
        if outward(blob):
            raise SystemExit(f"{mesh_src}: {outward(blob)} triangles face into the wall")
        i = add(f"{pre}_{name}.ZMS", blob, f"{pre}_{name}.DDS", tex_src)
        parts.append((i, i, props(parent=None if not parts else 0, collision=12 if collides else 0)))
    wall = (b"\0" * 12, parts, [], bb)
    # 2: the pillar, in the wall's texture
    wall_src = s["wall"][0]
    i = add(f"{pre}_PILLAR.ZMS", pillar_mesh(wall_src[0]), f"{pre}_WALL.DDS", wall_src[1])
    pillar = (b"\0" * 12, [(i, i, props(collision=12))], [], bb)
    # 3, 4: the medallions, no collision
    medal = open(jpath(s["medal_mesh"]), "rb").read()
    i = add(f"{pre}_MEDALLION.ZMS", medal, f"{pre}_MEDAL_HALL.DDS", s["medal_hall"])
    medal_hall = (b"\0" * 12, [(i, i, props())], [], bb)
    i = add(f"{pre}_MEDALLION.ZMS", medal, f"{pre}_MEDAL_BOSS.DDS", s["medal_boss"])
    medal_boss = (b"\0" * 12, [(i, i, props())], [], bb)
    objects = [empty, wall, pillar, medal_hall, medal_boss]

    zsc = struct.pack("<H", len(meshes)) + b"".join(p + b"\0" for p in meshes)
    zsc += struct.pack("<H", len(mats)) + b"".join(p + b"\0" + f for p, f in mats)
    zsc += struct.pack("<H", 0)                                             # no effects
    zsc += struct.pack("<H", len(objects)) + b"".join(serialize_object(o) for o in objects)
    back = Zsc(zsc)                                                         # it must read back
    if len(back.objects) != len(objects) or back.trailing or [len(o[1]) for o in back.objects] != \
            [len(o[1]) for o in objects]:
        raise SystemExit("the decoration table did not read back as written")
    files["3DDATA\\KARKIA\\" + s["zsc"]] = zsc
    return files


def zsc_rel(zone):
    return "3DDATA\\KARKIA\\" + SETS[zone]["zsc"]


# ---------------------------------------------------------------------- the layout
def edge_geometry(e, sw_cm, cell_cm):
    """(x, y, yaw degrees) of an interior edge's midpoint: the wall model lies along its
    own x, so an edge between east-west neighbours (a north-south line) is turned 90."""
    (ai, aj), (bi, bj) = maze.edge_cells(e)
    if e < maze.H_EDGES:
        return sw_cm + (ai + 1) * cell_cm, sw_cm + (aj + 0.5) * cell_cm, 90
    return sw_cm + (ai + 0.5) * cell_cm, sw_cm + (aj + 1) * cell_cm, 0


def placements(closed, sw_cm, cell_cm):
    """[(object, x cm, y cm, z cm, yaw degrees, scale)] of one crypt: the day's interior
    walls, then everything that never changes -- the outer wall, the pillars, the
    medallions."""
    n = maze.N
    out = [(OBJ_WALL, *edge_geometry(e, sw_cm, cell_cm)[:2], 0.0, edge_geometry(e, sw_cm, cell_cm)[2], SCALE)
           for e in range(maze.EDGES) if closed[e]]
    far = sw_cm + n * cell_cm
    for k in range(n):
        mid = sw_cm + (k + 0.5) * cell_cm
        out += [(OBJ_WALL, mid, sw_cm, 0.0, 0, SCALE), (OBJ_WALL, mid, far, 0.0, 0, SCALE),
                (OBJ_WALL, sw_cm, mid, 0.0, 90, SCALE), (OBJ_WALL, far, mid, 0.0, 90, SCALE)]
    for b in range(n + 1):
        for a in range(n + 1):
            # No pillar inside a room: the hall's inner corner is the arrival and revive
            # point, and the boss room is an open floor to fight on (the user, 2026-11-10).
            if any(r[0] < a < r[0] + r[2] and r[1] < b < r[1] + r[3] for r in (maze.HALL, maze.BOSS)):
                continue
            out.append((OBJ_PILLAR, sw_cm + a * cell_cm, sw_cm + b * cell_cm, 0.0, 0, SCALE))
    hall = (sw_cm + (maze.HALL[0] + maze.HALL[2] / 2.0) * cell_cm, sw_cm + (maze.HALL[1] + maze.HALL[3] / 2.0) * cell_cm)
    boss = (sw_cm + (maze.BOSS[0] + maze.BOSS[2] / 2.0) * cell_cm, sw_cm + (maze.BOSS[1] + maze.BOSS[3] / 2.0) * cell_cm)
    out.append((OBJ_MEDAL_HALL, hall[0], hall[1], MEDALLION_Z_CM, 0, SCALE))
    out.append((OBJ_MEDAL_BOSS, boss[0], boss[1], MEDALLION_Z_CM, 0, 2 * SCALE))   # 30 m: the arena's centre
    return out


def records_by_chunk(placed):
    """IFO decoration records per chunk slot (x, y), as mapgen.zone.build_zone takes them."""
    out = {}
    for obj, wx, wy, z, yaw, scale in placed:
        cx, cy = int(wx // 16000), int(wy // 16000)
        lx, ly = wx - cx * 16000, wy - cy * 16000
        half = math.radians(yaw) / 2.0
        rec = ifo.Record(name=b"", warp_id=0, event_id=0, obj_type=1, obj_id=obj,
                         map_x=int(lx // 250), map_y=63 - int(ly // 250),
                         rot=(0.0, 0.0, math.sin(half), math.cos(half)),
                         pos=(wx - 520000.0, wy - 520000.0, z), scale=(scale, scale, scale))
        out.setdefault((cx, cy), []).append(rec)
    return out
