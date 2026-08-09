#pragma once

#include <emper/Types.h>

namespace emper::interfaces::backend {
class IGPUComputeBackend;
class IRenderer;
}

namespace emper::simulation::world {
class World;
}

namespace emper::module {

struct FlockConfig;

// Placeholder for a future GPU implementation. It only depends on engine
// interfaces so a concrete backend can be injected without coupling modules.
class FlockGpuCompute final
{
public:
    FlockGpuCompute(
        simulation::world::World& world,
        FlockConfig& config,
        interfaces::backend::IGPUComputeBackend* backend = nullptr
    );

    bool initialize();
    bool isAvailable() const noexcept;
    void tick(f32 dt);
    void render(interfaces::backend::IRenderer& renderer);

private:
    simulation::world::World& world_;
    FlockConfig& config_;
    interfaces::backend::IGPUComputeBackend* backend_ = nullptr;
};

} // namespace emper::module
