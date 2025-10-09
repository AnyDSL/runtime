#include "levelzero_platform.h"

#include "runtime.h"

#ifndef _WIN32
#include <limits.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <sstream>


static std::string get_ze_error_code_str(int error) {
    #define ZE_ERROR_CODE(CODE) case CODE: return #CODE
    switch (error) {
        ZE_ERROR_CODE(ZE_RESULT_ERROR_UNINITIALIZED);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_DEVICE_LOST);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_OUT_OF_HOST_MEMORY);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_OUT_OF_DEVICE_MEMORY);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_INVALID_ENUMERATION);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_INVALID_NULL_POINTER);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_INVALID_NULL_HANDLE);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_INVALID_SIZE);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_HANDLE_OBJECT_IN_USE);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_UNSUPPORTED_SIZE);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_UNSUPPORTED_ALIGNMENT);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_INVALID_SYNCHRONIZATION_OBJECT);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_MODULE_BUILD_FAILURE);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_INVALID_NATIVE_BINARY);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_INVALID_KERNEL_NAME);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_INVALID_MODULE_UNLINKED);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_INVALID_GROUP_SIZE_DIMENSION);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_INVALID_KERNEL_ARGUMENT_INDEX);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_INVALID_KERNEL_ARGUMENT_SIZE);
        ZE_ERROR_CODE(ZE_RESULT_NOT_READY);
        ZE_ERROR_CODE(ZE_RESULT_ERROR_INVALID_ARGUMENT);

        default: return "unknown error code";
    }
    #undef ZE_ERROR_CODE
}

#define CHECK_LEVEL_ZERO(err, name)  check_ze_error(err, name, __FILE__, __LINE__)
#define WRAP_LEVEL_ZERO_HANDLER(FUNC, HANDLER) do { ze_result_t err = FUNC; { HANDLER } check_ze_error(err, #FUNC, __FILE__, __LINE__); } while(false)
#define WRAP_LEVEL_ZERO(FUNC) WRAP_LEVEL_ZERO_HANDLER(FUNC, if (false) {})

inline void check_ze_error(ze_result_t err, const char* name, const char* file, const int line) {
    if (err != ZE_RESULT_SUCCESS)
        error("oneAPI Level Zero function % (%) [file %, line %]: %", name, err, file, line, get_ze_error_code_str(err));
}

template<typename T>
std::string to_string(const T& value) {
    std::stringstream ss;
    ss << value;
    return ss.str();
}

template<>
std::string to_string(const ze_device_memory_property_flag_t& value)
{
    const auto bits = static_cast<uint32_t>(value);

    std::string str;

    if (0 == bits)
        str += "0 | ";

    if (static_cast<uint32_t>(ZE_DEVICE_MEMORY_PROPERTY_FLAG_TBD) & bits)
        str += "MEMORY_PROPERTY_FLAG_TBD | ";

    return (str.size() > 3)
        ? std::string("Device::{ ") + str.substr(0, str.size() - 3) + std::string(" }")
        : std::string("Device::{ ? }");
}

template<>
std::string to_string(const ze_device_memory_properties_t& value)
{
    std::string str;

    str += "flags : " + to_string((ze_device_memory_property_flag_t)value.flags) + "\n";
    str += "maxClockRate : " + to_string(value.maxClockRate) + "\n";
    str += "maxBusWidth : " + to_string(value.maxBusWidth) + "\n";
    str += "totalSize : " + to_string(value.totalSize) + "\n";
    str += "name : " + to_string(value.name) + "\n";

    return str;
}

