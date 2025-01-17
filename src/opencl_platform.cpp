#include "opencl_platform.h"

#include "runtime.h"

#ifndef _WIN32
#include <limits.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <string>

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
        #ifdef CL_INVALID_PROPERTY
        CL_ERROR_CODE(CL_INVALID_PROPERTY)
        #endif
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
        // error code from cl_ext.h
        #ifdef CL_PLATFORM_NOT_FOUND_KHR
        CL_ERROR_CODE(CL_PLATFORM_NOT_FOUND_KHR)
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

    debug("Number of available OpenCL Platforms: %", num_platforms);
    #ifdef CL_PLATFORM_NOT_FOUND_KHR
    if (err == CL_PLATFORM_NOT_FOUND_KHR) {
        debug("No valid OpenCL ICD");
        return;
    }
    #endif
    CHECK_OPENCL(err, "clGetPlatformIDs()");

    cl_platform_id* platforms = new cl_platform_id[num_platforms];

    err = clGetPlatformIDs(num_platforms, platforms, NULL);
    CHECK_OPENCL(err, "clGetPlatformIDs()");

    // get platform info for each platform
    for (size_t i=0; i<num_platforms; ++i) {
        auto platform = platforms[i];

        char buffer[1024];
        err  = clGetPlatformInfo(platform, CL_PLATFORM_NAME, sizeof(buffer), &buffer, NULL);
        debug("  Platform Name: %", buffer);
        std::string platform_name(buffer);
        err |= clGetPlatformInfo(platform, CL_PLATFORM_VENDOR, sizeof(buffer), &buffer, NULL);
        debug("  Platform Vendor: %", buffer);
        err |= clGetPlatformInfo(platform, CL_PLATFORM_VERSION, sizeof(buffer), &buffer, NULL);
        debug("  Platform Version: %", buffer);
        CHECK_OPENCL(err, "clGetPlatformInfo()");

        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, 0, NULL, &num_devices);
        if (err == CL_DEVICE_NOT_FOUND)
            continue;
        CHECK_OPENCL(err, "clGetDeviceIDs()");

        cl_device_id* devices = new cl_device_id[num_devices];
        err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_ALL, num_devices, devices, &num_devices);
        CHECK_OPENCL(err, "clGetDeviceIDs()");

        // get device info for each device
        for (size_t j=0; j<num_devices; ++j) {
            auto device = devices[j];
            cl_device_type dev_type;
            cl_uint device_vendor_id;

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
            std::string device_name(buffer);
            err |= clGetDeviceInfo(devices[j], CL_DEVICE_VENDOR, sizeof(buffer), &buffer, NULL);
            err |= clGetDeviceInfo(devices[j], CL_DEVICE_VENDOR_ID, sizeof(device_vendor_id), &device_vendor_id, NULL);
            debug("      Device Vendor: % (ID: %)", buffer, device_vendor_id);
            err |= clGetDeviceInfo(devices[j], CL_DEVICE_VERSION, sizeof(buffer), &buffer, NULL);
            debug("      Device OpenCL Version: %", buffer);
            std::string version(buffer);
            size_t version_offset;
            cl_uint version_major = std::stoi(version.substr(7), &version_offset);
            cl_uint version_minor = std::stoi(version.substr(7 + version_offset + 1));
            err |= clGetDeviceInfo(devices[j], CL_DRIVER_VERSION, sizeof(buffer), &buffer, NULL);
            debug("      Device Driver Version: %", buffer);

            std::string svm_caps_str = "none";
            #ifdef CL_VERSION_2_0
            cl_device_svm_capabilities svm_caps(0);
            if (version_major >= 2) {
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
            devices_.emplace_back(this, platform, device, version_major, version_minor, platform_name, device_name);

            #ifdef CL_VERSION_2_0
            devices_[dev].svm_caps = svm_caps;
            #endif

            // create context
            cl_context_properties ctx_props[3] = { CL_CONTEXT_PLATFORM, (cl_context_properties)platform, 0 };
            devices_[dev].ctx = clCreateContext(ctx_props, 1, &devices_[dev].dev, NULL, NULL, &err);
            CHECK_OPENCL(err, "clCreateContext()");

            // create command queue
            devices_[dev].queue = NULL;
            #ifdef CL_VERSION_2_0
            if (version_major >= 2) {
                cl_queue_properties queue_props[3] = { 0, 0, 0 };
                if (runtime_->profiling_enabled()) {
                    queue_props[0] = CL_QUEUE_PROPERTIES;
                    queue_props[1] = CL_QUEUE_PROFILING_ENABLE;
                }
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

            if (platform_name.find("FPGA") != std::string::npos) {
                devices_[dev].is_intel_fpga = true;
            } else if (platform_name.find("Xilinx") != std::string::npos) {
                devices_[dev].is_xilinx_fpga = true;

                // bitstream file needs to be loaded at the very beginning
                std::string filename;
                const char* env_var = std::getenv("ANYDSL_XRT_BITSTREAM");
                if (env_var) {
                    filename = env_var;
                } else {
                    #ifndef _WIN32
                    char buf[PATH_MAX];
                    ssize_t len;
                    if ((len = readlink("/proc/self/exe", buf, sizeof(buf)-1)) != -1) {
                        buf[len] = '\0';
                        filename = std::string(buf) + ".xclbin";
                    }
                    #else
                    error("Can't find XRT bitstream file, please specify the path to xclbin file using the ANYDSL_XRT_BITSTREAM environment variable");
                    #endif
                }

                auto canonical = std::filesystem::weakly_canonical(filename);
                if (canonical.extension() != ".xclbin")
                    error("Incorrect extension for XRT bitstream file '%' (should be '.xclbin')", canonical.string());

                std::string program_string = runtime_->load_file(canonical.string());

                auto src_path = canonical;
                src_path.replace_extension(".cl");
                auto& prog_cache = devices_[dev].programs;
                prog_cache[src_path.string()] = load_program_binary(DeviceId(dev), canonical.string(), program_string);
            }
        }
        delete[] devices;
    }
    delete[] platforms;
}

