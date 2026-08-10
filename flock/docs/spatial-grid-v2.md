# Emper Flock — Compact Spatial Grid (v2)

This document describes the GPU spatial-grid redesign for the Emper flock
module. It replaces the fixed-capacity bucket grid with a compact,
cell-contiguous layout that eliminates the uncoalesced memory access that
dominated the previous neighbour traversal.

---

## 1. Bottleneck analysis (why the old grid was slow)

The previous pipeline was:

```
PASS 0  clear cellCounts[]
PASS 1  build grid: cellParticles[cellId * maxCellParticles + slot] = index
PASS 2  flock: for each of 9 cells, for slot in [0, count):
            neighbour = cellParticles[cellId * maxCellParticles + slot]
            state     = inputStates[neighbour]     // <-- random access
```

Two structural problems made PASS 2 memory-bound:

1. **Uncoalesced neighbour-state reads.**
   `cellParticles[]` stored *original* boid indices. Two boids in the same
   cell could be thousands of slots apart in `inputStates[]`. Every
   `inputStates[neighbour]` load was a 16-byte gather from an arbitrary
   address. With ~100k boids and ~2300 candidate slots per boid (9 cells ×
   ~256 avg occupancy), that is ~230M scattered 16-byte loads per frame —
   the dominant cost. Adding separation/alignment/cohesion ALU did not
   change the time because the bottleneck was the memory pattern, not the
   math.

2. **Fixed-capacity bucket waste.**
   `cellParticles` was allocated as `cellCount × maxCellParticles` (1024)
   = ~1.6 MB for only 100k boids. The 4 KB stride between cells meant each
   cell's occupants were spread across cache lines, and dense cells could
   silently overflow (boids beyond 1024 in a cell were dropped).

The reference project (`flocking.cpp`) avoids both by **sorting the boid
buffer by cell** and traversing `indices[cellId]..indices[cellId+1]`
contiguous ranges. Emper adopts the same *idea* (compact ranges +
reordered state) but not its implementation (a bitonic sort of an AoS
buffer). Emper's ping-pong SoA layout lets us build the same layout with a
cheap O(N) scatter instead of an O(N log²N) sort.

---

## 2. Architecture proposal

New GPU data flow (5 dispatches per tick):

```
input (persistent buffer 0, arbitrary order)
   │
   ▼
PASS 0  clear cellCountsA[], cellCountsB[]
   │
   ▼
PASS 1  count: cellCountsA[cellOf(pos)]++          (exact occupancy)
   │
   ▼
PASS 2  prefix sum: cellStart[c] = exclusive scan  (cellStart[c+1] = end)
   │
   ▼
PASS 3  scatter: sorted[cellStart[cell] + slot] = state[i]
                  sortedTeams[...] = team[i]        (cell-contiguous)
   │
   ▼
PASS 4  flock: for each of 9 cells, for j in [cellStart[c], cellStart[c+1]):
                  neighbour = sorted[j]             (sequential!)
   │
   ▼
output (persistent buffer 0, integrated)
```

Buffer roles are **fixed** (no swap):

- `stateBuffers_[0]` / `teamBuffers_[0]` = persistent integrated state
  (also the rendered buffer).
- `stateBuffers_[1]` / `teamBuffers_[1]` = per-frame cell-sorted scratch.

Because two state-touching passes run in opposite directions each tick
(reorder then update), a classical single-swap ping-pong does not apply.

### Design choice: reordered state (B), not an index map (A)

We reorder the actual state/team buffers rather than keeping a
`particleIndices[]` indirection. In the hot loop this means:

```glsl
// Design A (rejected): two loads per candidate, second is random
uint boidIndex = particleIndices[j];
vec4 nb = inputStates[boidIndex];

// Design B (chosen): one sequential load per candidate
vec4 nb = inputStates[j];
```

Design B gives the best memory locality and lowest global-memory traffic in
the traversal. The cost is that boids are anonymous (their buffer slot
changes each frame), which is fine: flocking depends only on
position/velocity/team, and the team is reordered alongside the state.

---

## 3. Shader implementation

`assets/shaders/flock_comp.comp` — single shader, `pass` uniform 0..4.

Bindings:

| Binding | Buffer            | Role                                   |
|---------|-------------------|----------------------------------------|
| 0       | inputStates       | vec4[] (persistent or sorted)          |
| 1       | outputStates      | vec4[] (sorted or persistent)          |
| 2       | inputTeams        | uint[] (persistent or sorted)          |
| 3       | outputTeams       | uint[] (sorted or persistent)          |
| 4       | cellCountsA       | occupancy for prefix sum               |
| 5       | cellCountsB       | scatter counter for reorder            |
| 6       | cellStart         | cellStart[cellCount_ + 1]              |
| 7       | benchmarkCandidates | per-boid candidate count (optional)  |

Key passes:

- **PASS 1** `buildGrid`: `atomicAdd(cellCountsA[cellId], 1u)`.
- **PASS 2** `buildCellStart`: single-threaded exclusive scan over the
  (small) cell grid; writes `cellStart[c]` and `cellStart[cellCount]`.
