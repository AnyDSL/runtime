#ifndef OPENCL_PLATFORM_H
#define OPENCL_PLATFORM_H

#include "platform.h"

#include <atomic>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef __APPLE__
#include <OpenCL/cl.h>
#include <OpenCL/cl_ext.h>
#else
#define CL_USE_DEPRECATED_OPENCL_1_2_APIS
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

    void launch_kernel(DeviceId dev, const LaunchParams& launch_params) override;
    void synchronize(DeviceId dev) override;

    void copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_from_host(const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_to_host(DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_svm(const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size);
    void dynamic_profile(DeviceId dev, const std::string& filename);

    size_t dev_count() const override { return devices_.size(); }
    std::string name() const override { return "OpenCL"; }

    typedef std::unordered_map<std::string, cl_kernel> KernelMap;

    struct DeviceData {
        OpenCLPlatform* parent;
        cl_platform_id platform;
        cl_device_id dev;
        cl_uint version_major;
        cl_uint version_minor;
        std::string platform_name;
        std::string device_name;
        cl_command_queue queue = nullptr;
        cl_context ctx = nullptr;
        #ifdef CL_VERSION_2_0
        cl_device_svm_capabilities svm_caps;
        #endif
        bool is_intel_fpga = false;
        bool is_xilinx_fpga = false;

        std::unordered_map<std::string, cl_program> programs;
        std::unordered_map<cl_program, KernelMap> kernels;
        std::unordered_map<cl_kernel, cl_command_queue> kernels_queue;

        // Atomics do not have a move constructor. This structure introduces one.
        struct AtomicData {
            std::atomic_int timings_counter {};
            std::atomic_flag lock = ATOMIC_FLAG_INIT;
            AtomicData() = default;
            AtomicData(AtomicData&&) {}
        } atomic_data;

        DeviceData(
            OpenCLPlatform* parent,
            cl_platform_id platform,
            cl_device_id dev,
            cl_uint version_major,
            cl_uint version_minor,
            const std::string& platform_name,
            const std::string& device_name)
            : parent(parent)
            , platform(platform)
            , dev(dev)
            , version_major(version_major)
            , version_minor(version_minor)
            , platform_name(platform_name)
            , device_name(device_name)
        {}
        DeviceData(DeviceData&&) = default;
        DeviceData(const DeviceData&) = delete;

        void lock() {
            while (atomic_data.lock.test_and_set(std::memory_order_acquire)) ;
        }

        void unlock() {
            atomic_data.lock.clear(std::memory_order_release);
        }
    };

    std::vector<DeviceData> devices_;
    std::unordered_map<std::string, std::string> files_;

    cl_kernel load_kernel(DeviceId dev, const std::string& filename, const std::string& kernelname);

    cl_program load_program_binary(DeviceId dev, const std::string& filename, const std::string& program_string) const;
    cl_program load_program_source(DeviceId dev, const std::string& filename, const std::string& program_string) const;
    cl_program compile_program(DeviceId dev, cl_program program, const std::string& filename) const;
    cl_program load_and_compile_kernel(DeviceId dev, const std::string& filename);

    friend void time_kernel_callback(cl_event, cl_int, void*);
};

#endif
