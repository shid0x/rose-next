#include "stdafx.h"

#include "nav/nav_grid.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <intrin.h>

namespace Nav {

namespace {

constexpr int TC = 160; // cells per tile side
constexpr int TV = 65; // height vertices per tile side
constexpr int NCELL = TC * TC;
constexpr float CELL_CM = 100.f;
constexpr float VERT_CM = 250.f;
constexpr float CHUNK_CM = 16000.f;
constexpr short NO_HEIGHT = -32768;
constexpr float FOLLOW_SAMPLE_CM = 50.f; // half a cell: no cell is skipped along a step

#pragma pack(push, 1)
struct FileSpan {
    short z;
    unsigned char flags;
    unsigned char clearance;
    unsigned short component;
    unsigned char below, above; ///< version 2: the floor's surface spans z - below .. z + above
};
#pragma pack(pop)
static_assert(sizeof(FileSpan) == 8, "lnav span record is 8 bytes");
constexpr size_t FILE_SPAN_V1 = 6; // version 1: no below/above

uint32_t
crc32_of(const std::vector<unsigned char>& d) {
    static uint32_t s_Table[256];
    static bool s_bInit = [] {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            s_Table[i] = c;
        }
        return true;
    }();
    (void)s_bInit;
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char b: d)
        c = s_Table[(c ^ b) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

bool
read_file(const std::string& path, std::vector<unsigned char>& out) {
    FILE* fp = fopen(path.c_str(), "rb");
    if (!fp)
        return false;
    fseek(fp, 0, SEEK_END);
    const long n = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    out.resize(n > 0 ? (size_t)n : 0);
    const size_t got = n > 0 ? fread(out.data(), 1, (size_t)n, fp) : 0;
    fclose(fp);
    return got == out.size();
}

struct Cursor {
    const unsigned char* p;
    const unsigned char* end;
    bool ok = true;

    template <class T>
    T get() {
        T v{};
        if (end - p < (ptrdiff_t)sizeof(T)) {
            ok = false;
            return v;
        }
        memcpy(&v, p, sizeof(T));
        p += sizeof(T);
        return v;
    }
    const unsigned char* take(size_t n) {
        if ((size_t)(end - p) < n) {
            ok = false;
            return nullptr;
        }
        const unsigned char* r = p;
        p += n;
        return r;
    }
};

std::string
last_folder(const std::string& dir) {
    std::string d = dir;
    while (!d.empty() && (d.back() == '/' || d.back() == '\\'))
        d.pop_back();
    const size_t s = d.find_last_of("/\\");
    return s == std::string::npos ? d : d.substr(s + 1);
}

std::string
with_slash(const std::string& dir) {
    if (!dir.empty() && dir.back() != '/' && dir.back() != '\\')
        return dir + "/";
    return dir;
}

} // namespace

struct Grid::Tile {
    struct Cell {
        short visualTop;
        unsigned char n;
        unsigned char pad;
        uint32_t first;
    };

    short sx = 0, sy = 0;
    short heights[TV * TV];
    unsigned char simple[NCELL / 4];
    unsigned char complexBits[NCELL / 8];
    unsigned short rank32[NCELL / 32]; // complex cells before each 32-cell word
    unsigned short mainComponent = 0;
    std::vector<unsigned short> components; // per cell, or empty (mainComponent covers it)
    std::vector<Cell> cells;
    std::vector<FileSpan> spans;

