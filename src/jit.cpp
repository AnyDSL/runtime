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

        MemBuf buf(runtime_srcs, sizeof(runtime_srcs));
        std::istream is(&buf);
        impala::parse(runtime_items, is, "runtime");
        runtime_items_count = runtime_items.size();
    }

    void* compile(const char* program, uint32_t size, const char* fn_name, uint32_t opt) {
        static constexpr auto module_name = "jit";
        static constexpr bool debug = false;
        assert(opt <= 3);

        MemBuf buf(program, size);
        std::istream is(&buf);
        impala::Items items;
        impala::parse(items, is, module_name);

        // Move the runtime items inside the module
        auto items_count = items.size();
        items.resize(items_count + runtime_items_count);
        std::move(runtime_items.begin(), runtime_items.end(), items.begin() + items_count);

        auto module = std::make_unique<const impala::Module>(module_name, std::move(items));
        impala::global_num_warnings = 0;
        impala::global_num_errors   = 0;
        std::unique_ptr<impala::TypeTable> typetable;
        impala::check(typetable, module.get(), false);
        if (impala::global_num_errors != 0)
            return nullptr;

        thorin::World world(module_name);
        impala::emit(world, module.get());

        // Move runtime items back
        std::move(items.begin() + items_count, items.end(), runtime_items.begin());

        world.cleanup();
        world.opt();
        world.cleanup();
        thorin::codegen_prepare(world);
        thorin::CPUCodeGen cg(world);
        auto& llvm_module = cg.emit(opt, debug, false);
        auto fn = llvm_module->getFunction(fn_name);
        if (!fn)
            return nullptr;

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
        return engine->getPointerToFunction(fn);
    }

    void link(const char* lib) {
        llvm::sys::DynamicLibrary::LoadLibraryPermanently(lib);
    }

    impala::Items runtime_items;
    size_t runtime_items_count;
};

static JIT jit;

void anydsl_link(const char* lib) {
    jit.link(lib);
}

void* anydsl_compile(const char* program, uint32_t size, const char* fn_name, uint32_t opt) {
    return jit.compile(program, size, fn_name, opt);
}
