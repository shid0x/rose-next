"""Audit every zone's object lightmap index files (.lit) for entries that can
crash or corrupt the client. Read-only.

    python scripts/audit-lightmap-index.py            # every zone in LIST_ZONE
    python scripts/audit-lightmap-index.py --zone 12 --zone 89
    python scripts/audit-lightmap-index.py -v         # list every finding

Each map chunk x_y has x_y/LightMap/BuildingLightMapData.lit (IFO lump 3)
and ObjectLightMapData.lit (lump 1). The client reads them in
`CMAP::LoadLightMapINFO` (src/client/io_terrain.cpp) and checks almost
nothing:

* `m_pObjectIndex[lump][obj_index - 1]` -- the lump's array is NULL when the
  IFO has no such lump (**certain crash**: NULL dereference) and has exactly
  as many entries as the lump has records (an ordinal past it **reads past the
  heap array**: a garbage object index, so maybe nothing, maybe another
  object). An ordinal <= 0 ends the file there.
* `iObjPosInMAP % iObjPerWIDTH` -- a grid width of 0 **divides by zero**.
* `CObjFIXED::SetLightMap` maps the part through `GetPartIndex` (sequence 0 =
  the first root part, k = the k-th non-root part, else k itself) and indexes
  `m_pHNODES` / `m_pParts` / `m_pLightMapMaterial` with it unchecked: a part
  outside the model **reads and writes past those arrays**.
* `CMAP::AddObject` drops a record whose f32 position falls outside its chunk
  (10 m patches truncated toward zero, 0..15) and frees its object slot, but
  the record's ordinal keeps that slot number, and the next object created
  takes the slot (`Get_EmptySlot` resumes at the slot it last handed out): a
  .lit entry for the dropped record **lights whatever object took it**. The
  audit finds that object -- the next record the client creates, in this lump
  or a later one in file order -- and grades the case: nothing takes it
  (INFO, the entry resolves to nothing); the next record of the same lump,
  whose own entry comes later and covers the same parts (INFO, overwritten);
  a map object with too few parts (CRASH); a map object otherwise (VISUAL,
  it shows the dropped object's lighting); an object that is not a map
  object -- an NPC, morpher, warp, collision box or event object (RISK: the
  entry casts it to CObjFIXED). The dropped object itself is simply missing
  in game.

Not crashes, reported as visual: a missing atlas (missing assets degrade), a
cell outside its atlas grid, two parts on one cell (the material cache key is
the cell plus the atlas path, so the second draws with the first's texture),
an entry whose part maps to a different part than its index says, and an
entry baked for another model than the record now holds. Retail and mapgen
tga names are `<mesh>_<Object|Building>_<ordinal>_<part>_<x_y>_LightingMap.tga`,
so a mismatch is the trace of an object deleted in the map editor after the
bake (the editor does not renumber): every later entry lights its neighbour.

Records whose model the client cannot create (object id outside the ZSC, no
parts) get object index 0, which resolves to nothing: their entries are
ignored, safely -- but the object is missing in game, reported as INFO
(2026-10-03: Desert of the Dead, 561 decorations and all 6 buildings: our
LIST_DECO_JD / LIST_CNST_JD predate the map; every reference client has
the objects).

Severity: CRASH (certain or out-of-bounds), RISK (lights the wrong object,
may go out of bounds), VISUAL, INFO. Exit code 1 if any CRASH or RISK.
"""

import argparse
import importlib.util
import os
import re
import sys
from collections import Counter, defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
_spec = importlib.util.spec_from_file_location("mapgen_zone", os.path.join(HERE, "mapgen-zone.py"))
mz = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mz)
from mapgen import catalogue, ifo, lit, objlight  # noqa: E402

LITS = ((ifo.CNST, "buildinglightmapdata.lit"), (ifo.OBJECT, "objectlightmapdata.lit"))
# lumps whose records take an object slot (CMAP::ReadObjINFO), and whether
# that object is a map object (CObjFIXED) a .lit entry can be applied to
ALLOCATING = {ifo.OBJECT: True, ifo.CNST: True, ifo.MOB: False, ifo.MORPH: False,
              ifo.WARP: False, ifo.COLLISION: False, ifo.EVENT_OBJECT: False}
ALLOC_NAME = {ifo.OBJECT: "OBJECT", ifo.CNST: "CNST", ifo.MOB: "NPC", ifo.MORPH: "MORPH",
              ifo.WARP: "WARP", ifo.COLLISION: "COLLISION", ifo.EVENT_OBJECT: "EVENT_OBJECT"}
