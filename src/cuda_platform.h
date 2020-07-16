#ifndef CUDA_PLATFORM_H
#define CUDA_PLATFORM_H

#include "platform.h"
#include "runtime.h"

#include <atomic>
#include <forward_list>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#define CUDA_API_PER_THREAD_DEFAULT_STREAM
#include <cuda.h>
#include <nvvm.h>

#if CUDA_VERSION < 6050
    #error "CUDA 6.5 or higher required!"
#endif

#ifdef AnyDSL_runtime_CUDA_NVRTC
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
    void* get_device_ptr(DeviceId, void* ptr) override;
    void release(DeviceId dev, void* ptr) override;
    void release_host(DeviceId dev, void* ptr) override;

    void launch_kernel(DeviceId dev,
                       const char* file, const char* kernel,
                       const uint32_t* grid, const uint32_t* block,
                       void** args, const uint32_t* sizes, const uint32_t* aligns, const uint32_t* allocs, const KernelArgType* types,
                       uint32_t num_args) override;
    void synchronize(DeviceId dev) override;

    void copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_from_host(const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_to_host(DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) override;

    size_t dev_count() const override { return devices_.size(); }
    std::string name() const override { return "CUDA"; }

    typedef std::unordered_map<std::string, CUfunction> FunctionMap;

    struct DeviceData {
        CUdevice dev;
        CUcontext ctx;
        CUjit_target compute_capability;
        std::atomic_flag locked = ATOMIC_FLAG_INIT;
        std::unordered_map<std::string, CUmodule> modules;
        std::unordered_map<CUmodule, FunctionMap> functions;

        DeviceData() {}
        DeviceData(const DeviceData&) = delete;
        DeviceData(DeviceData&& data)
            : dev(data.dev)
            , ctx(data.ctx)
            , compute_capability(data.compute_capability)
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

    struct ProfileData {
        CudaPlatform* platform;
        CUcontext ctx;
        CUevent start;
        CUevent end;
    };

    std::mutex profile_lock_;
    std::forward_list<ProfileData*> profiles_;
    void erase_profiles(bool);

    CUfunction load_kernel(DeviceId dev, const std::string& filename, const std::string& kernelname);

    std::string compile_nvptx(DeviceId dev, const std::string& filename, const std::string& program_string) const;
    std::string compile_nvvm(DeviceId dev, const std::string& filename, const std::string& program_string) const;
    std::string compile_cuda(DeviceId dev, const std::string& filename, const std::string& program_string) const;
    CUmodule create_module(DeviceId dev, const std::string& filename, const std::string& ptx_string) const;
};

#endif
