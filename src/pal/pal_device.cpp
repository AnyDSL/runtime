#include "pal_device.h"
#include "pal_utils.h"

#include "../log.h"

constexpr const PalDevice::GpuVirtAddr_t nulladdr = 0;

void pal_destroy(Pal::IDestroyable* object) {
    assert(object != nullptr);
    object->Destroy();
    free(object);
}

PalDevice::PalDevice(Pal::IDevice* base_device, Runtime* runtime)
    : runtime_(runtime), device_(base_device) {
    Pal::Result result = init();
    CHECK_PAL(result, "init pal device");

    result = init_cmd_allocator();
    CHECK_PAL(result, "pal_init_cmd_allocator()");

    result = init_queue_and_cmd_buffer(queue_and_cmd_buffer_type::Compute, queue_, cmd_buffer_);
    CHECK_PAL(result, "pal_init_queue_and_cmd_buffer(Compute)");

    result = init_queue_and_cmd_buffer(queue_and_cmd_buffer_type::Dma, dma_queue_, dma_cmd_buffer_);
    CHECK_PAL(result, "pal_init_queue_and_cmd_buffer(Dma)");


    Pal::DeviceProperties device_properties;
    CHECK_PAL(device_->GetProperties(&device_properties), "device->GetProperties()");

    if (runtime_->profiling_enabled()) {
        allocate_memory(sizeof(ProfilingTimestamps), Pal::GpuHeap::GpuHeapLocal, &profiling_timestamps_);
        timestamps_frequency_ = device_properties.timestampFrequency;
    }

    gfx_level = device_properties.gfxLevel;
    isa = pal_utils::get_gfx_isa_id(device_properties.gfxLevel);
    name = pal_utils::get_gpu_name(device_properties.revision);
}

Pal::Result PalDevice::init() {
    // Query properties for the selected GPU.
    Pal::DeviceProperties device_properties = {};
    Pal::Result result = device_->GetProperties(&device_properties);
    CHECK_PAL(result, "GetProperties()");

    if (device_properties.engineProperties[Pal::QueueTypeUniversal].engineCount < 1) {
        // AnyDSL assumes a universal engine is available.
        return Pal::Result::ErrorUnavailable;
    }

    if (device_properties.engineProperties[Pal::QueueTypeUniversal].flags.supportsTimestamps != 1) {
        // Timestamps must be supported to support AnyDSL profiling
        return Pal::Result::Aborted;
    }

    if (device_properties.engineProperties[Pal::QueueTypeCompute].engineCount < 1) {
        // AnyDSL assumes a compute engine is available.
        return Pal::Result::ErrorUnavailable;
    }

    if (device_properties.engineProperties[Pal::QueueTypeCompute].flags.supportsTimestamps != 1) {
        // Timestamps must be supported to support AnyDSL profiling
        return Pal::Result::Aborted;
    }

    if (device_properties.engineProperties[Pal::QueueTypeDma].engineCount < 1) {
        // AnyDSL assumes a copy engine is available.
        return Pal::Result::ErrorUnavailable;
    }

    // At this point, we could configure PAL's behavior for this device:
    // Pal::PalPublicSettings* const settings = device_->GetPublicSettings();
    // ... modify the settings
    // Commit the settings change(s) we've made and perform initialization on the IDevice object.
    result = device_->CommitSettingsAndInit();
    CHECK_PAL(result, "CommitSettingsAndInit()");

    // Finalize is the final step of device initialization.  It must be called
    // after CommitSettingsAndInit().
    Pal::DeviceFinalizeInfo finalize_info = {};

    // The universal queue allows graphics, compute, and copy operations, and
    // corresponds to the hardware "graphics ring".
    // Request one universal queue.
    finalize_info.requestedEngineCounts[Pal::EngineTypeUniversal].engines = 1;

    // And request one compute queue.
    finalize_info.requestedEngineCounts[Pal::EngineTypeCompute].engines = 1;

    // And a copy engine
    finalize_info.requestedEngineCounts[Pal::EngineTypeDma].engines = 1;

    result = device_->Finalize(finalize_info);
    CHECK_PAL(result, "Finalize()");

    return result;
}

