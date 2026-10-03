"""Bake lightmaps for a zone made (or edited) in the map editor.

    python scripts/relight-zone.py settings              # every setting: default and what it does
    python scripts/relight-zone.py list                  # zones, and which are relit
    python scripts/relight-zone.py show  --zone 29       # what the bake will see; current settings
    python scripts/relight-zone.py bake  --zone 29 [--set sun_el=35 --set shadow_ratio=0.8 ...]
                                         [--settings my.json] [--save] [--dry-run] [--out DIR]
    python scripts/relight-zone.py shots --zone 29 [--at 5120,5070 ...] [--label before]
    python scripts/relight-zone.py verify  --zone 29
    python scripts/relight-zone.py restore --zone 29     # back to the zone's original lightmaps

--zone takes a LIST_ZONE row or the map folder name (JD04).

What bake does: reads the zone exactly as the client does (heights, the
objects of IFO lumps 1 and 3, water), bakes the ground lightmap of every
chunk (x_y/x_y_PlaneLightingMap.dds) and the object lightmaps
(x_y/LightMap/*.lit + Object_/Building_ atlases) with the map generator's
bakers (mapgen/lighting.py, mapgen/objlight.py), writes them over the
zone's own, and checks the result with scripts/audit-lightmap-index.py's
rules: anything that could crash the client rolls the whole run back.
Old atlases the new .lit files no longer name are removed. After a bake:
re-bake the VFS (scripts/pack.ps1); the servers do not read lightmaps.

Settings, in order: the defaults (the retail Junon look the generator is
tuned to), the zone's saved file scripts/mapgen/relight/<FOLDER>.json,
--settings FILE, then each --set. --save writes everything that differs
from the defaults to the zone's file, so the next bake (yours or a
teammate's) gives the same result. data/ is not in git: that file is the
record of how a zone was lit.

Backups: build/relight/<FOLDER>/ keeps the zone's files from before the
FIRST bake, so `restore` always returns to the original lightmaps however
many times the zone was relit. Generated zones (scripts/mapgen-zone.py) are
refused: their lighting lives in the layout's "lighting" section.

Re-run bake after editing a map in the editor: the editor never renumbers
lightmap entries when an object is deleted or inserted, so an edited map's
old lightmaps light the wrong objects (scripts/audit-lightmap-index.py).
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
import textwrap
import time
import types
from collections import Counter

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
_spec = importlib.util.spec_from_file_location("mapgen_zone", os.path.join(HERE, "mapgen-zone.py"))
mz = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(mz)
_spec = importlib.util.spec_from_file_location("audit_lightmap_index", os.path.join(HERE, "audit-lightmap-index.py"))
audit = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(audit)
from mapgen import ifo, relight  # noqa: E402

OUT = os.path.join(mz.REPO, "build", "relight")


# ----------------------------------------------------------------- zones

def zone_rows():
    """[(row, name, zone dir, deco rel, cnst rel)] of every LIST_ZONE row with a map."""
    zstb = mz.oro.Stb(mz.P(mz.ZONE_STB))
    out = []
    for row in range(zstb.rows):
        try:
            cells = [zstb.get(row, c).decode("latin-1").strip() for c in (mz.COL_NAME, mz.COL_ZON, mz.COL_DECO, mz.COL_CNST)]
        except Exception:                          # noqa: BLE001
            continue
        name, zon, deco, cnst = cells
        if zon and os.path.exists(mz.P(zon)):
            out.append((row, name, os.path.dirname(mz.P(zon)), deco.replace("\\\\", "\\"), cnst.replace("\\\\", "\\")))
    return out


def resolve(arg):
    """The Zone for a LIST_ZONE row or a map folder name."""
    rows = zone_rows()
    if arg.isdigit():
        hit = [r for r in rows if r[0] == int(arg)]
        if not hit:
            raise SystemExit("zone %s has no map in LIST_ZONE" % arg)
        zdir = hit[0][2]
    else:
        hit = [r for r in rows if os.path.basename(r[2]).lower() == arg.lower()]
        if not hit:
            raise SystemExit("no LIST_ZONE row uses a map folder named %s" % arg)
        zdir = hit[0][2]
    same = [r for r in rows if os.path.normcase(r[2]) == os.path.normcase(zdir)]
    if len({(r[3].lower(), r[4].lower()) for r in same}) > 1:
        raise SystemExit("zones %s share %s with different ZSCs; relight cannot tell which the map uses"
                         % ([r[0] for r in same], zdir))
    folder = os.path.basename(zdir)
    if os.path.exists(os.path.join(mz.BUILD, "installed", folder + ".json")):
        raise SystemExit("%s is a generated zone: set its lighting in the layout's \"lighting\" section and "
                         "reinstall it with scripts/mapgen-zone.py" % folder)
    z = relight.Zone(mz.DATA, zdir, same[0][3], same[0][4], [r[0] for r in same])
    z.names = [r[1] for r in same]
    return z


def settings_for(zone, a):
    """(settings, where each came from)."""
    s, src = {}, {}
    path = relight.settings_file(HERE, zone.folder)
    for k, v in relight.load_settings(path).items():
        s[k], src[k] = v, os.path.relpath(path, mz.REPO)
    if getattr(a, "settings", None):
        with open(a.settings, encoding="utf-8") as f:
            raw = json.load(f)
        for k, v in relight.normalise(raw.get("settings", raw)).items():
            s[k], src[k] = v, a.settings
    for kv in getattr(a, "set", None) or []:
        if "=" not in kv:
            raise SystemExit("--set wants key=value, got %r" % kv)
        k, v = kv.split("=", 1)
        try:
            s[k.strip()] = relight.parse_value(k.strip(), v)
        except ValueError as e:
            raise SystemExit(str(e))
        src[k.strip()] = "--set"
    return s, src


def fmt(v):
    if isinstance(v, (tuple, list)):
        return ",".join(("%g" % x) if isinstance(x, float) else str(x) for x in v) or "(none)"
    if isinstance(v, bool):
        return "true" if v else "false"
    return "%g" % v if isinstance(v, float) else str(v)


# ----------------------------------------------------------------- manifest
#
# build/relight/<FOLDER>/manifest.json:
#   original: {path: sha256 or null}  -- every file a bake ever touched, as it
#             was before the first bake (null: it did not exist); copies in
#             original/. Keys are lower-case relative paths: the bakers name
#             atlases Object_32_0.dds, retail OBJECT_32_0.DDS, and Windows
#             treats them as one file (a case-sensitive key once restored an
#             atlas and then deleted it as "created").
#   current:  {path: sha256 or null}  -- the same files after the last bake
#   ifo:      {chunk: sha256}         -- the IFOs at the first bake: the
#             original lightmaps are numbered for those records
#   runs:     [{when, settings, report}]


def manifest_path(zone):
    return os.path.join(OUT, zone.folder, "manifest.json")


def key_of(zone, path):
    return os.path.relpath(path, zone.zdir).replace("/", "\\").lower()


def sha(path):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def ifo_hashes(zone):
    return {ch["stem"].lower(): sha(os.path.join(zone.zdir, f))
            for ch in zone.chunks.values()
            for f in os.listdir(zone.zdir) if f.lower() == ch["stem"].lower() + ".ifo"}


def load_manifest(zone):
    p = manifest_path(zone)
    if not os.path.exists(p):
        return None
    with open(p, encoding="utf-8") as f:
        man = json.load(f)
    for part in ("original", "current"):               # older manifests: mixed-case keys
        merged = {}
        for k, v in man.get(part, {}).items():
            k2 = k.replace("/", "\\").lower()
            if merged.get(k2) is None:                  # twins: the one with a file wins
                merged[k2] = v
        man[part] = merged
    man.setdefault("ifo", {})
    return man


def save_manifest(zone, man):
    os.makedirs(os.path.dirname(manifest_path(zone)), exist_ok=True)
    tmp = manifest_path(zone) + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(man, f, indent=1, sort_keys=True)
    os.replace(tmp, manifest_path(zone))


# ----------------------------------------------------------------- commands

def cmd_settings():
    d = relight.defaults()
    group = None
    for key, g, text in relight.SETTINGS:
        if g != group:
            print("\n[%s]" % g)
            group = g
        lines = textwrap.wrap(text, 72) or [""]
        print("  %-20s %-16s %s" % (key, fmt(d[key]), lines[0]))
        for line in lines[1:]:
            print("  %-20s %-16s %s" % ("", "", line))
    print("\nSet any of them with --set key=value (colours R,G,B; lists 1,2,3), or in a JSON file "
          "{\"settings\": {...}}; --save keeps them for the zone.")
    return 0


def cmd_list():
    seen = set()
    for row, name, zdir, deco, cnst in zone_rows():
        folder = os.path.basename(zdir)
        if folder.lower() in seen:
            continue
        seen.add(folder.lower())
        gen = os.path.exists(os.path.join(mz.BUILD, "installed", folder + ".json"))
        relit = os.path.exists(os.path.join(OUT, folder, "manifest.json"))
        saved = os.path.exists(relight.settings_file(HERE, folder))
        print("%4d  %-28s %-12s %s" % (row, name[:28], folder,
                                        "generated (layout)" if gen else ("relit" if relit else "") +
                                        (" + saved settings" if saved else "")))
    return 0


def cmd_show(zone, a):
    s, src = settings_for(zone, a)
    _, _, eff = relight.bake_configs(s)
    plants, how = zone.small_plants(eff)
    n_obj = sum(len(ch["ifo"].lump(ifo.OBJECT) or []) for ch in zone.chunks.values())
    n_cn = sum(len(ch["ifo"].lump(ifo.CNST) or []) for ch in zone.chunks.values())
    print("zone %s (%s): %s" % ("/".join(map(str, zone.rows)), ", ".join(sorted(set(zone.names))), zone.zdir))
    print("  %d chunks (%dx%d area from chunk %d,%d), %d decorations, %d buildings"
          % (len(zone.chunks), zone.width, zone.height, zone.x0, zone.y0, n_obj, n_cn))
    print("  models: %s, %s" % (zone.deco_rel, zone.cnst_rel))
    print("  grass and flowers: %d object types (%s)" % (len(plants), how))
    from mapgen import lighting, objlight
    parts = objlight.Parts(mz.DATA, zone.zscs, lighting.Shapes(mz.DATA, zone.zscs))
    missing, bad = zone.unlightable(parts)
    if bad:
        print("  %d objects cannot take an object lightmap (no second uv set) and stay vertex-lit: %s"
              % (sum(bad.values()), ", ".join("%s x%d" % kv for kv in bad.most_common(5))))
    if missing:
        print("  %d records name objects the ZSC does not have: the game shows nothing there "
              "(scripts/audit-lightmap-index.py lists them)" % missing)
    print("  relit: %s" % ("yes, %d time(s)" % len(load_manifest(zone)["runs"]) if load_manifest(zone) else "no"))
    if s:
        print("  settings that differ from the defaults:")
        for k in sorted(s):
            print("    %-20s %-16s (%s)" % (k, fmt(s[k]), src[k]))
    else:
        print("  settings: all defaults (python scripts/relight-zone.py settings)")
    return 0


def cmd_bake(zone, a):
    s, src = settings_for(zone, a)
    if a.save:
        path = relight.settings_file(HERE, zone.folder)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        d = relight.defaults()
        keep = {k: (list(v) if isinstance(v, tuple) else v) for k, v in sorted(s.items()) if v != d[k]}
        with open(path, "w", encoding="utf-8", newline="\n") as f:
            json.dump({"_comment": "Lighting of map %s for scripts/relight-zone.py bake (settings: "
                                   "python scripts/relight-zone.py settings)." % zone.folder,
                       "settings": keep}, f, indent=1)
        print("saved %d settings to %s" % (len(keep), os.path.relpath(path, mz.REPO)))
    if a.dry_run:
        return cmd_show(zone, a)
    with open(os.path.join(mz.STATS_DIR, "jg_lightmap_cells.json"), encoding="utf-8") as f:
        cells = json.load(f)["cells"]
    t0 = time.time()
    print("baking %s (%d chunks)..." % (zone.folder, len(zone.chunks)))
    writes, deletes, report = relight.bake(zone, s, cells, log=lambda m: print("  " + m))
    if a.out:
        for path, blob in writes.items():
            dst = os.path.join(a.out, os.path.relpath(path, zone.zdir))
            os.makedirs(os.path.dirname(dst), exist_ok=True)
            with open(dst, "wb") as f:
                f.write(blob)
        print("wrote %d files to %s (data/ untouched) in %.0f s" % (len(writes), a.out, time.time() - t0))
        return 0

    man = load_manifest(zone)
    first = man is None
    if first:
        man = {"zone_dir": os.path.relpath(zone.zdir, mz.DATA), "original": {}, "current": {},
               "ifo": ifo_hashes(zone), "runs": []}
    bdir = os.path.join(OUT, zone.folder, "original")
    touched = list(writes) + deletes
    # this run's own rollback, and the originals of files no bake touched before
    pre = {path: (open(path, "rb").read() if os.path.exists(path) else None) for path in touched}
    for path in touched:
        k = key_of(zone, path)
        if k not in man["original"]:
            man["original"][k] = sha(path)
            if os.path.exists(path):
                os.makedirs(os.path.dirname(os.path.join(bdir, k)), exist_ok=True)
                shutil.copyfile(path, os.path.join(bdir, k))
    save_manifest(zone, man)

    def rollback():
        for path, blob in pre.items():
            if blob is None:
                if os.path.exists(path):
                    os.remove(path)
            else:
                os.makedirs(os.path.dirname(path), exist_ok=True)
                with open(path, "wb") as f:
                    f.write(blob)
        if first:                                     # the zone is as it was: forget the bake
            shutil.rmtree(os.path.join(OUT, zone.folder, "original"), ignore_errors=True)
            os.remove(manifest_path(zone))

    try:
        for path, blob in writes.items():
            os.makedirs(os.path.dirname(path), exist_ok=True)
            with open(path, "wb") as f:
                f.write(blob)
        for path in deletes:
            os.remove(path)
        zone2 = relight.Zone(mz.DATA, zone.zdir, zone.deco_rel, zone.cnst_rel, zone.rows)
        problems, warnings = relight.check_zone(zone2, audit, {os.path.normcase(p_).lower() for p_ in writes})
    except BaseException:
        rollback()
        print("the bake stopped part-way; every file it touched is back as it was")
        raise
    if problems:
        rollback()
        print("CHECKS FAILED, the run was rolled back:")
        for p_ in problems[:20]:
            print("  " + p_)
        return 1
    for path in touched:
        man["current"][key_of(zone, path)] = sha(path)
    man["runs"].append({"when": datetime.datetime.now().isoformat(timespec="seconds"),
                        "settings": {k: (list(v) if isinstance(v, tuple) else v) for k, v in s.items()},
                        "report": report})
    save_manifest(zone, man)
    print("baked in %.0f s: %d files written, %d old atlases removed; checks pass (no entry the client "
          "could crash on)." % (time.time() - t0, len(writes), len(deletes)))
    for w_ in warnings[:5]:
        print("  note, in files this bake kept: %s" % w_)
    print("Next: python scripts/relight-zone.py shots --zone %s, then re-bake the VFS (scripts/pack.ps1)."
          % zone.folder)
    return 0


def cmd_verify(zone):
    man = load_manifest(zone)
    if man is None:
        print("%s has not been relit" % zone.folder)
        return 1
    changed = [k for k, h in man["current"].items() if sha(os.path.join(zone.zdir, k)) != h]
    ours = {os.path.normcase(os.path.join(zone.zdir, k)).lower() for k, h in man["current"].items() if h}
    problems, warnings = relight.check_zone(zone, audit, ours)
    print("%s: %d files from the last bake, %d changed since%s" % (
        zone.folder, len(man["current"]), len(changed), (": " + ", ".join(changed[:5])) if changed else ""))
    for p_ in problems[:20]:
        print("  " + p_)
    for w_ in warnings[:5]:
        print("  note, in files the bakes kept: %s" % w_)
    print("checks: %s" % ("pass" if not problems else "%d problems" % len(problems)))
    return 1 if changed or problems else 0


def cmd_restore(zone, force):
    man = load_manifest(zone)
    if man is None:
        raise SystemExit("%s has not been relit; nothing to restore" % zone.folder)
    changed = [k for k, h in man["current"].items() if sha(os.path.join(zone.zdir, k)) != h]
    if changed and not force:
        raise SystemExit("%d files changed since the last bake (%s); --force restores anyway"
                         % (len(changed), ", ".join(changed[:3])))
    now = ifo_hashes(zone)
    edited = sorted(k for k, h in man["ifo"].items() if now.get(k) != h)
    if edited and not force:
        raise SystemExit("the map's objects changed since the first bake (%s): its original lightmaps are "
                         "numbered for the old objects and could crash the client. Bake again instead "
                         "(--force restores anyway)" % ", ".join(edited[:5]))
    bdir = os.path.join(OUT, zone.folder, "original")
    for k, h in man["original"].items():
        path = os.path.join(zone.zdir, k)
        if h is None:
            if os.path.exists(path):
                os.remove(path)
        else:
            os.makedirs(os.path.dirname(path), exist_ok=True)
            shutil.copyfile(os.path.join(bdir, k), path)
    bad = [k for k, h in man["original"].items() if sha(os.path.join(zone.zdir, k)) != h]
    if bad:
        raise SystemExit("restore incomplete, the backups are kept in %s: %s" % (bdir, ", ".join(bad[:5])))
    shutil.rmtree(bdir, ignore_errors=True)
    os.remove(manifest_path(zone))
    print("%s: %d files back to their originals. Re-bake the VFS." % (zone.folder, len(man["original"])))
    zone2 = relight.Zone(mz.DATA, zone.zdir, zone.deco_rel, zone.cnst_rel, zone.rows)
    problems, warnings = relight.check_zone(zone2, audit, None)
    crash = [p_ for p_ in problems if p_.startswith(("CRASH", "RISK"))]
    if crash:
        print("WARNING: the original lightmaps have entries the client can crash on (%d), e.g. %s -- "
              "bake again, or fix them (scripts/audit-lightmap-index.py)" % (len(crash), crash[0]))
    return 0


def cmd_shots(zone, spots, label):
    g = types.SimpleNamespace(field=zone.field_f, x0=zone.x0, y0=zone.y0)
    if not spots:
        # the busiest chunks: the centre of their decorations
        busy = sorted(zone.chunks.items(), key=lambda kv: -len(kv[1]["ifo"].lump(ifo.OBJECT) or []))[:6]
        spots = []
        for slot, ch in busy:
            recs = ch["ifo"].lump(ifo.OBJECT) or []
            if recs:
                x = np.mean([r.pos[0] for r in recs]) + 520000
                y = np.mean([r.pos[1] for r in recs]) + 520000
                spots.append("%d,%d" % (x / 100.0, y / 100.0))
    views = mz.at_views(g, spots)
    out_dir = os.path.join(OUT, zone.folder, "shots", label)
    os.makedirs(out_dir, exist_ok=True)
    for f in os.listdir(out_dir):
        if f.endswith(".png") or f == "shots.txt":
            os.remove(os.path.join(out_dir, f))
    job = os.path.join(out_dir, "job.txt")
    with open(job, "w", encoding="utf-8", newline="\n") as f:
        f.write("# written by relight-zone.py shots %s\n" % zone.folder)
        f.write("zone %d\nout %s\nhide %s\nsettle 90\n" % (zone.rows[0], out_dir, mz.SHOT_HIDE))
        for name, eye, tgt in views:
            f.write("view %s %s\n" % (name, " ".join("%.2f" % v for v in list(eye) + list(tgt))))
    print("shots of zone %d (%s): %d views -> %s" % (zone.rows[0], zone.folder, len(views), out_dir))
    try:
        rc = subprocess.run([mz.EDITOR_EXE, "--shots", job], cwd=mz.DATA, timeout=600).returncode
    except subprocess.TimeoutExpired:
        raise SystemExit("the editor did not finish within 10 minutes (see data/Map Editor.log)")
    done = os.path.join(out_dir, "shots.txt")
    lines = open(done, encoding="utf-8").read().split() if os.path.exists(done) else []
    if rc != 0 or not lines or lines[-1] != "done":
        raise SystemExit("the editor stopped early (exit %d; see data/Map Editor.log)" % rc)
    names = [v[0] for v in views]
    sheet = mz.contact_sheet(out_dir, names, os.path.join(out_dir, "sheet.png"))
    print("  contact sheet %s" % sheet)
    for n in names:
        print("    %s" % os.path.join(out_dir, n + ".png"))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0],
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("settings")
    sub.add_parser("list")
    for name in ("show", "bake", "shots", "verify", "restore"):
        p = sub.add_parser(name)
        p.add_argument("--zone", required=True, help="LIST_ZONE row or map folder name")
        if name in ("show", "bake"):
            p.add_argument("--set", action="append", metavar="KEY=VALUE")
            p.add_argument("--settings", metavar="FILE")
        if name == "bake":
            p.add_argument("--save", action="store_true", help="keep these settings for the zone")
            p.add_argument("--dry-run", action="store_true", help="show what the bake would see; bake nothing")
            p.add_argument("--out", metavar="DIR", help="write the files here instead of data/")
        if name == "shots":
            p.add_argument("--at", action="append", metavar="X,Y", help="world metres, as the minimap prints them")
            p.add_argument("--label", default="latest")
        if name == "restore":
            p.add_argument("--force", action="store_true")
    a = ap.parse_args()
    if a.cmd == "settings":
        return cmd_settings()
    if a.cmd == "list":
        return cmd_list()
    zone = resolve(a.zone)
    if a.cmd == "show":
        return cmd_show(zone, a)
    if a.cmd == "bake":
        return cmd_bake(zone, a)
    if a.cmd == "shots":
        return cmd_shots(zone, a.at, a.label)
    if a.cmd == "verify":
        return cmd_verify(zone)
    return cmd_restore(zone, a.force)


if __name__ == "__main__":
    sys.exit(main())
