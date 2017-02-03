#include "cuda_platform.h"
#include "runtime.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>

#ifndef LIBDEVICE_DIR
#define LIBDEVICE_DIR ""
#endif
#ifndef KERNEL_DIR
#define KERNEL_DIR ""
#endif

#define checkErrNvvm(err, name)  checkNvvmErrors  (err, name, __FILE__, __LINE__)
#define checkErrNvrtc(err, name) checkNvrtcErrors (err, name, __FILE__, __LINE__)
#define checkErrDrv(err, name)   checkCudaErrors  (err, name, __FILE__, __LINE__)

inline std::string cudaErrorString(CUresult errorCode) {
    const char* error_name;
    const char* error_string;
    cuGetErrorName(errorCode, &error_name);
    cuGetErrorString(errorCode, &error_string);
    return std::string(error_name) + ": " + std::string(error_string);
}

void CudaPlatform::checkCudaErrors(CUresult err, const char* name, const char* file, const int line) {
    if (CUDA_SUCCESS != err)
        error("Driver API function % (%) [file %, line %]: %", name, err, file, line, cudaErrorString(err));
}

void CudaPlatform::checkNvvmErrors(nvvmResult err, const char* name, const char* file, const int line) {
    if (NVVM_SUCCESS != err)
        error("NVVM API function % (%) [file %, line %]: %", name, err, file, line, nvvmGetErrorString(err));
}

#ifdef CUDA_NVRTC
void CudaPlatform::checkNvrtcErrors(nvrtcResult err, const char* name, const char* file, const int line) {
    if (NVRTC_SUCCESS != err)
        error("NVRTC API function % (%) [file %, line %]: %", name, err, file, line, nvrtcGetErrorString(err));
}
#endif

static thread_local CUevent start_kernel = nullptr;
static thread_local CUevent end_kernel = nullptr;
extern std::atomic_llong anydsl_kernel_time;

CudaPlatform::CudaPlatform(Runtime* runtime)
    : Platform(runtime)
{
    int device_count = 0, driver_version = 0, nvvm_major = 0, nvvm_minor = 0;

    #ifndef _WIN32
    setenv("CUDA_CACHE_DISABLE", "1", 1);
    #endif

    CUresult err = cuInit(0);
    checkErrDrv(err, "cuInit()");

    err = cuDeviceGetCount(&device_count);
    checkErrDrv(err, "cuDeviceGetCount()");

    err = cuDriverGetVersion(&driver_version);
    checkErrDrv(err, "cuDriverGetVersion()");

    nvvmResult errNvvm = nvvmVersion(&nvvm_major, &nvvm_minor);
    checkErrNvvm(errNvvm, "nvvmVersion()");

    debug("CUDA Driver Version %.%", driver_version/1000, (driver_version%100)/10);
    #ifdef CUDA_NVRTC
    int nvrtc_major = 0, nvrtc_minor = 0;
    nvrtcResult errNvrtc = nvrtcVersion(&nvrtc_major, &nvrtc_minor);
    checkErrNvrtc(errNvrtc, "nvrtcVersion()");
    debug("NVRTC Version %.%", nvrtc_major, nvrtc_minor);
    #endif
    debug("NVVM Version %.%", nvvm_major, nvvm_minor);

    devices_.resize(device_count);

    // Initialize devices
    for (int i = 0; i < device_count; ++i) {
        char name[128];

        err = cuDeviceGet(&devices_[i].dev, i);
        checkErrDrv(err, "cuDeviceGet()");
        err = cuDeviceGetName(name, 128, devices_[i].dev);
        checkErrDrv(err, "cuDeviceGetName()");
        err = cuDeviceGetAttribute(&devices_[i].compute_major, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MAJOR, devices_[i].dev);
        checkErrDrv(err, "cuDeviceGetAttribute()");
        err = cuDeviceGetAttribute(&devices_[i].compute_minor, CU_DEVICE_ATTRIBUTE_COMPUTE_CAPABILITY_MINOR, devices_[i].dev);
        checkErrDrv(err, "cuDeviceGetAttribute()");

        debug("  (%) %, Compute capability: %.%", i, name, devices_[i].compute_major, devices_[i].compute_minor);

        err = cuCtxCreate(&devices_[i].ctx, CU_CTX_MAP_HOST, devices_[i].dev);
        checkErrDrv(err, "cuCtxCreate()");
    }
}

