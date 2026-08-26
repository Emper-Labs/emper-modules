#include <emper/Emper_Engine.h>
#include <emper/interfaces/backend/IRenderer.h>
#include <emper/interfaces/module/ISystem.h>

#include <SDLOpenGLRenderer.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <string>

#include "CGoL.h"


namespace emper::module::cgol
{



Pattern loadRLE(const std::string& path)
{
    std::ifstream file(path);

    if (!file)
    {
        throw std::runtime_error(
            "Failed to open RLE file: " + path
        );
    }

    Pattern pattern;

    std::string line;
    std::string data;

    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        if (line[0] == '#')
        {
            if (line.size() > 3 && line[1] == 'N')
                pattern.name = line.substr(3);

            continue;
        }

        // Header:
        // x = 3, y = 3, rule = B3/S23
        if (line.find("x =") != std::string::npos)
        {
            auto parseValue =
                [&line](const char* key) -> emper::i32
            {
                const auto pos = line.find(key);

                if (pos == std::string::npos)
                    return 0;

                auto start =
                    pos + std::string(key).size();

                // Skip whitespace after "x ="
                while (
                    start < line.size() &&
                    std::isspace(
                        static_cast<unsigned char>(
                            line[start]
                        )
                    )
                )
                {
                    ++start;
                }

                auto end = start;

                while (
                    end < line.size() &&
                    std::isdigit(
                        static_cast<unsigned char>(
                            line[end]
                        )
                    )
                )
                {
                    ++end;
                }

                if (start == end)
                    throw std::runtime_error(
                        "Invalid RLE header: " + line
                    );

                return static_cast<emper::i32>(
                    std::stoi(
                        line.substr(
                            start,
                            end - start
                        )
                    )
                );
            };

            pattern.width  = parseValue("x =");
            pattern.height = parseValue("y =");

            continue;
        }

        data += line;
    }

    // Decode RLE
    emper::i32 x = 0;
    emper::i32 y = 0;

    std::size_t i = 0;

    while (i < data.size())
    {
        emper::i32 count = 0;

        while (
            i < data.size() &&
            std::isdigit(
                static_cast<unsigned char>(data[i])
            )
        )
        {
            count =
                count * 10 +
                static_cast<emper::i32>(
                    data[i] - '0'
                );

            ++i;
        }

        if (count == 0)
            count = 1;

        if (i >= data.size())
            break;

        const char token = data[i++];

        switch (token)
        {
            case 'o':
            {
                for (emper::i32 n = 0; n < count; ++n)
                {
                    pattern.cells.push_back({
                        x + n,
                        y
                    });
                }

                x += count;
                break;
            }

            case 'b':
            {
                x += count;
                break;
            }

            case '$':
            {
                y += count;
                x = 0;
                break;
            }

            case '!':
                return pattern;

            default:
                throw std::runtime_error(
                    "Invalid RLE token '" +
                    std::string(1, token) +
                    "' in: " +
                    path
                );
        }
    }

    return pattern;
}



void 
GameOfLife::tick(emper::f32 dt)
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
GameOfLife::render(
        emper::interfaces::backend::IRenderer& renderer
    )
{
    const float cellWidth =
        static_cast<float>(renderer.windowWidth()) /
        static_cast<float>(m_width);

    const float cellHeight =
        static_cast<float>(renderer.windowHeight()) /
        static_cast<float>(m_height);

    for (std::size_t y = 0; y < m_height; ++y)
    {
        for (std::size_t x = 0; x < m_width; ++x)
        {
            if (!m_current[y * m_width + x])
                continue;

            renderer.drawRect(
                static_cast<float>(x) * cellWidth,
                static_cast<float>(y) * cellHeight,
                cellWidth,
                cellHeight,
                0xFFFFFFFF
            );
        }
    }

    renderer.drawText(
        "Conway's Game of Life",
        10.0f,
        10.0f,
        1.0f
    );
}


void 
GameOfLife::randomize(float probability)
{
    std::mt19937 rng{std::random_device{}()};
    std::bernoulli_distribution alive(probability);

    for (auto& cell : m_current)
        cell = alive(rng);
}


void 
GameOfLife::load(
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
        continue;

    m_current[
        static_cast<std::size_t>(y) * m_width +
        static_cast<std::size_t>(x)
    ] = 1;
}
}

void 
GameOfLife::clear()
{
    std::fill(m_current.begin(), m_current.end(), 0);
    std::fill(m_next.begin(), m_next.end(), 0);

    m_generation = 0;
    m_accumulator = 0.0f;
}

#include <omp.h>

void
GameOfLife::step()
{
    #pragma omp parallel for
    for (std::size_t y = 1; y < m_height - 1; ++y)
    {
        const auto* rowUp   = &m_current[(y - 1) * m_width];
        const auto* row     = &m_current[y * m_width];
        const auto* rowDown = &m_current[(y + 1) * m_width];
        auto* next          = &m_next[y * m_width];

        for (std::size_t x = 1; x < m_width - 1; ++x)
        {
            const int neighbors =
                rowUp[x - 1] + rowUp[x] + rowUp[x + 1] +
                row[x - 1]              + row[x + 1] +
                rowDown[x - 1] + rowDown[x] + rowDown[x + 1];

            next[x] =
                row[x]
                    ? neighbors == 2 || neighbors == 3
                    : neighbors == 3;
        }
    }

    m_current.swap(m_next);
    ++m_generation;
}

int 
GameOfLife::countNeighbors(
    std::size_t x,
    std::size_t y
) const
{
    const std::size_t left =
        (x == 0) ? m_width - 1 : x - 1;

    const std::size_t right =
        (x + 1 == m_width) ? 0 : x + 1;

    const std::size_t up =
        (y == 0) ? m_height - 1 : y - 1;

    const std::size_t down =
        (y + 1 == m_height) ? 0 : y + 1;

    return
        m_current[up    * m_width + left]  +
        m_current[up    * m_width + x]     +
        m_current[up    * m_width + right] +

        m_current[y     * m_width + left]  +
        m_current[y     * m_width + right] +

        m_current[down  * m_width + left]  +
        m_current[down  * m_width + x]     +
        m_current[down  * m_width + right];
}
}