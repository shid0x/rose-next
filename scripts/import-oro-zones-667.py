"""Import the three Oro zones the RoseZA import did not have, from the 667 client.

`C:\\Users\\Thomas\\Desktop\\Testclients\\667` is a much later evolution-era client
than the RoseZA dump scripts/import-oro.py pulled from. Its Oro has three zones
ours lacks:

    COLOSSEUM   "Golden Colosseum"    6 map cells,  21 MB, no monsters, no NPCs
    ODGR01      "The Golden Ring"    30 map cells,  44 MB
    ODFS01      "Fossil Sanctuary"   30 map cells,  80 MB

Unlike the RoseZA import this is a *drop-in*, not a port, and that was worth
verifying rather than assuming. Measured before writing anything:

  * **Art.** All three zones place objects out of `LIST_DECO_ODD` / `LIST_CNST_ODD`,
    which we already have -- and our copies define exactly as many objects as the
    667's (112 / 16). Every object index the zones use is in range: COLOSSEUM deco
    5-63 and cnst 9-14, ODGR01 deco 1-101 and cnst 1-15, ODFS01 deco 1-111 and cnst
    9-15. So no meshes, materials or ZSC rows need importing.
  * **Terrain tiles.** Each zone's .ZON names 113 tile textures and every one
    resolves in our data -- they all use `TERRAIN/TILES/ORO/ORO/`. The 667 also
    ships a `TILES/ORO/ODG/` set we lack, but nothing here references it, so it is
    deliberately not imported.
  * **Revive points.** All three .ZONs contain both a `start` and a `restore` event
    position, so col 3 can just say "restore". This is the trap that bit the RoseZA
    import, where TOWN's is spelled `restor` and two others say `respawn`; a name
    that does not resolve leaves the zone with a NULL revive point.

Zone numbers deliberately match the 667's own (14 / 83 / 85) rather than being
packed into our free rows. Zone ids are stored as *literals* by warps and QSD
REWD_007 rewards, so keeping upstream numbering means anything later imported that
references these zones lands correctly without a rewrite -- the same reasoning that
kept Oro at 71-82. Our row 14 happens to be free, and 83/85 are appends.

Monsters are NOT imported, and the copied .IFOs get their MOB / REGEN / WARP /
EVENT_OBJECT lumps emptied on the way in (count = 0, lump table untouched), exactly
as import-oro.py's stage 1 does. This is not tidiness -- it is required. ODGR01
spawns 24 distinct monsters and ODFS01 20, and **eleven of those ids point at blank
rows in our LIST_NPC**:

    2181 Cactus (NPC)                     2279-2281 Golden Scarab / Warrior / King
    2284-2287 Darkened Scorpio / Stingertail / Crowned Asper / Hooded Asper
    2274-2276 Armastyx / Stealthed / Camouflaged King

Spawning a blank row is undefined, so the zones ship walkable and empty. Importing
those eleven needs their models, AI and STB rows, and three of them (levels 245,
247, 255) sit above our 240 character cap and would need the balance passes applied
-- see scripts/rebalance-oro-bosses.py and the level-gate note in CLAUDE.md.

Also not done here: the way in. Nothing warps to these zones yet; reach them with a
GM warp until an entrance is decided.

Idempotent -- re-running detects what is already in place and does nothing.

Usage:
    python scripts/import-oro-zones-667.py --dry-run
    python scripts/import-oro-zones-667.py
    python scripts/import-oro-zones-667.py --verify
"""
import argparse
import importlib.util
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
DEFAULT_SRC = r"C:\Users\Thomas\Desktop\Testclients\667\extracted data"

