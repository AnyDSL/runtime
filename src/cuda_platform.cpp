#include "cuda_platform.h"
#include "runtime.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>
#include <sstream>
#include <thread>

#ifdef AnyDSL_runtime_HAS_LLVM_SUPPORT
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Linker/Linker.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/IPO.h>
#endif

#ifdef CUDA_USE_NVPTXCOMPILER_API
#include <nvPTXCompiler.h>
#endif

using namespace std::literals;

#define CHECK_NVVM(err, name)  check_nvvm_errors  (err, name, __FILE__, __LINE__)
#define CHECK_NVRTC(err, name) check_nvrtc_errors (err, name, __FILE__, __LINE__)
#define CHECK_CUDA(err, name)  check_cuda_errors  (err, name, __FILE__, __LINE__)
#ifdef CUDA_USE_NVPTXCOMPILER_API
#define CHECK_NVPTXCOMPILER(err, name) check_nvptxcompiler_errors (err, name, __FILE__, __LINE__)
#endif

#define ANYDSL_CUDA_LIBDEVICE_PATH_ENV "ANYDSL_CUDA_LIBDEVICE_PATH"

inline void check_cuda_errors(CUresult err, const char* name, const char* file, const int line) {
    if (CUDA_SUCCESS != err) {
        const char* error_name;
        const char* error_string;
        cuGetErrorName(err, &error_name);
        cuGetErrorString(err, &error_string);
        auto msg = std::string(error_name) + ": " + std::string(error_string);
        error("Driver API function % (%) [file %, line %]: %", name, err, file, line, msg);
    }
}

inline void check_nvvm_errors(nvvmResult err, const char* name, const char* file, const int line) {
    if (NVVM_SUCCESS != err)
        error("NVVM API function % (%) [file %, line %]: %", name, err, file, line, nvvmGetErrorString(err));
}

inline void check_nvrtc_errors(nvrtcResult err, const char* name, const char* file, const int line) {
    if (NVRTC_SUCCESS != err)
        error("NVRTC API function % (%) [file %, line %]: %", name, err, file, line, nvrtcGetErrorString(err));
}

#ifdef CUDA_USE_NVPTXCOMPILER_API
static const char* _nvptxcompilerGetErrorString(nvPTXCompileResult err) {
    switch (err) {
        case NVPTXCOMPILE_SUCCESS: return "NVPTXCOMPILE_SUCCESS";
        case NVPTXCOMPILE_ERROR_INVALID_COMPILER_HANDLE: return "NVPTXCOMPILE_ERROR_INVALID_COMPILER_HANDLE";
        case NVPTXCOMPILE_ERROR_INVALID_INPUT: return "NVPTXCOMPILE_ERROR_INVALID_INPUT";
        case NVPTXCOMPILE_ERROR_COMPILATION_FAILURE: return "NVPTXCOMPILE_ERROR_COMPILATION_FAILURE";
        case NVPTXCOMPILE_ERROR_INTERNAL: return "NVPTXCOMPILE_ERROR_INTERNAL";
        case NVPTXCOMPILE_ERROR_OUT_OF_MEMORY: return "NVPTXCOMPILE_ERROR_OUT_OF_MEMORY";
        case NVPTXCOMPILE_ERROR_COMPILER_INVOCATION_INCOMPLETE: return "NVPTXCOMPILE_ERROR_COMPILER_INVOCATION_INCOMPLETE";
        case NVPTXCOMPILE_ERROR_UNSUPPORTED_PTX_VERSION: return "NVPTXCOMPILE_ERROR_UNSUPPORTED_PTX_VERSION";
        case NVPTXCOMPILE_ERROR_UNSUPPORTED_DEVSIDE_SYNC: return "NVPTXCOMPILE_ERROR_UNSUPPORTED_DEVSIDE_SYNC";
        default: return "<unknown>";
    }
}

