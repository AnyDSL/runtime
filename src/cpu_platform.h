#ifndef CPU_PLATFORM_H
#define CPU_PLATFORM_H

#include "platform.h"

#ifndef PAGE_SIZE
#define PAGE_SIZE 4096
#endif

#include <cstring>

/// CPU platform, allocation is guaranteed to be aligned to page size: 4096 bytes.
class CpuPlatform : public Platform {
public:
    CpuPlatform(Runtime* runtime);

protected:
    void* alloc(DeviceId, int64_t size) override {
        return Runtime::aligned_malloc(size, 32);
    }

    void* alloc_host(DeviceId, int64_t size) override {
        return Runtime::aligned_malloc(size, PAGE_SIZE);
    }

    void* alloc_unified(DeviceId, int64_t size) override {
        return Runtime::aligned_malloc(size, PAGE_SIZE);
    }

    void* get_device_ptr(DeviceId, void* ptr) override {
        return ptr;
    }

    void release(DeviceId, void* ptr) override {
        Runtime::aligned_free(ptr);
    }

    void release_host(DeviceId dev, void* ptr) override {
        release(dev, ptr);
    }

    void no_kernel() {
        error("Kernels are not supported on the CPU");
    }

    void launch_kernel(DeviceId, const LaunchParams&) override { no_kernel(); }
    void synchronize(DeviceId) override { no_kernel(); }

    void copy(const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) {
        memcpy((char*)dst + offset_dst, (char*)src + offset_src, size);
    }

    void copy(DeviceId, const void* src, int64_t offset_src,
              DeviceId, void* dst, int64_t offset_dst, int64_t size) override {
        copy(src, offset_src, dst, offset_dst, size);
    }
    void copy_from_host(const void* src, int64_t offset_src, DeviceId,
                        void* dst, int64_t offset_dst, int64_t size) override {
        copy(src, offset_src, dst, offset_dst, size);
    }
    void copy_to_host(DeviceId, const void* src, int64_t offset_src,
                      void* dst, int64_t offset_dst, int64_t size) override {
        copy(src, offset_src, dst, offset_dst, size);
    }

    std::string device_name_;
    size_t dev_count() const override { return 1; }
    std::string name() const override { return "CPU"; }
    const char* device_name(DeviceId) const override { return device_name_.c_str(); }
    bool device_check_feature_support(DeviceId, const char*) const override { return false; }
};

#endif
