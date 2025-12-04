#include "vulkan_platform.h"

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

VulkanPlatform::Buffer::~Buffer() {
    if (device_memory_)
        vkFreeMemory(device_.handle_, device_memory_, nullptr);
    vkDestroyBuffer(device_.handle_, handle_, nullptr);
}

uint64_t VulkanPlatform::Device::create_buffer_resource(size_t size, Buffer::BackingStorage backing, VkBufferUsageFlags usage) {
    std::unique_ptr<Buffer> buffer = std::make_unique<Buffer>(*this, size, backing, usage);

    assert(buffer->device_address_);
    auto& b = *(buffers_[buffer->device_address_] = std::move(buffer));

    return b.device_address_;
}

void VulkanPlatform::Device::destroy_buffer(uint64_t addr) {
    auto found = buffers_.find(addr);
    if (found != buffers_.end()) {
        buffers_.erase(found);
    }
}