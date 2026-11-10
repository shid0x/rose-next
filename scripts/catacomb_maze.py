#!/usr/bin/env python3
"""The Karkia catacombs' maze generator: the Python mirror.

Design: doc/karkia-catacombs-implementation-brief.md, section 3. The game server gets
its own C++ copy in milestone 3; **the two must produce the same edges for the same
seed**, which the test vectors at the bottom of this docstring check. Until then this
file is the generator: `scripts/import-catacombs.py` writes one fixed maze per crypt
into the map files with it (milestone 2).

    python scripts/catacomb_maze.py                    # today's static mazes (day 0), as text
    python scripts/catacomb_maze.py --maze 90 --day 20767
    python scripts/catacomb_maze.py --vectors          # the test vectors

The model
---------
A grid of N x N cells (10 x 10). Cells are always open; the maze is which **edges**
between neighbouring cells carry a wall. Two rooms are open inside and fixed: the
entrance hall, 2 x 2 cells in the south-west corner, and the boss room, 4 x 4 in the
north-east corner.

Edge ids, the bitset the server will share (bit set = wall):
  * 0 .. N*(N-1)-1         the edge between cell (i, j) and (i+1, j): id = i + j*(N-1)
  * N*(N-1) .. 2*N*(N-1)-1 the edge between cell (i, j) and (i, j+1): id = N*(N-1) + i + j*N
with i east, j north, both from 0.

The algorithm
-------------
1. Every edge inside a room is open; every other edge starts closed.
2. **Carve** with a depth-first walk (the recursive backtracker, written with a stack)
   over *nodes*: a plain cell is a node, a room is one node. From the node on top of
   the stack, list the closed edges that lead to an unvisited node, in ascending edge
   id, and open one at random; the walk starts in the hall. **The boss room is never
   walked on from**: it is entered once and left alone, so it has exactly one door.
3. **Braid**: while more than DEAD_ENDS plain cells have a single open edge, pick one
   of them at random (ascending cell id) and open one of its closed edges at random
   (ascending edge id), never an edge of the boss room. A dead end with no edge it may
   open is left as it is.

Randomness is splitmix64, written out here: no library generator, whose sequence would
not match the C++ one. `below(n)` is `next() % n`.

Test vectors (maze id, day -> FNV-1a 64 of the 180 edge bytes, 1 = wall), from
`--vectors`; the C++ port is done when it prints the same:

    see VECTORS below (kept in code, so --vectors checks the generator against them)
"""
import argparse
import sys

N = 10
HALL = (0, 0, 2, 2)            # i0, j0, width, height
BOSS = (6, 6, 4, 4)            # 60 m: 3 x 3 with pillars inside was too tight to fight in (the user, 2026-11-10)
DEAD_ENDS = 2                  # the user, 2026-11-09: "maybe 2 for now"; tuned by playing
SALT = 0x4B41524B49410001      # "KARKIA", version 1

H_EDGES = N * (N - 1)
EDGES = 2 * H_EDGES
MASK = (1 << 64) - 1
HALL_NODE, BOSS_NODE = N * N, N * N + 1

# (maze id, day): FNV-1a 64 of the edge bytes. Filled from a run; `--vectors` re-checks.
VECTORS = {
    (90, 0): 0x9B0FAB2781071B37,
    (90, 1): 0xCEF29EA72B1566A1,
    (90, 20767): 0xD1B5106FE281976E,
    (91, 0): 0x6B56403122DACE98,
    (91, 1): 0x3F94487D6960C511,
    (91, 20767): 0xADB6550618BB419D,
}


class Rng:
    """splitmix64."""

    def __init__(self, seed):
        self.s = seed & MASK

    def next(self):
        self.s = (self.s + 0x9E3779B97F4A7C15) & MASK
        z = self.s
        z = ((z ^ (z >> 30)) * 0xBF58476D1CE4E5B9) & MASK
        z = ((z ^ (z >> 27)) * 0x94D049BB133111EB) & MASK
        return z ^ (z >> 31)

    def below(self, n):
        return self.next() % n


def mix(x):
    return Rng(x).next()


def seed_for(maze_id, day):
    """The day's seed for a crypt: a hash of the salt, the crypt and the day."""
    return mix(mix(SALT ^ (maze_id & MASK)) ^ (day & MASK))


def in_rect(i, j, r):
    return r[0] <= i < r[0] + r[2] and r[1] <= j < r[1] + r[3]


def node_of(i, j):
    if in_rect(i, j, HALL):
        return HALL_NODE
    if in_rect(i, j, BOSS):
        return BOSS_NODE
    return i + j * N


def edge_cells(e):
    """The two cells an edge separates, (i, j) each."""
    if e < H_EDGES:
        i, j = e % (N - 1), e // (N - 1)
        return (i, j), (i + 1, j)
    e -= H_EDGES
    i, j = e % N, e // N
    return (i, j), (i, j + 1)


