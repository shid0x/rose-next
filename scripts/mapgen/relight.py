"""Relight an existing zone: bake its plane and object lightmaps from its own
files, for maps made by hand in the map editor (phase 8 tools).

The bakers are the map generator's (mapgen/lighting.py for the ground,
mapgen/objlight.py for objects); this module only reads a zone the way the
client does and feeds them. scripts/relight-zone.py is the command line.

What a zone gives the bake:

* heights: every x_y.HIM in the zone folder (holes between chunks are
  filled with the lowest height, so they cast nothing);
* objects: IFO lumps 1 (decorations) and 3 (buildings) of every chunk, with
  the zone's two ZSCs (LIST_ZONE cols 11/12);
* water: the flat water rectangles (IFO lump 9);
* grass and flowers (one unshaded value, never darker under trees): the
  editor's LIST_TERRAIN_OBJECT_<type>.STB files them under a GRASS source
  folder; zones without one (imported maps) by a GRASS folder in every
  mesh path of the object; "plants" / "not_plants" settings correct it;
* cell sizes: the zone's own .lit files say what size its artist gave each
  mesh (else the Junon table, else by size).
"""

import json
import os
import re
from collections import Counter

import numpy as np

from . import areas, catalogue, ifo, lighting, lit, objlight, prefab, tiles

# ----------------------------------------------------------------- settings

# (key, group, what it does). Defaults come from lighting.DEFAULT,
# objlight.OBJECT_DEFAULT and SWITCHES. "advanced" ones rarely need changing.
SETTINGS = [
    ("terrain", "switch", "Rebake the ground lightmaps (true/false). false keeps the zone's current ones."),
    ("objects", "switch", "Rebake the object lightmaps (true/false). false keeps the zone's current ones."),
    ("brightness", "light", "Overall brightness, a multiplier on open_rgb: 1.0 = default, 1.15 = 15% brighter."),
    ("open_rgb", "light", "Colour of open ground in full sun, R,G,B. 128,128,128 = the textures as painted; "
                          "retail Junon is 161,156,151 (x1.23, slightly warm). Sets the tint of everything."),
    ("sun_az", "light", "Compass bearing the sun shines FROM, degrees: 0 north, 90 east. Shadows fall the "
                        "other way. Every retail Junon map: about 62 (east-north-east)."),
    ("sun_el", "light", "Sun height above the horizon, degrees. Lower = longer shadows and more contrast "
                        "on slopes. 45 default; 30 = long late-afternoon shadows."),
    ("shadow_ratio", "shadow", "How light full shadow is, as a share of open ground (0-1). 0.70 = retail; "
                               "0.8 = softer, 0.55 = deep shadows."),
    ("solid_shadow", "shadow", "How much sun a solid object (rock, house) blocks on the ground (0-1)."),
    ("leaf_shadow", "shadow", "How much light one layer of leaves blocks (0-1); layers add up under thick trees."),
    ("terrain_soft_m", "shadow", "Softness of the edges of hill and cliff shadows, metres."),
    ("object_soft_m", "shadow", "Softness of the edges of object shadows on the ground, metres."),
    ("footprint_strength", "contact", "Darkening right under objects that touch the ground (0-1)."),
    ("footprint_radius_m", "contact", "How far that darkening spreads, metres."),
    ("halo_strength", "contact", "Faint darkening ring just outside rocks and trunks (0-1)."),
    ("halo_radius_m", "contact", "Width of that ring, metres."),
    ("contact_height_m", "contact", "Geometry lower than this above the ground counts as touching it, metres."),
    ("canopy_strength", "trees", "Extra darkness of the ground under tree canopies (0-1)."),
    ("canopy_radius_m", "trees", "Softness of that darkness, metres."),
    ("canopy_tint", "trees", "How much the ground under a canopy takes the leaves' colour (0-1)."),
    ("water_rgb", "water", "Colour of the ground under water, R,G,B."),
    ("water_full_cm", "water", "Water depth at which the ground is fully water-coloured, cm."),
    ("object_gain", "objects", "Brightness of objects in shade relative to the ground (0.8 = a little "
                               "darker, as retail)."),
    ("object_sun", "objects", "How strongly the sun lights objects' sunny sides (1.2 default)."),
    ("sky_occlusion", "objects", "How much undersides, nooks and the feet of objects darken (0-1)."),
    ("foliage_ndl", "objects", "Brightness of leaves and of grass/flowers in the sun (0-1)."),
    ("foliage_sky", "objects", "How much leaves inside a canopy darken (0-1)."),
    ("solid_block", "objects", "How much sun one object blocks on another object (0-1)."),
    ("plants", "objects", "Object ids (decorations) to treat as grass/flowers: one unshaded value. List, "
                          "e.g. 12,40."),
    ("not_plants", "objects", "Object ids NOT to treat as grass/flowers even though they are filed as grass."),
    ("buried_factor", "advanced", "Darkening of object parts sunk into the ground (0-1)."),
    ("buried_cm", "advanced", "How deep under the ground a part counts as sunk, cm."),
    ("shadow_res_cm", "advanced", "Object shadow map resolution, cm. Lower = sharper, slower."),
    ("shadow_bias_cm", "advanced", "Object self-shadowing tolerance, cm."),
    ("sky_res_cm", "advanced", "Sky occlusion map resolution, cm."),
    ("sky_bias_cm", "advanced", "Sky occlusion tolerance, cm."),
    ("atlas_max_px", "advanced", "Largest object lightmap atlas, pixels (512 = retail)."),
]
SWITCHES = {"terrain": True, "objects": True, "brightness": 1.0, "plants": [], "not_plants": []}
COLOURS = ("open_rgb", "water_rgb")
LISTS = ("plants", "not_plants")


