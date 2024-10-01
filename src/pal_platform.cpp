#include "pal_platform.h"
#include "pal/pal_fix_calling_convention_pass.h"
#include "pal/pal_insert_halt_pass.h"
#include "pal/pal_lower_builtins_pass.h"
#include "pal/pal_lower_kernel_arguments_pass.h"
#include "pal/pal_utils.h"
#include "runtime.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <sstream>
#include <string>
#include <thread>

#include <palPipelineAbi.h>

#ifdef AnyDSL_runtime_HAS_LLVM_SUPPORT
#include <lld/Common/Driver.h>
#include <llvm/IR/DataLayout.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Linker/Linker.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Pass.h>
#include <llvm/Passes/OptimizationLevel.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/GlobalDCE.h>
#include <llvm/Transforms/Utils/Cloning.h>
#endif

namespace llvm_utils {

// Initializes llvm with amdgpu using the passed in commandline options,
// as well as any further options set through the ANYDSL_LLVM_ARGS env variable.
void initialize_amdgpu(std::vector<std::string> custom_llvm_args) {
    // Useful options: ANYDSL_LLVM_ARGS ="-amdgpu-sroa -amdgpu-load-store-vectorizer -amdgpu-scalarize-global-loads -amdgpu-internalize-symbols -amdgpu-early-inline-all -amdgpu-sdwa-peephole -amdgpu-dpp-combine -enable-amdgpu-aa -amdgpu-late-structurize=0 -amdgpu-function-calls -amdgpu-simplify-libcall -amdgpu-ir-lower-kernel-arguments -amdgpu-atomic-optimizations -amdgpu-mode-register"
    // See: https://anydsl.github.io/Device-Code-Generation-and-Execution.html#amdgpu-code-generation-optimization
    const char* env_var = std::getenv("ANYDSL_LLVM_ARGS");    
    if (env_var) {
        std::istringstream stream(env_var);
        std::string tmp;
        while (stream >> tmp)
            custom_llvm_args.push_back(tmp);
    }
    if (!custom_llvm_args.empty()) {
        std::vector<const char*> c_llvm_args;
        for (auto& str : custom_llvm_args)
            c_llvm_args.push_back(str.c_str());
        llvm::cl::ParseCommandLineOptions(c_llvm_args.size(), c_llvm_args.data(), "AnyDSL gcn JIT compiler\n");
    }
    LLVMInitializeAMDGPUTarget();
    LLVMInitializeAMDGPUTargetInfo();
    LLVMInitializeAMDGPUTargetMC();
    LLVMInitializeAMDGPUAsmParser();
    LLVMInitializeAMDGPUAsmPrinter();
}

std::unique_ptr<llvm::TargetMachine> create_target_machine(
    const std::string& target_triple, const std::string& cpu) {
    std::string error_str;
    auto target = llvm::TargetRegistry::lookupTarget(target_triple, error_str);
    llvm::TargetOptions options;
    options.AllowFPOpFusion = llvm::FPOpFusion::Fast;
    options.NoTrappingFPMath = true;
    std::string attrs = "-trap-handler";
    return std::unique_ptr<llvm::TargetMachine>(target->createTargetMachine(target_triple, cpu, attrs,
        options, llvm::Reloc::PIC_, llvm::CodeModel::Small, llvm::CodeGenOptLevel::Aggressive));
}

void add_module_impl(std::unique_ptr<llvm::Module> module, const llvm::DataLayout& machine_data_layout,
    llvm::Linker& base_module, llvm::Linker::Flags flags, const llvm::SMDiagnostic& diagnostic_err,
    const char* module_name) {
    if (!module)
        error("Can't create module for '%':\n%", module_name, pal_utils::llvm_diagnostic_to_string(diagnostic_err));
    // override data layout with the one coming from the target machine
    module->setDataLayout(machine_data_layout);

    if (base_module.linkInModule(std::move(module), flags))
        error("Can't link '%' into module.", module_name);
}

// Creates a module, sets its data layout and links it to the base_module.
void add_module(llvm::Linker& base_module, const std::string& module_file_path,
    const llvm::DataLayout& machine_data_layout, llvm::LLVMContext& llvm_context, llvm::Linker::Flags flags) {
    llvm::SMDiagnostic diagnostic_err;
    std::unique_ptr<llvm::Module> module(llvm::parseIRFile(module_file_path, diagnostic_err, llvm_context));
    add_module_impl(std::move(module), machine_data_layout, base_module, flags, diagnostic_err, module_file_path.c_str());
}

// Creates a module, sets its data layout and links it to the base_module.
void add_module(llvm::Linker& base_module, const llvm::MemoryBufferRef& module_data_ref,
    const llvm::DataLayout& machine_data_layout, llvm::LLVMContext& llvm_context, llvm::Linker::Flags flags,
    const char* module_name) {
    llvm::SMDiagnostic diagnostic_err;
    std::unique_ptr<llvm::Module> module = llvm::parseIR(module_data_ref, diagnostic_err, llvm_context);
    add_module_impl(std::move(module), machine_data_layout, base_module, flags, diagnostic_err, module_name);
}

} // namespace llvm_utils

