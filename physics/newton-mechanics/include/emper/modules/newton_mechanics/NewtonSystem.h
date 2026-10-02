#pragma once

#include "emper/interfaces/module/ISystem.h"
#include "emper/simulation/world/World.h"

#include "emper/modules/newton_mechanics/objects/Partical.h"

#include <vector>

namespace emper::modules::newton_mechanics
{

class NewtonSystem : public emper::interfaces::module::ISystem
{
public:
    explicit NewtonSystem(
        emper::simulation::world::World& world
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

    static constexpr f32 G = 1.0f;
};

}