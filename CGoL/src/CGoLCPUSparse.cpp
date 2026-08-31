#include "CGoLCPUSparse.h"

#include <algorithm>
#include <bit>
#include <random>


//#define DBG


#ifdef DBG

#include <iostream>
#include <chrono>

#endif

namespace emper::module::cgol
{

namespace
{

constexpr std::uint64_t HashConstant1 =
    0x9e3779b97f4a7c15ULL;

constexpr std::uint64_t HashConstant2 =
    0xbf58476d1ce4e5b9ULL;

constexpr std::uint64_t HashConstant3 =
    0x94d049bb133111ebULL;


[[nodiscard]]
std::uint64_t splitmix64(
    std::uint64_t value
) noexcept
{
    value += HashConstant1;

    value =
        (value ^ (value >> 30)) *
        HashConstant2;

    value =
        (value ^ (value >> 27)) *
        HashConstant3;

    return value ^ (value >> 31);
}

} // namespace


std::size_t
GameOfLifeCPUSparse::TileCoordHash::operator()(
    const TileCoord& coord
) const noexcept
{
    const std::uint64_t x =
        splitmix64(
            static_cast<std::uint64_t>(coord.x)
        );

    const std::uint64_t y =
        splitmix64(
            static_cast<std::uint64_t>(coord.y)
        );

    return static_cast<std::size_t>(
        x ^ std::rotl(y, 32)
    );
}


GameOfLifeCPUSparse::GameOfLifeCPUSparse(
    std::size_t width,
    std::size_t height
)
    : m_width(width)
    , m_height(height)
    , m_tileColumns(
        (width + TileSize - 1) / TileSize
    )
    , m_tileRows(
        (height + TileSize - 1) / TileSize
    )
{
    m_lastWordMask = ~Word{0};

    if (m_width % TileSize != 0)
    {
        const std::size_t remainder =
            m_width % TileSize;

        m_lastWordMask =
            (Word{1} << remainder) - 1;
    }
}


void
GameOfLifeCPUSparse::tick(
    emper::f32 dt
)
{
    m_accumulator += dt;

    constexpr float fixedStep =
        1.0f / 30.0f;

    while (m_accumulator >= fixedStep)
    {
        step();

        m_accumulator -= fixedStep;
    }
}


void
GameOfLifeCPUSparse::add(
    BitCount& count,
    Word value
) noexcept
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


GameOfLifeCPUSparse::Word
GameOfLifeCPUSparse::equal2(
    const BitCount& count
) noexcept
{
    return
        ~count.b3 &
        ~count.b2 &
         count.b1 &
        ~count.b0;
}


GameOfLifeCPUSparse::Word
GameOfLifeCPUSparse::equal3(
    const BitCount& count
) noexcept
{
    return
        ~count.b3 &
        ~count.b2 &
         count.b1 &
         count.b0;
}


const GameOfLifeCPUSparse::Tile*
GameOfLifeCPUSparse::findTile(
    std::int64_t tileX,
    std::int64_t tileY
) const noexcept
{
    if (tileX < 0 ||
        tileY < 0 ||
        tileX >= static_cast<std::int64_t>(m_tileColumns) ||
        tileY >= static_cast<std::int64_t>(m_tileRows))
    {
        return nullptr;
    }

    const auto it =
        m_current.find(
            TileCoord{
                tileX,
                tileY
            }
        );

    if (it == m_current.end())
        return nullptr;

    return &it->second;
}


GameOfLifeCPUSparse::Tile&
GameOfLifeCPUSparse::getOrCreateTile(
    std::int64_t tileX,
    std::int64_t tileY
)
{
    return m_current[
        TileCoord{
            tileX,
            tileY
        }
    ];
}


void
GameOfLifeCPUSparse::setCell(
    std::int64_t x,
    std::int64_t y
)
{
    if (x < 0 ||
        y < 0 ||
        x >= static_cast<std::int64_t>(m_width) ||
        y >= static_cast<std::int64_t>(m_height))
    {
        return;
    }

    const std::int64_t tileX =
        x >> 6;

    const std::int64_t tileY =
        y >> 6;

    const std::size_t localX =
        static_cast<std::size_t>(x & 63);

    const std::size_t localY =
        static_cast<std::size_t>(y & 63);

    Tile& tile =
        getOrCreateTile(
            tileX,
            tileY
        );

    tile.rows[localY] |=
        Word{1} << localX;
}


bool
GameOfLifeCPUSparse::getCell(
    std::int64_t x,
    std::int64_t y
) const
{
    if (x < 0 ||
        y < 0 ||
        x >= static_cast<std::int64_t>(m_width) ||
        y >= static_cast<std::int64_t>(m_height))
    {
        return false;
    }

    const std::int64_t tileX =
        x >> 6;

    const std::int64_t tileY =
        y >> 6;

    const Tile* tile =
        findTile(
            tileX,
            tileY
        );

    if (!tile)
        return false;

    const std::size_t localX =
        static_cast<std::size_t>(x & 63);

    const std::size_t localY =
        static_cast<std::size_t>(y & 63);

    return
        (
            tile->rows[localY] &
            (Word{1} << localX)
        ) != 0;
}


void
GameOfLifeCPUSparse::load(
    const Pattern& pattern,
    emper::i32 offsetX,
    emper::i32 offsetY
)
{
    for (const auto& cell : pattern.cells)
    {
        setCell(
            static_cast<std::int64_t>(cell.x) +
                offsetX,

            static_cast<std::int64_t>(cell.y) +
                offsetY
        );
    }

    #if DBG
    
    std::cout
    << "tiles=" << m_current.size()
    << " tileColumns=" << m_tileColumns
    << " tileRows=" << m_tileRows
    << '\n';

    #endif
    
}


void
GameOfLifeCPUSparse::clear()
{
    m_current.clear();
    m_next.clear();
    m_acc.clear();

    m_generation = 0;
    m_accumulator = 0.0f;
}


void
GameOfLifeCPUSparse::randomize(
    float probability
)
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
            {
                setCell(
                    static_cast<std::int64_t>(x),
                    static_cast<std::int64_t>(y)
                );
            }
        }
    }
}


