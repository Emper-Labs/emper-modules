#include "CpuCompute.h"

#include "Flock.h"

#include <UniformGrid.h>
#include <emper/simulation/world/World.h>

#include <atomic>
#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>
#include <cassert>
#include <iostream>

namespace emper::module {
namespace {

constexpr f32 Pi2 = 6.28318530718f;
constexpr f32 EpsilonSquared = 0.000001f;

struct Boid
{
    Vec2 position;
    Vec2 velocity;
    std::size_t team = 0;
};

Vec2 limit(Vec2 value, f32 maximum)
{
    const f32 lengthSquared = value.x * value.x + value.y * value.y;
    const f32 maximumSquared = maximum * maximum;
    if (lengthSquared > maximumSquared && lengthSquared > 0.0f)
    {
        const f32 scale = maximum / std::sqrt(lengthSquared);
        value.x *= scale;
        value.y *= scale;
    }
    return value;
}

void validate(const FlockConfig& config)
{
    if (config.worldWidth <= 0.0f || config.worldHeight <= 0.0f)
        throw std::invalid_argument("Flock world dimensions must be positive");
    if (config.maxSpeed <= 0.0f || config.maxForce <= 0.0f ||
        config.initialSpeed <= 0.0f)
    {
        throw std::invalid_argument(
            "Flock speed and force values must be positive");
    }
    if (config.perceptionRadius <= 0.0f || config.separationRadius <= 0.0f)
        throw std::invalid_argument("Flock radii must be positive");
    if (config.teamCount == 0)
        throw std::invalid_argument("Flock team count must be greater than zero");
}

} // namespace

class FlockCpuCompute::Impl
{
public:
    Impl(simulation::world::World& world, FlockConfig& config)
        : world_(world), config_(config)
    {
        validate(config_);

        world_.registerType<Boid>()
            .field<&Boid::position>()
            .field<&Boid::velocity>()
            .field<&Boid::team>();

        world_.reserve<Boid>(config_.boidCount);
        spawn();
    }

