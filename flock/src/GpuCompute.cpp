#include "GpuCompute.h"

#include "Flock.h"

#include <emper/interfaces/backend/ICompute.h>
#include <emper/interfaces/backend/IRenderer.h>

#include <cmath>
#include <random>
#include <vector>

namespace {

struct GpuBoid
{
    float x;
    float y;
    float vx;
    float vy;
};

struct RenderConfig
{
    float worldWidth;
    float worldHeight;
    float padding[2]{};
};

} // namespace

namespace emper::module {

FlockGpuCompute::FlockGpuCompute(
    simulation::world::World& world,
    FlockConfig& config,
    interfaces::backend::IGPUComputeBackend* backend)
    : world_(world), config_(config), computeBackend_(backend)
{
}

bool FlockGpuCompute::initialize()
{
    if (!computeBackend_ || config_.boidCount == 0)
        return false;

    if (!computeBackend_->initialize())
        return false;

    const std::string shaderPath = "assets/shaders/flock_comp.comp";
    computeProgram_ = computeBackend_->compileShader(shaderPath);
    if (!computeProgram_)
        return false;

    boidCount_ = config_.boidCount;
    const std::size_t stateSize = boidCount_ * sizeof(GpuBoid);
    const std::size_t teamSize = boidCount_ * sizeof(std::uint32_t);
    gridColumns_ = std::max(
        1, static_cast<int>(std::ceil(
            config_.worldWidth / config_.perceptionRadius)));
    gridRows_ = std::max(
        1, static_cast<int>(std::ceil(
            config_.worldHeight / config_.perceptionRadius)));
    cellCount_ = static_cast<std::size_t>(gridColumns_) * gridRows_;

    stateBuffers_[0] = computeBackend_->createBuffer({stateSize});
    stateBuffers_[1] = computeBackend_->createBuffer({stateSize});
    teamBuffer_ = computeBackend_->createBuffer({teamSize});
    
    cellCountBuffer_ =
        computeBackend_->createBuffer({
            cellCount_ * sizeof(std::uint32_t)
        });

    cellParticleBuffer_ =
        computeBackend_->createBuffer({
            cellCount_ *
            maxCellParticles_ *
            sizeof(std::uint32_t)
        });
    
        renderConfigBuffer_ = computeBackend_->createBuffer(
        {sizeof(RenderConfig)});
    if (!stateBuffers_[0] || !stateBuffers_[1] || !teamBuffer_ ||
        !cellCountBuffer_ || !cellParticleBuffer_ || !renderConfigBuffer_)
    {
        shutdown();
        return false;
    }

    std::vector<GpuBoid> boids(boidCount_);
    std::vector<std::uint32_t> teams(boidCount_);
    const RenderConfig renderConfig{
        config_.worldWidth,
        config_.worldHeight
    };
    std::mt19937 rng(config_.randomSeed);
    std::uniform_real_distribution<f32> positionX(0.0f, config_.worldWidth);
    std::uniform_real_distribution<f32> positionY(0.0f, config_.worldHeight);
    std::uniform_real_distribution<f32> angle(0.0f, 6.28318530718f);

    for (std::size_t i = 0; i < boidCount_; ++i)
    {
        const f32 direction = angle(rng);
        boids[i] = GpuBoid{
            positionX(rng),
            positionY(rng),
            std::cos(direction) * config_.initialSpeed,
            std::sin(direction) * config_.initialSpeed
        };
        teams[i] = static_cast<std::uint32_t>(i % config_.teamCount);
    }

    if (!computeBackend_->writeBuffer(
            stateBuffers_[0], boids.data(), stateSize) ||
        !computeBackend_->writeBuffer(
            stateBuffers_[1], boids.data(), stateSize) ||
        !computeBackend_->writeBuffer(
            teamBuffer_, teams.data(), teamSize) ||
        !computeBackend_->writeBuffer(
            renderConfigBuffer_, &renderConfig, sizeof(renderConfig)))
    {
        shutdown();
        return false;
    }

    initialized_ = true;
    readBufferIndex_ = 0;
    return true;
}

bool FlockGpuCompute::isAvailable() const noexcept
{
    return initialized_;
}

void FlockGpuCompute::tick(f32 dt)
{
    if (!initialized_ || dt <= 0.0f)
        return;

    // =========================================================
    // Buffer ping-pong
    // =========================================================

    const std::size_t readIndex =
        readBufferIndex_;

    const std::size_t writeIndex =
        1 - readIndex;


    // =========================================================
    // Static configuration
    //
    // These values do not change every frame.
    // =========================================================

    static bool configured = false;

    if (!configured)
    {
        constexpr int MaxCellParticles = 1024;

        computeBackend_->setUniform1f(
            computeProgram_,
            "worldWidth",
            config_.worldWidth);

        computeBackend_->setUniform1f(
            computeProgram_,
            "worldHeight",
            config_.worldHeight);

        computeBackend_->setUniform1f(
            computeProgram_,
            "maxSpeed",
            config_.maxSpeed);

        computeBackend_->setUniform1f(
            computeProgram_,
            "maxForce",
            config_.maxForce);

        computeBackend_->setUniform1f(
            computeProgram_,
            "initialSpeed",
            config_.initialSpeed);

        computeBackend_->setUniform1f(
            computeProgram_,
            "perceptionRadius",
            config_.perceptionRadius);

        computeBackend_->setUniform1f(
            computeProgram_,
            "separationRadius",
            config_.separationRadius);

        computeBackend_->setUniform1f(
            computeProgram_,
            "cohesionRadius",
            config_.cohesionRadius);

        computeBackend_->setUniform1f(
            computeProgram_,
            "cohesionDeadZone",
            config_.cohesionDeadZone);

        computeBackend_->setUniform1f(
            computeProgram_,
            "separationWeight",
            config_.separationWeight);

        computeBackend_->setUniform1f(
            computeProgram_,
            "alignmentWeight",
            config_.alignmentWeight);

        computeBackend_->setUniform1f(
            computeProgram_,
            "cohesionWeight",
            config_.cohesionWeight);

        computeBackend_->setUniform1i(
            computeProgram_,
            "particleCount",
            static_cast<int>(boidCount_));

        computeBackend_->setUniform1i(
            computeProgram_,
            "maxNeighbours",
            static_cast<int>(
                config_.maxNeighbours));

        computeBackend_->setUniform1i(
            computeProgram_,
            "gridColumns",
            gridColumns_);

        computeBackend_->setUniform1i(
            computeProgram_,
            "gridRows",
            gridRows_);

        computeBackend_->setUniform1i(
            computeProgram_,
            "maxCellParticles",
            MaxCellParticles);

        configured = true;
    }


    // =========================================================
    // Per-frame state
    //
    // IMPORTANT:
    // These MUST be rebound every frame because the
    // ping-pong buffers change.
    // =========================================================

    computeBackend_->bindStorageBuffer(
        0,
        stateBuffers_[readIndex]);

    computeBackend_->bindStorageBuffer(
        1,
        stateBuffers_[writeIndex]);

    computeBackend_->bindStorageBuffer(
        2,
        teamBuffer_);

    computeBackend_->bindStorageBuffer(
        3,
        cellCountBuffer_);

    computeBackend_->bindStorageBuffer(
        4,
        cellParticleBuffer_);


    // dt changes every frame.

    computeBackend_->setUniform1f(
        computeProgram_,
        "dt",
        dt);


    // =========================================================
    // Dispatch sizes
    // =========================================================

    const u32 boidGroups =
        static_cast<u32>(
            (boidCount_ + 255) / 256);

    const u32 cellGroups =
        static_cast<u32>(
            (cellCount_ + 255) / 256);


    // =========================================================
    // PASS 0
    //
    // Clear cell counters.
    // =========================================================

    computeBackend_->setUniform1i(
        computeProgram_,
        "pass",
        0);

    computeBackend_->dispatch(
        computeProgram_,
        {
            cellGroups,
            1,
            1
        });

    computeBackend_->memoryBarrier();


    // =========================================================
    // PASS 1
    //
    // Build spatial grid.
    // =========================================================

    computeBackend_->setUniform1i(
        computeProgram_,
        "pass",
        1);

    computeBackend_->dispatch(
        computeProgram_,
        {
            boidGroups,
            1,
            1
        });

    computeBackend_->memoryBarrier();


    // =========================================================
    // PASS 2
    //
    // Update boids.
    // =========================================================

    computeBackend_->setUniform1i(
        computeProgram_,
        "pass",
        2);

    computeBackend_->dispatch(
        computeProgram_,
        {
            boidGroups,
            1,
            1
        });

    computeBackend_->memoryBarrier();


    // =========================================================
    // Swap ping-pong buffers
    // =========================================================

    readBufferIndex_ =
        writeIndex;
}

void FlockGpuCompute::render(
    interfaces::backend::IRenderer& renderer)
{
    if (!initialized_)
        return;

    auto* pipeline =
        dynamic_cast<
            interfaces::backend::IRendererShaderPipeline*
        >(&renderer);

    if (!pipeline)
        return;

    if (!renderProgram_)
    {
        renderProgram_ = pipeline->createGraphicsProgram(
            "assets/shaders/flock_ver.ver",
            "assets/shaders/flock_frag.frag");
        if (!renderProgram_)
            return;
        renderPipeline_ = pipeline;
    }

    pipeline->bindProgram(renderProgram_);
    pipeline->bindStorageBuffer(0, stateBuffers_[readBufferIndex_]);
    pipeline->bindStorageBuffer(2, teamBuffer_);
    pipeline->bindStorageBuffer(5, renderConfigBuffer_);
    pipeline->drawPoints(static_cast<u32>(boidCount_));
}

FlockGpuCompute::~FlockGpuCompute()
{
    shutdown();
}

void FlockGpuCompute::shutdown()
{
    if (!computeBackend_)
        return;

    if (renderProgram_ && renderPipeline_)
        renderPipeline_->destroyProgram(renderProgram_);
    renderProgram_ = 0;
    renderPipeline_ = nullptr;
    if (computeProgram_)
    {
        computeBackend_->destroyProgram(computeProgram_);
        computeProgram_ = 0;
    }
    for (auto& buffer : stateBuffers_)
    {
        if (buffer)
        {
            computeBackend_->destroyBuffer(buffer);
            buffer = 0;
        }
    }
    if (teamBuffer_)
    {
        computeBackend_->destroyBuffer(teamBuffer_);
        teamBuffer_ = 0;
    }
    if (cellCountBuffer_)
    {
        computeBackend_->destroyBuffer(cellCountBuffer_);
        cellCountBuffer_ = 0;
    }
    if (cellParticleBuffer_)
    {
        computeBackend_->destroyBuffer(cellParticleBuffer_);
        cellParticleBuffer_ = 0;
    }
    if (renderConfigBuffer_)
    {
        computeBackend_->destroyBuffer(renderConfigBuffer_);
        renderConfigBuffer_ = 0;
    }
    initialized_ = false;
}

} // namespace emper::module
