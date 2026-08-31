#pragma once

#include <emper/Types.h>

#include <cstddef>
#include <span>

namespace emper::module::cgol
{

// A live/dead cell in the current generation. Stored densely as 0/1.
using Cell = emper::u8;

// Integer cell coordinates within the world grid.
struct CellCoordinate
{
    emper::i32 x;
    emper::i32 y;
};

// General-purpose simulation data describing the current Conway's Game of
// Life state. Intentionally renderer-neutral: GameOfLifeData is consumed by
// rendering, debug UI, analytics, recording/replay, and any other future
// system.
//
// It MUST NOT depend on IRenderer / SDL / OpenGL or any backend class. It
// only references engine-level types (u8, i32) and std::span.
//
// `aliveCells` is a lightweight, read-only list of the live cells in the
// current generation. Each backend produces it with the same complexity as
// its native iteration (Sparse/Packed iterate only live cells = O(live
// cells); the Dense backend scans its grid = O(width*height), matching its
// rendering cost). This deliberately avoids materializing or scanning the
// full (potentially huge) grid every frame. Consumers must not mutate the
// data reachable through the span.
struct GameOfLifeData
{
    std::size_t width  = 0;
    std::size_t height = 0;

    std::size_t generation = 0;

    // Live cells in the current generation (1 entry per alive cell).
    std::span<const CellCoordinate> aliveCells;
};

} // namespace emper::module::cgol