#include "CGoLCPUPacked.h"

#include <algorithm>
#include <random>


namespace emper::module::cgol
{

GameOfLifeCPUPacked::GameOfLifeCPUPacked(
    std::size_t width,
    std::size_t height
)
    : m_width(width)
    , m_height(height)
    , m_wordsPerRow((width + 63) / 64)
    , m_current(m_wordsPerRow * height)
    , m_next(m_wordsPerRow * height)
{
}


void
GameOfLifeCPUPacked::tick(emper::f32 dt)
{
    m_accumulator += dt;

    constexpr float fixedStep = 1.0f / 30.0f;

    while (m_accumulator >= fixedStep)
    {
        step();
        m_accumulator -= fixedStep;
    }
}


void
GameOfLifeCPUPacked::add(
    BitCount& count,
    Word value
)
{
    const Word c0 =
        count.b0 & value;

    count.b0 ^= value;

    const Word c1 =
        count.b1 & c0;

    count.b1 ^= c0;

    const Word c2 =
        count.b2 & c1;

    count.b2 ^= c1;

    count.b3 ^= c2;
}


GameOfLifeCPUPacked::Word
GameOfLifeCPUPacked::equal2(
    const BitCount& count
)
{
    return
        ~count.b3 &
        ~count.b2 &
         count.b1 &
        ~count.b0;
}


GameOfLifeCPUPacked::Word
GameOfLifeCPUPacked::equal3(
    const BitCount& count
)
{
    return
        ~count.b3 &
        ~count.b2 &
         count.b1 &
         count.b0;
}


GameOfLifeCPUPacked::Word
GameOfLifeCPUPacked::getWord(
    std::size_t y,
    std::size_t word
) const
{
    return m_current[
        y * m_wordsPerRow + word
    ];
}


void
GameOfLifeCPUPacked::step()
{
    for (std::size_t y = 0; y < m_height; ++y)
    {
        const std::size_t up =
            (y == 0)
                ? m_height - 1
                : y - 1;

        const std::size_t down =
            (y + 1 == m_height)
                ? 0
                : y + 1;

        for (std::size_t word = 0;
             word < m_wordsPerRow;
             ++word)
        {
            /*
             * Get the current word and its vertical neighbors.
             */
            const Word rowUp =
                getWord(up, word);

            const Word row =
                getWord(y, word);

            const Word rowDown =
                getWord(down, word);

            /*
             * Get neighboring words for horizontal wrapping.
             */
            const std::size_t previousWord =
                (word == 0)
                    ? m_wordsPerRow - 1
                    : word - 1;

            const std::size_t nextWord =
                (word + 1 == m_wordsPerRow)
                    ? 0
                    : word + 1;

            const Word rowUpLeft =
                getWord(up, previousWord);

            const Word rowUpRight =
                getWord(up, nextWord);

            const Word rowLeft =
                getWord(y, previousWord);

            const Word rowRight =
                getWord(y, nextWord);

            const Word rowDownLeft =
                getWord(down, previousWord);

            const Word rowDownRight =
                getWord(down, nextWord);

            /*
             * Shift left/right while carrying bits
             * across uint64_t word boundaries.
             *
             * Bit 63 of the previous word becomes
             * the incoming bit for the current word.
             */
            const Word nw =
                (rowUp << 1) |
                (rowUpLeft >> 63);

            const Word n =
                rowUp;

            const Word ne =
                (rowUp >> 1) |
                (rowUpRight << 63);

            const Word w =
                (row << 1) |
                (rowLeft >> 63);

            const Word e =
                (row >> 1) |
                (rowRight << 63);

            const Word sw =
                (rowDown << 1) |
                (rowDownLeft >> 63);

            const Word s =
                rowDown;

            const Word se =
                (rowDown >> 1) |
                (rowDownRight << 63);

            /*
             * Count eight neighbors for all 64 cells
             * using bit-sliced addition.
             */
            BitCount count;

            add(count, nw);
            add(count, n);
            add(count, ne);

            add(count, w);
            add(count, e);

            add(count, sw);
            add(count, s);
            add(count, se);

            /*
             * Conway's Game of Life:
             *
             * Alive  + 2 neighbors -> survives
             * Alive  + 3 neighbors -> survives
             * Dead   + 3 neighbors -> born
             */
            const Word survive =
                row & equal2(count);

            const Word born =
                equal3(count);

            const Word next =
                survive | born;

            m_next[
                y * m_wordsPerRow + word
            ] = next;
        }
    }

    m_current.swap(m_next);

    ++m_generation;
}


void
GameOfLifeCPUPacked::setCell(
    std::size_t x,
    std::size_t y
)
{
    if (x >= m_width || y >= m_height)
        return;

    const std::size_t word =
        x / 64;

    const std::size_t bit =
        x % 64;

    m_current[
        y * m_wordsPerRow + word
    ] |=
        Word{1} << bit;
}


bool
GameOfLifeCPUPacked::getCell(
    std::size_t x,
    std::size_t y
) const
{
    if (x >= m_width || y >= m_height)
        return false;

    const std::size_t word =
        x / 64;

    const std::size_t bit =
        x % 64;

    return
        (
            m_current[
                y * m_wordsPerRow + word
            ] &
            (Word{1} << bit)
        ) != 0;
}


void
GameOfLifeCPUPacked::load(
    const Pattern& pattern,
    emper::i32 offsetX,
    emper::i32 offsetY
)
{
    for (const auto& cell : pattern.cells)
    {
        const auto x =
            cell.x + offsetX;

        const auto y =
            cell.y + offsetY;

        if (x < 0 || y < 0)
            continue;

        if (
            x >= static_cast<emper::i32>(m_width) ||
            y >= static_cast<emper::i32>(m_height)
        )
        {
            continue;
        }

        setCell(
            static_cast<std::size_t>(x),
            static_cast<std::size_t>(y)
        );
    }
}


void
GameOfLifeCPUPacked::randomize(float probability)
{
    std::mt19937 rng{
        std::random_device{}()
    };

    std::bernoulli_distribution alive(
        probability
    );

    clear();

    for (std::size_t y = 0; y < m_height; ++y)
    {
        for (std::size_t x = 0; x < m_width; ++x)
        {
            if (alive(rng))
                setCell(x, y);
        }
    }
}


void
GameOfLifeCPUPacked::clear()
{
    std::fill(
        m_current.begin(),
        m_current.end(),
        Word{0}
    );

    std::fill(
        m_next.begin(),
        m_next.end(),
        Word{0}
    );

    m_generation = 0;
    m_accumulator = 0.0f;
}


#include <bit>

void
GameOfLifeCPUPacked::render(
    emper::interfaces::backend::IRenderer& renderer
)
{
    const float cellWidth =
        static_cast<float>(
            renderer.windowWidth()
        ) /
        static_cast<float>(m_width);

    const float cellHeight =
        static_cast<float>(
            renderer.windowHeight()
        ) /
        static_cast<float>(m_height);

    for (std::size_t y = 0; y < m_height; ++y)
    {
        const std::size_t rowOffset =
            y * m_wordsPerRow;

        for (std::size_t wordIndex = 0;
             wordIndex < m_wordsPerRow;
             ++wordIndex)
        {
            Word word =
                m_current[rowOffset + wordIndex];

            while (word != 0)
            {
                const unsigned bit =
                    std::countr_zero(word);

                const std::size_t x =
                    wordIndex * 64 + bit;

                // Ignore padding bits in the final word.
                if (x < m_width)
                {
                    renderer.drawRect(
                        static_cast<float>(x) * cellWidth,
                        static_cast<float>(y) * cellHeight,
                        cellWidth,
                        cellHeight,
                        0xFFFFFFFF
                    );
                }

                word &= word - 1;
            }
        }
    }

    renderer.drawText(
        "Conway's Game of Life [CPU Packed]",
        10.0f,
        10.0f,
        1.0f
    );
}

} // namespace emper::module::cgol