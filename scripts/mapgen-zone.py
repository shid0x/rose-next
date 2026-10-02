"""Build, install, verify and uninstall a generated zone (mapgen phase 1+).

    python scripts/mapgen-zone.py oracle                # byte-compare with the editor's File > New
    python scripts/mapgen-zone.py preview SPEC          # terrain checks + build/mapgen/preview-<folder>.png
    python scripts/mapgen-zone.py build SPEC --out DIR  # write the zone folder anywhere
    python scripts/mapgen-zone.py install SPEC [--zone N] [--dry-run]
    python scripts/mapgen-zone.py verify SPEC
    python scripts/mapgen-zone.py uninstall SPEC [--dry-run]

SPEC is a JSON file such as scripts/mapgen/specs/phase1-flat.json.

What `install` writes into data/:

* the zone folder data/3DDATA/MAPS/<planet_dir>/<folder>/ (it refuses if the
  folder already exists);
* one LIST_ZONE.STB row, copied from the template zone's row and overridden
  where generated zones must differ (see ROW_OVERRIDES);
* one LIST_ZONE_S.STL key, "LZON%03d" % zone.

Everything is recorded in build/mapgen/installed/<folder>.json, with the
previous value of every cell, so `uninstall` is cell-level and never reverts
another script's edits. Whole-file copies of the two tables also go to
build/mapgen/backup/<stamp>/ for emergencies.

Nothing is ever written beside a file under data/: a .bak there would be
baked into the VFS.

Zone number: --zone N, or the lowest row below 250 that is blank in both
LIST_ZONE and ITEM_DROP (drop-table ids share the zone namespace) and has no
LZONnnn key. Rows past the end of LIST_ZONE are not used: growing the table
is avoidable.

After install: restart the servers (they read data/ directly) and re-bake the
VFS for the client (LIST_ZONE.STB, LIST_ZONE_S.STL and the map files are
packed). Then `/mm <zone> <x> <y>` with x/y in 10 m units (verify prints
them).
"""

import argparse
import datetime
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import sys

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DATA = os.path.join(REPO, "data")
BUILD = os.path.join(REPO, "build", "mapgen")
sys.path.insert(0, HERE)

import mapgen  # noqa: E402
from mapgen import chunk, preview, terrain, walk, zon  # noqa: E402
from mapgen.zone import (CHUNK_CM, EventPoint, Tile, ZoneParams, build_zone,  # noqa: E402
                         centre_world, chunk_stem)


def _load_oro():
    spec = importlib.util.spec_from_file_location("import_oro", os.path.join(HERE, "import-oro.py"))
    mod = importlib.util.module_from_spec(spec)
    argv, sys.argv = sys.argv, ["import-oro.py", "--help"]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = argv
    return mod


oro = _load_oro()      # STB / STL codecs (parse + to_bytes only; never its save())

ZONE_STB = r"3DDATA\STB\LIST_ZONE.STB"
ZONE_STL = r"3DDATA\STB\LIST_ZONE_S.STL"
DROP_STB = r"3DDATA\STB\ITEM_DROP.STB"
SKY_STB = r"3DDATA\STB\LIST_SKY.STB"
TEST_ZONE_NO = 250     # rows >= this are never served (lib_gsmain.cpp:38)

# LIST_ZONE game columns (src/common/include/rose/io/stb.h:472-519).
COL_NAME, COL_ZON, COL_START, COL_REVIVE = 0, 1, 2, 3
COL_SKY, COL_MINIMAP, COL_MM_X, COL_MM_Y = 7, 8, 9, 10
COL_DECO, COL_CNST = 11, 12
COL_JOIN, COL_KILL, COL_DEAD = 22, 23, 24
COL_STL = 26
COL_REVIVE_ZONE, COL_REVIVE_X, COL_REVIVE_Y = 31, 32, 33


def P(rel):
    return os.path.join(DATA, rel.replace("\\", os.sep))


def sha(b):
    return hashlib.sha256(b).hexdigest()


# --------------------------------------------------------------------- spec


def load_spec(path):
    with open(path, encoding="utf-8") as f:
        s = json.load(f)
    s["_path"] = os.path.abspath(path)
    return s


