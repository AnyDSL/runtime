#include "vulkan_platform.h"

const auto khr_validation = "VK_LAYER_KHRONOS_validation";

#define CHECK(stuff) { \
    auto rslt = stuff; \
    if (rslt != VK_SUCCESS) \
        error("error, failed %", #stuff); \
}

template<typename T, typename U>
void insert_pnext(T& base, U& append) {
    assert(base.pNext == nullptr);
    append.pNext = base.pNext;
    base.pNext = &append;
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

inline bool is_ext_available(std::vector<VkExtensionProperties>& ext_props, std::string ext_name) {
    for (auto& ext : ext_props) {
        if (strcmp(ext.extensionName, ext_name.c_str()) == 0)
            return true;
    }
    return false;
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
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
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
    uint32_t exts_count;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &exts_count, nullptr);
    std::vector<VkExtensionProperties> available_device_extensions(exts_count);
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &exts_count, available_device_extensions.data());

    std::vector<const char*> enabled_device_extensions {
        "VK_KHR_buffer_device_address",
        "VK_KHR_shader_non_semantic_info"
    };

    // Use this to import host memory as GPU-visible memory, otherwise use a fallback path that copies when uploading/downloading
    if (is_ext_available(available_device_extensions, "VK_EXT_external_memory_host")) {
        enabled_device_extensions.push_back("VK_EXT_external_memory_host");
        insert_pnext(properties, external_memory_host_properties);
        can_import_host_memory = true;
    }

    vkGetPhysicalDeviceProperties2(physical_device, &properties);
    auto& device_properties = properties.properties;

    debug("  GPU%:", device_id);
    debug("  Device name: %", device_properties.deviceName);
    debug("  Vulkan version %.%.%", VK_VERSION_MAJOR(device_properties.apiVersion), VK_VERSION_MINOR(device_properties.apiVersion), VK_VERSION_PATCH(device_properties.apiVersion));

    if (can_import_host_memory) {
        debug("  Min imported host ptr alignment: %", external_memory_host_properties.minImportedHostPointerAlignment);
        if (external_memory_host_properties.minImportedHostPointerAlignment == 0xFFFFFFFF)
            error("Device does not report minimum host pointer alignment");
    }

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

    auto bda_features = VkPhysicalDeviceBufferDeviceAddressFeaturesKHR {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR,
        .pNext = nullptr,
        .bufferDeviceAddress = true,
    };
    auto vk11_features = VkPhysicalDeviceVulkan11Features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &bda_features,
        .variablePointersStorageBuffer = true,
        .variablePointers = true,
    };
    auto enabled_features = VkPhysicalDeviceFeatures2 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &vk11_features,
        .features = {
            .vertexPipelineStoresAndAtomics = true,
            .fragmentStoresAndAtomics = true,
            .shaderInt64 = true,
            // .shaderInt16 = true,
        }
    };

    auto device_create_info = VkDeviceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = &enabled_features,
        .flags = 0,
        .queueCreateInfoCount = (uint32_t) queue_create_infos.size(),
        .pQueueCreateInfos = queue_create_infos.data(),
        .enabledLayerCount = 0,
        .ppEnabledLayerNames = nullptr,
        .enabledExtensionCount = (uint32_t) enabled_device_extensions.size(),
        .ppEnabledExtensionNames = enabled_device_extensions.data(),
        .pEnabledFeatures = nullptr // controlled via VkPhysicalDeviceFeatures2
    };
    CHECK(vkCreateDevice(physical_device, &device_create_info, nullptr, &handle_));
    vkGetDeviceQueue(handle_, compute_queue_family, 0, &queue);

    auto cmd_pool_create_info = VkCommandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = (uint32_t) compute_queue_family,
    };
    CHECK(vkCreateCommandPool(handle_, &cmd_pool_create_info, nullptr, &cmd_pool));

    // Load function pointers