PalDevice::~PalDevice() {
    static_assert(Pal::GpuHeapCount == 4, "Destroy code needs an update if GpuHeapCount != 4.");

    // Ensure the GPU is idle before destroying any objects.
    if (queue_ != nullptr) {
        queue_->WaitIdle();
    }

    // Destroy any IObject or IDestroyable objects and free their corresponding system memory.
    pal_destroy(cmd_buffer_);
    pal_destroy(queue_);
    pal_destroy(dma_cmd_buffer_);
    pal_destroy(dma_queue_);
    pal_destroy(cmd_allocator_);
    for (auto& [_, pipeline] : programs_) {
        pal_destroy(pipeline);
    }
    for (auto& [_, memory] : memory_objects_) {
        pal_destroy(memory);
    }
    device_->Cleanup();

    // The device is created by the platform, it does not need to be freed at
    // all, but we should no longer maintain a pointer to it.
    device_ = nullptr;
}

Pal::IPipeline* PalDevice::create_pipeline(const void* elf_data, size_t elf_data_size) {
    Pal::ComputePipelineCreateInfo pipeline_info = {};

    // Populate the info.
    pipeline_info.pipelineBinarySize = elf_data_size;
    pipeline_info.pPipelineBinary = elf_data;

    Pal::Result result = Pal::Result::ErrorUnknown;
    std::size_t pipeline_size = device_->GetComputePipelineSize(pipeline_info, &result);
    CHECK_PAL(result, "GetComputePipelineSize()");

    Pal::IPipeline* pipeline = nullptr;
    void* pipe_line_space = malloc(pipeline_size);
    result = device_->CreateComputePipeline(pipeline_info, pipe_line_space, &pipeline);
    CHECK_PAL(result, "CreateComputePipeline()");
    assert(pipe_line_space == pipeline);

    return pipeline;
}

Pal::Result PalDevice::init_cmd_allocator() {
    Pal::CmdAllocatorCreateInfo allocator_create_info = {};
    allocator_create_info.flags.threadSafe = 1;
    allocator_create_info.flags.autoMemoryReuse = 1;
    // An allocator that uses 32, 64KB suballocations (2MB per base allocation) is
    // a pretty reasonable configuration for universal command buffers. Clients
    // may wish to use smaller sizes for DMA or compute specific allocators.
    allocator_create_info.allocInfo[Pal::CommandDataAlloc].allocHeap = Pal::GpuHeapGartCacheable;
    allocator_create_info.allocInfo[Pal::CommandDataAlloc].suballocSize = 64 * 1024;
    allocator_create_info.allocInfo[Pal::CommandDataAlloc].allocSize =
        (32 * allocator_create_info.allocInfo[Pal::CommandDataAlloc].suballocSize);
    // For embedded data (which PAL uses internally), 32 4KB suballocations (128KB
    // per base allocation) is a pretty reasonable configuration for universal
    // command buffers. Clients may wish to use smaller sizes for DMA or compute
    // specific allocators.
    const Pal::gpusize embedded_data_suballocSize = 4 * 1024;
    const Pal::gpusize embedded_data_allocSize = 32 * embedded_data_suballocSize;
    allocator_create_info.allocInfo[Pal::EmbeddedDataAlloc].allocHeap = Pal::GpuHeapGartCacheable;
    allocator_create_info.allocInfo[Pal::EmbeddedDataAlloc].suballocSize = embedded_data_suballocSize;
    allocator_create_info.allocInfo[Pal::EmbeddedDataAlloc].allocSize = embedded_data_allocSize;
    // For GPU-local scratch memory, use the same allocation size as embedded data.
    allocator_create_info.allocInfo[Pal::GpuScratchMemAlloc].allocHeap = Pal::GpuHeapInvisible;
    allocator_create_info.allocInfo[Pal::GpuScratchMemAlloc].suballocSize = embedded_data_suballocSize;
    allocator_create_info.allocInfo[Pal::GpuScratchMemAlloc].allocSize = embedded_data_allocSize;

    Pal::Result result = Pal::Result::ErrorUnknown;
    const size_t allocator_size = device_->GetCmdAllocatorSize(allocator_create_info, &result);

    CHECK_PAL(result, "GetCmdAllocatorSize()");

    result = device_->CreateCmdAllocator(allocator_create_info, malloc(allocator_size), &cmd_allocator_);

    return result;
}

inline void ThrowIfFailed(Util::Result result, const std::string& message) {
    if (Util::IsErrorResult(result)) {
        error(message.c_str());
    }
}