def defaults():
    d = dict(lighting.DEFAULT)
    d.update(objlight.OBJECT_DEFAULT)
    d.update(SWITCHES)
    return d


def parse_value(key, text):
    """A setting from text: true/false, a number, R,G,B, or an id list."""
    if key not in defaults():
        raise ValueError("%r is not a setting (python scripts/relight-zone.py settings)" % key)
    t = str(text).strip()
    if key in ("terrain", "objects"):
        if t.lower() not in ("true", "false", "1", "0", "yes", "no"):
            raise ValueError("%s: true or false" % key)
        return t.lower() in ("true", "1", "yes")
    if key in COLOURS:
        v = [float(x) for x in t.split(",")]
        if len(v) != 3:
            raise ValueError("%s: three numbers R,G,B" % key)
        return tuple(v)
    if key in LISTS:
        return [int(x) for x in t.replace(" ", "").split(",") if x]
    return int(float(t)) if key == "atlas_max_px" else float(t)


def normalise(settings):
    """Settings from JSON, through the same checks and casts as --set (a
    JSON 512.0 must not reach range(), nor "false" count as true)."""
    out = {}
    for k, v in settings.items():
        if k.startswith("_"):
            continue
        if isinstance(v, (list, tuple)):
            v = ",".join(str(x) for x in v)
        out[k] = parse_value(k, v)
    return out


def bake_configs(settings):
    """(terrain cfg, object cfg) for lighting.bake / objlight.bake."""
    s = defaults()
    s.update(settings)
    t = {k: s[k] for k in lighting.DEFAULT}
    t["open_rgb"] = tuple(float(c) * float(s["brightness"]) for c in s["open_rgb"])
    o = {k: s[k] for k in objlight.OBJECT_DEFAULT}
    return t, o, s


# ----------------------------------------------------------------- zone

def find_ci(folder, name):
    """Existing path of name in folder, any case, or None."""
    try:
        for f in os.listdir(folder):
            if f.lower() == name.lower():
                return os.path.join(folder, f)
    except OSError:
        pass
    return None


def _baked_mesh(tga):
    """The mesh stem a retail/mapgen tga name was baked for, or None
    (`<mesh>_<Object|Building>_<ordinal>_<part>_<x_y>_LightingMap.tga`)."""
    t = tga.decode("latin-1")
    if not t.lower().endswith("_lightingmap.tga"):
        return None
    bits = t[:-len("_LightingMap.tga")].rsplit("_", 5)
    if len(bits) != 6 or bits[1] not in ("Object", "Building"):
        return None
    return bits[0].lower()


