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
    void* alloc_unified(DeviceId, int64_t) override;
    void* get_device_ptr(DeviceId, void*) override { command_unavailable("get_device_ptr"); }
    void release(DeviceId dev, void* ptr) override;
    void release_host(DeviceId, void*) override { command_unavailable("release_host"); }

    void* alloc_tex(DeviceId, void*, const TextureDesc&) override { command_unavailable("alloc_tex"); }
    void release_tex(DeviceId, void*) override { command_unavailable("release_tex"); }

    void register_file(const std::string& filename, const std::string& program_string) override;
    void launch_kernel(DeviceId dev,
                       const char* file, const char* kernel,
                       const uint32_t* grid, const uint32_t* block,
                       void** args, const uint32_t* sizes, const KernelArgType* types,
                       uint32_t num_args) override;
    void synchronize(DeviceId dev) override;

    void copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_from_host(const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_to_host(DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_svm(const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size);

    size_t dev_count() const override { return devices_.size(); }
    std::string name() const override { return "OpenCL"; }

    typedef std::unordered_map<std::string, cl_kernel> KernelMap;

    struct DeviceData {
        cl_platform_id platform;
        cl_device_id dev;
        cl_command_queue queue;
        cl_context ctx;
        bool is_intel_fpga = false;
        cl_uint version_major;
        cl_uint version_minor;
        #ifdef CL_VERSION_2_0
        cl_device_svm_capabilities svm_caps;
        #endif
        std::atomic_int timings_counter{};
        std::atomic_flag locked = ATOMIC_FLAG_INIT;
        std::unordered_map<std::string, cl_program> programs;
        std::unordered_map<cl_program, KernelMap> kernels;
        std::unordered_map<cl_kernel, cl_command_queue> kernels_queue;

        DeviceData() {}
        DeviceData(const DeviceData&) = delete;
        DeviceData(DeviceData&& data)
            : platform(data.platform)
            , dev(data.dev)
            , queue(data.queue)
            , ctx(data.ctx)
            , is_intel_fpga(data.is_intel_fpga)
            , version_major(data.version_major)
            , version_minor(data.version_minor)
            #ifdef CL_VERSION_2_0
            , svm_caps(data.svm_caps)
            #endif
            , timings_counter(0)
            , programs(std::move(data.programs))
            , kernels(std::move(data.kernels))
            , kernels_queue(std::move(data.kernels_queue))
        {}

        void lock() {
            while (locked.test_and_set(std::memory_order_acquire)) ;
        }

        void unlock() {
            locked.clear(std::memory_order_release);
        }
    };

    std::vector<DeviceData> devices_;
    std::unordered_map<std::string, std::string> files_;

    cl_kernel load_kernel(DeviceId dev, const std::string& filename, const std::string& kernelname);

    std::string load_file(const std::string& filename) const;
    cl_program compile_program(DeviceId dev, const std::string& filename, const std::string& program_string) const;

    friend void time_kernel_callback(cl_event, cl_int, void*);
};

#endif
