#ifndef OPENCL_PLATFORM_H
#define OPENCL_PLATFORM_H

#include "platform.h"
#include "runtime.h"

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __APPLE__
#include <OpenCL/cl.h>
#include <OpenCL/cl_ext.h>
#else
#include <CL/cl.h>
#include <CL/cl_ext.h>
#endif

/// OpenCL platform. Has the same number of devices as that of the OpenCL implementation.
class OpenCLPlatform : public Platform {
public:
    OpenCLPlatform(Runtime* runtime);
    ~OpenCLPlatform();

protected:
    void* alloc(DeviceId dev, int64_t size) override;
    void* alloc_host(DeviceId, int64_t) override { command_unavailable("alloc_host"); }
    void* alloc_unified(DeviceId, int64_t) override { command_unavailable("alloc_unified"); }
    void* get_device_ptr(DeviceId, void*) override { command_unavailable("get_device_ptr"); }
    void release(DeviceId dev, void* ptr) override;
    void release_host(DeviceId, void*) override { command_unavailable("release_host"); }

    void launch_kernel(DeviceId dev,
                       const char* file, const char* kernel,
                       const uint32_t* grid, const uint32_t* block,
                       void** args, const uint32_t* sizes, const KernelArgType* types,
                       uint32_t num_args) override;
    void synchronize(DeviceId dev) override;

    void copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_from_host(const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_to_host(DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) override;

    size_t dev_count() const override { return devices_.size(); }
    std::string name() const override { return "OpenCL"; }

    typedef std::unordered_map<std::string, cl_kernel> KernelMap;

    struct DeviceData {
        cl_platform_id platform;
        cl_device_id dev;
        bool isIntelFPGA = false;
        cl_command_queue queue;
        cl_context ctx;
        std::atomic_flag locked = ATOMIC_FLAG_INIT;
        std::unordered_map<std::string, cl_program> programs;
        std::unordered_map<cl_program, KernelMap> kernels;
        std::unordered_map<cl_kernel, cl_command_queue> kernels_queue; //FPGA, ordered

        DeviceData() {}
        DeviceData(const DeviceData&) = delete;
        DeviceData(DeviceData&& data)
            : platform(data.platform)
            , dev(data.dev)
            , isIntelFPGA(data.isIntelFPGA)
            , queue(data.queue)
            , ctx(data.ctx)
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

    std::vector<DeviceData> devices_;

    cl_program try_find_program(DeviceData& opencl_dev, const std::string& filename);
    void insert_program_into_cache(DeviceData& opencl_dev, const std::string& filename, cl_program& program);
    cl_kernel try_find_kernel(DeviceData& opencl_dev, const std::string& kernelname, cl_program& program);
    void insert_kernel_into_cache(DeviceData& opencl_dev, const std::string& kernelname, cl_program& program, cl_kernel& kernel);
    void insert_kernelqueue_into_cache(DeviceData& opencl_dev, cl_kernel& kernel, cl_command_queue& kernel_queue);

    cl_program create_program(DeviceData& opencl_dev, const std::string& filename, std::string& options);
    cl_program create_programFPGA(DeviceData& opencl_dev, const std::string& filename, std::string& options);
    cl_kernel create_kernel(DeviceData& opencl_dev, cl_program& program, const std::string& kernelname);
    cl_kernel create_kernelFPGA(DeviceData& opencl_dev, cl_program& program, const std::string& kernelname);
    cl_kernel load_kernel(DeviceId dev, const std::string& filename, const std::string& kernelname);

    void finish(DeviceData& opencl_dev);
    void finishFPGA(DeviceData& opencl_dev);
};

#endif