void determineDeviceCapabilities(ze_device_handle_t hDevice, LevelZeroPlatform::DeviceData& device)
{
    ze_device_compute_properties_t compute_properties = {};
    compute_properties.stype = ZE_STRUCTURE_TYPE_DEVICE_COMPUTE_PROPERTIES;
    WRAP_LEVEL_ZERO(zeDeviceGetComputeProperties(hDevice, &compute_properties));
    //std::cout << to_string(compute_properties) << "\n";

    uint32_t memoryCount = 0;
    WRAP_LEVEL_ZERO(zeDeviceGetMemoryProperties(hDevice, &memoryCount, nullptr));
    std::vector<ze_device_memory_properties_t> memoryProperties(memoryCount);
    for (uint32_t mem = 0; mem < memoryCount; ++mem)
    {
        memoryProperties[mem].stype = ZE_STRUCTURE_TYPE_DEVICE_MEMORY_PROPERTIES;
        memoryProperties[mem].pNext = nullptr;
    }
    WRAP_LEVEL_ZERO(zeDeviceGetMemoryProperties(hDevice, &memoryCount, memoryProperties.data()));
    for (uint32_t mem = 0; mem < memoryCount; ++mem)
    {
        debug("%", to_string(memoryProperties[mem]).c_str());
    }

    ze_device_memory_access_properties_t memory_access_properties = {};
    memory_access_properties.stype = ZE_STRUCTURE_TYPE_DEVICE_MEMORY_ACCESS_PROPERTIES;
    WRAP_LEVEL_ZERO(zeDeviceGetMemoryAccessProperties(hDevice, &memory_access_properties));
    //std::cout << to_string(memory_access_properties) << "\n";

    uint32_t cacheCount = 0;
    WRAP_LEVEL_ZERO(zeDeviceGetCacheProperties(hDevice, &cacheCount, nullptr));
    std::vector<ze_device_cache_properties_t> cacheProperties(cacheCount);
    for (uint32_t cache = 0; cache < cacheCount; ++cache)
    {
        cacheProperties[cache].stype = ZE_STRUCTURE_TYPE_DEVICE_CACHE_PROPERTIES;
        cacheProperties[cache].pNext = nullptr;
    }
    WRAP_LEVEL_ZERO(zeDeviceGetCacheProperties(hDevice, &cacheCount, cacheProperties.data()));
    for (uint32_t cache = 0; cache < cacheCount; ++cache)
    {
        //std::cout << to_string(pCacheProperties[cache]) << "\n";
    }

    ze_device_image_properties_t image_properties = {};
    image_properties.stype = ZE_STRUCTURE_TYPE_DEVICE_IMAGE_PROPERTIES;
    WRAP_LEVEL_ZERO(zeDeviceGetImageProperties(hDevice, &image_properties));
    //std::cout << to_string(image_properties) << "\n";
}

