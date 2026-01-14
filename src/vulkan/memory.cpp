#include "vulkan_platform.h"

uint32_t VulkanPlatform::Device::find_suitable_memory_type(uint32_t memory_type_bits, VkMemoryPropertyFlags memory_flags, VkMemoryHeapFlags heap_flags) {
    VkPhysicalDeviceMemoryProperties device_memory_properties;
    vkGetPhysicalDeviceMemoryProperties(physical_device_, &device_memory_properties);
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

VkDeviceMemory VulkanPlatform::Device::allocate_memory(VkDeviceSize size, uint32_t memory_type_bits, VkMemoryPropertyFlags memory_flags, VkMemoryHeapFlags heap_flags, VkMemoryAllocateFlags allocation_flags) {
    auto allocate_flags = VkMemoryAllocateFlagsInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_FLAGS_INFO,
        .pNext = nullptr,
        .flags = allocation_flags,
        .deviceMask = 0
    };

    auto allocation_info = VkMemoryAllocateInfo {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .pNext = &allocate_flags,
        .allocationSize = size, // the driver might want padding !
        .memoryTypeIndex = find_suitable_memory_type(memory_type_bits, memory_flags, heap_flags),
    };
    VkDeviceMemory memory;
    CHECK(vkAllocateMemory(handle_, &allocation_info, nullptr, &memory));

    return memory;
}

std::pair<VkDeviceMemory, size_t> VulkanPlatform::Device::import_host_memory(void *ptr, size_t size) {
    assert(can_import_host_memory_ && "This device does not support importing host memory");

    size_t alignment = external_memory_host_properties_.minImportedHostPointerAlignment;

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
    CHECK(extension_fns.vkGetMemoryHostPointerPropertiesEXT(handle_, VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT, (void*)aligned_host_ptr, &host_ptr_properties));
    uint32_t memory_type = find_suitable_memory_type(host_ptr_properties.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    // Import memory
    auto import_ptr_info = VkImportMemoryHostPointerInfoEXT {
            .sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_HOST_POINTER_INFO_EXT,
            .pNext = nullptr,
            .handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT,
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