#include "hsa_platform.h"
#include "runtime.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <sstream>
#include <thread>

#ifdef AnyDSL_runtime_HAS_JIT_SUPPORT
#include <lld/Common/Driver.h>
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#endif

#define CHECK_HSA(err, name) check_hsa_error(err, name, __FILE__, __LINE__)
#define CODE_OBJECT_VERSION 2

inline void check_hsa_error(hsa_status_t err, const char* name, const char* file, const int line) {
    if (err != HSA_STATUS_SUCCESS) {
        const char* status_string;
        hsa_status_t ret = hsa_status_string(err, &status_string);
        if (ret != HSA_STATUS_SUCCESS)
            info("hsa_status_string failed: %", ret);
        error("HSA API function % (%) [file %, line %]: %", name, err, file, line, status_string);
    }
}

std::string get_device_profile_str(hsa_profile_t profile) {
    #define HSA_PROFILE_TYPE(TYPE) case TYPE: return #TYPE;
    switch (profile) {
        HSA_PROFILE_TYPE(HSA_PROFILE_BASE)
        HSA_PROFILE_TYPE(HSA_PROFILE_FULL)
        default: return "unknown HSA profile";
    }
}

std::string get_device_type_str(hsa_device_type_t device_type) {
    #define HSA_DEVICE_TYPE(TYPE) case TYPE: return #TYPE;
    switch (device_type) {
        HSA_DEVICE_TYPE(HSA_DEVICE_TYPE_CPU)
        HSA_DEVICE_TYPE(HSA_DEVICE_TYPE_GPU)
        HSA_DEVICE_TYPE(HSA_DEVICE_TYPE_DSP)
        default: return "unknown HSA device type";
    }
}

std::string get_region_segment_str(hsa_region_segment_t region_segment) {
    #define HSA_REGION_SEGMENT(TYPE) case TYPE: return #TYPE;
    switch (region_segment) {
        HSA_REGION_SEGMENT(HSA_REGION_SEGMENT_GLOBAL)
        HSA_REGION_SEGMENT(HSA_REGION_SEGMENT_READONLY)
        HSA_REGION_SEGMENT(HSA_REGION_SEGMENT_PRIVATE)
        HSA_REGION_SEGMENT(HSA_REGION_SEGMENT_GROUP)
        HSA_REGION_SEGMENT(HSA_REGION_SEGMENT_KERNARG)
        default: return "unknown HSA region segment";
    }
}

std::string get_memory_pool_segment_str(hsa_amd_segment_t amd_segment) {
    #define HSA_AMD_SEGMENT(TYPE) case TYPE: return #TYPE;
    switch (amd_segment) {
        HSA_AMD_SEGMENT(HSA_AMD_SEGMENT_GLOBAL)
        HSA_AMD_SEGMENT(HSA_AMD_SEGMENT_READONLY)
        HSA_AMD_SEGMENT(HSA_AMD_SEGMENT_PRIVATE)
        HSA_AMD_SEGMENT(HSA_AMD_SEGMENT_GROUP)
        default: return "unknown HSA AMD segment";
    }
}

