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

VulkanPlatform::VulkanPlatform(Runtime* runtime) : Platform(runtime) {
    auto available_layers = query_layers_available();
    auto available_instance_extensions = query_extensions_available();

    std::vector<const char*> enabled_layers;
    std::vector<const char*> enabled_instance_extensions;

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
        .pApplicationName = "AnyDSL Runtime"
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
        VkPhysicalDeviceProperties properties;
        vkGetPhysicalDeviceProperties(dev, &properties);
        debug("  GPU%:", i);
        debug("  Device name: %", properties.deviceName);
        debug("  Vulkan version %.%.%", VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion));

        usable_devices.emplace_back(std::make_unique<Device>(*this, dev, i));
        i++;
    }
}

VulkanPlatform::~VulkanPlatform() {
    vkDestroyInstance(instance, nullptr);
}

VulkanPlatform::Device::Device(VulkanPlatform& platform, VkPhysicalDevice physical_device, size_t i)
 : platform(platform), physical_device(physical_device), i(i) {
    /*auto create_info = VkDeviceCreateInfo {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,

    };
    vkCreateDevice(physical_device, &create_info, nullptr, &device);*/
}

VulkanPlatform::Device::~Device() {
    //vkDestroyDevice(device, nullptr);
}

void *VulkanPlatform::alloc(DeviceId dev, int64_t size) {
    return nullptr;
}

void *VulkanPlatform::alloc_host(DeviceId dev, int64_t size) {
    return nullptr;
}

void *VulkanPlatform::get_device_ptr(DeviceId dev, void *ptr) {
    return nullptr;
}

void VulkanPlatform::release(DeviceId dev, void *ptr) {

}

void VulkanPlatform::release_host(DeviceId dev, void *ptr) {

}

void VulkanPlatform::launch_kernel(DeviceId dev, const LaunchParams &launch_params) {

}

void VulkanPlatform::synchronize(DeviceId dev) {

}

void VulkanPlatform::copy(DeviceId dev_src, const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst, int64_t size) {

}

void VulkanPlatform::copy_from_host(const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst, int64_t size) {

}

void VulkanPlatform::copy_to_host(DeviceId dev_src, const void *src, int64_t offset_src, void *dst, int64_t offset_dst, int64_t size) {

}

void register_vulkan_platform(Runtime* runtime) {
    runtime->register_platform<VulkanPlatform>();
}