def zone_dir_rel(s):
    return "3DDATA\\MAPS\\%s\\%s" % (s["planet_dir"], s["folder"])


def zon_rel(s):
    return "%s\\%s.ZON" % (zone_dir_rel(s), s["folder"])


def template_zon(s, zstb):
    rel = zstb.get(s["template_zone_row"], COL_ZON).decode("latin-1")
    with open(P(rel), "rb") as f:
        return zon.parse(f.read())


def params_from_spec(s):
    c = s["chunks"]
    p = ZoneParams(folder=s["folder"], x0=c["x0"], y0=c["y0"], width=c["width"],
                   height=c["height"], ground_cm=float(s.get("ground_cm", 0)),
                   tile=Tile(**{k: v for k, v in s["tile"].items() if not k.startswith("_")}),
                   events=[])
    t = s.get("terrain")
    if t:
        p.field = terrain.generate(p.width, p.height, t, t["seed"])
    anchor = start_vertex(s, p)
    for e in s["events"]:
        dc, dr = (int(round(v / terrain.GRID_CM)) for v in e.get("offset_cm", [0, 0]))
        if e.get("at") == "centre":
            r, col = (p.height * 32, p.width * 32)
        elif e.get("at") == "start_spot":
            r, col = anchor
        else:
            raise SystemExit("event 'at' must be 'centre' or 'start_spot'")
        r, col = r + dr, col + dc
        z = p.ground_cm if p.field is None else float(p.field[r, col])
        p.events.append(EventPoint(e["name"], p.x0 * CHUNK_CM + col * terrain.GRID_CM,
                                   p.y0 * CHUNK_CM + r * terrain.GRID_CM, z))
    p.start_chunk = (c["x0"] + c["width"] // 2, c["y0"] + c["height"] // 2)
    return p


def start_vertex(s, p):
    """(row, col) of the start point in the field: the gentle spot nearest the
    centre (terrain.pick_start), or the centre for flat ground."""
    if p.field is None:
        return (p.height * 32, p.width * 32)
    return terrain.pick_start(p.field)


def play_mask(s, p):
    """Cells inside the ridge band: the area the connectivity check is about."""
    rows, cols = p.height * 64, p.width * 64
    r = (s.get("terrain") or {}).get("ridge") or {}
    band = int(r.get("width_m", 0) * 100 / terrain.GRID_CM) if r.get("height_cm") else 0
    m = np.zeros((rows, cols), bool)
    m[band:rows - band, band:cols - band] = True
    return m


def terrain_checks(s, p, field, start, check):
    """The phase 2 terrain checks; `field` may come from the spec or from disk."""
    a = walk.analyse(field, start, play_mask(s, p))
    need = (s.get("terrain") or {}).get("min_connected_fraction", 0.95)
    check(a["trap_cells"] == 0,
          "no area the player can walk into but not climb out of (%d trap cells)" % a["trap_cells"])
    check(a["connected_fraction"] >= need,
          "start connected to %.1f%% of the gentle play area (need %.0f%%)"
          % (100 * a["connected_fraction"], 100 * need))
    print("        steepest cell %.1f deg; %d cells >= 54 deg; %d cells reachable"
          % (a["max_slope_deg"], a["steep_cells"], a["reach_cells"]))
    return a


def field_from_disk(zdir, p):
    """Rebuild the global field from installed HIMs; also return seam mismatches."""
    field = np.full(terrain.field_shape(p.width, p.height), np.nan, "<f4")
    seams = []
    for x, y in p.chunks():
        with open(os.path.join(zdir, chunk_stem(x, y) + ".HIM"), "rb") as f:
            h = chunk.parse_him(f.read()).heights[::-1]                 # row 0 = south
        r0, c0 = (y - p.y0) * 64, (x - p.x0) * 64
        dst = field[r0:r0 + 65, c0:c0 + 65]
        known = ~np.isnan(dst)
        if known.any() and not np.array_equal(dst[known], h[known]):
            seams.append(chunk_stem(x, y))
        field[r0:r0 + 65, c0:c0 + 65] = h
    return field, seams


def build_files(s, zstb):
    return build_zone(params_from_spec(s), template_zon(s, zstb))


def row_values(s, zone_no, zstb):
    """The LIST_ZONE row: the template's row with ROW_OVERRIDES applied."""
    t = s["template_zone_row"]
    c = s["chunks"]
    row = [zstb.get(t, c) for c in range(zstb.cols)]
    overrides = {
        COL_NAME: s["name"],
        COL_ZON: zon_rel(s),
        COL_START: "start",
        COL_REVIVE: "restore",
        COL_MINIMAP: s.get("minimap", "NOMAP"),   # NOMAP: empty panel; blank would keep the last zone's
        # Minimap origin as a chunk file stem (x, 64-y) of the north-west chunk.
        # Meaningless with NOMAP, but the editor's IsValidMap hides a row whose
        # cols 9/10 are blank (MapManager.cs:665-671, editor cols 10/11).
        COL_MM_X: str(c["x0"]),
        COL_MM_Y: str(64 - (c["y0"] + c["height"] - 1)),
        COL_JOIN: "", COL_KILL: "", COL_DEAD: "",  # template triggers are QSD names: never inherit
        COL_STL: "LZON%03d" % zone_no,
        COL_REVIVE_ZONE: "", COL_REVIVE_X: "", COL_REVIVE_Y: "",
    }
    for c, v in overrides.items():
        row[c] = v.encode("latin-1")
    return row


ROW_OVERRIDES = "name, ZON path, start/revive names, minimap, triggers, STL key, revive zone/x/y"


# ------------------------------------------------------------------- tables


def tables():
    return (oro.Stb(P(ZONE_STB)), oro.Stl(P(ZONE_STL)), oro.Stb(P(DROP_STB)))


def zone_is_free(n, zstb, zstl, dstb):
    return (0 < n < min(TEST_ZONE_NO, zstb.rows) and not zstb.occupied(n)
            and not (n < dstb.rows and dstb.occupied(n)) and not zstl.has("LZON%03d" % n))


def pick_zone(zstb, zstl, dstb):
    for n in range(1, min(TEST_ZONE_NO, zstb.rows)):
        if zone_is_free(n, zstb, zstl, dstb):
            return n
    raise SystemExit("no free LIST_ZONE row below %d" % TEST_ZONE_NO)


def stl_remove(stl, key):
    k = key.encode("latin-1")
    j = next((i for i, (x, _) in enumerate(stl.keys) if x == k), None)
    if j is None:
        return None
    text = stl.langs[0][j][0]
    del stl.keys[j]
    for rows in stl.langs:
        del rows[j]
    return text


def manifest_path(s):
    return os.path.join(BUILD, "installed", s["folder"] + ".json")


# ----------------------------------------------------------------- commands


def cmd_build(s, out):
    zstb, _, _ = tables()
    files = build_files(s, zstb)
    for rel, blob in files.items():
        dst = os.path.join(out, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(blob)
    print("wrote %d files to %s" % (len(files), out))
    return 0


def cmd_preview(s):
    """Generate, analyse and render without installing anything."""
    p = params_from_spec(s)
    if p.field is None:
        raise SystemExit("spec has no 'terrain' section")
    check = Checks()
    a = terrain_checks(s, p, p.field, start_vertex(s, p), check)
    os.makedirs(BUILD, exist_ok=True)
    out = preview.render(p.field, a, os.path.join(BUILD, "preview-%s.png" % s["folder"]))
    lo, hi = float(p.field.min()), float(p.field.max())
    print("  heights %.1f .. %.1f m; preview %s" % (lo / 100, hi / 100, out))
    again = terrain.generate(p.width, p.height, s["terrain"], s["terrain"]["seed"])
    check(np.array_equal(again.view("u4"), p.field.view("u4")), "same spec + seed -> bit-identical field")
    return 1 if check.failed else 0


def cmd_install(s, zone_arg, dry):
    if os.path.exists(manifest_path(s)):
        raise SystemExit("%s is already installed (%s); uninstall first" % (s["folder"], manifest_path(s)))
    zdir = P(zone_dir_rel(s))
    if os.path.exists(zdir):
        raise SystemExit("zone folder already exists and is not ours: %s" % zdir)
    zstb, zstl, dstb = tables()
    n = zone_arg if zone_arg is not None else pick_zone(zstb, zstl, dstb)
    if not zone_is_free(n, zstb, zstl, dstb):
        raise SystemExit("zone %d is not free (LIST_ZONE / ITEM_DROP row, or LZON%03d key)" % (n, n))

    files = build_files(s, zstb)
    p = params_from_spec(s)
    if p.field is not None:
        gate = Checks()
        print("terrain checks before install:")
        terrain_checks(s, p, p.field, start_vertex(s, p), gate)
        if gate.failed:
            raise SystemExit("terrain fails its checks; nothing installed (see `preview`)")
    row = row_values(s, n, zstb)
    cells = {c: [zstb.get(n, c).decode("latin-1"), row[c].decode("latin-1")]
             for c in range(zstb.cols) if zstb.get(n, c) != row[c]}
    key = "LZON%03d" % n

    print("zone %d  folder %s  (%d files, %d chunks)" % (n, zdir, len(files), s["chunks"]["width"] * s["chunks"]["height"]))
    print("LIST_ZONE row %d: %d cells; LIST_ZONE_S +%s %r" % (n, len(cells), key, s["name"]))
    if dry:
        for c in sorted(cells):
            print("   col %2d  %r" % (c, cells[c][1]))
        print("dry run: nothing written")
        return 0

    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    bdir = os.path.join(BUILD, "backup", stamp)
    os.makedirs(bdir, exist_ok=True)
    for rel in (ZONE_STB, ZONE_STL):
        shutil.copyfile(P(rel), os.path.join(bdir, os.path.basename(rel)))

    for c in range(zstb.cols):
        zstb.set(n, c, row[c])
    zstl.append(key, n, s["name"])
    stb_blob, stl_blob = zstb.to_bytes(), zstl.to_bytes()

    manifest = {"zone": n, "folder": s["folder"], "zone_dir": zone_dir_rel(s), "spec": s["_path"],
                "stl_key": key, "stl_text": s["name"], "cells": {str(c): v for c, v in cells.items()},
                "files": {rel: sha(b) for rel, b in files.items()}, "backup": bdir,
                "installed": stamp}
    os.makedirs(os.path.dirname(manifest_path(s)), exist_ok=True)
    with open(manifest_path(s), "w", encoding="utf-8") as f:     # first, so a crash is recoverable
        json.dump(manifest, f, indent=1)
    for rel, blob in files.items():
        dst = os.path.join(zdir, rel.replace("/", os.sep))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        with open(dst, "wb") as f:
            f.write(blob)
    with open(P(ZONE_STB), "wb") as f:
        f.write(stb_blob)
    with open(P(ZONE_STL), "wb") as f:
        f.write(stl_blob)
    print("installed; manifest %s; table backups %s" % (manifest_path(s), bdir))
    return cmd_verify(s)


def cmd_uninstall(s, dry):
    mp = manifest_path(s)
    if not os.path.exists(mp):
        raise SystemExit("not installed (no %s)" % mp)
    with open(mp, encoding="utf-8") as f:
        m = json.load(f)
    n, zdir = m["zone"], P(m["zone_dir"])
    zstb, zstl, _ = tables()
    restored, kept = 0, []
    for c, (old, new) in m["cells"].items():
        c = int(c)
        if zstb.get(n, c) == new.encode("latin-1"):
            zstb.set(n, c, old)
            restored += 1
        else:
            kept.append(c)
    removed_text = stl_remove(zstl, m["stl_key"])
    gone, changed = [], []
    for rel, h in m["files"].items():
        path = os.path.join(zdir, rel.replace("/", os.sep))
        if os.path.exists(path):
            with open(path, "rb") as f:
                (gone if sha(f.read()) == h else changed).append(path)
    print("zone %d: restore %d cells%s; remove STL %s (%r); delete %d files%s" % (
        n, restored, " (left changed cols %s)" % kept if kept else "", m["stl_key"], removed_text,
        len(gone), "; KEEP %d edited files" % len(changed) if changed else ""))
    if dry:
        print("dry run: nothing written")
        return 0
    with open(P(ZONE_STB), "wb") as f:
        f.write(zstb.to_bytes())
    with open(P(ZONE_STL), "wb") as f:
        f.write(zstl.to_bytes())
    for path in gone:
        os.remove(path)
    for dp, _, _ in sorted(os.walk(zdir), key=lambda t: -len(t[0])):
        if not os.listdir(dp):
            os.rmdir(dp)
    if not changed:
        os.remove(mp)
    print("uninstalled" + ("" if not changed else "; manifest kept because of edited files"))
    return 0


class Checks:
    def __init__(self):
        self.failed = 0

    def __call__(self, ok, what):
        print("  %s %s" % ("ok  " if ok else "FAIL", what))
        if not ok:
            self.failed += 1


def cmd_verify(s):
    mp = manifest_path(s)
    if not os.path.exists(mp):
        raise SystemExit("not installed (no %s)" % mp)
    with open(mp, encoding="utf-8") as f:
        m = json.load(f)
    n, zdir = m["zone"], P(m["zone_dir"])
    check = Checks()
    zstb, zstl, dstb = tables()
    print("verify zone %d (%s)" % (n, zdir))

    # Tables: the row, its references, and that both tables still round-trip.
    for rel, obj in ((ZONE_STB, zstb), (ZONE_STL, zstl)):
        with open(P(rel), "rb") as f:
            check(obj.to_bytes() == f.read(), "%s re-serialises byte-identical" % rel)
    check(n < TEST_ZONE_NO, "zone %d < %d (served by the gameserver)" % (n, TEST_ZONE_NO))
    check(not (n < dstb.rows and dstb.occupied(n)), "ITEM_DROP row %d is blank" % n)
    row = row_values(s, n, zstb)
    check(all(zstb.get(n, c) == row[c] for c in range(zstb.cols)), "LIST_ZONE row %d matches the spec" % n)
    # The editor's Open dialog lists a row only if these game columns are
    # non-blank (MapManager.IsValidMap, MapManager.cs:647-682; editor col = game col + 1).
    blank = [c for c in (COL_ZON, COL_START, COL_REVIVE, COL_MM_X, COL_MM_Y, COL_DECO, COL_CNST)
             if not zstb.get(n, c).strip()]
    check(not blank, "editor IsValidMap: required columns non-blank %s" % (blank or ""))
    check(zstl.has(m["stl_key"]) and zstl.name(m["stl_key"]) == s["name"],
          "LIST_ZONE_S %s = %r" % (m["stl_key"], s["name"]))
    zon_path = P(zstb.get(n, COL_ZON).decode("latin-1"))
    check(os.path.isfile(zon_path), "col %d ZON exists: %s" % (COL_ZON, zon_path))
    for c in (COL_DECO, COL_CNST):
        rel = zstb.get(n, c).decode("latin-1").replace("\\\\", "\\")
        check(os.path.isfile(P(rel)), "col %d ZSC exists: %s" % (c, rel))
    sky = oro.Stb(P(SKY_STB))
    sky_row = int(zstb.get(n, COL_SKY) or b"0")
    check(0 <= sky_row < sky.rows and sky.occupied(sky_row), "col %d sky row %d exists in LIST_SKY" % (COL_SKY, sky_row))

    # Files: present, unchanged since install, and round-trip through the codecs.
    bad_hash, bad_rt = [], []
    for rel, h in m["files"].items():
        path = os.path.join(zdir, rel.replace("/", os.sep))
        if not os.path.isfile(path):
            bad_hash.append(rel + " (missing)")
            continue
        with open(path, "rb") as f:
            blob = f.read()
        if sha(blob) != h:
            bad_hash.append(rel)
        parse, build = mapgen.CODECS[os.path.splitext(rel)[1].lower()]
        if build(parse(blob)) != blob:
            bad_rt.append(rel)
    check(not bad_hash, "%d files present and unchanged since install %s" % (len(m["files"]), bad_hash or ""))
    check(not bad_rt, "every file round-trips through the mapgen codecs %s" % (bad_rt or ""))

    # Content: chunk set, grid sizes, tile ids and textures, events.
    with open(zon_path, "rb") as f:
        z = zon.parse(f.read())
    tiles, textures = z.lump(zon.TILES), z.lump(zon.TEXTURES)
    p = params_from_spec(s)
    stems = {chunk_stem(x, y) for x, y in p.chunks()}
    on_disk = {os.path.splitext(f)[0] for f in os.listdir(zdir) if f.upper().endswith(".HIM")}
    check(on_disk == stems, "chunk set on disk = spec %s" % sorted(stems))
    used_tex, problems = set(), []
    for st in sorted(stems):
        missing = [ext for ext in (".HIM", ".TIL", ".IFO", ".MOV")
                   if not os.path.isfile(os.path.join(zdir, st + ext))]
        if missing:
            problems.append("%s missing %s" % (st, missing))
            continue
        with open(os.path.join(zdir, st + ".HIM"), "rb") as f:
            him = chunk.parse_him(f.read())
        if (him.width, him.height) != (65, 65):
            problems.append("%s.HIM is %dx%d" % (st, him.width, him.height))
        with open(os.path.join(zdir, st + ".TIL"), "rb") as f:
            til = chunk.parse_til(f.read())
        ids = np.unique(til.tiles["tile_id"])
        if ids.min() < 0 or ids.max() >= len(tiles):
            problems.append("%s.TIL tile ids %s outside the %d-row tile table" % (st, ids.tolist(), len(tiles)))
            continue
        for tid in ids:
            r = tiles[int(tid)]
            used_tex.update({int(r[0] + r[2]), int(r[1] + r[3])})
    check(not problems, "every chunk: HIM/TIL/IFO/MOV present, HIM 65x65, tile ids in the ZON tile table %s"
          % (problems or ""))
    missing_tex = [textures[i].decode("latin-1") for i in sorted(used_tex)
                   if not os.path.isfile(P(textures[i].decode("latin-1")))]
    check(not missing_tex, "the %d tile textures used exist on disk %s" % (len(used_tex), missing_tex or ""))
    names = [e.name for e in z.lump(zon.EVENTS)]
    for c in (COL_START, COL_REVIVE):
        check(zstb.get(n, c) in names, "col %d event %r is in the ZON" % (c, zstb.get(n, c)))
    for e in z.lump(zon.EVENTS):
        wx, wy = e.x + 520000, e.y + 520000
        inside = chunk_stem(int(wx // CHUNK_CM), int(wy // CHUNK_CM)) in stems
        check(inside, "event %r at world (%.0f, %.0f) lies on a chunk" % (e.name, wx, wy))
    start = next(e for e in z.lump(zon.EVENTS) if e.name == b"start")

    # Terrain, re-derived from the files on disk rather than from the spec.
    field, seams = field_from_disk(zdir, p)
    check(not seams, "neighbouring chunks share identical edge heights %s" % (seams or ""))
    bad_bounds = []
    for x, y in p.chunks():
        with open(os.path.join(zdir, chunk_stem(x, y) + ".HIM"), "rb") as f:
            him = chunk.parse_him(f.read())
        placeholder = bool(np.all(np.abs(him.bounds.patches) > 1e30))
        flat = float(him.heights.min()) == float(him.heights.max())
        if placeholder and flat:
            continue                       # phase-1 style flat chunk: editor placeholders
        want = chunk.compute_him_bounds(him.heights)
        if not (np.array_equal(want.patches, him.bounds.patches) and np.array_equal(want.quads, him.bounds.quads)):
            bad_bounds.append(chunk_stem(x, y))
    check(not bad_bounds, "HIM culling bounds are real and match the retail formula %s" % (bad_bounds or ""))
    sv = (int(round((start.y + 520000 - p.y0 * CHUNK_CM) / terrain.GRID_CM)),
          int(round((start.x + 520000 - p.x0 * CHUNK_CM) / terrain.GRID_CM)))
    check(abs(float(field[sv]) - start.z) < 1.0, "start event height %.1f cm = terrain %.1f cm" % (start.z, field[sv]))
    terrain_checks(s, p, field, sv, check)

    print("\n  GM warp: /mm %d %d %d" % (n, round((start.x + 520000) / 1000), round((start.y + 520000) / 1000)))
    print("  %s" % ("ALL CHECKS PASSED" if not check.failed else "%d CHECK(S) FAILED" % check.failed))
    return 1 if check.failed else 0


def cmd_oracle():
    """Byte-compare mapgen with the editor's File > New for identical inputs."""
    out = os.path.join(BUILD, "oracle-out")
    ps1 = os.path.join(HERE, "mapgen", "oracle", "Run-NewZoneOracle.ps1")
    r = subprocess.run(["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File", ps1,
                        "-OutDir", out, "-SizeX", "2", "-SizeY", "2", "-ZoneFile", "ORACLE.ZON"],
                       capture_output=True, text=True)
    if r.returncode:
        print(r.stdout, r.stderr)
        raise SystemExit("oracle failed")
    zstb, _, _ = tables()
    with open(P(r"3DDATA\MAPS\JUNON\JG01\JG01.ZON"), "rb") as f:
        tmpl = zon.parse(f.read())
    # The editor's New: chunk files "{y+30}_{x+30}" (first number = slot x),
    # i.e. slots x 30-31, y 33-34; start (4930 m, 5470 m), restore
    # (4910 m, 5500 m) in its 5200 m-shifted metres; ZON start 30/30; tile
    # brush 1 / set 1 / index 15 / id 1; economy constants (New.xaml.cs:241-452).
    p = ZoneParams(folder="ORACLE", x0=30, y0=33, width=2, height=2, ground_cm=0.0,
                   tile=Tile(brush=1, tile_set=1, tile_index=15, tile_id=1),
                   events=[EventPoint("start", 493000.0, 547000.0, 0.0),
                           EventPoint("restore", 491000.0, 550000.0, 0.0)],
                   start_chunk=(30, 30),
                   economy=zon.Economy(b"0", 0, b"button1", b"button2",
                                       [35, 500, 30, 10, 20, 20, 10, 10, 10, 10, 10, 30, 10]))
    ours = build_zone(p, tmpl)
    theirs = {}
    for dp, _, fs in os.walk(out):
        for fn in fs:
            rel = os.path.relpath(os.path.join(dp, fn), out).replace(os.sep, "/")
            with open(os.path.join(dp, fn), "rb") as f:
                theirs[rel] = f.read()
    ok = True
    for rel in sorted(set(ours) | set(theirs)):
        a, b = ours.get(rel), theirs.get(rel)
        same = a == b
        ok &= same
        print("  %s %-44s %s" % ("same" if same else "DIFF", rel,
                                  "" if same else "ours %s / editor %s" % (
                                      len(a) if a else "missing", len(b) if b else "missing")))
    print("\n%s (the editor also writes a 512x512 plane lightmap per chunk; mapgen does not, by design)"
          % ("BYTE-IDENTICAL to the editor's File > New" if ok else "DIFFERS from the editor"))
    return 0 if ok else 1


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("oracle")
    sub.add_parser("walk-selftest")
    pv = sub.add_parser("preview")
    pv.add_argument("spec")
    b = sub.add_parser("build")
    b.add_argument("spec")
    b.add_argument("--out", required=True)
    i = sub.add_parser("install")
    i.add_argument("spec")
    i.add_argument("--zone", type=int)
    i.add_argument("--dry-run", action="store_true")
    v = sub.add_parser("verify")
    v.add_argument("spec")
    u = sub.add_parser("uninstall")
    u.add_argument("spec")
    u.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    if a.cmd == "oracle":
        return cmd_oracle()
    if a.cmd == "walk-selftest":
        print("Walkability checker on synthetic terrain with known answers:")
        fails = walk.selftest()
        print("  %s" % ("ALL PASSED" if not fails else "FAILED: %s" % fails))
        return 1 if fails else 0
    s = load_spec(a.spec)
    if a.cmd == "preview":
        return cmd_preview(s)
    if a.cmd == "build":
        return cmd_build(s, a.out)
    if a.cmd == "install":
        return cmd_install(s, a.zone, a.dry_run)
    if a.cmd == "verify":
        return cmd_verify(s)
    return cmd_uninstall(s, a.dry_run)


if __name__ == "__main__":
    sys.exit(main())
