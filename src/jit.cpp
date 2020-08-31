#include <memory>
#include <fstream>
#include <sstream>
#include <string_view>

#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/ExecutionEngine/MCJIT.h>
#include <llvm/ExecutionEngine/RuntimeDyld.h>
#include <llvm/IR/Module.h>
#include <llvm/IRReader/IRReader.h>
#include <llvm/Support/raw_os_ostream.h>
#include <llvm/Support/DynamicLibrary.h>
#include <llvm/Support/SourceMgr.h>
#include <llvm/Support/TargetSelect.h>

#include <artic/bind.h>
#include <artic/check.h>
#include <artic/emit.h>
#include <artic/lexer.h>
#include <artic/locator.h>
#include <artic/log.h>
#include <artic/parser.h>
#include <artic/print.h>
#include <thorin/world.h>
#include <thorin/util/log.h>
#include <thorin/transform/codegen_prepare.h>
#include <thorin/be/llvm/llvm.h>

#include "anydsl_runtime.h"
#include "log.h"
#include "runtime.h"

struct MemBuf : public std::streambuf {
    MemBuf(const std::string& str) {
        setg(
            const_cast<char*>(str.data()),
            const_cast<char*>(str.data()),
            const_cast<char*>(str.data() + str.size()));
    }

    std::streampos seekoff(std::streamoff off, std::ios_base::seekdir way, std::ios_base::openmode) override {
        if (way == std::ios_base::beg)
            setg(eback(), eback() + off, egptr());
        else if (way == std::ios_base::cur)
            setg(eback(), gptr() + off, egptr());
        else if (way == std::ios_base::end)
            setg(eback(), egptr() + off, egptr());
        else
            return std::streampos(-1);
        return gptr() - eback();
    }

    std::streampos seekpos(std::streampos pos, std::ios_base::openmode mode) override {
        return seekoff(std::streamoff(pos), std::ios_base::beg, mode);
    }

    std::streamsize showmanyc() override {
        return egptr() - gptr();
    }
};

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

    JIT() {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
    }

    int32_t compile(const char* program_src, uint32_t size, uint32_t opt) {
        std::unique_ptr<llvm::LLVMContext> llvm_context;
        std::unique_ptr<llvm::Module> llvm_module;
        std::string program_str = std::string(program_src, size);
        std::string cached_llvm = runtime().load_cache(program_str, ".llvm");
        std::string module_name = "jit";
        if (cached_llvm.empty()) {
            bool debug = false;
            assert(opt <= 3);

            artic::Locator locator;
            artic::Log log(artic::log::err, &locator);
            artic::ast::ModDecl program;
            std::vector<std::string> contents;
            auto parse = [&] (std::string input, std::string filename) {
                contents.emplace_back(input);
                log.locator->register_file(filename, contents.back());
                MemBuf mem_buf(contents.back());

                std::istream is(&mem_buf);

                artic::Lexer lexer(log, filename, is);
                artic::Parser parser(log, lexer);
                auto module = parser.parse();
                if (log.errors > 0)
                    return false;

                program.decls.insert(
                    program.decls.end(),
                    std::make_move_iterator(module->decls.begin()),
                    std::make_move_iterator(module->decls.end())
                );

                return true;
            };

            artic::NameBinder name_binder(log);
            artic::TypeTable type_table;
            artic::TypeChecker type_checker(log, type_table);

            if (!parse(std::string(runtime_srcs), "runtime") ||
                !parse(program_str, module_name) ||
                !name_binder.run(program) ||
                !type_checker.run(program))
                error("JIT: error during type checking");

            thorin::Log::set(thorin::Log::Error, &std::cerr);
            thorin::World world(module_name);
            artic::Emitter emitter(log, world);
            if (!emitter.run(program))
                error("JIT: error during IR emission");

            world.opt();

            thorin::Backends backends(world);
            llvm_module = std::move(backends.cpu_cg->emit(opt, debug));
            llvm_context = std::move(backends.cpu_cg->context());
            std::stringstream stream;
            llvm::raw_os_ostream llvm_stream(stream);
            llvm_module->print(llvm_stream, nullptr);
            runtime().store_cache(program_str, stream.str(), ".llvm");

            auto emit_to_string = [&](thorin::CodeGen* cg, std::string ext) {
                if (cg) {
                    std::ostringstream stream;
                    cg->emit(stream, opt, debug);
                    runtime().store_cache(ext + program_str, stream.str(), ext);
                    runtime().register_file(std::string(module_name) + ext, stream.str());
                }
            };
            emit_to_string(backends.opencl_cg.get(), ".cl");
            emit_to_string(backends.cuda_cg.get(),   ".cu");
            emit_to_string(backends.nvvm_cg.get(),   ".nvvm");
            emit_to_string(backends.amdgpu_cg.get(), ".amdgpu");
            if (backends.hls_cg.get())
                error("JIT compilation of hls not supported!");
        } else {
            llvm::SMDiagnostic diagnostic_err;
            llvm_context.reset(new llvm::LLVMContext());
            llvm_module = llvm::parseIR(llvm::MemoryBuffer::getMemBuffer(cached_llvm)->getMemBufferRef(), diagnostic_err, *llvm_context);

            auto load_backend_src = [&](std::string ext) {
                std::string cached_src = runtime().load_cache(ext + program_str, ext);
                if (!cached_src.empty())
                    runtime().register_file(module_name + ext, cached_src);
            };
            load_backend_src(".cl");
            load_backend_src(".cu");
            load_backend_src(".nvvm");
            load_backend_src(".amdgpu");
        }

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
