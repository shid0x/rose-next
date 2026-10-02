"""Where can the player walk? Traps and connectivity (phase 2 checks).

The rule, from the client (docs/mapgen/FORMATS.md, "Slope rule"): on bare
terrain every step of the local player runs CObjCHAR_Collision::ApplySliding
(cobjchar_collision.cpp:1104-1180, called from CollisionResponseNone_Avatar
:1290-1301). The step is refused when the cell under the character has
|g| >= tan(0.3 pi) (54 deg) AND the move has a non-negative component along
g, the uphill direction. g is the cell's gradient (terrain.gradient).
Downhill moves are always allowed. The map border blocks (user-verified).
Monsters and other players are not slope-checked at all.

Model: nodes are the 2.5 m cells, moves go to the 8 neighbours with
direction d, and a cell c "allows" d iff |g_c| < T or g_c . d < 0. Which
cell the client tests during a step (the one left or the one entered)
depends on where the model's centre of mass is, so the analysis is made
sound in both directions:

* REACH from the start is liberal: 8 directions, and a move is possible if
  EITHER cell allows it. That is a superset of where a player can really get.
  On a steep cell the walkable directions are the open half-plane
  g . d < 0, so a grid neighbour is reachable exactly when its offset is in
  that half-plane.
* ESCAPE back to the start is strict: a step needs BOTH cells to allow its
  direction. A diagonal step also needs one of the two side cells it can
  pass through to allow it, because a real path cannot go exactly through
  the shared corner point. Every such step is a walk a real player can make,
  so this is a subset of the real escape routes.

Why the side-cell rule matters: the client takes a cell's slope from three
of its four corners only (SW, SE, NW). The cell at a pit's outer SW corner
therefore reads as FLAT even though its NE corner is at the pit floor. A
diagonal step from the floor through that shared corner point would look
like an escape, but any real path crosses one of the two steep side cells.

Dropping diagonals altogether is wrong the other way: at a pit's rim the
only way down can be diagonal (the selftest's ramp case). The synthetic pit
and ramp in `selftest()` pin both cases.

A trap is a cell in liberal-reach that cannot strictly escape. "No traps"
therefore holds for the real client whichever cell it tests.
"""

import math

import numpy as np

from .terrain import gradient

BLOCK_SLOPE = math.tan(0.3 * math.pi)           # 1.3764: |g| at 54 degrees

# (drow, dcol) to the 8 neighbours; row grows north, col grows east.
DIRS = [(-1, -1), (-1, 0), (-1, 1), (0, -1), (0, 1), (1, -1), (1, 0), (1, 1)]


def _allows(gx, gy, steep, dr, dc):
    """Cells that let a step in direction (dc east, dr north) through."""
    return ~steep | ((gx * dc + gy * dr) < 0)


def _shift(a, dr, dc):
    """out[r, c] = a[r - dr, c - dc] (value carried one step in (dr, dc)); False outside."""
    out = np.zeros_like(a)
    h, w = a.shape
    rs, re = max(dr, 0), h + min(dr, 0)
    cs, ce = max(dc, 0), w + min(dc, 0)
    out[rs:re, cs:ce] = a[rs - dr:re - dr, cs - dc:ce - dc]
    return out


def _flood(seed, step_ok):
    """Fixed point of: cell is set if it is the seed or a neighbour stepping into it is.

    step_ok[(dr, dc)][r, c] says the move from (r, c) to (r + dr, c + dc) is
    possible, indexed by the SOURCE cell.
    """
    cur = seed.copy()
    while True:
        nxt = cur.copy()
        for (dr, dc), ok in step_ok.items():
            nxt |= _shift(cur & ok, dr, dc)
        if np.array_equal(nxt, cur):
            return cur
        cur = nxt


