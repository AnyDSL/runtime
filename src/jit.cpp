#include <memory>
#include <istream>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/TargetSelect.h>

#include <impala/impala.h>
#include <impala/ast.h>
#include <thorin/world.h>
#include <thorin/transform/codegen_prepare.h>
#include <thorin/be/llvm/cpu.h>

struct MemBuf : public std::streambuf {
    MemBuf(const char* string, uint32_t size) {
        auto begin = const_cast<char*>(string);
        auto end   = const_cast<char*>(string + size);
        setg(begin, begin, end);
    }
};

void* anydsl_compile(const char* string, uint32_t size, const char* fn_name, uint32_t opt) {
    static constexpr auto module_name = "jit";
    static constexpr bool debug = false;
    assert(opt <= 3);

    MemBuf buf(string, size);
    std::istream is(&buf);
    impala::Items items;
    impala::Init init(module_name);
    impala::parse(items, is, module_name);

    auto module = std::make_unique<const impala::Module>(module_name, std::move(items));
    impala::global_num_warnings = 0;
    impala::global_num_errors   = 0;
    impala::check(init, module.get(), false);
    if (impala::global_num_errors != 0)
        return nullptr;

    impala::emit(init.world, module.get());

    init.world.cleanup();
    init.world.opt();
    init.world.cleanup();
    thorin::codegen_prepare(init.world);
    thorin::CPUCodeGen cg(init.world);
    auto& llvm_module = cg.emit(opt, debug, false);
    auto fn = llvm_module->getFunction(fn_name);
    if (!fn)
        return nullptr;

    llvm::InitializeNativeTarget();
    auto& ctx = llvm_module->getContext();
    auto engine = llvm::EngineBuilder(std::move(llvm_module))
        .setOptLevel(   opt == 0  ? llvm::CodeGenOpt::None    :
                        opt == 1  ? llvm::CodeGenOpt::Less    :
                        opt == 2  ? llvm::CodeGenOpt::Default :
                     /* opt == 3 */ llvm::CodeGenOpt::Aggressive)
        .setMArch("native")
        .create();
    if (!engine)
        return nullptr;

    return engine->getPointerToFunction(fn);
}
