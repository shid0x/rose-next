"""Set the camera near plane in LIST_CAMERA.STB (game column 5), all rows.

Diagnostic tool for the distance-dependent black-band artifact on map objects
(z-fighting on stacked/decal mesh parts). It is *not* the fix -- the fix is
negotiating a 24-bit depth-stencil format in the engine instead of the
hardcoded `D3DFMT_D16` at src/engine/src/zz_renderer_d3d.cpp:81. This script
exists to confirm the diagnosis from data alone, with no rebuild.

Why it proves anything
----------------------
Resolvable depth separation at view distance z is

    dz = z^2 * (f - n) / (n * f * 2^bits)

and with f >> n that is essentially z^2 / (n * 2^bits) -- the *near* plane is
the whole story and the far plane barely matters. Shipping values are near 1,
far 800 (LIST_CAMERA columns 5 and 6, multiplied by 100 into cm by
CObjClientStorage::ApplyCameraOption, then by ZZ_SCALE_IN into engine metres),
so n = 1 m and a 16-bit buffer resolves 15 cm at 100 m and 61 cm at 200 m.

Raising n to 5 multiplies precision by 5. Because dz grows as z^2, the distance
at which a given geometric gap collapses into one depth bucket scales as
sqrt(n) -- so the artifact should retreat by sqrt(5) ~= 2.24x and visibly
shrink. If it does not move at all, z-fighting is the wrong diagnosis.

Caveats
-------
* A larger near plane clips geometry closer than n metres to the eye. Check the
  avatar and any UI-adjacent 3D at minimum camera zoom before reading anything
  into the result. This is why the default is a modest 5 and not 20.
* All six rows are patched. They are camera quality/zoom presets selected by
  ApplyCameraOption from the [VIDEO] Camera option, and all six ship with
  near = 1, so patching one row would silently do nothing on most settings.
* `--restore` writes the shipped value back. Nothing is stored beside the STB
  on purpose: src/pipeline/src/pack.rs walks the data tree with no extension
  filter, so a .bak sidecar left here gets baked into the .vfs.

The file is rebuilt rather than byte-patched in place, so any value works, not
only ones that happen to have the same digit count as the current cell.

    python scripts/set-camera-near-plane.py            # set to 5
    python scripts/set-camera-near-plane.py 3 --dry-run
    python scripts/set-camera-near-plane.py --verify
    python scripts/set-camera-near-plane.py --restore  # back to the shipped 1
"""
import argparse
import io
import os
import struct
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
STB = os.path.join(HERE, os.pardir, "data", "3DDATA", "STB", "LIST_CAMERA.STB")

NEAR_PLANE_COL = 5   # CAMERA_NEAR_PLANE in src/common/include/rose/io/stb.h
SHIPPED_NEAR = b"1"


def load(path):
    """Split an STB1 into (header_block, cells). Cells are raw bytes, row-major.

    The header block spans byte 0 through data_offset and holds the column
    widths and the row/column name tables. Nothing in it depends on cell
    contents, so rewriting only the cell stream keeps data_offset valid.
    """
    raw = open(path, "rb").read()
    if raw[:4] != b"STB1":
        raise ValueError("%s: not an STB1 file (%r)" % (path, raw[:4]))
    data_offset, raw_rows, raw_cols = struct.unpack_from("<III", raw, 4)
    rows, cols = raw_rows - 1, raw_cols - 1

    f = io.BytesIO(raw)
    f.seek(data_offset)

    def cell():
        n, = struct.unpack("<H", f.read(2))
        return f.read(n)

    cells = [[cell() for _ in range(cols)] for _ in range(rows)]
    if f.tell() != len(raw):
        raise ValueError("%s: parsed %d of %d bytes -- layout mismatch"
                         % (path, f.tell(), len(raw)))
    return raw[:data_offset], cells


def save(path, header, cells):
    out = bytearray(header)
    for row in cells:
        for value in row:
            out += struct.pack("<H", len(value))
            out += value
    open(path, "wb").write(bytes(out))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("near", nargs="?", type=int, default=5,
                    help="near plane in metres (default 5; shipped value is 1)")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--verify", action="store_true", help="report and exit")
    ap.add_argument("--restore", action="store_true", help="write back the shipped 1")
    args = ap.parse_args()

    header, cells = load(STB)
    current = [row[NEAR_PLANE_COL] for row in cells]
    far = [row[NEAR_PLANE_COL + 1] for row in cells]
    print("LIST_CAMERA.STB: %d rows" % len(cells))
    for i, (n, f) in enumerate(zip(current, far)):
        print("  row %d  near=%-6s far=%s" % (i, n.decode("latin-1"), f.decode("latin-1")))

    if args.verify:
        return 0

    want = SHIPPED_NEAR if args.restore else str(args.near).encode("ascii")
    if all(v == want for v in current):
        print("\nalready near=%s -- nothing to do" % want.decode("latin-1"))
        return 0

    print("\n%s near plane -> %s on all %d rows"
          % ("would set" if args.dry_run else "setting", want.decode("latin-1"), len(cells)))
    if args.dry_run:
        return 0

    for row in cells:
        row[NEAR_PLANE_COL] = want
    save(STB, header, cells)

    _, check = load(STB)
    got = [row[NEAR_PLANE_COL] for row in check]
    if any(v != want for v in got):
        print("VERIFY FAILED: %r" % [v.decode('latin-1') for v in got])
        return 1
    print("verified: all rows now near=%s" % want.decode("latin-1"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