inline void check_nvptxcompiler_errors(nvPTXCompileResult err, const char* name, const char* file, const int line) {
    if (NVPTXCOMPILE_SUCCESS != err)
        error("NVPTXCOMPILER API function % (%) [file %, line %]: %", name, err, file, line, _nvptxcompilerGetErrorString(err));
}
#endif

CudaPlatform::CudaPlatform(Runtime* runtime)
    : Platform(runtime), dump_binaries(std::getenv("ANYDSL_DUMP_CUDA_BINARIES") != nullptr)
{
    int device_count = 0, driver_version = 0, nvrtc_major = 0, nvrtc_minor = 0, nvvm_major = 0, nvvm_minor = 0;

    #ifndef _WIN32
    setenv("CUDA_CACHE_DISABLE", "1", 1);
    #endif

    CUresult err = cuInit(0);
    if (err == CUDA_ERROR_NO_DEVICE) {
        info("CUDA backend did not initialize because no devices were found (CUDA_ERROR_NO_DEVICE).");
        return;
    }
    CHECK_CUDA(err, "cuInit()");

    err = cuDeviceGetCount(&device_count);
    CHECK_CUDA(err, "cuDeviceGetCount()");

    err = cuDriverGetVersion(&driver_version);
    CHECK_CUDA(err, "cuDriverGetVersion()");

    nvvmResult err_nvvm = nvvmVersion(&nvvm_major, &nvvm_minor);
    CHECK_NVVM(err_nvvm, "nvvmVersion()");

    nvrtcResult err_nvrtc = nvrtcVersion(&nvrtc_major, &nvrtc_minor);
    CHECK_NVRTC(err_nvrtc, "nvrtcVersion()");

    debug("CUDA Driver Version %.%", driver_version/1000, (driver_version%100)/10);
    debug("NVRTC Version %.%", nvrtc_major, nvrtc_minor);
    debug("NVVM Version %.%", nvvm_major, nvvm_minor);

    devices_.resize(device_count);

    // initialize devices
    for (int i = 0; i < device_count; ++i) {
        char name[128];
        int minor, major;

        err = cuDeviceGet(&devices_[i].dev, i);
        CHECK_CUDA(err, "cuDeviceGet()");
        err = cuDeviceGetName(name, 128, devices_[i].dev);
        CHECK_CUDA(err, "cuDeviceGetName()");
        err = cuDeviceGetAttribute(&major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, devices_[i].dev);
        CHECK_CUDA(err, "cuDeviceGetAttribute()");
        err = cuDeviceGetAttribute(&minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, devices_[i].dev);
        CHECK_CUDA(err, "cuDeviceGetAttribute()");

        devices_[i].name = name;

        devices_[i].compute_capability = (CUjit_target)(major * 10 + minor);
        debug("  (%) %, Compute capability: %.%", i, name, major, minor);

        err = cuDevicePrimaryCtxRetain(&devices_[i].ctx, devices_[i].dev);
        CHECK_CUDA(err, "cuDevicePrimaryCtxRetain()");
    }
}

void CudaPlatform::erase_profiles(bool erase_all) {
    std::lock_guard<std::mutex> guard(profile_lock_);
    profiles_.remove_if([=] (const ProfileData* profile) {
        cuCtxPushCurrent(profile->ctx);
        auto status = cuEventQuery(profile->end);
        auto erased = erase_all || status == CUDA_SUCCESS;
        if (erased) {
            float time;
            if (status == CUDA_SUCCESS) {
                cuEventElapsedTime(&time, profile->start, profile->end);
                runtime_->kernel_time().fetch_add(time * 1000);
            }
            cuEventDestroy(profile->start);
            cuEventDestroy(profile->end);
            delete profile;
        }
        cuCtxPopCurrent(NULL);
        return erased;
    });
}

CudaPlatform::~CudaPlatform() {
    erase_profiles(true);
    for (size_t i = 0; i < devices_.size(); i++)
        cuDevicePrimaryCtxRelease(devices_[i].dev);
}

