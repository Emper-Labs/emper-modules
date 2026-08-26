#pragma once 

#include <emper/Emper_Engine.h>

#include <string>
#include <vector>


namespace emper::module::cgol
{

struct CellCoordinate
{
    emper::i32 x;
    emper::i32 y;
};

struct Pattern
{
    std::string name;

    emper::i32 width  = 0;
    emper::i32 height = 0;

    std::vector<CellCoordinate> cells;
};

Pattern loadRLE(const std::string& path);
};