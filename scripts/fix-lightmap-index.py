"""Repair the object lightmap index entries (.lit) that crash-risk the client.

    python scripts/fix-lightmap-index.py --dry-run
    python scripts/fix-lightmap-index.py            # apply (backups first)
    python scripts/fix-lightmap-index.py --verify
    python scripts/fix-lightmap-index.py --restore

Found by scripts/audit-lightmap-index.py (2026-10-03). All three chunks
ship exactly like this in every reference client we own (139, 667, Evo,
Jrose, QQ-iROSE, RoseZA, ruff, titanRose, tsuki): retail bugs, left by
editing a map after its lightmaps were baked. The map editor keys .lit
entries by the record's ordinal and never renumbers them.

* JD04 (Desert of the Dead, zone 29) 32_34: 13 decoration records, 14
  entries. Entry 14 (baked for jdgrass002) belongs to a record deleted
  after the bake, so the client reads one int past the record array
  (`m_pObjectIndex[lump][13]`, heap garbage) and applies the entry to
  whatever object that names. Dropped.
* JZ01_1 (Goblin Cave B1, zone 31) 31_30: a nungcool05 was inserted at
  record 37 after the bake, so entries 37-39 belong to records 38-40: entry
  39 (tankwangtti01aa + tankwangtti02aa, 2 parts) lands on stone01 (1 part)
  and its part 1 indexes past the part arrays (`CObjFIXED::SetLightMap`).
  Renumbered; record 37 stays unlit, as in retail.
* JZ01_1 32_31: a fire and a road01 were inserted at records 51-52, so
  entries 51-54 belong to records 53-56: entry 51 (the 2-part tank) lands
  on fire (1 part). Renumbered; records 51, 52 and 57 stay unlit.

Every entry's tga name records the mesh it was baked for
(`<mesh>_Object_<ordinal>_<part>_<x_y>_LightingMap.tga`); a renumbering is
applied only if each moved entry's baked meshes match its new record part
by part, and a drop only if the ordinal is past the lump. Only the listed
chunks are touched (`FIXES`); anything else the audit reports must be
reviewed and added here by hand.

Backups and manifest: build/lightmap-index/ (never beside the file: pack.rs
would bake a .bak into the VFS). The .lit files are client-only: re-bake
the VFS afterwards, no server restart.
"""

import argparse
import hashlib
import importlib.util
import json
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
_spec = importlib.util.spec_from_file_location("audit_lightmap_index", os.path.join(HERE, "audit-lightmap-index.py"))
au = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(au)
mz, ifo, lit, catalogue = au.mz, au.ifo, au.lit, au.catalogue

BACKUP = os.path.join(mz.REPO, "build", "lightmap-index")
MANIFEST = os.path.join(BACKUP, "manifest.json")
# (map folder, chunk, decoration ZSC): {"renumber": {old: new}, "drop": [ordinal]}
FIXES = {
    ("JD04", "32_34", r"3DDATA\JUNON\LIST_DECO_JD.ZSC"): {"drop": [14]},
    ("JZ01_1", "31_30", r"3DDATA\JUNON\LIST_DECO_JZ.ZSC"): {"renumber": {37: 38, 38: 39, 39: 40}},
    ("JZ01_1", "32_31", r"3DDATA\JUNON\LIST_DECO_JZ.ZSC"): {"renumber": {51: 53, 52: 54, 53: 55, 54: 56}},
}


def paths(folder, stem):
    zdir = mz.P(r"3DDATA\MAPS\JUNON" + "\\" + folder)
    sub = os.path.join(zdir, au.listing(zdir)[stem.lower()])
    lm = os.path.join(sub, au.listing(sub)["lightmap"])
    return os.path.join(zdir, au.listing(zdir)[stem.lower() + ".ifo"]), \
        os.path.join(lm, au.listing(lm)["objectlightmapdata.lit"])


def sha(path):
    return hashlib.sha256(open(path, "rb").read()).hexdigest()


