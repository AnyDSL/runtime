#include "shady_platform.h"

ShadyPlatform::ShadyPlatform(Runtime *runtime) : Platform(runtime) {
    info("hi");
}

ShadyPlatform::~ShadyPlatform() {

}

void * ShadyPlatform::alloc(DeviceId dev, int64_t size) {}
void * ShadyPlatform::alloc_host(DeviceId dev, int64_t size) {}
void * ShadyPlatform::alloc_unified(DeviceId dev, int64_t size) {}
void * ShadyPlatform::get_device_ptr(DeviceId dev, void *ptr) {}
void ShadyPlatform::release(DeviceId dev, void *ptr) {}
void ShadyPlatform::release_host(DeviceId dev, void *ptr) {}

void ShadyPlatform::launch_kernel(DeviceId dev, const LaunchParams &launch_params) {}
void ShadyPlatform::synchronize(DeviceId dev) {}

void ShadyPlatform::copy(DeviceId dev_src, const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst, int64_t size) {}
void ShadyPlatform::copy_from_host(const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst, int64_t size) {}
void ShadyPlatform::copy_to_host(DeviceId dev_src, const void *src, int64_t offset_src, void *dst, int64_t offset_dst, int64_t size) {}

void register_shady_platform(Runtime* runtime) {
    runtime->register_platform<ShadyPlatform>();
}