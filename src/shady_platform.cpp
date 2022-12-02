#include "shady_platform.h"

ShadyPlatform::ShadyPlatform(Runtime *r) : Platform(r) {
    shady::RuntimeConfig cfg;
    cfg.dump_spv = true;
    cfg.use_validation = true;
    shd_rt = shady::initialize_runtime(cfg);
}

ShadyPlatform::~ShadyPlatform() {
    shady::shutdown_runtime(shd_rt);
}

void* ShadyPlatform::alloc(DeviceId dev, int64_t size) {
    auto device = shady::get_device(shd_rt, dev);
    shady::Buffer* buf = shady::allocate_buffer_device(device, size);
}

void* ShadyPlatform::alloc_host(DeviceId dev, int64_t size) {}
void* ShadyPlatform::alloc_unified(DeviceId dev, int64_t size) {}
void* ShadyPlatform::get_device_ptr(DeviceId dev, void *ptr) {}
void ShadyPlatform::release(DeviceId dev, void *ptr) {}
void ShadyPlatform::release_host(DeviceId dev, void *ptr) {}

void ShadyPlatform::launch_kernel(DeviceId dev, const LaunchParams &launch_params) {
    std::string program_src = runtime_->load_file(launch_params.file_name);
    shady::Program* program = shady::load_program(shd_rt, program_src.c_str());
    auto device = shady::get_device(shd_rt, dev);
    shady::Dispatch* d = shady::launch_kernel(program, device, launch_params.grid[0] / launch_params.block[0], launch_params.grid[1] / launch_params.block[1], launch_params.grid[2] / launch_params.block[2], 0, nullptr);
    assert(d);
    shady::wait_completion(d);
}
void ShadyPlatform::synchronize(DeviceId dev) {}

void ShadyPlatform::copy(DeviceId dev_src, const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst, int64_t size) {}
void ShadyPlatform::copy_from_host(const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst, int64_t size) {}
void ShadyPlatform::copy_to_host(DeviceId dev_src, const void *src, int64_t offset_src, void *dst, int64_t offset_dst, int64_t size) {}

void register_shady_platform(Runtime* runtime) {
    runtime->register_platform<ShadyPlatform>();
}