#pragma once

#include <emper/ComputeTypes.h>
#include <emper/Types.h>

#include <cstddef>
#include <span>

namespace emper::module {

// General-purpose simulation data describing the current Flock simulation
// state. Intentionally renderer-neutral and naming neutral: FlockData is
// consumed by rendering, debug UI, analytics, recording/replay, networking,
// AI and any other future system.
//
// It MUST NOT depend on IRenderer / IRendererShaderPipeline / SDL / ImGui /
// OpenGL renderer classes. It only references engine-level types
// (Vec2, f32) and the renderer-agnostic GPU compute backend handles
// (BufferHandle).
enum class FlockDataMode : u8
{
    CPU = 0,
    GPU = 1
};

struct FlockData
{
    FlockDataMode mode = FlockDataMode::CPU;

    // -------------------------------------------------------------
    // Common aggregate state (available in both modes).
    // -------------------------------------------------------------
    std::size_t boidCount = 0;
    f32 worldWidth = 0.0f;
    f32 worldHeight = 0.0f;

    // -------------------------------------------------------------
    // CPU mode
    //
    // Lightweight, read-only views into the live CPU simulation state.
    // Consumers must not mutate the data reachable through these spans.
    // No data is copied when producing a FlockData snapshot.
    // -------------------------------------------------------------
    std::span<const Vec2> positions;
    std::span<const Vec2> velocities;
    std::span<const std::size_t> teams;

    // -------------------------------------------------------------
    // GPU mode
    //
    // GPU buffers backing the simulation state. They are created and
    // owned by the GPU compute backend (IGPUComputeBackend) and must
    // remain valid while the owning simulation is alive. Consumers such
    // as a render pass may bind/view these buffers.
    // -------------------------------------------------------------
    emper::BufferHandle stateBuffer = 0;
    emper::BufferHandle teamBuffer = 0;
    emper::BufferHandle renderConfigBuffer = 0;
};

} // namespace emper::module