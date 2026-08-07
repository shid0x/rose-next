"""Add missing zone-name entries to 3DDATA/STB/LIST_ZONE_S.STL.

Symptom: a zone shows no name above the minimap (and none on the world map /
zone-change banner) even though LIST_ZONE.STB clearly has one.

Cause: the minimap dialog does not read the STB name column. `CMinimapDLG::
LoadMinimap` calls `ZONE_NAME(zone)`, which in the client resolves to
`CStringManager::GetZoneName` -> look up `ZONE_STRING_ID(zone)` (LIST_ZONE.STB
game column 26, e.g. "LZON064") in LIST_ZONE_S.STL. When the STL has no entry for
that key, `GetZoneName` returns the empty string and the label draws nothing.

Our LIST_ZONE_S.STL stopped at LZON062, so every Eldeon zone added after Shady
Jungle came up blank. The names written here are taken from our own
LIST_ZONE.STB name column (game column 0) and match the titanRose reference data
exactly. Descriptions are written empty, which is what every existing entry in
our STL has.

Idempotent: keys already present are left untouched (their text is not
rewritten). Makes a .bak backup. Use --dry-run to preview, --key/--name to add
an arbitrary entry instead of the built-in defaults.

This is client-only data -- the server never reads zone name strings.
After running: rebake/deploy the client VFS data (the STL loads through the VFS,
unlike UI_strID.ID).

STL binary layout (ITST01), which this script round-trips:
    pstr "ITST01"
    u32  key_count
    key_count x { pstr key; u32 id }
    u32  language_count
    language_count x u32 language_block_offset
    per language block, at its offset:
        key_count x u32 entry_offset      (absolute; the client skips these and
                                           reads the strings sequentially, but
                                           other tools use them, so keep valid)
        key_count x { pstr name; pstr description }
`pstr` is a 7-bit varint length (high bit of byte 0 set => a second byte carries
the upper bits) followed by raw UTF-8 bytes; the client runs the result through
CLocalizing::UTF8ToMBCS. All five language blocks in our data hold the same
English text, and the client picks block 1 (LANGUAGE_USA) on a western charset.
"""
import argparse, io, os, shutil, struct, sys

STL_REL = os.path.join("data", "3DDATA", "STB", "LIST_ZONE_S.STL")
ZONE_STB_REL = os.path.join("data", "3DDATA", "STB", "LIST_ZONE.STB")

# zone row -> string key; the display name is read from LIST_ZONE.STB column 0.
DEFAULT_ZONES = [63, 64, 65]

ZONE_NAME_COL = 0
ZONE_STRING_ID_COL = 26


# ------------------------------------------------------------------ pstr
def read_pstr(buf, o):
    b = buf[o]
    o += 1
    if b & 0x80:
        b2 = buf[o]
        o += 1
        n = (b2 << 7) | (b - 0x80)
    else:
        n = b
    s = buf[o:o + n]
    return s, o + n


def write_pstr(out, s):
    n = len(s)
    if n < 0x80:
        out.write(bytes([n]))
    elif n < 0x4000:
        out.write(bytes([(n & 0x7F) | 0x80, n >> 7]))
    else:
        raise ValueError(f"string too long for a pascal string: {n}")
    out.write(s)


# ------------------------------------------------------------------ STL
def stl_read(path):
    buf = open(path, "rb").read()
    o = 0
    fmt, o = read_pstr(buf, o)
    if fmt != b"ITST01":
        raise SystemExit(f"{path}: expected ITST01, got {fmt!r}")
    count, = struct.unpack_from("<I", buf, o)
    o += 4
    keys = []
    for _ in range(count):
        k, o = read_pstr(buf, o)
        i, = struct.unpack_from("<I", buf, o)
        o += 4
        keys.append((k, i))
    nlang, = struct.unpack_from("<I", buf, o)
    o += 4
    lang_off = list(struct.unpack_from(f"<{nlang}I", buf, o))
    langs = []
    for base in lang_off:
        entry_off = struct.unpack_from(f"<{count}I", buf, base)
        rows = []
        for eo in entry_off:
            name, p = read_pstr(buf, eo)
            desc, _ = read_pstr(buf, p)
            rows.append((name, desc))
        langs.append(rows)
    return keys, langs