// Initializes the root platform object.
Pal::Result pal_init_platform(Pal::IPlatform** platform) {
    Pal::PlatformCreateInfo create_info = {};
    create_info.pAllocCb = nullptr;       // No allocation callbacks - just let PAL call
                                          // malloc() and free() directly.
    create_info.pSettingsPath = "Vulkan"; // Read settings from Vulkan's location

    // To support alloc_unified() we need SVM mode.
    create_info.flags.enableSvmMode = 1;
    // Reserve address space for 64 GiB - just like the OpenCL driver does.
    create_info.maxSvmSize = static_cast<Pal::gpusize>(64ll * 1024ll * 1024ll * 1024ll);

    Pal::Result result = Pal::CreatePlatform(create_info, malloc(Pal::GetPlatformSize()), platform);

    Pal::PlatformProperties properties = {};

    if (result == Pal::Result::Success) {
        result = (*platform)->GetProperties(&properties);
    }

    return result;
}

PALPlatform::PALPlatform(Runtime* runtime)
    : Platform(runtime) {
    Pal::Result result = pal_init_platform(&platform_);
    CHECK_PAL(result, "pal_init_platform()");

    // Get a list of all available devices.
    Pal::IDevice* pal_devices[Pal::MaxDevices] = {};
    Pal::uint32 device_count = 0;
    result = platform_->EnumerateDevices(&device_count, pal_devices);
    if (device_count <= 0) {
        // This will happen if there are no supported GPUs: PAL only supports SI and newer.
        result = Pal::Result::ErrorUnavailable;
    }
    CHECK_PAL(result, "EnumerateDevices()");

    // Initialize devices.
    for (Pal::uint32 i = 0; i < device_count; ++i) {
        Pal::DeviceProperties device_properties;
        if (Pal::Result::Success == pal_devices[i]->GetProperties(&device_properties)) {
            if (device_properties.gpuType != Pal::GpuType::Discrete) {
                // We ignore non-discrete GPUs
                continue;
            }
        }

        devices_.emplace_back(pal_devices[i], runtime);
    }
}

PALPlatform::~PALPlatform() {
    devices_.clear();
    if (platform_ != nullptr) {
        platform_->Destroy();
        free(platform_);
    }
}

void* PALPlatform::alloc(DeviceId dev, int64_t size) {
    auto& device = devices_[dev];
    Pal::GpuHeap target_heap = pal_utils::find_gpu_local_heap(device.device_, size);
    if (target_heap == Pal::GpuHeap::GpuHeapCount) {
        return nullptr;
    }
    return reinterpret_cast<void*>(device.allocate_gpu_memory(size, target_heap));
}

void* PALPlatform::alloc_host(DeviceId dev, int64_t size) {
    auto& device = devices_[dev];
    return reinterpret_cast<void*>(device.allocate_gpu_memory(size, Pal::GpuHeap::GpuHeapGartCacheable));
}

