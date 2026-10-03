#pragma once

#include "emper/interfaces/module/ISystem.h"
#include "emper/simulation/world/World.h"

#include <emper/modules/classical-mechanics/objects/Particle.h>

#include <vector>

namespace emper::modules::classical_mechanics
{

class ClassicalMechanicsSystem : public emper::interfaces::module::ISystem
{
public:
    explicit ClassicalMechanicsSystem(
        emper::simulation::world::World& world,
        f32 gravitationalConstant = 1.0f
    );

    void initialize() override;
    void tick(f32 dt) override;
    void shutdown() override;

    void addObject(const objects::Particle& particle);
    const std::vector<objects::Particle>& particles() const;


private:
    void calculateGravity();
    void integrate(f32 dt);

private:
    emper::simulation::world::World& m_World;

    std::vector<objects::Particle> m_Particles;

    f32 m_G = 1.0f;
};

}