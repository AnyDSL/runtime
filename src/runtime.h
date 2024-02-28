#ifndef RUNTIME_H
#define RUNTIME_H

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <memory>

#include "log.h"

enum DeviceId   : uint32_t {};
enum PlatformId : uint32_t {};
enum class ProfileLevel : uint8_t { None = 0, Full, Fpga_dynamic };

class Platform;

enum class KernelArgType : uint8_t { Val = 0, Ptr, Struct };

struct ParamsArgs {
    void** data;
    const uint32_t* sizes;
    const uint32_t* aligns;
    const uint32_t* alloc_sizes;
    const KernelArgType* types;
};

/// The parameters to a `anydsl_launch_kernel()` call.
struct LaunchParams {
    const char* file_name;
    const char* kernel_name;
    const uint32_t* grid;
    const uint32_t* block;
    ParamsArgs args;
    uint32_t num_args;
};

class Runtime {
public:
    Runtime(std::pair<ProfileLevel, ProfileLevel>);

    /// Registers the given platform into the runtime.
    template <typename T, typename... Args>
    void register_platform(Args&&... args) {
        platforms_.emplace_back(new T(this, std::forward<Args&&>(args)...));
    }

    /// Displays available platforms.
    void display_info() const;

    /// Returns name of device.
    const char* device_name(PlatformId, DeviceId) const;
    /// Checks whether feature is supported on device.
    bool device_check_feature_support(PlatformId, DeviceId, const char*) const;

    /// Allocates memory on the given device.
    void* alloc(PlatformId plat, DeviceId dev, int64_t size);
    /// Allocates page-locked memory on the given platform and device.
    void* alloc_host(PlatformId plat, DeviceId dev, int64_t size);
    /// Allocates unified memory on the given platform and device.
    void* alloc_unified(PlatformId plat, DeviceId dev, int64_t size);
    /// Returns the device memory associated with the page-locked memory.
    void* get_device_ptr(PlatformId plat, DeviceId dev, void* ptr);
    /// Releases memory.
    void release(PlatformId plat, DeviceId dev, void* ptr);
    /// Releases previously allocated page-locked memory.
    void release_host(PlatformId plat, DeviceId dev, void* ptr);
    /// Copies memory between devices.
    void copy(
        PlatformId plat_src, DeviceId dev_src, const void* src, int64_t offset_src,
        PlatformId plat_dst, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size);

    /// Launches a kernel on the platform and device.
    void launch_kernel(PlatformId plat, DeviceId dev, const LaunchParams& launch_params);
    /// Waits for the completion of all kernels on the given platform and device.
    void synchronize(PlatformId plat, DeviceId dev);

    /// Associate a program string to a given filename.
    void register_file(const std::string& filename, const std::string& program_string) {
        files_[filename] = program_string;
    }

    std::string load_file(const std::string& filename) const;
    void store_file(const std::string& filename, const std::string& str) const;
    void store_file(const std::string& filename, const std::byte* data, size_t size) const;

    /// Set an optional directory for generated cache data. If not specified, or empty, an internal directory will be used. User has to make sure the directory exists.
    void set_cache_directory(const std::string& dir);
    std::string get_cache_directory() const;

    std::string load_from_cache(const std::string& str, const std::string& ext=".bin") const;
    void store_to_cache(const std::string& key, const std::string& str, const std::string ext=".bin") const;

    bool profiling_enabled() { return profile_.first == ProfileLevel::Full; }
    bool dynamic_profiling_enabled() { return profile_.second == ProfileLevel::Fpga_dynamic; }
    std::atomic<uint64_t>& kernel_time() { return kernel_time_; }

    static void* aligned_malloc(size_t, size_t);
    static void aligned_free(void*);

private:
    void check_device(PlatformId, DeviceId) const;
    std::string get_cached_filename(const std::string& str, const std::string& ext) const;

    std::pair<ProfileLevel, ProfileLevel> profile_;
    std::atomic<uint64_t> kernel_time_;
    std::vector<std::unique_ptr<Platform>> platforms_;
    std::unordered_map<std::string, std::string> files_;
    std::string cache_dir_;
};

#endif
