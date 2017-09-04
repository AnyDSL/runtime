#include "opencl_platform.h"
#include "runtime.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <string>

#ifndef KERNEL_DIR
#define KERNEL_DIR ""
#endif

static std::string get_opencl_error_code_str(int error) {
    #define CL_ERROR_CODE(CODE) case CODE: return #CODE;
    switch (error) {
        CL_ERROR_CODE(CL_SUCCESS)
        CL_ERROR_CODE(CL_DEVICE_NOT_FOUND)
        CL_ERROR_CODE(CL_DEVICE_NOT_AVAILABLE)
        CL_ERROR_CODE(CL_COMPILER_NOT_AVAILABLE)
        CL_ERROR_CODE(CL_MEM_OBJECT_ALLOCATION_FAILURE)
        CL_ERROR_CODE(CL_OUT_OF_RESOURCES)
        CL_ERROR_CODE(CL_OUT_OF_HOST_MEMORY)
        CL_ERROR_CODE(CL_PROFILING_INFO_NOT_AVAILABLE)
        CL_ERROR_CODE(CL_MEM_COPY_OVERLAP)
        CL_ERROR_CODE(CL_IMAGE_FORMAT_MISMATCH)
        CL_ERROR_CODE(CL_IMAGE_FORMAT_NOT_SUPPORTED)
        CL_ERROR_CODE(CL_BUILD_PROGRAM_FAILURE)
        CL_ERROR_CODE(CL_MAP_FAILURE)
        #ifdef CL_VERSION_1_1
        CL_ERROR_CODE(CL_MISALIGNED_SUB_BUFFER_OFFSET)
        CL_ERROR_CODE(CL_EXEC_STATUS_ERROR_FOR_EVENTS_IN_WAIT_LIST)
        #endif
        #ifdef CL_VERSION_1_2
        CL_ERROR_CODE(CL_COMPILE_PROGRAM_FAILURE)
        CL_ERROR_CODE(CL_LINKER_NOT_AVAILABLE)
        CL_ERROR_CODE(CL_LINK_PROGRAM_FAILURE)
        CL_ERROR_CODE(CL_DEVICE_PARTITION_FAILED)
        CL_ERROR_CODE(CL_KERNEL_ARG_INFO_NOT_AVAILABLE)
        #endif
        CL_ERROR_CODE(CL_INVALID_VALUE)
        CL_ERROR_CODE(CL_INVALID_DEVICE_TYPE)
        CL_ERROR_CODE(CL_INVALID_PLATFORM)
        CL_ERROR_CODE(CL_INVALID_DEVICE)
        CL_ERROR_CODE(CL_INVALID_CONTEXT)
        CL_ERROR_CODE(CL_INVALID_QUEUE_PROPERTIES)
        CL_ERROR_CODE(CL_INVALID_COMMAND_QUEUE)
        CL_ERROR_CODE(CL_INVALID_HOST_PTR)
        CL_ERROR_CODE(CL_INVALID_MEM_OBJECT)
        CL_ERROR_CODE(CL_INVALID_IMAGE_FORMAT_DESCRIPTOR)
        CL_ERROR_CODE(CL_INVALID_IMAGE_SIZE)
        CL_ERROR_CODE(CL_INVALID_SAMPLER)
        CL_ERROR_CODE(CL_INVALID_BINARY)
        CL_ERROR_CODE(CL_INVALID_BUILD_OPTIONS)
        CL_ERROR_CODE(CL_INVALID_PROGRAM)
        CL_ERROR_CODE(CL_INVALID_PROGRAM_EXECUTABLE)
        CL_ERROR_CODE(CL_INVALID_KERNEL_NAME)
        CL_ERROR_CODE(CL_INVALID_KERNEL_DEFINITION)
        CL_ERROR_CODE(CL_INVALID_KERNEL)
        CL_ERROR_CODE(CL_INVALID_ARG_INDEX)
        CL_ERROR_CODE(CL_INVALID_ARG_VALUE)
        CL_ERROR_CODE(CL_INVALID_ARG_SIZE)
        CL_ERROR_CODE(CL_INVALID_KERNEL_ARGS)
        CL_ERROR_CODE(CL_INVALID_WORK_DIMENSION)
        CL_ERROR_CODE(CL_INVALID_WORK_GROUP_SIZE)
        CL_ERROR_CODE(CL_INVALID_WORK_ITEM_SIZE)
        CL_ERROR_CODE(CL_INVALID_GLOBAL_OFFSET)
        CL_ERROR_CODE(CL_INVALID_EVENT_WAIT_LIST)
        CL_ERROR_CODE(CL_INVALID_EVENT)
        CL_ERROR_CODE(CL_INVALID_OPERATION)
        CL_ERROR_CODE(CL_INVALID_GL_OBJECT)
        CL_ERROR_CODE(CL_INVALID_BUFFER_SIZE)
        CL_ERROR_CODE(CL_INVALID_MIP_LEVEL)
        CL_ERROR_CODE(CL_INVALID_GLOBAL_WORK_SIZE)
        #ifdef CL_VERSION_1_1
        CL_ERROR_CODE(CL_INVALID_PROPERTY)
        #endif
        #ifdef CL_VERSION_1_2
        CL_ERROR_CODE(CL_INVALID_IMAGE_DESCRIPTOR)
        CL_ERROR_CODE(CL_INVALID_COMPILER_OPTIONS)
        CL_ERROR_CODE(CL_INVALID_LINKER_OPTIONS)
        CL_ERROR_CODE(CL_INVALID_DEVICE_PARTITION_COUNT)
        #endif
        #ifdef CL_VERSION_2_0
        CL_ERROR_CODE(CL_INVALID_PIPE_SIZE)
        CL_ERROR_CODE(CL_INVALID_DEVICE_QUEUE)
        #endif
        default: return "unknown error code";
    }
    #undef CL_ERROR_CODE
}