hsa_status_t HSAPlatform::iterate_agents_callback(hsa_agent_t agent, void* data) {
    auto devices_ = static_cast<std::vector<DeviceData>*>(data);
    hsa_status_t status;

    char name[64] = { 0 };
    status = hsa_agent_get_info(agent, HSA_AGENT_INFO_NAME, name);
    CHECK_HSA(status, "hsa_agent_get_info()");
    debug("  (%) Device Name: %", devices_->size(), name);
    status = hsa_agent_get_info(agent, HSA_AGENT_INFO_VENDOR_NAME, name);
    CHECK_HSA(status, "hsa_agent_get_info()");
    debug("      Device Vendor: %", name);

    hsa_profile_t profile;
    status = hsa_agent_get_info(agent, HSA_AGENT_INFO_PROFILE, &profile);
    CHECK_HSA(status, "hsa_agent_get_info()");
    debug("      Device profile: %", get_device_profile_str(profile));

    hsa_default_float_rounding_mode_t float_mode;
    status = hsa_agent_get_info(agent, HSA_AGENT_INFO_DEFAULT_FLOAT_ROUNDING_MODE, &float_mode);
    CHECK_HSA(status, "hsa_agent_get_info()");

    hsa_isa_t isa;
    status = hsa_agent_get_info(agent, HSA_AGENT_INFO_ISA, &isa);
    CHECK_HSA(status, "hsa_agent_get_info()");
    uint32_t name_length;
    status = hsa_isa_get_info_alt(isa, HSA_ISA_INFO_NAME_LENGTH, &name_length);
    status = hsa_isa_get_info_alt(isa, HSA_ISA_INFO_NAME, &name);
    debug("      Device ISA: %", name);
    std::string name_string = name;
    auto dash_pos = name_string.rfind('-');
    std::string isa_name = dash_pos != std::string::npos ? name_string.substr(dash_pos + 1) : "";

    hsa_device_type_t device_type;
    status = hsa_agent_get_info(agent, HSA_AGENT_INFO_DEVICE, &device_type);
    CHECK_HSA(status, "hsa_agent_get_info()");
    debug("      Device Type: %", get_device_type_str(device_type));

    uint16_t version_major, version_minor;
    status = hsa_agent_get_info(agent, HSA_AGENT_INFO_VERSION_MAJOR, &version_major);
    CHECK_HSA(status, "hsa_agent_get_info()");
    status = hsa_agent_get_info(agent, HSA_AGENT_INFO_VERSION_MINOR, &version_minor);
    CHECK_HSA(status, "hsa_agent_get_info()");
    debug("      HSA Runtime Version: %.%", version_major, version_minor);

    uint32_t queue_size = 0;
    status = hsa_agent_get_info(agent, HSA_AGENT_INFO_QUEUE_MAX_SIZE, &queue_size);
    CHECK_HSA(status, "hsa_agent_get_info()");
    debug("      Queue Size: %", queue_size);

    hsa_queue_t* queue = nullptr;
    if (queue_size > 0) {
        status = hsa_queue_create(agent, queue_size, HSA_QUEUE_TYPE_SINGLE, nullptr, nullptr, UINT32_MAX, UINT32_MAX, &queue);
        CHECK_HSA(status, "hsa_queue_create()");

        status = hsa_amd_profiling_set_profiler_enabled(queue, 1);
        CHECK_HSA(status, "hsa_amd_profiling_set_profiler_enabled()");
    }

    auto dev = devices_->size();
    devices_->resize(dev + 1);
    DeviceData* device = &(*devices_)[dev];
    device->agent = agent;
    device->profile = profile;
    device->float_mode = float_mode;
    device->isa = isa_name;
    device->queue = queue;
    device->kernarg_region.handle = { 0 };
    device->finegrained_region.handle = { 0 };
    device->coarsegrained_region.handle = { 0 };
    device->amd_kernarg_pool.handle = { 0 };
    device->amd_finegrained_pool.handle = { 0 };
    device->amd_coarsegrained_pool.handle = { 0 };

    status = hsa_signal_create(0, 0, nullptr, &device->signal);
    CHECK_HSA(status, "hsa_signal_create()");
    status = hsa_agent_iterate_regions(agent, iterate_regions_callback, device);
    CHECK_HSA(status, "hsa_agent_iterate_regions()");
    status = hsa_amd_agent_iterate_memory_pools(agent, iterate_memory_pools_callback, device);
    CHECK_HSA(status, "hsa_amd_agent_iterate_memory_pools()");

    return HSA_STATUS_SUCCESS;
}