# (zone row, folder, clean name, STL key). Rows and keys are the 667's own -- see
# the docstring on why upstream numbering is kept. LZON084 already exists in our
# STL ("The Golden Ring", shared with rows 75-77); LZON014 and LZON087 are new.
ZONES = [
    (14, "COLOSSEUM", "Golden Colosseum", "LZON014"),
    (83, "ODGR01", "The Golden Ring", "LZON084"),
    (85, "ODFS01", "Fossil Sanctuary", "LZON087"),
]
MAX_ZONE_ROW = max(r for r, _, _, _ in ZONES)
ZONE_COPY_COLS = 31          # cols 0..30 align between the two schemas; 31+ diverge
REVIVE_EVENT = b"restore"    # verified present in all three .ZONs

# --- state needed to undo an import exactly ----------------------------------
# --revert has to rebuild what the import overwrote, and the two tables were not
# simply appended to: row 14 already existed carrying one orphan cell (c19='1',
# no name and no .zon path, so never a functional zone), and only rows 83-85 were
# genuine appends. LZON084 already existed and is shared with rows 75-77, so it
# must survive; only LZON014 and LZON087 were added here.
#
# The sizes are the pre-import files, and --revert asserts against them: an exact
# byte-size match is a strong check that the header row-label truncation and the
# STL key removal both landed correctly.
ORIGINAL_ZONE_ROWS = 83
ORIGINAL_STL_KEYS = 45
ORIGINAL_ROW14 = {19: b"1"}          # every other column was blank
REVERT_STL_KEYS = ("LZON014", "LZON087")
ORIGINAL_SIZES = {"LIST_ZONE.STB": 21404, "LIST_ZONE_S.STL": 5565}


def load_importer():
    """Reuse import-oro.py's STB/STL/IFO codecs rather than reimplementing them."""
    spec = importlib.util.spec_from_file_location(
        "import_oro", os.path.join(HERE, "import-oro.py"))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, ["import-oro.py", "--help"]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    return mod


def copy_maps(m, ours, src, dry):
    """Copy the three map trees, emptying the lumps stage 1 must not carry."""
    copied = emptied = 0
    total = 0
    for _row, folder, _name, _key in ZONES:
        sdir = os.path.join(src, "3DDATA", "MAPS", "ORO", folder)
        ddir = os.path.join(ours, "3DDATA", "MAPS", "ORO", folder)
        if not os.path.isdir(sdir):
            sys.exit(f"source zone missing: {sdir}")
        for base, _dirs, files in os.walk(sdir):
            rel = os.path.relpath(base, sdir)
            dbase = os.path.join(ddir, rel) if rel != "." else ddir
            for f in files:
                sp = os.path.join(base, f)
                dp = os.path.join(dbase, f)
                if os.path.exists(dp):
                    continue
                if f.upper().endswith(".IFO"):
                    buf, bounds = m.read_ifo(sp)
                    repl = {}
                    for lt in m.LUMPS_STAGE1_EMPTY:
                        off, end = m.lump_block(bounds, lt)
                        if off is None or buf[off:off + 4] == b"\0\0\0\0":
                            continue
                        _objs, trailing = m.read_lump(buf, bounds, lt)
                        repl[lt] = m.build_object_lump([], trailing)
                    blob = m.build_ifo(bounds, buf, repl) if repl else buf
                    emptied += 1 if repl else 0
                    if not dry:
                        os.makedirs(dbase, exist_ok=True)
                        with open(dp, "wb") as fh:
                            fh.write(blob)
                else:
                    if not dry:
                        os.makedirs(dbase, exist_ok=True)
                        shutil.copyfile(sp, dp)
                copied += 1
                total += os.path.getsize(sp)
    print(f"    {'map files':26s} {copied:5d} new files  {total / 1048576:7.2f} MB"
          f"  ({emptied} .IFOs had spawn/warp lumps emptied)")
    return copied


