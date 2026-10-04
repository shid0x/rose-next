"""Fill the map-object ZSC entries a zone places but our table leaves empty.

    python scripts/fill-zsc-gaps.py --dry-run
    python scripts/fill-zsc-gaps.py            # apply (backups first)
    python scripts/fill-zsc-gaps.py --verify
    python scripts/fill-zsc-gaps.py --restore
    python scripts/fill-zsc-gaps.py --zone N   # another zone (repeatable)

Desert of the Dead (zone 29, JD04) showed bare sand with shadows on it: the
map places 53 decoration ids and 5 building ids, and 32 + 5 of them were
empty or past the end of our `LIST_DECO_JD.ZSC` (249 objects) /
`LIST_CNST_JD.ZSC` (37). `CMAP::AddObject` creates nothing for an object
with no parts, so 561 of the zone's 1006 decorations and all 6 buildings
(the church, the market, three houses) were missing while their baked
shadows stayed in the terrain lightmap. Found by
scripts/audit-lightmap-index.py (2026-10-03).

Our two tables are an older cut of the same table, not a different one:
every object we do fill is identical (mesh, texture, material flags,
transform, parent, collision) to the ruff, 139, Evo, Jrose and QQ-iROSE
dumps, and our JD04 IFOs place the same ids at the same positions as
theirs. RoseZA, 667, titanRose and tsuki diverge on a few rows (titan has
other buildings at CNST 37-41) and are not used. Every mesh, texture and
part animation the missing objects need was already in our data/; only the
table entries were missing. The source is ruff; the other four are
witnesses, and an object is taken only if every witness that has it agrees
-- a disagreement stops the run.

Only empty slots are written, and every object we already had keeps its
bytes, so the other zones on these tables (JD01-JD03 on both, Goblin Cave
B1-B3 on the building table) are untouched. Ids past our end are appended,
padding any unused id in between with an empty object so ids still line up.

Effects: four decorations (196 the cave-in's night lights, 240 the spider,
244 the burnt tank, 250 the campfire) carry dummy points that index the
table's effect list, which our decoration table did not have. The client
reads that index with no bounds check (`pEftKEY[nListIDX]`, IO_Model.h), so
each referenced effect is appended to our list and the index remapped --
never copied verbatim. Seven of the eight .eft files were missing here and
are copied with their chain (.ptl, particle textures, and the night light's
.ZMS/.ZMO mesh effect); `import-oro.py`'s ABS_ASSET_RE learnt `.zmo` for it.

Backups and manifest: build/zsc-gaps/ (never beside the file: pack.rs
would bake a .bak into the VFS). --restore puts the tables back and deletes
the files this run copied. ZSCs are client-only data: re-bake the VFS, no
server restart. Run scripts/audit-lightmap-index.py --zone N afterwards:
the zone's lightmap entries now land on real objects, and an entry baked
for another mesh would show there.
"""

import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import struct
import sys
from collections import defaultdict

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
DATA = os.path.join(REPO, "data")
sys.path.insert(0, HERE)
_spec = importlib.util.spec_from_file_location("import_oro", os.path.join(HERE, "import-oro.py"))
ro = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(ro)
from mapgen import catalogue, ifo, tiles  # noqa: E402

TESTCLIENTS = r"C:\Users\Thomas\Desktop\Testclients"
SOURCE = os.path.join(TESTCLIENTS, "ruff", "extracted data")
WITNESSES = {
    "139": os.path.join(TESTCLIENTS, "139", "extracted data"),
    "evo": os.path.join(TESTCLIENTS, "Evo 137 client", "Extracted data"),
    "jrose": os.path.join(TESTCLIENTS, "Jrose"),
    "qq": os.path.join(TESTCLIENTS, "QQ-iROSE Online", "QQiroseData"),
}
ZONES = [29]
BACKUP = os.path.join(REPO, "build", "zsc-gaps")
MANIFEST = os.path.join(BACKUP, "manifest.json")
COL_ZON, COL_DECO, COL_CNST = 1, 11, 12
EMPTY_OBJECT = b"\0" * 12 + struct.pack("<H", 0)


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def resolve(root, rel):
    """Case-insensitive data-relative lookup; None if absent."""
    cur = root
    for part in rel.replace("\\", "/").split("/"):
        if not part:
            continue
        if not os.path.isdir(cur):
            return None
        hit = [e for e in os.listdir(cur) if e.lower() == part.lower()]
        if not hit:
            return None
        cur = os.path.join(cur, hit[0])
    return cur