void* CudaPlatform::alloc(DeviceId dev, int64_t size) {
    cuCtxPushCurrent(devices_[dev].ctx);

    CUdeviceptr mem;
    CUresult err = cuMemAlloc(&mem, size);
    CHECK_CUDA(err, "cuMemAlloc()");

    cuCtxPopCurrent(NULL);
    return (void*)mem;
}

void* CudaPlatform::alloc_host(DeviceId dev, int64_t size) {
    cuCtxPushCurrent(devices_[dev].ctx);

    void* mem;
    CUresult err = cuMemHostAlloc(&mem, size, CU_MEMHOSTALLOC_DEVICEMAP);
    CHECK_CUDA(err, "cuMemHostAlloc()");

    cuCtxPopCurrent(NULL);
    return mem;
}

void* CudaPlatform::alloc_unified(DeviceId dev, int64_t size) {
    cuCtxPushCurrent(devices_[dev].ctx);

    CUdeviceptr mem;
    CUresult err = cuMemAllocManaged(&mem, size, CU_MEM_ATTACH_GLOBAL);
    CHECK_CUDA(err, "cuMemAllocManaged()");

    cuCtxPopCurrent(NULL);
    return (void*)mem;
}

void* CudaPlatform::get_device_ptr(DeviceId dev, void* ptr) {
    cuCtxPushCurrent(devices_[dev].ctx);

    CUdeviceptr mem;
    CUresult err = cuMemHostGetDevicePointer(&mem, ptr, 0);
    CHECK_CUDA(err, "cuMemHostGetDevicePointer()");

    cuCtxPopCurrent(NULL);
    return (void*)mem;
}

void CudaPlatform::release(DeviceId dev, void* ptr) {
    cuCtxPushCurrent(devices_[dev].ctx);
    CUresult err = cuMemFree((CUdeviceptr)ptr);
    CHECK_CUDA(err, "cuMemFree()");
    cuCtxPopCurrent(NULL);
}

void CudaPlatform::release_host(DeviceId dev, void* ptr) {
    cuCtxPushCurrent(devices_[dev].ctx);
    CUresult err = cuMemFreeHost(ptr);
    CHECK_CUDA(err, "cuMemFreeHost()");
    cuCtxPopCurrent(NULL);
}

void CudaPlatform::launch_kernel(DeviceId dev, const LaunchParams& launch_params) {
    cuCtxPushCurrent(devices_[dev].ctx);

    auto func = load_kernel(dev, launch_params.file_name, launch_params.kernel_name);

    CUevent start, end;
    if (runtime_->profiling_enabled()) {
        erase_profiles(false);
        CHECK_CUDA(cuEventCreate(&start, CU_EVENT_DEFAULT), "cuEventCreate()");
        CHECK_CUDA(cuEventRecord(start, 0), "cuEventRecord()");
    }

    CUresult err = cuLaunchKernel(func,
        launch_params.grid[0] / launch_params.block[0],
        launch_params.grid[1] / launch_params.block[1],
        launch_params.grid[2] / launch_params.block[2],
        launch_params.block[0], launch_params.block[1], launch_params.block[2],
        0, nullptr, launch_params.args.data, nullptr);
    CHECK_CUDA(err, "cuLaunchKernel()");

    if (runtime_->profiling_enabled()) {
        CHECK_CUDA(cuEventCreate(&end, CU_EVENT_DEFAULT), "cuEventCreate()");
        CHECK_CUDA(cuEventRecord(end, 0), "cuEventRecord()");
        profiles_.push_front(new ProfileData { this, devices_[dev].ctx, start, end });
    }
    cuCtxPopCurrent(NULL);
}

void CudaPlatform::synchronize(DeviceId dev) {
    auto& cuda_dev = devices_[dev];
    cuCtxPushCurrent(cuda_dev.ctx);

    CUresult err = cuCtxSynchronize();
    CHECK_CUDA(err, "cuCtxSynchronize()");

    cuCtxPopCurrent(NULL);
    erase_profiles(false);
}

