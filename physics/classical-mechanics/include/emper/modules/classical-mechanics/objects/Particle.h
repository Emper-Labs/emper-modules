#include "emper/Types.h"

namespace emper::modules::classical_mechanics::objects
{
class Particle
{
public:

    Vec3 position;
    Vec3 velocity;
    Vec3 acceleration;
    f32 mass;

};
}