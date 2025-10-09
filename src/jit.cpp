#include <memory>
#include <fstream>
#include <sstream>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/RuntimeDyld.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>

#include <thorin/be/codegen.h>
#include <thorin/be/llvm/cpu.h>
#include <thorin/world.h>

#include "anydsl_jit.h"
#include "log.h"
#include "runtime.h"

bool compile(
    const std::vector<std::string>& file_names,
    const std::vector<std::string>& file_data,
    thorin::World& world,
    std::ostream& error_stream);

static const char runtime_srcs[] = {
#include "runtime_srcs.inc"
0
};

struct JIT {
    struct Program {
        Program(llvm::ExecutionEngine* engine) : engine(engine) {}
        llvm::ExecutionEngine* engine;
    };

    std::vector<Program> programs;
    Runtime* runtime;
    thorin::LogLevel log_level;

    JIT(Runtime* runtime) : runtime(runtime), log_level(thorin::LogLevel::Warn) {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
    }

    int32_t compile(const char* program_src, uint32_t size, uint32_t opt) {
        // The LLVM context and module have to be alive for the duration of this function
        std::unique_ptr<llvm::LLVMContext> llvm_context;
        std::unique_ptr<llvm::Module> llvm_module;

        size_t prog_key = std::hash<std::string>{}(program_src);
        std::stringstream hex_stream;
        hex_stream << std::hex << prog_key;
        std::string program_str = std::string(program_src, size);
        std::string cached_llvm = runtime->load_from_cache(program_str, ".llvm");
        std::string module_name = "jit_" + hex_stream.str();
        if (cached_llvm.empty()) {
            bool debug = false;
            assert(opt <= 3);

            thorin::Thorin thorin(module_name);
            thorin.world().set(log_level);
            thorin.world().set(std::make_shared<thorin::Stream>(std::cerr));
            if (!::compile(
                { "runtime", module_name },
                { std::string(runtime_srcs), program_str },
                thorin.world(), std::cerr))
                error("JIT: error while compiling sources");

            thorin.opt();

            std::string host_triple, host_cpu, host_attr, hls_flags;
            thorin::DeviceBackends backends(thorin.world(), opt, debug, hls_flags);

            thorin::llvm::CPUCodeGen cg(thorin, opt, debug, host_triple, host_cpu, host_attr);
            std::tie(llvm_context, llvm_module) = cg.emit_module();
            std::stringstream stream;
            llvm::raw_os_ostream llvm_stream(stream);
            llvm_module->print(llvm_stream, nullptr);
            runtime->store_to_cache(program_str, stream.str(), ".llvm");

            for (auto& cg : backends.cgs) {
                if (cg) {
                    if (std::string(cg->file_ext()) == ".hls")
                        error("JIT compilation of hls not supported!");
                    std::ostringstream stream;
                    cg->emit_stream(stream);
                    runtime->store_to_cache(cg->file_ext() + program_str, stream.str(), cg->file_ext());
                    runtime->register_file(module_name + cg->file_ext(), stream.str());
                }
            }
        } else {
            llvm::SMDiagnostic diagnostic_err;
            llvm_context = std::make_unique<llvm::LLVMContext>();
            llvm_module = llvm::parseIR(llvm::MemoryBuffer::getMemBuffer(cached_llvm)->getMemBufferRef(), diagnostic_err, *llvm_context);

            auto load_backend_src = [&](std::string ext) {
                std::string cached_src = runtime->load_from_cache(ext + program_str, ext);
                if (!cached_src.empty())
                    runtime->register_file(module_name + ext, cached_src);
            };
            load_backend_src(".cl");
            load_backend_src(".cu");
            load_backend_src(".nvvm");
            load_backend_src(".amdgpu");
        }

        llvm::TargetOptions options;
        options.AllowFPOpFusion = llvm::FPOpFusion::Fast;

        auto engine = llvm::EngineBuilder(std::move(llvm_module))
            .setEngineKind(llvm::EngineKind::JIT)
            .setMCPU(llvm::sys::getHostCPUName())
            .setTargetOptions(options)
            .setOptLevel(   opt == 0  ? llvm::CodeGenOptLevel::None    :
                            opt == 1  ? llvm::CodeGenOptLevel::Less    :
                            opt == 2  ? llvm::CodeGenOptLevel::Default :
                        /* opt == 3 */ llvm::CodeGenOptLevel::Aggressive)
            .create();
        if (!engine)
            return -1;

        engine->finalizeObject();
        programs.push_back(Program(engine));

        return (int32_t)programs.size() - 1;
    }

    void* lookup_function(int32_t key, const char* fn_name) {
        if (key == -1)
            return nullptr;

        return (void *)programs[key].engine->getFunctionAddress(fn_name);
    }

    void link(const char* lib) {
        llvm::sys::DynamicLibrary::LoadLibraryPermanently(lib);
    }
};

JIT& jit() {
    static std::unique_ptr<JIT> jit(new JIT(&runtime()));
    return *jit;
}

void anydsl_set_cache_directory(const char* dir) {
    jit().runtime->set_cache_directory(dir == nullptr ? std::string() : dir);
}

const char* anydsl_get_cache_directory() {
    static std::string dir;
    dir = jit().runtime->get_cache_directory();
    return dir.c_str();
}

void anydsl_link(const char* lib) {
    jit().link(lib);
}

int32_t anydsl_compile(const char* program, uint32_t size, uint32_t opt) {
    return jit().compile(program, size, opt);
}

void anydsl_set_log_level(uint32_t log_level) {
    jit().log_level = log_level <= 4 ? static_cast<thorin::LogLevel>(log_level) : thorin::LogLevel::Warn;
}

void* anydsl_lookup_function(int32_t key, const char* fn_name) {
    return jit().lookup_function(key, fn_name);
}
