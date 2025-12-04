#include "vulkan_platform.h"

void VulkanPlatform::copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
    command_unavailable("copy");
}

void VulkanPlatform::copy_from_host(const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
    auto& device = usable_devices[dev_dst];
    auto dst_buffer = device->get_buffer_by_device_address(reinterpret_cast<VkDeviceAddress>(dst));

    std::unique_ptr<Buffer> tmp_buffer;

    void* host_ptr = (void*) ((size_t) src + offset_src);
    // Import host memory and wrap it in a buffer
    if (device->can_import_host_memory_) {
        tmp_buffer = std::make_unique<Buffer>(*device, size, Buffer::ImportedHostMemory { host_ptr }, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    } else {
        tmp_buffer = std::make_unique<Buffer>(*device, size, Buffer::HostMemory {}, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
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

void VulkanPlatform::copy_to_host(DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) {
    auto& device = usable_devices[dev_src];
    auto src_buffer = device->get_buffer_by_device_address(reinterpret_cast<VkDeviceAddress>(src));

    std::unique_ptr<Buffer> tmp_buffer;

    void* host_ptr = (void*) ((size_t) dst + offset_dst);
    // Import host memory and wrap it in a buffer
    if (device->can_import_host_memory_) {
        tmp_buffer = std::make_unique<Buffer>(*device, size, Buffer::ImportedHostMemory { host_ptr }, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    } else {
        tmp_buffer = std::make_unique<Buffer>(*device, size, Buffer::HostMemory {}, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    }

    device->execute_command_buffer_oneshot([&](VkCommandBuffer cmd_buf) {
        VkBufferCopy copy_region {
            .srcOffset = (VkDeviceSize) offset_src,
            .dstOffset = 0,
            .size = (VkDeviceSize) size,
        };
        vkCmdCopyBuffer(cmd_buf, src_buffer->handle_, tmp_buffer->handle_, 1, &copy_region);
    });

    if (!device->can_import_host_memory_) {
        void* mapped = nullptr;
        CHECK(vkMapMemory(device->handle_, tmp_buffer->device_memory_, 0, size, 0, &mapped));
        assert(mapped != nullptr);
        memcpy(host_ptr, mapped, size);
        vkUnmapMemory(device->handle_, tmp_buffer->device_memory_);
    }
}