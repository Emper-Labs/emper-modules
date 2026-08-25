#include "GpuCompute.h"

#include "Flock.h"

#include <emper/interfaces/backend/ICompute.h>

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
    : world_(world), config_(config), computeBackend_(backend),benchmark_(false)
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

    // Buffer 0 = persistent integrated state (also the rendered buffer).
    // Buffer 1 = per-frame cell-sorted scratch used only within a tick.
    stateBuffers_[0] = computeBackend_->createBuffer({stateSize});
    stateBuffers_[1] = computeBackend_->createBuffer({stateSize});
    teamBuffers_[0] = computeBackend_->createBuffer({teamSize});
    teamBuffers_[1] = computeBackend_->createBuffer({teamSize});

    // Two independent per-cell counters:
    //   [0] occupancy for the prefix sum (pass 1)
    //   [1] scatter counter for the reorder pass (pass 3)
    cellCountBuffers_[0] =
        computeBackend_->createBuffer({
            cellCount_ * sizeof(std::uint32_t)
        });
    cellCountBuffers_[1] =
        computeBackend_->createBuffer({
            cellCount_ * sizeof(std::uint32_t)
        });

    // cellStart has one extra element: the one-past-the-end
    // boundary for the final cell.
    cellStartBuffer_ =
        computeBackend_->createBuffer({
            (cellCount_ + 1) * sizeof(std::uint32_t)
        });

    // Optional per-boid benchmark counters for pass 4.
    // Layout: [candidateChecks (boidCount_)] [neighbours (boidCount_)].
    benchmarkBuffer_ =
        computeBackend_->createBuffer({
            2 * boidCount_ * sizeof(std::uint32_t)
        });

    renderConfigBuffer_ = computeBackend_->createBuffer(
        {sizeof(RenderConfig)});
    if (!stateBuffers_[0] || !stateBuffers_[1] ||
        !teamBuffers_[0] || !teamBuffers_[1] ||
        !cellCountBuffers_[0] || !cellCountBuffers_[1] ||
        !cellStartBuffer_ || !benchmarkBuffer_ ||
        !renderConfigBuffer_)
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
            teamBuffers_[0], teams.data(), teamSize) ||
        !computeBackend_->writeBuffer(
            teamBuffers_[1], teams.data(), teamSize) ||
        !computeBackend_->writeBuffer(
            renderConfigBuffer_, &renderConfig, sizeof(renderConfig)))
    {
        shutdown();
        return false;
    }

    initialized_ = true;
    return true;
}

bool FlockGpuCompute::isAvailable() const noexcept
{
    return initialized_;
}

void FlockGpuCompute::setBenchmarkEnabled(bool enabled) noexcept
{
    benchmark_ = enabled;
}

bool FlockGpuCompute::readCandidateCounts(
    std::vector<std::uint32_t>& out)
{
    if (!initialized_ || !benchmark_)
        return false;

    out.resize(boidCount_);
    return computeBackend_->readBuffer(
        benchmarkBuffer_,
        out.data(),
        boidCount_ * sizeof(std::uint32_t));
}

bool FlockGpuCompute::lastFrameStats(
    FlockFrameStats& out) const
{
    if (!initialized_ || !benchmark_)
        return false;

    std::vector<std::uint32_t> data(2 * boidCount_);
    if (!computeBackend_->readBuffer(
            benchmarkBuffer_,
            data.data(),
            2 * boidCount_ * sizeof(std::uint32_t)))
    {
        return false;
    }

    std::size_t totalCandidates = 0;
    std::size_t totalNeighbours = 0;
    std::size_t maxCandidates = 0;
    std::size_t maxNeighbours = 0;

    for (std::size_t i = 0; i < boidCount_; ++i)
    {
        const std::size_t candidates = data[i];
        const std::size_t neighbours = data[boidCount_ + i];

        totalCandidates += candidates;
        totalNeighbours += neighbours;
        maxCandidates = std::max(maxCandidates, candidates);
        maxNeighbours = std::max(maxNeighbours, neighbours);
    }

    lastStats_.boidCount = boidCount_;
    lastStats_.candidateChecks = totalCandidates;
    lastStats_.neighbours = totalNeighbours;
    lastStats_.maxCandidatesPerBoid = maxCandidates;
    lastStats_.maxNeighboursPerBoid = maxNeighbours;

    out = lastStats_;
    return true;
}

