#ifndef CUDA_PLATFORM_H
#define CUDA_PLATFORM_H

#include "platform.h"
#include "runtime.h"

#include <string>
#include <unordered_map>
#include <vector>
#include <atomic>

#include <cuda.h>
#include <nvvm.h>

#if CUDA_VERSION < 6050
    #error "CUDA 6.5 or higher required!"
#endif

#ifdef CUDA_NVRTC
#include <nvrtc.h>
#endif

/// CUDA platform. Has the same number of devices as that of the CUDA implementation.
class CudaPlatform : public Platform {
public:
    CudaPlatform(Runtime* runtime);
    ~CudaPlatform();

protected:
    void* alloc(DeviceId dev, int64_t size) override;
    void* alloc_host(DeviceId dev, int64_t size) override;
    void* alloc_unified(DeviceId dev, int64_t size) override;
    void* get_device_ptr(DeviceId, void*) override;
    void release(DeviceId dev, void* ptr) override;
    void release_host(DeviceId dev, void* ptr) override;

    void launch_kernel(DeviceId dev,
                       const char* file, const char* kernel,
                       const uint32_t* grid, const uint32_t* block,
                       void** args, const uint32_t* sizes, const KernelArgType* types,
                       uint32_t num_args) override;
    void synchronize(DeviceId dev) override;

    void copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_from_host(const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_to_host(DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) override;

    int dev_count() override;

    std::string name() override { return "CUDA"; }

    typedef std::unordered_map<std::string, CUfunction> FunctionMap;

    struct DeviceData {
        CUdevice dev;
        CUcontext ctx;
        int compute_minor;
        int compute_major;
        std::atomic_flag locked = ATOMIC_FLAG_INIT;
        std::unordered_map<std::string, CUmodule> modules;
        std::unordered_map<CUmodule, FunctionMap> functions;

        DeviceData() {}
        DeviceData(const DeviceData&) = delete;
        DeviceData(DeviceData&& data)
            : dev(data.dev)
            , ctx(data.ctx)
            , compute_minor(data.compute_minor)
            , compute_major(data.compute_major)
            , modules(std::move(data.modules))
            , functions(std::move(data.functions))
        {}

        void lock() {
            while (locked.test_and_set(std::memory_order_acquire)) ;
        }

        void unlock() {
            locked.clear(std::memory_order_release);
        }
    };

    std::vector<DeviceData> devices_;

    CUfunction load_kernel(DeviceId dev, const std::string& filename, const std::string& kernelname);

    std::string load_ptx(const std::string& filename);
    CUmodule compile_nvvm(DeviceId dev, const std::string& filename, CUjit_target target_cc) const;
    CUmodule compile_cuda(DeviceId dev, const std::string& filename, CUjit_target target_cc) const;
    CUmodule create_module(DeviceId dev, const std::string& filename, CUjit_target target_cc, const void* ptx) const;
};

#endif
