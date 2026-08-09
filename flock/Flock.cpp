#include "Flock.h"

#include <UniformGrid.h>
#include <emper/interfaces/backend/IRenderer.h>
#include <emper/simulation/world/World.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <random>
#include <stdexcept>

namespace emper::module {
namespace {

constexpr f32 Pi2 = 6.28318530718f;
constexpr f32 EpsilonSquared = 0.000001f;
constexpr std::array<u32, 3> TeamColors{
    0xFF4D4DFF, 0x4DFF88FF, 0x4D8DFFFF
};

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
    if (config.maxSpeed <= 0.0f || config.maxForce <= 0.0f || config.initialSpeed <= 0.0f)
        throw std::invalid_argument("Flock speed and force values must be positive");
    if (config.perceptionRadius <= 0.0f || config.separationRadius <= 0.0f)
        throw std::invalid_argument("Flock radii must be positive");
    if (config.teamCount == 0)
        throw std::invalid_argument("Flock team count must be greater than zero");
}

} // namespace

class Flock::Impl
{
public:
    Impl(simulation::world::World& world, const FlockConfig& config)
        : world_(world), config_(config), computeMode_(interfaces::module::ComputeMode::CPU)
    {
        validate(config_);

        // No GPU compute backend exists yet, so Auto and GPU use the CPU path.
        world_.registerType<Boid>()
            .field<&Boid::position>()
            .field<&Boid::velocity>()
            .field<&Boid::team>();

        world_.reserve<Boid>(config_.boidCount);
        spawn();
    }

    void spawn()
    {
        std::mt19937 rng(config_.randomSeed);
        std::uniform_real_distribution<f32> positionX(0.0f, config_.worldWidth);
        std::uniform_real_distribution<f32> positionY(0.0f, config_.worldHeight);
        std::uniform_real_distribution<f32> angle(0.0f, Pi2);

        for (std::size_t i = 0; i < config_.boidCount; ++i)
        {
            auto boid = world_.create<Boid>();
            const f32 direction = angle(rng);
            boid.set<&Boid::position>({ positionX(rng), positionY(rng) });
            boid.set<&Boid::velocity>({
                std::cos(direction) * config_.initialSpeed,
                std::sin(direction) * config_.initialSpeed
            });
            boid.set<&Boid::team>(i % config_.teamCount);
        }
    }

