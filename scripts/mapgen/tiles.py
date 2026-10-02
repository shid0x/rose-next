"""Terrain tiles as corner brushes (phase 3).

A TIL tile is one 10 m patch. Its texture is set by the brush (terrain
type) at each of its 4 corners. Neighbouring tiles share corners, so a zone's
painting is really one lattice of corner brushes, with (16 * chunks + 1)
corners per axis, row 0 = SOUTH. Docs: docs/mapgen/FORMATS.md, "TIL" and
"Tile painting".

Table_Tileset_<type>.STB (data/ESTB, editor data) defines:

* tile sets: game rows 1..N. Set k pairs (min, max) brushes:
  - mask bit set = that corner is `min`, clear = `max`;
  - mask 15 -> TileNumberF (+ TileCountF variants);
  - mask 0 -> TileNumber0;
  - otherwise TileNumber + (Direction < 0 ? 15 - mask : mask) * TileCount.
* the chain table after the sets: chain[a][b] == 99 means a and b may share
  a tile; otherwise it names the brush to put between them.

Mask bits -> corners (corpus-proven, 2026-10-02): 1 = SE, 2 = SW, 4 = NE,
8 = NW. With this assignment neighbouring tiles agree on 99.8% of shared
corners in JG01; the next best assignment manages 70%.
"""

import os
import struct

import numpy as np

from .chunk import TILE_DTYPE

BIT_CORNER = {1: (0, 1), 2: (0, 0), 4: (1, 1), 8: (1, 0)}   # bit -> (dr, dc) from the tile's SW corner
DIRECT = 99


def read_stb_cells(path, encoding="cp949"):
    """Data cells of an STB (game rows x game cols) as str."""
    with open(path, "rb") as f:
        raw = f.read()
    if raw[:3] != b"STB":
        raise ValueError("%s: not an STB" % path)
    off, rows, cols = struct.unpack_from("<III", raw, 4)
    pos, out = off, []
    for _ in range(rows - 1):
        row = []
        for _ in range(cols - 1):
            n = struct.unpack_from("<H", raw, pos)[0]
            row.append(raw[pos + 2:pos + 2 + n].decode(encoding, "replace"))
            pos += 2 + n
        out.append(row)
    return out


def _int(s):
    s = s.strip()
    try:
        return int(s)
    except ValueError:
        return 0


class Tileset:
    def __init__(self, path):
        cells = read_stb_cells(path)
        n = _int(cells[0][1])
        self.sets = []
        for r in range(1, n + 1):
            v = [_int(x) for x in cells[r][1:10]]
            self.sets.append(dict(minb=v[0], maxb=v[1], t0=v[2], c0=v[3], tf=v[4], cf=v[5],
                                  t=v[6], c=v[7], d=v[8]))
        nb = _int(cells[n + 1][1])
        # Each chain row is labelled with its brush in column 0 ("짙은 흙(0)").
        # The MAX_BRUSH_COUNT header row starts at brush 1, so it is not used.
        self.brush_names = [cells[n + 2 + b][0].strip() for b in range(nb)]
        self.chain = np.array([[_int(cells[n + 2 + a][1 + b]) for b in range(nb)] for a in range(nb)])
        self.pair = {(s["minb"], s["maxb"]): k for k, s in enumerate(self.sets)}
        self.full = {}                   # brush -> (set index, first id, variant count)
        for k, s in enumerate(self.sets):
            self.full.setdefault(s["minb"], (k, s["tf"], max(1, s["cf"])))

    @property
    def brushes(self):
        return len(self.chain)

    def direct(self, a, b):
        return a == b or self.chain[a][b] == DIRECT

    def edge_id(self, k, mask):
        s = self.sets[k]
        if mask == 15:
            return s["tf"]
        if mask == 0:
            return s["t0"]
        return s["t"] + ((15 - mask) if s["d"] < 0 else mask) * max(1, s["c"])

    def tile_for(self, corners, rng=None):
        """TIL record fields for a tile whose corners are {(dr, dc): brush}.

        Returns (brush, tile_set, tile_index, tile_id), or None if the corners
        hold more than two brushes or a pair no set covers. Full tiles pick a
        seeded variant (TileCountF of them) when rng is given.
        """
        distinct = set(corners.values())
        if len(distinct) == 1:
            b = distinct.pop()
            if b not in self.full:
                return None
            k, first, count = self.full[b]
            v = int(rng.integers(count)) if rng is not None else 0
            return b, k, 15, first + v
        if len(distinct) != 2:
            return None
        a, b = sorted(distinct)          # deterministic choice between mirror sets (same id)
        for lo, hi in ((a, b), (b, a)):
            if (lo, hi) in self.pair:
                k = self.pair[(lo, hi)]
                mask = sum(bit for bit, rc in BIT_CORNER.items() if corners[rc] == lo)
                return lo, k, mask, self.edge_id(k, mask)
        return None

    def corners_of(self, rec):
        """{(dr, dc): brush} of a TIL record (inverse of tile_for)."""
        s = self.sets[int(rec["tile_set"])]
        m = int(rec["tile_index"])
        return {rc: (s["minb"] if m & bit else s["maxb"]) for bit, rc in BIT_CORNER.items()}