#define CHECK_OPENCL(err, name)  check_opencl_error(err, name, __FILE__, __LINE__)

inline void check_opencl_error(cl_int err, const char* name, const char* file, const int line) {
    if (err != CL_SUCCESS)
        error("OpenCL API function % (%) [file %, line %]: %", name, err, file, line, get_opencl_error_code_str(err));
}

OpenCLPlatform::OpenCLPlatform(Runtime* runtime)
    : Platform(runtime)
{
    // get OpenCL platform count
    cl_uint num_platforms, num_devices;
    cl_int err = clGetPlatformIDs(0, NULL, &num_platforms);
    CHECK_OPENCL(err, "clGetPlatformIDs()");

    debug("Number of available OpenCL Platforms: %", num_platforms);

    cl_platform_id* platforms = new cl_platform_id[num_platforms];

    err = clGetPlatformIDs(num_platforms, platforms, NULL);
    CHECK_OPENCL(err, "clGetPlatformIDs()");

    // get platform info for each platform
    for (size_t i=0; i<num_platforms; ++i) {
        auto platform = platforms[i];

        char buffer[1024];
        err  = clGetPlatformInfo(platforms[i], CL_PLATFORM_NAME, sizeof(buffer), &buffer, NULL);
        debug("  Platform Name: %", buffer);
        err |= clGetPlatformInfo(platforms[i], CL_PLATFORM_VENDOR, sizeof(buffer), &buffer, NULL);
        debug("  Platform Vendor: %", buffer);
        err |= clGetPlatformInfo(platforms[i], CL_PLATFORM_VERSION, sizeof(buffer), &buffer, NULL);
        debug("  Platform Version: %", buffer);
        CHECK_OPENCL(err, "clGetPlatformInfo()");

        err = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, 0, NULL, &num_devices);
        CHECK_OPENCL(err, "clGetDeviceIDs()");

        cl_device_id* devices = new cl_device_id[num_devices];
        err = clGetDeviceIDs(platforms[i], CL_DEVICE_TYPE_ALL, num_devices, devices, &num_devices);
        CHECK_OPENCL(err, "clGetDeviceIDs()");

        // get device info for each device
        for (size_t j=0; j<num_devices; ++j) {
            auto device = devices[j];
            cl_device_type dev_type;
            cl_uint device_vendor_id;
            cl_uint cl_version_major, cl_version_minor;

            unused(cl_version_major, cl_version_minor);

            err  = clGetDeviceInfo(devices[j], CL_DEVICE_NAME, sizeof(buffer), &buffer, NULL);
            err |= clGetDeviceInfo(devices[j], CL_DEVICE_TYPE, sizeof(dev_type), &dev_type, NULL);
            std::string type_str;
            if (dev_type & CL_DEVICE_TYPE_CPU)         type_str  = "CL_DEVICE_TYPE_CPU";
            if (dev_type & CL_DEVICE_TYPE_GPU)         type_str  = "CL_DEVICE_TYPE_GPU";
            if (dev_type & CL_DEVICE_TYPE_ACCELERATOR) type_str  = "CL_DEVICE_TYPE_ACCELERATOR";
            #ifdef CL_VERSION_1_2
            if (dev_type & CL_DEVICE_TYPE_CUSTOM)      type_str  = "CL_DEVICE_TYPE_CUSTOM";
            #endif
            if (dev_type & CL_DEVICE_TYPE_DEFAULT)     type_str += "|CL_DEVICE_TYPE_DEFAULT";
            debug("  (%) Device Name: % (%)", devices_.size(), buffer, type_str);
            err |= clGetDeviceInfo(devices[j], CL_DEVICE_VENDOR, sizeof(buffer), &buffer, NULL);
            err |= clGetDeviceInfo(devices[j], CL_DEVICE_VENDOR_ID, sizeof(device_vendor_id), &device_vendor_id, NULL);
            debug("      Device Vendor: % (ID: %)", buffer, device_vendor_id);
            err |= clGetDeviceInfo(devices[j], CL_DEVICE_VERSION, sizeof(buffer), &buffer, NULL);
            debug("      Device OpenCL Version: %", buffer);
            std::string version(buffer);
            size_t sz;
            cl_version_major = std::stoi(version.substr(7), &sz);
            cl_version_minor = std::stoi(version.substr(7 + sz + 1));
            err |= clGetDeviceInfo(devices[j], CL_DRIVER_VERSION, sizeof(buffer), &buffer, NULL);
            debug("      Device Driver Version: %", buffer);

            std::string svm_caps_str = "none";
            #ifdef CL_VERSION_2_0
            if (cl_version_major >= 2) {
                cl_device_svm_capabilities svm_caps;
                err |= clGetDeviceInfo(devices[j], CL_DEVICE_SVM_CAPABILITIES, sizeof(svm_caps), &svm_caps, NULL);
                if (svm_caps & CL_DEVICE_SVM_COARSE_GRAIN_BUFFER) svm_caps_str = "CL_DEVICE_SVM_COARSE_GRAIN_BUFFER";
                if (svm_caps & CL_DEVICE_SVM_FINE_GRAIN_BUFFER)   svm_caps_str += " CL_DEVICE_SVM_FINE_GRAIN_BUFFER";
                if (svm_caps & CL_DEVICE_SVM_FINE_GRAIN_SYSTEM)   svm_caps_str += " CL_DEVICE_SVM_FINE_GRAIN_SYSTEM";
                if (svm_caps & CL_DEVICE_SVM_ATOMICS)             svm_caps_str += " CL_DEVICE_SVM_ATOMICS";
            }
            #endif
            debug("      Device SVM capabilities: %", svm_caps_str);

            cl_bool has_unified = false;
            err |= clGetDeviceInfo(devices[j], CL_DEVICE_HOST_UNIFIED_MEMORY, sizeof(has_unified), &has_unified, NULL);
            debug("      Device Host Unified Memory: %", has_unified ? "yes" : "no");
            CHECK_OPENCL(err, "clGetDeviceInfo()");

            auto dev = devices_.size();
            devices_.resize(dev + 1);
            devices_[dev].platform = platform;
            devices_[dev].dev = device;

            // create context
            cl_context_properties ctx_props[3] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0 };
            devices_[dev].ctx = clCreateContext(ctx_props, 1, &devices_[dev].dev, NULL, NULL, &err);
            CHECK_OPENCL(err, "clCreateContext()");

            // create command queue
            devices_[dev].queue = NULL;
            #ifdef CL_VERSION_2_0
            if (cl_version_major >= 2) {
                cl_queue_properties queue_props[] = runtime_->profiling_enabled() ?
                                                    { CL_QUEUE_PROPERTIES, CL_QUEUE_PROFILING_ENABLE, 0 } : { 0 };
                devices_[dev].queue = clCreateCommandQueueWithProperties(devices_[dev].ctx, devices_[dev].dev, queue_props, &err);
                CHECK_OPENCL(err, "clCreateCommandQueueWithProperties()");
            }
            #endif
            if (!devices_[dev].queue) {
                cl_command_queue_properties queue_props = 0;
                if (runtime_->profiling_enabled())
                    queue_props = CL_QUEUE_PROFILING_ENABLE;
                devices_[dev].queue = clCreateCommandQueue(devices_[dev].ctx, devices_[dev].dev, queue_props, &err);
                CHECK_OPENCL(err, "clCreateCommandQueue()");
            }
        }
        delete[] devices;
    }
    delete[] platforms;
}