LevelZeroPlatform::LevelZeroPlatform(Runtime* runtime)
    : Platform(runtime)
{
    // limit to GPUs since we do use VPUs yet
    WRAP_LEVEL_ZERO(zeInit(ZE_INIT_FLAG_GPU_ONLY));

    uint32_t driverCount = 0;
    WRAP_LEVEL_ZERO(zeDriverGet(&driverCount, nullptr));

    std::vector<ze_driver_handle_t> allDrivers(driverCount, nullptr);
    WRAP_LEVEL_ZERO(zeDriverGet(&driverCount, allDrivers.data()));

    for (uint32_t i = 0; i < driverCount; ++i) {
        ze_driver_handle_t hDriver = allDrivers[i];

        uint32_t deviceCount = 0;
        WRAP_LEVEL_ZERO(zeDeviceGet(hDriver, &deviceCount, nullptr));

        std::vector<ze_device_handle_t> allDevices(deviceCount, nullptr);
        WRAP_LEVEL_ZERO(zeDeviceGet(hDriver, &deviceCount, allDevices.data()));

        ze_api_version_t apiVersion;
        WRAP_LEVEL_ZERO(zeDriverGetApiVersion(hDriver, &apiVersion));

        ze_driver_properties_t driver_properties = {};
        driver_properties.stype = ZE_STRUCTURE_TYPE_DRIVER_PROPERTIES;
        WRAP_LEVEL_ZERO(zeDriverGetProperties(hDriver, &driver_properties));

        debug("oneAPI Level Zero Driver Version %", driver_properties.driverVersion);
        debug("oneAPI Level Zero API Version %.%", ZE_MAJOR_VERSION(apiVersion), ZE_MINOR_VERSION(apiVersion));

        std::vector<ze_device_handle_t> utilizedDevices;

        for (uint32_t d = 0; d < deviceCount; ++d) {
            ze_device_handle_t hDevice = allDevices[d];
            ze_device_properties_t device_properties;
            device_properties.stype = ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES;
            device_properties.pNext = nullptr;
            WRAP_LEVEL_ZERO(zeDeviceGetProperties(hDevice, &device_properties));

            if (device_properties.type != ZE_DEVICE_TYPE_GPU)
                continue;

            DeviceData& dev = devices_.emplace_back(this, hDriver, hDevice, std::string(device_properties.name));
            if (device_properties.stype == ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES_1_2) {
                dev.timerResolution = 1000000000.0 / static_cast<double>(device_properties.timerResolution);
            } else {
                dev.timerResolution = device_properties.timerResolution;
            }
            determineDeviceCapabilities(hDevice, dev);

            utilizedDevices.push_back(hDevice);
        }

        ze_context_desc_t context_desc = {};
        context_desc.stype = ZE_STRUCTURE_TYPE_CONTEXT_DESC;
        ze_context_handle_t ctx = nullptr;
        WRAP_LEVEL_ZERO(zeContextCreateEx(hDriver, &context_desc, utilizedDevices.size(), utilizedDevices.data(), &ctx));

        contexts_.push_back(ctx);

        for (DeviceData& dev : devices_) {
            dev.ctx = ctx;
            // Create an immediate command list for direct submission
            ze_command_queue_desc_t altdesc = {};
            altdesc.stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC;
            WRAP_LEVEL_ZERO(zeCommandListCreateImmediate(dev.ctx, dev.device, &altdesc, &dev.queue));
        }
    }
}

LevelZeroPlatform::~LevelZeroPlatform() {
    for (size_t i = 0; i < devices_.size(); ++i) {
        DeviceData& dev = devices_[i];

        for (auto& map : devices_[i].kernels)
            for (auto& it : map.second)
                WRAP_LEVEL_ZERO(zeKernelDestroy(it.second));

        for (auto& it : devices_[i].modules)
            WRAP_LEVEL_ZERO(zeModuleDestroy(it.second));

        WRAP_LEVEL_ZERO(zeCommandListDestroy(dev.queue));
    }

    for (auto& ctx : contexts_) {
        WRAP_LEVEL_ZERO(zeContextDestroy(ctx));
    }
}

void* LevelZeroPlatform::alloc(DeviceId dev, int64_t size) {
    if (!size) return nullptr;

    ze_device_mem_alloc_desc_t device_desc;
    device_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
    device_desc.pNext = nullptr;
    device_desc.ordinal = 0;

    const size_t alignment = 64;
    void* mem = nullptr;

    WRAP_LEVEL_ZERO(zeMemAllocDevice(devices_[dev].ctx, &device_desc, size, alignment, devices_[dev].device, &mem));

    if (mem == nullptr)
        error("zeMemAllocDevice() failed for Level Zero device %", dev);

    return mem;
}

void* LevelZeroPlatform::alloc_host(DeviceId dev, int64_t size) {
    if (!size) return nullptr;

    ze_host_mem_alloc_desc_t host_desc;
    host_desc.stype = ZE_STRUCTURE_TYPE_HOST_MEM_ALLOC_DESC;
    host_desc.pNext = nullptr;

    const size_t alignment = 64;
    void* mem = nullptr;

    WRAP_LEVEL_ZERO(zeMemAllocHost(devices_[dev].ctx, &host_desc, size, alignment, &mem));

    if (mem == nullptr)
        error("zeMemAllocHost() failed for Level Zero device %", dev);

    return mem;
}

