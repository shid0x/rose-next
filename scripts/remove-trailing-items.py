"""Remove a block of appended items from the end of a type's tables.

Undoes an import-item.py run. Only *trailing* rows can go: every id below them
must keep its number, because item ids are baked into character inventories,
drop tables, shop stock and quest rewards. The script refuses unless the rows it
is asked to drop really are the last ones and every one of them matches
--name-prefix, so a mistyped count cannot eat real content.

It touches four things, in the reverse order import-item.py built them:

    <type>.STB     drop the trailing rows AND their row-name entries, which live
                   at the tail of the header region before `data_offset`
    <type>.ZSC     drop the trailing objects (mesh/material lists are left alone;
                   entries there are shared and deduped, so removing any could
                   break an item that is staying)
    <type>_S.STL   drop the trailing keys and their per-language rows
    ITEM1.TSI      drop trailing sprites, but only ones this block introduced --
                   an icon an older item still points at is never removed

**Check the database first.** An item still held by a character, in storage, or
in the mail would be left pointing at a blank row, which is the same shape of bug
as a stale quest id (get_cstr returns null and the client builds a std::string
from it). There is no automatic check here because the server config lives
outside this script; query it yourself, e.g.

    SELECT max(game_data_id) FROM item WHERE type_id = 8;

Usage:
    python scripts/remove-trailing-items.py --type weapon --name-prefix "Rift " --dry-run
    python scripts/remove-trailing-items.py --type weapon --name-prefix "Rift "
"""
import argparse
import importlib.util
import io
import os
import shutil
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OURS = os.path.join(ROOT, "data")


def load(name, mod_name):
    spec = importlib.util.spec_from_file_location(mod_name, os.path.join(HERE, name))
    mod = importlib.util.module_from_spec(spec)
    saved, sys.argv = sys.argv, [name]
    try:
        spec.loader.exec_module(mod)
    except SystemExit:
        pass
    finally:
        sys.argv = saved
    return mod


def stb_truncate(path, ids, dry):
    """Drop rows `ids` (which must be the trailing block) and their row names."""
    with open(path, "rb") as fh:
        raw = fh.read()
    offset, rows, cols = struct.unpack_from("<III", raw, 4)
    if max(ids) != rows - 2:
        sys.exit(f"{path}: rows {ids} are not the trailing block (table ends at {rows - 2})")

    # Row names sit at the tail of the header, one <H len><bytes> each, appended
    # by import-item.py as str(new_id). Strip exactly those, newest first, so a
    # mismatch is caught rather than silently cutting into the column titles.
    header = raw[16:offset]
    for rid in sorted(ids, reverse=True):
        want = str(rid).encode("ascii")
        blk = struct.pack("<H", len(want)) + want
        if not header.endswith(blk):
            sys.exit(f"{path}: header does not end with the row-name block for {rid} "
                     f"(got {header[-len(blk) - 2:]!r}) -- refusing to guess")
        header = header[:-len(blk)]

    f = io.BytesIO(raw)
    f.seek(offset)
    def cell():
        n, = struct.unpack("<H", f.read(2))
        return f.read(n)
    data = [[cell() for _ in range(cols - 1)] for _ in range(rows - 1)]
    data = data[:min(ids)]

    out = io.BytesIO()
    out.write(b"STB1")
    out.write(struct.pack("<III", 16 + len(header), len(data) + 1, cols))
    out.write(header)
    for row in data:
        for c in row:
            out.write(struct.pack("<H", len(c)) + c)
    if not dry:
        with open(path, "wb") as fh:
            fh.write(out.getvalue())
    return len(data)


def zsc_truncate(imp, path, count, dry):
    """Drop `count` trailing objects. Mesh/material lists are deliberately kept:
    they are shared and deduped, so an entry the removed objects used may still
    be referenced by an item that is staying."""
    z = imp.Zsc(path)
    keep = len(z.objects) - count
    # re-serialise by walking to the start of the first doomed object
    f = io.BytesIO(z.d)
    f.seek(z.objcnt_pos + 2)
    for _ in range(keep):
        f.read(12)
        nparts, = struct.unpack("<H", f.read(2))
        if nparts > 0:
            for _ in range(nparts):
                f.read(4)
                while True:
                    t = f.read(1)[0]
                    if t == 0:
                        break
                    f.read(f.read(1)[0])
            ndummy, = struct.unpack("<H", f.read(2))
            for _ in range(ndummy):
                f.read(4)
                while True:
                    t = f.read(1)[0]
                    if t == 0:
                        break
                    f.read(f.read(1)[0])
            f.read(24)
    cut = f.tell()
    out = (z.d[:z.objcnt_pos] + struct.pack("<H", keep) + z.d[z.objcnt_pos + 2:cut])
    if not dry:
        with open(path, "wb") as fh:
            fh.write(out)
    return keep


