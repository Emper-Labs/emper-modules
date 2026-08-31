#pragma once 

#include <emper/Emper_Engine.h>

#include <string>
#include <vector>

#include "GameOfLifeData.h"

namespace emper::module::cgol
{

struct Pattern
{
    std::string name;

    emper::u32 width  = 0;
    emper::u32 height = 0;

    std::vector<CellCoordinate> cells;
};

Pattern loadRLE(const std::string& path);
};