void* PALPlatform::alloc_unified(DeviceId dev, int64_t size) {
    auto& device = devices_[dev];
    return reinterpret_cast<void*>(device.allocate_shared_virtual_memory(size));
}

void PALPlatform::release(DeviceId dev, void* ptr) {
    auto& device = devices_[dev];
    device.release_gpu_memory(ptr);
}

void PALPlatform::release_host(DeviceId, void* ptr) {
    if (ptr == nullptr) {
        assert(false);
        return;
    }

    Pal::IGpuMemory* memory = static_cast<Pal::IGpuMemory*>(ptr);
    memory->Destroy();
    free(memory);
}

void PALPlatform::launch_kernel(DeviceId dev, const LaunchParams& launch_params) {
    Pal::IPipeline* pipeline = load_kernel(dev, launch_params.file_name, launch_params.kernel_name);

    Pal::CmdBufferBuildInfo cmd_buffer_build_info = {};
    cmd_buffer_build_info.flags.optimizeExclusiveSubmit = 1;

    Pal::PipelineBindParams params = {};
    params.pipelineBindPoint = Pal::PipelineBindPoint::Compute;
    params.pPipeline = pipeline;

    constexpr Pal::HwPipePoint pipe_point = Pal::HwPipePostCs;
    Pal::BarrierInfo barrier_info = {};
    barrier_info.waitPoint = Pal::HwPipePostCs;
    barrier_info.pipePointWaitCount = 1;
    barrier_info.pPipePoints = &pipe_point;
    barrier_info.globalSrcCacheMask = Pal::CoherShader;
    barrier_info.globalDstCacheMask = Pal::CoherShader;

    auto& device = devices_[dev];
    device.dispatch(cmd_buffer_build_info, params, barrier_info, launch_params);
}

void PALPlatform::synchronize(DeviceId dev) {
    devices_[dev].WaitIdle();
}

void PALPlatform::copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst,
    int64_t offset_dst, int64_t size) {
    // TODO: Implement inter device copy.
    assert(dev_src == dev_dst);

    PalDevice& device = devices_[dev_src];

    // Define region to be copied from src to dst.
    Pal::MemoryCopyRegion copy_region = {};
    copy_region.srcOffset = offset_src;
    copy_region.dstOffset = offset_dst;
    copy_region.copySize = size;

    device.copy_gpu_data(src, dst, copy_region);
}

void PALPlatform::copy_from_host(
    const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
    auto& device = devices_[dev_dst];
    Pal::IGpuMemory* dst_memory = device.get_memory_object(dst);

    if (!pal_utils::allocation_is_host_visible(dst_memory)) {
        // Create a temporary CPU-visible buffer on the GPU.
        // memcpy the data from the host space to the CPU-visible buffer
        // and then copy into the given GPU destination buffer.
        void* virtual_gpu_address = reinterpret_cast<void*>(device.allocate_gpu_memory(size, Pal::GpuHeap::GpuHeapLocal));
        Pal::IGpuMemory* intermediate_mem = device.get_memory_object(virtual_gpu_address);
        pal_utils::write_to_memory(
            intermediate_mem, 0, static_cast<const uint8_t*>(src) + offset_src, size);

        copy(dev_dst, virtual_gpu_address, 0, dev_dst, dst, offset_dst, size);
        release(dev_dst, virtual_gpu_address);
    } else {
        // Map and memcpy directly to CPU-visible GPU allocation.
        pal_utils::write_to_memory(dst_memory, offset_dst, static_cast<const uint8_t*>(src) + offset_src, size);
    }
}

