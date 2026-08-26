#pragma once

#include "CGoL.h"

#include <emper/Emper_Engine.h>
#include <emper/interfaces/backend/IRenderer.h>
#include <emper/interfaces/module/ISystem.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <unordered_map>

namespace emper::module::cgol
{

class GameOfLifeCPUSparse
    : public emper::interfaces::module::ISystem
    , public emper::interfaces::behavior::IRenderable
{
public:

    using Word = std::uint64_t;

    static constexpr std::size_t TileSize  = 64;
    static constexpr std::size_t TileWords = TileSize;

    struct Tile
    {
        std::array<Word, TileWords> rows{};

        [[nodiscard]]
        bool empty() const noexcept
        {
            for (const Word row : rows)
            {
                if (row != 0)
                    return false;
            }

            return true;
        }
    };

private:

    struct TileCoord
    {
        std::int64_t x;
        std::int64_t y;

        bool operator==(const TileCoord&) const = default;
    };

    struct TileCoordHash
    {
        std::size_t operator()(
            const TileCoord& coord
        ) const noexcept;
    };

    using TileMap =
        std::unordered_map<
            TileCoord,
            Tile,
            TileCoordHash
        >;

public:

    GameOfLifeCPUSparse(
        std::size_t width,
        std::size_t height
    );

    void tick(emper::f32 dt);

    void render(
        emper::interfaces::backend::IRenderer& renderer
    ) override;

    void load(
        const Pattern& pattern,
        emper::i32 offsetX = 0,
        emper::i32 offsetY = 0
    );

    void clear();

    void randomize(
        float probability = 0.15f
    );

    [[nodiscard]]
    std::size_t generation() const noexcept
    {
        return m_generation;
    }

    [[nodiscard]]
    std::size_t tileCount() const noexcept
    {
        return m_current.size();
    }

private:

    struct BitCount
    {
        Word b0 = 0;
        Word b1 = 0;
        Word b2 = 0;
        Word b3 = 0;
    };

    static void add(
        BitCount& count,
        Word value
    ) noexcept;

    [[nodiscard]]
    static Word equal2(
        const BitCount& count
    ) noexcept;

    [[nodiscard]]
    static Word equal3(
        const BitCount& count
    ) noexcept;

    [[nodiscard]]
    const Tile* findTile(
        std::int64_t tileX,
        std::int64_t tileY
    ) const noexcept;

    [[nodiscard]]
    Tile& getOrCreateTile(
        std::int64_t tileX,
        std::int64_t tileY
    );

    void setCell(
        std::int64_t x,
        std::int64_t y
    );

    [[nodiscard]]
    bool getCell(
        std::int64_t x,
        std::int64_t y
    ) const;

    void step();

private:

    std::size_t m_width;
    std::size_t m_height;

    std::size_t m_tileColumns;
    std::size_t m_tileRows;

    Word m_lastWordMask = ~Word{0};

    TileMap m_current;
    TileMap m_next;

    std::size_t m_generation = 0;

    float m_accumulator = 0.0f;
};

} // namespace emper::module::cgol