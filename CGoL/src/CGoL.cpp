#include <emper/EmperEngine.h>

#include <algorithm>
#include <cctype>
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


}