void* LevelZeroPlatform::alloc_unified(DeviceId dev, int64_t size) {
    if (!size) return nullptr;

    ze_device_mem_alloc_desc_t device_desc;
    device_desc.stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC;
    device_desc.pNext = nullptr;
    device_desc.ordinal = 0;
    ze_host_mem_alloc_desc_t host_desc;
    host_desc.stype = ZE_STRUCTURE_TYPE_HOST_MEM_ALLOC_DESC;

    const size_t alignment = 64;
    void* mem = nullptr;

    WRAP_LEVEL_ZERO(zeMemAllocShared(devices_[dev].ctx, &device_desc, &host_desc, size, alignment, nullptr, &mem));

    if (mem == nullptr)
        error("zeMemAllocShared() failed for Level Zero device %", dev);

    return mem;
}

void LevelZeroPlatform::release(DeviceId dev, void* ptr) {
#ifndef NDEBUG
    ze_memory_allocation_properties_t props;
    props.stype = ZE_STRUCTURE_TYPE_MEMORY_ALLOCATION_PROPERTIES;
    props.pNext = nullptr;
    WRAP_LEVEL_ZERO(zeMemGetAllocProperties(devices_[dev].ctx, ptr, &props, &devices_[dev].device));
    assert(props.type == ZE_MEMORY_TYPE_DEVICE || props.type == ZE_MEMORY_TYPE_SHARED);
#endif // !NDEBUG
    WRAP_LEVEL_ZERO(zeMemFree(devices_[dev].ctx, ptr));
}

void LevelZeroPlatform::release_host(DeviceId dev, void* ptr) {
#ifndef NDEBUG
    ze_memory_allocation_properties_t props;
    props.stype = ZE_STRUCTURE_TYPE_MEMORY_ALLOCATION_PROPERTIES;
    props.pNext = nullptr;
    WRAP_LEVEL_ZERO(zeMemGetAllocProperties(devices_[dev].ctx, ptr, &props, &devices_[dev].device));
    assert(props.type == ZE_MEMORY_TYPE_HOST);
#endif // !NDEBUG
    WRAP_LEVEL_ZERO(zeMemFree(devices_[dev].ctx, ptr));
}

