"""Layout files: a map described in a small closed vocabulary (phase 7).

The user describes a map in words; Claude (in the chat, never an API call)
turns the description into a layout file; this module compiles the layout
into a full spec, deterministically. The AI decides layout and intent only:
every number below comes from this file, calibrated against retail Junon,
so a layout cannot ask for anything the generator has not been taught.

A layout (JSON):

    {
     "layout": 1,
     "description": "the user's words, kept for the record",
     "notes": ["what Claude chose, substituted or could not do"],
     "name": "Mapgen Coast", "folder": "MAPGEN02", "size": "4x4", "seed": 7,
     "terrain": {"character": "rolling", "like": "JG01",
                 "tilt": {"toward": "north", "rise": "gentle"},
                 "border": "cliffs" | "none" | {"cliffs": ["north", "east"]},
                 "features": [{"name": "nw_peaks", "kind": "mountains",
                               "where": "north-west", "size": "large", "height": "high"}]},
     "water": {"lakes": [{"name": "lake", "where": "south-west", "size": "medium",
                          "banks": {"west": "rocky"}}],
               "sea": {"edge": "south", "share": 0.3},
               "flood": {"share": 0.5}},
     "cover": [{"where": "north edge", "is": "forest", "density": "dense",
                "fade": "toward south"}],
     "villages": [{"name": "farm", "prefab": "breezy_farm", "where": "south-east",
                   "entrance": "farmgate"},
                  {"name": "village", "prefab": "adventurer_village", "on": "hill"}],
     "roads": [{"from": "farm", "to": "village"}],
     "start": {"near": "farm"}
    }

Places ("where") are words: "everywhere", "centre", a compass point
("north", "south-west"), "north edge", "north half", "north third",
"north-east of centre", "near <name>", "<name> north shore"; or an exact
{"at": [fx, fy], "radius": fr} / {"box": [fx0, fy0, fx1, fy1]} in fractions
of the map (x east, y north). Unknown words are errors, never guesses.

Every claim the layout makes also becomes an intent check (spec "intent"),
labelled with the layout's own words, so `preview` and `verify` report
whether the map does what the description said.
"""

import copy
import json
import os
import re

from . import terrain

HERE = os.path.dirname(os.path.abspath(__file__))

COMPASS = ["north", "south", "east", "west", "north-east", "north-west", "south-east", "south-west"]

# Terrain characters: fbm hills + the slope the play area is capped at.
# "rolling" is the phase 2-6 terrain the user walked; the others bracket it.
CHARACTER = {
    "flat":    {"amplitude_cm": 400,  "wavelength_m": 300, "octaves": 3, "persistence": 0.4,  "max_slope": 30},
    "gentle":  {"amplitude_cm": 1200, "wavelength_m": 280, "octaves": 4, "persistence": 0.45, "max_slope": 35},
    "rolling": {"amplitude_cm": 2500, "wavelength_m": 240, "octaves": 4, "persistence": 0.45, "max_slope": 38},
    "hilly":   {"amplitude_cm": 4500, "wavelength_m": 200, "octaves": 4, "persistence": 0.5,  "max_slope": 40},
    "rugged":  {"amplitude_cm": 3000, "wavelength_m": 130, "octaves": 4, "persistence": 0.6,  "max_slope": 45},
}
# The slope cap turns whatever it rewrites into planar cone facets. Keep
# each character's noise mostly under its own cap: rugged at 5500/120 had
# 83% of the map rewritten and looked like pyramids; 3000/130/0.6 rewrites
# 15% (median slope 28 deg, p90 38.5 deg). hilly rewrites 19%, rolling 0%.
SYNONYMS = {"jagged": "rugged", "rough": "rugged", "plain": "flat", "plains": "flat", "undulating": "rolling",
            "hills": "hilly"}

RISE_CM = {"gentle": 2000, "moderate": 3500, "steep": 5500}

