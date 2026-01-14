#include <thread>

#include "vulkan_platform.h"

void VulkanPlatform::copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
    command_unavailable("copy");
}

static void buffer_to_buffer_copy(VulkanPlatform::Device& device, VkBuffer src, size_t src_offset, VkBuffer dst, size_t dst_offset, size_t size) {
    assert(size % 4 == 0);
    device.execute_command_buffer_oneshot([&](VkCommandBuffer cmd_buf) {
        VkMemoryBarrier2 memory_barrier = {
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
            .pNext = nullptr,
            .srcStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            .srcAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
            .dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
        };
        VkDependencyInfo deps = {
            .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
            .pNext = nullptr,
            .dependencyFlags = 0,
            .memoryBarrierCount = 1,
            .pMemoryBarriers = &memory_barrier,
        };
        vkCmdPipelineBarrier2(cmd_buf, &deps);
        //vkCmdFillBuffer(cmd_buf, dst, 0, size, 0x55555555);
        VkBufferCopy copy_region {
            .srcOffset = src_offset,
            .dstOffset = (VkDeviceSize) dst_offset,
            .size = (VkDeviceSize) size,
        };
        vkCmdCopyBuffer(cmd_buf, src, dst, 1, &copy_region);
        vkCmdPipelineBarrier2(cmd_buf, &deps);
    });
}

void VulkanPlatform::copy_from_host(const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, const int64_t size) {
    auto& device = usable_devices[dev_dst];
    assert(offset_dst == 0);
    assert(size % 4 == 0);

    auto dst_buffer = device->get_buffer_by_device_address(reinterpret_cast<VkDeviceAddress>(dst));

    if (std::get_if<Buffer::HostMemory>(&dst_buffer->backing_storage_)) {
        void* mapped = nullptr;
        CHECK(vkMapMemory(device->handle_, dst_buffer->device_memory_, 0, size, 0, &mapped));
        assert(mapped != nullptr);
        memcpy(mapped, (uint8_t*) src + offset_src, size);
        vkUnmapMemory(device->handle_, dst_buffer->device_memory_);
        printf("fast\n");
        return;
    }

    std::unique_ptr<Buffer> tmp_buffer;

    void* host_ptr = (void*) ((size_t) src + offset_src);

    auto check_for_zeroes = [&](uint32_t* start) {
        uint32_t* host_iptr = (uint32_t*) host_ptr;
        int consecutive_zeroes = 0;
        size_t i = 0;
        for (uint32_t* word = start; word < start + size / sizeof(uint32_t); word++, i++) {
            auto data = *word;
            //printf("%u == %u,", data, host_iptr[i]);
            if (data != 0 && data == host_iptr[i])
                consecutive_zeroes = 0;
            else {
                consecutive_zeroes++;
                if (consecutive_zeroes >= 16) {
                    assert(false);
                }
            }
        };
    };

    check_for_zeroes((uint32_t*) host_ptr);

    // Import host memory and wrap it in a buffer
    if (device->can_import_host_memory_) {
        tmp_buffer = std::make_unique<Buffer>(*device, size, Buffer::ImportedHostMemory { host_ptr }, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
    } else {
        auto align = device->properties_.properties.limits.nonCoherentAtomSize;
        //auto host_size = ((size + align - 1) / align) * align;
        //assert(host_size >= size);
        tmp_buffer = std::make_unique<Buffer>(*device, size, Buffer::HostMemory {}, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
        void* mapped = nullptr;
        CHECK(vkMapMemory(device->handle_, tmp_buffer->device_memory_, 0, tmp_buffer->device_memory_bytes_, 0, &mapped));
        assert(mapped != nullptr);
        // for (size_t i = 0; i < size; i++) {
        //     ((uint8_t*)mapped)[i] = ((uint8_t*)host_ptr)[i];
        // }
        memcpy(mapped, host_ptr, size);
        VkMappedMemoryRange range = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .pNext = nullptr,
            .memory = tmp_buffer->device_memory_,
            .offset = 0,
            .size = (VkDeviceSize) tmp_buffer->device_memory_bytes_,
        };
        vkFlushMappedMemoryRanges(device->handle_, 1, &range);
        //check_for_zeroes((uint32_t*) mapped);
        vkUnmapMemory(device->handle_, tmp_buffer->device_memory_);
    }

    buffer_to_buffer_copy(*device, tmp_buffer->handle_, 0, dst_buffer->handle_, offset_dst, size);
    std::this_thread::sleep_for(std::chrono::milliseconds(01));

    void* sanity = malloc(size);
    copy_to_host(dev_dst, dst, offset_dst, sanity, 0, size);
    std::this_thread::sleep_for(std::chrono::milliseconds(01));
    check_for_zeroes((uint32_t*) sanity);
    printf("Buffer upload went OK. %d bytes\n", size);
    free(sanity);
}

void VulkanPlatform::copy_to_host(DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, const int64_t size) {
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

    buffer_to_buffer_copy(*device, src_buffer->handle_, offset_src, tmp_buffer->handle_, 0, size);

    if (!device->can_import_host_memory_) {
        void* mapped = nullptr;
        CHECK(vkMapMemory(device->handle_, tmp_buffer->device_memory_, 0, tmp_buffer->device_memory_bytes_, 0, &mapped));
        VkMappedMemoryRange range = {
            .sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
            .pNext = nullptr,
            .memory = tmp_buffer->device_memory_,
            .offset = 0,
            .size = (VkDeviceSize) tmp_buffer->device_memory_bytes_,
        };
        vkInvalidateMappedMemoryRanges(device->handle_, 1, &range);
        assert(mapped != nullptr);
        memcpy(host_ptr, mapped, size);
        // for (size_t i = 0; i < size; i++) {
        //     ((uint8_t*)host_ptr)[i] = ((uint8_t*)mapped)[i];
        // }
        vkUnmapMemory(device->handle_, tmp_buffer->device_memory_);
    }
}