OpenCLPlatform::~OpenCLPlatform() {
    for (size_t i = 0; i < devices_.size(); i++) {
        if (devices_[i].is_intel_fpga || devices_[i].is_xilinx_fpga)
            continue;

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
        cl_int err = clReleaseCommandQueue(devices_[i].queue);
        CHECK_OPENCL(err, "clReleaseCommandQueue()");
        err = clReleaseContext(devices_[i].ctx);
        CHECK_OPENCL(err, "clReleaseContext()");
    }
}

void* OpenCLPlatform::alloc(DeviceId dev, int64_t size) {
    if (!size) return nullptr;

    #ifdef CL_VERSION_2_0
    if (devices_[dev].version_major == 2) {
        cl_mem_flags flags = CL_MEM_READ_WRITE;
        void* mem = clSVMAlloc(devices_[dev].ctx, flags, size, 0);
        if (mem == nullptr)
            error("clSVMAlloc() returned % for OpenCL device %", mem, dev);

        return mem;
    }
    #endif
    cl_int err = CL_SUCCESS;
    cl_mem_flags flags = CL_MEM_READ_WRITE;
    cl_mem mem = clCreateBuffer(devices_[dev].ctx, flags, size, NULL, &err);
    CHECK_OPENCL(err, "clCreateBuffer()");

    return (void*)mem;
}