# Map shape (phase 7b, DESIGN.md "Map shape"): retail keeps 24-55% of a zone
# walkable and dry, the rest cliffs, highlands and sea biting in deep and
# unevenly from the edges. "square" is the phase 1-7 ring of cliffs.
SHAPES = ["organic", "basin", "winding", "valley north-south", "valley east-west", "square"]
PLAY = {"small": 0.3, "medium": 0.45, "large": 0.6}

# Cliff height around the play area (user, phase 7b review: "cliffs don't
# need to be so high"). Steepness, not height, stops a player, but a low
# curved face offers a sideways ramp (the client tests the cell a step
# lands in), so low and medium cliffs carry a rim of boulders or a fence
# (mapgen/barrier.py), which seals them; "none" is a fence line alone.
# Sealing measured on all five phase 7 maps (0-1 trap cells, 44-50% walkable).
CLIFFS = {"low": (2500, 12), "medium": (4500, 16), "high": (8000, 24), "none": (0, 0)}
RIMS = ["rocks", "fence", "none"]
# Mountains (the default border since the second phase 7b review): retail's
# borders are ~30 m ranges, a steep 10-15 m face and an irregular body
# (user: El Verloon's mountains "are not high yet players can't climb"). No
# rim: "mountains cannot be climbed after a certain slope". (face cm, face m,
# body cm)
# Only heights verified to hold on all five phase 7 maps are offered: lower
# faces or bodies (tried 19-28 m faces, 7-12 m bodies, 26-45 m ridges) left
# 10-20 m out under retail's steepness and reopened the flooded map. The
# medium range stands ~37-40 m 10-20 m out: taller than most retail borders
# (18-30 m), as tall as Gorge of Silence.
MOUNTAINS = {"medium": (3000, 10, 1500, 45), "high": (3800, 12, 2600, 45)}
# Tops: "plateau" (default since the third phase 7b review: a highground top,
# 4 m of gentle variation) or "ridges" (the sharp El Verloon-like body).
TOPS = {"plateau": 400, "ridges": None}

FEATURE = {
    # walkable: added before the slope cap
    "hill":       {"kind": "hill", "radius_m": {"small": 50, "medium": 75, "large": 100},
                   "height_cm": {"low": 600, "medium": 1200, "high": 2000}, "top": 0.15},
    "broad hill": {"kind": "hill", "radius_m": {"small": 70, "medium": 95, "large": 120},
                   "height_cm": {"low": 600, "medium": 1000, "high": 1600}, "top": 0.5},
    # scenery: added after the slope cap, flanks too steep to climb
    "mountains":  {"kind": "mountains", "radius_m": {"small": 70, "medium": 100, "large": 140},
                   "height_cm": {"low": 3000, "medium": 5000, "high": 7000}, "top": 0.2, "roughness": 0.35},
}

LAKE = {"small": {"radius_m": 25, "depth_cm": 300}, "medium": {"radius_m": 40, "depth_cm": 400},
        "large": {"radius_m": 60, "depth_cm": 500}}

