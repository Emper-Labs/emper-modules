#include "GpuCompute.h"

#include <emper/interfaces/backend/ICompute.h>
#include <emper/interfaces/backend/IRenderer.h>

namespace emper::module {

FlockGpuCompute::FlockGpuCompute(
    simulation::world::World& world,
    FlockConfig& config,
    interfaces::backend::IGPUComputeBackend* backend)
    : world_(world), config_(config), backend_(backend)
{
}

bool FlockGpuCompute::initialize()
{
    // TODO: Allocate buffers and compile the flock compute program through
    // IGPUComputeBackend. False keeps Auto/GPU on CPU until this is complete.
    return false;
}

bool FlockGpuCompute::isAvailable() const noexcept
{
    return false;
}

void FlockGpuCompute::tick(f32 dt)
{
    // TODO: Upload config changes, dispatch compute, then issue a barrier.
    static_cast<void>(dt);
    static_cast<void>(world_);
    static_cast<void>(config_);
    static_cast<void>(backend_);
}

void FlockGpuCompute::render(interfaces::backend::IRenderer& renderer)
{
    // TODO: Expose the resulting GPU boid buffer to a graphics pipeline.
    static_cast<void>(renderer);
}

} // namespace emper::module