class Zone:
    """A zone folder as the client reads it."""

    def __init__(self, data_dir, zdir, deco_rel, cnst_rel, rows):
        self.data_dir = data_dir
        self.zdir = zdir
        self.folder = os.path.basename(zdir)
        self.rows = rows
        self.deco_rel, self.cnst_rel = deco_rel, cnst_rel
        self.zscs = {"OBJECT": catalogue.read_zsc(os.path.join(data_dir, deco_rel)),
                     "CNST": catalogue.read_zsc(os.path.join(data_dir, cnst_rel))}
        self.field, (self.x0, self.y0) = prefab._zone_field(zdir)
        # holes between chunks: the lowest height for shadows (they must cast
        # nothing), the nearest real height for the slope term (a low fill
        # tilted the edge texels: off by > 0.2 n.l on ~20% of hole edges)
        self.field_f = np.nan_to_num(self.field, nan=float(np.nanmin(self.field)))
        holes = np.isnan(self.field)
        if holes.any():
            _, nr, nc = areas.nearest(~holes)
            self.field_n = self.field[nr, nc]
        else:
            self.field_n = None
        self.chunks = {}                                  # slot -> {"stem", "ifo", "dir"}
        for f in sorted(os.listdir(zdir)):
            m = re.match(r"(\d+)_(\d+)\.ifo$", f, re.I)
            if not m:
                continue
            with open(os.path.join(zdir, f), "rb") as fh:
                parsed = ifo.parse(fh.read())
            stem = f[:-4]
            slot = (int(m.group(1)), 64 - int(m.group(2)))
            self.chunks[slot] = {"stem": stem, "ifo": parsed, "dir": find_ci(zdir, stem)}
        self.width = (self.field.shape[1] - 1) // 64
        self.height = (self.field.shape[0] - 1) // 64

    # what kind of object
    def editor_categories(self):
        """{object id: source folder} from the editor's object list for the
        decoration ZSC's type (LIST_DECO_<T>.ZSC -> LIST_TERRAIN_OBJECT_<T>.STB),
        or None when there is none."""
        t = re.match(r"list_deco_(.+)\.zsc$", os.path.basename(self.deco_rel.replace("\\", "/")), re.I)
        if not t:
            return None
        p = os.path.join(self.data_dir, "3DDATA", "STB", "LIST_TERRAIN_OBJECT_%s.STB" % t.group(1).upper())
        if not os.path.exists(p):
            return None
        cats = {}
        for oid, row in enumerate(tiles.read_stb_cells(p, "latin-1")):
            parts = row[1].strip().upper().split("\\") if len(row) > 1 else []
            cats[oid] = parts[2] if len(parts) > 3 else ""
        return cats

    def small_plants(self, settings):
        """Decoration ids lit as grass/flowers, and how they were found."""
        meshes, objs = self.zscs["OBJECT"]
        cats = self.editor_categories()
        if cats is not None:
            ids = {i for i, c in cats.items() if c == "GRASS" and i < len(objs)}
            how = "the editor's object list (GRASS folder)"
        else:
            ids = set()
            for i, o in enumerate(objs):
                paths = [meshes[p["mesh"]].replace("\\", "/").upper() for p in o["parts"] if 0 <= p["mesh"] < len(meshes)]
                if paths and all("/GRASS/" in p for p in paths):
                    ids.add(i)
            how = "a GRASS folder in the mesh paths (no editor object list)"
        ids |= set(settings.get("plants", []))
        ids -= set(settings.get("not_plants", []))
        return ids, how

    def cell_table(self, base):
        """Cell size per mesh stem: the zone's own .lit files, over base."""
        counts = {}
        for slot, ch in self.chunks.items():
            lm = find_ci(ch["dir"], "LightMap") if ch["dir"] else None
            if not lm:
                continue
            for lump, fn in ((ifo.OBJECT, "ObjectLightMapData.lit"), (ifo.CNST, "BuildingLightMapData.lit")):
                p = find_ci(lm, fn)
                recs = ch["ifo"].lump(lump) or []
                if not p:
                    continue
                meshes, objs = self.zscs["OBJECT" if lump == ifo.OBJECT else "CNST"]
                try:
                    with open(p, "rb") as fh:
                        L = lit.parse_lit(fh.read())
                except Exception:                  # noqa: BLE001 -- a broken file just teaches nothing
                    continue
                for o in L.objects:
                    if not 1 <= o.obj_index <= len(recs):
                        continue
                    oid = recs[o.obj_index - 1].obj_id
                    if not 0 <= oid < len(objs):
                        continue
                    for pt in o.parts:
                        if not 0 <= pt.part_index < len(objs[oid]["parts"]):
                            continue
                        mi = objs[oid]["parts"][pt.part_index]["mesh"]
                        if not 0 <= mi < len(meshes):
                            continue
                        stem = os.path.splitext(os.path.basename(meshes[mi].replace("\\", "/")))[0].lower()
                        baked = _baked_mesh(pt.tga_name)
                        if baked and baked != stem:
                            continue               # an entry left stale by an editor edit
                        px = pt.pixels_per_part
                        if px not in (32, 64, 128, 256):
                            # Jrose-lineage files write 0: the atlas side / grid width
                            # is what the client samples
                            a = find_ci(lm, pt.dds_name.decode("latin-1"))
                            px = 0
                            if a and pt.parts_per_width > 0:
                                with open(a, "rb") as fh:
                                    head = fh.read(128)
                                if len(head) >= 32 and head[:4] == b"DDS ":
                                    px = lighting.dds_info(head)[0] // pt.parts_per_width
                        if px in (32, 64, 128, 256):
                            counts.setdefault(stem, Counter())[px] += 1
        table = dict(base)
        table.update({k: v.most_common(1)[0][0] for k, v in counts.items()})
        return table, len(counts)

    # inputs of the two bakers
    def placed(self, plants):
        """Objects as lighting.bake wants them (field-local cm)."""
        out = []
        for slot, ch in self.chunks.items():
            for lump, key in ((ifo.OBJECT, "OBJECT"), (ifo.CNST, "CNST")):
                for r in ch["ifo"].lump(lump) or []:
                    if not objlight.client_accepts(r.pos, slot):
                        continue                   # the client drops it: it casts nothing in game
                    x = r.pos[0] + objlight.ORIGIN_CM - self.x0 * objlight.CHUNK_CM
                    y = r.pos[1] + objlight.ORIGIN_CM - self.y0 * objlight.CHUNK_CM
                    g = float(lighting.ground_cm(self.field_f, np.array([x]), np.array([y]))[0])
                    out.append({"lump": key, "id": r.obj_id, "x": x, "y": y, "z": r.pos[2],
                                "rot": tuple(r.rot), "scale": tuple(r.scale), "sink": r.pos[2] - g,
                                "foliage": key == "OBJECT" and r.obj_id in plants,
                                "category": "BRIDGE" if self.is_bridge(key, r.obj_id) else ""})
        return out

    def is_bridge(self, lump, oid):
        """A bridge (a mesh named *bridge*): its deck rests near the bank and
        must not darken the ground under it like a rock's foot."""
        meshes, objs = self.zscs[lump]
        if not 0 <= oid < len(objs):
            return False
        return any("bridge" in os.path.basename(meshes[p["mesh"]].replace("\\", "/")).lower()
                   for p in objs[oid]["parts"] if 0 <= p["mesh"] < len(meshes))

    def water(self):
        """[(vertex mask, level cm)] from every chunk's flat water rectangles."""
        f, out = self.field_f, []
        for ch in self.chunks.values():
            o = ch["ifo"].lump(ifo.OCEAN)
            for sx, sz, sy, ex, ez, ey in (o.rects if o is not None else []):
                c0 = int(round((min(sx, ex) + 520000) / 250.0)) - self.x0 * 64
                c1 = int(round((max(sx, ex) + 520000) / 250.0)) - self.x0 * 64
                r0 = int(round((min(sy, ey) + 520000) / 250.0)) - self.y0 * 64
                r1 = int(round((max(sy, ey) + 520000) / 250.0)) - self.y0 * 64
                if r1 < 0 or c1 < 0 or r0 >= f.shape[0] or c0 >= f.shape[1]:
                    continue                       # off the map (a negative end would wrap)
                mask = np.zeros(f.shape, bool)
                mask[max(0, r0):r1 + 1, max(0, c0):c1 + 1] = True
                out.append((mask & (f < sz), sz))
        return out

    def object_records(self, plants):
        """objlight.bake's chunk_records."""
        out = {}
        for slot, ch in self.chunks.items():
            objs = list(ch["ifo"].lump(ifo.OBJECT) or [])
            out[slot] = {"OBJECT": objs, "CNST": list(ch["ifo"].lump(ifo.CNST) or []),
                         "foliage": {i for i, r in enumerate(objs) if r.obj_id in plants}}
        return out

    def unlightable(self, parts):
        """(records naming an object the ZSC lacks -- missing in game too --,
        Counter of reasons other objects cannot take a lightmap)."""
        missing, c = 0, Counter()
        for slot, ch in self.chunks.items():
            for lump, key in ((ifo.OBJECT, "OBJECT"), (ifo.CNST, "CNST")):
                objs = self.zscs[key][1]
                for r in ch["ifo"].lump(lump) or []:
                    if not (0 <= r.obj_id < len(objs) and objs[r.obj_id]["parts"]):
                        missing += 1
                        continue
                    prs, ok, why = parts.get(key, r.obj_id)
                    if not ok:
                        c[why.split(": ", 1)[-1] if ": " in why else why] += 1
        return missing, c


