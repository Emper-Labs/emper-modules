#pragma once

#include <emper/Types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace emper::module
{

// A uniform spatial grid for fast fixed-radius neighbour queries.
//
// The world is treated as toroidal: positions wrap around the world
// bounds, and neighbour queries wrap as well.
//
// Positions are stored in a structure-of-arrays layout
// (separate x / y float arrays) so the innermost query loop
// accesses contiguous scalar data.
//
// maxNeighbours:
//   0 -> unlimited
//   >0 -> stop after visiting maxNeighbours valid neighbours
//
// Cell traversal starts at the query cell and expands outward
// in Chebyshev rings. This makes nearby cells get visited before
// farther cells without allocating or sorting cells per query.
class UniformGrid
{
public:
    UniformGrid() = default;

    UniformGrid(
        float worldWidth,
        float worldHeight,
        float cellSize);

    void resize(
        float worldWidth,
        float worldHeight,
        float cellSize);

    void clear();

    // Rebuild from interleaved positions.
    // The data is deinterleaved into internal SoA storage.
    void rebuild(
        const emper::Vec2* positions,
        std::size_t count);

    // Rebuild from separate x / y arrays.
    // Data is copied into internal SoA storage so queries remain
    // independent from the caller's storage lifetime.
    void rebuild(
        const float* xs,
        const float* ys,
        std::size_t count);

    std::size_t size() const
    {
        return count_;
    }

    // Visit neighbours within `radius` of (x, y).
    //
    // Callback signature:
    //
    // visitor(
    //     std::size_t index,
    //     float wrappedDx,
    //     float wrappedDy,
    //     float distanceSq
    // )
    //
    // distanceSq is squared distance. No sqrt is performed here.
    //
    // The query visits cells from near to far using Chebyshev rings.
    // This does NOT guarantee that maxNeighbours are the mathematically
    // nearest individual particles; it only guarantees that nearby
    // cells are processed first.
    template <typename Visitor>
    void query(
        float x,
        float y,
        float radius,
        std::size_t maxNeighbours,
        Visitor&& visitor) const
    {
        if (cellSize_ <= 0.0f ||
            count_ == 0 ||
            radius < 0.0f)
        {
            return;
        }

        const float halfWidth =
            worldWidth_ * 0.5f;

        const float halfHeight =
            worldHeight_ * 0.5f;

        const float radiusSq =
            radius * radius;

        const float* const xs =
            xs_.data();

        const float* const ys =
            ys_.data();

        // Query center cell.
        const int centerX =
            gridCoord(x);

        const int centerY =
            gridCoord(y);

        // Number of cells that must be inspected in each direction.
        const int cellRadius =
            static_cast<int>(
                std::ceil(
                    radius *
                    invCellSize_));

        std::size_t neighbours = 0;

        // ------------------------------------------------------------
        // Expand outward from the query cell.
        //
        // Ring 0:
        //
        //                 X
        //
        // Ring 1:
        //
        //             X X X
        //             X   X
        //             X X X
        //
        // Ring 2:
        //
        //         X X X X X
        //         X       X
        //         X       X
        //         X       X
        //         X X X X X
        //
        // This is Chebyshev-distance ordering.
        // ------------------------------------------------------------

        for (int ring = 0;
             ring <= cellRadius;
             ++ring)
        {
            const int minOffset = -ring;
            const int maxOffset =  ring;

            for (int dy = minOffset;
                 dy <= maxOffset;
                 ++dy)
            {
                for (int dx = minOffset;
                     dx <= maxOffset;
                     ++dx)
                {
                    // Only visit the outer edge of this ring.
                    //
                    // Ring 0:
                    //   max(abs(dx), abs(dy)) == 0
                    //
                    // Ring 1:
                    //   max(abs(dx), abs(dy)) == 1
                    //
                    if (std::max(
                            std::abs(dx),
                            std::abs(dy)) != ring)
                    {
                        continue;
                    }

                    const int gx =
                        centerX + dx;

                    const int gy =
                        centerY + dy;

                    const std::size_t cell =
                        wrapCell(gx, gy);

                    const std::size_t start =
                        cellStart_[cell];

                    const std::size_t end =
                        cellStart_[cell + 1];

                    // ------------------------------------------------
                    // Visit objects stored in this cell.
                    // ------------------------------------------------

                    for (std::size_t k = start;
                         k < end;
                         ++k)
                    {
                        const std::size_t idx =
                            ordered_[k];

                        float wrappedDx =
                            xs[idx] - x;

                        float wrappedDy =
                            ys[idx] - y;

                        // Toroidal wrapping.
                        if (wrappedDx > halfWidth)
                            wrappedDx -= worldWidth_;

                        if (wrappedDx < -halfWidth)
                            wrappedDx += worldWidth_;

                        if (wrappedDy > halfHeight)
                            wrappedDy -= worldHeight_;

                        if (wrappedDy < -halfHeight)
                            wrappedDy += worldHeight_;

                        const float distanceSq =
                            wrappedDx * wrappedDx +
                            wrappedDy * wrappedDy;

                        if (distanceSq > radiusSq)
                            continue;

                        visitor(
                            idx,
                            wrappedDx,
                            wrappedDy,
                            distanceSq);

                        // maxNeighbours == 0 means unlimited.
                        if (maxNeighbours != 0)
                        {
                            ++neighbours;

                            if (neighbours >= maxNeighbours)
                                return;
                        }
                    }
                }
            }
        }
    }

private:

    int gridCoord(float value) const
    {
        return static_cast<int>(
            std::floor(
                value * invCellSize_));
    }

    std::size_t wrapCell(
        int gx,
        int gy) const
    {
        const int wrappedX =
            ((gx % cols_) + cols_) % cols_;

        const int wrappedY =
            ((gy % rows_) + rows_) % rows_;

        return static_cast<std::size_t>(
            wrappedY * cols_ +
            wrappedX);
    }

private:

    float worldWidth_  = 0.0f;
    float worldHeight_ = 0.0f;

    float cellSize_ =
        1.0f;

    float invCellSize_ =
        1.0f;

    int cols_ = 1;
    int rows_ = 1;

    // SoA position storage.
    std::vector<float> xs_;
    std::vector<float> ys_;

    // Objects ordered by cell.
    std::vector<std::size_t> ordered_;

    // Exclusive prefix sum.
    //
    // Cell c contains:
    //
    // [cellStart_[c], cellStart_[c + 1])
    //
    std::vector<std::size_t> cellStart_;

    // Number of objects in each cell.
    std::vector<std::size_t> cellCount_;

    // Temporary cursor used during scatter.
    std::vector<std::size_t> cursor_;

    std::size_t count_ = 0;
};

} // namespace emper::module