#include "vulkan_platform.h"

const auto khr_validation = "VK_LAYER_KHRONOS_validation";

#define CHECK(stuff) { \
    auto rslt = stuff; \
    if (rslt != VK_SUCCESS) \
        error("error, failed %", #stuff); \
}

inline std::vector<VkLayerProperties> query_layers_available() {
    uint32_t count;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    return layers;
}

inline std::vector<VkExtensionProperties> query_extensions_available() {
    uint32_t count;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
    return exts;
}

VulkanPlatform::VulkanPlatform(Runtime* runtime) : Platform(runtime) {
    auto available_layers = query_layers_available();
    auto available_instance_extensions = query_extensions_available();

    std::vector<const char*> enabled_layers;
    std::vector<const char*> enabled_instance_extensions {
        "VK_KHR_external_memory_capabilities"
    };

    bool should_enable_validation = true;
#ifdef NDEBUG
    should_enable_validation = false;
#endif
    if (should_enable_validation) {
        for (auto& layer : available_layers) {
            if (strcmp(khr_validation, layer.layerName) == 0) {
                enabled_layers.push_back(khr_validation);
                goto validation_done;
            }
        }
        info("Warning: validation enabled but layers not present");
    }
    validation_done:

    auto app_info = VkApplicationInfo {
        .pApplicationName = "AnyDSL Runtime",
        .apiVersion = VK_API_VERSION_1_2,
    };
    auto create_info = VkInstanceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = nullptr,
        .pApplicationInfo = &app_info,
        .enabledLayerCount = (uint32_t) enabled_layers.size(),
        .ppEnabledLayerNames = enabled_layers.data(),
        .enabledExtensionCount = (uint32_t) enabled_instance_extensions.size(),
        .ppEnabledExtensionNames = enabled_instance_extensions.data(),
    };
    vkCreateInstance(&create_info, nullptr, &instance);

    uint32_t physical_devices_count;
    vkEnumeratePhysicalDevices(instance, &physical_devices_count, nullptr);
    physical_devices.resize(physical_devices_count);
    vkEnumeratePhysicalDevices(instance, &physical_devices_count, physical_devices.data());

    debug("Available Vulkan physical devices: ");
    size_t i = 0;
    for (auto& dev : physical_devices) {
        usable_devices.emplace_back(std::make_unique<Device>(*this, dev, i));
        i++;
    }
    debug("Vulkan platform successfully initialized");
}

VulkanPlatform::~VulkanPlatform() {
    usable_devices.clear();
    vkDestroyInstance(instance, nullptr);
}

VulkanPlatform::Device::Device(VulkanPlatform& platform, VkPhysicalDevice physical_device, size_t device_id)
 : platform(platform), physical_device(physical_device), device_id(device_id) {
    auto external_memory_host_properties = VkPhysicalDeviceExternalMemoryHostPropertiesEXT {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT,
        .pNext = nullptr,
        .minImportedHostPointerAlignment = 0xDEADBEEF,
    };
    auto device_properties2 = VkPhysicalDeviceProperties2 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        .pNext = &external_memory_host_properties,
    };
    vkGetPhysicalDeviceProperties2(physical_device, &device_properties2);
    auto& device_properties = device_properties2.properties;

    debug("  GPU%:", device_id);
    debug("  Device name: %", device_properties.deviceName);
    debug("  Vulkan version %.%.%", VK_VERSION_MAJOR(device_properties.apiVersion), VK_VERSION_MINOR(device_properties.apiVersion), VK_VERSION_PATCH(device_properties.apiVersion));

    min_imported_host_ptr_alignment = external_memory_host_properties.minImportedHostPointerAlignment;
    debug("  Min imported host ptr alignment: %", min_imported_host_ptr_alignment);
    if (min_imported_host_ptr_alignment == 0xDEADBEEF)
        error("Device does not report minimum host pointer alignment");

    uint32_t exts_count;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &exts_count, nullptr);
    std::vector<VkExtensionProperties> available_device_extensions(exts_count);
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &exts_count, available_device_extensions.data());
    std::vector<const char*> enabled_instance_extensions {
        "VK_EXT_external_memory_host"
    };

    uint32_t queue_families_count;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_families_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_families_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_families_count, queue_families.data());
    int compute_queue_family = -1;
    int q = 0;
    for (auto& queue_f : queue_families) {
        bool has_gfx       = (queue_f.queueFlags & 0x00000001) != 0;
        bool has_compute   = (queue_f.queueFlags & 0x00000002) != 0;
        bool has_xfer      = (queue_f.queueFlags & 0x00000004) != 0;
        bool has_sparse    = (queue_f.queueFlags & 0x00000008) != 0;
        bool has_protected = (queue_f.queueFlags & 0x00000010) != 0;

        // TODO perform this intelligently
        if (compute_queue_family == -1 && has_compute)
            compute_queue_family = q;
        q++;
    }
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    float one = 1.0f;
    if (compute_queue_family != -1) {
        queue_create_infos.push_back(VkDeviceQueueCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = (uint32_t) compute_queue_family,
            .queueCount = 1,
            .pQueuePriorities = &one
        });
    } else {
        assert(false && "unsuitable device");
    }

    auto enabled_features = VkPhysicalDeviceFeatures {};

    auto device_create_info = VkDeviceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .queueCreateInfoCount = (uint32_t) queue_create_infos.size(),
        .pQueueCreateInfos = queue_create_infos.data(),
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = (uint32_t) enabled_instance_extensions.size(),
        .ppEnabledExtensionNames = enabled_instance_extensions.data(),
        .pEnabledFeatures = &enabled_features
    };
    CHECK(vkCreateDevice(physical_device, &device_create_info, nullptr, &device));
    vkGetDeviceQueue(device, compute_queue_family, 0, &queue);

    auto cmd_pool_create_info = VkCommandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = (uint32_t) compute_queue_family,
    };
    CHECK(vkCreateCommandPool(device, &cmd_pool_create_info, nullptr, &cmd_pool));

    // Load function pointers