hsa_status_t HSAPlatform::iterate_regions_callback(hsa_region_t region, void* data) {
    DeviceData* device = static_cast<DeviceData*>(data);
    hsa_status_t status;

    hsa_region_segment_t segment;
    status = hsa_region_get_info(region, HSA_REGION_INFO_SEGMENT, &segment);
    CHECK_HSA(status, "hsa_region_get_info()");
    debug("      Region Segment: %", get_region_segment_str(segment));

    hsa_region_global_flag_t flags;
    status = hsa_region_get_info(region, HSA_REGION_INFO_GLOBAL_FLAGS, &flags);
    CHECK_HSA(status, "hsa_region_get_info()");

    std::string global_flags;
    if (flags & HSA_REGION_GLOBAL_FLAG_KERNARG) {
        global_flags += "HSA_REGION_GLOBAL_FLAG_KERNARG ";
        device->kernarg_region = region;
    }
    if (flags & HSA_REGION_GLOBAL_FLAG_FINE_GRAINED) {
        global_flags += "HSA_REGION_GLOBAL_FLAG_FINE_GRAINED ";
        device->finegrained_region = region;
    }
    if (flags & HSA_REGION_GLOBAL_FLAG_COARSE_GRAINED) {
        global_flags += "HSA_REGION_GLOBAL_FLAG_COARSE_GRAINED ";
        device->coarsegrained_region = region;
    }
    debug("      Region Global Flags: %", global_flags);

    bool runtime_alloc_allowed;
    status = hsa_region_get_info(region, HSA_REGION_INFO_RUNTIME_ALLOC_ALLOWED, &runtime_alloc_allowed);
    CHECK_HSA(status, "hsa_region_get_info()");
    debug("      Region Runtime Alloc Allowed: %", runtime_alloc_allowed);

    return HSA_STATUS_SUCCESS;
}

hsa_status_t HSAPlatform::iterate_memory_pools_callback(hsa_amd_memory_pool_t memory_pool, void* data) {
    DeviceData* device = static_cast<DeviceData*>(data);
    hsa_status_t status;

    hsa_amd_segment_t segment;
    status = hsa_amd_memory_pool_get_info(memory_pool, HSA_AMD_MEMORY_POOL_INFO_SEGMENT, &segment);
    CHECK_HSA(status, "hsa_amd_memory_pool_get_info()");
    debug("      AMD Memory Pool Segment: %", get_memory_pool_segment_str(segment));

    hsa_amd_memory_pool_global_flag_t flags;
    status = hsa_amd_memory_pool_get_info(memory_pool, HSA_AMD_MEMORY_POOL_INFO_GLOBAL_FLAGS, &flags);
    CHECK_HSA(status, "hsa_amd_memory_pool_get_info()");

    std::string global_flags;
    if (flags & HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_KERNARG_INIT) {
        global_flags += "HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_KERNARG_INIT ";
        device->amd_kernarg_pool = memory_pool;
    }
    if (flags & HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_FINE_GRAINED) {
        global_flags += "HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_FINE_GRAINED ";
        device->amd_finegrained_pool = memory_pool;
    }
    if (flags & HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_COARSE_GRAINED) {
        global_flags += "HSA_AMD_MEMORY_POOL_GLOBAL_FLAG_COARSE_GRAINED ";
        device->amd_coarsegrained_pool = memory_pool;
    }
    debug("      AMD Memory Pool Global Flags: %", global_flags);

    bool runtime_alloc_allowed;
    status = hsa_amd_memory_pool_get_info(memory_pool, HSA_AMD_MEMORY_POOL_INFO_RUNTIME_ALLOC_ALLOWED, &runtime_alloc_allowed);
    CHECK_HSA(status, "hsa_amd_memory_pool_get_info()");
    debug("      AMD Memory Pool Runtime Alloc Allowed: %", runtime_alloc_allowed);

    return HSA_STATUS_SUCCESS;
}

// factory method
template<> template<>
Platform* PlatformFactory<HSAPlatform>::create(Runtime* runtime, const std::string&) {
    return new HSAPlatform(runtime);
};

HSAPlatform::HSAPlatform(Runtime* runtime)
    : Platform(runtime)
{
    hsa_status_t status = hsa_init();
    CHECK_HSA(status, "hsa_init()");

    uint16_t version_major, version_minor;
    status = hsa_system_get_info(HSA_SYSTEM_INFO_VERSION_MAJOR, &version_major);
    CHECK_HSA(status, "hsa_system_get_info()");
    status = hsa_system_get_info(HSA_SYSTEM_INFO_VERSION_MINOR, &version_minor);
    CHECK_HSA(status, "hsa_system_get_info()");
    debug("HSA System Runtime Version: %.%", version_major, version_minor);

    status = hsa_system_get_info(HSA_SYSTEM_INFO_TIMESTAMP_FREQUENCY, &frequency_);
    CHECK_HSA(status, "hsa_system_get_info()");

    status = hsa_iterate_agents(iterate_agents_callback, &devices_);
    CHECK_HSA(status, "hsa_iterate_agents()");
}