#define f(s) extension_fns.s = (PFN_##s) vkGetDeviceProcAddr(handle_, #s);
    DevicesExtensionsFunctions(f)
#undef f
}

VulkanPlatform::Device::~Device() {
    vkDestroyCommandPool(handle_, cmd_pool, nullptr);
    kernels.clear();
    //if (!resources.empty()) {
    //    info("Some vulkan resources were not released. Releasing those automatically...");
    //    resources.clear();
    //}
    vkDestroyDevice(handle_, nullptr);
}

uint32_t VulkanPlatform::Device::find_suitable_memory_type(uint32_t memory_type_bits, VkMemoryPropertyFlags memory_flags, VkMemoryHeapFlags heap_flags) {
    VkPhysicalDeviceMemoryProperties device_memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device, &device_memory_properties);
    for (size_t bit = 0; bit < 32; bit++) {
        auto& memory_type = device_memory_properties.memoryTypes[bit];
        auto& memory_heap = device_memory_properties.memoryHeaps[memory_type.heapIndex];

        if ((memory_type_bits & (1 << bit)) != 0) {
            if ((memory_type.propertyFlags & memory_flags) == memory_flags && (memory_heap.flags & heap_flags) == heap_flags)
                return bit;
        }
    }
    assert(false && "Unable to find a suitable memory type");
}

VkDeviceMemory VulkanPlatform::Device::allocate_memory(VkDeviceSize size, uint32_t memory_type_bits, VkMemoryPropertyFlags memory_flags, VkMemoryHeapFlags heap_flags) {
    auto allocate_flags = VkMemoryAllocateFlagsInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .pNext = nullptr,
        .flags = VK_MEMORY_ALLOCATE_DEVICE_ADDRESS_BIT_KHR,
        .deviceMask = 0
    };

    auto allocation_info = VkMemoryAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &allocate_flags,
        .allocationSize = size, // the driver might want padding !
        .memoryTypeIndex = find_suitable_memory_type(memory_type_bits, memory_flags, heap_flags),
    };
    VkDeviceMemory memory;
    vkAllocateMemory(handle_, &allocation_info, nullptr, &memory);

    return memory;
}