#define f(s) extension_fns.s = (PFN_##s) vkGetDeviceProcAddr(device, #s);
    DevicesExtensionsFunctions(f)
#undef f
}

VulkanPlatform::Device::~Device() {
    vkDestroyCommandPool(device, cmd_pool, nullptr);
    kernels.clear();
    if (device != nullptr)
        vkDestroyDevice(device, nullptr);
}

uint32_t VulkanPlatform::Device::find_suitable_memory_type(uint32_t memory_type_bits) {
    VkPhysicalDeviceMemoryProperties device_memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &device_memory_properties);
    for (size_t bit = 0; bit < 32; bit++) {
        auto& memory_type = device_memory_properties.memoryTypes[bit];
        auto& memory_heap = device_memory_properties.memoryHeaps[memory_type.heapIndex];

        bool is_device_local = (memory_type.propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;

        if ((memory_type_bits & (1 << bit)) != 0) {
            if (is_device_local)
                return bit;
        }
    }
    assert(false && "Unable to find a suitable memory type");
}

void* VulkanPlatform::alloc(DeviceId dev, int64_t size) {
    auto& device = usable_devices[dev];

    auto buffer_create_info = VkBufferCreateInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = (VkDeviceSize) size,
        .usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    VkBuffer buffer;
    vkCreateBuffer(device->device, &buffer_create_info, nullptr, &buffer);

    VkMemoryRequirements memory_requirements;
    vkGetBufferMemoryRequirements(device->device, buffer, &memory_requirements);

    auto allocation_info = VkMemoryAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = nullptr,
        .allocationSize = (VkDeviceSize) memory_requirements.size, // the driver might want padding !
        .memoryTypeIndex = device->find_suitable_memory_type(memory_requirements.memoryTypeBits),
    };
    VkDeviceMemory memory;
    vkAllocateMemory(device->device, &allocation_info, nullptr, &memory);

    vkBindBufferMemory(device->device, buffer, memory, 0);
    size_t id = device->next_resource_id++;

    std::unique_ptr<Buffer> res_buffer = std::make_unique<Buffer>(*device);
    res_buffer->alloc = memory;
    res_buffer->id = id;
    res_buffer->buffer = buffer;
    device->resources.push_back(std::move(res_buffer));

    return reinterpret_cast<void*>(id);
}

void* VulkanPlatform::alloc_host(DeviceId dev, int64_t size) {
    command_unavailable("alloc_host");
}

void* VulkanPlatform::get_device_ptr(DeviceId dev, void *ptr) {
    command_unavailable("get_device_ptr");
}