Pal::Result PalDevice::init_queue_and_cmd_buffer(queue_and_cmd_buffer_type type, Pal::IQueue*& queue, Pal::ICmdBuffer*& cmd_buffer) {
    // Choose queue and engine type corresponding to queue_and_cmd_buffer_type.
    Pal::QueueType queue_type;
    Pal::EngineType engine_type;
    switch (type) {
        case queue_and_cmd_buffer_type::Compute:
            queue_type = Pal::QueueType::QueueTypeCompute;
            engine_type = Pal::EngineType::EngineTypeCompute;
            break;
        case queue_and_cmd_buffer_type::Universal:
            queue_type = Pal::QueueType::QueueTypeUniversal;
            engine_type = Pal::EngineType::EngineTypeUniversal;
            break;
        case queue_and_cmd_buffer_type::Dma:
            queue_type = Pal::QueueType::QueueTypeDma;
            engine_type = Pal::EngineType::EngineTypeDma;
            break;
        default:
            // Queue and cmd buffer type not implemented.
            assert(false);
            break;
    }

    Pal::QueueCreateInfo queue_create_info = {};
    queue_create_info.queueType = queue_type;
    queue_create_info.engineType = engine_type;

    Pal::Result result = Pal::Result::ErrorUnknown;

    // Create the queue
    const size_t queue_size = device_->GetQueueSize(queue_create_info, &result);
    CHECK_PAL(result, "GetQueueSize()");

    result = device_->CreateQueue(queue_create_info, malloc(queue_size), &queue);
    CHECK_PAL(result, "CreateQueue()");

    // Create the command buffer.
    Pal::CmdBufferCreateInfo cmd_buffer_create_info = {};
    cmd_buffer_create_info.pCmdAllocator = cmd_allocator_;
    cmd_buffer_create_info.queueType = queue_type;
    cmd_buffer_create_info.engineType = engine_type;

    const size_t cmdBufSize = device_->GetCmdBufferSize(cmd_buffer_create_info, &result);
    CHECK_PAL(result, "GetCmdBufferSize()");

    return device_->CreateCmdBuffer(cmd_buffer_create_info, malloc(cmdBufSize), &cmd_buffer);
}

PalDevice::GpuVirtAddr_t PalDevice::allocate_gpu_memory(Pal::gpusize size_in_bytes, Pal::GpuHeap heap) {
    Pal::IGpuMemory* memory;
    Pal::Result result = allocate_memory(size_in_bytes, heap, &memory);
    CHECK_PAL(result, "allocate_gpu_memory(GpuHeapInvisible)");
    const auto gpu_virtual_addr = track_memory(memory);
    return gpu_virtual_addr;
}

Pal::Result PalDevice::allocate_memory(
    Pal::gpusize size_in_bytes, Pal::GpuHeap heap, Pal::IGpuMemory** gpu_memory_pp, Pal::gpusize alignment) {
    Pal::GpuMemoryHeapProperties heap_properties[Pal::GpuHeapCount] = {};
    Pal::Result result = device_->GetGpuMemoryHeapProperties(&heap_properties[0]);
    CHECK_PAL(result, "GetGpuMemoryHeapProperties()");

    Pal::GpuMemoryCreateInfo create_info = {};
    create_info.alignment = alignment;
    create_info.size = size_in_bytes;
    create_info.flags.cpuInvisible = ((heap_properties[heap].flags.cpuVisible == 0) ? 1 : 0);
    create_info.vaRange = Pal::VaRange::Default;
    create_info.heapCount = 1;
    create_info.priority = Pal::GpuMemPriority::Normal;
    create_info.priorityOffset = Pal::GpuMemPriorityOffset::Offset0;
    create_info.heaps[0] = heap;
    create_info.mallPolicy = Pal::GpuMemMallPolicy::Always;

    const size_t gpu_memory_size = device_->GetGpuMemorySize(create_info, &result);
    CHECK_PAL(result, "GetGpuMemorySize()");

    result = device_->CreateGpuMemory(create_info, malloc(gpu_memory_size), gpu_memory_pp);
    CHECK_PAL(result, "CreateGpuMemory()");

    Pal::GpuMemoryRef mem_ref = {};
    mem_ref.pGpuMemory = *gpu_memory_pp;

    // Make this allocation permanently resident. In AnyDSL usually the user takes care of
    // releasing memory as soon as it's not used anymore.
    return device_->AddGpuMemoryReferences(1, &mem_ref, nullptr, Pal::GpuMemoryRefCantTrim);
}

