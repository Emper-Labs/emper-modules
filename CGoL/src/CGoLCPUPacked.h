#pragma once

#include "CGoL.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace emper::module::cgol
{

class GameOfLifeCPUPacked
    : public emper::interfaces::module::ISystem
    , public emper::interfaces::behavior::IRenderable
{
public:

    GameOfLifeCPUPacked(
        std::size_t width,
        std::size_t height
    );

    void tick(emper::f32 dt);

    void render(
        emper::interfaces::backend::IRenderer& renderer
    ) override;

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

    std::size_t m_generation = 0;
    float m_accumulator = 0.0f;
};

} // namespace emper::module::cgol