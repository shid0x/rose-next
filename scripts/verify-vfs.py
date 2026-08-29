#!/usr/bin/env python3
"""Check a baked data.idx / rose*.vfs pair for offset overflow before it bites.

WHY
---
The .vfs stores each file's offset in a 32-bit field. It used to be read as
*signed*, capping an archive at 2 GB: anything stored past 2,147,483,647 read
back negative, the client seeked to garbage, and the symptom was a Lua parse
error in INIT.LUA plus a shader assert at startup -- pointing nowhere near the
archive. triggervfs now reads that field as unsigned, which moves the ceiling to
4 GB, but a ceiling is still a ceiling and the failure past it is just as silent.

The packer is supposed to roll over into rose_2.vfs before that happens. It has
never been observed doing so: a 1.95 GB archive was produced with the threshold
set to 1.90 GB and no second file. Until that is understood, this script is the
thing standing between a bulk bake and a corrupt deployment. Run it after every
bake that adds content.

It is deliberately independent of the packer: it re-derives everything from the
bytes on disk, so a bug in pack.rs cannot hide from it.

USAGE
-----
    python scripts/verify-vfs.py <game-dir>          # dir holding data.idx
    python scripts/verify-vfs.py <game-dir> --json   # machine-readable

Exit code 0 = safe, 1 = a problem worth stopping for.
"""

import argparse
import json
import os
import pathlib
import struct
import sys

U32_MAX = 0xFFFFFFFF
I32_MAX = 0x7FFFFFFF


class IdxError(Exception):
    pass


class Reader(object):
    """Little-endian cursor over the .idx bytes."""

    def __init__(self, data):
        self.d = data
        self.o = 0

    def i32(self):
        if self.o + 4 > len(self.d):
            raise IdxError("truncated .idx: wanted 4 bytes at %d" % self.o)
        v = struct.unpack_from("<i", self.d, self.o)[0]
        self.o += 4
        return v

    def u32(self):
        if self.o + 4 > len(self.d):
            raise IdxError("truncated .idx: wanted 4 bytes at %d" % self.o)
        v = struct.unpack_from("<I", self.d, self.o)[0]
        self.o += 4
        return v

    def str16(self):
        if self.o + 2 > len(self.d):
            raise IdxError("truncated .idx: wanted a string length at %d" % self.o)
        n = struct.unpack_from("<H", self.d, self.o)[0]
        self.o += 2
        if self.o + n > len(self.d):
            raise IdxError("truncated .idx: wanted %d string bytes at %d" % (n, self.o))
        s = self.d[self.o:self.o + n].decode("latin-1")
        self.o += n
        return s


def parse_idx(path):
    """Return [(archive_name, [(name, offset, size), ...]), ...].

    Mirrors roselib's VfsIndex::write: base/current version, a filesystem count,
    then per filesystem a name and a *reserved* i32 that is backfilled with the
    absolute offset of that filesystem's file table.
    """
    r = Reader(pathlib.Path(path).read_bytes())
    r.i32()  # base_version
    r.i32()  # current_version
    nfs = r.i32()
    if not (0 < nfs < 10000):
        raise IdxError("implausible filesystem count %d -- is this a data.idx?" % nfs)

    heads = []
    for _ in range(nfs):
        heads.append((r.str16(), r.i32()))

    out = []
    for name, table_off in heads:
        r.o = table_off
        count = r.i32()
        r.i32()  # deleted count
        r.i32()  # first offset (informational)
        files = []
        for _ in range(count):
            fn = r.str16()
            # UNSIGNED: this is the whole point. A legacy archive written past
            # 2 GB stores a value that reads negative as i32 and correct as u32.
            off = r.u32()
            size = r.u32()
            r.u32()          # block size
            r.o += 3         # deleted, compressed, encrypted
            r.i32()          # version
            r.i32()          # checksum
            files.append((fn, off, size))
        out.append((name, files))
    return out


def check(game_dir):
    game_dir = pathlib.Path(game_dir)
    idx = game_dir / "data.idx"
    if not idx.is_file():
        raise IdxError("no data.idx in %s" % game_dir)

    archives = parse_idx(idx)
    report = {"idx": str(idx), "archives": [], "problems": []}

    for name, files in archives:
        vfs = game_dir / name
        present = vfs.is_file()
        actual = vfs.stat().st_size if present else 0
        high = max((o + s for _n, o, s in files), default=0)

        # Would the OLD signed reader have coped? Useful when diagnosing an
        # archive baked before the unsigned change.
        signed_broken = sum(1 for _n, o, _s in files if o > I32_MAX)

        a = {
            "name": name,
            "present": present,
            "entries": len(files),
            "size_bytes": actual,
            "highest_end": high,
            "headroom_bytes": U32_MAX - high,
            "entries_past_2gb": signed_broken,
        }
        report["archives"].append(a)

        if not present:
            report["problems"].append(
                "%s is referenced by data.idx but MISSING. Every rose*.vfs must "
                "ship alongside data.idx; the client will read garbage without it." % name)
            continue
        if high > U32_MAX:
            report["problems"].append(
                "%s: an entry ends at %d, past the 4 GB field limit (%d). The "
                "archive is corrupt -- the packer's rollover did not fire."
                % (name, high, U32_MAX))
        if high > actual:
            report["problems"].append(
                "%s: an entry ends at %d but the file is only %d bytes. Truncated "
                "or mismatched .idx." % (name, high, actual))
    return report


def human(n):
    return "%.2f GB" % (n / 1e9) if n >= 1e9 else "%.1f MB" % (n / 1048576.0)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("game_dir", help="directory containing data.idx and rose*.vfs")
    ap.add_argument("--json", action="store_true", help="machine-readable output")
    args = ap.parse_args()

    try:
        rep = check(args.game_dir)
    except (IdxError, OSError) as e:
        print("FAIL: %s" % e)
        return 1

    if args.json:
        print(json.dumps(rep, indent=2))
        return 1 if rep["problems"] else 0

    print("index: %s" % rep["idx"])
    for a in rep["archives"]:
        print("  %-14s %s  %d entries" % (
            a["name"], "present" if a["present"] else "MISSING", a["entries"]))
        if not a["present"]:
            continue
        print("      file size        %s" % human(a["size_bytes"]))
        print("      highest end      %s" % human(a["highest_end"]))
        print("      headroom to 4 GB %s" % human(a["headroom_bytes"]))
        if a["entries_past_2gb"]:
            print("      %d entries sit past 2 GB -- these REQUIRE the unsigned-offset"
                  % a["entries_past_2gb"])
            print("      triggervfs. An older client will read garbage for them.")

    if rep["problems"]:
        print()
        for p in rep["problems"]:
            print("PROBLEM: %s" % p)
        return 1

    print("\nOK: every entry is addressable and every referenced archive is present.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