void* OpenCLPlatform::alloc_unified(DeviceId dev, int64_t size) {
    if (!size) return nullptr;

    #ifdef CL_VERSION_2_0
    if (devices_[dev].version_major == 2) {
        cl_mem_flags flags = CL_MEM_READ_WRITE;
        if (devices_[dev].svm_caps & CL_DEVICE_SVM_FINE_GRAIN_BUFFER)
            flags |= CL_MEM_SVM_FINE_GRAIN_BUFFER;
        if (devices_[dev].svm_caps & CL_DEVICE_SVM_ATOMICS)
            flags |= CL_MEM_SVM_ATOMICS;
        void* mem = clSVMAlloc(devices_[dev].ctx, flags, size, 0);
        if (mem == nullptr)
            error("clSVMAlloc() returned % for OpenCL device %", mem, dev);

        return mem;
    }
    #endif
    error("clSVMAlloc() requires at least OpenCL 2.0 for OpenCL device %", dev);
}

void OpenCLPlatform::release(DeviceId dev, void* ptr) {
    #ifdef CL_VERSION_2_0
    if (devices_[dev].version_major == 2)
        return clSVMFree(devices_[dev].ctx, ptr);
    #endif
    unused(dev);
    cl_int err = clReleaseMemObject((cl_mem)ptr);
    CHECK_OPENCL(err, "clReleaseMemObject()");
}

void time_kernel_callback(cl_event event, cl_int, void* data) {
    auto dev = reinterpret_cast<OpenCLPlatform::DeviceData*>(data);
    cl_ulong end, start;
    cl_int err = clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_END, sizeof(cl_ulong), &end, 0);
    err |= clGetEventProfilingInfo(event, CL_PROFILING_COMMAND_START, sizeof(cl_ulong), &start, 0);
    CHECK_OPENCL(err, "clGetEventProfilingInfo()");
    cl_ulong time = (end - start) / 1000;
    dev->parent->runtime_->kernel_time().fetch_add(time);
    dev->atomic_data.timings_counter.fetch_sub(1);
    err = clReleaseEvent(event);
    CHECK_OPENCL(err, "clReleaseEvent()");
}