    void tick(f32 dt)
    {
        if (dt <= 0.0f)
            return;

        auto boids = world_.storage<Boid>();

        auto positions =
            boids.column<&Boid::position>();

        auto velocities =
            boids.column<&Boid::velocity>();

        auto teams =
            boids.column<&Boid::team>();

        const std::size_t count =
            boids.size();

        if (count == 0)
            return;

        resizeGridIfNeeded();

        grid_.rebuild(
            positions.data(),
            count
        );

        // ------------------------------------------------------------
        // Precomputed constants
        // ------------------------------------------------------------

        const f32 separationRadius =
            config_.separationRadius;

        const f32 separationRadiusSquared =
            separationRadius *
            separationRadius;

        const f32 cohesionDeadZone =
            config_.cohesionDeadZone;

        const f32 cohesionDeadZoneSquared =
            cohesionDeadZone *
            cohesionDeadZone;

        const f32 cohesionRadius =
            config_.cohesionRadius;

        const f32 minimumSpeed =
            config_.initialSpeed * 0.75f;

        const f32 minimumSpeedSquared =
            minimumSpeed * minimumSpeed;

        const f32 maximumSpeedSquared =
            config_.maxSpeed *
            config_.maxSpeed;

        const f32 maximumForceSquared =
            config_.maxForce *
            config_.maxForce;

        // ------------------------------------------------------------
        // Per-frame stats (accumulated across all threads)
        // ------------------------------------------------------------

        std::atomic<std::size_t> totalCandidates{0};
        std::atomic<std::size_t> totalNeighbours{0};

        // ------------------------------------------------------------
        // Steering
        // ------------------------------------------------------------

        #pragma omp parallel for
       for (long long i = 0; i < static_cast<long long>(count); ++i) {
            const f32 px =
                positions[i].x;

            const f32 py =
                positions[i].y;

            const f32 vx =
                velocities[i].x;

            const f32 vy =
                velocities[i].y;

            // --------------------------------------------------------
            // Accumulators
            // --------------------------------------------------------

            f32 separationX = 0.0f;
            f32 separationY = 0.0f;

            f32 alignmentX = 0.0f;
            f32 alignmentY = 0.0f;

            f32 cohesionX = 0.0f;
            f32 cohesionY = 0.0f;

            std::size_t sameTeamNeighbours = 0;

            // --------------------------------------------------------
            // Neighbour query
            // --------------------------------------------------------

            grid_.query(
                px,
                py,
                config_.perceptionRadius,
                config_.maxNeighbours,

                [&](std::size_t neighbour,
                    f32 dx,
                    f32 dy,
                    f32 distanceSquared)
                {
                    if (neighbour == i)
                        return;

                    // ------------------------------------------------
                    // Separation
                    //
                    // All boids participate in separation.
                    // This means different teams still avoid
                    // occupying the same space.
                    // ------------------------------------------------

                    if (distanceSquared <
                            separationRadiusSquared &&
                        distanceSquared >
                            EpsilonSquared)
                    {
                        const f32 distance =
                            std::sqrt(
                                distanceSquared
                            );

                        const f32 scale =
                            (separationRadius - distance) /
                            (separationRadius * distance);

                        separationX -=
                            dx * scale;

                        separationY -=
                            dy * scale;
                    }

                    // ------------------------------------------------
                    // Alignment + Cohesion
                    //
                    // Only same-team boids participate.
                    // ------------------------------------------------

                    if (teams[i] ==
                        teams[neighbour])
                    {
                        // Alignment:
                        // average neighbour velocity
                        alignmentX +=
                            velocities[neighbour].x;

                        alignmentY +=
                            velocities[neighbour].y;

                        // Cohesion:
                        // average displacement toward
                        // the centre of the group.
                        cohesionX += dx;
                        cohesionY += dy;

                        ++sameTeamNeighbours;
                    }
                }
            );

            // --------------------------------------------------------
            // Normalize neighbour contributions
            // --------------------------------------------------------

            const f32 inverseNeighbours =
                sameTeamNeighbours > 0
                    ? 1.0f /
                        static_cast<f32>(
                            sameTeamNeighbours)
                    : 0.0f;

            // ========================================================
            // SEPARATION
            // ========================================================

            Vec2 separation{
                separationX,
                separationY
            };

            separation =
                limit(
                    separation,
                    config_.maxForce
                );

            // ========================================================
            // ALIGNMENT
            // ========================================================

            Vec2 alignment{
                0.0f,
                0.0f
            };

            if (sameTeamNeighbours > 0)
            {
                // Average velocity of neighbours.
                const f32 averageVelocityX =
                    alignmentX *
                    inverseNeighbours;

                const f32 averageVelocityY =
                    alignmentY *
                    inverseNeighbours;

                // Reynolds:
                //
                // steering =
                //     desiredVelocity - currentVelocity
                //
                alignmentX =
                    averageVelocityX - vx;

                alignmentY =
                    averageVelocityY - vy;

                alignment = limit(
                    {
                        alignmentX,
                        alignmentY
                    },
                    config_.maxForce
                );
            }

            // ========================================================
            // COHESION
            // ========================================================

            Vec2 cohesion{
                cohesionX * inverseNeighbours,
                cohesionY * inverseNeighbours
            };

            const f32 distanceSquared =
                cohesion.x * cohesion.x +
                cohesion.y * cohesion.y;

            if (distanceSquared > cohesionDeadZoneSquared)
            {
                const f32 distance =
                    std::sqrt(distanceSquared);

                const f32 distanceOutsideDeadZone =
                    distance - cohesionDeadZone;

                const f32 normalizedDistance =
                    std::min(
                        distanceOutsideDeadZone /
                            cohesionRadius,
                        1.0f
                    );

                const f32 desiredSpeed =
                    config_.maxSpeed *
                    normalizedDistance;

                const f32 scale =
                    desiredSpeed / distance;

                Vec2 desired{
                    cohesion.x * scale,
                    cohesion.y * scale
                };

                cohesion = {
                    desired.x - vx,
                    desired.y - vy
                };

                cohesion = limit(
                    cohesion,
                    config_.maxForce
                );
            }
            else
            {
                cohesion = {0.0f, 0.0f};
            }

            // ========================================================
            // COMBINE STEERING FORCES
            // ========================================================

            f32 accelerationX =
                separation.x *
                    config_.separationWeight +

                alignment.x *
                    config_.alignmentWeight +

                cohesion.x *
                    config_.cohesionWeight;

            f32 accelerationY =
                separation.y *
                    config_.separationWeight +

                alignment.y *
                    config_.alignmentWeight +

                cohesion.y *
                    config_.cohesionWeight;

            // --------------------------------------------------------
            // Global steering-force limit
            // --------------------------------------------------------

            const f32 accelerationSquared =
                accelerationX *
                    accelerationX +
                accelerationY *
                    accelerationY;

            if (accelerationSquared >
                maximumForceSquared)
            {
                const f32 scale =
                    config_.maxForce /
                    std::sqrt(
                        accelerationSquared
                    );

                accelerationX *= scale;
                accelerationY *= scale;
            }

            // ========================================================
            // INTEGRATE VELOCITY
            // ========================================================

            f32 newVelocityX =
                vx +
                accelerationX * dt;

            f32 newVelocityY =
                vy +
                accelerationY * dt;

            // ========================================================
            // MINIMUM SPEED
            // ========================================================

            const f32 newVelocitySquared =
                newVelocityX *
                    newVelocityX +
                newVelocityY *
                    newVelocityY;

            if (newVelocitySquared <
                minimumSpeedSquared)
            {
                if (newVelocitySquared >
                    EpsilonSquared)
                {
                    const f32 scale =
                        minimumSpeed /
                        std::sqrt(
                            newVelocitySquared
                        );

                    newVelocityX *= scale;
                    newVelocityY *= scale;
                }
                else
                {
                    // ------------------------------------------------
                    // Velocity completely collapsed.
                    // Restore previous direction if possible.
                    // ------------------------------------------------

                    const f32 oldVelocitySquared =
                        vx * vx +
                        vy * vy;

                    if (oldVelocitySquared >
                        EpsilonSquared)
                    {
                        const f32 scale =
                            minimumSpeed /
                            std::sqrt(
                                oldVelocitySquared
                            );

                        newVelocityX =
                            vx * scale;

                        newVelocityY =
                            vy * scale;
                    }
                    else
                    {
                        // Last resort.
                        newVelocityX =
                            config_.initialSpeed;

                        newVelocityY = 0.0f;
                    }
                }
            }

            // ========================================================
            // MAXIMUM SPEED
            // ========================================================

            const f32 finalVelocitySquared =
                newVelocityX *
                    newVelocityX +
                newVelocityY *
                    newVelocityY;

            if (finalVelocitySquared >
                maximumSpeedSquared)
            {
                const f32 scale =
                    config_.maxSpeed /
                    std::sqrt(
                        finalVelocitySquared
                    );

                newVelocityX *= scale;
                newVelocityY *= scale;
            }

            velocities[i] = {
                newVelocityX,
                newVelocityY
            };
        }

        // ============================================================
        // INTEGRATE POSITION
        // ============================================================

        #pragma omp parallel for
        for (long long i = 0; i < static_cast<long long>(count); ++i) {
            f32 x =
                positions[i].x +
                velocities[i].x * dt;

            f32 y =
                positions[i].y +
                velocities[i].y * dt;

            // --------------------------------------------------------
            // Toroidal world
            // --------------------------------------------------------

            if (x < 0.0f)
            {
                x += config_.worldWidth;
            }
            else if (x >= config_.worldWidth)
            {
                x -= config_.worldWidth;
            }

            if (y < 0.0f)
            {
                y += config_.worldHeight;
            }
            else if (y >= config_.worldHeight)
            {
                y -= config_.worldHeight;
            }

            positions[i] = {
                x,
                y
            };
        }
    }