CudaPlatform::~CudaPlatform() {
    for (size_t i = 0; i < devices_.size(); i++) {
        cuCtxDestroy(devices_[i].ctx);
    }
}

void* CudaPlatform::alloc(DeviceId dev, int64_t size) {
    cuCtxPushCurrent(devices_[dev].ctx);

    CUdeviceptr mem;
    CUresult err = cuMemAlloc(&mem, size);
    checkErrDrv(err, "cuMemAlloc()");

    cuCtxPopCurrent(NULL);
    return (void*)mem;
}

void* CudaPlatform::alloc_host(DeviceId dev, int64_t size) {
    cuCtxPushCurrent(devices_[dev].ctx);

    void* mem;
    CUresult err = cuMemHostAlloc(&mem, size, CU_MEMHOSTALLOC_DEVICEMAP);
    checkErrDrv(err, "cuMemHostAlloc()");

    cuCtxPopCurrent(NULL);
    return mem;
}

void* CudaPlatform::alloc_unified(DeviceId dev, int64_t size) {
    cuCtxPushCurrent(devices_[dev].ctx);

    CUdeviceptr mem;
    CUresult err = cuMemAllocManaged(&mem, size, CU_MEM_ATTACH_GLOBAL);
    checkErrDrv(err, "cuMemAllocManaged()");

    cuCtxPopCurrent(NULL);
    return (void*)mem;
}

void* CudaPlatform::get_device_ptr(DeviceId dev, void* ptr) {
    cuCtxPushCurrent(devices_[dev].ctx);

    CUdeviceptr mem;
    CUresult err = cuMemHostGetDevicePointer(&mem, ptr, 0);
    checkErrDrv(err, "cuMemHostGetDevicePointer()");

    cuCtxPopCurrent(NULL);
    return (void*)mem;
}

void CudaPlatform::release(DeviceId dev, void* ptr) {
    cuCtxPushCurrent(devices_[dev].ctx);
    CUresult err = cuMemFree((CUdeviceptr)ptr);
    checkErrDrv(err, "cuMemFree()");
    cuCtxPopCurrent(NULL);
}

void CudaPlatform::release_host(DeviceId dev, void* ptr) {
    cuCtxPushCurrent(devices_[dev].ctx);
    CUresult err = cuMemFreeHost(ptr);
    checkErrDrv(err, "cuMemFreeHost()");
    cuCtxPopCurrent(NULL);
}