def zone_targets(zones):
    """{zsc rel: {lump, ids used, zones}} for the given LIST_ZONE rows."""
    rows = tiles.read_stb_cells(os.path.join(DATA, "3DDATA", "STB", "LIST_ZONE.STB"))
    out = {}
    for z in zones:
        row = rows[z]
        mapdir = resolve(DATA, os.path.dirname(row[COL_ZON].replace("\\", "/")))
        if not mapdir:
            raise SystemExit("zone %d: no map folder for %s" % (z, row[COL_ZON]))
        used = {ifo.OBJECT: set(), ifo.CNST: set()}
        for name in os.listdir(mapdir):
            if name.lower().endswith(".ifo"):
                parsed = ifo.parse(open(os.path.join(mapdir, name), "rb").read())
                for lump in used:
                    used[lump] |= {r.obj_id for r in (parsed.lump(lump) or [])}
        for col, lump in ((COL_DECO, ifo.OBJECT), (COL_CNST, ifo.CNST)):
            rel = row[col].replace("\\\\", "\\").replace("\\", "/")
            t = out.setdefault(rel.lower(), {"rel": rel, "ids": set(), "zones": []})
            t["ids"] |= used[lump]
            t["zones"].append(z)
    return out


def semantic(meshes, objs, i):
    """What the client builds from object i: per part mesh, texture, flags, transform, parent, collision."""
    if i >= len(objs):
        return None
    return tuple((ro.norm(meshes[p["mesh"]]), ro.norm(p["texture"]), tuple(sorted(p["material"].items())),
                  tuple(round(v, 3) for v in p["pos"]), tuple(round(v, 4) for v in p["rot"]),
                  tuple(round(v, 4) for v in p["scale"]), p["parent"], p["collision"])
                 for p in objs[i]["parts"])


def dummy_effects(z, i):
    """Object i's dummy points as (effect path or None, effect type, props)."""
    out = []
    for a, props in z.objects[i][2]:
        idx, typ = struct.unpack("<hh", a)
        out.append((ro.norm(z.effects[idx]) if 0 <= idx < len(z.effects) else None, typ, props))
    return out


def object_slices(z):
    """Each object's original bytes, checked to re-serialise exactly."""
    out, o = [], z.objcnt_pos + 2
    for obj in z.objects:
        blob = z.object_bytes(*obj)
        if z.d[o:o + len(blob)] != blob:
            raise SystemExit("%s: object %d does not re-serialise; refusing to rewrite" % (z.path, len(out)))
        out.append(blob)
        o += len(blob)
    if o != z.obj_end or z.obj_end != len(z.d):
        raise SystemExit("%s: %d trailing bytes after the objects" % (z.path, len(z.d) - z.obj_end))
    return out