def tileset_path(data_dir, name):
    return os.path.join(data_dir, "ESTB", "Table_Tileset_%s.STB" % name)


def load_zone_tiles(zone_dir):
    """All of a zone folder's TILs as one grid (row 0 = south), a mask of
    tiles that exist, and the SW chunk slot (x0, y0)."""
    import re
    from .chunk import parse_til
    found = {}
    for f in os.listdir(zone_dir):
        m = re.match(r"(\d+)_(\d+)\.til$", f, re.I)
        if m:
            with open(os.path.join(zone_dir, f), "rb") as fh:
                found[(int(m.group(1)), 64 - int(m.group(2)))] = parse_til(fh.read()).tiles
    xs = [k[0] for k in found]
    ys = [k[1] for k in found]
    x0, y0 = min(xs), min(ys)
    shape = ((max(ys) - y0 + 1) * 16, (max(xs) - x0 + 1) * 16)
    grid = np.zeros(shape, dtype=TILE_DTYPE)
    have = np.zeros(shape, bool)
    for (x, y), t in found.items():
        r, c = (y - y0) * 16, (x - x0) * 16
        grid[r:r + 16, c:c + 16] = t[::-1]          # TIL row 0 is north
        have[r:r + 16, c:c + 16] = True
    return grid, have, (x0, y0)


def regenerate_check(grid, have, ts):
    """Rebuild every tile from the corner lattice and compare with the file.

    Edge tiles must match the stored tile_id exactly. Full tiles must land in
    the same brush's variant range, since the variant is the painter's
    random pick. Returns (lattice agreement, matched, total, illegal).
    """
    lattice, agreement = lattice_votes(grid, ts, have)
    matched = total = illegal = 0
    rows, cols = grid.shape
    for r in range(rows):
        for c in range(cols):
            if not have[r, c]:
                continue
            total += 1
            cs = {rc: int(lattice[r + rc[0], c + rc[1]]) for rc in BIT_CORNER.values()}
            t = ts.tile_for(cs)
            if t is None:
                illegal += 1
                continue
            brush, _, mask, tid = t
            fid = int(grid[r, c]["tile_id"])
            if mask == 15:
                _, first, count = ts.full[brush]
                matched += first <= fid < first + count
            else:
                matched += fid == tid
    return agreement, matched, total, illegal


# ------------------------------------------------------------------ lattice


def legalize(corners, ts, priority, max_rounds=64, allow_saddles=False, protected=None):
    """Make every tile's corners a legal combination, in place.

    A tile is legal when its corners hold one brush, or two that the chain
    table lets touch. Where two corners of a tile clash, the corner whose
    brush has the LOWER `priority` becomes chain[a][b], the brush the tileset
    puts between them (grass next to a path becomes dark soil, and so on).
    Repeats until stable. Then, unless allow_saddles, checkerboard tiles are
    removed without touching `protected` corners (remove_saddles). Returns
    the number of corners changed.
    """
    pr = np.asarray(priority)
    changed = 0
    for _ in range(max_rounds):
        moved = 0
        rows, cols = corners.shape
        for r in range(rows - 1):
            for c in range(cols - 1):
                cs = [(r, c), (r, c + 1), (r + 1, c), (r + 1, c + 1)]
                for i in range(4):
                    for j in range(i + 1, 4):
                        a, b = corners[cs[i]], corners[cs[j]]
                        if ts.direct(a, b):
                            continue
                        lo = cs[i] if pr[a] < pr[b] else cs[j]
                        hi_brush = b if lo == cs[i] else a
                        corners[lo] = ts.chain[corners[lo]][hi_brush]
                        moved += 1
        changed += moved
        if not moved:
            if not allow_saddles:
                changed += remove_saddles(corners, ts, protected)
            return changed
    raise RuntimeError("legalize did not converge in %d rounds" % max_rounds)