void LevelZeroPlatform::launch_kernel(DeviceId dev, const LaunchParams& launch_params) {

    DeviceData& ze_dev = devices_[dev];

    ze_kernel_handle_t hKernel = load_kernel(dev, launch_params.file_name, launch_params.kernel_name);

    // set up arguments
    for (uint32_t argIdx = 0; argIdx < launch_params.num_args; ++argIdx) {
        WRAP_LEVEL_ZERO(zeKernelSetArgumentValue(hKernel, argIdx, launch_params.args.sizes[argIdx], launch_params.args.data[argIdx]));
    }

    WRAP_LEVEL_ZERO(zeKernelSetGroupSize(hKernel, launch_params.block[0] , launch_params.block[1] , launch_params.block[2]));

    ze_group_count_t launchArgs;
    launchArgs.groupCountX = launch_params.grid[0] / launch_params.block[0];
    launchArgs.groupCountY = launch_params.grid[1] / launch_params.block[1];
    launchArgs.groupCountZ = launch_params.grid[2] / launch_params.block[2];

    // TODO: create persistent eventpool per device
    ze_event_pool_handle_t eventPool;
    ze_event_pool_desc_t eventPoolDesc;
    eventPoolDesc.stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC;
    eventPoolDesc.pNext = nullptr;
    eventPoolDesc.count = 1;
    eventPoolDesc.flags = ZE_EVENT_POOL_FLAG_KERNEL_TIMESTAMP;
    WRAP_LEVEL_ZERO(zeEventPoolCreate(ze_dev.ctx, &eventPoolDesc, 1, &ze_dev.device, &eventPool));

    ze_event_handle_t kernelTsEvent = nullptr;

    if (runtime_->profiling_enabled()) {
        ze_event_desc_t eventDesc;
        eventDesc.stype = ZE_STRUCTURE_TYPE_EVENT_DESC;
        eventDesc.pNext = nullptr;
        eventDesc.index = 0;
        eventDesc.signal = ZE_EVENT_SCOPE_FLAG_HOST;
        eventDesc.wait = ZE_EVENT_SCOPE_FLAG_HOST;
        WRAP_LEVEL_ZERO(zeEventCreate(eventPool, &eventDesc, &kernelTsEvent));
    }

    WRAP_LEVEL_ZERO(zeCommandListAppendLaunchKernel(ze_dev.queue, hKernel, &launchArgs, kernelTsEvent, 0, nullptr));

    if (runtime_->profiling_enabled()) {
        // TODO: handle kernel time aggregation on device to avoid host sync

        //WRAP_LEVEL_ZERO(zeCommandListAppendBarrier(ze_dev.queue, nullptr, 0u, nullptr));
        //WRAP_LEVEL_ZERO(zeCommandListAppendWaitOnEvents(ze_dev.queue, 1, &kernelTsEvent));
        WRAP_LEVEL_ZERO(zeEventHostSynchronize(kernelTsEvent, UINT32_MAX));

        ze_kernel_timestamp_result_t kernelTsResults;
        WRAP_LEVEL_ZERO(zeEventQueryKernelTimestamp(kernelTsEvent, &kernelTsResults));

        WRAP_LEVEL_ZERO(zeEventDestroy(kernelTsEvent));

        uint64_t kernelDuration = kernelTsResults.context.kernelEnd - kernelTsResults.context.kernelStart;
        debug("Kernel timestamp statistics on device %:", dev);
        debug("  Launch configuration: (%, %, %) / (%, %, %)",
            launch_params.block[0], launch_params.block[1], launch_params.block[2],
            launch_params.grid[0], launch_params.grid[1], launch_params.grid[2]);
        debug("  Global start: % cycles", kernelTsResults.global.kernelStart);
        debug("  Kernel start: % cycles", kernelTsResults.context.kernelStart);
        debug("  Kernel end:   % cycles", kernelTsResults.context.kernelEnd);
        debug("  Global end:   % cycles", kernelTsResults.global.kernelEnd);
        debug("  timerResolution clock: % ns", ze_dev.timerResolution);
        debug("  Kernel duration : % cycles, % ns", kernelDuration, uint64_t(kernelDuration * ze_dev.timerResolution));

        uint64_t kernelTime = kernelDuration * ze_dev.timerResolution / 1000.0;
        runtime_->kernel_time().fetch_add(kernelTime);
    }

    WRAP_LEVEL_ZERO(zeEventPoolDestroy(eventPool));
}

void LevelZeroPlatform::synchronize(DeviceId dev) {
    WRAP_LEVEL_ZERO(zeCommandListHostSynchronize(devices_[dev].queue, UINT64_MAX));
}

void LevelZeroPlatform::copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
    assert(dev_src == dev_dst);
    unused(dev_dst);

    //assert(offset_src == 0 && offset_dst == 0);

    DeviceData& dev = devices_[dev_src];
    const uint8_t* src_ptr = static_cast<const uint8_t*>(src) + offset_src;
    uint8_t*       dst_ptr = static_cast<uint8_t*>(dst) + offset_dst;

    //ze_event_handle_t hSignalEvent;
    // we use an immediate commandlist that is supposed to block syncronously
    WRAP_LEVEL_ZERO(zeCommandListAppendMemoryCopy(dev.queue, dst_ptr, src_ptr, size, nullptr, 0, nullptr));
}

void LevelZeroPlatform::copy_from_host(const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
    //assert(offset_src == 0 && offset_dst == 0);

    DeviceData& dev = devices_[dev_dst];
    const uint8_t* src_ptr = static_cast<const uint8_t*>(src) + offset_src;
    uint8_t*       dst_ptr = static_cast<uint8_t*>(dst)       + offset_dst;

    //ze_event_handle_t hSignalEvent;
    // we use an immediate commandlist that is supposed to block syncronously
    WRAP_LEVEL_ZERO(zeCommandListAppendMemoryCopy(dev.queue, dst_ptr, src_ptr, size, nullptr, 0, nullptr));
}