def plan(t):
    """(new bytes, filled ids, files the objects need) for one table; None if nothing to fill."""
    ours_path = resolve(DATA, t["rel"])
    ours = ro.Zsc(ours_path)
    gaps = sorted(i for i in t["ids"] if i >= len(ours.objects) or not ours.objects[i][1])
    if not gaps:
        return None
    src = ro.Zsc(resolve(SOURCE, t["rel"]))
    s_meshes, s_objs = catalogue.read_zsc(src.path)
    wit = {}
    for name, root in WITNESSES.items():
        p = resolve(root, t["rel"])
        if p:
            wit[name] = (ro.Zsc(p), catalogue.read_zsc(p))
    problems = []
    for i in gaps:
        if i >= len(src.objects) or not src.objects[i][1]:
            problems.append("object %d is empty in the source too" % i)
            continue
        want, want_fx = semantic(s_meshes, s_objs, i), dummy_effects(src, i)
        agree = 0
        for name, (wz, (wm, wo)) in wit.items():
            if i >= len(wo):
                continue
            if semantic(wm, wo, i) != want or dummy_effects(wz, i) != want_fx:
                problems.append("object %d: %s disagrees with the source" % (i, name))
            else:
                agree += 1
        if not agree:
            problems.append("object %d: no witness has it" % i)
    if problems:
        raise SystemExit("%s:\n  %s" % (t["rel"], "\n  ".join(problems)))

    mesh_idx = {ro.norm(m): k for k, m in enumerate(ours.meshes)}
    mat_idx = {ro.norm(p): k for k, (p, _) in enumerate(ours.materials)}
    eft_idx = {ro.norm(e): k for k, e in enumerate(ours.effects)}
    new_meshes, new_mats, new_efts = [], [], []
    files, efts = set(), set()
    filled = {}
    for i in gaps:
        cyl, sparts, sdummies, sbb = src.objects[i]
        parts = []
        for mid, tid, props in sparts:
            mk, tk = ro.norm(src.meshes[mid]), ro.norm(src.materials[tid][0])
            if mk not in mesh_idx:
                mesh_idx[mk] = len(ours.meshes) + len(new_meshes)
                new_meshes.append(src.meshes[mid])
            if tk not in mat_idx:
                mat_idx[tk] = len(ours.materials) + len(new_mats)
                new_mats.append(src.materials[tid])
            files.add(src.meshes[mid].decode("latin-1"))
            files.add(src.materials[tid][0].decode("latin-1"))
            files |= set(ro.prop_paths(props))
            parts.append((mesh_idx[mk], mat_idx[tk], props))
        dummies = []
        for a, props in sdummies:
            idx, typ = struct.unpack("<hh", a)
            if idx >= 0:
                path = src.effects[idx]
                if ro.norm(path) not in eft_idx:
                    eft_idx[ro.norm(path)] = len(ours.effects) + len(new_efts)
                    new_efts.append(path)
                if path[:2].upper() == b"3D":             # not a light name
                    efts.add(path.decode("latin-1"))
                a = struct.pack("<hh", eft_idx[ro.norm(path)], typ)
            dummies.append((a, props))
        filled[i] = ours.object_bytes(cyl, parts, dummies, sbb)
    files |= ro.effect_chain(sorted(efts), SOURCE)

    body = object_slices(ours)
    for i, blob in filled.items():
        while len(body) <= i:
            body.append(EMPTY_OBJECT)
        body[i] = blob
    data = b"".join([
        struct.pack("<H", len(ours.meshes) + len(new_meshes)), ours.d[2:ours.mesh_end],
        b"".join(m + b"\0" for m in new_meshes),
        struct.pack("<H", len(ours.materials) + len(new_mats)), ours.d[ours.mesh_end + 2:ours.mat_end],
        b"".join(p + b"\0" + fl for p, fl in new_mats),
        struct.pack("<H", len(ours.effects) + len(new_efts)), ours.d[ours.mat_end + 2:ours.objcnt_pos],
        b"".join(e + b"\0" for e in new_efts),
        struct.pack("<H", len(body)), b"".join(body)])
    print("%s (zones %s): %d objects -> %d; filled %d: %s" % (
        t["rel"], t["zones"], len(ours.objects), len(body), len(gaps), gaps))
    print("    +%d meshes, +%d materials, +%d effects" % (len(new_meshes), len(new_mats), len(new_efts)))
    return {"path": ours_path, "data": data, "filled": gaps, "files": files}


def copy_plan(files):
    """(rel, source path) for every needed file we lack; exits if the source lacks one too."""
    need, missing = [], []
    for rel in sorted(files, key=str.lower):
        if resolve(DATA, rel):
            continue
        s = resolve(SOURCE, rel)
        if not s:
            missing.append(rel)
        else:
            need.append((rel, s))
    if missing:
        raise SystemExit("needed but in neither data/ nor the source:\n  " + "\n  ".join(missing))
    return need


def dest_path(rel):
    """Where a copied file lands: the deepest existing folder keeps its casing."""
    parts = [p for p in rel.replace("\\", "/").split("/") if p]
    cur = DATA
    for k, part in enumerate(parts[:-1]):
        hit = resolve(cur, part)
        cur = hit if hit else os.path.join(cur, *parts[k:-1])
        if not hit:
            break
    return os.path.join(cur, parts[-1])