def write_zone_rows(m, ours, src, dry):
    src_zone = m.Stb(os.path.join(src, "3DDATA", "STB", "LIST_ZONE.STB"))
    zstb = m.Stb(os.path.join(ours, "3DDATA", "STB", "LIST_ZONE.STB"))
    added = zstb.grow_to(MAX_ZONE_ROW + 1, labels={r: n.encode("latin-1")
                                                   for r, _, n, _ in ZONES})
    changed = 0
    for row, folder, name, key in ZONES:
        src_row = None
        for i in range(1, src_zone.rows):
            p = src_zone.get(i, 1).decode("latin-1", "replace").upper()
            if f"\\{folder}\\".upper() in p.replace("/", "\\"):
                src_row = i
                break
        if src_row is None:
            sys.exit(f"could not find {folder} in the source LIST_ZONE.STB")
        before = list(zstb.d[row])
        for c in range(ZONE_COPY_COLS):
            zstb.set(row, c, src_zone.get(src_row, c))
        zstb.set(row, 0, name.encode("latin-1"))   # drop the "(CODE) " prefix
        zstb.set(row, 3, REVIVE_EVENT)
        zstb.set(row, 26, key.encode("latin-1"))
        for c, v in zip(m.ZONE_TRIGGER_COLS, m.ZONE_TRIGGERS):
            zstb.set(row, c, v)                    # our own Oro entry trigger
        if zstb.d[row] != before:
            changed += 1
    print(f"    {'LIST_ZONE.STB':26s} +{added} rows (now {zstb.rows}), "
          f"{changed} zone rows written")
    zstb.save(dry)
    return changed


def write_zone_names(m, ours, dry):
    zstl = m.Stl(os.path.join(ours, "3DDATA", "STB", "LIST_ZONE_S.STL"))
    n = 0
    for _row, _folder, name, key in ZONES:
        if zstl.has(key):
            continue
        zstl.append(key, int(key[4:]), name)
        n += 1
    print(f"    {'LIST_ZONE_S.STL':26s} +{n} keys (now {len(zstl.keys)})")
    if n and not dry:
        zstl.save(dry)
    return n


def verify(m, ours):
    bad = []
    zstb = m.Stb(os.path.join(ours, "3DDATA", "STB", "LIST_ZONE.STB"))
    zstl = m.Stl(os.path.join(ours, "3DDATA", "STB", "LIST_ZONE_S.STL"))
    for row, folder, name, key in ZONES:
        d = os.path.join(ours, "3DDATA", "MAPS", "ORO", folder)
        nhim = len([f for f in os.listdir(d)
                    if f.upper().endswith(".HIM")]) if os.path.isdir(d) else 0
        got = zstb.get(row, 0).decode("latin-1", "replace").strip()
        zon = zstb.get(row, 1).decode("latin-1", "replace").upper()
        ok = (nhim > 0 and got == name and folder.upper() in zon.replace("/", "\\")
              and zstl.has(key))
        print(f"    {folder:12s} row {row:3d}  {nhim:2d} map cells  "
              f"name={got!r:26s} stl={'yes' if zstl.has(key) else 'NO':3s}  "
              f"{'OK' if ok else 'FAILED'}")
        if not ok:
            bad.append(folder)
    return bad