void CudaPlatform::launch_kernel(DeviceId dev,
                                 const char* file, const char* kernel,
                                 const uint32_t* grid, const uint32_t* block,
                                 void** args, const uint32_t*, const KernelArgType*,
                                 uint32_t) {
    auto& cuda_dev = devices_[dev];
    auto& mod_cache = cuda_dev.modules;
    auto mod_it = mod_cache.find(file);

    cuCtxPushCurrent(cuda_dev.ctx);

    // Lock the device mutex when the function cache is accessed
    cuda_dev.mutex.lock();

    CUmodule mod;
    if (mod_it == mod_cache.end()) {
        CUjit_target target_cc =
            (CUjit_target)(cuda_dev.compute_major * 10 +
                           cuda_dev.compute_minor);

        // Compile the given file
        if (std::ifstream(std::string(file) + ".nvvm.bc").good()) {
             compile_nvvm(dev, file, target_cc);
        } else if (std::ifstream(std::string(file) + ".ptx").good()) {
            create_module(dev, file, target_cc, load_ptx(file).c_str());
        } else if (std::ifstream(std::string(file) + ".cu").good()) {
            compile_cuda(dev, file, target_cc);
        } else {
            error("Could not find kernel file '%'.[nvvm.bc|ptx|cu]", file);
        }

        mod = mod_cache[file];
    } else {
        mod = mod_it->second;
    }

    // Checks that the function exists
    auto& func_cache = devices_[dev].functions;
    auto& func_map = func_cache[mod];
    auto func_it = func_map.find(kernel);

    CUfunction func;
    if (func_it == func_map.end()) {
        CUresult err = cuModuleGetFunction(&func, mod, kernel);
        if (err != CUDA_SUCCESS)
            debug("Function '%' is not present in '%'", kernel, file);
        checkErrDrv(err, "cuModuleGetFunction()");
        func_map.emplace(kernel, func);
        int regs, cmem, lmem, smem, threads;
        err = cuFuncGetAttribute(&regs, CU_FUNC_ATTRIBUTE_NUM_REGS, func);
        checkErrDrv(err, "cuFuncGetAttribute()");
        err = cuFuncGetAttribute(&smem, CU_FUNC_ATTRIBUTE_SHARED_SIZE_BYTES, func);
        checkErrDrv(err, "cuFuncGetAttribute()");
        err = cuFuncGetAttribute(&cmem, CU_FUNC_ATTRIBUTE_CONST_SIZE_BYTES, func);
        checkErrDrv(err, "cuFuncGetAttribute()");
        err = cuFuncGetAttribute(&lmem, CU_FUNC_ATTRIBUTE_LOCAL_SIZE_BYTES, func);
        checkErrDrv(err, "cuFuncGetAttribute()");
        err = cuFuncGetAttribute(&threads, CU_FUNC_ATTRIBUTE_MAX_THREADS_PER_BLOCK, func);
        checkErrDrv(err, "cuFuncGetAttribute()");
        debug("Function '%' using % registers, % | % | % bytes shared | constant | local memory allowing up to % threads per block", kernel, regs, smem, cmem, lmem, threads);
    } else {
        func = func_it->second;
    }

    cuda_dev.mutex.unlock();

    if (!start_kernel) checkErrDrv(cuEventCreate(&start_kernel, CU_EVENT_DEFAULT), "cuEventCreate()");
    if (!end_kernel)   checkErrDrv(cuEventCreate(&end_kernel,   CU_EVENT_DEFAULT), "cuEventCreate()");

    checkErrDrv(cuEventRecord(start_kernel, 0), "cuEventRecord()");

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
    checkErrDrv(err, "cuLaunchKernel()");

    checkErrDrv(cuEventRecord(end_kernel, 0), "cuEventRecord()");
    cuCtxPopCurrent(NULL);
}

void CudaPlatform::synchronize(DeviceId dev) {
    if (!end_kernel) return;

    auto& cuda_dev = devices_[dev];
    cuCtxPushCurrent(cuda_dev.ctx);

    float time;
    CUresult err = cuEventSynchronize(end_kernel);
    checkErrDrv(err, "cuEventSynchronize()");

    cuEventElapsedTime(&time, start_kernel, end_kernel);
    anydsl_kernel_time.fetch_add(time * 1000);

    err = cuCtxSynchronize();
    checkErrDrv(err, "cuCtxSynchronize()");
    cuCtxPopCurrent(NULL);
}

void CudaPlatform::copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
    assert(dev_src == dev_dst);

    cuCtxPushCurrent(devices_[dev_src].ctx);

    CUdeviceptr src_mem = (CUdeviceptr)src;
    CUdeviceptr dst_mem = (CUdeviceptr)dst;
    CUresult err = cuMemcpyDtoD(dst_mem + offset_dst, src_mem + offset_src, size);
    checkErrDrv(err, "cuMemcpyDtoD()");

    cuCtxPopCurrent(NULL);
}