HSAPlatform::~HSAPlatform() {
    hsa_status_t status;

    for (size_t i = 0; i < devices_.size(); i++) {
        for (auto& it : devices_[i].programs) {
            status = hsa_executable_destroy(it.second);
            CHECK_HSA(status, "hsa_executable_destroy()");
        }
        if (auto queue = devices_[i].queue) {
            status = hsa_queue_destroy(queue);
            CHECK_HSA(status, "hsa_queue_destroy()");
        }
        status = hsa_signal_destroy(devices_[i].signal);
        CHECK_HSA(status, "hsa_signal_destroy()");
        for (auto kernel_pair : devices_[i].kernels) {
            for (auto kernel : kernel_pair.second) {
                if (kernel.second.kernarg_segment) {
                    status = hsa_memory_free(kernel.second.kernarg_segment);
                    CHECK_HSA(status, "hsa_memory_free()");
                }
            }
        }
    }

    hsa_shut_down();
}

void* HSAPlatform::alloc_hsa(int64_t size, hsa_region_t region) {
    if (!size)
        return nullptr;

    char* mem;
    hsa_status_t status = hsa_memory_allocate(region, size, (void**) &mem);
    CHECK_HSA(status, "hsa_memory_allocate()");

    return (void*)mem;
}

void* HSAPlatform::alloc_hsa(int64_t size, hsa_amd_memory_pool_t memory_pool) {
    if (!size)
        return nullptr;

    char* mem;
    hsa_status_t status = hsa_amd_memory_pool_allocate(memory_pool, size, 0, (void**) &mem);
    CHECK_HSA(status, "hsa_amd_memory_pool_allocate()");

    return (void*)mem;
}

void HSAPlatform::release(DeviceId, void* ptr) {
    hsa_status_t status = hsa_amd_memory_pool_free(ptr);
    CHECK_HSA(status, "hsa_amd_memory_pool_free()");
}

extern std::atomic<uint64_t> anydsl_kernel_time;

