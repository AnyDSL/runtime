#include <fstream>
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
#include <thorin/be/llvm/cpu.h>

#include "anydsl_runtime.h"

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
    JIT() {
        impala::init();
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
    }

    llvm::ExecutionEngine* compile(const char* program, uint32_t size, uint32_t opt) {
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
            return nullptr;

        thorin::World world(module_name);
        impala::emit(world, module.get());

        world.cleanup();
        world.opt();
        world.cleanup();
        thorin::codegen_prepare(world);
        thorin::CPUCodeGen cg(world);

        auto& llvm_module = cg.emit(opt, debug, false);
        auto engine = llvm::EngineBuilder(std::move(llvm_module))
            .setEngineKind(llvm::EngineKind::JIT)
            .setOptLevel(   opt == 0  ? llvm::CodeGenOpt::None    :
                            opt == 1  ? llvm::CodeGenOpt::Less    :
                            opt == 2  ? llvm::CodeGenOpt::Default :
                         /* opt == 3 */ llvm::CodeGenOpt::Aggressive)
            .create();
        if (!engine)
            return nullptr;

        engine->finalizeObject();
        return engine;
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

void* anydsl_compile(const char* program, uint32_t size, const char* fn_name, uint32_t opt) {
    return anydsl_lookup(anydsl_get_engine(program, size, opt), fn_name);
}

void* anydsl_get_engine(const char* program, uint32_t size, uint32_t opt) {
    return jit().compile(program, size, opt);
}

void* anydsl_lookup(void* engine_ptr, const char* fn_name) {
    auto engine = (llvm::ExecutionEngine *)engine_ptr;
    return (void *)engine->getFunctionAddress(fn_name);
}