def plan(folder, stem, zsc, fix):
    """The repaired .lit bytes, after checking every move against the baked
    mesh names; raises on anything unexpected."""
    ifo_path, lit_path = paths(folder, stem)
    recs = ifo.parse(open(ifo_path, "rb").read()).lump(ifo.OBJECT)
    meshes, objs = catalogue.read_zsc(mz.P(zsc))
    L = lit.parse_lit(open(lit_path, "rb").read())
    by = {o.obj_index: o for o in L.objects}
    for d in fix.get("drop", []):
        if d not in by or d <= len(recs):
            raise SystemExit("%s %s: entry %d is not an entry past the %d records" % (folder, stem, d, len(recs)))
    ren = fix.get("renumber", {})
    taken = set(by) - set(ren) - set(fix.get("drop", []))
    for old, new in ren.items():
        if old not in by or not 1 <= new <= len(recs) or new in taken:
            raise SystemExit("%s %s: cannot move entry %d to record %d" % (folder, stem, old, new))
        parts = objs[recs[new - 1].obj_id]["parts"]
        for pt in by[old].parts:
            want = au.baked_mesh(pt.tga_name)
            have = au.mesh_stem(meshes, parts[pt.part_index]) if pt.part_index < len(parts) else None
            if want != have:
                raise SystemExit("%s %s: entry %d part %d was baked for %s, record %d part is %s"
                                 % (folder, stem, old, pt.part_index, want, new, have))
        taken.add(new)
    objects = []
    for o in L.objects:
        if o.obj_index in fix.get("drop", []):
            continue
        objects.append(lit.LitObject(obj_index=ren.get(o.obj_index, o.obj_index), parts=o.parts))
    objects.sort(key=lambda o: o.obj_index)
    L.objects = objects
    return lit_path, lit.build_lit(L)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    g = ap.add_mutually_exclusive_group()
    g.add_argument("--dry-run", action="store_true")
    g.add_argument("--verify", action="store_true")
    g.add_argument("--restore", action="store_true")
    a = ap.parse_args()

    if a.restore:
        if not os.path.exists(MANIFEST):
            raise SystemExit("nothing to restore (%s missing)" % MANIFEST)
        man = json.load(open(MANIFEST, encoding="utf-8"))
        for e in man["files"]:
            if sha(e["path"]) != e["after"]:
                raise SystemExit("%s changed since the fix; not restoring it" % e["path"])
        for e in man["files"]:
            shutil.copyfile(os.path.join(BACKUP, e["backup"]), e["path"])
            print("restored %s" % e["path"])
        os.remove(MANIFEST)
        return 0

    plans = []
    for (folder, stem, zsc), fix in FIXES.items():
        lit_path = paths(folder, stem)[1]
        if a.verify or (os.path.exists(MANIFEST) and not a.dry_run):
            plans.append((folder, stem, lit_path, None))
            continue
        plans.append((folder, stem) + plan(folder, stem, zsc, fix))
    if a.verify:
        rc = 0
        for folder, stem, lit_path, _ in plans:
            if not os.path.exists(MANIFEST):
                print("not applied")
                return 1
            want = {e["path"]: e["after"] for e in json.load(open(MANIFEST, encoding="utf-8"))["files"]}
            ok = sha(lit_path) == want.get(lit_path)
            rc |= not ok
            print("%-4s %s %s" % ("ok" if ok else "FAIL", folder, stem))
        rows = [29, 31]
        print("audit of zones %s:" % rows)
        sys.argv = [sys.argv[0]] + sum((["--zone", str(r)] for r in rows), [])
        rc |= au.main()
        return rc
    if os.path.exists(MANIFEST):
        print("already applied (%s); --verify or --restore" % MANIFEST)
        return 0
    for folder, stem, lit_path, data in plans:
        print("%s %s: %s -> %d bytes (was %d)" % (folder, stem, os.path.basename(lit_path), len(data),
                                                  os.path.getsize(lit_path)))
    if a.dry_run:
        print("dry run: nothing written")
        return 0
    os.makedirs(BACKUP, exist_ok=True)
    files = []
    for folder, stem, lit_path, data in plans:
        name = "%s_%s_%s" % (folder, stem, os.path.basename(lit_path))
        shutil.copyfile(lit_path, os.path.join(BACKUP, name))
        before = sha(lit_path)
        with open(lit_path, "wb") as f:
            f.write(data)
        files.append({"path": lit_path, "backup": name, "before": before, "after": sha(lit_path)})
    with open(MANIFEST, "w", encoding="utf-8", newline="\n") as f:
        json.dump({"files": files}, f, indent=1)
    print("applied; backups in %s. Re-bake the VFS (client-only data)." % BACKUP)
    return 0


if __name__ == "__main__":
    sys.exit(main())