# Cover: decoration multipliers per object kind (catalogue category, or the
# finer kinds in stats/jg_object_kinds.json) and brush biases (JG brushes:
# 0 dark soil, 1 bright grass, 2 dark grass, 3 sparse grass, 4 rock, 5
# bright soil). The density word raises every multiplier to a power, so
# "dense meadow" means even fewer trees, not more.
COVER = {
    "forest":    {"mult": {"TREE": 3.0, "PLANT": 1.5, "MUSHROOM": 2.0, "GRASS": 0.7, "FLOWER": 0.5, "STONE": 0.8},
                  "bias": {"2": 0.8}, "intent": ("TREE", 1.5)},
    "woods":     {"mult": {"TREE": 2.0, "PLANT": 1.3, "GRASS": 0.9}, "bias": {"2": 0.4}, "intent": ("TREE", 1.3)},
    "meadow":    {"mult": {"TREE": 0.15, "STONE": 0.3, "GRASS": 1.3, "FLOWER": 1.5},
                  "bias": {"1": 0.6, "4": -1.0}, "intent": ("TREE", 0.5)},
    "flowers":   {"mult": {"FLOWER": 4.0, "GRASS": 0.6, "TREE": 0.1, "STONE": 0.2},
                  "bias": {"1": 0.8, "4": -1.5}, "intent": ("FLOWER", 3.0)},
    "rocky":     {"mult": {"STONE": 5.0, "TREE": 0.25, "GRASS": 0.6, "FLOWER": 0.3},
                  "bias": {"4": 1.2, "3": 0.6}, "intent": ("STONE", 2.0)},
    "bare":      {"mult": {"TREE": 0.05, "STONE": 0.5, "GRASS": 0.2, "FLOWER": 0.1, "PLANT": 0.2},
                  "bias": {"0": 0.8, "5": 0.6, "3": 0.5}, "intent": ("GRASS", 0.5)},
    "overgrown": {"mult": {"GRASS": 2.5, "PLANT": 2.5, "TREE": 1.5, "FLOWER": 1.0, "MUSHROOM": 2.0},
                  "bias": {"2": 0.5}, "intent": ("GRASS", 1.5)},
}
COVER_SYNONYMS = {"open meadow": "meadow", "grassland": "meadow", "flower field": "flowers", "boulders": "rocky",
                  "rocks": "rocky", "dense forest": "forest", "trees": "woods", "wasteland": "bare"}
DENSITY = {"sparse": 0.5, "light": 0.75, "normal": 1.0, "dense": 1.3, "very dense": 1.6}

ROAD_STYLES = ("road", "path")


class LayoutError(ValueError):
    pass


def _err(msg):
    raise LayoutError(msg)


def _one_of(value, allowed, what):
    if value not in allowed:
        _err("%s %r is not a word the generator knows; use one of: %s" % (what, value, ", ".join(allowed)))
    return value


# ------------------------------------------------------------------ places


def _signs(word):
    """(x sign, y sign) of a compass word: north-east -> (1, 1)."""
    v = {"north": (0, 1), "south": (0, -1), "east": (1, 0), "west": (-1, 0)}
    return (sum(v[p][0] for p in word.split("-")), sum(v[p][1] for p in word.split("-")))


def area(word, names=()):
    """A place word -> an areas.py descriptor."""
    if isinstance(word, dict):
        if "at" in word:
            return {"point": list(word["at"]), "radius": word.get("radius", 0.15)}
        if "box" in word:
            return {"box": list(word["box"])}
        _err("an exact place needs 'at' or 'box': %r" % (word,))
    w = word.strip().lower()
    if w in ("everywhere", "whole map", "all"):
        return {"all": True}
    if w in ("centre", "center", "middle"):
        return {"point": [0.5, 0.5], "radius": 0.22}
    if w in COMPASS:
        sx, sy = _signs(w)
        k = 0.28 if "-" in w else 0.3                    # sides at 0.8, corners at (0.78, 0.78)
        return {"point": [0.5 + sx * k, 0.5 + sy * k], "radius": 0.22}
    m = re.fullmatch(r"(%s) of (the )?centre" % "|".join(COMPASS), w)
    if m:
        sx, sy = _signs(m.group(1))
        return {"point": [0.5 + sx * 0.16, 0.5 + sy * 0.16], "radius": 0.14}
    m = re.fullmatch(r"(north|south|east|west) (edge|half|third)", w)
    if m:
        side, part = m.groups()
        if part == "edge":
            return {"edge": side, "depth": 0.22}
        f = 0.5 if part == "half" else 1 / 3.0
        box = {"north": [0, 1 - f, 1, 1], "south": [0, 0, 1, f], "east": [1 - f, 0, 1, 1], "west": [0, 0, f, 1]}[side]
        return {"box": box}
    m = re.fullmatch(r"near (the )?(\S+)", w)
    if m:
        name = m.group(2)
        if names and name not in names:
            _err("place %r: nothing called %r (known: %s)" % (word, name, ", ".join(names)))
        return {"near": name, "extra_m": 40.0}
    m = re.fullmatch(r"(the )?(\S+?)('s)? (%s) (shore|bank)" % "|".join(COMPASS), w)
    if m:
        name = m.group(2)
        if names and name not in names:
            _err("place %r: nothing called %r (known: %s)" % (word, name, ", ".join(names)))
        return {"shore": name, "side": m.group(4), "radius_m": 30.0}
    _err("place %r is not a word the generator knows. Use: everywhere, centre, a compass point (%s), "
         "'<north|south|east|west> edge|half|third', '<compass> of centre', 'near <name>', "
         "'<name> <compass> shore', or {\"at\": [x, y], \"radius\": r} / {\"box\": [x0, y0, x1, y1]}"
         % (word, ", ".join(COMPASS)))


