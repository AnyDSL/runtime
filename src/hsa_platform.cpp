#include "hsa_platform.h"
#include "runtime.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

#ifndef KERNEL_DIR
#define KERNEL_DIR ""
#endif

#define CHECK_HSA(err, name)  check_hsa_error(err, name, __FILE__, __LINE__)

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
        status = hsa_queue_create(agent, queue_size, HSA_QUEUE_TYPE_SINGLE, NULL, NULL, UINT32_MAX, UINT32_MAX, &queue);
        CHECK_HSA(status, "hsa_queue_create()");
    }

    hsa_signal_t signal;
    status = hsa_signal_create(0, 0, NULL, &signal);
    CHECK_HSA(status, "hsa_signal_create()");

    auto dev = devices_->size();
    devices_->resize(dev + 1);
    DeviceData* device = &(*devices_)[dev];
    device->agent = agent;
    device->profile = profile;
    device->float_mode = float_mode;
    device->queue = queue;
    device->signal = signal;
    device->kernarg_region.handle = { 0 };
    device->finegrained_region.handle = { 0 };
    device->coarsegrained_region.handle = { 0 };

    status = hsa_agent_iterate_regions(agent, iterate_regions_callback, device);
    CHECK_HSA(status, "hsa_agent_iterate_regions()");

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
        CHECK_HSA(status, "hsa_queue_destroy()");
    }

    hsa_shut_down();
}

void* HSAPlatform::alloc(DeviceId dev, int64_t size) {
    if (!size)
        return nullptr;

    char* mem;
    hsa_status_t status = hsa_memory_allocate(devices_[dev].finegrained_region, size, (void**) &mem);
    CHECK_HSA(status, "hsa_memory_allocate()");

    return (void*)mem;
}

void HSAPlatform::release(DeviceId, void* ptr) {
    hsa_status_t status = hsa_memory_free(ptr);
    CHECK_HSA(status, "hsa_memory_free()");
}

//static thread_local cl_event end_kernel;
extern std::atomic<uint64_t> anydsl_kernel_time;

void HSAPlatform::launch_kernel(DeviceId dev,
                                const char* file, const char* name,
                                const uint32_t* grid, const uint32_t* block,
                                void** args, const uint32_t* sizes, const KernelArgType*,
                                uint32_t num_args) {
    uint64_t kernel;
    uint32_t kernarg_segment_size;
    uint32_t group_segment_size;
    uint32_t private_segment_size;
    std::tie(kernel, kernarg_segment_size, group_segment_size, private_segment_size) = load_kernel(dev, file, name);

    // set up arguments
    hsa_status_t status;
    void* kernarg_address = nullptr;
    status = hsa_memory_allocate(devices_[dev].kernarg_region, kernarg_segment_size, &kernarg_address);
    CHECK_HSA(status, "hsa_memory_allocate()");
    size_t offset = 0;
    auto align_address = [] (size_t base, size_t align) {
        return ((base + align - 1) / align) * align;
    };
    for (uint32_t i = 0; i < num_args; i++) {
        // align base address for next kernel argument
        offset = align_address(offset, sizes[i]);
        std::memcpy((void*)((char*)kernarg_address + offset), args[i], sizes[i]);
        offset += sizes[i];
    }
    if (offset != kernarg_segment_size)
        debug("HSA kernarg segment size for kernel '%' differs from argument size: % vs. %", name, kernarg_segment_size, offset);

    auto queue = devices_[dev].queue;
    auto signal = devices_[dev].signal;
    hsa_signal_add_relaxed(signal, 1);

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
    aql.completion_signal = signal;
    aql.kernel_object = kernel;
    aql.kernarg_address = kernarg_address;
    aql.private_segment_size = private_segment_size;
    aql.group_segment_size = group_segment_size;

    // write to command queue
    const uint64_t index = hsa_queue_load_write_index_relaxed(queue);
    const uint32_t queue_mask = queue->size - 1;
    ((hsa_kernel_dispatch_packet_t*)(queue->base_address))[index & queue_mask] = aql;
    hsa_queue_store_write_index_relaxed(queue, index + 1);
    hsa_signal_store_relaxed(queue->doorbell_signal, index);
}

void HSAPlatform::synchronize(DeviceId dev) {
    auto signal = devices_[dev].signal;
    hsa_signal_value_t completion = hsa_signal_wait_relaxed(signal, HSA_SIGNAL_CONDITION_EQ, 0, UINT64_MAX, HSA_WAIT_STATE_ACTIVE);
    if (completion != 0)
        debug("HSA signal completion failed: %", completion);
}

void HSAPlatform::copy(const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) {
    hsa_status_t status = hsa_memory_copy((char*)dst + offset_dst, (char*)src + offset_src, size);
    CHECK_HSA(status, "hsa_memory_copy()");
}