void PALPlatform::copy_to_host(
    DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) {
    auto& device = devices_[dev_src];
    Pal::IGpuMemory* src_memory = device.get_memory_object(src);

    if (!pal_utils::allocation_is_host_visible(src_memory)) {
        // Create a temporary GPU-visible cached buffer on the CPU and copy into that
        // CPU-visible buffer, then memcpy the data to the host destination space.
        // Using a cached buffer seems to be crucial for performance!
        void* virtual_gpu_address = reinterpret_cast<void*>(device.allocate_gpu_memory(size, Pal::GpuHeap::GpuHeapGartCacheable));
        copy(dev_src, src, offset_src, dev_src, virtual_gpu_address, 0, size);

        Pal::IGpuMemory* intermediate_mem = device.get_memory_object(virtual_gpu_address);
        pal_utils::read_from_memory(static_cast<uint8_t*>(dst) + offset_dst, intermediate_mem, 0, size);
        release(dev_src, virtual_gpu_address);
    } else {
        pal_utils::read_from_memory(static_cast<uint8_t*>(dst) + offset_dst, src_memory, offset_src, size);
    }
}

Pal::IPipeline* PALPlatform::load_kernel(DeviceId dev, const std::string& filename, const std::string& kernelname) {
    auto& pal_dev = devices_[dev];
    pal_dev.lock();

    Pal::IPipeline* pipeline = nullptr;
    auto canonical = std::filesystem::weakly_canonical(filename);
    auto& prog_cache = pal_dev.programs_;

    // Files can contain multiple kernels, but PAL only accepts a single kernel per pipeline
    // and that kernel must have a fixed name.
    // Therefore, we use the anydsl kernelname for our prog_cache and make sure to rename only that function
    // during jit compilation.
    const std::string kernel_id = canonical.string() + "_" + kernelname;
    if (auto prog_it = prog_cache.find(kernel_id); prog_it == prog_cache.end()) {
        pal_dev.unlock();

        if (canonical.extension() != ".gcn" && canonical.extension() != ".amdgpu")
            error("Incorrect extension for kernel file '%' (should be '.gcn')", canonical.string());

        // Load file from disk or cache.
        const std::string src_code = runtime_->load_file(canonical.string());

        pal_utils::ShaderSrc shader_src = pal_utils::ShaderSrc(canonical, src_code, kernelname);

        // Use elf from file or load it from cache.
        // Important: add kernelname to key to make sure each kernel gets a separate cached file.
        const auto cache_key = devices_[dev].isa + src_code + kernelname;
        std::string gcn = canonical.extension() == ".gcn"
                              ? src_code
                              : runtime_->load_from_cache(cache_key);
        if (gcn.empty()) {
            // Was not an elf file or could not be loaded from cache. Compile source code.
            if (canonical.extension() == ".amdgpu") {
                gcn = compile_gcn(dev, std::move(shader_src));
            }
            // Cache elf.
            runtime_->store_to_cache(cache_key, gcn);
        }

        pipeline = pal_dev.create_pipeline(static_cast<const void*>(gcn.data()), gcn.size());

        pal_dev.lock();
        prog_cache[kernel_id] = pipeline;

    } else {
        pipeline = prog_it->second;
    }

    pal_dev.unlock();

    return pipeline;
}

// Extracts the work group size from the thorin metadata.
std::array<uint64_t, 3> extract_thread_group_dims(const pal_utils::ShaderSrc& shader_src) {
    const char* metadata_key = "reqd_work_group_size";
    std::array<uint64_t, 3> tg_dims;
    assert(shader_src.function->getMetadata(metadata_key)->getNumOperands() == 3 && "Work group size malformed.");

    tg_dims[0] = pal_utils::get_metadata_uint(shader_src.function, metadata_key, 0);
    tg_dims[1] = pal_utils::get_metadata_uint(shader_src.function, metadata_key, 1);
    tg_dims[2] = pal_utils::get_metadata_uint(shader_src.function, metadata_key, 2);
    return tg_dims;
}

