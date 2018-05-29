#include <fstream>
#include <sstream>
#include <memory>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/RuntimeDyld.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/DynamicLibrary.h>

#include <impala/impala.h>
#include <impala/ast.h>
#include <thorin/world.h>
#include <thorin/transform/codegen_prepare.h>
#include <thorin/be/llvm/llvm.h>

#include "anydsl_runtime.h"
#include "runtime.h"

struct MemBuf : public std::streambuf {
    MemBuf(const char* string, uint32_t size) {
        auto begin = const_cast<char*>(string);
        auto end   = const_cast<char*>(string + size);
        setg(begin, begin, end);
    }
};

static const char runtime_srcs[] = {
#include "runtime_srcs.inc"
};

struct JIT {
    struct Program {
        Program(llvm::ExecutionEngine* engine) : engine(engine) {}
        llvm::ExecutionEngine* engine;
    };

    std::vector<Program> programs;

    JIT() {
        impala::init();
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
    }

    int32_t compile(const char* program, uint32_t size, uint32_t opt) {
        static constexpr auto module_name = "jit";
        static constexpr bool debug = false;
        assert(opt <= 3);

        impala::Items items;
        MemBuf program_buf(program, size);
        MemBuf runtime_buf(runtime_srcs, sizeof(runtime_srcs));
        std::istream program_is(&program_buf);
        std::istream runtime_is(&runtime_buf);
        impala::parse(items, runtime_is, "runtime");
        impala::parse(items, program_is, module_name);

        auto module = std::make_unique<impala::Module>(module_name, std::move(items));
        impala::num_warnings() = 0;
        impala::num_errors()   = 0;
        std::unique_ptr<impala::TypeTable> typetable;
        impala::check(typetable, module.get(), false);
        if (impala::num_errors() != 0)
            return -1;

        thorin::World world(module_name);
        impala::emit(world, module.get());

        world.cleanup();
        world.opt();
        world.cleanup();

        thorin::Backends backends(world);
        auto& llvm_module = backends.cpu_cg->emit(opt, debug);
        auto engine = llvm::EngineBuilder(std::move(llvm_module))
            .setEngineKind(llvm::EngineKind::JIT)
            .setOptLevel(   opt == 0  ? llvm::CodeGenOpt::None    :
                            opt == 1  ? llvm::CodeGenOpt::Less    :
                            opt == 2  ? llvm::CodeGenOpt::Default :
                        /* opt == 3 */ llvm::CodeGenOpt::Aggressive)
            .create();
        if (!engine)
            return -1;

        engine->finalizeObject();
        programs.push_back(Program(engine));

        auto emit_to_string = [&](thorin::CodeGen* cg, PlatformId id, std::string ext) {
            if (cg) {
                std::ostringstream stream;
                cg->emit(stream, opt, debug);
                runtime().register_file(id, (std::string(module_name) + ext).c_str(), stream.str().c_str());
            }
        };
        emit_to_string(backends.opencl_cg.get(), PlatformId(ANYDSL_OPENCL), ".cl");
        emit_to_string(backends.cuda_cg.get(),   PlatformId(ANYDSL_CUDA),   ".cu");
        emit_to_string(backends.nvptx_cg.get(),  PlatformId(ANYDSL_CUDA),   ".ptx");
        emit_to_string(backends.amdgpu_cg.get(), PlatformId(ANYDSL_HSA),    ".amdgpu");
        if (backends.nvvm_cg.get())
            error("JIT compilation of nvvm not supported, use nvptx backend for JIT!");
        if (backends.hls_cg.get())
            error("JIT compilation of hls not supported!");

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
    static std::unique_ptr<JIT> jit(new JIT());
    return *jit;
}

void anydsl_link(const char* lib) {
    jit().link(lib);
}

int32_t anydsl_compile(const char* program, uint32_t size, uint32_t opt) {
    return jit().compile(program, size, opt);
}

void* anydsl_lookup_function(int32_t key, const char* fn_name) {
    return jit().lookup_function(key, fn_name);
}