LUMP_NAME = {ifo.CNST: "CNST", ifo.OBJECT: "OBJECT"}


def part_index(parts, seq):
    """CObjFIXED::GetPartIndex."""
    if seq == 0:
        for i, p in enumerate(parts):
            if p["parent"] < 0:
                return i
    else:
        n = 1
        for i, p in enumerate(parts):
            if p["parent"] < 0:
                continue
            if seq == n:
                return i
            n += 1
    return seq


def baked_mesh(tga):
    """The mesh stem a retail/mapgen tga name was baked for, or None."""
    t = tga.decode("latin-1")
    if not t.lower().endswith("_lightingmap.tga"):
        return None
    bits = t[:-len("_LightingMap.tga")].rsplit("_", 5)
    if len(bits) != 6 or bits[1] not in ("Object", "Building"):
        return None
    return bits[0].lower()


def mesh_stem(meshes, part):
    i = part["mesh"]
    if not 0 <= i < len(meshes):
        return None
    return os.path.splitext(os.path.basename(meshes[i].replace("\\", "/")))[0].lower()


def slot_taker(m, lump, i, zscs):
    """(lump, record index) of the next record the client creates after
    record i of lump, in IFO lump-table order, or None."""
    order = [r.lump_type for r in m.regions if r.lump_type in ALLOCATING]
    for lt in order[order.index(lump):]:
        recs = m.lump(lt) or []
        for j in range(i + 1 if lt == lump else 0, len(recs)):
            if lt in (ifo.OBJECT, ifo.CNST):
                objs = zscs[lt][1]
                if not (0 <= recs[j].obj_id < len(objs) and objs[recs[j].obj_id]["parts"]):
                    continue                   # never created, takes no slot
            return lt, j
    return None


def grade_dropped(m, lump, o, L, zscs, where, add):
    """A .lit entry for a record the client drops: grade what happens to
    the object that takes its slot (see the module docstring)."""
    head = "%s: ordinal %d is outside its chunk (the client drops it)" % (where, o.obj_index)
    t = slot_taker(m, lump, o.obj_index - 1, zscs)
    if t is None:
        add("INFO", head + "; no later object takes its slot, the entry resolves to nothing")
        return
    lt, j = t
    if not ALLOCATING[lt]:
        add("RISK", head + "; its slot goes to %s record %d, not a map object: the entry casts it to CObjFIXED"
            % (ALLOC_NAME[lt], j + 1))
        return
    tparts = zscs[lt][1][m.lump(lt)[j].obj_id]["parts"]
    mapped = [part_index(tparts, pt.part_index) for pt in o.parts]
    if any(not 0 <= k < len(tparts) for k in mapped):
        add("CRASH", head + "; its slot goes to %s record %d (%d parts), and the entry lists parts %s"
            % (ALLOC_NAME[lt], j + 1, len(tparts), [pt.part_index for pt in o.parts]))
        return
    own = [x for x in L.objects if x.obj_index == j + 1] if lt == lump else []
    later = bool(own) and L.objects.index(own[0]) > L.objects.index(o)
    if later and {pt.part_index for pt in o.parts} <= {pt.part_index for pt in own[0].parts}:
        add("INFO", head + "; record %d takes its slot and its own entry, later, overwrites it" % (j + 1))
    else:
        add("VISUAL", head + "; %s record %d takes its slot and shows its lighting" % (ALLOC_NAME[lt], j + 1))


def listing(path):
    """{lower-case name: real name} of a folder, or {}."""
    try:
        return {f.lower(): f for f in os.listdir(path)}
    except OSError:
        return {}


def dds_side(path):
    with open(path, "rb") as f:
        h = f.read(20)
    if len(h) < 20 or h[:4] != b"DDS ":
        return None
    import struct
    hgt, wid = struct.unpack_from("<2I", h, 12)
    return wid, hgt