void CudaPlatform::copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
    assert(dev_src == dev_dst);
    unused(dev_dst);

    cuCtxPushCurrent(devices_[dev_src].ctx);

    CUdeviceptr src_mem = (CUdeviceptr)src;
    CUdeviceptr dst_mem = (CUdeviceptr)dst;
    CUresult err = cuMemcpyDtoD(dst_mem + offset_dst, src_mem + offset_src, size);
    CHECK_CUDA(err, "cuMemcpyDtoD()");

    cuCtxPopCurrent(NULL);
}

void CudaPlatform::copy_from_host(const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
    cuCtxPushCurrent(devices_[dev_dst].ctx);

    CUdeviceptr dst_mem = (CUdeviceptr)dst;

    CUresult err = cuMemcpyHtoD(dst_mem + offset_dst, (char*)src + offset_src, size);
    CHECK_CUDA(err, "cuMemcpyHtoD()");

    cuCtxPopCurrent(NULL);
}

void CudaPlatform::copy_to_host(DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) {
    cuCtxPushCurrent(devices_[dev_src].ctx);

    CUdeviceptr src_mem = (CUdeviceptr)src;
    CUresult err = cuMemcpyDtoH((char*)dst + offset_dst, src_mem + offset_src, size);
    CHECK_CUDA(err, "cuMemcpyDtoH()");

    cuCtxPopCurrent(NULL);
}

CUfunction CudaPlatform::load_kernel(DeviceId dev, const std::string& filename, const std::string& kernelname) {
    auto& cuda_dev = devices_[dev];

    // lock the device when the function cache is accessed
    cuda_dev.lock();

    CUmodule mod;
    auto canonical = std::filesystem::weakly_canonical(filename);
    auto& mod_cache = cuda_dev.modules;
    auto mod_it = mod_cache.find(canonical.string());
    if (mod_it == mod_cache.end()) {
        cuda_dev.unlock();

        bool use_nvptx = true;

        if (canonical.extension() != ".ptx" && canonical.extension() != ".cu" && canonical.extension() != ".nvvm")
            error("Incorrect extension for kernel file '%' (should be '.ptx', '.cu', or '.nvvm')", canonical.string());

        // load file from disk or cache
        auto src_path = canonical;
        if (src_path.extension() == ".nvvm" && !use_nvptx)
            src_path.replace_extension(".nvvm.bc");
        std::string src_code = runtime_->load_file(src_path.string());

        // compile src or load from cache
        std::string compute_capability_str = std::to_string(devices_[dev].compute_capability);
        std::string ptx = canonical.extension() == ".ptx" ? src_code : runtime_->load_from_cache(compute_capability_str + src_code);
        if (ptx.empty()) {
            if (canonical.extension() == ".cu") {
                ptx = compile_cuda(dev, src_path.string(), src_code);
            } else if (canonical.extension() == ".nvvm") {
                if (use_nvptx)
                    ptx = compile_nvptx(dev, src_path.string(), src_code);
                else
                    ptx = compile_nvvm(dev, src_path.string(), src_code);
            }
            runtime_->store_to_cache(compute_capability_str + src_code, ptx);
        }

        mod = create_module(dev, src_path.string(), ptx);

        cuda_dev.lock();
        mod_cache[canonical.string()] = mod;
    } else {
        mod = mod_it->second;
    }

    // checks that the function exists
    auto& func_cache = devices_[dev].functions;
    auto& func_map = func_cache[mod];
    auto func_it = func_map.find(kernelname);

    CUfunction func;
    if (func_it == func_map.end()) {
        cuda_dev.unlock();

        CUresult err = cuModuleGetFunction(&func, mod, kernelname.c_str());
        if (err != CUDA_SUCCESS)
            info("Function '%' is not present in '%'", kernelname, filename);
        CHECK_CUDA(err, "cuModuleGetFunction()");
        int regs, cmem, lmem, smem, threads;
        err = cuFuncGetAttribute(&regs, CU_FUNC_ATTRIBUTE_NUM_REGS, func);
        CHECK_CUDA(err, "cuFuncGetAttribute()");
        err = cuFuncGetAttribute(&smem, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, func);
        CHECK_CUDA(err, "cuFuncGetAttribute()");
        err = cuFuncGetAttribute(&cmem, CU_FUNC_ATTRIBUTE_CONST_SIZE_BYTES, func);
        CHECK_CUDA(err, "cuFuncGetAttribute()");
        err = cuFuncGetAttribute(&lmem, CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES, func);
        CHECK_CUDA(err, "cuFuncGetAttribute()");
        err = cuFuncGetAttribute(&threads, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, func);
        CHECK_CUDA(err, "cuFuncGetAttribute()");
        debug("Function '%' using % registers, % | % | % bytes shared | constant | local memory allowing up to % threads per block", kernelname, regs, smem, cmem, lmem, threads);

        cuda_dev.lock();
        func_cache[mod][kernelname] = func;
    } else {
        func = func_it->second;
    }

    cuda_dev.unlock();

    return func;
}