def cell_edges(i, j):
    """The interior edges of a cell, ascending."""
    out = []
    if i > 0:
        out.append((i - 1) + j * (N - 1))
    if i < N - 1:
        out.append(i + j * (N - 1))
    if j > 0:
        out.append(H_EDGES + i + (j - 1) * N)
    if j < N - 1:
        out.append(H_EDGES + i + j * N)
    return sorted(out)


def generate(seed):
    """The maze of a seed: a list of EDGES booleans, True = wall."""
    rng = Rng(seed)
    nodes = [tuple(node_of(*c) for c in edge_cells(e)) for e in range(EDGES)]
    closed = [a != b for a, b in nodes]            # 1. rooms are open inside

    # 2. carve
    visited = {HALL_NODE}
    stack = [HALL_NODE]
    while stack:
        cur = stack[-1]
        cands = [e for e in range(EDGES) if closed[e]
                 and ((nodes[e][0] == cur and nodes[e][1] not in visited)
                      or (nodes[e][1] == cur and nodes[e][0] not in visited))]
        if not cands:
            stack.pop()
            continue
        e = cands[rng.below(len(cands))]
        closed[e] = False
        other = nodes[e][1] if nodes[e][0] == cur else nodes[e][0]
        visited.add(other)
        if other != BOSS_NODE:                     # the boss room: one door, a leaf
            stack.append(other)

    # 3. braid
    stuck = set()
    while True:
        dead = [(i, j) for j in range(N) for i in range(N)
                if node_of(i, j) < N * N and sum(not closed[e] for e in cell_edges(i, j)) == 1]
        free = [c for c in dead if c not in stuck]
        if len(dead) <= DEAD_ENDS or not free:
            break
        c = free[rng.below(len(free))]
        may = [e for e in cell_edges(*c) if closed[e] and BOSS_NODE not in nodes[e]]
        if not may:
            stuck.add(c)
            continue
        closed[may[rng.below(len(may))]] = False
    return closed


def fnv1a64(closed):
    h = 0xCBF29CE484222325
    for b in closed:
        h = ((h ^ (1 if b else 0)) * 0x100000001B3) & MASK
    return h


def dead_ends(closed):
    return [(i, j) for j in range(N) for i in range(N)
            if node_of(i, j) < N * N and sum(not closed[e] for e in cell_edges(i, j)) == 1]


def connected(closed):
    """Every cell reachable from the hall (the generator guarantees it; a check)."""
    seen, todo = {(0, 0)}, [(0, 0)]
    while todo:
        i, j = todo.pop()
        for e in cell_edges(i, j):
            if not closed[e]:
                a, b = edge_cells(e)
                o = b if a == (i, j) else a
                if o not in seen:
                    seen.add(o)
                    todo.append(o)
    return len(seen) == N * N


def render(closed):
    """The maze as text, north at the top: # wall or pillar, H hall, B boss room."""
    w = 2 * N + 1
    g = [["#"] * w for _ in range(w)]
    for j in range(N):
        for i in range(N):
            g[2 * j + 1][2 * i + 1] = "H" if in_rect(i, j, HALL) else ("B" if in_rect(i, j, BOSS) else " ")
    for e in range(EDGES):
        if not closed[e]:
            (ai, aj), (bi, bj) = edge_cells(e)
            room = node_of(ai, aj) == node_of(bi, bj)
            g[aj + bj + 1][ai + bi + 1] = ("H" if in_rect(ai, aj, HALL) else "B") if room else " "
    # a pillar inside a room is drawn as the room (the hall's is not placed at all)
    for rect, ch in ((HALL, "H"), (BOSS, "B")):
        for b in range(rect[1] + 1, rect[1] + rect[3]):
            for a in range(rect[0] + 1, rect[0] + rect[2]):
                g[2 * b][2 * a] = ch
    return "\n".join("".join(row) for row in reversed(g))


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--maze", type=int, action="append", help="crypt (90 Reminiscence, 91 Sorrow)")
    ap.add_argument("--day", type=int, default=0)
    ap.add_argument("--vectors", action="store_true")
    args = ap.parse_args()
    if args.vectors:
        bad = 0
        for maze_id in (90, 91):
            for day in (0, 1, 20767):
                h = fnv1a64(generate(seed_for(maze_id, day)))
                want = VECTORS.get((maze_id, day))
                state = "" if want is None else ("  ok" if want == h else "  MISMATCH")
                bad += state.endswith("MISMATCH")
                print("    (%d, %d): 0x%016X,%s" % (maze_id, day, h, state))
        return 1 if bad else 0
    for maze_id in args.maze or (90, 91):
        seed = seed_for(maze_id, args.day)
        closed = generate(seed)
        print("maze %d, day %d, seed 0x%016X: %d walls of %d edges, dead ends %s, %s, hash 0x%016X" % (
            maze_id, args.day, seed, sum(closed), EDGES, dead_ends(closed),
            "connected" if connected(closed) else "NOT CONNECTED", fnv1a64(closed)))
        print(render(closed))
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