VulkanPlatform::Buffer::Buffer(Device& device, size_t size, BackingStorage backing, VkBufferUsageFlags2 usage) : Resource(device) {
    VkBufferCreateInfo buffer_create_info {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .size = (VkDeviceSize) size,
        .usage = static_cast<VkBufferUsageFlags>(usage),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .queueFamilyIndexCount = 0,
        .pQueueFamilyIndices = nullptr,
    };

    auto create_buffer = [&]() { vkCreateBuffer(device.handle_, &buffer_create_info, nullptr, &handle_); };

    if (const auto* import_host = std::get_if<ImportedHostMemory>(&backing)) {
        VkExternalMemoryBufferCreateInfo external_mem_buffer_create_info {
            .sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_BUFFER_CREATE_INFO,
            .pNext = nullptr,
            .handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT
        };
        insert_pnext(buffer_create_info, external_mem_buffer_create_info);
        create_buffer();

        size_t imported_offset;
        std::tie(device_memory_, imported_offset) = device.import_host_memory(import_host->host_memory_, size);
        vkBindBufferMemory(device.handle_, handle_, device_memory_, imported_offset);
    } else if (std::get_if<DeviceMemory>(&backing)) {
        create_buffer();
        VkMemoryRequirements memory_requirements;
        vkGetBufferMemoryRequirements(device.handle_, handle_, &memory_requirements);
        device_memory_ = device.allocate_memory(memory_requirements.size, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        vkBindBufferMemory(device.handle_, handle_, device_memory_, 0);
    } else if (std::get_if<HostMemory>(&backing)) {
        create_buffer();
        VkMemoryRequirements memory_requirements;
        vkGetBufferMemoryRequirements(device.handle_, handle_, &memory_requirements);
        device_memory_ = device.allocate_memory(memory_requirements.size, memory_requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        vkBindBufferMemory(device.handle_, handle_, device_memory_, 0);
    } else {
        abort();
    }

    if (usage & VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT) {
        VkBufferDeviceAddressInfoKHR bda_info{
            .sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO_KHR,
            .pNext = nullptr,
            .buffer = handle_
        };
        device_address_ = device.extension_fns.vkGetBufferDeviceAddressKHR(device.handle_, &bda_info);
        assert(device_address_ != 0 && "vkGetBufferDeviceAddress failed");
    }
}

uint64_t VulkanPlatform::Device::create_buffer_resource(size_t size, Buffer::BackingStorage backing, VkBufferUsageFlags usage) {
    std::unique_ptr<Buffer> buffer = std::make_unique<Buffer>(*this, size, backing, usage);

    assert(buffer->device_address_);
    auto& b = *(buffers_[buffer->device_address_] = std::move(buffer));

    return b.device_address_;
}

void* VulkanPlatform::alloc(DeviceId dev, int64_t size) {
    auto& device = usable_devices[dev];
    return reinterpret_cast<void*>(device->create_buffer_resource(size, Buffer::DeviceMemory(), Buffer::ALL_BUFFER_USAGE));
}

void* VulkanPlatform::alloc_host(DeviceId dev, int64_t size) {
    auto& device = usable_devices[dev];
    return reinterpret_cast<void*>(device->create_buffer_resource(size, Buffer::HostMemory(), Buffer::ALL_BUFFER_USAGE));
}

void* VulkanPlatform::get_device_ptr(DeviceId dev, void *ptr) {
    command_unavailable("get_device_ptr");
}

void VulkanPlatform::release(DeviceId dev, void *ptr) {
    if (ptr == nullptr)
        return;

    auto& device = usable_devices[dev];
    auto found = device->buffers_.find(reinterpret_cast<VkDeviceAddress>(ptr));

    if (found != device->buffers_.end()) {
        device->buffers_.erase(found);
        return;
    }

    assert(false && "Could not find such a buffer to release");
}

void VulkanPlatform::release_host(DeviceId dev, void *ptr) {
    release(dev, ptr);
}

VulkanPlatform::Buffer::~Buffer() {
    if (device_memory_)
        vkFreeMemory(device_.handle_, device_memory_, nullptr);
    vkDestroyBuffer(device_.handle_, handle_, nullptr);
}

VulkanPlatform::Kernel *VulkanPlatform::Device::load_kernel(const std::string& filename) {
    auto ki = kernels.find(filename);
    if (ki == kernels.end()) {
        auto [i,b] = kernels.emplace(filename, std::make_unique<Kernel>(*this));
        Kernel* kernel = i->second.get();

        std::string bin = platform.runtime_->load_file(filename);
        auto shader_module_create_info = VkShaderModuleCreateInfo {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .codeSize = bin.size(),
            .pCode = reinterpret_cast<const uint32_t *>(bin.c_str()),
        };
        CHECK(vkCreateShaderModule(handle_, &shader_module_create_info, nullptr, &kernel->shader_module));

        auto stage = VkPipelineShaderStageCreateInfo {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = kernel->shader_module,
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
        CHECK(vkCreatePipelineLayout(handle_, &layout_create_info, nullptr, &kernel->    layout));

        auto compute_pipeline_create_info = VkComputePipelineCreateInfo {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .stage = stage,
            .layout = kernel->layout,
            .basePipelineHandle = VK_NULL_HANDLE,
            .basePipelineIndex = 0,
        };
        CHECK(vkCreateComputePipelines(handle_, nullptr, 1, &compute_pipeline_create_info, nullptr, &kernel->pipeline));
        return kernel;
    }

    return ki->second.get();
}

void VulkanPlatform::launch_kernel(DeviceId dev, const LaunchParams &launch_params) {
    auto& device = usable_devices[dev];
    auto kernel = device->load_kernel(launch_params.file_name);

    device->execute_command_buffer_oneshot([&](VkCommandBuffer cmd_buf) {
        vkCmdBindPipeline(cmd_buf, VK_PIPELINE_BIND_POINT_COMPUTE, kernel->pipeline);
        std::array<char, 128> push_constants {};
        size_t offset = 0;
        //for (uint32_t arg = 0; arg < launch_params.num_args; arg++) {
        //    if (launch_params.args.types[arg] == KernelArgType::Val) {
        //        assert(launch_params.args.sizes[arg] == 4 && "Preliminary support...");
        //        memcpy(push_constants.data() + offset, launch_params.args.data[arg], 4);
        //        offset += 4;
        //    } else if (launch_params.args.types[arg] == KernelArgType::Ptr) {
        //        void* buffer = *(void**)launch_params.args.data[arg];
        //        auto dst_buffer_resource = (Buffer*) device->find_buffer_by_device_address((uint64_t) buffer);
        //        uint64_t buffer_bda = dst_buffer_resource->bda;
        //        memcpy(push_constants.data() + offset, &buffer_bda, 8);
        //        offset += 8;
        //    } else {
        //        assert(false && "no struct support yet");
        //    }
        //}
        vkCmdPushConstants(cmd_buf, kernel->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 128, &push_constants);
        vkCmdDispatch(cmd_buf, launch_params.grid[0] / launch_params.block[0], launch_params.grid[1] / launch_params.block[1], launch_params.grid[2] / launch_params.block[2]);
    });
}

void VulkanPlatform::synchronize(DeviceId dev) {
    // TODO: don't wait for idle everywhere
}

VkExternalMemoryHandleTypeFlagBits imported_host_memory_handle_type = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;

std::pair<VkDeviceMemory, size_t> VulkanPlatform::Device::import_host_memory(void *ptr, size_t size) {
    assert(can_import_host_memory && "This device does not support importing host memory");

    size_t alignment = external_memory_host_properties.minImportedHostPointerAlignment;

    // Align stuff
    size_t mask = ~(alignment - 1);
    size_t host_ptr = (size_t)ptr;
    size_t aligned_host_ptr = host_ptr & mask;

    size_t end = host_ptr + size;
    size_t aligned_end = ((end + alignment - 1) / alignment) * alignment;
    size_t aligned_size = aligned_end - aligned_host_ptr;

    // where the memory we wanted to import will actually start
    size_t offset = host_ptr - aligned_host_ptr;

    // Find the corresponding device memory type index
    VkMemoryHostPointerPropertiesEXT host_ptr_properties {
        .sType = VK_STRUCTURE_TYPE_MEMORY_HOST_POINTER_PROPERTIES_EXT,
    };
    CHECK(extension_fns.vkGetMemoryHostPointerPropertiesEXT(handle_, imported_host_memory_handle_type, (void*)aligned_host_ptr, &host_ptr_properties));
    uint32_t memory_type = find_suitable_memory_type(host_ptr_properties.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    // Import memory
    auto import_ptr_info = VkImportMemoryHostPointerInfoEXT {
        .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
        .pNext = nullptr,
        .handleType = imported_host_memory_handle_type,
        .pHostPointer = (void*) aligned_host_ptr,
    };
    auto allocation_info = VkMemoryAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &import_ptr_info,
        .allocationSize = (VkDeviceSize) aligned_size,
        .memoryTypeIndex = memory_type
    };
    VkDeviceMemory imported_memory;
    CHECK(vkAllocateMemory(handle_, &allocation_info, nullptr, &imported_memory));
    return std::make_pair(imported_memory, offset);
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
    CHECK(vkAllocateCommandBuffers(handle_, &cmd_buf_create_info, &cmd_buf));
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
    CHECK(vkBeginCommandBuffer(cmd_buf, &begin_command_buffer_info));
    fn(cmd_buf);
    CHECK(vkEndCommandBuffer(cmd_buf));
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
    CHECK(vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE));
    CHECK(vkDeviceWaitIdle(handle_));
    return_command_buffer(cmd_buf);
}

void VulkanPlatform::copy(DeviceId dev_src, const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst, int64_t size) {
    command_unavailable("copy");
}

void VulkanPlatform::copy_from_host(const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst, int64_t size) {
    auto& device = usable_devices[dev_dst];
    auto dst_buffer = device->get_buffer_by_device_address(reinterpret_cast<VkDeviceAddress>(dst));

    std::unique_ptr<Buffer> tmp_buffer;

    void* host_ptr = (void*)((size_t)src + offset_src);
        // Import host memory and wrap it in a buffer
    if (device->can_import_host_memory) {
        tmp_buffer = std::make_unique<Buffer>(*device, size, Buffer::ImportedHostMemory { host_ptr }, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    } else {
        tmp_buffer = std::make_unique<Buffer>(*device, size, Buffer::HostMemory { }, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        void* mapped = nullptr;
        CHECK(vkMapMemory(device->handle_, tmp_buffer->device_memory_, 0, size, 0, &mapped));
        assert(mapped != nullptr);
        memcpy(mapped, host_ptr, size);
        vkUnmapMemory(device->handle_, tmp_buffer->device_memory_);
    }

    device->execute_command_buffer_oneshot([&](VkCommandBuffer cmd_buf) {
        VkBufferCopy copy_region {
            .srcOffset = 0,
            .dstOffset = (VkDeviceSize) offset_dst,
            .size = (VkDeviceSize) size,
        };
        vkCmdCopyBuffer(cmd_buf, tmp_buffer->handle_, dst_buffer->handle_, 1, &copy_region);
    });
}

void VulkanPlatform::copy_to_host(DeviceId dev_src, const void *src, int64_t offset_src, void *dst, int64_t offset_dst, int64_t size) {
    auto& device = usable_devices[dev_src];
    auto src_buffer = device->get_buffer_by_device_address(reinterpret_cast<VkDeviceAddress>(src));

    std::unique_ptr<Buffer> tmp_buffer;

    void* host_ptr = (void*)((size_t)dst + offset_dst);
        // Import host memory and wrap it in a buffer
    if (device->can_import_host_memory) {
        tmp_buffer = std::make_unique<Buffer>(*device, size, Buffer::ImportedHostMemory { host_ptr }, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    } else {
        tmp_buffer = std::make_unique<Buffer>(*device, size, Buffer::HostMemory { }, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }

    device->execute_command_buffer_oneshot([&](VkCommandBuffer cmd_buf) {
        VkBufferCopy copy_region {
            .srcOffset = (VkDeviceSize) offset_src,
            .dstOffset = 0,
            .size = (VkDeviceSize) size,
        };
        vkCmdCopyBuffer(cmd_buf, src_buffer->handle_, tmp_buffer->handle_, 1, &copy_region);
    });

    if (!device->can_import_host_memory) {
        void* mapped = nullptr;
        CHECK(vkMapMemory(device->handle_, tmp_buffer->device_memory_, 0, size, 0, &mapped));
        assert(mapped != nullptr);
        memcpy(host_ptr, mapped, size);
        vkUnmapMemory(device->handle_, tmp_buffer->device_memory_);
    }
}

const char *VulkanPlatform::device_name(DeviceId dev) const {
    return usable_devices[dev]->properties.properties.deviceName;
}

void register_vulkan_platform(Runtime* runtime) {
    runtime->register_platform<VulkanPlatform>();
}

VulkanPlatform::Kernel::~Kernel() {
    vkDestroyPipeline(device.handle_, pipeline, nullptr);
    vkDestroyPipelineLayout(device.handle_, layout, nullptr);
    vkDestroyShaderModule(device.handle_, shader_module, nullptr);
}
