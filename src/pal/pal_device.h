#ifndef PAL_DEVICE_DATA_H
#define PAL_DEVICE_DATA_H

#include "../runtime.h"
#include "pal_utils.h"

#include <atomic>
#include <functional>
#include <string>
#include <unordered_map>

#include <pal.h>
#include <palLib.h>
#include <palPipeline.h>
#include <palPlatform.h>
#include <palQueue.h>

class PALPlatform;

class PalDevice {
public:
    typedef Pal::gpusize GpuVirtAddr_t;
    typedef std::unordered_map<std::string, Pal::IPipeline*> KernelMap;

    enum class queue_and_cmd_buffer_type { Compute, Universal, Dma };

    PalDevice(){};
    PalDevice(Pal::IDevice* base_device, Runtime* runtime);
    PalDevice(const PalDevice&) = delete;
    PalDevice(PalDevice&& other)
        : runtime_(other.runtime_)
        , device_(other.device_)
        , cmd_allocator_(other.cmd_allocator_)
        , queue_(other.queue_)
        , cmd_buffer_(other.cmd_buffer_)
        , profiling_timestamps_(other.profiling_timestamps_)
        , timestamps_frequency_(other.timestamps_frequency_)
        , programs_(std::move(other.programs_))
        , kernels_(std::move(other.kernels_))
        , memory_objects_(std::move(other.memory_objects_))
        , gfx_level(other.gfx_level)
        , isa(std::move(other.isa))
        , name(std::move(other.name)) {}

    ~PalDevice();

    void lock() {
        while (locked_.test_and_set(std::memory_order_acquire))
            ;
    }

    void unlock() { locked_.clear(std::memory_order_release); }

    Pal::IPipeline* create_pipeline(const void* elf_data, size_t elf_data_size);

    // Allocates memory of the requested size on the requested gpu heap (controls visibility).
    // Returns the virtual gpu address of the allocated memory.
    GpuVirtAddr_t allocate_gpu_memory(Pal::gpusize size_in_bytes, Pal::GpuHeap heap);

    GpuVirtAddr_t allocate_shared_virtual_memory(Pal::gpusize sizeInBytes);

    void release_gpu_memory(GpuVirtAddr_t virtual_address);
    void release_gpu_memory(void* virtual_address) {
        release_gpu_memory(reinterpret_cast<PalDevice::GpuVirtAddr_t>(virtual_address));
    }

    void copy_gpu_data(
        const GpuVirtAddr_t source, GpuVirtAddr_t destination, const Pal::MemoryCopyRegion& copy_region);
    void copy_gpu_data(const void* source, void* destination, const Pal::MemoryCopyRegion& copy_region) {
        copy_gpu_data(reinterpret_cast<PalDevice::GpuVirtAddr_t>(source),
            reinterpret_cast<PalDevice::GpuVirtAddr_t>(destination), copy_region);
    }

    void dispatch(const Pal::CmdBufferBuildInfo& cmd_buffer_build_info,
        const Pal::PipelineBindParams& pipeline_bind_params, const Pal::BarrierInfo& barrier_info,
        const LaunchParams& launch_params);

    void WaitIdle();

private:
    friend PALPlatform;

    Pal::Result init();

    // Creates a PAL queue object and corresponding command buffer object into the given pointers.
    Pal::Result init_queue_and_cmd_buffer(queue_and_cmd_buffer_type type, Pal::IQueue*& queue, Pal::ICmdBuffer*& cmd_buffer);

    // Creates a PAL command allocator which is needed to allocate memory for all
    // command buffer objects.
    Pal::Result init_cmd_allocator();

    Pal::Result allocate_memory(Pal::gpusize size_in_bytes, Pal::GpuHeap heap,
        Pal::IGpuMemory** gpu_memory_pp, Pal::gpusize alignment = 256 * 1024);

    // Returns the key to the new map entry. This key is the virtual gpu memory address.
    GpuVirtAddr_t track_memory(Pal::IGpuMemory* memory);
    void forget_memory(GpuVirtAddr_t gpu_address);
    // Returns nullptr if key is not present in memory_objects map.
    Pal::IGpuMemory* get_memory_object(const GpuVirtAddr_t gpu_address) const;
    Pal::IGpuMemory* get_memory_object(const void* gpu_address) const {
        return get_memory_object(reinterpret_cast<PalDevice::GpuVirtAddr_t>(gpu_address));
    }

    // Build a buffer holding the kernel arguments and upload to the GPU.
    // Returns the address of the buffer on the gpu.
    GpuVirtAddr_t build_kernargs_buffer(const ParamsArgs& params_args, int num_args, const char* kernel_name);

    // Helper function that allocates a gpu-only buffer of the given size and uploads the data written by the
    // write_callback
    PalDevice::GpuVirtAddr_t write_data_to_gpu(
        Pal::gpusize byte_size, std::function<void(void*)> write_callback);

    uint32_t calculate_launch_params_size(const ParamsArgs& params_args, uint32_t num_args);
    // Write kernel arguments to memory. Returns the number of bytes occupied by the passed in kernel arguments.
    size_t write_launch_params(const ParamsArgs& params_args, uint32_t num_args, void* memory, size_t memory_size);

private:
    Runtime* runtime_ = nullptr;

    Pal::IDevice* device_ = nullptr;
    Pal::ICmdAllocator* cmd_allocator_ = nullptr;
    Pal::IQueue* queue_ = nullptr;
    Pal::ICmdBuffer* cmd_buffer_ = nullptr;

    Pal::IQueue* dma_queue_ = nullptr;
    Pal::ICmdBuffer* dma_cmd_buffer_ = nullptr;

    struct ProfilingTimestamps {
        uint64_t start;
        uint64_t end;
    };
    Pal::IGpuMemory* profiling_timestamps_ = nullptr;
    uint64_t timestamps_frequency_ = 0;

    std::atomic_flag locked_ = ATOMIC_FLAG_INIT;

    std::unordered_map<std::string, Pal::IPipeline*> programs_;
    std::unordered_map<uint64_t, KernelMap> kernels_;

    // Map virtual addresses on the GPU to the PAL objects representing the memory.
    // This is needed because AnyDSL assumes it deals with gpu-legal addresses in its API.
    // However, to interact with PAL we need to have the wrapper objects at hand.
    // The IGpuMemory objects should not be used outside of this class.
    std::unordered_map<GpuVirtAddr_t, Pal::IGpuMemory*> memory_objects_;

public:
    Pal::GfxIpLevel gfx_level;
    std::string isa;
    std::string name;
};

#endif