def analyse(field, start_vertex, play_mask=None):
    """Return a dict of masks over cells (rows-1, cols-1) plus summary numbers.

    start_vertex is (row, col) in the field; the start cell is the one to its
    north-east. play_mask optionally restricts the connectivity statistic to
    the intended play area (e.g. inside the ridge).
    """
    gx, gy = gradient(field)
    slope = np.hypot(gx, gy)
    steep = slope >= BLOCK_SLOPE
    h, w = slope.shape
    sr = min(start_vertex[0], h - 1)
    sc = min(start_vertex[1], w - 1)

    liberal, strict_rev = {}, {}
    for dr, dc in DIRS:
        src = _allows(gx, gy, steep, dr, dc)                 # source cell allows leaving
        dst = _shift(src, -dr, -dc)                          # target cell (indexed at source) allows entering
        inside = _shift(np.ones_like(src), -dr, -dc)         # target exists
        liberal[(dr, dc)] = (src | dst) & inside
        # strict move a -> b; for the reverse flood (who can reach the start)
        # we walk edges backwards: from b to a, indexed by b.
        strict = src & dst & inside                          # indexed by a, move a -> a+(dr,dc)
        if dr and dc:                                        # diagonal: via a side cell, not the corner point
            strict &= _shift(src, -dr, 0) | _shift(src, 0, -dc)
        strict_rev[(-dr, -dc)] = _shift(strict, dr, dc)      # indexed by b, "came from" b-(dr,dc)

    seed = np.zeros((h, w), bool)
    seed[sr, sc] = True
    reach = _flood(seed, liberal)
    can_return = _flood(seed, strict_rev)
    traps = reach & ~can_return

    gentle = ~steep
    if play_mask is not None:
        gentle &= play_mask
    home = reach & can_return
    connected = float((home & gentle).sum()) / max(1, int(gentle.sum()))
    return {
        "field_shape": field.shape,
        "slope": slope, "steep": steep, "reach": reach, "home": home, "traps": traps,
        "start_cell": (sr, sc),
        "trap_cells": int(traps.sum()),
        "reach_cells": int(reach.sum()),
        "steep_cells": int(steep.sum()),
        "connected_fraction": connected,
        "max_slope_deg": math.degrees(math.atan(float(slope.max()))),
    }


def selftest():
    """Synthetic terrains with known answers. Returns a list of failures.

    Fields are 65x65 vertices (one chunk); the start is the centre vertex.
    Heights in cm; one grid = 250 cm, so a 1000 cm rise in one grid is
    about 76 degrees, far past the 54-degree limit.
    """
    fails = []
    n = 65
    centre = (32, 32)

    def expect(name, field, traps, home_frac=None):
        a = analyse(field, centre)
        ok = (a["trap_cells"] > 0) == traps
        if home_frac is not None:
            ok &= abs(a["connected_fraction"] - home_frac) < 0.02
        print("  %s %-58s traps %4d  connected %5.1f%%" % (
            "ok  " if ok else "FAIL", name, a["trap_cells"], 100 * a["connected_fraction"]))
        if not ok:
            fails.append(name)
        return a

    flat = np.zeros((n, n))
    expect("flat ground: nothing to trap anyone", flat, traps=False, home_frac=1.0)

    pit = flat.copy()
    pit[10:20, 10:20] = -1000.0             # 10 m deep, vertical walls
    a = expect("pit with vertical walls: walk in, cannot climb out", pit, traps=True)
    inside = a["traps"][11:18, 11:18].all()
    print("        pit floor flagged as trap: %s" % inside)
    if not inside:
        fails.append("pit floor not flagged")

    ramp = pit.copy()
    for i in range(20, 30):                 # gentle ramp out of the pit's east side (~22 deg)
        ramp[12:16, i] = -1000.0 + (i - 19) * 100.0
    ramp[12:16, 30:] = 0.0
    expect("same pit with a gentle ramp out: no trap", ramp, traps=False)

    plateau = flat.copy()
    plateau[45:60, 45:60] = 1500.0          # 15 m cliff UP from the start's level
    expect("cliff up to a plateau: unreachable, so not a trap", plateau, traps=False)

    high = np.full((n, n), 1500.0)
    high[:, :20] = 0.0                      # start on a plateau; west strip is 15 m below
    expect("drop off a cliff to low ground with no way back: trap", high, traps=True)
    back = high.copy()
    for i in range(20, 40):                 # ramp back up (~17 deg)
        back[30:34, i] = (i - 19) * 75.0
    back[30:34, 40:] = 1500.0
    expect("same drop with a ramp back up: no trap", back, traps=False)

    slope53 = np.add.outer(np.arange(n), np.zeros(n)) * 250.0 * math.tan(math.radians(53))
    expect("uniform 53-degree slope: climbable everywhere", slope53, traps=False, home_frac=1.0)
    bowl = np.hypot(*np.meshgrid(np.arange(n) - 32.0, np.arange(n) - 32.0)) * 250.0 * math.tan(math.radians(56))
    bowl[28:37, 28:37] = bowl[28:37, 28:37].min()   # flat floor at the centre
    expect("start inside a 56-degree bowl: can walk nowhere uphill", bowl, traps=False, home_frac=None)
    return fails