# ----------------------------------------------------------------- bake

ATLAS_RE = re.compile(r"(object|building)_\d+_\d+\.dds$", re.I)


def bake(zone, settings, cell_base, log=print):
    """Bake a zone. Returns (writes {abs path: bytes}, deletes [abs path],
    report dict). Nothing is written here."""
    t_cfg, o_cfg, s = bake_configs(settings)
    plants, how = zone.small_plants(s)
    writes, deletes, report = {}, [], {"plants": len(plants), "plants_from": how}
    if s["terrain"]:
        placed = zone.placed(plants)
        water = zone.water()
        shapes = lighting.Shapes(zone.data_dir, zone.zscs)
        lm = lighting.bake(zone.field_f, placed, shapes, water, t_cfg, normal_field=zone.field_n)
        images = {}
        for (cx, cy), img in lighting.chunk_images(lm, zone.width, zone.height).items():
            slot = (zone.x0 + cx, zone.y0 + cy)
            if slot in zone.chunks:
                images[slot] = img
        dds = lighting.encode_dds(images)
        for slot, blob in dds.items():
            ch = zone.chunks[slot]
            cdir = ch["dir"] or os.path.join(zone.zdir, ch["stem"])
            name = "%s_PlaneLightingMap.dds" % ch["stem"]
            writes[find_ci(cdir, name) or os.path.join(cdir, name)] = blob
        report["ground"] = "%d chunks, %d objects cast shadows, %d water rectangles" % (len(dds), len(placed), len(water))
        log("ground: %s" % report["ground"])
    if s["objects"]:
        parts = objlight.Parts(zone.data_dir, zone.zscs, lighting.Shapes(zone.data_dir, zone.zscs))
        table, n_own = zone.cell_table(cell_base)
        res = objlight.bake(zone.field_f, zone.object_records(plants), parts, table, cfg=o_cfg,
                            terrain_cfg=t_cfg, x0=zone.x0, y0=zone.y0,
                            log=lambda m: log(m) if "object lighting" in m else None)
        images = {(slot, name): a for slot, r in res.items() for name, a in r["atlases"].items()}
        dds = lighting.encode_dds(images, mips=3)
        n_parts = 0
        for slot, r in res.items():
            ch = zone.chunks[slot]
            cdir = ch["dir"] or os.path.join(zone.zdir, ch["stem"])
            lm_dir = find_ci(cdir, "LightMap") or os.path.join(cdir, "LightMap")
            for key, fn in (("OBJECT", "ObjectLightMapData.lit"), ("CNST", "BuildingLightMapData.lit")):
                writes[find_ci(lm_dir, fn) or os.path.join(lm_dir, fn)] = r[key]
                n_parts += sum(len(o.parts) for o in lit.parse_lit(r[key]).objects)
            new = {n.lower() for n in r["atlases"]}
            for name in r["atlases"]:
                writes[find_ci(lm_dir, name) or os.path.join(lm_dir, name)] = dds[(slot, name)]
            if os.path.isdir(lm_dir):
                deletes += [os.path.join(lm_dir, f) for f in os.listdir(lm_dir)
                            if ATLAS_RE.match(f) and f.lower() not in new]
        missing, bad = zone.unlightable(parts)
        report["objects"] = "%d parts in %d atlases; cell sizes from the zone's own files for %d meshes" % (
            n_parts, len(images), n_own)
        report["unlightable"] = dict(bad)
        report["missing"] = missing
        log("objects: %s" % report["objects"])
        if bad:
            log("  %d objects cannot take a lightmap and stay vertex-lit (mesh without a second uv set): %s"
                % (sum(bad.values()), ", ".join("%s x%d" % kv for kv in bad.most_common(5))))
        if missing:
            log("  %d records name objects the zone's ZSC does not have: missing in game, nothing to light"
                % missing)
    log("grass and flowers: %d object types, from %s" % (len(plants), how))
    return writes, deletes, report


