#pragma once

#include "FlockData.h"
#include "FlockFrameStats.h"

#include <emper/Types.h>

#include <memory>

namespace emper::simulation::world {
class World;
}

namespace emper::module {

struct FlockConfig;

// Owns the CPU-side flock storage, neighbour search and simulation update.
// This is a pure simulation component: it knows nothing about rendering.
class FlockCpuCompute final
{
public:
    FlockCpuCompute(
        simulation::world::World& world,
        FlockConfig& config
    );
    ~FlockCpuCompute();

    FlockCpuCompute(const FlockCpuCompute&) = delete;
    FlockCpuCompute& operator=(const FlockCpuCompute&) = delete;

    void tick(f32 dt);

    // Read-only snapshot of the current CPU simulation state.
    FlockData data() const;

    // Returns false: CPU mode does not collect per-boid benchmark
    // statistics on the host.
    bool lastFrameStats(FlockFrameStats& out) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace emper::module