# ------------------------------------------------------------------ compile


def base_spec():
    """Calibrated defaults (the phase 6 spec's paint / decorate / water)."""
    with open(os.path.join(HERE, "specs", "phase6-village.json"), encoding="utf-8") as f:
        s = json.load(f)
    for k in ("villages", "_comment"):
        s.pop(k, None)
    s["paint"]["paths"] = []
    # Paint in retail-sized patches (phase 7b, DESIGN.md "Ground paint"):
    # despeckle the noise picks, longer patches, a cliff palette above 40
    # deg. Measured over the five phase 7 maps against JG01-05/07: corners
    # agree 74.5 / 80.1 / 83.8% (all / >=45 / >=60 deg; retail 75.5 / 83.6
    # / 88.8), cliff soil 10-14% (retail 11-12; was 24-35), rock 10-12%.
    s["paint"].update({"despeckle": 2, "patch_wavelength_m": 90, "calibration_rounds": 16,
                       # one cliff material for the whole map, from its theme
                       # (user, phase 7b review: a rock cliff beside a meadow looks
                       # strange; El Verloon's mountains are all its desert rock)
                       "cliff": {"deg": 40, "material": "grass", "wavelength_m": 400, "spread": 1.0,
                                 "materials": {"grass": {"1": 0.85, "2": 0.15},
                                               "rock": {"4": 0.85, "1": 0.15},
                                               "earth": {"0": 0.8, "1": 0.2}}},
                       "coherence_target": {"all": 75.5, "steep": 83.6, "tolerance": 6.0}})
    s["water"] = {k: v for k, v in s["water"].items() if k != "lakes"}
    s["decorate"]["kinds"] = "jg_object_kinds.json"
    s["decorate"]["max_per_chunk"] = 250            # retail's busiest chunk holds 278
    # nothing but rocks on cliff faces (user, third phase 7b review: grass
    # tufts and shrubs on the border's faces)
    s["decorate"]["max_slope_deg"] = 45
    return s


def load_profiles():
    path = os.path.join(HERE, "stats", "jg_zone_profiles.json")
    if not os.path.exists(path):
        return {}
    with open(path, encoding="utf-8") as f:
        return json.load(f)["zones"]