    FlockData data() const
    {
        FlockData result;
        result.mode = FlockDataMode::CPU;

        auto boids = world_.storage<Boid>();

        auto positions = boids.column<&Boid::position>();
        auto velocities = boids.column<&Boid::velocity>();
        auto teams = boids.column<&Boid::team>();

        result.boidCount = positions.size();
        result.worldWidth = config_.worldWidth;
        result.worldHeight = config_.worldHeight;

        result.positions = std::span<const Vec2>(
            positions.data(), positions.size());
        result.velocities = std::span<const Vec2>(
            velocities.data(), velocities.size());
        result.teams = std::span<const std::size_t>(
            teams.data(), teams.size());

        return result;
    }

    bool lastFrameStats(FlockFrameStats& out) const
    {
        (void)out;
        // CPU mode does not collect per-boid benchmark statistics on
        // the host. The simulation still exposes FlockFrameStats through
        // the GPU path; CPU consumers simply have no metrics available.
        return false;
    }
    
private:
    void spawn()
    {
        std::mt19937 rng(config_.randomSeed);
        std::uniform_real_distribution<f32> positionX(
            0.0f, config_.worldWidth);
        std::uniform_real_distribution<f32> positionY(
            0.0f, config_.worldHeight);
        std::uniform_real_distribution<f32> angle(0.0f, Pi2);

        for (std::size_t i = 0; i < config_.boidCount; ++i)
        {
            auto boid = world_.create<Boid>();

            //std::cout << "slot = " << boid.slot() << '\n';

            boid.set<&Boid::position>({positionX(rng), positionY(rng)});
            
             const f32 direction = angle(rng);
            //std::cout << "position OK\n";

            boid.set<&Boid::velocity>({
                std::cos(direction) * config_.initialSpeed,
                std::sin(direction) * config_.initialSpeed
            });

            //std::cout << "velocity OK\n";

            boid.set<&Boid::team>(i % config_.teamCount);

            //std::cout << "team OK\n";
        }
    }

    void resizeGridIfNeeded()
    {
        if (gridWidth_ == config_.worldWidth &&
            gridHeight_ == config_.worldHeight)
        {
            return;
        }

        grid_.resize(
            config_.worldWidth,
            config_.worldHeight,
            config_.perceptionRadius);
        gridWidth_ = config_.worldWidth;
        gridHeight_ = config_.worldHeight;
    }

    simulation::world::World& world_;
    FlockConfig& config_;
    UniformGrid grid_;
    f32 gridWidth_ = 0.0f;
    f32 gridHeight_ = 0.0f;
};

FlockCpuCompute::FlockCpuCompute(
    simulation::world::World& world,
    FlockConfig& config)
    : impl_(std::make_unique<Impl>(world, config))
{
}

FlockCpuCompute::~FlockCpuCompute() = default;

void FlockCpuCompute::tick(f32 dt)
{
    impl_->tick(dt);
}

FlockData FlockCpuCompute::data() const
{
    return impl_->data();
}

bool FlockCpuCompute::lastFrameStats(FlockFrameStats& out) const
{
    return impl_->lastFrameStats(out);
}

} // namespace emper::module
