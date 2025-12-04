#include "vulkan_platform.h"

extern "C" {
#include "shady/jit/vulkan.h"
#include "shady/be/spirv.h"
}

VulkanPlatform::Module::Module(VulkanPlatform::Device& device, std::string file_name, std::string kernel_name) : device_(device), entry_point(kernel_name) {
    TargetConfig specialized_target = device_.target_config_;
    specialized_target.execution_model = ShdExecutionModelCompute;
    specialized_target.entry_point = kernel_name.c_str();

    std::string program_src = device_.platform_.runtime_->load_file(file_name);
    shd_driver_load_source_file(&device_.platform_.compiler_config_, &device_.target_config_, SrcSPIRV, program_src.size(), program_src.c_str(), "test", &shady_module_);
    // TODO: this will be removed in a future version of Shady
    CompilerConfig specialized_config = device_.platform_.compiler_config_;
    specialized_config.dynamic_scheduling = false;
    SPVBackendConfig backend_config;
    shd_jit_vk_get_compiler_config_for_device(&device_.shady_caps_, &device_.target_config_, &backend_config, &specialized_config);
    shd_jit_vk_compile_module(&shady_module_, &specialized_target, &backend_config, &specialized_config);
    size_t spirv_size;
    char* spirv_bytes;
    shd_emit_spirv(&specialized_config, &backend_config, shady_module_, &spirv_size, &spirv_bytes);

    size_t interface_size;
    shd_rt_vk_get_module_interface(shady_module_, &interface_size, nullptr);
    interface.resize(interface_size);
    shd_rt_vk_get_module_interface(shady_module_, &interface_size, interface.data());

    for (auto& e: interface) {
        if (e.dst_kind == RuntimeInterfaceItem::SHD_RII_Dst_PushConstant)
            push_constant_size = std::max(push_constant_size, e.dst_details.push_constant.offset + e.dst_details.push_constant.size);
    }

    auto shader_module_create_info = VkShaderModuleCreateInfo {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .pNext = nullptr,
        .flags = 0,
        .codeSize = spirv_size,
        .pCode = reinterpret_cast<const uint32_t*>(spirv_bytes),
    };
    CHECK(vkCreateShaderModule(device.handle_, &shader_module_create_info, nullptr, &shader_module));
}

VulkanPlatform::Module::~Module() {
    vkDestroyShaderModule(device_.handle_, shader_module, nullptr);
}

VulkanPlatform::Module* VulkanPlatform::Device::load_module(const std::string& filename, const std::string& kernel_name) {
    auto key = filename + "::" + kernel_name;
    auto ki = modules_.find(key);
    if (ki == modules_.end()) {
        auto [i, b] = modules_.emplace(key, std::make_unique<Module>(*this, filename, kernel_name));
        return &*i->second;
    }
    return ki->second.get();
}

void VulkanPlatform::Module::setup(VkCommandBuffer cmdbuf, char* push_constants, size_t count, void** args) {
    for (auto& e: interface) {
        if (e.dst_kind == RuntimeInterfaceItem::SHD_RII_Dst_PushConstant) {
            switch (e.src_kind) {
                case RuntimeInterfaceItem::SHD_RII_Src_Param:
                    //assert(e.dst_details.push_constant.size == launch_params.args.sizes[e.src_details.param.param_idx]);
                    assert(e.src_details.param.param_idx < count);
                    memcpy(reinterpret_cast<uint8_t*>(push_constants) + e.dst_details.push_constant.offset, args[e.src_details.param.param_idx], e.dst_details.push_constant.size);
                    break;
                default:
                    error("TODO");
                    //case RuntimeInterfaceItem::SHD_RII_Src_TmpAllocation:
                    //    break;
                    //case RuntimeInterfaceItem::SHD_RII_Src_LiftedConstant:
                    //    break;
                    //case RuntimeInterfaceItem::SHD_RII_Src_ScratchBuffer:
                    //    break;
            }

        } else {
            error("todo: implement descriptors");
        }
    }
}