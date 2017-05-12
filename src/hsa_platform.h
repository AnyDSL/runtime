#ifndef HSA_PLATFORM_H
#define HSA_PLATFORM_H

#include "platform.h"
#include "runtime.h"

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#include <hsa.h>

/// HSA platform. Has the same number of devices as that of the HSA implementation.
class HSAPlatform : public Platform {
public:
    HSAPlatform(Runtime* runtime);
    ~HSAPlatform();

protected:
    struct dim3 {
        int x, y, z;
        dim3(int x = 1, int y = 1, int z = 1) : x(x), y(y), z(z) {}
    };

    void* alloc(DeviceId dev, int64_t size) override;
    void* alloc_host(DeviceId, int64_t) override { platform_error(); return nullptr; }
    void* alloc_unified(DeviceId, int64_t) override { platform_error(); return nullptr; }
    void* get_device_ptr(DeviceId, void*) override { platform_error(); return nullptr; }
    void release(DeviceId dev, void* ptr) override;
    void release_host(DeviceId, void*) override { platform_error(); }

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

    int dev_count() override;

    std::string name() override { return "HSA"; }

    typedef std::unordered_map<std::string, hsa_agent_t> KernelMap;

    struct DeviceData {
        hsa_agent_t agent;
        hsa_queue_t* queue;
        hsa_region_t region;
        std::atomic_flag locked = ATOMIC_FLAG_INIT;
        //std::unordered_map<std::string, cl_program> programs;
        //std::unordered_map<cl_program, KernelMap> kernels;

        DeviceData() {}
        DeviceData(const DeviceData&) = delete;
        DeviceData(DeviceData&& data)
            : agent(data.agent)
            , queue(data.queue)
            , region(data.region)
            //, programs(std::move(data.programs))
            //, kernels(std::move(data.kernels))
        {}

        void lock() {
            while (locked.test_and_set(std::memory_order_acquire)) ;
        }

        void unlock() {
            locked.clear(std::memory_order_release);
        }
    };

    std::vector<DeviceData> devices_;

    static hsa_status_t iterate_agents_callback(hsa_agent_t, void*);
    static hsa_status_t iterate_regions_callback(hsa_region_t, void*);
    hsa_agent_t load_kernel(DeviceId dev, const std::string& filename, const std::string& kernelname);
};

#endif
