#include "vulkan_platform.h"

static std::string desired_device_extensions[] {
    "VK_KHR_swapchain",
    "VK_KHR_buffer_device_address",
    "VK_EXT_external_memory_host",
    "VK_KHR_shader_non_semantic_info",
    "VK_EXT_mesh_shader",
};

VulkanPlatform::Device::Device(VulkanPlatform& platform, VkPhysicalDevice physical_device, size_t device_id)
    : platform_(platform), physical_device_(physical_device), device_id_(device_id) {
    uint32_t exts_count;
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &exts_count, nullptr);
    std::vector<VkExtensionProperties> available_device_extensions(exts_count);
    vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &exts_count, available_device_extensions.data());

    std::vector<const char*> enabled_device_extensions;
    for (auto& desired : desired_device_extensions) {
        for (auto available : available_device_extensions) {
            if (desired == available.extensionName)
                enabled_device_extensions.push_back(desired.c_str());
        }
    }

    // Use this to import host memory as GPU-visible memory, otherwise use a fallback path that copies when uploading/downloading
    //if (is_ext_available(available_device_extensions, "VK_EXT_external_memory_host")) {
    //    enabled_device_extensions.push_back("VK_EXT_external_memory_host");
    //    insert_pnext(properties, external_memory_host_properties);
    //    can_import_host_memory = true;
    //}


    properties_.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
    properties_.pNext = nullptr;
    vkGetPhysicalDeviceProperties2(physical_device, &properties_);
    auto& device_properties = properties_.properties;

    debug("  GPU%:", device_id);
    debug("  Device name: %", device_properties.deviceName);
    debug("  Vulkan version %.%.%", VK_VERSION_MAJOR(device_properties.apiVersion), VK_VERSION_MINOR(device_properties.apiVersion), VK_VERSION_PATCH(device_properties.apiVersion));

    if (can_import_host_memory_) {
        debug("  Min imported host ptr alignment: %", external_memory_host_properties_.minImportedHostPointerAlignment);
        if (external_memory_host_properties_.minImportedHostPointerAlignment == 0xFFFFFFFF)
            error("Device does not report minimum host pointer alignment");
    }

    uint32_t queue_families_count;
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_families_count, nullptr);
    std::vector<VkQueueFamilyProperties> queue_families(queue_families_count);
    vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_families_count, queue_families.data());

    int q = 0;
    for (auto& queue_f : queue_families) {
        bool has_gfx       = (queue_f.queueFlags & 0x00000001) != 0;
        bool has_compute   = (queue_f.queueFlags & 0x00000002) != 0;
        bool has_xfer      = (queue_f.queueFlags & 0x00000004) != 0;
        bool has_sparse    = (queue_f.queueFlags & 0x00000008) != 0;
        bool has_protected = (queue_f.queueFlags & 0x00000010) != 0;

        // TODO perform this intelligently
        if (selected_queue_family_ == -1 && has_compute && has_gfx)
            selected_queue_family_ = q;
        q++;
    }
    std::vector<VkDeviceQueueCreateInfo> queue_create_infos;
    float one = 1.0f;
    if (selected_queue_family_ != -1) {
        queue_create_infos.push_back(VkDeviceQueueCreateInfo {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .pNext = nullptr,
            .flags = 0,
            .queueFamilyIndex = (uint32_t) selected_queue_family_,
            .queueCount = 1,
            .pQueuePriorities = &one
        });
    } else {
        assert(false && "unsuitable device");
    }

    auto dynamic_rendering_features = VkPhysicalDeviceDynamicRenderingFeatures {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .dynamicRendering = true,
    };
    auto sync2features = VkPhysicalDeviceSynchronization2Features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES,
        .pNext = &dynamic_rendering_features,
        .synchronization2 = true,
    };
    auto bda_features = VkPhysicalDeviceBufferDeviceAddressFeaturesKHR {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR,
        .pNext = &sync2features,
        .bufferDeviceAddress = true,
    };
    auto vk11_features = VkPhysicalDeviceVulkan11Features {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES,
        .pNext = &bda_features,
        //.variablePointersStorageBuffer = true,
        //.variablePointers = true,
    };
    auto mesh_shader_features = VkPhysicalDeviceMeshShaderFeaturesEXT {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT,
        .pNext = &vk11_features,
        .meshShader = true,
    };
    auto enabled_features = VkPhysicalDeviceFeatures2 {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
        .pNext = &mesh_shader_features,
        .features = {
            //.vertexPipelineStoresAndAtomics = true,
            //.fragmentStoresAndAtomics = true,
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
    vkGetDeviceQueue(handle_, selected_queue_family_, 0, &queue_);

    auto cmd_pool_create_info = VkCommandPoolCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .pNext = nullptr,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        .queueFamilyIndex = (uint32_t) selected_queue_family_,
    };
    CHECK(vkCreateCommandPool(handle_, &cmd_pool_create_info, nullptr, &pool_));

    // Load function pointers
#define f(s) extension_fns.s = (PFN_##s) vkGetDeviceProcAddr(handle_, #s);
    DevicesExtensionsFunctions(f)
#undef f

    bool device_ok = shd_rt_vk_check_physical_device_suitability(physical_device, &shady_caps_);
    assert(device_ok);
    target_config_ = shd_rt_vk_get_device_target_config(&platform_.compiler_config_, &shady_caps_);
}

VulkanPlatform::Device::~Device() {
    kernels_.clear();
    buffers_.clear();
    modules_.clear();
    command_buffers_.clear();
    vkDestroyCommandPool(handle_, pool_, nullptr);
    //if (!resources.empty()) {
    //    info("Some vulkan resources were not released. Releasing those automatically...");
    //    resources.clear();
    //}
    vkDestroyDevice(handle_, nullptr);
}