void HSAPlatform::launch_kernel(DeviceId dev,
                                const char* file, const char* name,
                                const uint32_t* grid, const uint32_t* block,
                                void** args, const uint32_t* sizes, const uint32_t* aligns, const KernelArgType*,
                                uint32_t num_args) {
    auto queue = devices_[dev].queue;
    if (!queue)
        error("The selected HSA device '%' cannot execute kernels", dev);

    auto& kernel_info = load_kernel(dev, file, name);

    // set up arguments
    if (num_args) {
        if (!kernel_info.kernarg_segment) {
            size_t total_size = 0;
            for (uint32_t i = 0; i < num_args; i++) {
                total_size += sizes[i];
                if (i != num_args - 1 && total_size % aligns[i + 1])
                    total_size += aligns[i + 1] - total_size % aligns[i + 1];
            }
            kernel_info.kernarg_segment_size = total_size;
            hsa_status_t status = hsa_memory_allocate(devices_[dev].kernarg_region, kernel_info.kernarg_segment_size, &kernel_info.kernarg_segment);
            CHECK_HSA(status, "hsa_memory_allocate()");
        }
        void*  cur   = kernel_info.kernarg_segment;
        size_t space = kernel_info.kernarg_segment_size;
        for (uint32_t i = 0; i < num_args; i++) {
            // align base address for next kernel argument
            if (!std::align(aligns[i], sizes[i], cur, space))
                error("Incorrect kernel argument alignment detected");
            std::memcpy(cur, args[i], sizes[i]);
            cur = reinterpret_cast<uint8_t*>(cur) + sizes[i];
        }
        size_t total = reinterpret_cast<uint8_t*>(cur) - reinterpret_cast<uint8_t*>(kernel_info.kernarg_segment);
        if (total != kernel_info.kernarg_segment_size)
            error("HSA kernarg segment size for kernel '%' differs from argument size: % vs. %", name, kernel_info.kernarg_segment_size, total);
    }

    auto signal = devices_[dev].signal;
    hsa_signal_add_relaxed(signal, 1);

    hsa_signal_t launch_signal;
    if (runtime_->profiling_enabled()) {
        hsa_status_t status = hsa_signal_create(1, 0, nullptr, &launch_signal);
        CHECK_HSA(status, "hsa_signal_create()");
    } else
        launch_signal = signal;

    // construct aql packet
    hsa_kernel_dispatch_packet_t aql;
    std::memset(&aql, 0, sizeof(aql));

    aql.header = (HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_ACQUIRE_FENCE_SCOPE) |
                 (HSA_FENCE_SCOPE_SYSTEM << HSA_PACKET_HEADER_RELEASE_FENCE_SCOPE) |
                 (HSA_PACKET_TYPE_KERNEL_DISPATCH << HSA_PACKET_HEADER_TYPE);
    aql.setup = 3 << HSA_KERNEL_DISPATCH_PACKET_SETUP_DIMENSIONS;
    aql.workgroup_size_x = (uint16_t)block[0];
    aql.workgroup_size_y = (uint16_t)block[1];
    aql.workgroup_size_z = (uint16_t)block[2];
    aql.grid_size_x = grid[0];
    aql.grid_size_y = grid[1];
    aql.grid_size_z = grid[2];
    aql.completion_signal    = launch_signal;
    aql.kernel_object        = kernel_info.kernel;
    aql.kernarg_address      = kernel_info.kernarg_segment;
    aql.private_segment_size = kernel_info.private_segment_size;
    aql.group_segment_size   = kernel_info.group_segment_size;

    // write to command queue
    const uint64_t index = hsa_queue_load_write_index_relaxed(queue);
    const uint32_t queue_mask = queue->size - 1;
    ((hsa_kernel_dispatch_packet_t*)(queue->base_address))[index & queue_mask] = aql;
    hsa_queue_store_write_index_relaxed(queue, index + 1);
    hsa_signal_store_relaxed(queue->doorbell_signal, index);

    if (runtime_->profiling_enabled())
        std::thread ([=] {
            hsa_signal_value_t completion = hsa_signal_wait_relaxed(launch_signal, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_ACTIVE);
            if (completion != 0)
                error("HSA launch_signal completion failed: %", completion);

            hsa_amd_profiling_dispatch_time_t dispatch_times = { 0, 0 };
            hsa_status_t status = hsa_amd_profiling_get_dispatch_time(devices_[dev].agent, launch_signal, &dispatch_times);
            CHECK_HSA(status, "hsa_amd_profiling_get_dispatch_time()");

            anydsl_kernel_time.fetch_add(1000000.0 * double(dispatch_times.end - dispatch_times.start) / double(frequency_));
            hsa_signal_subtract_relaxed(signal, 1);

            status = hsa_signal_destroy(launch_signal);
            CHECK_HSA(status, "hsa_signal_destroy()");
        }).detach();
}

void HSAPlatform::synchronize(DeviceId dev) {
    auto signal = devices_[dev].signal;
    hsa_signal_value_t completion = hsa_signal_wait_relaxed(signal, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_ACTIVE);
    if (completion != 0)
        error("HSA signal completion failed: %", completion);
}

void HSAPlatform::copy(const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) {
    hsa_status_t status = hsa_memory_copy((char*)dst + offset_dst, (char*)src + offset_src, size);
    CHECK_HSA(status, "hsa_memory_copy()");
}