# ----------------------------------------------------------------- checks

def check_zone(zone, audit, files=None):
    """(problems, warnings) on disk: the audit's rules on the .lit files
    and the format of the lightmap files. `files` (lower-case paths): what a
    run wrote; only those can make it fail -- findings elsewhere (a .lit a
    ground-only bake kept, a retail file in a format we do not write) are
    warnings. files=None judges everything."""
    problems, warnings = [], []

    def ours(path):
        return files is None or os.path.normcase(path).lower() in files
    zscs = {ifo.OBJECT: zone.zscs["OBJECT"], ifo.CNST: zone.zscs["CNST"]}
    for slot, ch in sorted(zone.chunks.items()):
        cdir = ch["dir"]
        lm = find_ci(cdir, "LightMap") if cdir else None
        found = []
        audit.audit_chunk(zone.zdir, ch["stem"], zscs, lambda sev, text: found.append((sev, text)))
        for sev, text in found:
            if sev not in ("CRASH", "RISK", "VISUAL"):
                continue
            m = re.search(r"(\S+\.lit)", text, re.I)               # "<stem> <LIT FILE>: ..."
            path = find_ci(lm, m.group(1)) if lm and m else None
            (problems if path and ours(path) else warnings).append("%s %s" % (sev, text))
        if not cdir:
            continue
        for f in os.listdir(cdir):
            path = os.path.join(cdir, f)
            if f.lower().endswith("_planelightingmap.dds") and ours(path):
                with open(path, "rb") as fh:
                    w, h, mips, four = lighting.dds_info(fh.read(128))
                if four != "DXT5" or w != h or mips < 2:
                    problems.append("%s/%s: %s %dx%d, %d mips" % (ch["stem"], f, four, w, h, mips))
        for f in (os.listdir(lm) if lm else []):
            path = os.path.join(lm, f)
            if ATLAS_RE.match(f) and ours(path):
                with open(path, "rb") as fh:
                    w, h, mips, four = lighting.dds_info(fh.read(128))
                if four != "DXT5" or w != h or mips != 3:
                    problems.append("%s/%s: atlas %s %dx%d, %d mips" % (ch["stem"], f, four, w, h, mips))
    return problems, warnings


def settings_file(scripts_dir, folder):
    return os.path.join(scripts_dir, "mapgen", "relight", "%s.json" % folder)


def load_settings(path):
    if not os.path.exists(path):
        return {}
    with open(path, encoding="utf-8") as f:
        return normalise(json.load(f).get("settings", {}))
