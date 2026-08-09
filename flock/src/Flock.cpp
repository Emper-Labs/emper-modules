#include "Flock.h"

#include "CpuCompute.h"
#include "GpuCompute.h"

#include <emper/interfaces/backend/IRenderer.h>
#include <emper/simulation/world/World.h>

#include <memory>

namespace emper::module {

class Flock::Impl
{
public:
    Impl(simulation::world::World& world, const FlockConfig& config)
        : world_(world), config_(config)
    {
        selectCompute();
    }

    void tick(f32 dt)
    {
        if (computeMode_ == interfaces::module::ComputeMode::GPU)
            gpuCompute_->tick(dt);
        else
            cpuCompute_->tick(dt);
    }

    void render(interfaces::backend::IRenderer& renderer)
    {
        if (computeMode_ == interfaces::module::ComputeMode::GPU)
            gpuCompute_->render(renderer);
        else
            cpuCompute_->render(renderer);
    }

    void selectCompute()
    {
        if (config_.mode != interfaces::module::ComputeMode::CPU)
        {
            gpuCompute_ = std::make_unique<FlockGpuCompute>(world_, config_);
            if (gpuCompute_->initialize())
            {
                computeMode_ = interfaces::module::ComputeMode::GPU;
                return;
            }
        }

        // GPU is currently a skeleton, so Auto and GPU safely fall back to CPU.
        cpuCompute_ = std::make_unique<FlockCpuCompute>(world_, config_);
        computeMode_ = interfaces::module::ComputeMode::CPU;
    }

    simulation::world::World& world_;
    FlockConfig config_;
    interfaces::module::ComputeMode computeMode_ =
        interfaces::module::ComputeMode::CPU;
    std::unique_ptr<FlockCpuCompute> cpuCompute_;
    std::unique_ptr<FlockGpuCompute> gpuCompute_;
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