PalDevice::GpuVirtAddr_t PalDevice::allocate_shared_virtual_memory(Pal::gpusize sizeInBytes) {
    Pal::SvmGpuMemoryCreateInfo create_info = {};
    create_info.size = sizeInBytes;
    create_info.alignment =
        4096; // This seems to be the minimum alignment needed to avoid triggering asserts in PAL
    create_info.mallPolicy = Pal::GpuMemMallPolicy::Always;
    create_info.isUsedForKernel = false;
    create_info.pReservedGpuVaOwner = nullptr;

    Pal::Result result = Pal::Result::Success;
    const size_t gpu_memory_size = device_->GetSvmGpuMemorySize(create_info, &result);
    CHECK_PAL(result, "GetSvmGpuMemorySize()");

    Pal::IGpuMemory* shared_virtual_memory;
    result = device_->CreateSvmGpuMemory(create_info, malloc(gpu_memory_size), &shared_virtual_memory);
    CHECK_PAL(result, "CreateSvmGpuMemory()");

    const auto shared_virtual_addr = track_memory(shared_virtual_memory);
    return shared_virtual_addr;
}

PalDevice::GpuVirtAddr_t PalDevice::track_memory(Pal::IGpuMemory* memory) {
    const GpuVirtAddr_t gpu_address = memory->Desc().gpuVirtAddr;
    assert(memory_objects_.find(gpu_address) == memory_objects_.end()
           && "Virtual address is already in memory_objects map.");
    memory_objects_[gpu_address] = memory;
    return gpu_address;
}

void PalDevice::forget_memory(GpuVirtAddr_t gpu_address) {
    assert(memory_objects_.find(gpu_address) != memory_objects_.end()
           && "Virtual address is not in memory_objects map.");

    memory_objects_.erase(gpu_address);
}

Pal::IGpuMemory* PalDevice::get_memory_object(const GpuVirtAddr_t gpu_address) const {
    auto memory_object_it = memory_objects_.find(gpu_address);
    if (memory_object_it == memory_objects_.end()) {
        assert(false && "Key (virtual gpu memory address) not in memory_objects map.");
        return nullptr;
    }
    return memory_object_it->second;
}

void PalDevice::release_gpu_memory(GpuVirtAddr_t virtual_address) {
    if (virtual_address == nulladdr) {
        assert(false && "release() provided with nullptr.");
        return;
    }
    Pal::IGpuMemory* memory = get_memory_object(virtual_address);
    if (memory == nullptr) {
        return;
    }

    memory->Destroy();
    free(memory);
    forget_memory(virtual_address);
}

void PalDevice::copy_gpu_data(
    const GpuVirtAddr_t source, GpuVirtAddr_t destination, const Pal::MemoryCopyRegion& copy_region) {
    Pal::CmdBufferBuildInfo cmd_buffer_build_info = {};
    cmd_buffer_build_info.flags.optimizeExclusiveSubmit = 1;
    Pal::Result result = dma_cmd_buffer_->Begin(cmd_buffer_build_info);
    CHECK_PAL(result, "dma_cmd_buffer_->Begin()");

    // Add copy command to cmd buffer.
    const Pal::uint32 region_count = 1;

    const auto& src_memory = *get_memory_object(source);
    const auto& dst_memory = *get_memory_object(destination);
    dma_cmd_buffer_->CmdCopyMemory(src_memory, dst_memory, region_count, &copy_region);

    result = dma_cmd_buffer_->End();
    CHECK_PAL(result, "dma_cmd_buffer_->End()");

    // Submit cmd buffer to queue.
    Pal::PerSubQueueSubmitInfo per_sub_queue_submit_info = {};
    per_sub_queue_submit_info.cmdBufferCount = 1;
    per_sub_queue_submit_info.ppCmdBuffers = &dma_cmd_buffer_;
    per_sub_queue_submit_info.pCmdBufInfoList = nullptr;

    Pal::SubmitInfo submit_info = {};
    submit_info.pPerSubQueueInfo = &per_sub_queue_submit_info;
    submit_info.perSubQueueInfoCount = 1;

    result = dma_queue_->Submit(submit_info);
    CHECK_PAL(result, "dma_queue_->Submit()");

    // Wait for queue to complete commands.
    // TODO: Do we want this WaitIdle here?
    result = dma_queue_->WaitIdle();
    CHECK_PAL(result, "dma_queue_->WaitIdle()");
}

