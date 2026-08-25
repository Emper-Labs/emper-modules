#pragma once

#include "FlockData.h"
#include "FlockFrameStats.h"

#include <emper/ComputeTypes.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace emper::interfaces::backend
{
class IGPUComputeBackend;
}

namespace emper::simulation::world
{
class World;
}

namespace emper::module
{

struct FlockConfig;

// GPU implementation using only the engine compute backend interface.
class FlockGpuCompute final
{
public:
    FlockGpuCompute(
        simulation::world::World& world,
        FlockConfig& config,
        interfaces::backend::IGPUComputeBackend* backend = nullptr);

    ~FlockGpuCompute();

    FlockGpuCompute(const FlockGpuCompute&) = delete;
    FlockGpuCompute& operator=(const FlockGpuCompute&) = delete;

    bool initialize();
    bool isAvailable() const noexcept;

    void shutdown();
    void tick(f32 dt);

    // Enables per-boid candidate-check counting in pass 4.
    // Must be set before the first tick (the uniform is cached).
    void setBenchmarkEnabled(bool enabled) noexcept;

    // Reads back the per-boid candidate-check counts written when
    // benchmark mode is enabled. Fills `out` with boidCount_ uints.
    // Returns false if benchmark mode is disabled or the read fails.
    bool readCandidateCounts(
        std::vector<std::uint32_t>& out);

    // Fills the average candidates/boid and average neighbours/boid
    // for the most recent tick. Returns false if benchmark readback
    // is unavailable.
    bool lastFrameStats(FlockFrameStats& out) const;

    // Read-only snapshot of the current GPU simulation state/resources.
    FlockData data() const;

private:
    simulation::world::World& world_;
    FlockConfig& config_;

    interfaces::backend::IGPUComputeBackend* computeBackend_ =
        nullptr;


    // =========================================================
    // Compute programs
    // =========================================================

    emper::ProgramHandle computeProgram_ = 0;


    // =========================================================
    // Boid state
    //
    // Fixed-role buffers (no swap between ticks):
    //
    //   stateBuffers_[0] = persistent integrated state
    //                      (also the rendered buffer)
    //   stateBuffers_[1] = per-frame cell-sorted scratch
    //
    // Two state-touching passes run in opposite directions each
    // tick, so a single swap ping-pong does not apply:
    //
    //   PASS 3 reads buffer 0 and scatters into buffer 1
    //          (cell-contiguous order)
    //   PASS 4 reads buffer 1 (sequential neighbour access)
    //          and writes integrated results back into buffer 0
    //
    // =========================================================

    emper::BufferHandle stateBuffers_[2]{};

    emper::BufferHandle teamBuffers_[2]{};


    // =========================================================
    // GPU spatial grid
    //
    // cellCountBuffers_[2]:
    //
    //   Exact per-cell occupancy (64-bit atomics usage is not
    //   required).
    //
    //   cellCountsA (binding 4): occupancy used by the prefix sum.
    //   cellCountsB (binding 5): counter used by the reorder pass.
    //
    // cellStartBuffer_:
    //
    //   cellStart      [cellCount_ + 1]
    //   cellStart[c]     = first sorted index of cell c
    //   cellStart[c + 1] = one-past-the-end of cell c
    //
    // benchmarkBuffer_:
    //
    //   Optional per-boid candidate-check count written during
    //   pass 4 when benchark mode is enabled. Sized
    //   [boidCount_] uints; read back to host for statistics.
    //
    // =========================================================

    emper::BufferHandle cellCountBuffers_[2]{};

    emper::BufferHandle cellStartBuffer_ = 0;

    emper::BufferHandle benchmarkBuffer_ = 0;


    // =========================================================
    // Render configuration
    // =========================================================

    emper::BufferHandle renderConfigBuffer_ = 0;


    // =========================================================
    // Simulation dimensions
    // =========================================================

    std::size_t boidCount_ = 0;

    std::size_t cellCount_ = 0;

    int gridColumns_ = 0;
    int gridRows_ = 0;


    // =========================================================
    // Benchmark mode
    // =========================================================

    bool benchmark_ = false;
    mutable FlockFrameStats lastStats_;


    // =========================================================
    // State
    // =========================================================

    bool uniformsConfigured_ = false;
    bool initialized_ = false;
    bool configured_ = false;
};

} // namespace emper::module