def stl_write(path, keys, langs):
    out = io.BytesIO()
    write_pstr(out, b"ITST01")
    out.write(struct.pack("<I", len(keys)))
    for k, i in keys:
        write_pstr(out, k)
        out.write(struct.pack("<I", i))
    out.write(struct.pack("<I", len(langs)))
    lang_off_pos = out.tell()
    out.write(b"\0" * (4 * len(langs)))

    lang_offsets = []
    for rows in langs:
        base = out.tell()
        lang_offsets.append(base)
        entry_off_pos = out.tell()
        out.write(b"\0" * (4 * len(keys)))
        entry_offsets = []
        for name, desc in rows:
            entry_offsets.append(out.tell())
            write_pstr(out, name)
            write_pstr(out, desc)
        end = out.tell()
        out.seek(entry_off_pos)
        out.write(struct.pack(f"<{len(keys)}I", *entry_offsets))
        out.seek(end)

    end = out.tell()
    out.seek(lang_off_pos)
    out.write(struct.pack(f"<{len(langs)}I", *lang_offsets))
    out.seek(end)

    with open(path, "wb") as fh:
        fh.write(out.getvalue())


# ------------------------------------------------------------------ STB
def stb_read(path):
    raw = open(path, "rb").read()
    f = io.BytesIO(raw)
    if f.read(4) != b"STB1":
        raise SystemExit(f"{path}: not an STB1 file")
    offset, rows, cols = struct.unpack("<III", f.read(12))
    f.seek(offset)

    def pstr():
        n, = struct.unpack("<H", f.read(2))
        return f.read(n)

    return [[pstr() for _ in range(cols - 1)] for _ in range(rows - 1)]


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true", help="preview without writing")
    ap.add_argument("--root", default=".", help="repo root (default: cwd)")
    ap.add_argument("--zone", type=int, action="append",
                    help="zone row to add (repeatable; default: 63 64 65)")
    ap.add_argument("--name", help="override the display name (single --zone only)")
    args = ap.parse_args()

    stl_path = os.path.join(args.root, STL_REL)
    stb_path = os.path.join(args.root, ZONE_STB_REL)
    for p in (stl_path, stb_path):
        if not os.path.isfile(p):
            raise SystemExit(f"not found: {p} (run from the repo root or pass --root)")

    zones = args.zone or DEFAULT_ZONES
    if args.name and len(zones) != 1:
        raise SystemExit("--name requires exactly one --zone")

    zone_tbl = stb_read(stb_path)
    keys, langs = stl_read(stl_path)
    have = {k for k, _ in keys}
    print(f"{STL_REL}: {len(keys)} entries, {len(langs)} language blocks")

    added = 0
    for z in zones:
        if z >= len(zone_tbl):
            print(f"  zone {z}: not in LIST_ZONE.STB -- SKIPPED")
            continue
        key = zone_tbl[z][ZONE_STRING_ID_COL]
        name = (args.name.encode("utf-8") if args.name
                else zone_tbl[z][ZONE_NAME_COL])
        if not key:
            print(f"  zone {z}: LIST_ZONE.STB has no ZONE_STRING_ID -- SKIPPED")
            continue
        if not name:
            print(f"  zone {z}: LIST_ZONE.STB has no name -- SKIPPED")
            continue
        if key in have:
            print(f"  zone {z}: {key.decode()} already present -- ok")
            continue
        print(f"  zone {z}: + {key.decode()} = {name.decode('utf-8', 'replace')!r}")
        keys.append((key, z))
        for rows in langs:
            rows.append((name, b""))
        have.add(key)
        added += 1

    if not added:
        print("nothing to do")
        return
    if args.dry_run:
        print(f"dry run: {added} entr(ies) would be added")
        return

    shutil.copyfile(stl_path, stl_path + ".bak")
    stl_write(stl_path, keys, langs)

    vkeys, vlangs = stl_read(stl_path)
    if len(vkeys) != len(keys) or len(vlangs) != len(langs):
        raise SystemExit("VERIFY FAILED: entry/language count mismatch after write")
    for i, (k, _) in enumerate(vkeys):
        for li, rows in enumerate(vlangs):
            if rows[i] != langs[li][i]:
                raise SystemExit(
                    f"VERIFY FAILED: {k!r} lang {li} reads {rows[i]!r}, expected {langs[li][i]!r}")
    print(f"wrote {added} entr(ies); backup at {os.path.basename(stl_path)}.bak; verified")


if __name__ == "__main__":
    sys.exit(main())