int HSAPlatform::dev_count() {
    return devices_.size();
}

std::tuple<uint64_t, uint32_t, uint32_t, uint32_t> HSAPlatform::load_kernel(DeviceId dev, const std::string& filename, const std::string& kernelname) {
    auto& hsa_dev = devices_[dev];
    hsa_status_t status;

    hsa_dev.lock();

    hsa_executable_t executable = { 0 };
    auto& prog_cache = hsa_dev.programs;
    auto prog_it = prog_cache.find(filename);
    if (prog_it == prog_cache.end()) {
        hsa_dev.unlock();
        if (std::ifstream(filename).good()) {
            hsa_code_object_reader_t reader;
            hsa_file_t file = open(std::string(KERNEL_DIR + filename).c_str(), O_RDONLY);
            status = hsa_code_object_reader_create_from_file(file, &reader);
            CHECK_HSA(status, "hsa_code_object_reader_create_from_file()");

            debug("Compiling '%' on HSA device %", filename, dev);

            status = hsa_executable_create_alt(HSA_PROFILE_FULL /* hsa_dev.profile */, hsa_dev.float_mode, NULL, &executable);
            CHECK_HSA(status, "hsa_executable_create_alt()");

            // TODO
            //hsa_loaded_code_object_t program_code_object;
            //status = hsa_executable_load_program_code_object(executable, reader, "", &program_code_object);
            //CHECK_HSA(status, "hsa_executable_load_program_code_object()");
            // -> hsa_executable_global_variable_define()
            // -> hsa_executable_agent_variable_define()
            // -> hsa_executable_readonly_variable_define()

            hsa_loaded_code_object_t agent_code_object;
            status = hsa_executable_load_agent_code_object(executable, hsa_dev.agent, reader, NULL, &agent_code_object);
            CHECK_HSA(status, "hsa_executable_load_agent_code_object()");

            status = hsa_executable_freeze(executable, NULL);
            CHECK_HSA(status, "hsa_executable_freeze()");

            status = hsa_code_object_reader_destroy(reader);
            CHECK_HSA(status, "hsa_code_object_reader_destroy()");
            close(file);

            uint32_t validated;
            status = hsa_executable_validate(executable, &validated);
            CHECK_HSA(status, "hsa_executable_validate()");

            if (validated != 0)
                debug("HSA executable validation failed: %", validated);
        } else {
            error("Could not find kernel file '%'", filename);
        }

        hsa_dev.lock();
        prog_cache[filename] = executable;
    } else {
        executable = prog_it->second;
    }

    // checks that the kernel exists
    auto& kernel_cache = hsa_dev.kernels;
    auto& kernel_map = kernel_cache[executable.handle];
    auto kernel_it = kernel_map.find(kernelname);
    uint64_t kernel = 0;
    uint32_t kernarg_segment_size = 0;
    uint32_t group_segment_size = 0;
    uint32_t private_segment_size = 0;
    if (kernel_it == kernel_map.end()) {
        hsa_dev.unlock();

        hsa_executable_symbol_t kernel_symbol = { 0 };
        // DEPRECATED: use hsa_executable_get_symbol_by_linker_name if available
        status = hsa_executable_get_symbol_by_name(executable, kernelname.c_str(), &hsa_dev.agent, &kernel_symbol);
        CHECK_HSA(status, "hsa_executable_get_symbol_by_name()");

        status = hsa_executable_symbol_get_info(kernel_symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_OBJECT, &kernel);
        CHECK_HSA(status, "hsa_executable_symbol_get_info()");
        status = hsa_executable_symbol_get_info(kernel_symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_KERNARG_SEGMENT_SIZE, &kernarg_segment_size);
        CHECK_HSA(status, "hsa_executable_symbol_get_info()");
        status = hsa_executable_symbol_get_info(kernel_symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_GROUP_SEGMENT_SIZE, &group_segment_size);
        CHECK_HSA(status, "hsa_executable_symbol_get_info()");
        status = hsa_executable_symbol_get_info(kernel_symbol, HSA_EXECUTABLE_SYMBOL_INFO_KERNEL_PRIVATE_SEGMENT_SIZE, &private_segment_size);
        CHECK_HSA(status, "hsa_executable_symbol_get_info()");

        hsa_dev.lock();
        kernel_cache[executable.handle].emplace(kernelname, std::make_tuple(kernel, kernarg_segment_size, group_segment_size, private_segment_size));
    } else {
        std::tie(kernel, kernarg_segment_size, group_segment_size, private_segment_size) = kernel_it->second;
    }

    hsa_dev.unlock();

    return std::make_tuple(kernel, kernarg_segment_size, group_segment_size, private_segment_size);
}