def _is_saddle(sw, se, nw, ne):
    return sw == ne and se == nw and sw != se


def _tile_ok(corners, ts, r, c):
    """Tile with SW corner (r, c): legal (one brush or a direct pair) and not a saddle."""
    sw, se, nw, ne = corners[r, c], corners[r, c + 1], corners[r + 1, c], corners[r + 1, c + 1]
    distinct = {sw, se, nw, ne}
    if len(distinct) > 2:
        return False
    if len(distinct) == 2 and not ts.direct(*distinct):
        return False
    return not _is_saddle(sw, se, nw, ne)


def remove_saddles(corners, ts, protected=None):
    """Remove checkerboard tiles (SW == NE != SE == NW, masks 6 / 9), in place.

    They are legal, and the tileset has textures for them (the *_04 alpha
    with two blobs), but retail JG uses them on ~0.04% of tiles. A diagonal
    line of single corners turns into a dotted row of them, which reads as a
    "ladder" or dotted line.

    A saddle is fixed by giving one of its corners the other diagonal's
    brush, only when every tile touching that corner stays legal and no new
    saddle appears. Each accepted change removes at least one saddle and
    creates none, so this terminates. Saddles with no safe fix are left;
    phase 3's check allows 0.2%. Returns the number of corners changed.
    """
    rows, cols = corners.shape
    changed = 0
    progress = True
    while progress:
        progress = False
        for r in range(rows - 1):
            for c in range(cols - 1):
                sw, se, nw, ne = corners[r, c], corners[r, c + 1], corners[r + 1, c], corners[r + 1, c + 1]
                if not _is_saddle(sw, se, nw, ne):
                    continue
                for (cr, cc), new in (((r, c), se), ((r, c + 1), sw), ((r + 1, c + 1), se), ((r + 1, c), sw)):
                    if protected is not None and protected[cr, cc]:
                        continue
                    old = corners[cr, cc]
                    corners[cr, cc] = new
                    ok = all(_tile_ok(corners, ts, tr, tc)
                             for tr in (cr - 1, cr) for tc in (cc - 1, cc)
                             if 0 <= tr < rows - 1 and 0 <= tc < cols - 1)
                    if ok:
                        changed += 1
                        progress = True
                        break
                    corners[cr, cc] = old
    return changed


def tiles_from_lattice(corners, ts, rng):
    """(rows-1, cols-1) TILE_DTYPE array (row 0 = south) from a legal lattice."""
    rows, cols = corners.shape
    out = np.zeros((rows - 1, cols - 1), dtype=TILE_DTYPE)
    for r in range(rows - 1):
        for c in range(cols - 1):
            cs = {rc: int(corners[r + rc[0], c + rc[1]]) for rc in BIT_CORNER.values()}
            t = ts.tile_for(cs, rng)
            if t is None:
                raise ValueError("illegal tile at (%d, %d): %s" % (r, c, cs))
            # By name: TILE_DTYPE's field order (brush, tile_index, tile_set,
            # tile_id) is not tile_for's (brush, tile_set, tile_index, tile_id).
            rec = out[r, c]
            rec["brush"], rec["tile_set"], rec["tile_index"], rec["tile_id"] = t
            out[r, c] = rec
    return out


def lattice_votes(tiles, ts, have=None):
    """Corner lattice from tiles (row 0 = south) by majority vote, plus the
    fraction of shared corners on which every incident tile agrees.

    `have` masks tiles that exist (zones need not be rectangular); corners
    with no tile get -1."""
    rows, cols = tiles.shape
    votes = [[[] for _ in range(cols + 1)] for _ in range(rows + 1)]
    for r in range(rows):
        for c in range(cols):
            if have is not None and not have[r, c]:
                continue
            for (dr, dc), b in ts.corners_of(tiles[r, c]).items():
                votes[r + dr][c + dc].append(b)
    lattice = np.full((rows + 1, cols + 1), -1, int)
    agree = shared = 0
    for r in range(rows + 1):
        for c in range(cols + 1):
            v = votes[r][c]
            if not v:
                continue
            vals, counts = np.unique(v, return_counts=True)
            lattice[r, c] = vals[np.argmax(counts)]
            if len(v) > 1:
                shared += 1
                agree += len(vals) == 1
    return lattice, agree / max(1, shared)
