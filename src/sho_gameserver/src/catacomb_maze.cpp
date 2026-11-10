#include "stdafx.h"

#include "catacomb_maze.h"

#include <cmath>

namespace CatacombMaze {

namespace {

struct Rect {
    int i0, j0, w, h;
};
constexpr Rect HALL = {0, 0, 2, 2};
constexpr Rect BOSS = {6, 6, 4, 4};
constexpr int DEAD_ENDS = 2; // the user, 2026-11-09: "maybe 2 for now"
constexpr unsigned long long SALT = 0x4B41524B49410001ull; // "KARKIA", version 1
constexpr int HALL_NODE = N * N, BOSS_NODE = N * N + 1;
constexpr int WALL_HALF = 3; // mask cells blocked each side of a closed edge's line
constexpr int CELL_M = 15;

// (maze id, day) -> hash, from `python scripts/catacomb_maze.py --vectors`.
struct Vector {
    int maze;
    long long day;
    unsigned long long hash;
};
constexpr Vector EDGE_VECTORS[] = {
    {90, 0, 0x9B0FAB2781071B37ull},
    {90, 1, 0xCEF29EA72B1566A1ull},
    {90, 20767, 0xD1B5106FE281976Eull},
    {91, 0, 0x6B56403122DACE98ull},
    {91, 1, 0x3F94487D6960C511ull},
    {91, 20767, 0xADB6550618BB419Dull},
};
constexpr Vector MASK_VECTORS[] = {
    {90, 0, 0xCE1F5470FBD8E490ull},
    {91, 0, 0xCE34136D42D814ABull},
};

/// splitmix64, written out: a library generator's sequence would not match Python's.
struct Rng {
    unsigned long long s;
    explicit Rng(unsigned long long seed): s(seed) {}
    unsigned long long Next() {
        s += 0x9E3779B97F4A7C15ull;
        unsigned long long z = s;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    int Below(int n) { return (int)(Next() % (unsigned long long)n); }
};

unsigned long long
Mix(unsigned long long x) {
    return Rng(x).Next();
}

bool
InRect(int i, int j, const Rect& r) {
    return r.i0 <= i && i < r.i0 + r.w && r.j0 <= j && j < r.j0 + r.h;
}

int
NodeOf(int i, int j) {
    if (InRect(i, j, HALL))
        return HALL_NODE;
    if (InRect(i, j, BOSS))
        return BOSS_NODE;
    return i + j * N;
}

/// The two cells an edge separates.
void
EdgeCells(int e, int& ai, int& aj, int& bi, int& bj) {
    if (e < H_EDGES) {
        ai = e % (N - 1);
        aj = e / (N - 1);
        bi = ai + 1;
        bj = aj;
    } else {
        e -= H_EDGES;
        ai = e % N;
        aj = e / N;
        bi = ai;
        bj = aj + 1;
    }
}

/// A cell's interior edges, ascending; returns the count.
int
CellEdges(int i, int j, int out[4]) {
    int n = 0;
    if (i > 0)
        out[n++] = (i - 1) + j * (N - 1);
    if (i < N - 1)
        out[n++] = i + j * (N - 1);
    if (j > 0)
        out[n++] = H_EDGES + i + (j - 1) * N;
    if (j < N - 1)
        out[n++] = H_EDGES + i + j * N;
    return n;
}

unsigned long long
Fnv(const unsigned char* p, size_t n) {
    unsigned long long h = 0xCBF29CE484222325ull;
    for (size_t k = 0; k < n; k++)
        h = (h ^ p[k]) * 0x100000001B3ull;
    return h;
}

} // namespace

unsigned long long
SeedFor(int iMazeID, long long llDay) {
    return Mix(Mix(SALT ^ (unsigned long long)(long long)iMazeID) ^ (unsigned long long)llDay);
}

void
Generate(unsigned long long ullSeed, Maze& out) {
    Rng rng(ullSeed);
    int nodeA[EDGES], nodeB[EDGES];
    for (int e = 0; e < EDGES; e++) {
        int ai, aj, bi, bj;
        EdgeCells(e, ai, aj, bi, bj);
        nodeA[e] = NodeOf(ai, aj);
        nodeB[e] = NodeOf(bi, bj);
        out.closed[e] = nodeA[e] != nodeB[e]; // 1. rooms are open inside
    }

    // 2. carve: a depth-first walk over nodes (a room is one node), from the hall. The
    // boss room is entered once and never walked on from: one door.
    bool visited[N * N + 2] = {};
    std::vector<int> stack;
    visited[HALL_NODE] = true;
    stack.push_back(HALL_NODE);
    int cands[EDGES];
    while (!stack.empty()) {
        const int cur = stack.back();
        int n = 0;
        for (int e = 0; e < EDGES; e++) {
            if (out.closed[e]
                && ((nodeA[e] == cur && !visited[nodeB[e]]) || (nodeB[e] == cur && !visited[nodeA[e]])))
                cands[n++] = e;
        }
        if (!n) {
            stack.pop_back();
            continue;
        }
        const int e = cands[rng.Below(n)];
        out.closed[e] = false;
        const int other = nodeA[e] == cur ? nodeB[e] : nodeA[e];
        visited[other] = true;
        if (other != BOSS_NODE)
            stack.push_back(other);
    }

    // 3. braid: open a wall at dead ends until DEAD_ENDS are left, never a wall of the
    // boss room.
    bool stuck[N * N] = {};
    for (;;) {
        int dead[N * N], nDead = 0, freeCells[N * N], nFree = 0;
        for (int j = 0; j < N; j++) {
            for (int i = 0; i < N; i++) {
                if (NodeOf(i, j) >= N * N)
                    continue;
                int edges[4];
                const int ne = CellEdges(i, j, edges);
                int open = 0;
                for (int k = 0; k < ne; k++)
                    open += out.closed[edges[k]] ? 0 : 1;
                if (open == 1) {
                    dead[nDead++] = i + j * N;
                    if (!stuck[i + j * N])
                        freeCells[nFree++] = i + j * N;
                }
            }
        }
        if (nDead <= DEAD_ENDS || !nFree)
            break;
        const int c = freeCells[rng.Below(nFree)];
        int edges[4], may[4], nMay = 0;
        const int ne = CellEdges(c % N, c / N, edges);
        for (int k = 0; k < ne; k++) {
            const int e = edges[k];
            if (out.closed[e] && nodeA[e] != BOSS_NODE && nodeB[e] != BOSS_NODE)
                may[nMay++] = e;
        }
        if (!nMay) {
            stuck[c] = true;
            continue;
        }
        out.closed[may[rng.Below(nMay)]] = false;
    }
}

unsigned long long
Hash(const Maze& m) {
    unsigned char bytes[EDGES];
    for (int e = 0; e < EDGES; e++)
        bytes[e] = m.closed[e] ? 1 : 0;
    return Fnv(bytes, EDGES);
}

void
Walls(const Maze& m, std::vector<Wall>& out) {
    out.clear();
    for (int e = 0; e < EDGES; e++) {
        if (!m.closed[e])
            continue;
        int ai, aj, bi, bj;
        EdgeCells(e, ai, aj, bi, bj);
        Wall w;
        if (e < H_EDGES) { // between east-west neighbours: the wall runs north-south
            w.x = SW_CM + (ai + 1) * CELL_CM;
            w.y = SW_CM + (aj + 0.5f) * CELL_CM;
            w.rotDeg = 90;
        } else {
            w.x = SW_CM + (ai + 0.5f) * CELL_CM;
            w.y = SW_CM + (aj + 1) * CELL_CM;
            w.rotDeg = 0;
        }
        out.push_back(w);
    }
}

void
BlockMask(const Maze& m, std::vector<unsigned char>& out) {
    out.assign((size_t)MASK_SIDE * MASK_SIDE, 0);
    for (int e = 0; e < EDGES; e++) {
        if (!m.closed[e])
            continue;
        int ai, aj, bi, bj;
        EdgeCells(e, ai, aj, bi, bj);
        int x0, x1, y0, y1;
        if (e < H_EDGES) {
            x0 = (ai + 1) * CELL_M - WALL_HALF;
            x1 = (ai + 1) * CELL_M + WALL_HALF;
            y0 = aj * CELL_M;
            y1 = aj * CELL_M + CELL_M;
        } else {
            x0 = ai * CELL_M;
            x1 = ai * CELL_M + CELL_M;
            y0 = (aj + 1) * CELL_M - WALL_HALF;
            y1 = (aj + 1) * CELL_M + WALL_HALF;
        }
        for (int y = y0; y < y1; y++) {
            for (int x = x0; x < x1; x++)
                out[(size_t)y * MASK_SIDE + x] = 1;
        }
    }
}

unsigned long long
MaskHash(const std::vector<unsigned char>& mask) {
    return Fnv(mask.data(), mask.size());
}

bool
Route(const Maze& m, float x0, float y0, float x1, float y1, std::vector<tPOINTF>& out) {
    const int si = (int)std::floor((x0 - SW_CM) / CELL_CM), sj = (int)std::floor((y0 - SW_CM) / CELL_CM);
    const int gi = (int)std::floor((x1 - SW_CM) / CELL_CM), gj = (int)std::floor((y1 - SW_CM) / CELL_CM);
    if (si < 0 || si >= N || sj < 0 || sj >= N || gi < 0 || gi >= N || gj < 0 || gj >= N)
        return false;
    const int start = si + sj * N, goal = gi + gj * N;
    if (start == goal)
        return false;

    // breadth-first from the goal, so each cell's parent is its next step towards it
    int next[N * N], via[N * N]; // the next cell, and the edge crossed to reach it
    for (int c = 0; c < N * N; c++)
        next[c] = -1;
    int queue[N * N], head = 0, tail = 0;
    next[goal] = goal;
    queue[tail++] = goal;
    while (head < tail && next[start] < 0) {
        const int c = queue[head++];
        const int i = c % N, j = c / N;
        int edges[4];
        const int ne = CellEdges(i, j, edges);
        for (int k = 0; k < ne; k++) {
            const int e = edges[k];
            if (m.closed[e])
                continue;
            int ai, aj, bi, bj;
            EdgeCells(e, ai, aj, bi, bj);
            const int o = (ai + aj * N == c) ? bi + bj * N : ai + aj * N;
            if (next[o] < 0) {
                next[o] = c;
                via[o] = e;
                queue[tail++] = o;
            }
        }
    }
    if (next[start] < 0)
        return false; // cannot happen: every maze is connected

    std::vector<tPOINTF> points;
    for (int c = start; c != goal; c = next[c]) {
        int ai, aj, bi, bj;
        EdgeCells(via[c], ai, aj, bi, bj);
        tPOINTF p;
        if (via[c] < H_EDGES) {
            p.x = SW_CM + (ai + 1) * CELL_CM;
            p.y = SW_CM + (aj + 0.5f) * CELL_CM;
        } else {
            p.x = SW_CM + (ai + 0.5f) * CELL_CM;
            p.y = SW_CM + (aj + 1) * CELL_CM;
        }
        // a doorway on the straight line between its neighbours is not a corner
        const size_t n = points.size();
        if (n >= 2) {
            const float cross = (points[n - 1].x - points[n - 2].x) * (p.y - points[n - 2].y)
                                - (points[n - 1].y - points[n - 2].y) * (p.x - points[n - 2].x);
            if (cross == 0.f)
                points.pop_back();
        }
        points.push_back(p);
    }
    tPOINTF end;
    end.x = x1;
    end.y = y1;
    points.push_back(end);
    out.swap(points);
    return true;
}

std::string
Render(const Maze& m) {
    const int w = 2 * N + 1;
    std::vector<std::string> g(w, std::string(w, '#'));
    for (int j = 0; j < N; j++) {
        for (int i = 0; i < N; i++)
            g[2 * j + 1][2 * i + 1] = InRect(i, j, HALL) ? 'H' : (InRect(i, j, BOSS) ? 'B' : ' ');
    }
    for (int e = 0; e < EDGES; e++) {
        if (m.closed[e])
            continue;
        int ai, aj, bi, bj;
        EdgeCells(e, ai, aj, bi, bj);
        const bool room = NodeOf(ai, aj) == NodeOf(bi, bj);
        g[aj + bj + 1][ai + bi + 1] = room ? (InRect(ai, aj, HALL) ? 'H' : 'B') : ' ';
    }
    for (const Rect& r: {HALL, BOSS}) {
        for (int b = r.j0 + 1; b < r.j0 + r.h; b++) {
            for (int a = r.i0 + 1; a < r.i0 + r.w; a++)
                g[2 * b][2 * a] = r.i0 == HALL.i0 ? 'H' : 'B'; // no pillar inside a room
        }
    }
    std::string s;
    for (int row = w - 1; row >= 0; row--) {
        s += g[row];
        s += '\n';
    }
    return s;
}

int
SelfTest(std::string& report) {
    int bad = 0;
    Maze m;
    for (const Vector& v: EDGE_VECTORS) {
        Generate(SeedFor(v.maze, v.day), m);
        if (Hash(m) != v.hash) {
            bad++;
            report += fmt::format(" maze ({}, {}) is {:016X}, want {:016X};", v.maze, v.day, Hash(m), v.hash);
        }
    }
    std::vector<unsigned char> mask;
    for (const Vector& v: MASK_VECTORS) {
        Generate(SeedFor(v.maze, v.day), m);
        BlockMask(m, mask);
        if (MaskHash(mask) != v.hash) {
            bad++;
            report += fmt::format(" mask ({}, {}) is {:016X}, want {:016X};", v.maze, v.day, MaskHash(mask), v.hash);
        }
    }
    if (!bad)
        report = fmt::format("{} mazes and {} wall masks match scripts/catacomb_maze.py",
            sizeof(EDGE_VECTORS) / sizeof(Vector),
            sizeof(MASK_VECTORS) / sizeof(Vector));
    return bad;
}

} // namespace CatacombMaze