VulkanPlatform::Resource* VulkanPlatform::Device::find_resource_by_id(size_t id) {
    size_t i = 0;
    for (auto& resource : resources) {
        if (resource->id == id) {
            return resources[i].get();
        }
        i++;
    }
    return nullptr;
}

void VulkanPlatform::release(DeviceId dev, void *ptr) {
    if (ptr == nullptr)
        return;

    auto& device = usable_devices[dev];
    size_t id = reinterpret_cast<size_t>(ptr);
    size_t i = 0;
    for (auto& resource : device->resources) {
        if (resource->id == id) {
            device->resources.erase(device->resources.begin() + i);
            return;
        }
        i++;
    }
    assert(false && "Could not find such a buffer to release");
}

void VulkanPlatform::release_host(DeviceId dev, void *ptr) {
    command_unavailable("release_host");
}

VulkanPlatform::Kernel *VulkanPlatform::Device::load_kernel(const std::string& filename) {
    auto ki = kernels.find(filename);
    if (ki == kernels.end()) {
        auto [i,b] = kernels.emplace(filename, Kernel(*this));
        Kernel& kernel = i->second;

        std::string bin = platform.runtime_->load_file(filename);
        auto shader_module_create_info = VkShaderModuleCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = bin.size(),
            .pCode = reinterpret_cast<const uint32_t *>(bin.c_str()),
        };
        vkCreateShaderModule(device, &shader_module_create_info, nullptr, &kernel.shader_module);

        auto stage = VkPipelineShaderStageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = kernel.shader_module,
            .pName = "kernel_main",
            .pSpecializationInfo = nullptr,
        };

        std::vector<VkPushConstantRange> push_constants {
            VkPushConstantRange {
                .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                .offset = 0,
                .size = 128
            }
        };
        auto layout_create_info = VkPipelineLayoutCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .setLayoutCount = 0,
            .pSetLayouts = nullptr,
            .pushConstantRangeCount = (uint32_t) push_constants.size(),
            .pPushConstantRanges = push_constants.data(),
        };
        vkCreatePipelineLayout(device, &layout_create_info, nullptr, &kernel.layout);

        auto compute_pipeline_create_info = VkComputePipelineCreateInfo {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = stage,
            .layout = kernel.layout,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0,
        };
        CHECK(vkCreateComputePipelines(device, nullptr, 1, &compute_pipeline_create_info, nullptr, &kernel.pipeline));
        return &kernel;
    }

    return &ki->second;
}

void VulkanPlatform::launch_kernel(DeviceId dev, const LaunchParams &launch_params) {
    auto& device = usable_devices[dev];
    auto kernel = device->load_kernel(launch_params.file_name);

    device->execute_command_buffer_oneshot([&](VkCommandBuffer cmd_buf) {
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, kernel->pipeline);
        std::array<char, 128> push_constants {};
        vkCmdPushConstants(cmd_buf, kernel->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 128, &push_constants);
        vkCmdDispatch(cmd_buf, launch_params.grid[0], launch_params.grid[1], launch_params.grid[2]);
    });
}

void VulkanPlatform::synchronize(DeviceId dev) {
    // TODO: don't wait for idle everywhere
}

VkDeviceMemory VulkanPlatform::Device::import_host_memory(void *ptr, size_t size) {
    VkExternalMemoryHandleTypeFlagBits handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;

    // Align stuff
    size_t mask = !(min_imported_host_ptr_alignment - 1);
    size_t host_ptr = (size_t)ptr;
    size_t aligned_host_ptr = host_ptr & mask;

    size_t end = host_ptr + size;
    size_t aligned_end = ((end + min_imported_host_ptr_alignment - 1) / min_imported_host_ptr_alignment) * min_imported_host_ptr_alignment;
    size_t aligned_size = aligned_end - aligned_host_ptr;

    // Find the corresponding device memory type index
    VkMemoryHostPointerPropertiesEXT host_ptr_properties {
        .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT,
    };
    extension_fns.vkGetMemoryHostPointerPropertiesEXT(device, handle_type, (void*)aligned_host_ptr, &host_ptr_properties);
    uint32_t memory_type = find_suitable_memory_type(host_ptr_properties.memoryTypeBits);

    // Import memory
    auto import_ptr_info = VkImportMemoryHostPointerInfoEXT {
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
            .pNext = nullptr,
            .handleType = handle_type,
            .pHostPointer = (void*) aligned_host_ptr,
    };
    auto allocation_info = VkMemoryAllocateInfo {
            .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
            .pNext = &import_ptr_info,
            .allocationSize = (VkDeviceSize) aligned_size,
            .memoryTypeIndex = memory_type
    };
    VkDeviceMemory imported_memory;
    CHECK(vkAllocateMemory(device, &allocation_info, nullptr, &imported_memory));
    return imported_memory;
}

