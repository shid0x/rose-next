#!/usr/bin/env python3
"""Rebuild a usable ZONETYPEINFO.STB for a foreign ROSE data set.

Why this exists
---------------
`3DDATA\\TERRAIN\\TILES\\ZONETYPEINFO.STB` maps a map's `ZoneType` (an int in
block 0 of its `.ZON`) to the editor-side tileset helper tables in `ESTB\\`.
It is **editor-only** data: the client and the servers never read it, only
xadet does, in `MapManager.GetTileSetFile` -> `Cells[zoneType][6]`.

That makes it a soft target. QQ-iROSE ships theirs *encrypted* -- signature
`F5 CA F2 2E` instead of `STB1`, 7.9 bits/byte, and it is the only non-`STB1`
table in their entire dump. The map editor used to die on it at startup; it now
degrades (see `xadet/rose-online-map-editor/CLAUDE.md`), but with no zone-type
table the tile brush palette is empty on every map.

Recovering the table without decrypting anything
------------------------------------------------
The `ZoneType` of every QQ `.ZON`, cross-referenced against its map folder,
reproduces our own table exactly for types 0-12:

    0=JG(JG01..)  1=JD(JD01..)  2=JDT(JDT01)  3=JPT(JPT01..)  4=JPTBG(TITLE_JPT)
    5=JD_1(JD03)  6=JG_1(JG05)  7=JZ(JZ01_*)  8=LP(LP01..)    9=JG_2(JG07)
    10=JG_mov(JMOV01)  11=LZ(LZ01..)  12=EJ(EJ01..)

so rows 0-13 are copied byte-for-byte from our data set. QQ added types 14-17
on top; only two of those are recoverable from the map names:

    14 -> JZP   (JZP01, JZP02, JZP03, GM)                 unambiguous
    15 -> EZ    (EZ01, plus all of ORO -- see below)       best effort
    16 -> ?     (JUNON\\SC, ORO\\ODRP01)                    not recoverable
    17 -> ?     (JUNON\\SCHOOL)                            not recoverable

Type 15 is shared between Eldeon's EZ01 and QQ's custom Oro maps, so one of the
two is going to get the wrong brush palette whatever we pick; EZ is the retail
meaning and gets it. Types 16 and 17 are deliberately left *off the end* of the
table rather than written blank -- an out-of-range ZoneType makes the editor log
"Unknown ZoneType N" and fall back to inferring the tileset from the map folder
name, which is more useful than a silent empty palette.

Only column 6 (the `Table_Tileset_*.STB` name) is ever read. The other columns
of the appended rows are filled from the closest sibling row so the table still
reads sensibly if a human opens it.

The written file is verified by re-parsing it, and `--selftest` proves the
container rewrite is byte-identical before anything is touched.

Usage
-----
    python scripts/rebuild-zonetypeinfo.py --selftest
    python scripts/rebuild-zonetypeinfo.py --target "<data dir>" --dry-run
    python scripts/rebuild-zonetypeinfo.py --target "<data dir>"

STB binary format (little-endian)
---------------------------------
    char[4]  "STB1"
    i32      offset of the cell data (everything after the root column)
    i32      row_count      -- includes the header row, so len(rows) + 1
    i32      column_count   -- includes the root column
    i32      row_size
    i16      column_count+1 column widths
    (i16 len, bytes)         column_count+1 column names
    (i16 len, bytes)         row_count-1 root-column cells
    (i16 len, bytes)         (row_count-1) * (column_count-1) cells, row-major
"""

import argparse
import os
import shutil
import struct
import sys

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE_STB = os.path.join(REPO_ROOT, "data", "3DDATA", "TERRAIN", "TILES", "ZONETYPEINFO.STB")
RELATIVE_STB = os.path.join("3DDATA", "TERRAIN", "TILES", "ZONETYPEINFO.STB")
BACKUP_SUFFIX = ".qq-encrypted"

# Column 6 is the only cell the editor reads; the rest are cosmetic.
COL_TILESET = 6
COL_TILELOOKUP = 5
COL_PATH = 4
COL_DESCRIPTION = 7

# zone type -> (row to clone for the untouched columns, tileset suffix, path stem)
APPENDED_ROWS = [
    (14, 7, "JZP", "Junon\\JZP.txt"),
    (15, 12, "EZ", "Eldeon\\EZ.txt"),
]


class STB(object):
    """A byte-preserving STB table. Cells stay `bytes` so untouched rows round-trip."""

    def __init__(self, data):
        signature = data[:4]

        if signature[:3] != b"STB":
            raise ValueError(
                "not an STB file (signature %s); it is encrypted, compressed or corrupt"
                % " ".join("%02X" % b for b in signature)
            )

        _offset, row_count, column_count, self.row_size = struct.unpack_from("<iiii", data, 4)

        pos = 20

        self.column_widths = list(struct.unpack_from("<%dh" % (column_count + 1), data, pos))
        pos += 2 * (column_count + 1)

        self.column_names, pos = self._read_strings(data, pos, column_count + 1)

        root, pos = self._read_strings(data, pos, row_count - 1)
        self.rows = [[cell] for cell in root]

        for row in self.rows:
            cells, pos = self._read_strings(data, pos, column_count - 1)
            row.extend(cells)

        if pos != len(data):
            raise ValueError("trailing data: parsed %d of %d bytes" % (pos, len(data)))

    @staticmethod
    def _read_strings(data, pos, count):
        out = []

        for _ in range(count):
            (length,) = struct.unpack_from("<h", data, pos)
            pos += 2

            if length < 0 or pos + length > len(data):
                raise ValueError("bad string length %d at offset %d" % (length, pos - 2))

            out.append(data[pos:pos + length])
            pos += length

        return out, pos

    def dump(self):
        column_count = len(self.rows[0])

        out = bytearray()
        out += b"STB1"
        out += struct.pack("<iiii", 0, len(self.rows) + 1, column_count, self.row_size)
        out += struct.pack("<%dh" % len(self.column_widths), *self.column_widths)

        for name in self.column_names:
            out += struct.pack("<h", len(name)) + name

        for row in self.rows:
            out += struct.pack("<h", len(row[0])) + row[0]

        struct.pack_into("<i", out, 4, len(out))

        for row in self.rows:
            for cell in row[1:]:
                out += struct.pack("<h", len(cell)) + cell

        return bytes(out)


