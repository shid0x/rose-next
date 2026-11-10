#pragma once

#include <string>
#include <vector>

/// The Karkia catacombs' maze: the generator, and what the server derives from a maze
/// (doc/karkia-catacombs-implementation-brief.md, section 3).
///
/// **scripts/catacomb_maze.py is the same generator in Python**, and the two must give
/// the same edges for the same seed: SelfTest checks this one against the Python test
/// vectors when the server starts. Change the algorithm, a constant or the layout in
/// both, and regenerate the vectors (`catacomb_maze.py --vectors`).
///
/// The model: a grid of N x N cells, 15 m each, from (SW_CM, SW_CM). Cells are always
/// open; the maze is which edges between neighbouring cells carry a wall. Edge ids:
///   0 .. N*(N-1)-1            between cell (i, j) and (i+1, j): i + j*(N-1)
///   N*(N-1) .. 2*N*(N-1)-1    between cell (i, j) and (i, j+1): N*(N-1) + i + j*N
/// with i east and j north. The entrance hall (2 x 2, south-west) and the boss room
/// (4 x 4, north-east, one door) are open inside.
namespace CatacombMaze {

constexpr int N = 10;
constexpr int H_EDGES = N * (N - 1);
constexpr int EDGES = 2 * H_EDGES;
constexpr float SW_CM = 504500.f; // the maze's south-west corner, both axes (import-catacombs.py)
constexpr float CELL_CM = 1500.f;
constexpr int MASK_SIDE = N * 15; // the wall mask: 1 m cells over the maze

struct Maze {
    bool closed[EDGES]; // true = a wall on that edge
};

/// The day's seed for a crypt (its zone number): a hash of a salt, the crypt and the day.
unsigned long long SeedFor(int iMazeID, long long llDay);

void Generate(unsigned long long ullSeed, Maze& out);

/// FNV-1a 64 of the edge bytes: what `catacomb_maze.py` prints for a maze.
unsigned long long Hash(const Maze& m);

/// A wall to place: the edge's midpoint (world cm) and the wall model's turn about Z.
struct Wall {
    float x, y;
    int rotDeg;
};
void Walls(const Maze& m, std::vector<Wall>& out);

/// The walls as blocked 1 m cells, MASK_SIDE x MASK_SIDE from (SW_CM, SW_CM), row 0
/// south, 1 = blocked: the cells within 3 m of a closed edge's line along its 15 m,
/// which is what the bake blocks round a wall placed in the map file.
void BlockMask(const Maze& m, std::vector<unsigned char>& out);
unsigned long long MaskHash(const std::vector<unsigned char>& mask);

/// The way from one maze cell to another through the open edges: the midpoint of each
/// doorway crossed, then the goal itself. Every leg lies inside one cell, so it is
/// clear of walls and pillars whatever the maze. False (out untouched) when either
/// point is off the maze or both are in the same cell: a plain search does that.
bool Route(const Maze& m, float x0, float y0, float x1, float y1, std::vector<tPOINTF>& out);

/// The maze as text, north at the top (# wall or pillar, H hall, B boss room): the
/// same picture `catacomb_maze.py` prints.
std::string Render(const Maze& m);

/// The generator and the mask against the Python test vectors. Returns the number of
/// mismatches and what it found, for the log.
int SelfTest(std::string& report);

} // namespace CatacombMaze