def stl_truncate(imp, path, keys, dry):
    ks, langs = imp.stl_read(path)
    doomed = {k if isinstance(k, bytes) else k.encode() for k in keys}
    tail = [k for k, _ in ks[-len(doomed):]]
    if set(tail) != doomed:
        sys.exit(f"{path}: trailing keys {tail} do not match {sorted(doomed)}")
    keep = len(ks) - len(doomed)
    if not dry:
        imp.stl_write(path, ks[:keep], [rows[:keep] for rows in langs], dry)
    return keep


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--type", required=True)
    ap.add_argument("--name-prefix", required=True,
                    help="every removed row's name must start with this")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    imp = load("import-item.py", "import_item")
    ico = load("add-item-icon.py", "add_item_icon")
    if args.type not in imp.TYPES:
        sys.exit(f"unknown type {args.type}")
    _, stb_rel, zsc_rels, stl_rel, prefix, _ = imp.TYPES[args.type]

    stb_path = os.path.join(OURS, stb_rel)
    _, _, rows, cols, data = imp.stb_read(stb_path)
    ids = [i for i in range(1, rows - 1)
           if data[i][0].decode("euc-kr", "replace").startswith(args.name_prefix)]
    if not ids:
        print(f"no rows in {os.path.basename(stb_rel)} start with {args.name_prefix!r}")
        return
    if ids != list(range(min(ids), max(ids) + 1)) or max(ids) != rows - 2:
        sys.exit(f"rows {ids} are not a contiguous trailing block -- refusing")

    names = [data[i][0].decode("euc-kr", "replace") for i in ids]
    keys = [data[i][cols - 2] for i in ids]
    icons = sorted({int(data[i][9] or 0) for i in ids})
    print(f"removing {len(ids)} item(s) from {args.type}: rows {min(ids)}-{max(ids)}")
    for i, nm in zip(ids, names):
        print(f"   {i:<6} {nm}")

    # Only drop sprites this block introduced: any icon a surviving row still
    # points at stays, and only a trailing run can go without shifting indices.
    kept_icons = {int(data[i][9] or 0) for i in range(1, rows - 1) if i not in ids}
    _, blocks = ico.tsi_read(ico.TSI)
    total = sum(c for c, _ in blocks)
    drop = [ix for ix in icons if ix not in kept_icons]
    run = 0
    while run < len(drop) and total - 1 - run in drop:
        run += 1
    print(f"   icons introduced here: {drop or 'none'}  -> dropping {run} trailing sprite(s)")

    new_rows = stb_truncate(stb_path, ids, args.dry_run)
    print(f"   STB {rows - 1} -> {new_rows} rows")
    for rel in zsc_rels:
        n = zsc_truncate(imp, os.path.join(OURS, rel), len(ids), args.dry_run)
        print(f"   {os.path.basename(rel)} -> {n} objects")
    n = stl_truncate(imp, os.path.join(OURS, stl_rel), keys, args.dry_run)
    print(f"   STL -> {n} keys")
    if run:
        textures, blocks = ico.tsi_read(ico.TSI)
        cnt, raw = blocks[-1]
        if run > cnt:
            sys.exit("sprite run to drop spans more than the last sheet")
        blocks[-1] = (cnt - run, raw[:(cnt - run) * 54])
        ico.tsi_write(ico.TSI, textures, blocks, args.dry_run)
        print(f"   ITEM1.TSI {total} -> {total - run} sprites")

    print("\ndone." + ("  (dry run -- nothing written)" if args.dry_run else
                       "  Rebake the VFS and restart servers + client."))


if __name__ == "__main__":
    main()