void FlockGpuCompute::tick(f32 dt)
{
    if (!initialized_ || dt <= 0.0f)
        return;

    // =========================================================
    // Fixed buffer roles
    //
    // Two state-touching passes run in opposite directions each
    // tick (reorder followed by update), so a classical single
    // swap ping-pong does not apply. Instead the two buffers
    // have FIXED roles:
    //
    //   buffer 0 = persistent integrated state
    //              (also the rendered buffer)
    //   buffer 1 = per-frame cell-sorted scratch
    //
    // Pass 3 reads buffer 0 and writes cell-sorted results
    // into buffer 1.
    // Pass 4 reads buffer 1 (sequential neighbour access) and
    // writes integrated results back into buffer 0.
    //
    // No swap happens at the end of the tick.
    // =========================================================

    constexpr std::size_t PersistentIndex = 0;
    constexpr std::size_t SortedIndex = 1;


    // =========================================================
    // Static configuration
    //
    // These values do not change every frame.
    // =========================================================

    if (!configured_)
    {
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
            "benchmark",
            benchmark_ ? 1 : 0);

        configured_ = true;
    }


    // dt changes every frame.

    computeBackend_->setUniform1f(
        computeProgram_,
        "dt",
        dt);


    // =========================================================
    // Bindings shared by all passes
    // =========================================================

    computeBackend_->bindStorageBuffer(
        4,
        cellCountBuffers_[0]);

    computeBackend_->bindStorageBuffer(
        5,
        cellCountBuffers_[1]);

    computeBackend_->bindStorageBuffer(
        6,
        cellStartBuffer_);

    computeBackend_->bindStorageBuffer(
        7,
        benchmarkBuffer_);


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
    // State bindings for passes 0-3 (persistent -> sorted)
    // =========================================================

    computeBackend_->bindStorageBuffer(
        0,
        stateBuffers_[PersistentIndex]);

    computeBackend_->bindStorageBuffer(
        1,
        stateBuffers_[SortedIndex]);

    computeBackend_->bindStorageBuffer(
        2,
        teamBuffers_[PersistentIndex]);

    computeBackend_->bindStorageBuffer(
        3,
        teamBuffers_[SortedIndex]);


    // =========================================================
    // PASS 0
    //
    // Clear both cell counters.
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
    // Count boids per cell (exact occupancy).
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
    // Exclusive prefix sum -> cellStart[].
    //
    // Single-threaded serial scan; the cell grid is small
    // (< 4096 cells) so this is effectively free.
    // =========================================================

    computeBackend_->setUniform1i(
        computeProgram_,
        "pass",
        2);

    computeBackend_->dispatch(
        computeProgram_,
        {
            1,
            1,
            1
        });

    computeBackend_->memoryBarrier();


    // =========================================================
    // PASS 3
    //
    // Scatter boids into cell-contiguous order.
    //
    // Reads buffer 0 (persistent state/teams), writes the
    // reordered layout into buffer 1 (sorted state/teams).
    // =========================================================

    computeBackend_->setUniform1i(
        computeProgram_,
        "pass",
        3);

    computeBackend_->dispatch(
        computeProgram_,
        {
            boidGroups,
            1,
            1
        });

    computeBackend_->memoryBarrier();


    // =========================================================
    // Rebind state bindings for pass 4 (sorted -> integrated)
    //
    // Pass 4 must READ the sorted buffer 1 and WRITE the
    // integrated result into buffer 0, so the input/output
    // roles of bindings 0/1 and 2/3 are reversed.
    // =========================================================

    computeBackend_->bindStorageBuffer(
        0,
        stateBuffers_[SortedIndex]);

    computeBackend_->bindStorageBuffer(
        1,
        stateBuffers_[PersistentIndex]);

    computeBackend_->bindStorageBuffer(
        2,
        teamBuffers_[SortedIndex]);

    computeBackend_->bindStorageBuffer(
        3,
        teamBuffers_[PersistentIndex]);


    // =========================================================
    // PASS 4
    //
    // Flock + integrate.
    //
    // Reads the cell-sorted buffer 1 (sequential neighbour
    // access), writes integrated results back into buffer 0.
    // =========================================================

    computeBackend_->setUniform1i(
        computeProgram_,
        "pass",
        4);

    computeBackend_->dispatch(
        computeProgram_,
        {
            boidGroups,
            1,
            1
        });

    computeBackend_->memoryBarrier();
}

FlockData FlockGpuCompute::data() const
{
    FlockData result;
    result.mode = FlockDataMode::GPU;
    result.boidCount = boidCount_;
    result.worldWidth = config_.worldWidth;
    result.worldHeight = config_.worldHeight;

    // stateBuffers_[0] is the persistent, integrated boid state (the
    // buffer a rendering consumer draws). teamBuffers_[0] and
    // renderConfigBuffer_ accompany it. (index 0 == PersistentIndex)
    result.stateBuffer = stateBuffers_[0];
    result.teamBuffer = teamBuffers_[0];
    result.renderConfigBuffer = renderConfigBuffer_;

    return result;
}

FlockGpuCompute::~FlockGpuCompute()
{
    shutdown();
}

void FlockGpuCompute::shutdown()
{
    if (!computeBackend_)
        return;

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
    for (auto& buffer : teamBuffers_)
    {
        if (buffer)
        {
            computeBackend_->destroyBuffer(buffer);
            buffer = 0;
        }
    }
    for (auto& buffer : cellCountBuffers_)
    {
        if (buffer)
        {
            computeBackend_->destroyBuffer(buffer);
            buffer = 0;
        }
    }
    if (cellStartBuffer_)
    {
        computeBackend_->destroyBuffer(cellStartBuffer_);
        cellStartBuffer_ = 0;
    }
    if (benchmarkBuffer_)
    {
        computeBackend_->destroyBuffer(benchmarkBuffer_);
        benchmarkBuffer_ = 0;
    }
    if (renderConfigBuffer_)
    {
        computeBackend_->destroyBuffer(renderConfigBuffer_);
        renderConfigBuffer_ = 0;
    }
    initialized_ = false;
}

} // namespace emper::module