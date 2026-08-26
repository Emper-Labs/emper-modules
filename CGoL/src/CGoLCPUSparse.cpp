#include "CGoLCPUSparse.h"

#include <algorithm>
#include <bit>
#include <random>

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
}


void
GameOfLifeCPUSparse::clear()
{
    m_current.clear();
    m_next.clear();

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
    TileMap next;
    TileMap candidates;

    next.reserve(m_current.size() * 2 + 8);
    candidates.reserve(m_current.size() * 3 + 8);

    /*
     * Every live tile can affect itself and its
     * eight neighboring tiles.
     *
     * Non-toroidal world:
     * tiles outside the world are ignored.
     */
    for (const auto& [coord, tile] : m_current)
    {
        if (tile.empty())
            continue;

        for (std::int64_t dy = -1; dy <= 1; ++dy)
        {
            for (std::int64_t dx = -1; dx <= 1; ++dx)
            {
                const std::int64_t tileX =
                    coord.x + dx;

                const std::int64_t tileY =
                    coord.y + dy;

                if (
                    tileX < 0 ||
                    tileY < 0 ||
                    tileX >= static_cast<std::int64_t>(
                        m_tileColumns
                    ) ||
                    tileY >= static_cast<std::int64_t>(
                        m_tileRows
                    )
                )
                {
                    continue;
                }

                candidates.try_emplace(
                    TileCoord{
                        tileX,
                        tileY
                    }
                );
            }
        }
    }

    static constexpr Tile emptyTile{};

    /*
     * No wrapping.
     *
     * Outside the world => nullptr.
     */
    auto getTile =
        [this](std::int64_t x,
               std::int64_t y) -> const Tile*
    {
        return findTile(x, y);
    };

    for (const auto& [coord, unused] : candidates)
    {
        const Tile* center =
            getTile(
                coord.x,
                coord.y
            );

        const Tile* up =
            getTile(
                coord.x,
                coord.y - 1
            );

        const Tile* down =
            getTile(
                coord.x,
                coord.y + 1
            );

        const Tile* left =
            getTile(
                coord.x - 1,
                coord.y
            );

        const Tile* right =
            getTile(
                coord.x + 1,
                coord.y
            );

        const Tile* upLeft =
            getTile(
                coord.x - 1,
                coord.y - 1
            );

        const Tile* upRight =
            getTile(
                coord.x + 1,
                coord.y - 1
            );

        const Tile* downLeft =
            getTile(
                coord.x - 1,
                coord.y + 1
            );

        const Tile* downRight =
            getTile(
                coord.x + 1,
                coord.y + 1
            );

        if (!center)
            center = &emptyTile;

        if (!up)
            up = &emptyTile;

        if (!down)
            down = &emptyTile;

        if (!left)
            left = &emptyTile;

        if (!right)
            right = &emptyTile;

        if (!upLeft)
            upLeft = &emptyTile;

        if (!upRight)
            upRight = &emptyTile;

        if (!downLeft)
            downLeft = &emptyTile;

        if (!downRight)
            downRight = &emptyTile;

        Tile result{};

        for (std::size_t y = 0; y < TileSize; ++y)
        {
            const bool firstRow =
                y == 0;

            const bool lastRow =
                y + 1 == TileSize;

            const Word row =
                center->rows[y];

            /*
             * NORTH
             */
            const Word north =
                firstRow
                    ? up->rows[TileSize - 1]
                    : center->rows[y - 1];

            /*
             * SOUTH
             */
            const Word south =
                lastRow
                    ? down->rows[0]
                    : center->rows[y + 1];

            /*
             * WEST / EAST
             */
            const Word west =
                (row << 1) |
                (left->rows[y] >> 63);

            const Word east =
                (row >> 1) |
                (right->rows[y] << 63);

            /*
             * NORTH-WEST
             *
             * If we are on the first row of this tile,
             * the source row is the last row of the tile
             * above.
             *
             * Otherwise it is the previous row of center,
             * and the carry comes from the left tile.
             */
            const Word northWest =
                firstRow
                    ? (
                        (up->rows[TileSize - 1] << 1) |
                        (upLeft->rows[TileSize - 1] >> 63)
                    )
                    : (
                        (center->rows[y - 1] << 1) |
                        (left->rows[y - 1] >> 63)
                    );

            /*
             * NORTH-EAST
             */
            const Word northEast =
                firstRow
                    ? (
                        (up->rows[TileSize - 1] >> 1) |
                        (upRight->rows[TileSize - 1] << 63)
                    )
                    : (
                        (center->rows[y - 1] >> 1) |
                        (right->rows[y - 1] << 63)
                    );

            /*
             * SOUTH-WEST
             */
            const Word southWest =
                lastRow
                    ? (
                        (down->rows[0] << 1) |
                        (downLeft->rows[0] >> 63)
                    )
                    : (
                        (center->rows[y + 1] << 1) |
                        (left->rows[y + 1] >> 63)
                    );

            /*
             * SOUTH-EAST
             */
            const Word southEast =
                lastRow
                    ? (
                        (down->rows[0] >> 1) |
                        (downRight->rows[0] << 63)
                    )
                    : (
                        (center->rows[y + 1] >> 1) |
                        (right->rows[y + 1] << 63)
                    );

            /*
             * Count all eight neighbors.
             */
            BitCount count{};

            add(count, northWest);
            add(count, north);
            add(count, northEast);

            add(count, west);
            add(count, east);

            add(count, southWest);
            add(count, south);
            add(count, southEast);

            /*
             * Conway's Game of Life:
             *
             * alive + 2 => survive
             * alive + 3 => survive
             * dead  + 3 => born
             */
            result.rows[y] =
                (row & equal2(count)) |
                equal3(count);
        }

        /*
         * Remove X padding in the final tile.
         *
         * Example:
         *
         * width = 100
         *
         * last tile has only 36 valid bits.
         */
        if (
            coord.x ==
            static_cast<std::int64_t>(
                m_tileColumns - 1
            )
        )
        {
            for (Word& row : result.rows)
                row &= m_lastWordMask;
        }

        /*
         * Remove Y padding in the final tile.
         *
         * Example:
         *
         * height = 100
         *
         * last tile has only 36 valid rows.
         */
        if (
            coord.y ==
            static_cast<std::int64_t>(
                m_tileRows - 1
            )
        )
        {
            const std::size_t validRows =
                m_height % TileSize;

            if (validRows != 0)
            {
                for (
                    std::size_t y = validRows;
                    y < TileSize;
                    ++y
                )
                {
                    result.rows[y] = 0;
                }
            }
        }

        /*
         * Sparse representation:
         * don't store dead tiles.
         */
        if (!result.empty())
        {
            next.emplace(
                coord,
                std::move(result)
            );
        }
    }

    m_current.swap(next);

    ++m_generation;
}

void
GameOfLifeCPUSparse::render(
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

    for (const auto& [coord, tile] : m_current)
    {
        const std::int64_t baseX =
            coord.x * static_cast<std::int64_t>(TileSize);

        const std::int64_t baseY =
            coord.y * static_cast<std::int64_t>(TileSize);

        for (std::size_t y = 0; y < TileSize; ++y)
        {
            Word row =
                tile.rows[y];

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
                    renderer.drawRect(
                        static_cast<float>(x) *
                            cellWidth,

                        static_cast<float>(worldY) *
                            cellHeight,

                        cellWidth,
                        cellHeight,
                        0xFFFFFFFF
                    );
                }

                row &= row - 1;
            }
        }
    }

    renderer.drawText(
        "Conway's Game of Life [CPU Sparse]",
        10.0f,
        10.0f,
        20.0f
    );
}

} // namespace emper::module::cgol