#include "UniformGrid.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace emper::module
{

UniformGrid::UniformGrid(
    float worldWidth,
    float worldHeight,
    float cellSize)
{
    resize(
        worldWidth,
        worldHeight,
        cellSize);
}

void UniformGrid::resize(
    float worldWidth,
    float worldHeight,
    float cellSize)
{
    worldWidth_ =
        worldWidth > 0.0f
            ? worldWidth
            : 1.0f;

    worldHeight_ =
        worldHeight > 0.0f
            ? worldHeight
            : 1.0f;

    cellSize_ =
        cellSize > 0.0f
            ? cellSize
            : 1.0f;

    invCellSize_ =
        1.0f / cellSize_;

    cols_ =
        std::max(
            1,
            static_cast<int>(
                std::floor(
                    worldWidth_ *
                    invCellSize_)));

    rows_ =
        std::max(
            1,
            static_cast<int>(
                std::floor(
                    worldHeight_ *
                    invCellSize_)));

    clear();
}

void UniformGrid::clear()
{
    xs_.clear();
    ys_.clear();

    ordered_.clear();

    cellStart_.clear();
    cellCount_.clear();
    cursor_.clear();

    count_ = 0;
}

// ---------------------------------------------------------------------------
// SoA rebuild
// ---------------------------------------------------------------------------

void UniformGrid::rebuild(
    const float* xs,
    const float* ys,
    std::size_t count)
{
    if (xs == nullptr ||
        ys == nullptr ||
        count == 0)
    {
        clear();
        return;
    }

    const std::size_t totalCells =
        static_cast<std::size_t>(cols_) *
        static_cast<std::size_t>(rows_);

    // ------------------------------------------------------------
    // Copy input positions into internal SoA storage.
    // ------------------------------------------------------------

    xs_.assign(
        xs,
        xs + count);

    ys_.assign(
        ys,
        ys + count);

    count_ = count;

    // ------------------------------------------------------------
    // Prepare cell data.
    // ------------------------------------------------------------

    ordered_.resize(count);

    cellCount_.assign(
        totalCells,
        0);

    // ------------------------------------------------------------
    // Pass 1:
    //
    // Count how many objects belong to each cell.
    // ------------------------------------------------------------

    for (std::size_t i = 0;
         i < count;
         ++i)
    {
        const int gx =
            gridCoord(xs_[i]);

        const int gy =
            gridCoord(ys_[i]);

        const std::size_t cell =
            wrapCell(
                gx,
                gy);

        ++cellCount_[cell];
    }

    // ------------------------------------------------------------
    // Prefix sum.
    //
    // cellStart_[c] gives the first object belonging to cell c.
    // cellStart_[c + 1] gives one-past-the-end.
    // ------------------------------------------------------------

    cellStart_.resize(
        totalCells + 1);

    cellStart_[0] = 0;

    for (std::size_t c = 0;
         c < totalCells;
         ++c)
    {
        cellStart_[c + 1] =
            cellStart_[c] +
            cellCount_[c];
    }

    // ------------------------------------------------------------
    // Prepare scatter cursors.
    //
    // cursor_[cell] starts at the beginning of that cell's range.
    // ------------------------------------------------------------

    if (cursor_.size() < totalCells)
    {
        cursor_.resize(
            totalCells);
    }

    std::copy(
        cellStart_.begin(),
        cellStart_.begin() + totalCells,
        cursor_.begin());

    // ------------------------------------------------------------
    // Pass 2:
    //
    // Scatter object indices into cell ranges.
    // ------------------------------------------------------------

    for (std::size_t i = 0;
         i < count;
         ++i)
    {
        const int gx =
            gridCoord(xs_[i]);

        const int gy =
            gridCoord(ys_[i]);

        const std::size_t cell =
            wrapCell(
                gx,
                gy);

        ordered_[cursor_[cell]++] =
            i;
    }
}

// ---------------------------------------------------------------------------
// AoS rebuild
// ---------------------------------------------------------------------------

void UniformGrid::rebuild(
    const emper::Vec2* positions,
    std::size_t count)
{
    if (positions == nullptr ||
        count == 0)
    {
        clear();
        return;
    }

    xs_.resize(count);
    ys_.resize(count);

    for (std::size_t i = 0;
         i < count;
         ++i)
    {
        xs_[i] =
            positions[i].x;

        ys_[i] =
            positions[i].y;
    }

    // Build directly from the internal SoA arrays.
    //
    // IMPORTANT:
    // Calling rebuild(xs_.data(), ys_.data(), count) here would
    // re-copy the same arrays into themselves. Therefore the actual
    // grid construction is performed below.

    const std::size_t totalCells =
        static_cast<std::size_t>(cols_) *
        static_cast<std::size_t>(rows_);

    count_ = count;

    ordered_.resize(count);

    cellCount_.assign(
        totalCells,
        0);

    // ------------------------------------------------------------
    // Pass 1: count objects per cell.
    // ------------------------------------------------------------

    for (std::size_t i = 0;
         i < count;
         ++i)
    {
        const std::size_t cell =
            wrapCell(
                gridCoord(xs_[i]),
                gridCoord(ys_[i]));

        ++cellCount_[cell];
    }

    // ------------------------------------------------------------
    // Prefix sum.
    // ------------------------------------------------------------

    cellStart_.resize(
        totalCells + 1);

    cellStart_[0] = 0;

    for (std::size_t c = 0;
         c < totalCells;
         ++c)
    {
        cellStart_[c + 1] =
            cellStart_[c] +
            cellCount_[c];
    }

    // ------------------------------------------------------------
    // Prepare cursors.
    // ------------------------------------------------------------

    if (cursor_.size() < totalCells)
    {
        cursor_.resize(
            totalCells);
    }

    std::copy(
        cellStart_.begin(),
        cellStart_.begin() + totalCells,
        cursor_.begin());

    // ------------------------------------------------------------
    // Pass 2: scatter.
    // ------------------------------------------------------------

    for (std::size_t i = 0;
         i < count;
         ++i)
    {
        const std::size_t cell =
            wrapCell(
                gridCoord(xs_[i]),
                gridCoord(ys_[i]));

        ordered_[cursor_[cell]++] =
            i;
    }
}

} // namespace emper::module