#include "Flock.h"

#include "CpuCompute.h"
#include "FlockData.h"
#include "GpuCompute.h"

#include <emper/simulation/world/World.h>

namespace emper::module {

class Flock::Impl {
public:
  Impl(simulation::world::World &world, const FlockConfig &config,
       interfaces::backend::IGPUComputeBackend *gpuBackend)
      : world_(world), config_(config), gpuBackend_(gpuBackend) {
    selectCompute();
    //gpuCompute_->setBenchmarkEnabled(false);
  }

  void tick(f32 dt) {
    if (computeMode_ == interfaces::module::ComputeMode::GPU)
      gpuCompute_->tick(dt);
    else
      cpuCompute_->tick(dt);
  }

  void selectCompute() {
    if (config_.mode != interfaces::module::ComputeMode::CPU) {
      gpuCompute_ =
          std::make_unique<FlockGpuCompute>(world_, config_, gpuBackend_);
      if (gpuCompute_->initialize()) {
        computeMode_ = interfaces::module::ComputeMode::GPU;
        return;
      }
    }
    cpuCompute_ = std::make_unique<FlockCpuCompute>(world_, config_);
    computeMode_ = interfaces::module::ComputeMode::CPU;
  }

  FlockData data() const {
    if (computeMode_ == interfaces::module::ComputeMode::GPU)
      return gpuCompute_->data();
    return cpuCompute_->data();
  }

  bool lastFrameStats(FlockFrameStats &out) const {
    if (computeMode_ == interfaces::module::ComputeMode::GPU)
      return gpuCompute_->lastFrameStats(out);
    return cpuCompute_->lastFrameStats(out);
  }

  void synchronizeWorldSize(f32 width, f32 height) {
    if (width <= 0.0f || height <= 0.0f)
      return;
    config_.worldWidth = width;
    config_.worldHeight = height;
  }

  simulation::world::World &world_;
  FlockConfig config_;
  interfaces::backend::IGPUComputeBackend *gpuBackend_ = nullptr;
  interfaces::module::ComputeMode computeMode_ =
      interfaces::module::ComputeMode::CPU;
  std::unique_ptr<FlockCpuCompute> cpuCompute_;
  std::unique_ptr<FlockGpuCompute> gpuCompute_;
};

Flock::Flock(simulation::world::World &world, const FlockConfig &config,
             interfaces::backend::IGPUComputeBackend *gpuBackend)
    : impl_(std::make_unique<Impl>(world, config, gpuBackend)) {
  setConfig(config);
}

Flock::~Flock() = default;

void Flock::tick(f32 dt) { impl_->tick(dt); }

FlockData Flock::data() const { return impl_->data(); }

bool Flock::lastFrameStats(FlockFrameStats &out) const {
  return impl_->lastFrameStats(out);
}

void Flock::synchronizeWorldSize(f32 width, f32 height) {
  impl_->synchronizeWorldSize(width, height);
}

const FlockConfig &Flock::config() const { return impl_->config_; }

interfaces::module::ComputeMode Flock::computeMode() const {
  return impl_->computeMode_;
}

} // namespace emper::module