VkCommandBuffer VulkanPlatform::Device::obtain_command_buffer() {
    if (spare_cmd_bufs.size() > 0) {
        VkCommandBuffer cmd_buf = spare_cmd_bufs.back();
        spare_cmd_bufs.pop_back();
        return cmd_buf;
    }
    auto cmd_buf_create_info = VkCommandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = cmd_pool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkCommandBuffer cmd_buf;
    vkAllocateCommandBuffers(device, &cmd_buf_create_info, &cmd_buf);
    return cmd_buf;
}

void VulkanPlatform::Device::return_command_buffer(VkCommandBuffer cmd_buf) {
    vkResetCommandBuffer(cmd_buf, 0);
    spare_cmd_bufs.push_back(cmd_buf);
}

void VulkanPlatform::Device::execute_command_buffer_oneshot(std::function<void(VkCommandBuffer)> fn) {
    VkCommandBuffer cmd_buf = obtain_command_buffer();
    auto begin_command_buffer_info = VkCommandBufferBeginInfo {
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .pNext = nullptr,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
            .pInheritanceInfo = nullptr,
    };
    vkBeginCommandBuffer(cmd_buf, &begin_command_buffer_info);
    fn(cmd_buf);
    vkEndCommandBuffer(cmd_buf);
    auto submit_info = VkSubmitInfo {
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .pNext = nullptr,
            .waitSemaphoreCount = 0,
            .pWaitSemaphores = nullptr,
            .pWaitDstStageMask = nullptr,
            .commandBufferCount = 1,
            .pCommandBuffers = &cmd_buf,
            .signalSemaphoreCount = 0,
            .pSignalSemaphores = nullptr,
    };
    vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE);
    vkDeviceWaitIdle(device);
    return_command_buffer(cmd_buf);
}

void VulkanPlatform::copy(DeviceId dev_src, const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst, int64_t size) {

}

void VulkanPlatform::copy_from_host(const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst, int64_t size) {
    auto& device = usable_devices[dev_dst];
    auto dst_buffer_resource = (Buffer*) device->find_resource_by_id((size_t) dst);
    auto dst_buffer = dst_buffer_resource->buffer;

    // Import host memory and wrap it in a buffer
    size_t host_ptr = (size_t)src + offset_src;
    VkDeviceMemory imported_memory = device->import_host_memory((void*)host_ptr, size);
    auto tmp_buffer_create_info = VkBufferCreateInfo {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = (VkDeviceSize) size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };
    VkBuffer tmp_buffer;
    vkCreateBuffer(device->device, &tmp_buffer_create_info, nullptr, &tmp_buffer);
    vkBindBufferMemory(device->device, tmp_buffer, imported_memory, 0);

    device->execute_command_buffer_oneshot([&](VkCommandBuffer cmd_buf) {
        VkBufferCopy copy_region {
                .srcOffset = 0,
                .dstOffset = (VkDeviceSize) offset_dst,
                .size = (VkDeviceSize) size,
        };
        vkCmdCopyBuffer(cmd_buf, tmp_buffer, dst_buffer, 1, &copy_region);
    });

    // Cleanup
    vkFreeMemory(device->device, imported_memory, nullptr);
    vkDestroyBuffer(device->device, tmp_buffer, nullptr);
}

void VulkanPlatform::copy_to_host(DeviceId dev_src, const void *src, int64_t offset_src, void *dst, int64_t offset_dst, int64_t size) {

}

void register_vulkan_platform(Runtime* runtime) {
    runtime->register_platform<VulkanPlatform>();
}

VulkanPlatform::Resource::~Resource() {
    vkFreeMemory(device.device, alloc, nullptr);
}

VulkanPlatform::Buffer::~Buffer() {
    vkDestroyBuffer(device.device, buffer, nullptr);
}

VulkanPlatform::Kernel::~Kernel() {
    vkDestroyPipeline(device.device, pipeline, nullptr);
    vkDestroyPipelineLayout(device.device, layout, nullptr);
    vkDestroyShaderModule(device.device, shader_module, nullptr);
}
