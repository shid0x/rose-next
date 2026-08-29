#!/usr/bin/env python3
"""Build a synthetic .vfs/.idx larger than 2 GB, to prove the unsigned-offset fix.

WHY
---
The .vfs stores file offsets in a 32-bit field. It used to be read as *signed*,
so an archive could not exceed 2 GB: anything past 2,147,483,647 read back
negative and the client silently read garbage. triggervfs now reads that field
as unsigned, lifting the ceiling to 4 GB.

That change cannot be trusted on inspection alone -- the failure mode is silent
wrong data, not a crash -- and the only honest test is an archive that actually
crosses the boundary. Baking a real >2 GB one takes minutes and gigabytes; this
builds a targeted one in seconds by leaving a hole in the middle.

The payload is deterministic (byte i of a file = (i * 31 + seed) & 0xFF, seed
derived from the name), so the reader can verify content it was never told,
which is what makes it an independent oracle rather than a tautology.

Pair with vfs_buffer_tests.exe --bigtest <dir>, which reads it back through the
real triggervfs and checks the pattern.

USAGE
-----
    python scripts/make-oversize-vfs.py <out-dir> [--size-gb 2.3]

The hole is sparse where the filesystem allows it, so this costs far less disk
than its nominal size. Delete the directory when done.
"""

import argparse
import pathlib
import struct
import subprocess
import sys

# Files placed either side of the 2 GB line. The last one is the whole point.
LAYOUT = [
    ("3DDATA\\TEST\\LOW.DAT", 4096),        # comfortably below 2 GB
    ("3DDATA\\TEST\\NEAR.DAT", 8192),       # just below the boundary
    ("3DDATA\\TEST\\HIGH.DAT", 16384),      # just above it
    ("3DDATA\\TEST\\HIGHEST.DAT", 4096),    # near the end of the archive
]

I32_MAX = 0x7FFFFFFF


def seed_for(name):
    s = 0
    for ch in name:
        s = (s * 131 + ord(ch)) & 0xFFFFFFFF
    return s & 0xFF


def payload(name, size):
    """Deterministic content the reader can re-derive from the name alone."""
    seed = seed_for(name)
    return bytes(((i * 31 + seed) & 0xFF) for i in range(size))


def w_str16(f, s):
    b = s.encode("latin-1")
    f.write(struct.pack("<H", len(b)))
    f.write(b)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out_dir")
    ap.add_argument("--size-gb", type=float, default=2.3,
                    help="nominal archive size; must exceed 2.147 to be a real test")
    args = ap.parse_args()

    total = int(args.size_gb * 1e9)
    if total <= I32_MAX:
        print("refusing: --size-gb %.2f does not cross the 2 GB line, so it would "
              "prove nothing" % args.size_gb)
        return 1

    out = pathlib.Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    vfs_path = out / "rose.vfs"
    idx_path = out / "data.idx"

    # Offsets: two below the boundary, two above.
    placed = []
    offsets = [0, I32_MAX - 40000, I32_MAX + 100000, total - 8192]
    for (name, size), off in zip(LAYOUT, offsets):
        placed.append((name, off, size))

    # Sparse where possible: the gap between entries is never written.
    if sys.platform == "win32":
        subprocess.run(["fsutil", "sparse", "setflag", str(vfs_path)],
                       capture_output=True)
    with open(vfs_path, "wb") as f:
        if sys.platform == "win32":
            f.flush()
            subprocess.run(["fsutil", "sparse", "setflag", str(vfs_path)],
                           capture_output=True)
        for name, off, size in placed:
            f.seek(off)
            f.write(payload(name, size))
        f.seek(total - 1)
        f.write(b"\0")

    with open(idx_path, "wb") as f:
        f.write(struct.pack("<iii", 100, 100, 1))   # base ver, cur ver, 1 filesystem
        w_str16(f, "rose.vfs")
        table_off_pos = f.tell()
        f.write(struct.pack("<i", 0))               # placeholder, backfilled below

        table_off = f.tell()
        f.write(struct.pack("<iii", len(placed), 0, placed[0][1]))
        for name, off, size in placed:
            w_str16(f, name)
            # Bit-preserving, exactly as pack.rs does: an offset above 2 GB is
            # written as a negative i32 whose four bytes are the correct u32.
            as_i32 = struct.unpack("<i", struct.pack("<I", off))[0]
            f.write(struct.pack("<i", as_i32))
            f.write(struct.pack("<i", size))      # size
            f.write(struct.pack("<i", size))      # block size
            f.write(bytes(3))                 # deleted, compressed, encrypted
            f.write(struct.pack("<i", 1))         # version
            f.write(struct.pack("<i", 0))         # checksum

        f.seek(table_off_pos)
        f.write(struct.pack("<i", table_off))

    actual = vfs_path.stat().st_size
    print("wrote %s" % vfs_path)
    print("  nominal size   : %.2f GB" % (actual / 1e9))
    print("  entries        : %d" % len(placed))
    for name, off, size in placed:
        side = "ABOVE 2 GB" if off > I32_MAX else "below"
        print("    %-28s offset %13d  %s" % (name, off, side))
    print("wrote %s" % idx_path)
    print("\nnow: vfs_buffer_tests.exe --bigtest \"%s\"" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
