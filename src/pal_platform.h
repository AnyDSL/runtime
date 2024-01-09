// Copyright (c) 2023 Advanced Micro Devices, Inc. All rights reserved.

#ifndef PAL_PLATFORM_H
#define PAL_PLATFORM_H

#include "pal/pal_device.h"
#include "pal/pal_utils.h"
#include "platform.h"
#include "runtime.h"

#include <atomic>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

#include <pal.h>
#include <palLib.h>
#include <palPipeline.h>
#include <palPlatform.h>
#include <palQueue.h>

#ifdef AnyDSL_runtime_HAS_LLVM_SUPPORT
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Pass.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Target/TargetMachine.h>
#endif

namespace llvm_utils {

// Initializes llvm with amdgpu using the following commandline options:
// ANYDSL_LLVM_ARGS="-<custom_llvm_args> -amdgpu-sroa -amdgpu-load-store-vectorizer
// -amdgpu-scalarize-global-loads -amdgpu-internalize-symbols
// -amdgpu-early-inline-all -amdgpu-sdwa-peephole -amdgpu-dpp-combine
// -enable-amdgpu-aa -amdgpu-late-structurize=0 -amdgpu-function-calls
// -amdgpu-simplify-libcall -amdgpu-ir-lower-kernel-arguments
// -amdgpu-atomic-optimizations -amdgpu-mode-register"
void initialize_amdgpu(std::vector<std::string> custom_llvm_args);

std::unique_ptr<llvm::TargetMachine> create_target_machine(const std::string& target_triple, const std::string& cpu);

// Creates a module, sets its data layout and links it to the base_module.
void add_module(llvm::Linker& base_module, const std::string& module_file_path, const llvm::DataLayout& machine_data_layout,
    llvm::LLVMContext& llvm_context, llvm::Linker::Flags flags);

// Creates a module, sets its data layout and links it to the base_module.
void add_module(llvm::Linker& base_module, const llvm::MemoryBufferRef& module_data_ref, const llvm::DataLayout& machine_data_layout,
    llvm::LLVMContext& llvm_context, llvm::Linker::Flags flags, const char* module_name);

} // namespace llvm_utils

class PALPlatform : public Platform {
public:
    PALPlatform(Runtime* runtime);
    ~PALPlatform();

protected:
    void* alloc(DeviceId dev, int64_t size) override;
    void* alloc_host(DeviceId dev, int64_t size) override;
    void* alloc_unified(DeviceId dev, int64_t size) override;
    void* alloc_upload(DeviceId dev, int64_t size);
    void* get_device_ptr(DeviceId, void*) override { command_unavailable("get_device_ptr"); }
    void release(DeviceId dev, void* ptr) override;
    void release_host(DeviceId dev, void* ptr) override;

    void launch_kernel(DeviceId dev, const LaunchParams& launch_params) override;

    void synchronize(DeviceId dev) override;

    void copy(DeviceId, const void* src, int64_t offset_src, DeviceId, void* dst, int64_t offset_dst,
        int64_t size) override;
    void copy_from_host(
        const void* src, int64_t offset_src, DeviceId, void* dst, int64_t offset_dst, int64_t size) override;
    void copy_to_host(
        DeviceId, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) override;

    size_t dev_count() const override { return devices_.size(); }
    std::string name() const override { return "PAL"; }
    const char* device_name(DeviceId dev) const override;
    bool device_check_feature_support(DeviceId, const char*) const override { return false; }

    Pal::IPipeline* load_kernel(DeviceId dev, const std::string& filename, const std::string& kernelname);
    std::string compile_gcn(DeviceId dev, pal_utils::ShaderSrc&& shader_src) const;
    std::string emit_gcn(pal_utils::ShaderSrc&& shader_src, const std::string& cpu,
        Pal::GfxIpLevel gfx_level, llvm::OptimizationLevel opt) const;

protected:
    Pal::IPlatform* platform_;
    std::vector<PalDevice> devices_;
};

#endif