#ifdef AnyDSL_runtime_HAS_LLVM_SUPPORT
#ifndef AnyDSL_runtime_PAL_BITCODE_PATH
#define AnyDSL_runtime_PAL_BITCODE_PATH "rocm-device-libs/build/amdgcn/bitcode/"
#endif
#ifndef AnyDSL_runtime_PAL_BITCODE_SUFFIX
#define AnyDSL_runtime_PAL_BITCODE_SUFFIX ".bc"
#endif
bool llvm_amdgpu_pal_initialized = false;
std::string PALPlatform::emit_gcn(pal_utils::ShaderSrc&& shader_src, const std::string& cpu,
    Pal::GfxIpLevel gfx_level, llvm::OptimizationLevel opt) const {
    if (!llvm_amdgpu_pal_initialized) {
        llvm_utils::initialize_amdgpu({"gcn"});
        llvm_amdgpu_pal_initialized = true;
    }

    std::unique_ptr<llvm::TargetMachine> machine =
        llvm_utils::create_target_machine(shader_src.llvm_module->getTargetTriple(), cpu);

    // Override data layout with the one coming from the target machine.
    shader_src.llvm_module->setDataLayout(machine->createDataLayout());

    std::array<uint64_t, 3> tg_dims = extract_thread_group_dims(shader_src);

    if (!shader_src.rename_entry_point())
        error("Could not find a compute shader entry function in the module!");

    // Set up the MsgPack-encoded metadata PAL expects in shader binaries
    const uint32_t pal_wavefront_size = 32; // Use fixed wave32 mode
    llvm::msgpack::Document document =
        pal_utils::build_metadata(shader_src, gfx_level, tg_dims, pal_wavefront_size);

    // Write the MsgPack document into an LLVM-IR metadata node in this specific way
    // because that's what the AMDGPU backend expects.
    std::string blob;
    document.writeToBlob(blob);
    llvm::MDString* abiMetaString = llvm::MDString::get(shader_src.llvm_module->getContext(), blob);
    llvm::MDNode* abiMetaNode = llvm::MDNode::get(shader_src.llvm_module->getContext(), abiMetaString);
    llvm::NamedMDNode* namedMeta = shader_src.llvm_module->getOrInsertNamedMetadata("amdgpu.pal.metadata.msgpack");

    if (namedMeta->getNumOperands() == 0) {
        namedMeta->addOperand(abiMetaNode);
    } else {
        // If the PAL metadata was already written, then we need to replace the previous value.
        assert(namedMeta->getNumOperands() == 1);
        namedMeta->setOperand(0, abiMetaNode);
    }

    // link ocml.amdgcn and ocml config
    llvm::Linker linker(*shader_src.llvm_module.get());
    if (cpu.compare(0, 3, "gfx"))
        error("Expected gfx ISA, got %", cpu);
    std::string isa_version = std::string(&cpu[3]);
    std::string wavefrontsize64 = std::stoi(isa_version) >= 1000 ? "0" : "1";

    std::string ocml_config = R"(; Module anydsl ocml config
                                @__oclc_finite_only_opt = addrspace(4) constant i8 0
                                @__oclc_unsafe_math_opt = addrspace(4) constant i8 0
                                @__oclc_daz_opt = addrspace(4) constant i8 0
                                @__oclc_correctly_rounded_sqrt32 = addrspace(4) constant i8 0
                                @__oclc_wavefrontsize64 = addrspace(4) constant i8 )"
                              + wavefrontsize64;
    llvm_utils::add_module(linker, llvm::MemoryBuffer::getMemBuffer(ocml_config)->getMemBufferRef(),
        machine->createDataLayout(), shader_src.llvm_context, llvm::Linker::Flags::None, "ocml config");

    std::string bitcode_path(AnyDSL_runtime_PAL_BITCODE_PATH);
    std::string bitcode_suffix(AnyDSL_runtime_PAL_BITCODE_SUFFIX);

    std::string ocml_file = bitcode_path + "ocml.ll";
    llvm_utils::add_module(linker, ocml_file, machine->createDataLayout(), shader_src.llvm_context, llvm::Linker::Flags::LinkOnlyNeeded);

    std::string ockl_file = bitcode_path + "ockl" + bitcode_suffix;
    llvm_utils::add_module(linker, ockl_file, machine->createDataLayout(), shader_src.llvm_context, llvm::Linker::Flags::LinkOnlyNeeded);

    std::string isa_file = bitcode_path + "oclc_isa_version_" + isa_version + bitcode_suffix;
    llvm_utils::add_module(linker, isa_file, machine->createDataLayout(), shader_src.llvm_context, llvm::Linker::Flags::LinkOnlyNeeded);

    auto run_pass_manager = [&](std::unique_ptr<llvm::Module> module, llvm::CodeGenFileType cogen_file_type,
                                std::string out_filename, bool print_ir = false) {
        machine->Options.MCOptions.AsmVerbose = true;

        llvm::LoopAnalysisManager LAM;
        llvm::FunctionAnalysisManager FAM;
        llvm::CGSCCAnalysisManager CGAM;
        llvm::ModuleAnalysisManager MAM;

        llvm::PassBuilder PB(machine.get());

        PB.registerModuleAnalyses(MAM);
        PB.registerCGSCCAnalyses(CGAM);
        PB.registerFunctionAnalyses(FAM);
        PB.registerLoopAnalyses(LAM);
        PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

        PB.registerVectorizerStartEPCallback([](llvm::FunctionPassManager& FPM, llvm::OptimizationLevel) {
            FPM.addPass(PalPlatformLowerKernelArgumentsPass());
            FPM.addPass(PalPlatformInsertHaltPass());
        });
        PB.registerPipelineEarlySimplificationEPCallback(
            [gfx_level, tg_dims](llvm::ModulePassManager& MPM, llvm::OptimizationLevel) {
                MPM.addPass(PalPlatformLowerBuiltinsPass(gfx_level, tg_dims));
                MPM.addPass(llvm::GlobalDCEPass());
            });

        // Late insertion to change calling convention of all calls so that the AMDGPU backend doesn't complain
        PB.registerOptimizerLastEPCallback([](llvm::ModulePassManager& MPM, llvm::OptimizationLevel) {
            MPM.addPass(PalPlatformFixCallingConventionPass());
        });

        llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(opt);

        MPM.run(*module, MAM);

        llvm::legacy::PassManager module_pass_manager;
        llvm::SmallString<0> outstr;
        llvm::raw_svector_ostream llvm_stream(outstr);

        machine->addPassesToEmitFile(module_pass_manager, llvm_stream, nullptr, cogen_file_type, true);
        module_pass_manager.run(*module);

        if (print_ir) {
            std::error_code EC;
            llvm::raw_fd_ostream outstream(shader_src.filename + "_final.ll", EC);
            module->print(outstream, nullptr);
        }

        std::string out(outstr.begin(), outstr.end());
        runtime_->store_file(out_filename, out);
    };

    std::string asm_file = shader_src.filename + "_" + shader_src.kernelname + ".asm";
    std::string obj_file = shader_src.filename + ".obj";

    bool print_ir = true;
    if (print_ir)
        run_pass_manager(llvm::CloneModule(*shader_src.llvm_module.get()), llvm::CodeGenFileType::CGFT_AssemblyFile,
            asm_file, print_ir);
    run_pass_manager(std::move(shader_src.llvm_module), llvm::CodeGenFileType::CGFT_ObjectFile, obj_file);

    return runtime_->load_file(obj_file);
}
#else
std::string PALPlatform::emit_gcn(
    const std::string&, const std::string&, Pal::GfxIpLevel, const std::string&, int) const {
    error("Recompile runtime with LLVM enabled for gcn support.");
}
#endif

std::string PALPlatform::compile_gcn(DeviceId dev, pal_utils::ShaderSrc&& shader_src) const {
    debug("Compiling AMDGPU to GCN using amdgpu for kernel '%' in file '%' on PAL device %", shader_src.kernelname,
        shader_src.filename, dev);
    const auto& device = devices_[dev];
    return emit_gcn(std::move(shader_src), device.isa, device.gfx_level, llvm::OptimizationLevel::O3);
}

const char* PALPlatform::device_name(DeviceId dev) const { return devices_[dev].name.c_str(); }

void register_pal_platform(Runtime* runtime) { runtime->register_platform<PALPlatform>(); }