void PalDevice::dispatch(const Pal::CmdBufferBuildInfo& cmd_buffer_build_info,
    const Pal::PipelineBindParams& pipeline_bind_params, const Pal::BarrierInfo& barrier_info,
    const LaunchParams& launch_params) {
    // Make sure we can cast to uint16_t
    assert(launch_params.block[0] <= 65535);
    assert(launch_params.block[1] <= 65535);
    assert(launch_params.block[2] <= 65535);

    const uint16_t block_size_x = (uint16_t)launch_params.block[0];
    const uint16_t block_size_y = (uint16_t)launch_params.block[1];
    const uint16_t block_size_z = (uint16_t)launch_params.block[2];
    // Grid size is provided in threads (not in thread groups, as pal expects).
    const uint32_t grid_size_x = launch_params.grid[0];
    const uint32_t grid_size_y = launch_params.grid[1];
    const uint32_t grid_size_z = launch_params.grid[2];

    const Pal::DispatchDims dispatch_dimensions{
        .x = std::max(1u, ((grid_size_x + block_size_x - 1) / block_size_x)),
        .y = std::max(1u, ((grid_size_y + block_size_y - 1) / block_size_y)),
        .z = std::max(1u, ((grid_size_z + block_size_z - 1) / block_size_z)),
    };

    struct PalUserData {
        Pal::uint32 buffer_vaddr[2];
    } pal_user_data = {};

    const bool are_params_present = launch_params.num_args > 0;
    if (are_params_present) {
        GpuVirtAddr_t kernargs_buffer =
            build_kernargs_buffer(launch_params.args, launch_params.num_args, launch_params.kernel_name);
        assert(kernargs_buffer && "Kernargs buffer could not be built for launch param arguments.");

        std::memcpy(&pal_user_data.buffer_vaddr, &kernargs_buffer, sizeof(uint64_t));
    }

    Pal::Result result = cmd_buffer_->Begin(cmd_buffer_build_info);
    CHECK_PAL(result, "cmdBuffer->Begin()");

    // Omit first two uint32s (hold parameter buffer address) in the struct if there are no parameters.
    cmd_buffer_->CmdSetUserData(Pal::PipelineBindPoint::Compute, 0, are_params_present ? 2 : 0,
        reinterpret_cast<Pal::uint32*>(&pal_user_data));

    cmd_buffer_->CmdBindPipeline(pipeline_bind_params);

    if (runtime_->profiling_enabled()) {
        constexpr Pal::HwPipePoint pipe_point = Pal::HwPipeTop;
        Pal::BarrierInfo barrier_info = {};
        barrier_info.waitPoint = Pal::HwPipePreCs;
        barrier_info.pipePointWaitCount = 1;
        barrier_info.pPipePoints = &pipe_point;
        barrier_info.globalSrcCacheMask = Pal::CoherShader;
        barrier_info.globalDstCacheMask = Pal::CoherShader;
        cmd_buffer_->CmdBarrier(barrier_info);
        // Record start
        cmd_buffer_->CmdWriteTimestamp(Pal::HwPipeBottom, *profiling_timestamps_, offsetof(ProfilingTimestamps, start));
    }

    cmd_buffer_->CmdDispatch(dispatch_dimensions);
    cmd_buffer_->CmdBarrier(barrier_info);

    if (runtime_->profiling_enabled()) {
        // Record end
        cmd_buffer_->CmdWriteTimestamp(Pal::HwPipeBottom, *profiling_timestamps_, offsetof(ProfilingTimestamps, end)); 
    }

    result = cmd_buffer_->End();
    CHECK_PAL(result, "cmdBuffer->End()");

    Pal::PerSubQueueSubmitInfo per_sub_queue_submit_info = {};
    per_sub_queue_submit_info.cmdBufferCount = 1;
    per_sub_queue_submit_info.ppCmdBuffers = &cmd_buffer_;
    per_sub_queue_submit_info.pCmdBufInfoList = nullptr;

    Pal::SubmitInfo submit_info = {};
    submit_info.pPerSubQueueInfo = &per_sub_queue_submit_info;
    submit_info.perSubQueueInfoCount = 1;

    result = queue_->Submit(submit_info);
    CHECK_PAL(result, "queue->Submit()");

    if (runtime_->profiling_enabled()) {
        result = queue_->WaitIdle();
        CHECK_PAL(result, "queue->WaitIdle() for profiling purposes");

        ProfilingTimestamps timestamps;
        pal_utils::read_from_memory(reinterpret_cast<uint8_t*>(&timestamps), profiling_timestamps_, 0, sizeof(ProfilingTimestamps));

        // Convert to seconds, as frequency is in HZ, then convert to microseconds for reporting
        runtime_->kernel_time().fetch_add(1000000.0 * double(timestamps.end - timestamps.start) / double(timestamps_frequency_));
    }
}

