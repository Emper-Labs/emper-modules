#pragma once

#include <emper/interfaces/module/ISystem.h>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace emper::simulation::world {
class World;
}

namespace emper::interfaces::backend {
class IGPUComputeBackend;
}

namespace emper::module {

struct FlockConfig : interfaces::module::ISystemConfig
{
    std::size_t boidCount = 100'000;

    f32 worldWidth = 1280.0f;
    f32 worldHeight = 720.0f;

    f32 maxSpeed = 100.0f;
    f32 maxForce = 30.0f;
    f32 initialSpeed = 60.0f;

    f32 perceptionRadius = 45.0f;
    f32 separationRadius = 12.0f;

    // Cohesion
    f32 cohesionRadius = 45.0f;
    f32 cohesionDeadZone = 10.0f;

    // Steering weights
    f32 separationWeight = 2.5f;
    f32 alignmentWeight = 1.0f;
    f32 cohesionWeight = 0.8f;

    std::size_t maxNeighbours = 32;
    std::size_t teamCount = 3;

    std::uint32_t randomSeed = 42;
};

class Flock final
    : public interfaces::module::ISystem
    , public interfaces::behavior::IRenderable
{
public:
    Flock(
        simulation::world::World& world,
        const FlockConfig& config = FlockConfig{},
        interfaces::backend::IGPUComputeBackend* gpuBackend = nullptr
    );
    ~Flock() override;

    Flock(const Flock&) = delete;
    Flock& operator=(const Flock&) = delete;

    void tick(f32 dt) override;
    void render(interfaces::backend::IRenderer& renderer) override;

    const FlockConfig& config() const;
    interfaces::module::ComputeMode computeMode() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace emper::module
 