#ifndef ANYDSL_RUNTIME_RUNTIME_SHADY_H
#define ANYDSL_RUNTIME_RUNTIME_SHADY_H

#include "platform.h"

namespace shady {
extern "C" {
#include "shady/runtime.h"
}
}

class ShadyPlatform : public Platform {
public:
    ShadyPlatform(Runtime* runtime);
    ~ShadyPlatform() override;

    void* alloc(DeviceId dev, int64_t size) override;
    void* alloc_host(DeviceId dev, int64_t size) override;
    void* alloc_unified(DeviceId dev, int64_t size) override;
    void* get_device_ptr(DeviceId dev, void* ptr) override;
    void release(DeviceId dev, void* ptr) override;
    void release_host(DeviceId dev, void* ptr) override;

    void launch_kernel(DeviceId dev, const LaunchParams& launch_params) override;
    void synchronize(DeviceId dev) override;

    void copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_from_host(const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_to_host(DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) override;

    std::string name() const override { return "shady"; }
    size_t dev_count() const override { return 1; }
    const char * device_name(DeviceId dev) const override { return "TODO"; }
    bool device_check_feature_support(DeviceId dev, const char* feature) const override { return false; }

private:
    shady::Runtime* shd_rt;
};

#endif //ANYDSL_RUNTIME_RUNTIME_SHADY_H