void CudaPlatform::copy_from_host(const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
    cuCtxPushCurrent(devices_[dev_dst].ctx);

    CUdeviceptr dst_mem = (CUdeviceptr)dst;

    CUresult err = cuMemcpyHtoD(dst_mem + offset_dst, (char*)src + offset_src, size);
    checkErrDrv(err, "cuMemcpyHtoD()");

    cuCtxPopCurrent(NULL);
}

void CudaPlatform::copy_to_host(DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) {
    cuCtxPushCurrent(devices_[dev_src].ctx);

    CUdeviceptr src_mem = (CUdeviceptr)src;
    CUresult err = cuMemcpyDtoH((char*)dst + offset_dst, src_mem + offset_src, size);
    checkErrDrv(err, "cuMemcpyDtoH()");

    cuCtxPopCurrent(NULL);
}

int CudaPlatform::dev_count() {
    return devices_.size();
}

std::string CudaPlatform::load_ptx(const std::string& filename) {
    std::string ptx_filename = filename + ".ptx";
    std::ifstream src_file(ptx_filename);
    if (!src_file.is_open())
        error("Can't open PTX source file '%'", ptx_filename);

    return std::string(std::istreambuf_iterator<char>(src_file), (std::istreambuf_iterator<char>()));
}

void CudaPlatform::compile_nvvm(DeviceId dev, const std::string& filename, CUjit_target target_cc) {
    // Select libdevice module according to documentation
    std::string libdevice_filename;
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

    std::ifstream libdevice_file(std::string(LIBDEVICE_DIR) + libdevice_filename);
    if (!libdevice_file.is_open())
        error("Can't open libdevice source file '%'", libdevice_filename);

    std::string libdevice_string = std::string(std::istreambuf_iterator<char>(libdevice_file), (std::istreambuf_iterator<char>()));

    std::string nvvm_filename = filename + ".nvvm.bc";
    std::ifstream src_file(std::string(KERNEL_DIR) + nvvm_filename);
    if (!src_file.is_open())
        error("Can't open NVVM source file '%/%'", KERNEL_DIR, nvvm_filename);

    std::string src_string = std::string(std::istreambuf_iterator<char>(src_file), (std::istreambuf_iterator<char>()));

    nvvmProgram program;
    nvvmResult err = nvvmCreateProgram(&program);
    checkErrNvvm(err, "nvvmCreateProgram()");

    err = nvvmAddModuleToProgram(program, libdevice_string.c_str(), libdevice_string.length(), libdevice_filename.c_str());
    checkErrNvvm(err, "nvvmAddModuleToProgram()");

    err = nvvmAddModuleToProgram(program, src_string.c_str(), src_string.length(), nvvm_filename.c_str());
    checkErrNvvm(err, "nvvmAddModuleToProgram()");

    std::string compute_arch("-arch=compute_" + std::to_string(target_cc));
    int num_options = 2;
    const char* options[3];
    options[0] = compute_arch.c_str();
    options[1] = "-opt=3";
    options[2] = "-g";

    debug("Compiling NVVM to PTX for '%' on CUDA device %", filename, dev);
    err = nvvmCompileProgram(program, num_options, options);
    if (err != NVVM_SUCCESS) {
        size_t log_size;
        nvvmGetProgramLogSize(program, &log_size);
        std::string error_log(log_size, '\0');
        nvvmGetProgramLog(program, &error_log[0]);
        error("Compilation error: %", error_log);
    }
    checkErrNvvm(err, "nvvmCompileProgram()");

    size_t ptx_size;
    err = nvvmGetCompiledResultSize(program, &ptx_size);
    checkErrNvvm(err, "nvvmGetCompiledResultSize()");

    std::string ptx(ptx_size, '\0');
    err = nvvmGetCompiledResult(program, &ptx[0]);
    checkErrNvvm(err, "nvvmGetCompiledResult()");

    err = nvvmDestroyProgram(&program);
    checkErrNvvm(err, "nvvmDestroyProgram()");

    create_module(dev, filename, target_cc, ptx.c_str());
}

