#include "GameOfLifeGPU.h"

#include <emper/interfaces/backend/ICompute.h>

#include <algorithm>
#include <array>
#include <random>

namespace emper::module::cgol
{

GameOfLifeGPU::GameOfLifeGPU(
    std::size_t width,
    std::size_t height,
    interfaces::backend::IGPUComputeBackend* gpuBackend)
    : m_width(width)
    , m_height(height)
    , m_cellCount(width * height)
    , computeBackend_(gpuBackend)
{
    m_hostGrid.assign(m_cellCount, Word{0});
}


void
GameOfLifeGPU::initialize()
{
    // Idempotent: Simulation::start() forwards ISystem::initialize() to every
    // registered system, but the sample also probes initialize() to decide
    // whether the GPU path is usable. Guard so the program/buffers are not
    // rebuilt (and leaked) if initialize() is called twice.
    if (m_initialized)
        return;

    if (!computeBackend_ || m_cellCount == 0)
        return;

    if (!computeBackend_->initialize())
        return;

    // Compile the compute shader implementing one CGoL step.
    stepProgram_ =
        computeBackend_->compileShader(
            "assets/shaders/cgol_comp.comp");

    if (!stepProgram_)
    {
        shutdown();
        return;
    }

    // Double-buffered grid: m_grid[m_read] is the current generation,
    // m_grid[m_write] receives the next one. Each cell is a Word.
    const std::size_t gridBytes =
        m_cellCount * sizeof(Word);

    m_grid[0] = computeBackend_->createBuffer({gridBytes});
    m_grid[1] = computeBackend_->createBuffer({gridBytes});

    // vec4(surfaceW, surfaceH, gridW, gridH) for the GPU render vertex shader.
    renderConfigBuffer_ =
        computeBackend_->createBuffer({4 * sizeof(f32)});

    // Sparse stepping scratch buffers.
    markBuffer_ = computeBackend_->createBuffer({gridBytes});
    candidateBuffer_ = computeBackend_->createBuffer({gridBytes});
    candidateCountBuffer_ = computeBackend_->createBuffer({sizeof(Word)});

    if (!m_grid[0] || !m_grid[1] || !renderConfigBuffer_ ||
        !markBuffer_ || !candidateBuffer_ || !candidateCountBuffer_)
    {
        shutdown();
        return;
    }

    m_read  = 0;
    m_write = 1;

    m_available = true;
    m_initialized = true;
}


void
GameOfLifeGPU::shutdown()
{
    if (!computeBackend_)
    {
        m_initialized = false;
        m_available = false;
        return;
    }

    if (stepProgram_)
    {
        computeBackend_->destroyProgram(stepProgram_);
        stepProgram_ = 0;
    }

    for (auto& buffer : m_grid)
    {
        if (buffer)
        {
            computeBackend_->destroyBuffer(buffer);
            buffer = 0;
        }
    }

    if (renderConfigBuffer_)
    {
        computeBackend_->destroyBuffer(renderConfigBuffer_);
        renderConfigBuffer_ = 0;
    }

    if (markBuffer_)
    {
        computeBackend_->destroyBuffer(markBuffer_);
        markBuffer_ = 0;
    }

    if (candidateBuffer_)
    {
        computeBackend_->destroyBuffer(candidateBuffer_);
        candidateBuffer_ = 0;
    }

    if (candidateCountBuffer_)
    {
        computeBackend_->destroyBuffer(candidateCountBuffer_);
        candidateCountBuffer_ = 0;
    }

    m_initialized = false;
    m_available = false;
}
void
GameOfLifeGPU::stepOnGpu()
{
    if (!m_initialized)
        return;

    // Common uniforms shared by all passes.
    computeBackend_->setUniform1i(
        stepProgram_,
        "width",
        static_cast<int>(m_width));

    computeBackend_->setUniform1i(
        stepProgram_,
        "height",
        static_cast<int>(m_height));

    computeBackend_->setUniform1i(
        stepProgram_,
        "cellCount",
        static_cast<int>(m_cellCount));

    // Pass 0 - reset the next grid, the mark buffer and the candidate counter.
    computeBackend_->setUniform1i(stepProgram_, "pass", 0);

    computeBackend_->bindStorageBuffer(1, m_grid[m_write]);
    computeBackend_->bindStorageBuffer(2, markBuffer_);
    computeBackend_->bindStorageBuffer(4, candidateCountBuffer_);

    computeBackend_->dispatch(
        stepProgram_,
        {static_cast<emper::u32>(m_cellCount), 1, 1});

    computeBackend_->memoryBarrier();

    // Pass 1 - compact candidate indices (3x3 neighbourhoods of live cells).
    computeBackend_->setUniform1i(stepProgram_, "pass", 1);

    computeBackend_->bindStorageBuffer(0, m_grid[m_read]);
    computeBackend_->bindStorageBuffer(2, markBuffer_);
    computeBackend_->bindStorageBuffer(3, candidateBuffer_);
    computeBackend_->bindStorageBuffer(4, candidateCountBuffer_);

    computeBackend_->dispatch(
        stepProgram_,
        {static_cast<emper::u32>(m_cellCount), 1, 1});

    computeBackend_->memoryBarrier();

    // Read back the (tiny) candidate count so pass 2 dispatches ONLY the
    // cells that can actually change. This is a 4-byte hint, not a grid
    // pull-back.
    Word candidateCount = 0;
    computeBackend_->readBuffer(
        candidateCountBuffer_,
        &candidateCount,
        sizeof(Word));

    // Pass 2 - evaluate only the candidate cells into the next grid.
    if (candidateCount > 0)
    {
        computeBackend_->setUniform1i(stepProgram_, "pass", 2);

        computeBackend_->bindStorageBuffer(0, m_grid[m_read]);
        computeBackend_->bindStorageBuffer(1, m_grid[m_write]);
        computeBackend_->bindStorageBuffer(3, candidateBuffer_);
        computeBackend_->bindStorageBuffer(4, candidateCountBuffer_);

        computeBackend_->dispatch(
            stepProgram_,
            {candidateCount, 1, 1});

        computeBackend_->memoryBarrier();
    }

    // Ping-pong: the buffer we just wrote becomes the current generation.
    std::swap(m_read, m_write);
    ++m_generation;
}


void
GameOfLifeGPU::tick(
    emper::f32 dt)
{
    m_accumulator += dt;

    // Fixed simulation step, matching the CPU backends.
    constexpr float fixedStep = 1.0f / 30.0f;

    while (m_accumulator >= fixedStep)
    {
        stepOnGpu();
        m_accumulator -= fixedStep;
    }
}


void
GameOfLifeGPU::clear()
{
    std::fill(m_hostGrid.begin(), m_hostGrid.end(), Word{0});

    if (m_initialized)
    {
        computeBackend_->writeBuffer(
            m_grid[m_read],
            m_hostGrid.data(),
            m_hostGrid.size() * sizeof(Word));
    }

    m_generation = 0;
    m_accumulator = 0.0f;
}


void
GameOfLifeGPU::randomize(
    float probability)
{
    std::mt19937 rng{
        std::random_device{}()
    };

    std::bernoulli_distribution alive(
        probability
    );

    for (std::size_t y = 0; y < m_height; ++y)
    {
        for (std::size_t x = 0; x < m_width; ++x)
        {
            m_hostGrid[y * m_width + x] =
                alive(rng) ? Word{1} : Word{0};
        }
    }

    if (m_initialized)
    {
        computeBackend_->writeBuffer(
            m_grid[m_read],
            m_hostGrid.data(),
            m_hostGrid.size() * sizeof(Word));
    }

    m_generation = 0;
    m_accumulator = 0.0f;
}
void
GameOfLifeGPU::load(
    const Pattern& pattern,
    emper::i32 offsetX,
    emper::i32 offsetY)
{
    std::fill(m_hostGrid.begin(), m_hostGrid.end(), Word{0});

    for (const auto& cell : pattern.cells)
    {
        const std::int64_t x =
            static_cast<std::int64_t>(cell.x) + offsetX;

        const std::int64_t y =
            static_cast<std::int64_t>(cell.y) + offsetY;

        if (x < 0 || y < 0 ||
            x >= static_cast<std::int64_t>(m_width) ||
            y >= static_cast<std::int64_t>(m_height))
        {
            continue;
        }

        m_hostGrid[
            (static_cast<std::size_t>(y) * m_width) +
            static_cast<std::size_t>(x)] = Word{1};
    }

    if (m_initialized)
    {
        // Both buffers start from the same state so a later ping-pong
        // never reads stale data after a reload.
        computeBackend_->writeBuffer(
            m_grid[0],
            m_hostGrid.data(),
            m_hostGrid.size() * sizeof(Word));

        computeBackend_->writeBuffer(
            m_grid[1],
            m_hostGrid.data(),
            m_hostGrid.size() * sizeof(Word));
    }

    m_read  = 0;
    m_write = 1;

    m_generation = 0;
    m_accumulator = 0.0f;
}


GameOfLifeData
GameOfLifeGPU::data() const
{
    GameOfLifeData data;

    data.mode = GameOfLifeDataMode::GPU;

    data.width  = m_width;
    data.height = m_height;

    data.generation = m_generation;

    // GPU mode: expose the opaque GPU buffer handles directly (mirroring the
    // Flock module's FlockData). We do NOT pull the grid back to the host for
    // processing; the grid lives and steps entirely in GPU memory. Only the
    // current (read) ping-pong buffer is exposed so consumers render exactly
    // the latest generation.
    data.gridBuffer = m_grid[m_read];
    data.renderConfigBuffer = renderConfigBuffer_;

    return data;
}


void
GameOfLifeGPU::synchronizeSurface(
    f32 width,
    f32 height)
{
    if (!m_initialized || !renderConfigBuffer_)
        return;

    if (width <= 0.0f || height <= 0.0f)
        return;

    // vec4(surfaceWidth, surfaceHeight, gridWidth, gridHeight).
    const std::array<f32, 4> config{
        width,
        height,
        static_cast<f32>(m_width),
        static_cast<f32>(m_height)
    };

    computeBackend_->writeBuffer(
        renderConfigBuffer_,
        config.data(),
        config.size() * sizeof(f32));
}


GameOfLifeGPU::~GameOfLifeGPU()
{
    shutdown();
}

} // namespace emper::module::cgol