def revert(m, ours, dry):
    """Undo the import: drop the map trees, the zone rows and the two STL keys."""
    import struct

    removed = 0
    freed = 0
    for _row, folder, _name, _key in ZONES:
        d = os.path.join(ours, "3DDATA", "MAPS", "ORO", folder)
        if not os.path.isdir(d):
            continue
        for base, _dirs, files in os.walk(d):
            for f in files:
                freed += os.path.getsize(os.path.join(base, f))
                removed += 1
        if not dry:
            shutil.rmtree(d)
    print(f"    {'map files':26s} {removed:5d} removed  {freed / 1048576:7.2f} MB")

    # --- zone rows. Clear ours, restore row 14's orphan cell, then truncate the
    # appended rows *and* their row labels, which live at the end of the header.
    zp = os.path.join(ours, "3DDATA", "STB", "LIST_ZONE.STB")
    z = m.Stb(zp)
    if z.rows > ORIGINAL_ZONE_ROWS:
        for row, _f, _n, _k in ZONES:
            for c in range(z.cols):
                z.set(row, c, ORIGINAL_ROW14.get(c, b"") if row == 14 else b"")
        offs, o = [], 0
        while o < len(z.header):
            n, = struct.unpack_from("<H", z.header, o)
            offs.append(o)
            o += 2 + n
        drop = z.rows - ORIGINAL_ZONE_ROWS
        z.header = z.header[:offs[len(offs) - drop]]
        z.d = z.d[:ORIGINAL_ZONE_ROWS]
        z.rows = ORIGINAL_ZONE_ROWS
        blob = z.to_bytes()
        want = ORIGINAL_SIZES["LIST_ZONE.STB"]
        if len(blob) != want:
            sys.exit(f"refusing to write LIST_ZONE.STB: rebuilt {len(blob)} bytes, "
                     f"expected {want} -- reverting would corrupt the table")
        print(f"    {'LIST_ZONE.STB':26s} {z.rows} rows, {len(blob)} bytes (matches pre-import)")
        if not dry:
            with open(zp, "wb") as fh:
                fh.write(blob)
    else:
        print(f"    {'LIST_ZONE.STB':26s} already at {z.rows} rows -- nothing to do")

    # --- zone names. Only the two keys this script appended; LZON084 predates it.
    sp = os.path.join(ours, "3DDATA", "STB", "LIST_ZONE_S.STL")
    s = m.Stl(sp)
    drop_idx = [i for i, (k, _) in enumerate(s.keys)
                if k.decode("latin-1") in REVERT_STL_KEYS]
    if drop_idx:
        for i in sorted(drop_idx, reverse=True):
            s.keys.pop(i)
            for rows in s.langs:
                rows.pop(i)
        blob = s.to_bytes()
        want = ORIGINAL_SIZES["LIST_ZONE_S.STL"]
        if len(blob) != want:
            sys.exit(f"refusing to write LIST_ZONE_S.STL: rebuilt {len(blob)} bytes, "
                     f"expected {want}")
        print(f"    {'LIST_ZONE_S.STL':26s} {len(s.keys)} keys, {len(blob)} bytes "
              f"(matches pre-import)")
        if not dry:
            with open(sp, "wb") as fh:
                fh.write(blob)
    else:
        print(f"    {'LIST_ZONE_S.STL':26s} keys already absent -- nothing to do")


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--revert", action="store_true",
                    help="undo the import (map trees, zone rows, the two STL keys)")
    ap.add_argument("--verify", action="store_true")
    ap.add_argument("--root", default=ROOT)
    ap.add_argument("--source", default=DEFAULT_SRC)
    args = ap.parse_args()

    m = load_importer()
    ours = os.path.join(args.root, "data")

    if args.verify:
        print("verifying the three 667 Oro zones:")
        bad = verify(m, ours)
        sys.exit(1 if bad else 0)

    if args.revert:
        print("reverting the 667 Oro zone import:")
        revert(m, ours, args.dry_run)
        if args.dry_run:
            print("\ndry run -- nothing written")
        else:
            print("\nreverted. Re-pack the VFS (scripts/pack.ps1) and restart the servers.")
        return

    if not os.path.isdir(args.source):
        sys.exit(f"source not found: {args.source}")

    print(f"importing {len(ZONES)} Oro zones from {args.source}")
    copy_maps(m, ours, args.source, args.dry_run)
    write_zone_rows(m, ours, args.source, args.dry_run)
    write_zone_names(m, ours, args.dry_run)

    if args.dry_run:
        print("\ndry run -- nothing written")
        return
    print("\ndone. Rebake the client VFS and restart the servers (STBs are cached).")
    print("The zones are walkable but empty: no monsters, no NPCs, and no warp in.")


if __name__ == "__main__":
    main()