void
GameOfLifeCPUSparse::step()
{


    
#ifdef DBG
    using Clock = std::chrono::steady_clock;

    const auto stepStart = Clock::now();
#endif

    m_next.clear();
    m_acc.clear();

    m_next.reserve(m_current.size() * 2 + 8);
    m_acc.reserve(m_current.size() * 8 + 16);

    // ------------------------------------------------------------
    // Scatter phase: for each non-empty source tile, add its live-neighbour
    // contributions into the 3x3 surrounding candidate tiles. This is the
    // transpose of the old per-candidate evaluate loop and needs ~9x fewer
    // hash lookups (each source is read once instead of once per 3x3 that it
    // participates in).
    // ------------------------------------------------------------

    std::size_t hashTouches = 0;

    auto acc =
        [this, &hashTouches](std::int64_t x, std::int64_t y) -> AccTile&
    {
        ++hashTouches;
        return m_acc[TileCoord{x, y}];
    };

    auto addRow =
        [&acc](AccTile& t, std::size_t row, Word value)
    {
        if (value == 0)
            return;

        BitCount cnt{
            t.c0[row],
            t.c1[row],
            t.c2[row],
            t.c3[row]
        };

        add(cnt, value);

        t.c0[row] = cnt.b0;
        t.c1[row] = cnt.b1;
        t.c2[row] = cnt.b2;
        t.c3[row] = cnt.b3;
    };

    const std::int64_t cols =
        static_cast<std::int64_t>(m_tileColumns);

    const std::int64_t rows =
        static_cast<std::int64_t>(m_tileRows);

    for (const auto& [coord, src] : m_current)
    {
        if (src.empty())
            continue;

        const std::int64_t sx = coord.x;
        const std::int64_t sy = coord.y;

        const bool hasUp    = sy > 0;
        const bool hasDown  = sy + 1 < rows;
        const bool hasLeft  = sx > 0;
        const bool hasRight = sx + 1 < cols;

        // Self tile: previous alive state + the eight intra-tile neighbours.
        AccTile& self = acc(sx, sy);
        for (std::size_t y = 0; y < TileSize; ++y)
        {
            const Word row = src.rows[y];
            const Word n   = (y > 0) ? src.rows[y - 1] : Word{0};
            const Word s   = (y + 1 < TileSize) ? src.rows[y + 1] : Word{0};

            BitCount cnt{
                self.c0[y],
                self.c1[y],
                self.c2[y],
                self.c3[y]
            };

            add(cnt, n);
            add(cnt, s);
            add(cnt, row << 1);
            add(cnt, row >> 1);
            add(cnt, n << 1);
            add(cnt, n >> 1);
            add(cnt, s << 1);
            add(cnt, s >> 1);

            self.c0[y] = cnt.b0;
            self.c1[y] = cnt.b1;
            self.c2[y] = cnt.b2;
            self.c3[y] = cnt.b3;
            self.alive[y] = row;
        }

        // Cardinal neighbours.
        if (hasUp)
        {
            AccTile& t = acc(sx, sy - 1);
            addRow(t, TileSize - 1, src.rows[0]);
            addRow(t, TileSize - 1, src.rows[0] << 1);
            addRow(t, TileSize - 1, src.rows[0] >> 1);
        }
        if (hasDown)
        {
            AccTile& t = acc(sx, sy + 1);
            addRow(t, 0, src.rows[TileSize - 1]);
            addRow(t, 0, src.rows[TileSize - 1] << 1);
            addRow(t, 0, src.rows[TileSize - 1] >> 1);
        }
        if (hasLeft)
        {
            AccTile& t = acc(sx - 1, sy);
            for (std::size_t y = 0; y < TileSize; ++y)
            {
                const Word c = src.rows[y];
                addRow(t, y, c << 63);
                if (y > 0)
                    addRow(t, y, src.rows[y - 1] << 63);
                if (y + 1 < TileSize)
                    addRow(t, y, src.rows[y + 1] << 63);
            }
        }
        if (hasRight)
        {
            AccTile& t = acc(sx + 1, sy);
            for (std::size_t y = 0; y < TileSize; ++y)
            {
                const Word c = src.rows[y];
                addRow(t, y, c >> 63);
                if (y > 0)
                    addRow(t, y, src.rows[y - 1] >> 63);
                if (y + 1 < TileSize)
                    addRow(t, y, src.rows[y + 1] >> 63);
            }
        }

        // Diagonal corner contributions.
        if (hasLeft && hasUp)
            addRow(acc(sx - 1, sy - 1), TileSize - 1, (src.rows[0] & 1) << 63);
        if (hasRight && hasUp)
            addRow(acc(sx + 1, sy - 1), TileSize - 1, src.rows[0] >> 63);
        if (hasLeft && hasDown)
            addRow(acc(sx - 1, sy + 1), 0, (src.rows[TileSize - 1] & 1) << 63);
        if (hasRight && hasDown)
            addRow(acc(sx + 1, sy + 1), 0, src.rows[TileSize - 1] >> 63);
    }

#if DBG
    const auto scatterEnd = Clock::now();
#endif
    // ------------------------------------------------------------
    // Combine phase: derive each candidate tile's next state from its
    // accumulated live-neighbour count and previous alive state.
    // ------------------------------------------------------------

    for (auto& [coord, a] : m_acc)
    {
        Tile result{};

        for (std::size_t y = 0; y < TileSize; ++y)
        {
            const BitCount cnt{
                a.c0[y],
                a.c1[y],
                a.c2[y],
                a.c3[y]
            };

            const Word alive = a.alive[y];

            result.rows[y] =
                (alive & equal2(cnt)) |
                equal3(cnt);
        }

        // X boundary
        if (coord.x == cols - 1)
        {
            for (Word& row : result.rows)
                row &= m_lastWordMask;
        }

        // Y boundary
        if (coord.y == rows - 1)
        {
            const std::size_t validRows =
                m_height % TileSize;

            if (validRows != 0)
            {
                for (std::size_t y = validRows;
                     y < TileSize;
                     ++y)
                {
                    result.rows[y] = 0;
                }
            }
        }

        if (!result.empty())
            m_next.emplace(coord, std::move(result));
    }

    m_current.swap(m_next);
    ++m_generation;

    // ------------------------------------------------------------
    // Instrumentation
    // ------------------------------------------------------------
 #if DBG
    if (m_generation <= 10 || m_generation % 100 == 0)
    {
       
        const auto now = Clock::now();

        const double scatterMs =
            std::chrono::duration<double, std::milli>(
                scatterEnd - stepStart
            ).count();

        const double stepMs =
            std::chrono::duration<double, std::milli>(
                now - stepStart
            ).count();
            

        std::cout
            << "[GoL] gen=" << m_generation
            << " current=" << m_current.size()
            << " candidates=" << m_acc.size()
            << " next=" << m_current.size()
            << " hashTouches=" << hashTouches
            << " scatter=" << scatterMs << " ms"
            << " step=" << stepMs << " ms"
            << '\n';    

    }
                
    #endif
}
GameOfLifeData
GameOfLifeCPUSparse::data() const
{
    GameOfLifeData data;

    data.width  = m_width;
    data.height = m_height;

    data.generation = m_generation;

    // The sparse backend stores cells as 64x64 bit-tiles in a hash map; iterate
    // only the non-empty tiles and bit-scan them to collect the alive cells.
    // This is O(live cells) — the same cost as the original sparse render loop
    // — and avoids materializing the (potentially huge) full grid.
    m_alive.clear();

    for (const auto& [coord, tile] : m_current)
    {
        const std::int64_t baseX =
            coord.x * static_cast<std::int64_t>(TileSize);

        const std::int64_t baseY =
            coord.y * static_cast<std::int64_t>(TileSize);

        for (std::size_t y = 0; y < TileSize; ++y)
        {
            Word row = tile.rows[y];

            while (row != 0)
            {
                const unsigned bit =
                    std::countr_zero(row);

                const std::int64_t x =
                    baseX +
                    static_cast<std::int64_t>(bit);

                const std::int64_t worldY =
                    baseY +
                    static_cast<std::int64_t>(y);

                if (
                    x >= 0 &&
                    worldY >= 0 &&
                    x < static_cast<std::int64_t>(m_width) &&
                    worldY < static_cast<std::int64_t>(m_height)
                )
                {
                    m_alive.push_back({
                        static_cast<emper::i32>(x),
                        static_cast<emper::i32>(worldY)
                    });
                }

                row &= row - 1;
            }
        }
    }

    data.aliveCells = m_alive;

    return data;
}
} // namespace emper::module::cgol