static inline bool ends_with(std::string_view str, std::string_view suffix) {
    if (str.size() < suffix.size())
        return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

void OpenCLPlatform::launch_kernel(DeviceId dev, const LaunchParams& launch_params) {
    if (devices_[dev].is_intel_fpga && launch_params.num_args == 0) {
        debug("processing by autorun kernel");
        return;
    }

    auto kernel = load_kernel(dev, launch_params.file_name, launch_params.kernel_name);
    bool is_spirv = ends_with(launch_params.file_name, ".spv");

    // set up arguments
    std::vector<cl_mem> kernel_structs;
    for (uint32_t i = 0; i < launch_params.num_args; i++) {
        if (!is_spirv && launch_params.args.types[i] == KernelArgType::Struct) {
            // create a buffer for each structure argument
            cl_int err = CL_SUCCESS;
            cl_mem_flags flags = CL_MEM_READ_WRITE | CL_MEM_USE_HOST_PTR;
            cl_mem struct_buf = clCreateBuffer(devices_[dev].ctx, flags, launch_params.args.sizes[i], launch_params.args.data[i], &err);
            CHECK_OPENCL(err, "clCreateBuffer()");
            kernel_structs.push_back(struct_buf);
            clSetKernelArg(kernel, i, sizeof(cl_mem), &struct_buf);
        } else {
            #ifdef CL_VERSION_2_0
            if (launch_params.args.types[i] == KernelArgType::Ptr && devices_[dev].version_major == 2) {
                cl_int err = clSetKernelArgSVMPointer(kernel, i, *(void**)launch_params.args.data[i]);
                CHECK_OPENCL(err, "clSetKernelArgSVMPointer()");
                continue;
            }
            #endif
            cl_int err = clSetKernelArg(kernel, i,
                launch_params.args.types[i] == KernelArgType::Ptr ? sizeof(cl_mem) : launch_params.args.sizes[i],
                launch_params.args.data[i]);
            CHECK_OPENCL(err, "clSetKernelArg()");
        }
    }

    size_t global_work_size[] = {launch_params.grid [0], launch_params.grid [1], launch_params.grid [2]};
    size_t local_work_size[]  = {launch_params.block[0], launch_params.block[1], launch_params.block[2]};

    // launch the kernel
    cl_event event = 0;
    auto queue = devices_[dev].queue;
    if (devices_[dev].is_intel_fpga || devices_[dev].is_xilinx_fpga)
        queue = devices_[dev].kernels_queue[kernel];

    if (devices_[dev].is_xilinx_fpga && global_work_size[0] == 1 && global_work_size[1] == 1 && global_work_size[2] == 1) {
        cl_int err = clEnqueueTask(queue, kernel, 0, NULL, &event);
        CHECK_OPENCL(err, "clEnqueueTask()");
    } else {
        cl_int err = clEnqueueNDRangeKernel(queue, kernel, 3, NULL, global_work_size, local_work_size, 0, NULL, &event);
        CHECK_OPENCL(err, "clEnqueueNDRangeKernel()");
    }

    if (runtime_->profiling_enabled() && event) {
        cl_int err = clSetEventCallback(event, CL_COMPLETE, &time_kernel_callback, &devices_[dev]);
        devices_[dev].atomic_data.timings_counter.fetch_add(1);
        CHECK_OPENCL(err, "clSetEventCallback()");
    } else {
        cl_int err = clReleaseEvent(event);
        CHECK_OPENCL(err, "clReleaseEvent()");
    }

    if (runtime_->dynamic_profiling_enabled())
        dynamic_profile(dev, launch_params.file_name);

    // release temporary buffers for struct arguments
    for (auto tmp : kernel_structs) {
        cl_int err = clReleaseMemObject(tmp);
        CHECK_OPENCL(err, "clReleaseMemObject()");
    }
}

void OpenCLPlatform::synchronize(DeviceId dev) {
    if (devices_[dev].is_intel_fpga || devices_[dev].is_xilinx_fpga) {
        auto& queue_map = devices_[dev].kernels_queue;
        for (auto& it : queue_map) {
            cl_int err = clFinish(it.second);
            CHECK_OPENCL(err, "clFinish()");
        }
    } else {
        cl_int err = clFinish(devices_[dev].queue);
        // clFinish does not ensure that the callback has been called.
        // We must thus add another layer of synchronization when profiling is enabled.
        while (runtime_->profiling_enabled() && devices_[dev].atomic_data.timings_counter.load() != 0) ;
        CHECK_OPENCL(err, "clFinish()");
    }
}

void OpenCLPlatform::copy(DeviceId dev_src, const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
    assert(dev_src == dev_dst);
    unused(dev_dst);

    #ifdef CL_VERSION_2_0
    if (devices_[dev_src].version_major == 2 && devices_[dev_dst].version_major == 2)
        return copy_svm(src, offset_src, dst, offset_dst, size);
    if ((devices_[dev_src].version_major == 2 && devices_[dev_dst].version_major == 1) ||
        (devices_[dev_src].version_major == 1 && devices_[dev_dst].version_major == 2))
        error("copy between SVM and non-SVM OpenCL devices % and %", dev_src, dev_dst);
    #endif

    cl_int err = clEnqueueCopyBuffer(devices_[dev_src].queue, (cl_mem)src, (cl_mem)dst, offset_src, offset_dst, size, 0, NULL, NULL);
    err |= clFinish(devices_[dev_src].queue);
    CHECK_OPENCL(err, "clEnqueueCopyBuffer()");
}

void OpenCLPlatform::copy_from_host(const void* src, int64_t offset_src, DeviceId dev_dst, void* dst, int64_t offset_dst, int64_t size) {
    #ifdef CL_VERSION_2_0
    if (devices_[dev_dst].version_major == 2)
        return copy_svm(src, offset_src, dst, offset_dst, size);
    #endif
    cl_int err = clEnqueueWriteBuffer(devices_[dev_dst].queue, (cl_mem)dst, CL_FALSE, offset_dst, size, (char*)src + offset_src, 0, NULL, NULL);
    err |= clFinish(devices_[dev_dst].queue);
    CHECK_OPENCL(err, "clEnqueueWriteBuffer()");
}

void OpenCLPlatform::copy_to_host(DeviceId dev_src, const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) {
    #ifdef CL_VERSION_2_0
    if (devices_[dev_src].version_major == 2)
        return copy_svm(src, offset_src, dst, offset_dst, size);
    #endif
    cl_int err = clEnqueueReadBuffer(devices_[dev_src].queue, (cl_mem)src, CL_FALSE, offset_src, size, (char*)dst + offset_dst, 0, NULL, NULL);
    err |= clFinish(devices_[dev_src].queue);
    CHECK_OPENCL(err, "clEnqueueReadBuffer()");
}

void OpenCLPlatform::copy_svm(const void* src, int64_t offset_src, void* dst, int64_t offset_dst, int64_t size) {
    std::copy((char*)src + offset_src, (char*)src + offset_src + size, (char*)dst + offset_dst);
}

static std::string program_as_string(cl_program program) {
    size_t binary_size;
    cl_int err = clGetProgramInfo(program, CL_PROGRAM_BINARY_SIZES, sizeof(size_t), &binary_size, NULL);
    CHECK_OPENCL(err, "clGetProgramInfo()");
    unsigned char** binaries = new unsigned char*[1];
    binaries[0] = new unsigned char[binary_size];
    err = clGetProgramInfo(program, CL_PROGRAM_BINARIES, sizeof(unsigned char *) * 1, binaries, NULL);
    CHECK_OPENCL(err, "clGetProgramInfo()");
    std::string binary((char*)binaries[0], binary_size);

    delete[] binaries[0];
    delete[] binaries;

    return binary;
}

cl_program OpenCLPlatform::load_program_binary(DeviceId dev, const std::string& filename, const std::string& program_string) const {
    const size_t program_length = program_string.length();
    const char* program_c_str = program_string.c_str();
    cl_int err = CL_SUCCESS;
    cl_int binary_status;
    cl_program program = clCreateProgramWithBinary(devices_[dev].ctx, 1, &devices_[dev].dev, &program_length, (const unsigned char**)&program_c_str, &binary_status, &err);
    CHECK_OPENCL(err, "clCreateProgramWithBinary()");
    CHECK_OPENCL(binary_status, "Binary status: clCreateProgramWithBinary()");
    debug("Loading binary '%' for OpenCL device %", filename, dev);

    return program;
}

cl_program OpenCLPlatform::load_program_il(DeviceId dev, const std::string& filename, const std::string& program_string) const {
#if CL_VERSION_2_1
    const size_t program_length = program_string.length();
    const char* program_c_str = program_string.c_str();
    cl_int err = CL_SUCCESS;
    cl_program program = clCreateProgramWithIL(devices_[dev].ctx, (const void*)program_c_str, program_length, &err);
    CHECK_OPENCL(err, "clCreateProgramWithIL()");
    debug("Loading IL '%' for OpenCL device %", filename, dev);

    return program;
#else
    error("OpenCL 2.1 or later is required for SPIR-V support.");
#endif
}

cl_program OpenCLPlatform::load_program_source(DeviceId dev, const std::string& filename, const std::string& program_string) const {
    const size_t program_length = program_string.length();
    const char* program_c_str = program_string.c_str();
    cl_int err = CL_SUCCESS;
    cl_program program = clCreateProgramWithSource(devices_[dev].ctx, 1, (const char**)&program_c_str, &program_length, &err);
    CHECK_OPENCL(err, "clCreateProgramWithSource()");
    debug("Loading source '%' for OpenCL device %", filename, dev);

    return program;
}

cl_program OpenCLPlatform::compile_program(DeviceId dev, cl_program program, const std::string& filename) const {
    debug("Compiling '%' on OpenCL device %", filename, dev);
    std::string options = "-cl-fast-relaxed-math";
    options += " -cl-std=CL" + std::to_string(devices_[dev].version_major) + "." + std::to_string(devices_[dev].version_minor);

    cl_build_status build_status;
    cl_int err  = clBuildProgram(program, 0, NULL, options.c_str(), NULL, NULL);
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

    return program;
}

void OpenCLPlatform::dynamic_profile(DeviceId dev, const std::string& filename) {
    auto& opencl_dev = devices_[dev];
    cl_program program = opencl_dev.programs[filename];

    if(opencl_dev.is_intel_fpga) {
        typedef cl_int (*clGetProfileDataDevice_fn) (cl_device_id, cl_program, cl_bool, cl_bool, cl_bool, size_t, void *, size_t *, cl_int *);

        clGetProfileDataDevice_fn get_profile_data_ptr = (clGetProfileDataDevice_fn)
            clGetExtensionFunctionAddressForPlatform(opencl_dev.platform, "clGetProfileDataDeviceIntelFPGA");
        cl_int profile_status = CL_SUCCESS;
        profile_status = get_profile_data_ptr(opencl_dev.dev, program, false, true, false, 0, NULL, NULL, NULL);
        CHECK_OPENCL(profile_status, "clGetProfileDataDeviceIntelFPGA()");
    } else
        error("Dynamic Profiling is not available for this platform");
}

cl_kernel OpenCLPlatform::load_kernel(DeviceId dev, const std::string& filename, const std::string& kernelname) {
    auto& opencl_dev = devices_[dev];

    opencl_dev.lock();

    cl_int err = CL_SUCCESS;
    cl_program program;
    auto canonical = std::filesystem::weakly_canonical(filename);
    auto& prog_cache = opencl_dev.programs;
    auto prog_it = prog_cache.find(canonical.string());
    if (prog_it == prog_cache.end()) {
        opencl_dev.unlock();

        // load file from disk or cache
        auto src_path = canonical;
        if (opencl_dev.is_intel_fpga)
            src_path.replace_extension(".aocx");
        std::string src_code = runtime_->load_file(src_path.string());

        if (canonical.extension() == ".spv") {
            program = load_program_il(dev, src_path.string(), src_code);
            program = compile_program(dev, program, src_path.string());
        } else if (canonical.extension() == ".cl") {
            // compile src or load from cache
            std::string bin = opencl_dev.is_intel_fpga ? src_code : runtime_->load_from_cache(devices_[dev].platform_name + devices_[dev].device_name + src_code);
            if (bin.empty()) {
                program = load_program_source(dev, src_path.string(), src_code);
                program = compile_program(dev, program, src_path.string());
                runtime_->store_to_cache(devices_[dev].platform_name + devices_[dev].device_name + src_code, program_as_string(program));
            } else {
                program = load_program_binary(dev, src_path.string(), bin);
                program = compile_program(dev, program, src_path.string());
            }
        } else
            error("Incorrect extension for kernel file '%' (should be '.cl' or .'spv')", canonical.string());

        opencl_dev.lock();
        prog_cache[canonical.string()] = program;
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

        if (devices_[dev].is_intel_fpga || devices_[dev].is_xilinx_fpga) {
            // Intel SDK for FPGA needs a new queue for each kernel
            cl_command_queue kernel_queue = clCreateCommandQueue(opencl_dev.ctx, opencl_dev.dev, CL_QUEUE_PROFILING_ENABLE, &err);
            devices_[dev].kernels_queue[kernel] = kernel_queue;
            CHECK_OPENCL(err, "clCreateCommandQueue()");
        }

        opencl_dev.lock();
        kernel_cache[program].emplace(kernelname, kernel);
    } else {
        kernel = kernel_it->second;
    }

    opencl_dev.unlock();

    return kernel;
}

const char* OpenCLPlatform::device_name(DeviceId dev) const {
    return devices_[dev].device_name.c_str();
}

void register_opencl_platform(Runtime* runtime) {
    runtime->register_platform<OpenCLPlatform>();
}
