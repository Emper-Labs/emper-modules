#pragma once

#include "FlockFrameStats.h"

#include <emper/Types.h>

#include <memory>

namespace emper::interfaces::backend {
class IRenderer;
}

namespace emper::simulation::world {
class World;
}

namespace emper::module {

struct FlockConfig;

// Owns the CPU-side flock storage, neighbour search and simulation update.
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
    void render(interfaces::backend::IRenderer& renderer);

    // Fills the average candidates/boid and average neighbours/boid
    // for the most recent tick.
    bool lastFrameStats(FlockFrameStats& out) const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace emper::module