def verify(targets):
    if not os.path.exists(MANIFEST):
        print("not applied")
        return 1
    man = json.load(open(MANIFEST, encoding="utf-8"))
    rc = 0
    for t in targets.values():
        ours = ro.Zsc(resolve(DATA, t["rel"]))
        meshes, objs = catalogue.read_zsc(ours.path)
        empty = sorted(i for i in t["ids"] if i >= len(ours.objects) or not ours.objects[i][1])
        rc |= bool(empty)
        print("%-4s %s: %d ids placed, %d empty%s" % ("ok" if not empty else "FAIL", t["rel"], len(t["ids"]),
                                                    len(empty), " %s" % empty if empty else ""))
        entry = next((e for e in man["tables"] if e["rel"].lower() == t["rel"].lower()), None)
        if entry is None:
            continue
        old = ro.Zsc(os.path.join(BACKUP, entry["backup"]))
        kept = object_slices(old)
        now = object_slices(ours)
        changed = [i for i in range(len(kept)) if i not in entry["filled"] and kept[i] != now[i]]
        src = ro.Zsc(resolve(SOURCE, t["rel"]))
        s_meshes, s_objs = catalogue.read_zsc(src.path)
        wrong = [i for i in entry["filled"] if semantic(meshes, objs, i) != semantic(s_meshes, s_objs, i)
                 or dummy_effects(ours, i) != dummy_effects(src, i)]
        rc |= bool(changed or wrong)
        print("%-4s   earlier objects unchanged%s; filled objects match the source%s" % (
            "ok" if not (changed or wrong) else "FAIL", " (CHANGED %s)" % changed if changed else "",
            " (DIFFER %s)" % wrong if wrong else ""))
        refs = {r for r in ro.zsc_asset_refs(ours)}
        refs |= ro.effect_chain(sorted(e.decode("latin-1") for e in ours.effects if e[:2].upper() == b"3D"), DATA)
        absent = sorted(r for r in refs if not resolve(DATA, r))
        rc |= bool(absent)
        print("%-4s   %d files the table names are present%s" % (
            "ok" if not absent else "FAIL", len(refs) - len(absent), "; MISSING %s" % absent if absent else ""))
    return rc


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--dry-run", action="store_true")
    g.add_argument("--verify", action="store_true")
    g.add_argument("--restore", action="store_true")
    ap.add_argument("--zone", type=int, action="append", help="LIST_ZONE row (repeatable); default %s" % ZONES)
    a = ap.parse_args()

    if a.restore:
        if not os.path.exists(MANIFEST):
            raise SystemExit("nothing to restore (%s missing)" % MANIFEST)
        man = json.load(open(MANIFEST, encoding="utf-8"))
        for e in man["tables"]:
            if sha(e["path"]) != e["after"]:
                raise SystemExit("%s changed since the fill; not restoring it" % e["path"])
        for e in man["tables"]:
            shutil.copyfile(os.path.join(BACKUP, e["backup"]), e["path"])
            print("restored %s" % e["path"])
        for e in man["copied"]:
            if os.path.exists(e["path"]) and sha(e["path"]) == e["sha"]:
                os.remove(e["path"])
                print("removed %s" % e["path"])
            else:
                print("left %s (changed or gone)" % e["path"])
        os.remove(MANIFEST)
        return 0

    targets = zone_targets(a.zone or ZONES)
    if a.verify:
        return verify(targets)
    if os.path.exists(MANIFEST) and not a.dry_run:
        print("already applied (%s); --verify or --restore" % MANIFEST)
        return 0

    plans = [p for p in (plan(t) for t in targets.values()) if p]
    if not plans:
        print("nothing to fill")
        return 0
    copies = copy_plan(set().union(*(p["files"] for p in plans)))
    print("files to copy from %s: %d" % (SOURCE, len(copies)))
    for rel, _ in copies:
        print("    %s" % rel)
    if a.dry_run:
        print("dry run: nothing written")
        return 0

    os.makedirs(BACKUP, exist_ok=True)
    tables, copied = [], []
    for p in plans:
        name = os.path.basename(p["path"])
        shutil.copyfile(p["path"], os.path.join(BACKUP, name))
        before = sha(p["path"])
        with open(p["path"], "wb") as f:
            f.write(p["data"])
        tables.append({"rel": os.path.relpath(p["path"], DATA).replace("\\", "/"), "path": p["path"],
                       "backup": name, "before": before, "after": sha(p["path"]), "filled": p["filled"]})
    for rel, s in copies:
        d = dest_path(rel)
        os.makedirs(os.path.dirname(d), exist_ok=True)
        shutil.copyfile(s, d)
        copied.append({"rel": rel, "path": d, "sha": sha(d)})
    with open(MANIFEST, "w", encoding="utf-8") as f:
        json.dump({"source": SOURCE, "tables": tables, "copied": copied}, f, indent=1)
    print("written; manifest %s" % MANIFEST)
    return verify(targets)


if __name__ == "__main__":
    sys.exit(main())
