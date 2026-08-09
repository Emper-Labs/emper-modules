#pragma once

#include <emper/ComputeTypes.h>

#include <cstddef>

namespace emper::interfaces::backend
{
class IGPUComputeBackend;
class IRenderer;
class IRendererShaderPipeline;
}

namespace emper::simulation::world
{
class World;
}

namespace emper::module
{

struct FlockConfig;

// GPU implementation using only the engine compute and renderer interfaces.
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

    void render(
        interfaces::backend::IRenderer& renderer);

private:
    simulation::world::World& world_;
    FlockConfig& config_;

    interfaces::backend::IGPUComputeBackend* computeBackend_ =
        nullptr;


    // =========================================================
    // Compute programs
    // =========================================================

    emper::ProgramHandle computeProgram_ = 0;
    emper::ProgramHandle renderProgram_ = 0;


    // =========================================================
    // Boid state
    //
    // Ping-pong buffers:
    //
    //   stateBuffers_[read]  -> current frame
    //   stateBuffers_[write] -> next frame
    //
    // =========================================================

    emper::BufferHandle stateBuffers_[2]{};

    emper::BufferHandle teamBuffer_ = 0;


    // =========================================================
    // GPU spatial grid
    //
    // cellCountBuffer_:
    //
    //   Number of boids currently occupying each cell.
    //
    // cellParticleBuffer_:
    //
    //   Flat fixed-capacity bucket storage:
    //
    //   cellParticles[
    //       cell * maxCellParticles + slot
    //   ]
    //
    // =========================================================

    emper::BufferHandle cellCountBuffer_ = 0;

    emper::BufferHandle cellParticleBuffer_ = 0;


    // =========================================================
    // Render configuration
    // =========================================================

    emper::BufferHandle renderConfigBuffer_ = 0;

    interfaces::backend::IRendererShaderPipeline*
        renderPipeline_ = nullptr;


    // =========================================================
    // Simulation dimensions
    // =========================================================

    std::size_t boidCount_ = 0;

    std::size_t cellCount_ = 0;

    int gridColumns_ = 0;
    int gridRows_ = 0;


    // =========================================================
    // GPU bucket configuration
    // =========================================================

    std::size_t maxCellParticles_ = 1024;


    // =========================================================
    // Ping-pong state
    // =========================================================

    std::size_t readBufferIndex_ = 0;


    // =========================================================
    // State
    // =========================================================

    bool uniformsConfigured_ = false;
    bool initialized_ = false;
};

} // namespace emper::module