HSAPlatform::KernelInfo& HSAPlatform::load_kernel(DeviceId dev, const std::string& filename, const std::string& kernelname) {
    auto& hsa_dev = devices_[dev];
    hsa_status_t status;

    hsa_dev.lock();

    hsa_executable_t executable = { 0 };
    auto& prog_cache = hsa_dev.programs;
    auto prog_it = prog_cache.find(filename);
    if (prog_it == prog_cache.end()) {
        hsa_dev.unlock();

        // find the file extension
        auto ext_pos = filename.rfind('.');
        std::string ext = ext_pos != std::string::npos ? filename.substr(ext_pos + 1) : "";
        if (ext != "gcn" && ext != "amdgpu")
            error("Incorrect extension for kernel file '%' (should be '.gcn' or '.amdgpu')", filename);

        // load file from disk or cache
        std::string src_code = runtime().load_file(filename);

        // compile src or load from cache
        std::string gcn = ext == "gcn" ? src_code : runtime().load_cache(devices_[dev].isa + src_code);
        if (gcn.empty()) {
            if (ext == "amdgpu") {
                gcn = compile_gcn(dev, filename, src_code);
            }
            runtime().store_cache(devices_[dev].isa + src_code, gcn);
        }

        hsa_code_object_reader_t reader;
        status = hsa_code_object_reader_create_from_memory(gcn.data(), gcn.size(), &reader);
        CHECK_HSA(status, "hsa_code_object_reader_create_from_file()");

        debug("Compiling '%' on HSA device %", filename, dev);

        status = hsa_executable_create_alt(HSA_PROFILE_FULL /* hsa_dev.profile */, hsa_dev.float_mode, nullptr, &executable);
        CHECK_HSA(status, "hsa_executable_create_alt()");

        // TODO
        //hsa_loaded_code_object_t program_code_object;
        //status = hsa_executable_load_program_code_object(executable, reader, "", &program_code_object);
        //CHECK_HSA(status, "hsa_executable_load_program_code_object()");
        // -> hsa_executable_global_variable_define()
        // -> hsa_executable_agent_variable_define()
        // -> hsa_executable_readonly_variable_define()

        hsa_loaded_code_object_t agent_code_object;
        status = hsa_executable_load_agent_code_object(executable, hsa_dev.agent, reader, nullptr, &agent_code_object);
        CHECK_HSA(status, "hsa_executable_load_agent_code_object()");

        status = hsa_executable_freeze(executable, nullptr);
        CHECK_HSA(status, "hsa_executable_freeze()");

        status = hsa_code_object_reader_destroy(reader);
        CHECK_HSA(status, "hsa_code_object_reader_destroy()");

        uint32_t validated;
        status = hsa_executable_validate(executable, &validated);
        CHECK_HSA(status, "hsa_executable_validate()");

        if (validated != 0)
            debug("HSA executable validation failed: %", validated);

        hsa_dev.lock();
        prog_cache[filename] = executable;
    } else {
        executable = prog_it->second;
    }

    // checks that the kernel exists
    auto& kernel_cache = hsa_dev.kernels;
    auto& kernel_map = kernel_cache[executable.handle];
    auto kernel_it = kernel_map.find(kernelname);
    if (kernel_it == kernel_map.end()) {
        hsa_dev.unlock();

        hsa_executable_symbol_t kernel_symbol = { 0 };
        std::string symbol_name = kernelname;
        #if CODE_OBJECT_VERSION == 3
        symbol_name += ".kd";
        #endif
        // DEPRECATED: use hsa_executable_get_symbol_by_linker_name if available
        status = hsa_executable_get_symbol_by_name(executable, symbol_name.c_str(), &hsa_dev.agent, &kernel_symbol);
        CHECK_HSA(status, "hsa_executable_get_symbol_by_name()");

        KernelInfo kernel_info;
        kernel_info.kernarg_segment = nullptr;

        status = hsa_executable_symbol_get_info(kernel_symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT, &kernel_info.kernel);
        CHECK_HSA(status, "hsa_executable_symbol_get_info()");
        status = hsa_executable_symbol_get_info(kernel_symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE, &kernel_info.kernarg_segment_size);
        CHECK_HSA(status, "hsa_executable_symbol_get_info()");
        status = hsa_executable_symbol_get_info(kernel_symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE, &kernel_info.group_segment_size);
        CHECK_HSA(status, "hsa_executable_symbol_get_info()");
        status = hsa_executable_symbol_get_info(kernel_symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE, &kernel_info.private_segment_size);
        CHECK_HSA(status, "hsa_executable_symbol_get_info()");

        #if CODE_OBJECT_VERSION == 2
        // metadata are not yet extracted from code object version 3
        if (kernel_info.kernarg_segment_size) {
            status = hsa_memory_allocate(hsa_dev.kernarg_region, kernel_info.kernarg_segment_size, &kernel_info.kernarg_segment);
            CHECK_HSA(status, "hsa_memory_allocate()");
        }
        #endif

        hsa_dev.lock();
        std::tie(kernel_it, std::ignore) = kernel_cache[executable.handle].emplace(kernelname, kernel_info);
    }

    // We need to get the reference now, while we have the lock, since re-hashing
    // may impact the validity of the iterator (but references are *not* invalidated)
    KernelInfo& kernel_info = kernel_it->second;
    hsa_dev.unlock();

    return kernel_info;
}