void LevelZeroPlatform::copy_to_host(DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) {
    //assert(offset_src == 0 && offset_dst == 0);

    DeviceData& dev = devices_[dev_src];
    const uint8_t* src_ptr = static_cast<const uint8_t*>(src) + offset_src;
    uint8_t*       dst_ptr = static_cast<uint8_t*>(dst)       + offset_dst;

    //ze_event_handle_t hSignalEvent;
    // we use an immediate commandlist that is supposed to block syncronously
    WRAP_LEVEL_ZERO(zeCommandListAppendMemoryCopy(dev.queue, dst_ptr, src_ptr, size, nullptr, 0, nullptr));
    // unfortunately the immediate command list guarantees only the order of commands
    // whenever we read back memory, we have to explicitly sync on device level or use signaling events later
    synchronize(dev_src);
}

ze_kernel_handle_t LevelZeroPlatform::load_kernel(DeviceId dev, const std::string& filename, const std::string& kernelname) {
    DeviceData& ze_dev = devices_[dev];

    ze_module_handle_t hModule = nullptr;
    ze_kernel_handle_t hKernel = nullptr;

    std::filesystem::path canonical = std::filesystem::weakly_canonical(filename);
    auto& module_cache = ze_dev.modules;
    auto mod_it = module_cache.find(canonical.string());
    if (mod_it == module_cache.end()) {
        if (canonical.extension() != ".spv")
            error("Incorrect extension for module file '%' (should be '.spv')", canonical.string());

        // load file from disk or cache
        auto src_path = canonical;
        std::string mod_src = runtime_->load_file(src_path.string());

        ze_module_desc_t mod_desc = {};
        mod_desc.stype = ZE_STRUCTURE_TYPE_MODULE_DESC;
        mod_desc.pNext = nullptr;
        mod_desc.format = ZE_MODULE_FORMAT_IL_SPIRV;
        mod_desc.inputSize = mod_src.length();
        mod_desc.pBuildFlags = nullptr;
        mod_desc.pConstants = nullptr;
        mod_desc.pInputModule = reinterpret_cast<const uint8_t*>(mod_src.c_str());

        ze_module_build_log_handle_t hBuildLog;

        WRAP_LEVEL_ZERO_HANDLER(
            zeModuleCreate(ze_dev.ctx, ze_dev.device, &mod_desc, &hModule, &hBuildLog),
            if (err == ZE_RESULT_ERROR_MODULE_BUILD_FAILURE) {
                size_t blSize;
                std::string buildlog;
                WRAP_LEVEL_ZERO(zeModuleBuildLogGetString(hBuildLog, &blSize, nullptr));
                buildlog.resize(blSize);
                WRAP_LEVEL_ZERO(zeModuleBuildLogGetString(hBuildLog, &blSize, buildlog.data()));
                debug("Build log of Level Zero module %\n%", filename, buildlog);
            }
        );
        WRAP_LEVEL_ZERO(zeModuleBuildLogDestroy(hBuildLog));

        module_cache[canonical.string()] = hModule;
    } else {
        hModule = mod_it->second;
    }

    assert(hModule != nullptr);

    // checks that the kernel exists
    auto& kernel_cache = ze_dev.kernels;
    auto& kernel_map = kernel_cache[hModule];
    auto kernel_it = kernel_map.find(kernelname);
    if (kernel_it == kernel_map.end()) {
        ze_kernel_desc_t kernel_desc = {};
        kernel_desc.stype = ZE_STRUCTURE_TYPE_KERNEL_DESC;
        kernel_desc.pNext = nullptr;
        kernel_desc.pKernelName = kernelname.c_str();

        WRAP_LEVEL_ZERO(zeKernelCreate(hModule, &kernel_desc, &hKernel));

        kernel_cache[hModule].emplace(kernelname, hKernel);
    } else {
        hKernel = kernel_it->second;
    }

    return hKernel;
}

const char* LevelZeroPlatform::device_name(DeviceId dev) const {
    return devices_[dev].device_name.c_str();
}

void register_levelzero_platform(Runtime* runtime) {
    runtime->register_platform<LevelZeroPlatform>();
}
