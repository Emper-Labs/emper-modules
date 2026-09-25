#pragma once

#include "CGoL.h"
#include "GameOfLifeData.h"

#include <emper/EmperEngine.h>
#include <emper/interfaces/module/ISystem.h>

#include <cstddef>
#include <cstdint>
#include <vector>

namespace emper::interfaces::backend
{
class IGPUComputeBackend;
}

namespace emper::module::cgol
{

// GPU-backed Conway's Game of Life system.
//
// The simulation step runs entirely on the GPU through an
// IGPUComputeBackend, following the pipeline:
//
//     GPU buffer -> compute shader -> CGoL step -> GPU buffer
//
// `initialize()` compiles the compute shader (cgol_comp.comp) and allocates a
// double-buffered grid on the GPU. Each `tick()` fixed step dispatches the
// shader once: it reads the current generation from buffer[read_], writes the
// next generation into buffer[write_], and the two buffers are swapped
// (ping-pong). The state lives in GPU memory at rest and is only copied back
// to the host by `data()` (a full-grid readback) so the existing CPU-oriented
// consumers (GameOfLifeRenderPass, debug UI, ...) keep working unchanged.
//
// Public API mirrors the CPU backends (GameOfLifeCPUScalar/Packed/Sparse);
// load()/clear()/randomize() upload the host grid to GPU memory.
class GameOfLifeGPU final
    : public emper::interfaces::module::ISystem
{
public:
    using Word = std::uint32_t;

    GameOfLifeGPU(
        std::size_t width,
        std::size_t height,
        interfaces::backend::IGPUComputeBackend* gpuBackend = nullptr);

    GameOfLifeGPU(const GameOfLifeGPU&) = delete;
    GameOfLifeGPU& operator=(const GameOfLifeGPU&) = delete;

    ~GameOfLifeGPU() override;

    void initialize() override;
    void shutdown() override;

    void tick(emper::f32 dt) override;

    // Read-only snapshot of the current simulation state; aliveCells lists the
    // live cells. Produces a full-grid readback from the GPU. O(width*height).
    GameOfLifeData data() const;

    void load(
        const Pattern& pattern,
        emper::i32 offsetX = 0,
        emper::i32 offsetY = 0);

    void clear();

    void randomize(
        float probability = 0.15f);

    // (Re)writes the GPU render-config buffer with the drawable surface size.
    // Must be called after initialize() (and on surface resize) so the GPU
    // render vertex shader can lay the grid out correctly.
    void synchronizeSurface(f32 width, f32 height);

    [[nodiscard]]
    std::size_t generation() const noexcept
    {
        return m_generation;
    }

    // True when a compute backend was supplied and initialized.
    [[nodiscard]]
    bool isAvailable() const noexcept
    {
        return m_initialized && m_available;
    }

private:
    // Advances the grid by one generation on the GPU and increments the
    // generation counter. Requires an initialized compute backend.
    void stepOnGpu();

    // Full host copy of the grid used as an upload bounce buffer.
    std::vector<Word> m_hostGrid;

    std::size_t m_width  = 0;
    std::size_t m_height = 0;
    std::size_t m_cellCount = 0;

    interfaces::backend::IGPUComputeBackend* computeBackend_ = nullptr;

    emper::ProgramHandle stepProgram_ = 0;

    // Ping-pong grid buffers. m_grid[r] is the current generation.
    emper::BufferHandle m_grid[2]{0, 0};
    std::size_t m_read  = 0;
    std::size_t m_write = 1;

    // Sparse stepping scratch buffers (allocated once, reused every step):
    //   markBuffer_[cellCount]          claim flags; first marker wins
    //   candidateBuffer_[cellCount]     compacted candidate cell indices
    //   candidateCountBuffer_[1]        number of candidates this step
    emper::BufferHandle markBuffer_ = 0;
    emper::BufferHandle candidateBuffer_ = 0;
    emper::BufferHandle candidateCountBuffer_ = 0;

    // vec4(surfaceWidth, surfaceHeight, gridWidth, gridHeight) consumed by
    // the GPU render vertex shader.
    emper::BufferHandle renderConfigBuffer_ = 0;

    std::size_t m_generation = 0;

    float m_accumulator = 0.0f;

    bool m_available  = false;
    bool m_initialized = false;
};

} // namespace emper::module::cgol