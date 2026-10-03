#include "emper/modules/classical-mechanics/ClassicalMechanicsSystem.h"


#include <cmath>

namespace emper::modules::classical_mechanics
{

ClassicalMechanicsSystem::ClassicalMechanicsSystem(
    emper::simulation::world::World& world,
    f32 gravitationalConstant
)
    : m_World(world), m_G(gravitationalConstant)
{
}

void ClassicalMechanicsSystem::initialize()
{
}

void ClassicalMechanicsSystem::tick(f32 dt)
{
    calculateGravity();
    integrate(dt);
}

void ClassicalMechanicsSystem::shutdown()
{
}

void ClassicalMechanicsSystem::addObject(const objects::Particle& particle)
{
    m_Particles.push_back(particle);
}

const std::vector<objects::Particle>& ClassicalMechanicsSystem::particles() const
{
    return m_Particles;
}

void ClassicalMechanicsSystem::calculateGravity()
{
    for (auto& particle : m_Particles)
    {
        particle.acceleration = Vec3{0.0f, 0.0f, 0.0f};
    }

    for (std::size_t i = 0; i < m_Particles.size(); ++i)
    {
        for (std::size_t j = i + 1; j < m_Particles.size(); ++j)
        {
            auto& a = m_Particles[i];
            auto& b = m_Particles[j];

            Vec3 direction = b.position - a.position;

            // lim r-> 0, F -> inf :V
            constexpr f32 softening = 0.01f;

            f32 distanceSquared =
                dot(direction, direction);

            distanceSquared += softening * softening;

            if (distanceSquared == 0.0f)
                continue;

            f32 distance = std::sqrt(distanceSquared);

            Vec3 directionNormalized = direction / distance;

            f32 forceMagnitude =
                m_G * a.mass * b.mass / distanceSquared;

            Vec3 force =
                directionNormalized * forceMagnitude;

            a.acceleration += force / a.mass;
            b.acceleration -= force / b.mass;
        }
    }
}

void ClassicalMechanicsSystem::integrate(f32 dt)
{
    for (auto& particle : m_Particles)
    {
        particle.velocity += particle.acceleration * dt;
        particle.position += particle.velocity * dt;
    }
}

}