def compile_layout(lay):
    """Layout dict -> full spec dict (with "intent"). Raises LayoutError."""
    if lay.get("layout") != 1:
        _err("not a layout file (needs \"layout\": 1)")
    s = base_spec()
    s["_comment"] = "Compiled from a layout file by mapgen/layout.py; do not edit, edit the layout."
    s["_layout"] = {"description": lay.get("description", ""), "notes": lay.get("notes", [])}
    s["name"] = lay.get("name", "Mapgen")
    s["folder"] = lay.get("folder") or _err("layout needs a 'folder' (MAPGENnn)")
    size = lay.get("size")
    if size is None:
        # a map need not be big (user, phase 7b review): one village, one
        # lake and no mountains fit a 3x3 (480 m); more needs a 4x4
        small = (len(lay.get("villages", [])) <= 1 and len((lay.get("water") or {}).get("lakes", [])) <= 1
                 and not (lay.get("terrain") or {}).get("features"))
        size = "3x3" if small else "4x4"
    m = re.fullmatch(r"(\d)x(\d)", str(size))
    if not m:
        _err("size must be like '4x4'")
    w, h = int(m.group(1)), int(m.group(2))
    if not (1 <= w <= 8 and 1 <= h <= 8):
        _err("size %dx%d: 1x1 to 8x8 chunks" % (w, h))
    s["chunks"] = {"x0": 32 - w // 2, "y0": 32 - h // 2, "width": w, "height": h}
    seed = int(lay.get("seed", 1))
    intent = []

    # names a place may refer to, in build order
    names = [l["name"] for l in (lay.get("water") or {}).get("lakes", []) if "name" in l]
    names += [f["name"] for f in (lay.get("terrain") or {}).get("features", [])]
    if (lay.get("water") or {}).get("sea"):
        names.append("sea")
    if (lay.get("water") or {}).get("flood"):
        names.append("water")
    water_names = [n for n in names if n not in [f["name"] for f in (lay.get("terrain") or {}).get("features", [])]]
    names += [v.get("name", v.get("prefab")) for v in lay.get("villages", [])]

    # --- terrain
    lt = lay.get("terrain") or {}
    t = s["terrain"]
    t["seed"] = seed
    like = lt.get("like")
    char = SYNONYMS.get(lt.get("character", "rolling"), lt.get("character", "rolling"))
    _one_of(char, list(CHARACTER), "terrain character")
    ch = dict(CHARACTER[char])
    density_like = None
    if like:
        prof = load_profiles().get(like)
        if not prof:
            _err("like %r: no measured profile (run `mapgen-zone.py profiles`; known: %s)"
                 % (like, ", ".join(sorted(load_profiles())) or "none"))
        if "character" not in lt:
            char = prof["character"]
            ch = dict(CHARACTER[char])
        density_like = prof["density_vs_jg"]
    t["hills"] = {"amplitude_cm": ch["amplitude_cm"], "wavelength_m": ch["wavelength_m"],
                  "octaves": ch["octaves"], "persistence": ch["persistence"]}
    t["max_play_slope_deg"] = ch["max_slope"]
    t["min_connected_fraction"] = 0.95
    if lt.get("cliff_material"):
        s["paint"]["cliff"]["material"] = _one_of(lt["cliff_material"], ["auto", "grass", "rock", "earth"],
                                                  "cliff material")
    shape = _one_of(lt.get("shape", "organic"), SHAPES, "map shape")
    border = lt.get("border", "cliffs")
    edges = list(terrain.EDGES)
    sea = (lay.get("water") or {}).get("sea")
    if shape != "square":
        play = lt.get("play", "medium")
        share = PLAY[_one_of(play, list(PLAY), "play area size")] if isinstance(play, str) else float(play)
        bd = lt.get("border", "mountains")
        if bd == "cliffs":
            bd = "mountains"            # "ringed by cliffs": a retail-like range is that ring
        if not lt.get("cliffs") and (isinstance(bd, dict) or bd in ("mountains", "fence")):
            kind_b = bd if isinstance(bd, str) else bd.get("kind", "mountains")
            kind_b = _one_of(kind_b, ["mountains", "fence"], "border")
            if kind_b == "fence":
                # a fence line alone (villages, cities): natural ground outside
                t["shape"] = {"kind": shape, "share": share, "open": [sea["edge"]] if sea else [],
                              "wall_m": 0, "wall_cm": 0, "highland_cm": 0, "edge": "fence"}
            else:
                h = _one_of(bd.get("height", "medium") if isinstance(bd, dict) else "medium", list(MOUNTAINS),
                            "mountain height")
                face_cm, face_m, body_cm, ridge_m = MOUNTAINS[h]
                top = _one_of(bd.get("top", "plateau") if isinstance(bd, dict) else "plateau", list(TOPS),
                              "mountain top")
                t["shape"] = {"kind": shape, "share": share, "open": [sea["edge"]] if sea else [],
                              "profile": "range", "wall_m": face_m, "wall_cm": face_cm,
                              "body_cm": TOPS[top] or body_cm, "top": top,
                              "range_wavelength_m": ridge_m, "edge": "none"}
            intent.append({"check": "border_like_retail",
                           "label": "%s border as steep as retail's (El Verloon, Adventurer's Plain ...)" % kind_b})
        else:
            # the phase 7b first-round cliffs, kept for comparison
            cl = lt.get("cliffs", "low")
            height = _one_of(cl.get("height", "low") if isinstance(cl, dict) else cl, list(CLIFFS), "cliff height")
            rim_default = "fence" if height == "none" else "rocks" if height in ("low", "medium") else "none"
            rim = _one_of((cl.get("rim") if isinstance(cl, dict) else None) or rim_default, RIMS, "cliff rim")
            wall_cm, wall_m = CLIFFS[height]
            t["shape"] = {"kind": shape, "share": share, "open": [sea["edge"]] if sea else [],
                          "wall_m": wall_m, "wall_cm": wall_cm, "highland_cm": 2500 if wall_cm else 0, "edge": rim}
        border = "none"                          # the shape's own cliffs replace the ring
        intent.append({"check": "play_share", "target": share, "tol": 0.12,
                       "label": "%s play area, about %d%% of the map" % (shape, round(share * 100))})
    if isinstance(border, dict):
        edges = [_one_of(e, list(terrain.EDGES), "cliff edge") for e in border.get("cliffs", [])]
    elif border == "none":
        edges = []
    elif border != "cliffs":
        _err("border must be 'cliffs', 'none' or {\"cliffs\": [edges]}")
    if sea:
        edges = [e for e in edges if e != sea["edge"]]
    if edges:
        t["ridge"] = {"height_cm": 4500, "width_m": 36, "edges": edges}
    else:
        t.pop("ridge", None)
    tilt = lt.get("tilt")
    if sea and not tilt:
        opposite = {"north": "south", "south": "north", "east": "west", "west": "east"}[sea["edge"]]
        tilt = {"toward": opposite, "rise": "moderate"}
    if tilt:
        rise = tilt.get("rise", "gentle")
        rise_cm = RISE_CM[_one_of(rise, list(RISE_CM), "tilt rise")] if isinstance(rise, str) else float(rise) * 100
        t["tilt"] = {"toward": _one_of(tilt["toward"], COMPASS, "tilt direction"), "rise_cm": rise_cm}
        intent.append({"check": "tilt", "toward": tilt["toward"], "min_rise_cm": 0.4 * rise_cm,
                       "label": "rising toward the %s" % tilt["toward"]})
    feats = []
    for f in lt.get("features", []):
        kind = _one_of(f.get("kind", "hill"), list(FEATURE), "terrain feature")
        spec = FEATURE[kind]
        size = _one_of(f.get("size", "medium"), ["small", "medium", "large"], "feature size")
        height = _one_of(f.get("height", "medium"), ["low", "medium", "high"], "feature height")
        r = spec["radius_m"][size]
        top = spec["top"] * r
        for v in lay.get("villages", []):        # a village on this hill: its pad fits the top
            if v.get("on") == f["name"]:
                pf = _prefab(v["prefab"])
                top = max(top, pf["radius_m"] + 8.0)
                r = max(r, top + 35.0)
        desc = area(f["where"], names)
        feats.append({"name": f["name"], "kind": spec["kind"], "area": desc, "radius_m": r,
                      "height_cm": spec["height_cm"][height], "top_m": top,
                      "roughness": spec.get("roughness", 0.0)})
        intent.append({"check": "higher", "area": {"point": desc.get("point", [0.5, 0.5]), "radius": 0.08}
                       if "point" in desc else desc, "min_cm": 0.4 * spec["height_cm"][height],
                       "label": "%s %s in the %s" % (size, kind, f["where"])})
    if feats:
        t["features"] = feats

    # --- water
    lw = lay.get("water") or {}
    wtr = s["water"]
    lakes = []
    for i, l in enumerate(lw.get("lakes", [])):
        size = _one_of(l.get("size", "medium"), list(LAKE), "lake size")
        lk = {"name": l.get("name", "lake%d" % (i + 1)), "radius_m": LAKE[size]["radius_m"],
              "depth_cm": LAKE[size]["depth_cm"], "shore_m": 14, "shore_slope_deg": 18, "irregularity": 0.25,
              "ring_m": 25, "margin_cm": 30, "bank_m": 10, "skirt_m": 40, "max_cut_cm": 1200}
        if l.get("where"):
            lk["where"] = area(l["where"], names)
            intent.append({"check": "inside", "feature": lk["name"], "area": lk["where"],
                           "label": "%s lake in the %s" % (size, l["where"])})
        else:
            lk["centre"] = "auto"
        banks = {}
        for side, style in (l.get("banks") or {}).items():
            banks[_one_of(side, COMPASS, "bank side")] = _one_of(style, ["rocky", "gentle"], "bank style")
        if banks:
            lk["banks"] = {k: v for k, v in banks.items() if v == "rocky"}
        lakes.append(lk)
    if lakes:
        wtr["lakes"] = lakes
    flood = lw.get("flood") or (sea and {"share": sea.get("share", 0.3)})
    if flood:
        share = float(flood["share"])
        if not 0.02 <= share <= 0.8:
            _err("water share %.2f: 0.02 to 0.8" % share)
        # compact basins (water.flood_compact): retail water is seas and big
        # lakes, not the maze of inlets raw hilly terrain floods into (90 m:
        # at 60 m the half-water map still had paint broken by shore rings)
        wtr["flood"] = {"name": "sea" if sea else "water", "fraction": share, "compact_m": 90}
        intent.append({"check": "water_share", "target": share, "tol": 0.06,
                       "label": "%d%% water" % round(share * 100)})
        if sea:
            intent.append({"check": "wet_in", "area": {"edge": sea["edge"], "depth": 0.15}, "min": 0.5,
                           "label": "the sea along the %s edge" % sea["edge"]})
    if not lakes and not flood:
        s.pop("water")

    # --- villages, roads, start
    vills = []
    for v in lay.get("villages", []):
        name = v.get("name", v["prefab"])
        _prefab(v["prefab"])
        out = {"name": name, "prefab": v["prefab"], "rotation": v.get("rotation", "auto"), "pad_margin_m": 6,
               "skirt_m": 30, "keep_clear_m": 6, "pad_from_walls": True}
        if v.get("entrance"):
            out["entrance"] = v["entrance"]
        if v.get("on"):
            hill = next((f for f in feats if f["name"] == v["on"]), None)
            if hill is None or hill["kind"] != "hill":
                _err("village %s is 'on' %r, which is not a hill feature" % (name, v["on"]))
            out["on"] = v["on"]
            intent.append({"check": "raised", "feature": name, "min_cm": 0.4 * hill["height_cm"],
                           "label": "%s on the %s" % (name, v["on"])})
        elif v.get("where"):
            out["where"] = area(v["where"], names)
            ref = out["where"].get("near") or out["where"].get("shore")
            if ref in water_names:
                out["near_water"] = True             # only the pad keeps off the shore
                out["skirt_m"] = 15
            label = "%s %s" % (name, v["where"]) if isinstance(v["where"], str) else name
            if "shore" in out["where"]:
                intent.append({"check": "shore", "feature": name, "lake": out["where"]["shore"],
                               "side": out["where"]["side"], "max_m": 45.0, "label": label})
            elif "near" in out["where"]:
                intent.append({"check": "near", "feature": name, "to": out["where"]["near"], "max_m": 60.0,
                               "label": label})
            else:
                intent.append({"check": "inside", "feature": name, "area": out["where"], "label": label})
        if _prefab(v["prefab"]).get("water") and not out.get("near_water") and not v.get("pond"):
            land = [f[:-5] for f in sorted(os.listdir(os.path.join(HERE, "prefabs")))
                    if f.endswith(".json") and not _prefab(f[:-5]).get("water")]
            _err("village %s: %s stands on stilts over water (DESIGN.md, Villages). Put it 'near' a lake or "
                 "the sea, ask for \"pond\": true, or use a land village: %s" % (name, v["prefab"], ", ".join(land)))
        if v.get("pond"):
            out["pond"] = True
        if "connect" in v:
            out["connect"] = bool(v["connect"])
        if v.get("skirt_m"):
            out["skirt_m"] = float(v["skirt_m"])
        vills.append(out)
    if vills:
        s["villages"] = vills
    village_names = [v["name"] for v in vills]
    roads = []
    for r in lay.get("roads", []):
        for end in (r["from"], r["to"]):
            if end != "start" and end not in names:
                _err("road end %r: not a village or feature (known: %s)" % (end, ", ".join(names)))
        roads.append({"from": r["from"], "to": r["to"]})
        intent.append({"check": "road", "from": r["from"], "to": r["to"], "label": "%s %s to %s"
                       % (_one_of(r.get("style", "road"), list(ROAD_STYLES), "road style"), r["from"], r["to"])})
    if roads:
        s["roads"] = roads
    st = lay.get("start")
    if st:
        if st.get("near"):
            if st["near"] not in names:
                _err("start near %r: not a village or feature" % st["near"])
            s["start"] = {"near": st["near"]}
            intent.append({"check": "near", "feature": "start", "to": st["near"], "max_m": 5.0,
                           "label": "the player starts at the %s" % st["near"]})
        elif st.get("where"):
            s["start"] = {"area": area(st["where"], names)}

    # --- cover
    cover = []
    if density_like:
        cover.append({"area": {"all": True}, "mult": {k: round(v, 3) for k, v in density_like.items()}})
    for c in lay.get("cover", []):
        kind = COVER_SYNONYMS.get(c["is"], c["is"])
        _one_of(kind, list(COVER), "cover")
        dens = _one_of(c.get("density", "normal"), list(DENSITY), "density")
        k = DENSITY[dens]
        spec = COVER[kind]
        e = {"area": area(c["where"], names), "mult": {kk: round(vv ** k, 4) for kk, vv in spec["mult"].items()},
             "brush_bias": {b: round(x * min(k, 1.3), 3) for b, x in spec["bias"].items()}}
        if c.get("fade"):
            m = re.fullmatch(r"toward (the )?(%s)" % "|".join(COMPASS), c["fade"])
            if not m:
                _err("fade %r: use 'toward <compass>'" % c["fade"])
            e["fade"] = {"toward": m.group(2), "to": 0.3}
        cover.append(e)
        what, ratio = spec["intent"]
        if ratio >= 1:
            ratio = 1 + (ratio - 1) * min(k, 1.0)
        intent.append({"check": "density", "area": e["area"], "kind": what, "ratio": round(ratio, 2),
                       "label": "%s %s in the %s" % (dens, kind, c["where"]) if isinstance(c["where"], str)
                       else "%s %s" % (dens, kind)})
    for lk in lakes:
        for side in (lk.get("banks") or {}):
            e = {"area": {"shore": lk["name"], "side": side, "radius_m": 45.0},
                 "mult": {kk: round(vv ** 1.3, 4) for kk, vv in COVER["rocky"]["mult"].items()},
                 "brush_bias": {b: x for b, x in COVER["rocky"]["bias"].items()}}
            cover.append(e)
            intent.append({"check": "density", "area": e["area"], "kind": "STONE", "ratio": 2.0,
                           "label": "rocky %s bank of %s" % (side, lk["name"])})
    if cover:
        s["cover"] = cover
    s["intent"] = intent
    return s


def _prefab(name):
    path = os.path.join(HERE, "prefabs", name + ".json")
    if not os.path.exists(path):
        have = sorted(f[:-5] for f in os.listdir(os.path.join(HERE, "prefabs")) if f.endswith(".json"))
        _err("prefab %r does not exist; have: %s" % (name, ", ".join(have)))
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def load(path):
    with open(path, encoding="utf-8") as f:
        lay = json.load(f)
    s = compile_layout(lay)
    s["_layout_path"] = os.path.abspath(path)
    return s