    int ComplexIndex(int i) const {
        uint32_t w;
        memcpy(&w, complexBits + (i >> 5) * 4, 4);
        const uint32_t bit = 1u << (i & 31);
        if (!(w & bit))
            return -1;
        return rank32[i >> 5] + (int)__popcnt(w & (bit - 1));
    }
    int SimpleClass(int i) const { return (simple[i >> 2] >> ((i & 3) * 2)) & 3; }
};

Grid::Grid() {
    memset(m_pTiles, 0, sizeof(m_pTiles));
}

Grid::~Grid() {
    for (auto& row: m_pTiles)
        for (Tile* t: row)
            delete t;
}

std::unique_ptr<Grid>
Grid::Load(const std::string& zoneDir, const std::string& baseDir, std::string& err, int& staleInputs) {
    staleInputs = 0;
    const std::string dir = with_slash(zoneDir);
    const std::string path = dir + last_folder(zoneDir) + ".lnav";
    std::vector<unsigned char> data;
    if (!read_file(path, data)) {
        err = "no " + path;
        return nullptr;
    }

    Cursor c{data.data(), data.data() + data.size()};
    const unsigned char* magic = c.take(4);
    if (!magic || memcmp(magic, "LNAV", 4) != 0) {
        err = path + ": not an LNAV file";
        return nullptr;
    }
    const unsigned short version = c.get<unsigned short>();
    const unsigned short cellCm = c.get<unsigned short>();
    if ((version < 1 || version > 3) || cellCm != (unsigned short)CELL_CM) {
        err = path + ": unsupported version or cell size (re-bake with scripts/nav-bake.py)";
        return nullptr;
    }
    for (int i = 0; i < 4; ++i)
        c.get<short>(); // the bake's rules, for the record
    const short objStep = version >= 3 ? c.get<short>() : (short)OBJ_STEP_CM; // version 3: the map's own

    // the bake's inputs: a changed map file means the grid no longer matches the map
    const unsigned short nInputs = c.get<unsigned short>();
    const std::string base = with_slash(baseDir);
    for (int i = 0; i < nInputs && c.ok; ++i) {
        const unsigned char len = c.get<unsigned char>();
        const unsigned char* name = c.take(len);
        const uint32_t crc = c.get<uint32_t>();
        if (!c.ok)
            break;
        const std::string n((const char*)name, len);
        const std::string file = (n.find('/') != std::string::npos ? base : dir) + n;
        std::vector<unsigned char> input;
        if (!read_file(file, input) || crc32_of(input) != crc)
            ++staleInputs;
    }

    struct Index {
        short sx, sy;
        uint32_t off, size;
    };
    const unsigned short nTiles = c.get<unsigned short>();
    std::vector<Index> index(nTiles);
    for (auto& e: index) {
        e.sx = c.get<short>();
        e.sy = c.get<short>();
        e.off = c.get<uint32_t>();
        e.size = c.get<uint32_t>();
    }
    const uint32_t nAnchors = c.get<uint32_t>();
    std::unique_ptr<Grid> grid(new Grid());
    grid->m_fObjStep = objStep > 0 ? (float)objStep : OBJ_STEP_CM;
    grid->m_Anchors.resize(c.ok ? nAnchors : 0);
    for (Anchor& a: grid->m_Anchors) {
        a.x = c.get<float>();
        a.y = c.get<float>();
        a.z = c.get<short>();
        a.flags = c.get<unsigned short>();
    }
    if (!c.ok) {
        err = path + ": truncated header";
        return nullptr;
    }
    std::sort(grid->m_Anchors.begin(), grid->m_Anchors.end(),
        [](const Anchor& a, const Anchor& b) { return a.x < b.x; });
    grid->m_Bytes = sizeof(Grid) + grid->m_Anchors.capacity() * sizeof(Anchor);
    for (const Index& e: index) {
        if (e.sx < 0 || e.sx >= 64 || e.sy < 0 || e.sy >= 64 || (size_t)e.off + e.size > data.size()) {
            err = path + ": bad tile index";
            return nullptr;
        }
        Cursor t{data.data() + e.off, data.data() + e.off + e.size};
        std::unique_ptr<Tile> tile(new Tile());
        tile->sx = e.sx;
        tile->sy = e.sy;
        const unsigned char* h = t.take(sizeof(tile->heights));
        const unsigned char* s = t.take(sizeof(tile->simple));
        const unsigned char* cb = t.take(sizeof(tile->complexBits));
        if (!t.ok) {
            err = path + ": truncated tile";
            return nullptr;
        }
        memcpy(tile->heights, h, sizeof(tile->heights));
        memcpy(tile->simple, s, sizeof(tile->simple));
        memcpy(tile->complexBits, cb, sizeof(tile->complexBits));
        tile->mainComponent = t.get<unsigned short>();
        if (t.get<unsigned char>()) {
            const unsigned char* comp = t.take(NCELL * sizeof(unsigned short));
            if (comp) {
                tile->components.resize(NCELL);
                memcpy(tile->components.data(), comp, NCELL * sizeof(unsigned short));
            }
        }
        const uint32_t nComplex = t.get<uint32_t>();
        unsigned short run = 0;
        for (int w = 0; w < NCELL / 32; ++w) {
            tile->rank32[w] = run;
            uint32_t bits;
            memcpy(&bits, tile->complexBits + w * 4, 4);
            run = (unsigned short)(run + __popcnt(bits));
        }
        if (!t.ok || run != nComplex) {
            err = path + ": complex cell count does not match its bitmap";
            return nullptr;
        }
        tile->cells.resize(nComplex);
        uint32_t first = 0;
        for (auto& cell: tile->cells) {
            cell.visualTop = t.get<short>();
            cell.n = t.get<unsigned char>();
            cell.pad = t.get<unsigned char>();
            cell.first = first;
            first += cell.n;
        }
        const size_t spanSize = version >= 2 ? sizeof(FileSpan) : FILE_SPAN_V1;
        const unsigned char* sp = t.take((size_t)first * spanSize);
        if (!t.ok || t.p != t.end) {
            err = path + ": bad span block";
            return nullptr;
        }
        tile->spans.resize(first);
        for (uint32_t i = 0; i < first; ++i) {
            FileSpan& fs = tile->spans[i];
            fs.below = fs.above = 0;
            memcpy(&fs, sp + i * spanSize, spanSize);
        }
        grid->m_Bytes += sizeof(Tile) + tile->components.capacity() * sizeof(unsigned short)
            + tile->cells.capacity() * sizeof(Tile::Cell) + tile->spans.capacity() * sizeof(FileSpan);
        grid->m_pTiles[e.sy][e.sx] = tile.release();
        ++grid->m_iTiles;
    }
    return grid;
}

const Grid::Tile*
Grid::TileAt(float x, float y, int& cell) const {
    const int sx = (int)floorf(x / CHUNK_CM), sy = (int)floorf(y / CHUNK_CM);
    if (sx < 0 || sx >= 64 || sy < 0 || sy >= 64)
        return nullptr;
    const Tile* t = m_pTiles[sy][sx];
    if (!t)
        return nullptr;
    int c = (int)((x - sx * CHUNK_CM) / CELL_CM), r = (int)((y - sy * CHUNK_CM) / CELL_CM);
    c = c < 0 ? 0 : (c >= TC ? TC - 1 : c);
    r = r < 0 ? 0 : (r >= TC ? TC - 1 : r);
    cell = r * TC + c;
    return t;
}

float
Grid::TerrainZ(float x, float y) const {
    int cell;
    const Tile* t = TileAt(x, y, cell);
    if (!t)
        return NO_Z;
    const float fx = (x - t->sx * CHUNK_CM) / VERT_CM, fy = (y - t->sy * CHUNK_CM) / VERT_CM;
    int ix = (int)fx, iy = (int)fy;
    ix = ix < 0 ? 0 : (ix > TV - 2 ? TV - 2 : ix);
    iy = iy < 0 ? 0 : (iy > TV - 2 ? TV - 2 : iy);
    const float tx = fx - ix, ty = fy - iy;
    const short* h = t->heights;
    return h[iy * TV + ix] * (1 - tx) * (1 - ty) + h[iy * TV + ix + 1] * tx * (1 - ty)
        + h[(iy + 1) * TV + ix] * (1 - tx) * ty + h[(iy + 1) * TV + ix + 1] * tx * ty;
}

// A terrain floor's stored height is its cell centre's, or a slab merged into it. Off the
// centre the exact terrain is better, unless the floor is that slab.
float
Grid::CellZ(const Tile& t, int cell, float x, float y, float stored, unsigned char flags) const {
    if (!(flags & SPAN_TERRAIN))
        return stored;
    const float cx = t.sx * CHUNK_CM + (cell % TC + 0.5f) * CELL_CM;
    const float cy = t.sy * CHUNK_CM + (cell / TC + 0.5f) * CELL_CM;
    if (stored > TerrainZ(cx, cy) + 5.f)
        return stored;
    return TerrainZ(x, y);
}

int
Grid::SpansAt(float x, float y, Span* out, int iMax) const {
    int cell;
    const Tile* t = TileAt(x, y, cell);
    if (!t || iMax <= 0)
        return 0;
    const int ci = t->ComplexIndex(cell);
    if (ci < 0) {
        const int cls = t->SimpleClass(cell);
        out[0].z = out[0].lo = out[0].hi = TerrainZ(x, y);
        out[0].flags = SPAN_TERRAIN | SPAN_UP | (cls ? (SPAN_WALK | SPAN_REACHED) : 0);
        out[0].clearance = (unsigned char)cls;
        out[0].component =
            cls ? (t->components.empty() ? t->mainComponent : t->components[cell]) : 0;
        Masked(x, y, out, 1);
        return 1;
    }
    const Tile::Cell& rec = t->cells[ci];
    int n = 0;
    for (int k = 0; k < rec.n && n < iMax; ++k, ++n) {
        const FileSpan& s = t->spans[rec.first + k];
        out[n].z = CellZ(*t, cell, x, y, s.z, s.flags);
        out[n].lo = (s.flags & SPAN_TERRAIN) ? out[n].z : (float)s.z - s.below;
        out[n].hi = (s.flags & SPAN_TERRAIN) ? out[n].z : (float)s.z + s.above;
        out[n].flags = s.flags;
        out[n].clearance = s.clearance;
        out[n].component = s.component;
    }
    Masked(x, y, out, n);
    return n;
}

void
Grid::SetBlockMask(float x0, float y0, int w, int h, const unsigned char* cells) {
    if (!cells || w <= 0 || h <= 0) {
        m_pMask.store(nullptr, std::memory_order_release);
        return;
    }
    std::unique_ptr<BlockMask> m(new BlockMask());
    m->x0 = x0;
    m->y0 = y0;
    m->w = w;
    m->h = h;
    m->cells.assign(cells, cells + (size_t)w * h);
    m_pMask.store(m.get(), std::memory_order_release);
    m_Masks.push_back(std::move(m));
}

// A cell under the block mask keeps its floors and loses SPAN_WALK.
void
Grid::Masked(float x, float y, Span* out, int n) const {
    const BlockMask* m = m_pMask.load(std::memory_order_acquire);
    if (!m)
        return;
    const int cx = (int)floorf((x - m->x0) / 100.f), cy = (int)floorf((y - m->y0) / 100.f);
    if (cx < 0 || cx >= m->w || cy < 0 || cy >= m->h || !m->cells[(size_t)cy * m->w + cx])
        return;
    for (int k = 0; k < n; ++k)
        out[k].flags &= (unsigned char)~SPAN_WALK;
}

float
Grid::VisualTopZ(float x, float y) const {
    int cell;
    const Tile* t = TileAt(x, y, cell);
    if (!t)
        return NO_Z;
    const int ci = t->ComplexIndex(cell);
    if (ci >= 0 && t->cells[ci].visualTop != NO_HEIGHT)
        return t->cells[ci].visualTop;
    return TerrainZ(x, y);
}

float
Grid::Pick(const Span* spans, int n, float curZ) const {
    // the highest up-facing floor within a step up, else the lowest one (the walker falls); a
    // cell with no up-facing floor at all (rare: kept reached surfaces) uses every one
    bool bAnyUp = false;
    for (int k = 0; k < n && !bAnyUp; ++k)
        bAnyUp = (spans[k].flags & SPAN_UP) != 0;
    float best = 0.f, lowest = 0.f;
    bool found = false, bLowest = false;
    for (int k = 0; k < n; ++k) {
        if (bAnyUp && !(spans[k].flags & SPAN_UP))
            continue;
        if (!bLowest || spans[k].z < lowest) {
            lowest = spans[k].z;
            bLowest = true;
        }
        if (spans[k].z <= curZ + STEP_CM && (!found || spans[k].z > best)) {
            best = spans[k].z;
            found = true;
        }
    }
    return found ? best : lowest;
}

float
Grid::FollowZ(float fromZ, float fromX, float fromY, float toX, float toY) const {
    Span spans[32];
    const float dx = toX - fromX, dy = toY - fromY;
    const float len = sqrtf(dx * dx + dy * dy);
    const int steps = (int)ceilf(len / FOLLOW_SAMPLE_CM);
    float z = fromZ;
    for (int i = 1; i <= steps; ++i) {
        const float f = (float)i / steps;
        const int n = SpansAt(fromX + dx * f, fromY + dy * f, spans, 32);
        if (n)
            z = Pick(spans, n, z);
    }
    if (steps == 0) {
        const int n = SpansAt(toX, toY, spans, 32);
        if (n)
            z = Pick(spans, n, z);
    }
    return z;
}

float
Grid::AnchorZ(float x, float y) const {
    // the server computes a regen point's position as the bake did, give or take a float's
    // last bit: match within a metre
    constexpr float MATCH_CM = 100.f;
    auto it = std::lower_bound(m_Anchors.begin(), m_Anchors.end(), x - MATCH_CM,
        [](const Anchor& a, float v) { return a.x < v; });
    for (; it != m_Anchors.end() && it->x <= x + MATCH_CM; ++it) {
        if (fabsf(it->y - y) <= MATCH_CM) {
            return it->z;
        }
    }
    return NO_Z;
}

bool
Grid::NearestSpan(float x, float y, float z, float within, Span& out, unsigned char mask) const {
    Span spans[32];
    const int n = SpansAt(x, y, spans, 32);
    float best = within;
    bool found = false;
    for (int k = 0; k < n; ++k) {
        if ((spans[k].flags & mask) != mask)
            continue;
        const float d = fabsf(spans[k].z - z);
        if (d <= best) {
            best = d;
            out = spans[k];
            found = true;
        }
    }
    return found;
}

bool
Grid::LineWalk(float x0, float y0, float z0, float x1, float y1, float* pLastX, float* pLastY,
    float* pLastZ) const {
    constexpr unsigned char WALKABLE = SPAN_WALK | SPAN_REACHED;
    Span spans[32];
    int n = SpansAt(x0, y0, spans, 32);
    if (pLastX) {
        *pLastX = x0;
        *pLastY = y0;
    }
    if (!n)
        return false;

    // the start: the floor nearest the walker's height, whatever its flags
    int k0 = 0;
    for (int k = 1; k < n; ++k)
        if (fabsf(spans[k].z - z0) < fabsf(spans[k0].z - z0))
            k0 = k;
    Span cur = spans[k0];
    if (pLastZ)
        *pLastZ = cur.z;

    // every 1 m cell the segment crosses (Amanatides & Woo), 4-connected
    int cx = (int)floorf(x0 / CELL_CM), cy = (int)floorf(y0 / CELL_CM);
    const int ex = (int)floorf(x1 / CELL_CM), ey = (int)floorf(y1 / CELL_CM);
    const float dx = x1 - x0, dy = y1 - y0;
    const int sx = dx > 0 ? 1 : -1, sy = dy > 0 ? 1 : -1;
    const float INF = 1.0e30f;
    float tMaxX = dx != 0 ? (((dx > 0 ? cx + 1 : cx) * CELL_CM - x0) / dx) : INF;
    float tMaxY = dy != 0 ? (((dy > 0 ? cy + 1 : cy) * CELL_CM - y0) / dy) : INF;
    const float tDX = dx != 0 ? CELL_CM / fabsf(dx) : INF, tDY = dy != 0 ? CELL_CM / fabsf(dy) : INF;
    // exactly |ex - cx| + |ey - cy| steps, so float error near a boundary cannot step past the
    // end cell
    for (int steps = abs(ex - cx) + abs(ey - cy); steps > 0; --steps) {
        if (cy == ey || (cx != ex && tMaxX < tMaxY)) {
            cx += sx;
            tMaxX += tDX;
        } else {
            cy += sy;
            tMaxY += tDY;
        }
        const float px = (cx + 0.5f) * CELL_CM, py = (cy + 0.5f) * CELL_CM;
        n = SpansAt(px, py, spans, 32);
        int best = -1;
        float bestD = INF;
        for (int k = 0; k < n; ++k) {
            if ((spans[k].flags & WALKABLE) != WALKABLE)
                continue;
            const float d = fabsf(spans[k].z - cur.z);
            if (d < bestD && StepLinks(cur, spans[k], m_fObjStep)) {
                best = k;
                bestD = d;
            }
        }
        if (best < 0)
            return false;
        cur = spans[best];
        if (pLastX) {
            *pLastX = px;
            *pLastY = py;
        }
        if (pLastZ)
            *pLastZ = cur.z;
    }
    if (pLastX) {
        *pLastX = x1;
        *pLastY = y1;
    }
    return true;
}

} // namespace Nav
