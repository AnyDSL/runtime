#ifndef RUNTIME_H
#define RUNTIME_H

#include "platform.h"

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>
#include <memory>

enum class ProfileLevel : uint8_t { None = 0, Full };

class Runtime {
public:
    Runtime();

    /// Registers the given platform into the runtime.
    template <typename T, typename... Args>
    void register_platform(Args&&... args) {
        platforms_.emplace_back(new T(this, std::forward<Args&&>(args)...));
    }

    /// Displays available platforms.
    void display_info() {
        info("Available platforms:");
        for (auto& p: platforms_) {
            info("    * %: % device(s)", p->name(), p->dev_count());
        }
    }

    /// Allocates memory on the given device.
    void* alloc(PlatformId plat, DeviceId dev, int64_t size) {
        check_device(plat, dev);
        return platforms_[plat]->alloc(dev, size);
    }

    /// Allocates page-locked memory on the given platform and device.
    void* alloc_host(PlatformId plat, DeviceId dev, int64_t size) {
        check_device(plat, dev);
        return platforms_[plat]->alloc_host(dev, size);
    }

    /// Allocates unified memory on the given platform and device.
    void* alloc_unified(PlatformId plat, DeviceId dev, int64_t size) {
        check_device(plat, dev);
        return platforms_[plat]->alloc_unified(dev, size);
    }

    /// Returns the device memory associated with the page-locked memory.
    void* get_device_ptr(PlatformId plat, DeviceId dev, void* ptr) {
        check_device(plat, dev);
        return platforms_[plat]->get_device_ptr(dev, ptr);
    }

    /// Releases memory.
    void release(PlatformId plat, DeviceId dev, void* ptr) {
        check_device(plat, dev);
        platforms_[plat]->release(dev, ptr);
    }

    /// Releases previously allocated page-locked memory.
    void release_host(PlatformId plat, DeviceId dev, void* ptr) {
        check_device(plat, dev);
        platforms_[plat]->release_host(dev, ptr);
    }

    /// Associate a program string to a given filename.
    void register_file(const std::string& filename, const std::string& program_string) {
        files_[filename] = program_string;
    }

    std::string load_file(const std::string& filename) const;
    void store_file(const std::string& filename, const std::string& str) const;

    AnyDSL_runtime_API std::string load_cache(const std::string& str, const std::string& ext=".bin") const;
    AnyDSL_runtime_API void store_cache(const std::string& key, const std::string& str, const std::string ext=".bin") const;

    /// Launches a kernel on the platform and device.
    void launch_kernel(PlatformId plat, DeviceId dev, const LaunchParams& launch_params) {
        check_device(plat, dev);
        assert(launch_params.grid[0] > 0 && launch_params.grid[0] % launch_params.block[0] == 0 &&
               launch_params.grid[1] > 0 && launch_params.grid[1] % launch_params.block[1] == 0 &&
               launch_params.grid[2] > 0 && launch_params.grid[2] % launch_params.block[2] == 0 &&
               "The grid size is not a multiple of the block size");
        platforms_[plat]->launch_kernel(dev, launch_params);
    }

    /// Waits for the completion of all kernels on the given platform and device.
    void synchronize(PlatformId plat, DeviceId dev) {
        check_device(plat, dev);
        platforms_[plat]->synchronize(dev);
    }

    /// Copies memory.
    void copy(PlatformId plat_src, DeviceId dev_src, const void* src, int64_t offset_src,
              PlatformId plat_dst, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
        check_device(plat_src, dev_src);
        check_device(plat_dst, dev_dst);
        if (plat_src == plat_dst) {
            // Copy from same platform
            platforms_[plat_src]->copy(dev_src, src, offset_src, dev_dst, dst, offset_dst, size);
            debug("Copy between devices % and % on platform %", dev_src, dev_dst, plat_src);
        } else {
            // Copy from another platform
            if (plat_src == 0) {
                // Source is the CPU platform
                platforms_[plat_dst]->copy_from_host(src, offset_src, dev_dst, dst, offset_dst, size);
                debug("Copy from host to device % on platform %", dev_dst, plat_dst);
            } else if (plat_dst == 0) {
                // Destination is the CPU platform
                platforms_[plat_src]->copy_to_host(dev_src, src, offset_src, dst, offset_dst, size);
                debug("Copy to host from device % on platform %", dev_src, plat_src);
            } else {
                error("Cannot copy memory between different platforms");
            }
        }
    }

    bool profiling_enabled() { return profile_ == ProfileLevel::Full; }
    std::atomic<uint64_t>& kernel_time() { return kernel_time_; }

private:
    void check_device(PlatformId plat, DeviceId dev) {
        assert((size_t)dev < platforms_[plat]->dev_count() && "Invalid device");
        unused(plat, dev);
    }

    ProfileLevel profile_;
    std::atomic<uint64_t> kernel_time_;
    std::vector<std::unique_ptr<Platform>> platforms_;
    std::unordered_map<std::string, std::string> files_;
};

AnyDSL_runtime_API Runtime& runtime();

#endif