OpenCLPlatform::~OpenCLPlatform() {
    for (size_t i = 0; i < devices_.size(); i++) {
        for (auto& map : devices_[i].kernels) {
            for (auto& it : map.second) {
                cl_int err = clReleaseKernel(it.second);
                CHECK_OPENCL(err, "clReleaseKernel()");
            }
        }
        for (auto& it : devices_[i].programs) {
            cl_int err = clReleaseProgram(it.second);
            CHECK_OPENCL(err, "clReleaseProgram()");
        }
    }
}

void* OpenCLPlatform::alloc(DeviceId dev, int64_t size) {
    if (!size) return 0;

    cl_int err = CL_SUCCESS;
    cl_mem_flags flags = CL_MEM_READ_WRITE;
    cl_mem mem = clCreateBuffer(devices_[dev].ctx, flags, size, NULL, &err);
    CHECK_OPENCL(err, "clCreateBuffer()");

    return (void*)mem;
}

void OpenCLPlatform::release(DeviceId, void* ptr) {
    cl_int err = clReleaseMemObject((cl_mem)ptr);
    CHECK_OPENCL(err, "clReleaseMemObject()");
}

static thread_local cl_event end_kernel;
extern std::atomic<uint64_t> anydsl_kernel_time;

void OpenCLPlatform::launch_kernel(DeviceId dev,
                                   const char* file, const char* name,
                                   const uint32_t* grid, const uint32_t* block,
                                   void** args, const uint32_t* sizes, const KernelArgType* types,
                                   uint32_t num_args) {
    auto kernel = load_kernel(dev, file, name);

    // set up arguments
    std::vector<cl_mem> kernel_structs(num_args);
    for (size_t i = 0; i < num_args; i++) {
        if (types[i] == KernelArgType::Struct) {
            // create a buffer for each structure argument
            cl_int err = CL_SUCCESS;
            cl_mem_flags flags = CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR;
            cl_mem struct_buf = clCreateBuffer(devices_[dev].ctx, flags, sizes[i], args[i], &err);
            CHECK_OPENCL(err, "clCreateBuffer()");
            kernel_structs[i] = struct_buf;
            clSetKernelArg(kernel, i, sizeof(cl_mem), &kernel_structs[i]);
        } else {
            cl_int err = clSetKernelArg(kernel, i, types[i] == KernelArgType::Ptr ? sizeof(cl_mem) : sizes[i], args[i]);
            CHECK_OPENCL(err, "clSetKernelArg()");
        }
    }

    size_t global_work_size[] = {grid [0], grid [1], grid [2]};
    size_t local_work_size[]  = {block[0], block[1], block[2]};

    // launch the kernel
    cl_int err = clEnqueueNDRangeKernel(devices_[dev].queue, kernel, 2, NULL, global_work_size, local_work_size, 0, NULL, &end_kernel);
    CHECK_OPENCL(err, "clEnqueueNDRangeKernel()");

    // release temporary buffers for struct arguments
    for (size_t i = 0; i < num_args; i++) {
        if (types[i] == KernelArgType::Struct) {
            cl_int err = clReleaseMemObject(kernel_structs[i]);
            CHECK_OPENCL(err, "clReleaseMemObject()");
        }
    }
}

