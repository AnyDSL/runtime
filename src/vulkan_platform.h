#ifndef ANYDSL_RUNTIME_VULKAN_PLATFORM_H
#define ANYDSL_RUNTIME_VULKAN_PLATFORM_H

#include "platform.h"
#include <vulkan/vulkan.h>

#include <functional>

/// Vulkan requires you to manually load certain function pointers, we use a macro to automate the boilerplate
#define DevicesExtensionsFunctions(f) \
    f(vkGetMemoryHostPointerPropertiesEXT) \

class VulkanPlatform : public Platform {
public:
    VulkanPlatform(Runtime* runtime);
    ~VulkanPlatform() override;

protected:
    void *alloc(DeviceId dev, int64_t size) override;
    void *alloc_host(DeviceId dev, int64_t size) override;
    void *alloc_unified(DeviceId dev, int64_t size) override { command_unavailable("alloc_unified"); }

    void *get_device_ptr(DeviceId dev, void *ptr) override;

    void release(DeviceId dev, void *ptr) override;

    void release_host(DeviceId dev, void *ptr) override;

    void launch_kernel(DeviceId dev, const LaunchParams &launch_params) override;

    void synchronize(DeviceId dev) override;

    void copy(DeviceId dev_src, const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst,
              int64_t size) override;

    void copy_from_host(const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst,
                        int64_t size) override;

    void copy_to_host(DeviceId dev_src, const void *src, int64_t offset_src, void *dst, int64_t offset_dst,
                      int64_t size) override;

    size_t dev_count() const override { return usable_devices.size(); }
    std::string name() const override { return "Vulkan"; }

    struct Device;

    struct Resource {
    //public:
        Device& device;
        size_t id;
        VkDeviceMemory alloc;

        Resource(Device& device) : device(device) {}
        virtual ~Resource();
    };

    struct Buffer : public Resource {
        VkBuffer buffer;

        Buffer(Device& device) : Resource(device) {}
        ~Buffer() override;
    };

    struct Kernel {
        Device& device;

        VkShaderModule shader_module;
        VkPipelineLayout layout;
        VkPipeline pipeline;

        Kernel(Device& device) : device(device) {}
        ~Kernel();
    };

    struct ExtensionFns {
#define f(s) PFN_##s s;
        DevicesExtensionsFunctions(f)
#undef f
    };

    struct Device {
        VulkanPlatform& platform;
        VkPhysicalDevice physical_device;
        size_t device_id;
        VkDevice device = nullptr;

        size_t min_imported_host_ptr_alignment;

        std::vector<std::unique_ptr<Resource>> resources;
        size_t next_resource_id = 1; // resource id 0 is reserved
        VkQueue queue;
        VkCommandPool cmd_pool;
        std::vector<VkCommandBuffer> spare_cmd_bufs;
        std::unordered_map<std::string, Kernel> kernels;
        ExtensionFns extension_fns;

        Device(VulkanPlatform& platform, VkPhysicalDevice physical_device, size_t device_id);
        ~Device();

        Resource* find_resource_by_id(size_t id);
        uint32_t find_suitable_memory_type(uint32_t memory_type_bits, bool prefer_device_local);
        VkDeviceMemory import_host_memory(void* ptr, size_t size);
        VkCommandBuffer obtain_command_buffer();
        void return_command_buffer(VkCommandBuffer cmd_buf);
        Kernel* load_kernel(const std::string&);

        void execute_command_buffer_oneshot(std::function<void(VkCommandBuffer)> fn);
    };

    VkInstance instance;
    std::vector<VkPhysicalDevice> physical_devices;
    std::vector<std::unique_ptr<Device>> usable_devices;
};

#endif