#ifdef AnyDSL_runtime_HAS_JIT_SUPPORT
bool llvm_amdgpu_initialized = false;
std::string HSAPlatform::emit_gcn(const std::string& program, const std::string& cpu, const std::string &filename, int opt) const {
    if (!llvm_amdgpu_initialized) {
        // ANYDSL_LLVM_ARGS="-amdgpu-sroa -amdgpu-load-store-vectorizer -amdgpu-scalarize-global-loads -amdgpu-internalize-symbols -amdgpu-early-inline-all -amdgpu-sdwa-peephole -amdgpu-dpp-combine -enable-amdgpu-aa -amdgpu-late-structurize=0 -amdgpu-function-calls -amdgpu-simplify-libcall -amdgpu-ir-lower-kernel-arguments -amdgpu-atomic-optimizations -amdgpu-mode-register"
        const char* env_var = std::getenv("ANYDSL_LLVM_ARGS");
        if (env_var) {
            std::vector<const char*> c_llvm_args;
            std::vector<std::string> llvm_args = { "gcn" };
            std::istringstream stream(env_var);
            std::string tmp;
            while (stream >> tmp)
                llvm_args.push_back(tmp);
            for (auto &str : llvm_args)
                c_llvm_args.push_back(str.c_str());
            llvm::cl::ParseCommandLineOptions(c_llvm_args.size(), c_llvm_args.data(), "AnyDSL gcn JIT compiler\n");
        }
        LLVMInitializeAMDGPUTarget();
        LLVMInitializeAMDGPUTargetInfo();
        LLVMInitializeAMDGPUTargetMC();
        LLVMInitializeAMDGPUAsmParser();
        LLVMInitializeAMDGPUAsmPrinter();
        llvm_amdgpu_initialized = true;
    }

    llvm::LLVMContext llvm_context;
    llvm::SMDiagnostic diagnostic_err;
    std::unique_ptr<llvm::Module> llvm_module = llvm::parseIR(llvm::MemoryBuffer::getMemBuffer(program)->getMemBufferRef(), diagnostic_err, llvm_context);

    if (!llvm_module) {
        std::string stream;
        llvm::raw_string_ostream llvm_stream(stream);
        diagnostic_err.print("", llvm_stream);
        error("Parsing IR file %: %", filename, llvm_stream.str());
    }

    auto triple_str = llvm_module->getTargetTriple();
    std::string error_str;
    auto target = llvm::TargetRegistry::lookupTarget(triple_str, error_str);
    llvm::TargetOptions options;
    options.AllowFPOpFusion = llvm::FPOpFusion::Fast;
    options.NoTrappingFPMath = true;
    std::string attrs = "-trap-handler";
    #if CODE_OBJECT_VERSION == 2
    attrs += ",-code-object-v3";
    #endif
    std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(triple_str, cpu, attrs, options, llvm::Reloc::PIC_, llvm::CodeModel::Small, llvm::CodeGenOpt::Aggressive));

    // link ocml.amdgcn and ocml config
    if (cpu.compare(0, 3, "gfx"))
        error("Expected gfx ISA, got %", cpu);
    std::string isa_version = std::string(&cpu[3]);
    std::string wavefrontsize64 = std::stoi(isa_version) >= 1000 ? "0" : "1";
    std::string  isa_file = "/opt/rocm/lib/oclc_isa_version_" + isa_version + ".amdgcn.bc";
    std::string ocml_file = "/opt/rocm/lib/ocml.amdgcn.bc";
    std::string ockl_file = "/opt/rocm/lib/ockl.amdgcn.bc";
    std::string ocml_config = R"(; Module anydsl ocml config
                                @__oclc_finite_only_opt = addrspace(4) constant i8 0
                                @__oclc_unsafe_math_opt = addrspace(4) constant i8 0
                                @__oclc_daz_opt = addrspace(4) constant i8 0
                                @__oclc_correctly_rounded_sqrt32 = addrspace(4) constant i8 0
                                @__oclc_wavefrontsize64 = addrspace(4) constant i8 )" + wavefrontsize64;
    std::unique_ptr<llvm::Module> isa_module(llvm::parseIRFile(isa_file, diagnostic_err, llvm_context));
    if (isa_module == nullptr)
        error("Can't create isa module for '%'", isa_file);
    std::unique_ptr<llvm::Module> config_module = llvm::parseIR(llvm::MemoryBuffer::getMemBuffer(ocml_config)->getMemBufferRef(), diagnostic_err, llvm_context);
    if (config_module == nullptr)
        error("Can't create ocml config module");
    std::unique_ptr<llvm::Module> ocml_module(llvm::parseIRFile(ocml_file, diagnostic_err, llvm_context));
    if (ocml_module == nullptr)
        error("Can't create ocml module for '%'", ocml_file);
    std::unique_ptr<llvm::Module> ockl_module(llvm::parseIRFile(ockl_file, diagnostic_err, llvm_context));
    if (ockl_module == nullptr)
        error("Can't create ockl module for '%'", ockl_file);

    // override data layout with the one coming from the target machine
    llvm_module->setDataLayout(machine->createDataLayout());
     isa_module->setDataLayout(machine->createDataLayout());
    ocml_module->setDataLayout(machine->createDataLayout());
    ockl_module->setDataLayout(machine->createDataLayout());
    config_module->setDataLayout(machine->createDataLayout());

    llvm::Linker linker(*llvm_module.get());
    if (linker.linkInModule(std::move(ocml_module), llvm::Linker::Flags::LinkOnlyNeeded))
        error("Can't link ocml into module");
    if (linker.linkInModule(std::move(ockl_module), llvm::Linker::Flags::LinkOnlyNeeded))
        error("Can't link ockl into module");
    if (linker.linkInModule(std::move(isa_module), llvm::Linker::Flags::None))
        error("Can't link isa into module");
    if (linker.linkInModule(std::move(config_module), llvm::Linker::Flags::None))
        error("Can't link config into module");

    llvm::legacy::FunctionPassManager function_pass_manager(llvm_module.get());
    llvm::legacy::PassManager module_pass_manager;

    module_pass_manager.add(llvm::createTargetTransformInfoWrapperPass(machine->getTargetIRAnalysis()));
    function_pass_manager.add(llvm::createTargetTransformInfoWrapperPass(machine->getTargetIRAnalysis()));

    llvm::PassManagerBuilder builder;
    builder.OptLevel = opt;
    builder.Inliner = llvm::createFunctionInliningPass(builder.OptLevel, 0, false);
    machine->adjustPassManager(builder);
    builder.populateFunctionPassManager(function_pass_manager);
    builder.populateModulePassManager(module_pass_manager);

    machine->Options.MCOptions.AsmVerbose = true;

    llvm::SmallString<0> outstr;
    llvm::raw_svector_ostream llvm_stream(outstr);

    //machine->addPassesToEmitFile(module_pass_manager, llvm_stream, nullptr, llvm::TargetMachine::CGFT_AssemblyFile, true);
    machine->addPassesToEmitFile(module_pass_manager, llvm_stream, nullptr, llvm::TargetMachine::CGFT_ObjectFile, true);

    function_pass_manager.doInitialization();
    for (auto func = llvm_module->begin(); func != llvm_module->end(); ++func)
        function_pass_manager.run(*func);
    function_pass_manager.doFinalization();
    module_pass_manager.run(*llvm_module);

    std::string obj(outstr.begin(), outstr.end());
    std::string obj_file = filename + ".obj";
    std::string gcn_file = filename + ".gcn";
    runtime().store_file(obj_file, obj);
    std::vector<const char*> lld_args = {
        "ld",
        "-shared",
        obj_file.c_str(),
        "-o",
        gcn_file.c_str()
    };
    if (!lld::elf::link(lld_args, false))
        error("Generating gcn using ld");

    return runtime().load_file(gcn_file);
}
#else
std::string HSAPlatform::emit_gcn(const std::string&, const std::string&, const std::string &, int) const {
    error("Recompile runtime with RUNTIME_JIT enabled for gcn support.");
}
#endif

std::string HSAPlatform::compile_gcn(DeviceId dev, const std::string& filename, const std::string& program_string) const {
    debug("Compiling AMDGPU to GCN using amdgpu for '%' on HSA device %", filename, dev);
    return emit_gcn(program_string, devices_[dev].isa, filename, 3);
}
