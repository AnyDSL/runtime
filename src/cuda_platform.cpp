#include "cuda_platform.h"
#include "runtime.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <sstream>
#include <thread>

#ifdef RUNTIME_ENABLE_JIT
#include <llvm/Analysis/TargetTransformInfo.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>
#endif

#ifndef LIBDEVICE_DIR
#define LIBDEVICE_DIR ""
#endif

#define CHECK_NVVM(err, name)  check_nvvm_errors  (err, name, __FILE__, __LINE__)
#define CHECK_NVRTC(err, name) check_nvrtc_errors (err, name, __FILE__, __LINE__)
#define CHECK_CUDA(err, name)  check_cuda_errors  (err, name, __FILE__, __LINE__)

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

#ifdef CUDA_NVRTC
inline void check_nvrtc_errors(nvrtcResult err, const char* name, const char* file, const int line) {
    if (NVRTC_SUCCESS != err)
        error("NVRTC API function % (%) [file %, line %]: %", name, err, file, line, nvrtcGetErrorString(err));
}
#endif

extern std::atomic<uint64_t> anydsl_kernel_time;

CudaPlatform::CudaPlatform(Runtime* runtime)
    : Platform(runtime)
{
    int device_count = 0, driver_version = 0, nvvm_major = 0, nvvm_minor = 0;

    #ifndef _WIN32
    setenv("CUDA_CACHE_DISABLE", "1", 1);
    #endif

    CUresult err = cuInit(0);
    CHECK_CUDA(err, "cuInit()");

    err = cuDeviceGetCount(&device_count);
    CHECK_CUDA(err, "cuDeviceGetCount()");

    err = cuDriverGetVersion(&driver_version);
    CHECK_CUDA(err, "cuDriverGetVersion()");

    nvvmResult errNvvm = nvvmVersion(&nvvm_major, &nvvm_minor);
    CHECK_NVVM(errNvvm, "nvvmVersion()");

    debug("CUDA Driver Version %.%", driver_version/1000, (driver_version%100)/10);
    #ifdef CUDA_NVRTC
    int nvrtc_major = 0, nvrtc_minor = 0;
    nvrtcResult errNvrtc = nvrtcVersion(&nvrtc_major, &nvrtc_minor);
    CHECK_NVRTC(errNvrtc, "nvrtcVersion()");
    debug("NVRTC Version %.%", nvrtc_major, nvrtc_minor);
    #endif
    debug("NVVM Version %.%", nvvm_major, nvvm_minor);

    devices_.resize(device_count);

    // initialize devices
    for (int i = 0; i < device_count; ++i) {
        char name[128];

        err = cuDeviceGet(&devices_[i].dev, i);
        CHECK_CUDA(err, "cuDeviceGet()");
        err = cuDeviceGetName(name, 128, devices_[i].dev);
        CHECK_CUDA(err, "cuDeviceGetName()");
        err = cuDeviceGetAttribute(&devices_[i].compute_major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, devices_[i].dev);
        CHECK_CUDA(err, "cuDeviceGetAttribute()");
        err = cuDeviceGetAttribute(&devices_[i].compute_minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, devices_[i].dev);
        CHECK_CUDA(err, "cuDeviceGetAttribute()");

        debug("  (%) %, Compute capability: %.%", i, name, devices_[i].compute_major, devices_[i].compute_minor);

        err = cuCtxCreate(&devices_[i].ctx, CU_CTX_MAP_HOST, devices_[i].dev);
        CHECK_CUDA(err, "cuCtxCreate()");
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
                anydsl_kernel_time.fetch_add(time * 1000);
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
        cuCtxDestroy(devices_[i].ctx);
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

void CudaPlatform::register_file(const std::string& filename, const std::string& program_string) {
    files_[filename] = program_string;
}

void CudaPlatform::launch_kernel(DeviceId dev,
                                 const char* file, const char* kernel,
                                 const uint32_t* grid, const uint32_t* block,
                                 void** args, const uint32_t*, const KernelArgType*,
                                 uint32_t) {
    cuCtxPushCurrent(devices_[dev].ctx);

    auto func = load_kernel(dev, file, kernel);

    CUevent start, end;
    if (runtime_->profiling_enabled()) {
        erase_profiles(false);
        CHECK_CUDA(cuEventCreate(&start, CU_EVENT_DEFAULT), "cuEventCreate()");
        CHECK_CUDA(cuEventRecord(start, 0), "cuEventRecord()");
    }

    assert(grid[0] > 0 && grid[0] % block[0] == 0 &&
           grid[1] > 0 && grid[1] % block[1] == 0 &&
           grid[2] > 0 && grid[2] % block[2] == 0 &&
           "The grid size is not a multiple of the block size");

    CUresult err = cuLaunchKernel(func,
        grid[0] / block[0],
        grid[1] / block[1],
        grid[2] / block[2],
        block[0], block[1], block[2],
        0, nullptr, args, nullptr);
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

CUfunction CudaPlatform::load_kernel(DeviceId dev, const std::string& file, const std::string& kernelname) {
    auto& cuda_dev = devices_[dev];

    // lock the device when the function cache is accessed
    cuda_dev.lock();

    CUmodule mod;
    auto& mod_cache = cuda_dev.modules;
    auto mod_it = mod_cache.find(file);
    if (mod_it == mod_cache.end()) {
        cuda_dev.unlock();

        CUjit_target target_cc =
            (CUjit_target)(cuda_dev.compute_major * 10 +
                           cuda_dev.compute_minor);

        // find the file extension
        auto ext_pos = file.rfind('.');
        std::string ext = ext_pos != std::string::npos ? file.substr(ext_pos + 1) : "";
        if (ext != "ptx" && ext != "cu" && ext != "nvvm")
            error("Incorrect extension for kernel file '%' (should be '.ptx', '.cu', or '.nvvm')", file);

        // compile the given file
        std::string ptx;
        std::string filename = file;
        if (std::ifstream(file + ".ptx").good()) {
            filename += ".ptx";
            ptx = load_file(filename);
        } else if (ext == "ptx" && (std::ifstream(file).good() || files_.count(file))) {
            ptx = load_file(filename);
        } else if (ext == "cu" && (std::ifstream(file).good() || files_.count(file))) {
            ptx = compile_cuda(dev, filename, load_file(filename), target_cc);
        } else if (ext == "nvvm" && (std::ifstream(file).good() || files_.count(file))) {
            ptx = compile_nvptx(dev, filename, load_file(filename), target_cc);
        } else if (ext == "nvvm" && (std::ifstream(file + ".bc").good() || files_.count(file))) {
            filename += ".bc";
            ptx = compile_nvvm(dev, filename, load_file(filename), target_cc);
        } else {
            error("Cannot find kernel file '%'", file);
        }
        mod = create_module(dev, filename, ptx, target_cc);

        cuda_dev.lock();
        mod_cache[file] = mod;
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
            info("Function '%' is not present in '%'", kernelname, file);
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

std::string CudaPlatform::load_file(const std::string& filename) const {
    auto file_it = files_.find(filename);
    if (file_it != files_.end())
        return file_it->second;

    std::ifstream src_file(filename);
    if (!src_file.is_open())
        error("Can't open source file '%'", filename);

    return std::string(std::istreambuf_iterator<char>(src_file), (std::istreambuf_iterator<char>()));
}

void CudaPlatform::store_file(const std::string& filename, const std::string& str) const {
    std::ofstream dst_file(filename);
    if (!dst_file)
        error("Can't open destination file '%'", filename);
    dst_file << str;
    dst_file.close();
}

std::string get_libdevice_filename(CUjit_target target_cc) {
    std::string libdevice_filename = "libdevice.10.bc";

    #if CUDA_VERSION < 9000
    // select libdevice module according to documentation
    if (target_cc < 30)
        libdevice_filename = "libdevice.compute_20.10.bc";
    else if (target_cc == 30)
        libdevice_filename = "libdevice.compute_30.10.bc";
    else if (target_cc <  35)
        libdevice_filename = "libdevice.compute_20.10.bc";
    else if (target_cc <= 37)
        libdevice_filename = "libdevice.compute_35.10.bc";
    else if (target_cc <  50)
        libdevice_filename = "libdevice.compute_30.10.bc";
    else if (target_cc <= 53)
        libdevice_filename = "libdevice.compute_50.10.bc";
    else
        libdevice_filename = "libdevice.compute_30.10.bc";
    #endif

    return libdevice_filename;
}

std::string get_libdevice_path(CUjit_target target_cc) {
    return std::string(LIBDEVICE_DIR) + get_libdevice_filename(target_cc);
}

#ifdef RUNTIME_ENABLE_JIT
static std::string emit_nvptx(const std::string& program, const std::string& libdevice_file, const std::string& cpu, int opt) {
    LLVMInitializeNVPTXTarget();
    LLVMInitializeNVPTXTargetInfo();
    LLVMInitializeNVPTXTargetMC();
    LLVMInitializeNVPTXAsmPrinter();

    llvm::LLVMContext llvm_context;
    llvm::SMDiagnostic diagnostic_err;
    std::unique_ptr<llvm::Module> llvm_module = llvm::parseIR(llvm::MemoryBuffer::getMemBuffer(program)->getMemBufferRef(), diagnostic_err, llvm_context);

    auto triple_str = llvm_module->getTargetTriple();
    std::string error_str;
    auto target = llvm::TargetRegistry::lookupTarget(triple_str, error_str);
    llvm::TargetOptions options;
    options.AllowFPOpFusion = llvm::FPOpFusion::Fast;
    std::unique_ptr<llvm::TargetMachine> machine(target->createTargetMachine(triple_str, cpu, "" /* attrs */, options, llvm::Reloc::PIC_, llvm::CodeModel::Default, llvm::CodeGenOpt::Aggressive));

    // link libdevice
    std::unique_ptr<llvm::Module> libdevice_module(llvm::parseIRFile(libdevice_file, diagnostic_err, llvm_context));
    if (libdevice_module == nullptr)
        error("Can't create libdevice module for '%'", libdevice_file);

    llvm::Linker linker(*llvm_module.get());
    if (linker.linkInModule(std::move(libdevice_module), llvm::Linker::Flags::LinkOnlyNeeded))
        error("Can't link libdevice into module");

    llvm_module->addModuleFlag(llvm::Module::Override, "nvvm-reflect-ftz", 1);
    for (auto &fun : *llvm_module)
        fun.addFnAttr("nvptx-f32ftz", "true");
    llvm::legacy::FunctionPassManager function_pass_manager(llvm_module.get());
    llvm::legacy::PassManager module_pass_manager;

    module_pass_manager.add(llvm::createTargetTransformInfoWrapperPass(machine->getTargetIRAnalysis()));
    function_pass_manager.add(llvm::createTargetTransformInfoWrapperPass(machine->getTargetIRAnalysis()));

    llvm::PassManagerBuilder builder;
    builder.OptLevel = opt;
    builder.Inliner = llvm::createFunctionInliningPass(builder.OptLevel, 0, false);
    machine->adjustPassManager(builder);
    builder.populateFunctionPassManager(function_pass_manager);
    builder.populateModulePassManager(module_pass_manager);

    machine->Options.MCOptions.AsmVerbose = true;

    llvm::SmallString<0> outstr;
    llvm::raw_svector_ostream llvm_stream(outstr);

    machine->addPassesToEmitFile(module_pass_manager, llvm_stream, llvm::TargetMachine::CGFT_AssemblyFile, true);

    function_pass_manager.doInitialization();
    for (auto func = llvm_module->begin(); func != llvm_module->end(); ++func)
        function_pass_manager.run(*func);
    function_pass_manager.doFinalization();
    module_pass_manager.run(*llvm_module);
    return outstr.c_str();
}
#else
static std::string emit_nvptx(const std::string&, const std::string&, const std::string&, int) {
    error("Recompile runtime with RUNTIME_JIT enabled for nvptx support.");
}
#endif

std::string CudaPlatform::compile_nvptx(DeviceId dev, const std::string& filename, const std::string& program_string, CUjit_target target_cc) const {
    debug("Compiling NVVM to PTX using nvptx for '%' on CUDA device %", filename, dev);
    std::string cpu = "sm_" + std::to_string(target_cc);
    return emit_nvptx(program_string, get_libdevice_path(target_cc), cpu, 3);
}

std::string CudaPlatform::compile_nvvm(DeviceId dev, const std::string& filename, const std::string& program_string, CUjit_target target_cc) const {
    nvvmProgram program;
    nvvmResult err = nvvmCreateProgram(&program);
    CHECK_NVVM(err, "nvvmCreateProgram()");

    std::string libdevice_string = load_file(get_libdevice_path(target_cc));
    err = nvvmAddModuleToProgram(program, libdevice_string.c_str(), libdevice_string.length(), get_libdevice_filename(target_cc).c_str());
    CHECK_NVVM(err, "nvvmAddModuleToProgram()");

    err = nvvmAddModuleToProgram(program, program_string.c_str(), program_string.length(), filename.c_str());
    CHECK_NVVM(err, "nvvmAddModuleToProgram()");

    std::string compute_arch("-arch=compute_" + std::to_string(target_cc));
    int num_options = 2;
    const char* options[] = {
        compute_arch.c_str(),
        "-opt=3",
        "-g",
        "-generate-line-info" };

    debug("Compiling NVVM to PTX for '%' on CUDA device %", filename, dev);
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

#ifdef CUDA_NVRTC
#ifndef NVCC_INC
#define NVCC_INC "/usr/local/cuda/include"
#endif
std::string CudaPlatform::compile_cuda(DeviceId dev, const std::string& filename, const std::string& program_string, CUjit_target target_cc) const {
    nvrtcProgram program;
    nvrtcResult err = nvrtcCreateProgram(&program, program_string.c_str(), filename.c_str(), 0, NULL, NULL);
    CHECK_NVRTC(err, "nvrtcCreateProgram()");

    std::string compute_arch("-arch=compute_" + std::to_string(target_cc));
    int num_options = 3;
    const char* options[] = {
        compute_arch.c_str(),
        "-I",
        NVCC_INC,
        "-G",
        "-lineinfo" };

    debug("Compiling CUDA to PTX for '%' on CUDA device %", filename, dev);
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

    return ptx;
}
#else
#ifndef NVCC_BIN
#define NVCC_BIN "nvcc"
#endif
std::string CudaPlatform::compile_cuda(DeviceId dev, const std::string& filename, const std::string& program_string, CUjit_target target_cc) const {
    #if CUDA_VERSION < 9000
    target_cc = target_cc == CU_TARGET_COMPUTE_21 ? CU_TARGET_COMPUTE_20 : target_cc; // compute_21 does not exist for nvcc
    #endif
    std::string ptx_filename = std::string(filename) + ".ptx";
    std::string command = (NVCC_BIN " -O4 -ptx -arch=compute_") + std::to_string(target_cc) + " ";
    command += filename + " -o " + ptx_filename + " 2>&1";

    if (!program_string.empty())
        store_file(filename, program_string);

    debug("Compiling CUDA to PTX for '%' on CUDA device %", filename, dev);
    if (auto stream = popen(command.c_str(), "r")) {
        std::string log;
        char buffer[256];

        while (fgets(buffer, 256, stream))
            log += buffer;

        int exit_status = pclose(stream);

        if (WEXITSTATUS(exit_status))
            error("Compilation error: %", log);
        if (!log.empty())
            info("%", log);
    } else {
        error("Cannot run NVCC");
    }

    return load_file(ptx_filename);
}
#endif

CUmodule CudaPlatform::create_module(DeviceId dev, const std::string& filename, const std::string& ptx_string, CUjit_target target_cc) const {
    const unsigned int opt_level = 4;
    const unsigned int error_log_size = 10240;
    const unsigned int num_options = 4;
    char error_log_buffer[error_log_size] = { 0 };

    CUjit_option options[] = { CU_JIT_ERROR_LOG_BUFFER, CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES, CU_JIT_TARGET, CU_JIT_OPTIMIZATION_LEVEL };
    void* option_values[]  = { (void*)error_log_buffer, (void*)error_log_size, (void*)target_cc, (void*)opt_level };

    debug("Creating module from PTX '%' on CUDA device %", filename, dev);
    CUmodule mod;
    CUresult err = cuModuleLoadDataEx(&mod, ptx_string.c_str(), num_options, options, option_values);
    if (err != CUDA_SUCCESS)
        info("Compilation error: %", error_log_buffer);
    CHECK_CUDA(err, "cuModuleLoadDataEx()");

    return mod;
}