#ifdef AnyDSL_runtime_HAS_LLVM_SUPPORT
bool llvm_nvptx_initialized = false;
static std::string emit_nvptx(const std::string& program, const std::string& cpu, const std::string& filename, llvm::OptimizationLevel opt_level) {
    if (!llvm_nvptx_initialized) {
        // ANYDSL_LLVM_ARGS="-nvptx-sched4reg -nvptx-fma-level=2 -nvptx-prec-divf32=0 -nvptx-prec-sqrtf32=0 -nvptx-f32ftz=1"
        const char* env_var = std::getenv("ANYDSL_LLVM_ARGS");
        if (env_var) {
            std::vector<const char*> c_llvm_args;
            std::vector<std::string> llvm_args = { "nvptx" };
            std::istringstream stream(env_var);
            std::string tmp;
            while (stream >> tmp)
                llvm_args.push_back(tmp);
            for (auto &str : llvm_args)
                c_llvm_args.push_back(str.c_str());
            llvm::cl::ParseCommandLineOptions(c_llvm_args.size(), c_llvm_args.data(), "AnyDSL nvptx JIT compiler\n");
        }

        LLVMInitializeNVPTXTarget();
        LLVMInitializeNVPTXTargetInfo();
        LLVMInitializeNVPTXTargetMC();
        LLVMInitializeNVPTXAsmPrinter();
        llvm_nvptx_initialized = true;
    }

    llvm::LLVMContext llvm_context;
    llvm::SMDiagnostic diagnostic_err;
    std::unique_ptr<llvm::Module> llvm_module = llvm::parseIR(llvm::MemoryBuffer::getMemBuffer(program)->getMemBufferRef(), diagnostic_err, llvm_context);

    if (!llvm_module) {
        std::string stream;
        llvm::raw_string_ostream llvm_stream(stream);
        diagnostic_err.print("", llvm_stream);
        error("Parsing IR file %: %", filename, llvm_stream.str());
    }

    auto triple_str = llvm_module->getTargetTriple();
    std::string error_str;
    auto target = llvm::TargetRegistry::lookupTarget(triple_str, error_str);
    llvm::TargetOptions options;
    options.AllowFPOpFusion = llvm::FPOpFusion::Fast;
    llvm::TargetMachine* machine = target->createTargetMachine(triple_str, cpu, "" /* attrs */, options, llvm::Reloc::PIC_, llvm::CodeModel::Small, llvm::CodeGenOptLevel::Aggressive);

    // link libdevice
    const char* env_libdevice_path = std::getenv(ANYDSL_CUDA_LIBDEVICE_PATH_ENV);
    if (!env_libdevice_path)
        env_libdevice_path = AnyDSL_runtime_LIBDEVICE_LIB;

    std::unique_ptr<llvm::Module> libdevice_module(llvm::parseIRFile(env_libdevice_path, diagnostic_err, llvm_context));
    if (libdevice_module == nullptr)
        error("Can't create libdevice module for '%'", env_libdevice_path);

    // override data layout with the one coming from the target machine
    llvm_module->setDataLayout(machine->createDataLayout());
    libdevice_module->setDataLayout(machine->createDataLayout());
    libdevice_module->setTargetTriple(triple_str);

    llvm::Linker linker(*llvm_module.get());
    if (linker.linkInModule(std::move(libdevice_module), llvm::Linker::Flags::LinkOnlyNeeded))
        error("Can't link libdevice into module");



    machine->Options.MCOptions.AsmVerbose = true;

    // create the analysis managers
    llvm::LoopAnalysisManager LAM;
    llvm::FunctionAnalysisManager FAM;
    llvm::CGSCCAnalysisManager CGAM;
    llvm::ModuleAnalysisManager MAM;

    llvm::PassBuilder PB(machine);

    PB.registerModuleAnalyses(MAM);
    PB.registerCGSCCAnalyses(CGAM);
    PB.registerFunctionAnalyses(FAM);
    PB.registerLoopAnalyses(LAM);
    PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

    llvm::ModulePassManager MPM = PB.buildPerModuleDefaultPipeline(opt_level);

    MPM.run(*llvm_module, MAM);

    llvm::legacy::PassManager module_pass_manager;
    llvm::SmallString<0> outstr;
    llvm::raw_svector_ostream llvm_stream(outstr);
    machine->addPassesToEmitFile(module_pass_manager, llvm_stream, nullptr, llvm::CodeGenFileType::AssemblyFile, true);
    module_pass_manager.run(*llvm_module);

    return outstr.c_str();
}