def audit_chunk(zdir, stem, zscs, add):
    """Findings for one chunk: add(severity, text)."""
    m_ = re.match(r"(\d+)_(\d+)$", stem)
    slot = (int(m_.group(1)), 64 - int(m_.group(2)))
    try:
        with open(os.path.join(zdir, stem + ".IFO"), "rb") as f:
            m = ifo.parse(f.read())
    except Exception as e:                       # noqa: BLE001 -- report and move on
        add("INFO", "%s.IFO does not parse (%s); its lightmaps not checked" % (stem, e))
        return 0
    sub = listing(zdir).get(stem.lower())
    lm_dir = None
    if sub:
        lm = listing(os.path.join(zdir, sub)).get("lightmap")
        lm_dir = os.path.join(zdir, sub, lm) if lm else None
    files = listing(lm_dir) if lm_dir else {}
    dropped = {}
    for lump in (ifo.OBJECT, ifo.CNST):
        recs = m.lump(lump)
        dropped[lump] = {i for i, r in enumerate(recs or []) if not objlight.client_accepts(r.pos, slot)}
    n_entries = 0
    sides = {}
    for lump, fn in LITS:
        if fn not in files:
            continue
        recs = m.lump(lump)
        meshes, objs = zscs[lump]
        where = "%s %s" % (stem, files[fn])
        try:
            with open(os.path.join(lm_dir, files[fn]), "rb") as f:
                L = lit.parse_lit(f.read())
        except Exception as e:                   # noqa: BLE001
            add("CRASH", "%s does not parse (%s): the client reads garbage counts and ordinals" % (where, e))
            continue
        used = {}
        for o in L.objects:
            n_entries += 1
            if o.obj_index <= 0:
                add("INFO", "%s: ordinal %d ends the file for the client (%d later objects ignored)"
                    % (where, o.obj_index, len(L.objects) - L.objects.index(o) - 1))
                break
            if recs is None:
                add("CRASH", "%s: entry for ordinal %d but the IFO has no %s lump (NULL dereference)"
                    % (where, o.obj_index, LUMP_NAME[lump]))
                break
            if o.obj_index > len(recs):
                add("CRASH", "%s: ordinal %d past the %d records of lump %s (reads past the array)"
                    % (where, o.obj_index, len(recs), LUMP_NAME[lump]))
                continue
            rec = recs[o.obj_index - 1]
            oid = rec.obj_id
            if not 0 <= oid < len(objs) or not objs[oid]["parts"]:
                continue                         # never created: object index 0, entry ignored
            parts = objs[oid]["parts"]
            if o.obj_index - 1 in dropped[lump]:
                grade_dropped(m, lump, o, L, zscs, where, add)
                continue                         # applied to the slot's taker, graded there
            for pt in o.parts:
                k = part_index(parts, pt.part_index)
                if not 0 <= k < len(parts):
                    add("CRASH", "%s: ordinal %d (object %d, %d parts) part %d -> %d (past the part arrays)"
                        % (where, o.obj_index, oid, len(parts), pt.part_index, k))
                elif k != pt.part_index:
                    add("VISUAL", "%s: ordinal %d (object %d) part %d is drawn on part %d (GetPartIndex)"
                        % (where, o.obj_index, oid, pt.part_index, k))
                bm = baked_mesh(pt.tga_name)
                if bm and 0 <= k < len(parts) and bm != mesh_stem(meshes, parts[k]):
                    add("VISUAL", "%s: ordinal %d part %d was baked for %s, the record is now %s (object "
                        "deleted after the bake?)" % (where, o.obj_index, pt.part_index, bm,
                                                      mesh_stem(meshes, parts[k])))
                ppw = pt.parts_per_width
                if ppw == 0:
                    add("CRASH", "%s: ordinal %d part %d has grid width 0 (divide by zero)"
                        % (where, o.obj_index, pt.part_index))
                    continue
                if ppw < 0 or not 0 <= pt.position_in_map < ppw * ppw:
                    add("VISUAL", "%s: ordinal %d part %d cell %d outside a %d-wide grid"
                        % (where, o.obj_index, pt.part_index, pt.position_in_map, ppw))
                name = pt.dds_name.decode("latin-1")
                real = files.get(name.lower())
                if real is None:
                    add("VISUAL", "%s: ordinal %d part %d atlas %s missing" % (where, o.obj_index, pt.part_index, name))
                else:
                    if real not in sides:
                        sides[real] = dds_side(os.path.join(lm_dir, real))
                    s = sides[real]
                    if s and (s[0] != s[1] or s[0] % ppw):
                        add("VISUAL", "%s: atlas %s is %dx%d, not a %d-wide square grid" % (where, real, s[0], s[1], ppw))
                key = (name.lower(), pt.position_in_map % ppw, pt.position_in_map // ppw)
                if key in used:
                    add("VISUAL", "%s: ordinal %d part %d shares cell %d of %s with ordinal %d"
                        % (where, o.obj_index, pt.part_index, pt.position_in_map, name, used[key]))
                used[key] = o.obj_index
    for lump in (ifo.OBJECT, ifo.CNST):
        objs = zscs[lump][1]
        empty = [r.obj_id for r in m.lump(lump) or [] if not (0 <= r.obj_id < len(objs) and objs[r.obj_id]["parts"])]
        if empty:
            add("INFO", "%s: %d %s records name objects the ZSC does not have (%s): the client creates nothing, "
                "missing in game" % (stem, len(empty), LUMP_NAME[lump], ", ".join(map(str, sorted(set(empty))[:8]))))
        for i in sorted(dropped[lump]):
            r = m.lump(lump)[i]
            add("INFO", "%s: %s record %d (object %d) is outside its chunk; the client drops it (missing in game)"
                % (stem, LUMP_NAME[lump], i + 1, r.obj_id))
    return n_entries


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--zone", type=int, action="append", help="LIST_ZONE row (repeatable); default every zone")
    ap.add_argument("-v", "--verbose", action="store_true", help="print every finding, not the first few per kind")
    a = ap.parse_args()
    zstb = mz.oro.Stb(mz.P(mz.ZONE_STB))

    def cell(row, col):
        try:
            return zstb.get(row, col).decode("latin-1").strip()
        except Exception:                          # noqa: BLE001
            return ""
    # zones that share a folder and both ZSCs are one map
    maps = defaultdict(list)
    for row in (a.zone or range(zstb.rows)):
        zon, deco, cnst = cell(row, mz.COL_ZON), cell(row, mz.COL_DECO), cell(row, mz.COL_CNST)
        if not zon or not os.path.exists(mz.P(zon)):
            continue
        maps[(os.path.dirname(mz.P(zon)).lower(), deco.lower(), cnst.lower())].append(row)
    zsc_cache = {}

    def zsc(rel):
        rel = rel.replace("\\\\", "\\")         # LIST_ZONE stores ZSC paths with doubled backslashes
        if rel not in zsc_cache:
            try:
                zsc_cache[rel] = catalogue.read_zsc(mz.P(rel))
            except Exception as e:                 # noqa: BLE001
                zsc_cache[rel] = None
                print("cannot read %s (%s): its maps are not checked" % (rel, e))
        return zsc_cache[rel]
    totals = Counter()
    n_chunks = n_entries = 0
    for (zdir, deco, cnst), rows in sorted(maps.items(), key=lambda kv: kv[1][0]):
        zscs = {ifo.OBJECT: zsc(deco), ifo.CNST: zsc(cnst)}
        found = []
        if None in zscs.values():
            totals["unchecked"] += 1
            continue
        add = lambda sev, text: found.append((sev, text))   # noqa: E731
        stems = sorted(os.path.splitext(f)[0] for f in os.listdir(zdir)
                       if re.match(r"\d+_\d+\.ifo$", f, re.I))
        for stem in stems:
            n_entries += audit_chunk(zdir, stem, zscs, add)
        n_chunks += len(stems)
        c = Counter(sev for sev, _ in found)
        totals.update(c)
        if not found:
            continue
        label = "zone %s %s" % ("/".join(map(str, rows)), os.path.relpath(zdir, mz.DATA))
        print("%s: %s" % (label, ", ".join("%d %s" % (c[s], s) for s in ("CRASH", "RISK", "VISUAL", "INFO") if c[s])))
        shown = Counter()
        for sev in ("CRASH", "RISK", "VISUAL", "INFO"):
            for s_, text in found:
                if s_ != sev:
                    continue
                kind = re.sub(r"\d+", "#", text.split(": ", 1)[-1])[:60]
                shown[(sev, kind)] += 1
                if a.verbose or sev in ("CRASH", "RISK") or shown[(sev, kind)] <= 2:
                    print("  %-6s %s" % (sev, text))
                elif shown[(sev, kind)] == 3:
                    print("  %-6s ... (more like this; -v lists them)" % sev)
    print("\n%d maps (%d not checked: ZSC unreadable), %d chunks, %d lightmap entries: %s" % (
        len(maps), totals["unchecked"], n_chunks, n_entries,
        ", ".join("%d %s" % (totals[s], s) for s in ("CRASH", "RISK", "VISUAL", "INFO"))))
    return 1 if totals["CRASH"] or totals["RISK"] else 0


if __name__ == "__main__":
    sys.exit(main())
