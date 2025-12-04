#include "vulkan_platform.h"

const auto khr_validation = "VK_LAYER_KHRONOS_validation";

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

static std::string desired_instance_extensions[] = {
    "VK_KHR_external_memory_capabilities",
    "VK_KHR_surface",
    "VK_KHR_wayland_surface",
    "VK_KHR_xcb_surface",
    "VK_EXT_metal_surface",
};

VulkanPlatform::VulkanPlatform(Runtime* runtime) : Platform(runtime) {
    auto available_layers = query_layers_available();
    auto available_instance_extensions = query_extensions_available();

    std::vector<const char*> enabled_layers;
    std::vector<const char*> enabled_instance_extensions;
    for (auto& desired : desired_instance_extensions) {
        for (auto available : available_instance_extensions) {
            if (desired == available.extensionName)
                enabled_instance_extensions.push_back(desired.c_str());
        }
    }

    bool should_enable_validation = false;
    if (getenv("ANYDSL_VULKAN_VALIDATION"))
        should_enable_validation = true;
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
        .apiVersion = VK_API_VERSION_1_3,
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
    auto vkCreateInstanceR = vkCreateInstance(&create_info, nullptr, &instance);
    printf("vkCreateInstance: %d\n", vkCreateInstanceR);

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

VulkanPlatform::Kernel::Kernel(Device& device, Module& module) : device_(device), module_(module) {
    auto stage = VkPipelineShaderStageCreateInfo {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = VK_SHADER_STAGE_COMPUTE_BIT,
        .module = module_.shader_module,
        .pName = module.entry_point.c_str(),
        .pSpecializationInfo = nullptr,
    };

    std::vector<VkPushConstantRange> push_constants {
        VkPushConstantRange {
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
            .offset = 0,
            .size = static_cast<uint32_t>(module_.push_constant_size)
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
    CHECK(vkCreatePipelineLayout(device.handle_, &layout_create_info, nullptr, &layout));

    auto compute_pipeline_create_info = VkComputePipelineCreateInfo {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .stage = stage,
        .layout = layout,
        .basePipelineHandle = VK_NULL_HANDLE,
        .basePipelineIndex = 0,
    };
    CHECK(vkCreateComputePipelines(device.handle_, nullptr, 1, &compute_pipeline_create_info, nullptr, &pipeline));
}

VulkanPlatform::Kernel::~Kernel() {
    vkDestroyPipeline(device_.handle_, pipeline, nullptr);
    vkDestroyPipelineLayout(device_.handle_, layout, nullptr);
}

VulkanPlatform::Kernel* VulkanPlatform::Device::load_kernel(const std::string& filename, const std::string& kernel_name) {
    auto key = filename + "::" + kernel_name;
    auto ki = kernels_.find(key);
    if (ki == kernels_.end()) {
        auto [i,b] = kernels_.emplace(key, std::make_unique<Kernel>(*this, *load_module(filename, kernel_name)));
        return &*i->second;
    }

    return ki->second.get();
}

void VulkanPlatform::Kernel::dispatch(VkCommandBuffer cmdbuf, const LaunchParams& launch_params) {
    vkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
    std::vector<char> push_constants;
    push_constants.resize(module_.push_constant_size);

    module_.setup(cmdbuf, push_constants.data(), launch_params.num_args, launch_params.args.data);

    vkCmdPushConstants(cmdbuf, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, module_.push_constant_size, push_constants.data());
    vkCmdDispatch(cmdbuf, launch_params.grid[0] / launch_params.block[0], launch_params.grid[1] / launch_params.block[1], launch_params.grid[2] / launch_params.block[2]);
}

void VulkanPlatform::launch_kernel(DeviceId dev, const LaunchParams &launch_params) {
    auto& device = usable_devices[dev];
    auto kernel = device->load_kernel(launch_params.file_name, launch_params.kernel_name);

    device->execute_command_buffer_oneshot([&](VkCommandBuffer cmd_buf) {
        kernel->dispatch(cmd_buf, launch_params);
    });
}

void VulkanPlatform::synchronize(DeviceId dev) {
    // TODO: don't wait for idle everywhere
}

VkCommandBuffer VulkanPlatform::Device::obtain_command_buffer() {
    if (command_buffers_.size() > 0) {
        VkCommandBuffer cmd_buf = command_buffers_.back();
        command_buffers_.pop_back();
        return cmd_buf;
    }
    auto cmd_buf_create_info = VkCommandBufferAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .pNext = nullptr,
        .commandPool = pool_,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1
    };
    VkCommandBuffer cmd_buf;
    CHECK(vkAllocateCommandBuffers(handle_, &cmd_buf_create_info, &cmd_buf));
    return cmd_buf;
}

void VulkanPlatform::Device::return_command_buffer(VkCommandBuffer cmd_buf) {
    vkResetCommandBuffer(cmd_buf, 0);
    command_buffers_.push_back(cmd_buf);
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
    CHECK(vkQueueSubmit(queue_, 1, &submit_info, VK_NULL_HANDLE));
    CHECK(vkDeviceWaitIdle(handle_));
    return_command_buffer(cmd_buf);
}

const char *VulkanPlatform::device_name(DeviceId dev) const {
    return usable_devices[dev]->properties_.properties.deviceName;
}

void register_vulkan_platform(Runtime* runtime) {
    runtime->register_platform<VulkanPlatform>();
}