std::string CudaPlatform::compile_nvptx(DeviceId dev, const std::string& filename, const std::string& program_string) const {
    debug("Compiling NVVM to PTX using NVPTX for '%' on CUDA device %", filename, dev);
    std::string cpu = "sm_" + std::to_string(devices_[dev].compute_capability);
    return emit_nvptx(program_string, cpu, filename, llvm::OptimizationLevel::O3);
}
#else
std::string CudaPlatform::compile_nvptx(DeviceId dev, const std::string& filename, const std::string& program_string) const {
    error("Recompile runtime with LLVM enabled for nvptx support.");
    return std::string{};
}
#endif

std::string CudaPlatform::compile_nvvm(DeviceId dev, const std::string& filename, const std::string& program_string) const {
    nvvmProgram program;
    nvvmResult err = nvvmCreateProgram(&program);
    CHECK_NVVM(err, "nvvmCreateProgram()");

    std::string libdevice_string = runtime_->load_file(AnyDSL_runtime_LIBDEVICE_LIB);
    err = nvvmLazyAddModuleToProgram(program, libdevice_string.c_str(), libdevice_string.length(), AnyDSL_runtime_LIBDEVICE_LIB);
    CHECK_NVVM(err, "nvvmAddModuleToProgram()");

    err = nvvmAddModuleToProgram(program, program_string.c_str(), program_string.length(), filename.c_str());
    CHECK_NVVM(err, "nvvmAddModuleToProgram()");

    std::string compute_arch("-arch=compute_" + std::to_string(devices_[dev].compute_capability));
    int num_options = 3;
    const char* options[] = {
        compute_arch.c_str(),
        "-opt=3",
        "-generate-line-info",
        "-ftz=1",
        "-prec-div=0",
        "-prec-sqrt=0",
        "-fma=1",
        "-g" };

    err = nvvmVerifyProgram(program, num_options, options);
    if (err != NVVM_SUCCESS) {
        size_t log_size;
        nvvmGetProgramLogSize(program, &log_size);
        std::string error_log(log_size, '\0');
        nvvmGetProgramLog(program, &error_log[0]);
        info("Verify program error: %", error_log);
    }

    debug("Compiling NVVM to PTX using libNVVM for '%' on CUDA device %", filename, dev);
    err = nvvmCompileProgram(program, num_options, options);
    if (err != NVVM_SUCCESS) {
        size_t log_size;
        nvvmGetProgramLogSize(program, &log_size);
        std::string error_log(log_size, '\0');
        nvvmGetProgramLog(program, &error_log[0]);
        info("Compilation error: %", error_log);
    }
    CHECK_NVVM(err, "nvvmCompileProgram()");

    size_t ptx_size;
    err = nvvmGetCompiledResultSize(program, &ptx_size);
    CHECK_NVVM(err, "nvvmGetCompiledResultSize()");

    std::string ptx(ptx_size, '\0');
    err = nvvmGetCompiledResult(program, &ptx[0]);
    CHECK_NVVM(err, "nvvmGetCompiledResult()");

    err = nvvmDestroyProgram(&program);
    CHECK_NVVM(err, "nvvmDestroyProgram()");

    return ptx;
}

