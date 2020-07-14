#ifndef DUMMY_PLATFORM_H
#define DUMMY_PLATFORM_H

#include "platform.h"
#include "runtime.h"

#include <limits>

/// Dummy platform, implemented
class DummyPlatform : public Platform {
public:
    DummyPlatform(Runtime* runtime, const std::string& name)
        : Platform(runtime), name_(name)
    {}

protected:
    void* alloc(DeviceId, int64_t) override { platform_error(); }
    void* alloc_host(DeviceId, int64_t) override { platform_error(); }
    void* alloc_unified(DeviceId, int64_t) override { platform_error(); }
    void* get_device_ptr(DeviceId, void*) override { platform_error(); }
    void release(DeviceId, void*) override { platform_error(); }
    void release_host(DeviceId, void*) override { platform_error(); }

    void launch_kernel(DeviceId,
                       const char*, const char*,
                       const uint32_t*, const uint32_t*,
                       void**, const uint32_t*, const uint32_t*, const uint32_t*, const KernelArgType*,
                       uint32_t) override { platform_error(); }
    void synchronize(DeviceId) override { platform_error(); }

    void copy(DeviceId, const void*, int64_t, DeviceId, void*, int64_t, int64_t) override { platform_error(); }
    void copy_from_host(const void*, int64_t, DeviceId, void*, int64_t, int64_t) override { platform_error(); }
    void copy_to_host(DeviceId, const void*, int64_t, void*, int64_t, int64_t) override { platform_error(); }

    // Maximum number of devices to prevent assertions in debug mode
    size_t dev_count() const override { return std::numeric_limits<size_t>::max(); }
    std::string name() const override { return name_; }

    std::string name_;
};

#endif