- **PASS 3** `reorderBoids`: `slot = atomicAdd(cellCountsB[cellId], 1u)`;
  `dest = cellStart[cellId] + slot`; copy state + team.
- **PASS 4** `updateBoid`: neighbour loop reads `inputStates[j]` for
  `j in [cellStart[cellId], cellStart[cellId+1])` — sequential.

The flocking math (separation/alignment/cohesion, team filter,
`maxNeighbours` early exit, speed clamps, toroidal wrap) is unchanged from
the original.

---

## 4. C++ OpenGL changes

`src/GpuCompute.h` / `src/GpuCompute.cpp`:

- **SSBOs**: replaced `cellCountBuffer_` + `cellParticleBuffer_` with
  `cellCountBuffers_[2]`, `cellStartBuffer_`, `benchmarkBuffer_`; replaced
  single `teamBuffer_` with `teamBuffers_[2]`.
- **Allocation**: `cellStartBuffer_` is `(cellCount_ + 1) × u32`;
  `cellCountBuffers_` are `cellCount_ × u32`; `benchmarkBuffer_` is
  `boidCount_ × u32`.
- **Buffer roles**: fixed (no swap). Buffer 0 = persistent, buffer 1 =
  sorted scratch.
- **Dispatches**: 5 passes (clear, count, prefix, reorder, flock) with a
  `memoryBarrier()` after each. Pass 2 dispatches a single workgroup.
- **Rebind between pass 3 and pass 4**: pass 4 reads the sorted buffer 1
  and writes buffer 0, so bindings 0/1 and 2/3 are swapped before pass 4.
- **Uniforms**: removed `maxCellParticles`; added `benchmark`.
- **Benchmark hooks**: `setBenchmarkEnabled(bool)` and
  `readCandidateCounts(std::vector<uint32_t>&)`.

---

## 5. Compatibility

**Unchanged:**

- `Flock.h` / `Flock.cpp` — public module API and config.
- `CpuCompute.*` — CPU path untouched.
- `flock_ver.ver` / `flock_frag.frag` — render shaders. They read
  `states[gl_VertexID]` and `teams[gl_VertexID]` from buffer 0, which is
  the persistent integrated state; boids are anonymous so rendering is
  unaffected.
- `emper-samples/flock/src/main.cpp` — sample unchanged.
- Backend interfaces (`ICompute.h`, `OpenGLComputeBackend`) — unchanged.

**Changed:**

- `flock_comp.comp` — new 5-pass pipeline.
- `GpuCompute.h` / `GpuCompute.cpp` — new buffers, fixed roles, 5
  dispatches, benchmark hooks.

**Behavior preserved:** same flocking equations, team filtering,
`maxNeighbours` semantics, toroidal wrap, and speed clamps.

---

## 6. Benchmark plan

Incremental measurement to isolate which optimization matters. Use the
existing FPS counter in `Flock.cpp` plus the new benchmark hooks.

| Step | Configuration | What it isolates |
|------|---------------|------------------|
| 1    | Old fixed grid (git baseline) | baseline |
| 2    | Compact grid, no reorder (indices only) | cost of removing bucket stride |
| 3    | Compact + reordered state (this v2) | cost of sequential traversal |
| 4    | v2 + full flock math | cost of flocking ALU on top of v2 |

**Metrics per step:**

- FPS and ms/frame (CPU-side `std::chrono` around `tick()`).
- Candidate checks/frame and average candidates/boid — via
  `setBenchmarkEnabled(true)` + `readCandidateCounts()`.
- Maximum cell occupancy — read back `cellCountsA` (or derive from
  `cellStart` diffs) after PASS 1.
- GPU memory usage — sum of SSBO sizes (see below).
- Number of compute dispatches — 3 (old) vs 5 (new).

**Memory footprint (100k boids, 390 cells):**

| Buffer            | Size        |
|-------------------|-------------|
| stateBuffers[2]   | 2 × 1.6 MB  |
| teamBuffers[2]    | 2 × 0.4 MB  |
| cellCountBuffers[2] | 2 × 1.6 KB |
| cellStartBuffer   | 1.6 KB      |
| benchmarkBuffer   | 0.4 MB      |
| renderConfig      | 16 B        |
| **Total**         | ~4.8 MB     |

(Old grid: ~1.6 MB state + ~1.6 MB cellParticles + ~0.4 MB teams ≈ 3.6 MB;
new layout is comparable but with dramatically better access patterns.)

**Expected result (to be verified by measurement):** PASS 4 should become
memory-bound on *sequential* streaming rather than scattered gathers, so
the traversal cost should drop by a large factor. The exact number depends
on the GPU's cache/bandwidth and must be measured — this document does not
claim a specific FPS without a benchmark run.

---

## 7. Notes / future work

- The prefix-sum pass is a single-threaded serial scan. It is negligible
  for < 4096 cells but should be replaced with a workgroup scan if the
  grid ever grows large.
- Shared-memory/workgroup cooperative loading of the 9-cell neighbourhood
  is a possible further optimization, but only if profiling shows the
  sequential traversal is still the bottleneck.
- The scatter pass uses one atomic per boid on `cellCountsB`. If profiling
  shows atomic contention, a warp-aggregated or two-level counter could be
  introduced.