#define MAKE_NVCC_LANGUAGE_DIALECT_FLAG(version) "-std=c++" #version
#define MAKE_NVCC_LANGUAGE_DIALECT_FLAG_HELPER(version) MAKE_NVCC_LANGUAGE_DIALECT_FLAG(version)
#define NVCC_LANGUAGE_DIALECT_FLAG MAKE_NVCC_LANGUAGE_DIALECT_FLAG_HELPER(AnyDSL_runtime_CUDA_CXX_STANDARD)

#ifndef AnyDSL_runtime_NVCC_INC
#define AnyDSL_runtime_NVCC_INC "/usr/local/cuda/include"
#endif
std::string CudaPlatform::compile_cuda(DeviceId dev, const std::string& filename, const std::string& program_string) const {
    nvrtcProgram program;
    nvrtcResult err = nvrtcCreateProgram(&program, program_string.c_str(), filename.c_str(), 0, NULL, NULL);
    CHECK_NVRTC(err, "nvrtcCreateProgram()");

    std::string compute_arch("-arch=compute_" + std::to_string(devices_[dev].compute_capability));
    int num_options = 5;
    const char* options[] = {
        compute_arch.c_str(),
        "-I",
        AnyDSL_runtime_NVCC_INC,
        "-lineinfo",
        NVCC_LANGUAGE_DIALECT_FLAG,
        "-G" };

    debug("Compiling CUDA to PTX using NVRTC for '%' on CUDA device %", filename, dev);
    err = nvrtcCompileProgram(program, num_options, options);
    if (err != NVRTC_SUCCESS) {
        size_t log_size;
        nvrtcGetProgramLogSize(program, &log_size);
        std::string error_log(log_size, '\0');
        nvrtcGetProgramLog(program, &error_log[0]);
        info("Compilation error: %", error_log);
    }
    CHECK_NVRTC(err, "nvrtcCompileProgram()");

    size_t ptx_size;
    err = nvrtcGetPTXSize(program, &ptx_size);
    CHECK_NVRTC(err, "nvrtcGetPTXSize()");

    std::string ptx(ptx_size, '\0');
    err = nvrtcGetPTX(program, &ptx[0]);
    CHECK_NVRTC(err, "nvrtcGetPTX()");

    err = nvrtcDestroyProgram(&program);
    CHECK_NVRTC(err, "nvrtcDestroyProgram()");

    auto address_pos = ptx.find(".address_size");
    if (address_pos != std::string::npos)
        ptx.insert(ptx.find('\n', address_pos), "\n\n.extern .func (.param .s32 status) vprintf (.param .b64 format, .param .b64 valist);\n");

    return ptx;
}