    void tick(f32 dt)
    {
        if (dt <= 0.0f)
            return;

        auto boids = world_.storage<Boid>();
        auto positions = boids.column<&Boid::position>();
        auto velocities = boids.column<&Boid::velocity>();
        auto teams = boids.column<&Boid::team>();
        const std::size_t count = boids.size();
        if (count == 0)
            return;

        if (gridWidth_ != config_.worldWidth || gridHeight_ != config_.worldHeight)
        {
            grid_.resize(config_.worldWidth, config_.worldHeight, config_.perceptionRadius);
            gridWidth_ = config_.worldWidth;
            gridHeight_ = config_.worldHeight;
        }
        grid_.rebuild(positions.data(), count);

        const f32 separationRadiusSquared = config_.separationRadius * config_.separationRadius;
        const f32 minimumSpeed = config_.initialSpeed * 0.75f;
        const f32 minimumSpeedSquared = minimumSpeed * minimumSpeed;
        const f32 maximumSpeedSquared = config_.maxSpeed * config_.maxSpeed;
        const f32 maximumForceSquared = config_.maxForce * config_.maxForce;

        #pragma omp parallel for
        for (std::size_t i = 0; i < count; ++i)
        {
            const f32 px = positions[i].x;
            const f32 py = positions[i].y;
            const f32 vx = velocities[i].x;
            const f32 vy = velocities[i].y;
            f32 separationX = 0.0f;
            f32 separationY = 0.0f;
            f32 alignmentX = 0.0f;
            f32 alignmentY = 0.0f;
            f32 cohesionX = 0.0f;
            f32 cohesionY = 0.0f;
            std::size_t sameTeamNeighbours = 0;

            grid_.query(
                px, py, config_.perceptionRadius, config_.maxNeighbours,
                [&](std::size_t neighbour, f32 dx, f32 dy, f32 distanceSquared)
                {
                    if (neighbour == i)
                        return;

                    if (distanceSquared < separationRadiusSquared && distanceSquared > EpsilonSquared)
                    {
                        const f32 distance = std::sqrt(distanceSquared);
                        const f32 scale =
                            (config_.separationRadius - distance) /
                            (config_.separationRadius * distance);
                        separationX -= dx * scale;
                        separationY -= dy * scale;
                    }

                    if (teams[i] == teams[neighbour])
                    {
                        alignmentX += velocities[neighbour].x;
                        alignmentY += velocities[neighbour].y;
                        cohesionX += dx;
                        cohesionY += dy;
                        ++sameTeamNeighbours;
                    }
                });

            const f32 inverseNeighbours = sameTeamNeighbours > 0
                ? 1.0f / static_cast<f32>(sameTeamNeighbours)
                : 0.0f;

            Vec2 separation = limit({ separationX, separationY }, config_.maxForce);
            if (sameTeamNeighbours > 0)
            {
                alignmentX = alignmentX * inverseNeighbours - vx;
                alignmentY = alignmentY * inverseNeighbours - vy;
            }
            Vec2 alignment = limit({ alignmentX, alignmentY }, config_.maxForce);

            Vec2 cohesion{
                cohesionX * inverseNeighbours,
                cohesionY * inverseNeighbours
            };
            const f32 cohesionSquared = cohesion.x * cohesion.x + cohesion.y * cohesion.y;
            if (cohesionSquared > EpsilonSquared)
            {
                const f32 distance = std::sqrt(cohesionSquared);
                const f32 desiredSpeed = config_.maxSpeed * std::min(
                    distance / config_.perceptionRadius, 1.0f);
                const f32 scale = desiredSpeed / distance;
                cohesion.x = cohesion.x * scale - vx;
                cohesion.y = cohesion.y * scale - vy;
                cohesion = limit(cohesion, config_.maxForce);
            }

            f32 accelerationX =
                separation.x * config_.separationWeight +
                alignment.x * config_.alignmentWeight +
                cohesion.x * config_.cohesionWeight;
            f32 accelerationY =
                separation.y * config_.separationWeight +
                alignment.y * config_.alignmentWeight +
                cohesion.y * config_.cohesionWeight;
            const f32 accelerationSquared = accelerationX * accelerationX + accelerationY * accelerationY;
            if (accelerationSquared > maximumForceSquared)
            {
                const f32 scale = config_.maxForce / std::sqrt(accelerationSquared);
                accelerationX *= scale;
                accelerationY *= scale;
            }

            f32 newVelocityX = vx + accelerationX * dt;
            f32 newVelocityY = vy + accelerationY * dt;
            const f32 newVelocitySquared = newVelocityX * newVelocityX + newVelocityY * newVelocityY;
            if (newVelocitySquared < minimumSpeedSquared)
            {
                if (newVelocitySquared > EpsilonSquared)
                {
                    const f32 scale = minimumSpeed / std::sqrt(newVelocitySquared);
                    newVelocityX *= scale;
                    newVelocityY *= scale;
                }
                else
                {
                    const f32 oldVelocitySquared = vx * vx + vy * vy;
                    if (oldVelocitySquared > EpsilonSquared)
                    {
                        const f32 scale = minimumSpeed / std::sqrt(oldVelocitySquared);
                        newVelocityX = vx * scale;
                        newVelocityY = vy * scale;
                    }
                    else
                    {
                        newVelocityX = config_.initialSpeed;
                        newVelocityY = 0.0f;
                    }
                }
            }

            const f32 finalVelocitySquared = newVelocityX * newVelocityX + newVelocityY * newVelocityY;
            if (finalVelocitySquared > maximumSpeedSquared)
            {
                const f32 scale = config_.maxSpeed / std::sqrt(finalVelocitySquared);
                newVelocityX *= scale;
                newVelocityY *= scale;
            }
            velocities[i] = { newVelocityX, newVelocityY };
        }

        #pragma omp parallel for
        for (std::size_t i = 0; i < count; ++i)
        {
            f32 x = positions[i].x + velocities[i].x * dt;
            f32 y = positions[i].y + velocities[i].y * dt;
            if (x < 0.0f) x += config_.worldWidth;
            else if (x >= config_.worldWidth) x -= config_.worldWidth;
            if (y < 0.0f) y += config_.worldHeight;
            else if (y >= config_.worldHeight) y -= config_.worldHeight;
            positions[i] = { x, y };
        }
    }

    void render(interfaces::backend::IRenderer& renderer)
    {
        config_.worldWidth = renderer.windowWidth();
        config_.worldHeight = renderer.windowHeight();

        auto boids = world_.storage<Boid>();
        auto positions = boids.column<&Boid::position>();
        auto teams = boids.column<&Boid::team>();
        for (std::size_t i = 0; i < boids.size(); ++i)
        {
            renderer.drawPoint(
                positions[i].x,
                positions[i].y,
                TeamColors[teams[i] % TeamColors.size()]);
        } 
    }

    simulation::world::World& world_;
    FlockConfig config_;
    interfaces::module::ComputeMode computeMode_;
    UniformGrid grid_;
    f32 gridWidth_ = 0.0f;
    f32 gridHeight_ = 0.0f;
};

Flock::Flock(simulation::world::World& world, const FlockConfig& config)
    : impl_(std::make_unique<Impl>(world, config))
{
    setConfig(config);
}

Flock::~Flock() = default;

void Flock::tick(f32 dt)
{
    impl_->tick(dt);
}

void Flock::render(interfaces::backend::IRenderer& renderer)
{
    impl_->render(renderer);
}

const FlockConfig& Flock::config() const
{
    return impl_->config_;
}

interfaces::module::ComputeMode Flock::computeMode() const
{
    return impl_->computeMode_;
}

} // namespace emper::module