void OpenCLPlatform::synchronize(DeviceId dev) {
    cl_int err = clFinish(devices_[dev].queue);
    CHECK_OPENCL(err, "clFinish()");

    if (runtime_->profiling_enabled()) {
        cl_ulong end, start;
        err = clWaitForEvents(1, &end_kernel);
        CHECK_OPENCL(err, "clWaitForEvents()");
        err = clGetEventProfilingInfo(end_kernel, CL_PROFILING_COMMAND_END, sizeof(cl_ulong), &end, 0);
        err |= clGetEventProfilingInfo(end_kernel, CL_PROFILING_COMMAND_START, sizeof(cl_ulong), &start, 0);
        CHECK_OPENCL(err, "clGetEventProfilingInfo()");
        float time = (end-start)*1.0e-6f;
        anydsl_kernel_time.fetch_add(time * 1000);

        err = clReleaseEvent(end_kernel);
        CHECK_OPENCL(err, "clReleaseEvent()");
    }
}

void OpenCLPlatform::copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
    assert(dev_src == dev_dst);
    unused(dev_dst);

    cl_int err = clEnqueueCopyBuffer(devices_[dev_src].queue, (cl_mem)src, (cl_mem)dst, offset_src, offset_dst, size, 0, NULL, NULL);
    err |= clFinish(devices_[dev_src].queue);
    CHECK_OPENCL(err, "clEnqueueCopyBuffer()");
}

