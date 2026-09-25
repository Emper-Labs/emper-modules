#pragma once

#include <emper/ComputeTypes.h>
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

// Execution/consumption mode for the simulation state.
enum class GameOfLifeDataMode : u8
{
    CPU = 0,
    GPU = 1
};

// General-purpose simulation data describing the current Conway's Game of
// Life state. Intentionally renderer-neutral: GameOfLifeData is consumed by
// rendering, debug UI, analytics, recording/replay, and any other future
// system.
//
// It MUST NOT depend on IRenderer / SDL / OpenGL or any backend class. It
// only references engine-level types (u8, i32) and std::span, plus the
// renderer-agnostic GPU compute backend handles (BufferHandle) for GPU mode.
//
// Designed after the Flock module's FlockData: a `mode` discriminates between
// a CPU section (read-only host spans) and a GPU section (opaque GPU buffer
// handles). Consumers pick the branch matching `mode`; the GPU branch never
// requires the host to pull the simulation grid back for processing.
//
// -------------------------------------------------------------
// CPU mode (`mode == CPU`)
// -------------------------------------------------------------
// `aliveCells` is a lightweight, read-only list of the live cells in the
// current generation. Each backend produces it with the same complexity as
// its native iteration (Sparse/Packed iterate only live cells = O(live
// cells); the Dense backend scans its grid = O(width*height)). This
// deliberately avoids materializing or scanning the full (potentially huge)
// grid every frame. Consumers must not mutate the data reachable through the
// span.
//
// -------------------------------------------------------------
// GPU mode (`mode == GPU`)
// -------------------------------------------------------------
// `gridBuffer` backs the current generation (a flat row-major array of
// u32, 0/1, `width * height` entries; index = y * width + x). It is owned by
// the GPU compute backend and must remain valid while the owning simulation
// is alive. `renderConfigBuffer` holds a vec4
// (surfaceWidth, surfaceHeight, gridWidth, gridHeight) describing how to
// lay the grid out on a drawable surface.
struct GameOfLifeData
{
    GameOfLifeDataMode mode = GameOfLifeDataMode::CPU;

    std::size_t width  = 0;
    std::size_t height = 0;

    std::size_t generation = 0;

    // -------------------------------------------------------------
    // CPU mode
    // -------------------------------------------------------------
    std::span<const CellCoordinate> aliveCells;

    // -------------------------------------------------------------
    // GPU mode
    // -------------------------------------------------------------
    emper::BufferHandle gridBuffer = 0;
    emper::BufferHandle renderConfigBuffer = 0;
};

} // namespace emper::module::cgol