#ifndef PLATFORM_H
#define PLATFORM_H

#include "anydsl_runtime_config.h"
#include "log.h"

#include <cstdint>
#include <string>

class Runtime;
enum DeviceId   : uint32_t {};
enum PlatformId : uint32_t {};

enum class KernelArgType : uint8_t { Val = 0, Ptr, Struct };

/// A runtime platform. Exposes a set of devices, a copy function,
/// and functions to allocate and release memory.
class Platform {
public:
    Platform(Runtime* runtime)
        : runtime_(runtime)
    {}

    virtual ~Platform() {}

    /// Allocates memory for a device on this platform.
    virtual void* alloc(DeviceId dev, int64_t size) = 0;
    /// Allocates page-locked host memory for a platform (and a device).
    virtual void* alloc_host(DeviceId dev, int64_t size) = 0;
    /// Allocates unified memory for a platform (and a device).
    virtual void* alloc_unified(DeviceId dev, int64_t size) = 0;
    /// Returns the device memory associated with the page-locked memory.
    virtual void* get_device_ptr(DeviceId dev, void* ptr) = 0;
    /// Releases memory for a device on this platform.
    virtual void release(DeviceId dev, void* ptr) = 0;
    /// Releases page-locked host memory for a device on this platform.
    virtual void release_host(DeviceId dev, void* ptr) = 0;

    /// Launches a kernel with the given block/grid size and arguments.
    virtual void launch_kernel(DeviceId dev,
                               const char* file, const char* kernel,
                               const uint32_t* grid, const uint32_t* block,
                               void** args, const uint32_t* sizes, const uint32_t* aligns, const uint32_t* allocs, const KernelArgType* types,
                               uint32_t num_args) = 0;
    /// Waits for the completion of all the launched kernels on the given device.
    virtual void synchronize(DeviceId dev) = 0;

    /// Copies memory. Copy can only be performed devices in the same platform.
    virtual void copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) = 0;
    /// Copies memory from the host (CPU).
    virtual void copy_from_host(const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) = 0;
    /// Copies memory to the host (CPU).
    virtual void copy_to_host(DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) = 0;

    /// Returns the number of devices in this platform.
    virtual size_t dev_count() const = 0;
    /// Returns the platform name.
    virtual std::string name() const = 0;

protected:
    [[noreturn]] void platform_error() {
        error("The selected '%' platform is not available", name());
    }

    [[noreturn]] void command_unavailable(const std::string& command) {
        error("The command '%' is unavailable on platform '%'", command, name());
    }

    Runtime* runtime_;
};


template<class PLATFORM>
struct PlatformFactory {
    template<typename... Args>
    Platform* create(Runtime* runtime, const std::string& reference, Args... args);
};


#endif