def build(source_bytes):
    """Return the rebuilt table bytes, and a list of the appended rows for reporting."""
    table = STB(source_bytes)

    if len(table.rows) != 14:
        raise ValueError(
            "expected 14 zone types in the source table, found %d -- the row->tileset "
            "mapping in this script was derived against the 14-row table" % len(table.rows)
        )

    appended = []

    for zone_type, clone_from, suffix, path in APPENDED_ROWS:
        if zone_type != len(table.rows):
            raise ValueError("zone type %d would not land at row %d" % (zone_type, len(table.rows)))

        row = list(table.rows[clone_from])
        row[COL_PATH] = path.encode("cp949")
        row[COL_TILELOOKUP] = ("TileLookup_Type_%s.STB" % suffix).encode("cp949")
        row[COL_TILESET] = ("Table_Tileset_%s.STB" % suffix).encode("cp949")
        row[COL_DESCRIPTION] = ("%s (recovered)" % suffix).encode("cp949")

        table.rows.append(row)
        appended.append((zone_type, row[COL_TILESET].decode("cp949")))

    return table.dump(), appended


def selftest(source_bytes):
    """Parsing and re-dumping the source must be byte-identical."""
    roundtrip = STB(source_bytes).dump()

    if roundtrip != source_bytes:
        print("SELFTEST FAILED: round-trip is not byte-identical", file=sys.stderr)
        print("  source %d bytes, rewritten %d bytes" % (len(source_bytes), len(roundtrip)), file=sys.stderr)

        for i, (a, b) in enumerate(zip(source_bytes, roundtrip)):
            if a != b:
                print("  first difference at offset %d: %02X != %02X" % (i, a, b), file=sys.stderr)
                break

        return False

    print("selftest: round-trip of %s is byte-identical (%d bytes)" % (SOURCE_STB, len(source_bytes)))
    return True


def verify(path, expected_rows):
    table = STB(open(path, "rb").read())

    if len(table.rows) != expected_rows:
        raise ValueError("verify: wrote %d rows, expected %d" % (len(table.rows), expected_rows))

    for zone_type, tileset in [(t, "Table_Tileset_%s.STB" % s) for t, _, s, _ in APPENDED_ROWS]:
        actual = table.rows[zone_type][COL_TILESET].decode("cp949")

        if actual != tileset:
            raise ValueError("verify: row %d column 6 is %r, expected %r" % (zone_type, actual, tileset))

    return table


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--target", help="data directory to install into (the one holding 3DDATA)")
    parser.add_argument("--source", default=SOURCE_STB, help="table to build from (default: our data/)")
    parser.add_argument("--dry-run", action="store_true", help="report what would happen, write nothing")
    parser.add_argument("--selftest", action="store_true", help="prove the container rewrite is lossless and exit")
    args = parser.parse_args()

    source_bytes = open(args.source, "rb").read()

    if args.selftest:
        return 0 if selftest(source_bytes) else 1

    if not args.target:
        parser.error("--target is required (or use --selftest)")

    if not selftest(source_bytes):
        return 1

    rebuilt, appended = build(source_bytes)

    destination = os.path.join(args.target, RELATIVE_STB)

    if not os.path.isdir(os.path.dirname(destination)):
        print("no such directory: %s" % os.path.dirname(destination), file=sys.stderr)
        return 1

    print("source     : %s (14 zone types)" % args.source)
    print("destination: %s" % destination)

    if os.path.exists(destination):
        existing = open(destination, "rb").read()
        readable = existing[:3] == b"STB"
        print("existing   : %d bytes, %s" % (len(existing), "STB1" if readable else "NOT an STB (encrypted/corrupt)"))

    for zone_type, tileset in appended:
        print("appended   : zone type %d -> %s" % (zone_type, tileset))

    print("result     : %d zone types, %d bytes" % (14 + len(appended), len(rebuilt)))

    if args.dry_run:
        print("dry run, nothing written")
        return 0

    if os.path.exists(destination):
        backup = destination + BACKUP_SUFFIX

        if not os.path.exists(backup):
            shutil.copy2(destination, backup)
            print("backed up  : %s" % backup)
        else:
            print("backup     : %s already exists, kept" % backup)

    with open(destination, "wb") as handle:
        handle.write(rebuilt)

    table = verify(destination, 14 + len(appended))
    print("verified   : re-parsed %d zone types from disk" % len(table.rows))

    for i, row in enumerate(table.rows):
        print("  %2d -> %s" % (i, row[COL_TILESET].decode("cp949")))

    return 0


if __name__ == "__main__":
    sys.exit(main())
