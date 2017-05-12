#ifndef CPU_PLATFORM_H
#define CPU_PLATFORM_H

#include "platform.h"
#include "runtime.h"

#include <cstring>

/// CPU platform, allocation is guaranteed to be aligned to page size: 4096 bytes.
class CpuPlatform : public Platform {
public:
    CpuPlatform(Runtime* runtime)
        : Platform(runtime)
    {}

protected:
    void* alloc(DeviceId, int64_t size) override {
        return anydsl_aligned_malloc(size, 4096);
    }

    void* alloc_host(DeviceId dev, int64_t size) override {
        return alloc(dev, size);
    }

    void* alloc_unified(DeviceId dev, int64_t size) override {
        return alloc(dev, size);
    }

    void* get_device_ptr(DeviceId, void* ptr) override {
        return ptr;
    }

    void release(DeviceId, void* ptr) override {
        anydsl_aligned_free(ptr);
    }

    void release_host(DeviceId dev, void* ptr) override {
        release(dev, ptr);
    }

    void no_kernel() { error("Kernels are not supported on the CPU"); }

    void launch_kernel(DeviceId,
                       const char*, const char*,
                       const uint32_t*, const uint32_t*,
                       void**, const uint32_t*, const KernelArgType*,
                       uint32_t) override { no_kernel(); }
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

    int dev_count() override { return 1; }

    std::string name() override { return "CPU"; }
};

#endif