#ifdef CUDA_NVRTC
#ifndef NVCC_INC
#define NVCC_INC "/usr/local/cuda/include"
#endif
void CudaPlatform::compile_cuda(DeviceId dev, const std::string& filename, CUjit_target target_cc) {
    std::string cuda_filename = filename + ".cu";
    std::ifstream src_file(std::string(KERNEL_DIR) + cuda_filename);
    if (!src_file.is_open())
        error("Can't open CUDA source file '%/%'", KERNEL_DIR, cuda_filename);

    std::string src_string = std::string(std::istreambuf_iterator<char>(src_file), (std::istreambuf_iterator<char>()));

    nvrtcProgram program;
    nvrtcResult err = nvrtcCreateProgram(&program, src_string.c_str(), cuda_filename.c_str(), 0, NULL, NULL);
    checkErrNvrtc(err, "nvrtcCreateProgram()");

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
        error("Compilation error: %", error_log);
    }
    checkErrNvrtc(err, "nvrtcCompileProgram()");

    size_t ptx_size;
    err = nvrtcGetPTXSize(program, &ptx_size);
    checkErrNvrtc(err, "nvrtcGetPTXSize()");

    std::string ptx(ptx_size, '\0');
    err = nvrtcGetPTX(program, &ptx[0]);
    checkErrNvrtc(err, "nvrtcGetPTX()");

    err = nvrtcDestroyProgram(&program);
    checkErrNvrtc(err, "nvrtcDestroyProgram()");

    create_module(dev, filename, target_cc, ptx.c_str());
}
#else
#ifndef NVCC_BIN
#define NVCC_BIN "nvcc"
#endif
void CudaPlatform::compile_cuda(DeviceId dev, const std::string& filename, CUjit_target target_cc) {
    target_cc = target_cc == CU_TARGET_COMPUTE_21 ? CU_TARGET_COMPUTE_20 : target_cc; // compute_21 does not exist for nvcc
    std::string cuda_filename = std::string(filename) + ".cu";
    std::string ptx_filename = std::string(cuda_filename) + ".ptx";
    std::string command = (NVCC_BIN " -O4 -ptx -arch=compute_") + std::to_string(target_cc) + " ";
    command += std::string(KERNEL_DIR) + cuda_filename + " -o " + ptx_filename + " 2>&1";

    debug("Compiling CUDA to PTX for '%' on CUDA device %", filename, dev);
    if (auto stream = popen(command.c_str(), "r")) {
        std::string log;
        char buffer[256];

        while (fgets(buffer, 256, stream))
            log += buffer;

        int exit_status = pclose(stream);
        if (!WEXITSTATUS(exit_status)) {
            info("%", log);
        } else {
            error("Compilation error: %", log);
        }
    } else {
        error("Cannot run NVCC");
    }

    create_module(dev, filename, target_cc, load_ptx(cuda_filename).c_str());
}
#endif

void CudaPlatform::create_module(DeviceId dev, const std::string& filename, CUjit_target target_cc, const void* ptx) {
    const unsigned opt_level = 3;
    const int error_log_size = 10240;
    const int num_options = 4;
    char error_log_buffer[error_log_size] = { 0 };

    CUjit_option options[] = { CU_JIT_ERROR_LOG_BUFFER, CU_JIT_ERROR_LOG_BUFFER_SIZE_BYTES, CU_JIT_TARGET, CU_JIT_OPTIMIZATION_LEVEL };
    void* option_values[]  = { (void*)error_log_buffer, (void*)(size_t)error_log_size, (void*)target_cc, (void*)(size_t)opt_level };

    debug("Creating module from PTX '%' on CUDA device %", filename, dev);
    CUmodule mod;
    CUresult err = cuModuleLoadDataEx(&mod, ptx, num_options, options, option_values);
    checkErrDrv(err, "cuModuleLoadDataEx()");

    devices_[dev].modules[filename] = mod;
}