void PalDevice::WaitIdle() {
    CHECK_PAL(queue_->WaitIdle(), "queue_->WaitIdle()");
    CHECK_PAL(dma_queue_->WaitIdle(), "dma_queue_->WaitIdle()");
}

PalDevice::GpuVirtAddr_t PalDevice::write_data_to_gpu(
    Pal::gpusize byte_size, std::function<void(void*)> write_callback) {
    PalDevice::GpuVirtAddr_t gpu_only_buffer =
        allocate_gpu_memory(byte_size, pal_utils::find_gpu_local_heap(device_, byte_size));

    PalDevice::GpuVirtAddr_t intermediate_mem = allocate_gpu_memory(byte_size, Pal::GpuHeap::GpuHeapLocal);
    {
        Pal::IGpuMemory* intermediate_mem_obj = get_memory_object(intermediate_mem);
        void* mapped_intermediate = nullptr;
        Pal::Result result = intermediate_mem_obj->Map(&mapped_intermediate);
        CHECK_PAL(result, "intermediate_mem_obj->Map()");

        write_callback(mapped_intermediate);

        result = intermediate_mem_obj->Unmap();
        CHECK_PAL(result, "intermediate_mem_obj->Unmap()");
    }
    copy_gpu_data(intermediate_mem, gpu_only_buffer, {.srcOffset = 0, .dstOffset = 0, .copySize = byte_size});
    release_gpu_memory(intermediate_mem);

    return gpu_only_buffer;
}

uint32_t round_up(uint32_t val, uint32_t step_size) {
    return (val + step_size - 1) / step_size * step_size;
}

uint32_t PalDevice::calculate_launch_params_size(const ParamsArgs& params_args, uint32_t num_args) {
    uint32_t total_size = 0;
    for (uint32_t i = 0; i < num_args; i++) {
        total_size = round_up(total_size, params_args.aligns[i]) + params_args.alloc_sizes[i];
    }
    return total_size;
};

size_t PalDevice::write_launch_params(const ParamsArgs& params_args, uint32_t num_args, void* memory, size_t memory_size) {
    const uint8_t* const initial_memory_ptr = reinterpret_cast<uint8_t*>(memory);
    for (uint32_t i = 0; i < num_args; i++) {
        // align base address for next kernel argument
        if (!std::align(
                params_args.aligns[i], params_args.alloc_sizes[i], memory, memory_size)) {
            error("Incorrect kernel argument alignment detected.");
        }
        std::memcpy(memory, params_args.data[i], params_args.sizes[i]);
        memory = reinterpret_cast<uint8_t*>(memory) + params_args.alloc_sizes[i];
    }
    // Return the number of bytes occupied by launch_paramas.
    return reinterpret_cast<uint8_t*>(memory) - initial_memory_ptr;
}

PalDevice::GpuVirtAddr_t PalDevice::build_kernargs_buffer(
    const ParamsArgs& params_args, int num_args, const char* kernel_name) {
    if (num_args == 0)
        return nulladdr;

    uint32_t kernarg_segment_size = calculate_launch_params_size(params_args, num_args);

    size_t used_memory;
    GpuVirtAddr_t kernargs_buffer = write_data_to_gpu(kernarg_segment_size, [&](void* dest_mem) {
        used_memory = write_launch_params(params_args, num_args, dest_mem, kernarg_segment_size);
    });

    if (used_memory != kernarg_segment_size) {
        error("PAL kernarg segment size for kernel '%' differs from argument size: % vs. %", kernel_name,
            kernarg_segment_size, used_memory);
    }

    return kernargs_buffer;
}
