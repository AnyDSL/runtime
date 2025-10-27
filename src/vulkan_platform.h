#ifndef ANYDSL_RUNTIME_VULKAN_PLATFORM_H
#define ANYDSL_RUNTIME_VULKAN_PLATFORM_H

#include "platform.h"
#include <vulkan/vulkan.h>

extern "C" {
#include "shady/runtime/vulkan.h"
}

#include <functional>
#include <variant>

/// Vulkan requires you to manually load certain function pointers, we use a macro to automate the boilerplate
#define DevicesExtensionsFunctions(f) \
    f(vkGetMemoryHostPointerPropertiesEXT) \
    f(vkGetBufferDeviceAddressKHR)

class VulkanPlatform : public Platform {
public:
    VulkanPlatform(Runtime* runtime);
    ~VulkanPlatform() override;

public:
    void *alloc(DeviceId dev, int64_t size) override;
    void *alloc_host(DeviceId dev, int64_t size) override;
    void *alloc_unified(DeviceId dev, int64_t size) override { command_unavailable("alloc_unified"); }
    void *get_device_ptr(DeviceId dev, void *ptr) override;
    void release(DeviceId dev, void *ptr) override;
    void release_host(DeviceId dev, void *ptr) override;

    void launch_kernel(DeviceId dev, const LaunchParams &launch_params) override;
    void synchronize(DeviceId dev) override;

    void copy(DeviceId dev_src, const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst, int64_t size) override;
    void copy_from_host(const void *src, int64_t offset_src, DeviceId dev_dst, void *dst, int64_t offset_dst, int64_t size) override;
    void copy_to_host(DeviceId dev_src, const void *src, int64_t offset_src, void *dst, int64_t offset_dst, int64_t size) override;

    size_t dev_count() const override { return usable_devices.size(); }
    std::string name() const override { return "Vulkan"; }

    const char* device_name(DeviceId dev) const override;
    bool device_check_feature_support(DeviceId, const char*) const override { return false; }

    struct Device;

    struct Resource {
        Device& device_;

        Resource(Device& device) : device_(device) {}
        virtual ~Resource() {};
    };

    struct Buffer : public Resource {
        VkBuffer handle_;

        void* host_address_ = nullptr;
        VkDeviceAddress device_address_ = 0;

        VkDeviceMemory device_memory_;

        const static VkBufferUsageFlags2 ALL_BUFFER_USAGE =
            VK_BUFFER_USAGE_2_STORAGE_BUFFER_BIT |
            VK_BUFFER_USAGE_2_SHADER_DEVICE_ADDRESS_BIT |
            VK_BUFFER_USAGE_2_TRANSFER_SRC_BIT |
            VK_BUFFER_USAGE_2_TRANSFER_DST_BIT;


        struct ImportedHostMemory {
            void* host_memory_;
        };

        struct DeviceMemory {};
        struct HostMemory {};
        struct UnifiedMemory {};

        using BackingStorage = std::variant<ImportedHostMemory, DeviceMemory, HostMemory, UnifiedMemory>;
        friend Device;
        friend Platform;

        Buffer(Device& device, size_t size, BackingStorage backing_storage, VkBufferUsageFlags2 usages = ALL_BUFFER_USAGE);
        ~Buffer() override;
    };

    struct Module {
        Device& device_;

        std::string entry_point;
        ::Module* shady_module_;
        std::vector<RuntimeInterfaceItem> interface;
        size_t push_constant_size = 0;

        VkShaderModule shader_module;

        Module(Device& device, std::string, std::string);
        void setup(VkCommandBuffer, char*, size_t count, void** args);
        ~Module();
    };

    struct Kernel {
        Device& device_;
        Module& module_;

        VkPipelineLayout layout;
        VkPipeline pipeline;

        Kernel(Device& device, Module&);
        void dispatch(VkCommandBuffer, const LaunchParams &launch_params);
        ~Kernel();
    };

    struct ExtensionFns {
#define f(s) PFN_##s s;
        DevicesExtensionsFunctions(f)
#undef f
    };

    struct Device {
        VulkanPlatform& platform_;
        VkPhysicalDevice physical_device;
        VkDevice handle_ = nullptr;
        size_t device_id;

        ExtensionFns extension_fns;

        VkPhysicalDeviceProperties2 properties = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,
        };

        bool can_import_host_memory = false;
        VkPhysicalDeviceExternalMemoryHostPropertiesEXT external_memory_host_properties {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_MEMORY_HOST_PROPERTIES_EXT,
            .pNext = nullptr,
            .minImportedHostPointerAlignment = 0xFFFFFFFF,
        };

        ShadyVkrPhysicalDeviceCaps shady_caps_;
        TargetConfig target_config_;

        std::unordered_map<VkDeviceAddress, std::unique_ptr<Buffer>> buffers_;
        std::unordered_map<std::string, std::unique_ptr<Module>> modules;
        std::unordered_map<std::string, std::unique_ptr<Kernel>> kernels;

        VkQueue queue;
        VkCommandPool cmd_pool;
        std::vector<VkCommandBuffer> spare_cmd_bufs;

        Device(VulkanPlatform& platform, VkPhysicalDevice physical_device, size_t device_id);
        ~Device();

        uint32_t find_suitable_memory_type(uint32_t memory_type_bits, VkMemoryPropertyFlags, VkMemoryHeapFlags = 0);
        VkDeviceMemory allocate_memory(VkDeviceSize, uint32_t memory_type_bits, VkMemoryPropertyFlags memory_flags, VkMemoryHeapFlags heap_flags = 0);
        std::pair<VkDeviceMemory, size_t> import_host_memory(void* ptr, size_t size);

        Buffer* get_buffer_by_device_address(VkDeviceAddress addr) {
            auto found = buffers_.find(addr);
            if (found != buffers_.end())
                return &*found->second;
            return nullptr;
        }

        uint64_t create_buffer_resource(size_t, Buffer::BackingStorage backing, VkBufferUsageFlags usage_flags);

        VkCommandBuffer obtain_command_buffer();
        void return_command_buffer(VkCommandBuffer cmd_buf);
        void execute_command_buffer_oneshot(std::function<void(VkCommandBuffer)> fn);

        Module* load_module(const std::string&, const std::string&);
        Kernel* load_kernel(const std::string&, const std::string&);
    };

    VkInstance instance;
    std::vector<VkPhysicalDevice> physical_devices;
    std::vector<std::unique_ptr<Device>> usable_devices;

    CompilerConfig compiler_config_ = shd_default_compiler_config();
};

#endif
