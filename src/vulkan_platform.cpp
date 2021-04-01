#include "vulkan_platform.h"

VulkanPlatform::VulkanPlatform(Runtime* runtime) : Platform(runtime) {
    instance = vk::createInstance({});
    devices = instance.enumeratePhysicalDevices();
    debug("Available Vulkan physical devices: ");
    size_t i = 0;
    for (auto& dev : devices) {
        auto properties = dev.getProperties();
        debug("  GPU%:", i++);
        debug("  Device name: %", properties.deviceName);
        debug("  Vulkan version %.%.%", VK_VERSION_MAJOR(properties.apiVersion), VK_VERSION_MINOR(properties.apiVersion), VK_VERSION_PATCH(properties.apiVersion));
    }
}

VulkanPlatform::~VulkanPlatform() {
    instance.destroy();
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
