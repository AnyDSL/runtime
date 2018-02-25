#ifndef HSA_PLATFORM_H
#define HSA_PLATFORM_H

#include "platform.h"
#include "runtime.h"

#include <atomic>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>

#include <hsa/hsa.h>
#include <hsa/hsa_ext_amd.h>

/// HSA platform. Has the same number of devices as that of the HSA implementation.
class HSAPlatform : public Platform {
public:
    HSAPlatform(Runtime* runtime);
    ~HSAPlatform();

protected:
    void* alloc(DeviceId dev, int64_t size) override { return alloc_hsa(size, devices_[dev].coarsegrained_region); }
    void* alloc_host(DeviceId dev, int64_t size) override { return alloc_hsa(size, devices_[dev].coarsegrained_region); }
    void* alloc_unified(DeviceId dev, int64_t size) override { return alloc_hsa(size, devices_[dev].finegrained_region); }
    void* get_device_ptr(DeviceId, void* ptr) override { return ptr; }
    void release(DeviceId dev, void* ptr) override;
    void release_host(DeviceId dev, void* ptr) override { release(dev, ptr); }

    void launch_kernel(DeviceId dev,
                       const char* file, const char* kernel,
                       const uint32_t* grid, const uint32_t* block,
                       void** args, const uint32_t* sizes, const KernelArgType* types,
                       uint32_t num_args) override;
    void synchronize(DeviceId dev) override;

    void copy(const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size);
    void copy(DeviceId, const void* src, int64_t offset_src, DeviceId, void* dst, int64_t offset_dst, int64_t size) override { copy(src, offset_src, dst, offset_dst, size); }
    void copy_from_host(const void* src, int64_t offset_src, DeviceId, void* dst, int64_t offset_dst, int64_t size) override { copy(src, offset_src, dst, offset_dst, size); }
    void copy_to_host(DeviceId, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) override { copy(src, offset_src, dst, offset_dst, size); }

    size_t dev_count() const override { return devices_.size(); }
    std::string name() const override { return "HSA"; }

    typedef std::unordered_map<std::string, std::tuple<uint64_t, uint32_t, uint32_t, uint32_t>> KernelMap;

    struct DeviceData {
        hsa_agent_t agent;
        hsa_profile_t profile;
        hsa_default_float_rounding_mode_t float_mode;
        hsa_queue_t* queue;
        hsa_signal_t signal;
        hsa_region_t kernarg_region, finegrained_region, coarsegrained_region;
        std::atomic_flag locked = ATOMIC_FLAG_INIT;
        std::unordered_map<std::string, hsa_executable_t> programs;
        std::unordered_map<uint64_t, KernelMap> kernels;

        DeviceData() {}
        DeviceData(const DeviceData&) = delete;
        DeviceData(DeviceData&& data)
            : agent(data.agent)
            , profile(data.profile)
            , float_mode(data.float_mode)
            , queue(data.queue)
            , signal(data.signal)
            , kernarg_region(data.kernarg_region)
            , finegrained_region(data.finegrained_region)
            , coarsegrained_region(data.coarsegrained_region)
            , programs(std::move(data.programs))
            , kernels(std::move(data.kernels))
        {}

        void lock() {
            while (locked.test_and_set(std::memory_order_acquire)) ;
        }

        void unlock() {
            locked.clear(std::memory_order_release);
        }
    };

    uint64_t frequency_;
    std::vector<DeviceData> devices_;

    void* alloc_hsa(int64_t, hsa_region_t);
    static hsa_status_t iterate_agents_callback(hsa_agent_t, void*);
    static hsa_status_t iterate_regions_callback(hsa_region_t, void*);
    std::tuple<uint64_t, uint32_t, uint32_t, uint32_t> load_kernel(DeviceId, const std::string&, const std::string&);
};

#endif
