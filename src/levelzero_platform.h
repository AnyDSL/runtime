#ifndef LEVEL_ZERO_PLATFORM_H
#define LEVEL_ZERO_PLATFORM_H

#include "platform.h"

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include <level_zero/ze_api.h>


/// oneAPI Level Zero platform
class LevelZeroPlatform : public Platform {
public:
    LevelZeroPlatform(Runtime* runtime);
    ~LevelZeroPlatform();

protected:
    void* alloc(DeviceId dev, int64_t size) override;
    void* alloc_host(DeviceId, int64_t) override;
    void* alloc_unified(DeviceId, int64_t) override;
    void* get_device_ptr(DeviceId, void*) override { command_unavailable("get_device_ptr"); }
    void release(DeviceId dev, void* ptr) override;
    void release_host(DeviceId, void*) override;

    void launch_kernel(DeviceId dev, const LaunchParams& launch_params) override;
    void synchronize(DeviceId dev) override;

    void copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_from_host(const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_to_host(DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) override;

    size_t dev_count() const override { return devices_.size(); }
    std::string name() const override { return "oneAPI Level Zero"; }
    const char* device_name(DeviceId dev) const override;
    bool device_check_feature_support(DeviceId, const char*) const override { return false; }

    typedef std::unordered_map<std::string, ze_kernel_handle_t> KernelMap;

    struct DeviceData {
        LevelZeroPlatform* parent;
        ze_driver_handle_t driver;
        ze_device_handle_t device;
        std::string device_name;
        ze_command_list_handle_t queue = nullptr;
        ze_context_handle_t ctx = nullptr;
        std::unordered_map<std::string, ze_module_handle_t> modules;
        std::unordered_map<ze_module_handle_t, KernelMap> kernels;
        double timerResolution;

        DeviceData(
            LevelZeroPlatform* parent,
            ze_driver_handle_t driver,
            ze_device_handle_t device,
            const std::string& device_name)
            : parent(parent)
            , driver(driver)
            , device(device)
            , device_name(device_name)
        {}
        DeviceData(DeviceData&&) = default;
        DeviceData(const DeviceData&) = delete;
    };

    std::vector<DeviceData> devices_;
    std::vector<ze_context_handle_t> contexts_;

    ze_kernel_handle_t load_kernel(DeviceId dev, const std::string& filename, const std::string& kernelname);
    friend void determineDeviceCapabilities(ze_device_handle_t hDevice, LevelZeroPlatform::DeviceData& device);
};

#endif
