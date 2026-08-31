#pragma once

#include "CGoL.h"
#include "GameOfLifeData.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace emper::module::cgol
{

class GameOfLifeCPUPacked
    : public emper::interfaces::module::ISystem
{
public:

    GameOfLifeCPUPacked(
        std::size_t width,
        std::size_t height
    );

    void tick(emper::f32 dt);

    // Read-only snapshot of the current simulation state; aliveCells lists the
    // live cells. Populated by iterating only the set bits (O(live cells)), so
    // no full-grid densification occurs. Simulation data only.
    GameOfLifeData data() const;

    void randomize(float probability = 0.15f);

    void load(
        const Pattern& pattern,
        emper::i32 offsetX = 0,
        emper::i32 offsetY = 0
    );

    void clear();

private:

    using Word = std::uint64_t;

    struct BitCount
    {
        Word b0 = 0;
        Word b1 = 0;
        Word b2 = 0;
        Word b3 = 0;
    };

private:

    void step();

    static void add(
        BitCount& count,
        Word value
    );

    static Word equal2(
        const BitCount& count
    );

    static Word equal3(
        const BitCount& count
    );

    Word getWord(
        std::size_t y,
        std::size_t word
    ) const;

    void setCell(
        std::size_t x,
        std::size_t y
    );

    bool getCell(
        std::size_t x,
        std::size_t y
    ) const;

private:

    std::size_t m_width;
    std::size_t m_height;

    std::size_t m_wordsPerRow;

    std::vector<Word> m_current;
    std::vector<Word> m_next;

    // Reused destination for the on-demand live-cell snapshot produced by
    // data(). Populated by iterating only the set bits (O(live cells)).
    mutable std::vector<CellCoordinate> m_alive;

    std::size_t m_generation = 0;
    float m_accumulator = 0.0f;
};

} // namespace emper::module::cgol