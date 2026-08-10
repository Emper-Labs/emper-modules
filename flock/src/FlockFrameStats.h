#pragma once

#include <cstddef>

namespace emper::module {

// Per-frame statistics collected by either the CPU or GPU flock
// implementation. Used by Flock::Impl::render to print diagnostic
// metrics alongside the FPS counter.
struct FlockFrameStats
{
    std::size_t boidCount = 0;

    // Total number of spatial-grid candidate entries examined
    // before distance/team culling, summed over all boids.
    std::size_t candidateChecks = 0;

    // Total number of neighbours actually used for steering
    // (same-team, within perception radius), summed over all boids.
    std::size_t neighbours = 0;

    std::size_t maxCandidatesPerBoid = 0;
    std::size_t maxNeighboursPerBoid = 0;
};

} // namespace emper::module