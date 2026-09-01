#pragma once

#include <emper/EmperEngine.h>

#include <string>
#include <vector>

#include "CGoL.h"
#include "GameOfLifeData.h"

namespace emper::module::cgol
{

class GameOfLifeCPUScalar
    : public emper::interfaces::module::ISystem
{
public:

    GameOfLifeCPUScalar(
        std::size_t width,
        std::size_t height
    )
        : m_width(width)
        , m_height(height)
        , m_current(width * height)
        , m_next(width * height)
    {
    }

    void tick(emper::f32 dt);

    // Read-only snapshot of the current simulation state; aliveCells lists the
    // live cells. Simulation data only; intended for any consumer (rendering,
    // analytics, replay, debug UI). The backing buffer is reused across calls.
    GameOfLifeData data() const;

    void randomize(float probability = 0.15f);

    void load(
        const Pattern& pattern,
        emper::i32 offsetX = 0,
        emper::i32 offsetY = 0
    );

    void clear();

private:

    void step();

private:

    std::size_t m_width;
    std::size_t m_height;

    std::vector<Cell> m_current;
    std::vector<Cell> m_next;

    // Reused destination for the on-demand live-cell snapshot produced by
    // data(). The dense backend must scan its grid to collect alive cells (this
    // matches the cost of the original dense render loop).
    mutable std::vector<CellCoordinate> m_alive;

    std::size_t m_generation = 0;
    float m_accumulator = 0.0f;
};

} // namespace emper::module::cgol