void OpenCLPlatform::copy_from_host(const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
    cl_int err = clEnqueueWriteBuffer(devices_[dev_dst].queue, (cl_mem)dst, CL_FALSE, offset_dst, size, (char*)src + offset_src, 0, NULL, NULL);
    err |= clFinish(devices_[dev_dst].queue);
    CHECK_OPENCL(err, "clEnqueueWriteBuffer()");
}

void OpenCLPlatform::copy_to_host(DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) {
    cl_int err = clEnqueueReadBuffer(devices_[dev_src].queue, (cl_mem)src, CL_FALSE, offset_src, size, (char*)dst + offset_dst, 0, NULL, NULL);
    err |= clFinish(devices_[dev_src].queue);
    CHECK_OPENCL(err, "clEnqueueReadBuffer()");
}

int OpenCLPlatform::dev_count() {
    return devices_.size();
}

cl_kernel OpenCLPlatform::load_kernel(DeviceId dev, const std::string& filename, const std::string& kernelname) {
    auto& opencl_dev = devices_[dev];

    opencl_dev.lock();

    cl_int err = CL_SUCCESS;
    cl_program program;
    auto& prog_cache = opencl_dev.programs;
    auto prog_it = prog_cache.find(filename);
    if (prog_it == prog_cache.end()) {
        opencl_dev.unlock();

        std::string options = "-cl-fast-relaxed-math";
        if (std::ifstream(filename).good()) {
            std::ifstream src_file(KERNEL_DIR + filename);
            std::string program_string(std::istreambuf_iterator<char>(src_file), (std::istreambuf_iterator<char>()));
            const size_t program_length = program_string.length();
            const char* program_c_str = program_string.c_str();
            options += " -cl-std=CL1.2";
            program = clCreateProgramWithSource(devices_[dev].ctx, 1, (const char**)&program_c_str, &program_length, &err);
            CHECK_OPENCL(err, "clCreateProgramWithSource()");
            debug("Compiling '%' on OpenCL device %", filename, dev);
        } else {
            error("Could not find kernel file '%'", filename);
        }

        cl_build_status build_status;
        err  = clBuildProgram(program, 0, NULL, options.c_str(), NULL, NULL);
        err |= clGetProgramBuildInfo(program, devices_[dev].dev, CL_PROGRAM_BUILD_STATUS, sizeof(build_status), &build_status, NULL);

        if (build_status == CL_BUILD_ERROR || err != CL_SUCCESS) {
            // determine the size of the options and log
            size_t log_size, options_size;
            err |= clGetProgramBuildInfo(program, devices_[dev].dev, CL_PROGRAM_BUILD_OPTIONS, 0, NULL, &options_size);
            err |= clGetProgramBuildInfo(program, devices_[dev].dev, CL_PROGRAM_BUILD_LOG, 0, NULL, &log_size);

            // allocate memory for the options and log
            char* program_build_options = new char[options_size];
            char* program_build_log = new char[log_size];

            // get the options and log
            err |= clGetProgramBuildInfo(program, devices_[dev].dev, CL_PROGRAM_BUILD_OPTIONS, options_size, program_build_options, NULL);
            err |= clGetProgramBuildInfo(program, devices_[dev].dev, CL_PROGRAM_BUILD_LOG, log_size, program_build_log, NULL);
            info("OpenCL build options : %", program_build_options);
            info("OpenCL build log : %", program_build_log);

            // free memory for options and log
            delete[] program_build_options;
            delete[] program_build_log;
        }
        CHECK_OPENCL(err, "clBuildProgram(), clGetProgramBuildInfo()");

        opencl_dev.lock();
        prog_cache[filename] = program;
    } else {
        program = prog_it->second;
    }

    // checks that the kernel exists
    auto& kernel_cache = opencl_dev.kernels;
    auto& kernel_map = kernel_cache[program];
    auto kernel_it = kernel_map.find(kernelname);
    cl_kernel kernel;
    if (kernel_it == kernel_map.end()) {
        opencl_dev.unlock();

        kernel = clCreateKernel(program, kernelname.c_str(), &err);
        CHECK_OPENCL(err, "clCreateKernel()");

        opencl_dev.lock();
        kernel_cache[program].emplace(kernelname, kernel);
    } else {
        kernel = kernel_it->second;
    }

    opencl_dev.unlock();

    return kernel;
}
