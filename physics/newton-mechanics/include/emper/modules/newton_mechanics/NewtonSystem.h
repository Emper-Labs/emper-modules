#include "emper/interfaces/module/ISystem.h"
#include "emper/simulation/world/World.h"

namespace emper::modules::newton_mechanics
{

class NewtonSystem : public emper::interfaces::module::ISystem
{
public:
    NewtonSystem(emper::simulation::world::World& world)
        : m_World(world)
    {
    }


    void initialize() override;
    void tick(f32 dt) override;
    void shutdown() override;
    
    template<typename TObject>
    void addObject(const TObject& object);

private:
    emper::simulation::world::World& m_World;
};

}// namespace emper::module::newton_mechanics