CUmodule CudaPlatform::create_module(DeviceId dev, const std::string& filename, const std::string& ptx_string) const {
    CUmodule mod;

#ifdef CUDA_USE_NVPTXCOMPILER_API
    const unsigned int opt_level = 3;

    debug("Creating module from PTX '%' on CUDA device % using nvPTXCompiler", filename, dev);

    nvPTXCompilerHandle handle;
    nvPTXCompileResult err = nvPTXCompilerCreate(&handle, ptx_string.length(), const_cast<char*>(ptx_string.c_str()));
    CHECK_NVPTXCOMPILER(err, "nvPTXCompilerCreate()");

    std::string sm_arch("-arch=sm_" + std::to_string(devices_[dev].compute_capability));
    std::string opt_str("-O" + std::to_string(opt_level));
    const char* options[] = {
        sm_arch.c_str(),
        opt_str.c_str(),
    };
    int num_options = std::size(options);

    err = nvPTXCompilerCompile(handle, num_options, options);
    size_t info_log_size;
    size_t error_log_size;
    nvPTXCompilerGetInfoLogSize(handle, &info_log_size);
    nvPTXCompilerGetErrorLogSize(handle, &error_log_size);
    if (info_log_size) {
        std::string info_log(info_log_size, '\0');
        nvPTXCompilerGetInfoLog(handle, &info_log[0]);
        info("Compilation info: %", info_log);
    }
    if (error_log_size) {
        std::string error_log(error_log_size, '\0');
        nvPTXCompilerGetErrorLog(handle, &error_log[0]);
        info("Compilation error: %", error_log);
    }
    CHECK_NVPTXCOMPILER(err, "nvPTXCompilerCompile()");

    size_t binary_size;
    CHECK_NVPTXCOMPILER(nvPTXCompilerGetCompiledProgramSize(handle, &binary_size), "nvPTXCompilerGetCompiledProgramSize()");

    std::string binary(binary_size, '\0');
    CHECK_NVPTXCOMPILER(nvPTXCompilerGetCompiledProgram(handle, &binary[0]), "nvPTXCompilerGetCompiledProgram()");

    if (dump_binaries) {
        auto cubin_name = filename + ".sm_" + std::to_string(devices_[dev].compute_capability) + ".cubin";
        runtime_->store_file(cubin_name, reinterpret_cast<const std::byte*>(binary.c_str()), binary_size);
    }

    CHECK_CUDA(cuModuleLoadData(&mod, &binary[0]), "cuModuleLoadData()");

    CHECK_NVPTXCOMPILER(nvPTXCompilerDestroy(&handle), "nvPTXCompilerDestroy()");
#else
    const unsigned int opt_level = 4;
    const unsigned int error_log_size = 10240;
    const unsigned int info_log_size = 10240;
    const unsigned int num_options = 4;
    char error_log_buffer[error_log_size] = { 0 };
    char info_log_buffer[info_log_size] = { 0 };

    CUjit_option options[] = { CU_JIT_INFO_LOG_BUFFER, CU_JIT_INFO_LOG_BUFFER_SIZE_BYTES, CU_JIT_ERROR_LOG_BUFFER, CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES, CU_JIT_TARGET, CU_JIT_OPTIMIZATION_LEVEL };
    void* option_values[]  = { (void*)info_log_buffer, (void*)info_log_size, (void*)error_log_buffer, (void*)error_log_size, (void*)devices_[dev].compute_capability, (void*)opt_level };

    debug("Creating module from PTX '%' on CUDA device % using CUDA driver PTX compiler", filename, dev);
    CUresult err = cuModuleLoadDataEx(&mod, ptx_string.c_str(), num_options, options, option_values);
    if (strnlen_s(info_log_buffer, info_log_size))
        info("Compilation info: %", info_log_buffer);
    if (strnlen_s(error_log_buffer, error_log_size))
        info("Compilation error: %", error_log_buffer);
    CHECK_CUDA(err, "cuModuleLoadDataEx()");
#endif

    return mod;
}

const char* CudaPlatform::device_name(DeviceId dev) const {
    return devices_[dev].name.c_str();
}

bool CudaPlatform::device_check_feature_support(DeviceId dev, const char* feature) const {
    if (feature == "ITS"sv)
        return static_cast<int>(devices_[dev].compute_capability) >= 70;
    return false;
}

void register_cuda_platform(Runtime* runtime) {
    runtime->register_platform<CudaPlatform>();
}
