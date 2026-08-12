#include "Flock.h"

#include "CpuCompute.h"
#include "GpuCompute.h"

#include <emper/interfaces/backend/IRenderer.h>
#include <emper/simulation/world/World.h>

#include <chrono>
#include <iostream>
#include <memory>

namespace emper::module {

class Flock::Impl {
public:
  Impl(simulation::world::World &world, const FlockConfig &config,
       interfaces::backend::IGPUComputeBackend *gpuBackend)
      : world_(world), config_(config), gpuBackend_(gpuBackend) {
    selectCompute();
    gpuCompute_->setBenchmarkEnabled(true);
  }

  void tick(f32 dt) {
    if (computeMode_ == interfaces::module::ComputeMode::GPU)
      gpuCompute_->tick(dt);
    else
      cpuCompute_->tick(dt);
  }

  void render(interfaces::backend::IRenderer &renderer) {
    if (computeMode_ == interfaces::module::ComputeMode::GPU)
      gpuCompute_->render(renderer);
    else
      cpuCompute_->render(renderer);

    static auto lastTime = std::chrono::steady_clock::now();
    static int frames = 0;

    ++frames;

    const auto now = std::chrono::steady_clock::now();
    const std::chrono::duration<float> elapsed = now - lastTime;
    
    static float fps;
    if (elapsed.count() >= 1.0f)
    {
        fps =
            static_cast<float>(frames) /
            elapsed.count();

        frames = 0;
        lastTime = now;
    }
    const char* mode =
        computeMode_ == interfaces::module::ComputeMode::GPU
            ? "GPU"
            : "CPU";

    renderer.drawText(
        std::format("FPS: {}", fps),
        20.0f,
        20.0f,
        10.0f);

    renderer.drawText(
        std::format("Mode: {}", mode),
        20.0f,
        32.0f,
        10.0f);

    renderer.drawText(
        std::format("Boids: {}", config_.boidCount),
        20.0f,
        44.0f,
        10.0f);
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

    // GPU is currently a skeleton, so Auto and GPU safely fall back to CPU.
    cpuCompute_ = std::make_unique<FlockCpuCompute>(world_, config_);
    computeMode_ = interfaces::module::ComputeMode::CPU;
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

void Flock::render(interfaces::backend::IRenderer &renderer) {
  impl_->render(renderer);
}

const FlockConfig &Flock::config() const { return impl_->config_; }

interfaces::module::ComputeMode Flock::computeMode() const {
  return impl_->computeMode_;
}

} // namespace emper::module
