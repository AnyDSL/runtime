
#include <cuda.h>
#include <nvPTXCompiler.h>
#include <string>


#if CUDA_VERSION <= 11010
#error "PTX Compiler support requires CUDA 11.2 or newer"
#endif

#ifdef _MSC_VER
#define NVPTXCOMPILE_API __declspec(dllexport)
#endif // _MSC_VER


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
    if (NVPTXCOMPILE_SUCCESS != err);
        //error("NVPTXCOMPILER API function % (%) [file %, line %]: %", name, err, file, line, _nvptxcompilerGetErrorString(err));
}


bool NVPTXCOMPILE_API nvPTXCompile(
    int num_options, const char* options[],
    const std::string& filename,
    const std::string& ptx_string,
    std::string& binary
) {

#define CHECK_NVPTXCOMPILER(err, name) check_nvptxcompiler_errors (err, name, __FILE__, __LINE__)

    nvPTXCompilerHandle handle;
    nvPTXCompileResult err = nvPTXCompilerCreate(&handle, ptx_string.length(), const_cast<char*>(ptx_string.c_str()));
    CHECK_NVPTXCOMPILER(err, "nvPTXCompilerCreate()");

    err = nvPTXCompilerCompile(handle, num_options, options);
    size_t info_log_size;
    size_t error_log_size;
    nvPTXCompilerGetInfoLogSize(handle, &info_log_size);
    nvPTXCompilerGetErrorLogSize(handle, &error_log_size);
    if (info_log_size) {
        std::string info_log(info_log_size, '\0');
        nvPTXCompilerGetInfoLog(handle, &info_log[0]);
        //info("Compilation info: %", info_log);
    }
    if (error_log_size) {
        std::string error_log(error_log_size, '\0');
        nvPTXCompilerGetErrorLog(handle, &error_log[0]);
        //info("Compilation error: %", error_log);
    }
    CHECK_NVPTXCOMPILER(err, "nvPTXCompilerCompile()");

    size_t binary_size;
    CHECK_NVPTXCOMPILER(nvPTXCompilerGetCompiledProgramSize(handle, &binary_size), "nvPTXCompilerGetCompiledProgramSize()");

    binary.resize(binary_size, '\0');
    CHECK_NVPTXCOMPILER(nvPTXCompilerGetCompiledProgram(handle, &binary[0]), "nvPTXCompilerGetCompiledProgram()");

    CHECK_NVPTXCOMPILER(nvPTXCompilerDestroy(&handle), "nvPTXCompilerDestroy()");
    return true;
}
