#include "pal_fix_calling_convention_pass.h"
#include "pal_utils.h"

#include <unordered_set>

#include <llvm/IR/CallingConv.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>

using namespace llvm;

void fix_calling_conv(Module* m, Function* f, std::unordered_set<Function*>& traversed_functions) {
    if (traversed_functions.find(f) != traversed_functions.end()) {
        // already visited this function -> prevent recursive loop
        return;
    }

    traversed_functions.insert(f);
    f->addFnAttr(llvm::Attribute::AlwaysInline);

    // Find and inspect all function calls inside of this function
    for (auto& bb : *f) {
        for (auto& instruction : bb) {
            if (CallInst* call_inst = dyn_cast<CallInst>(&instruction)) {
                if (call_inst->getCallingConv() != CallingConv::AMDGPU_Gfx) {
                    call_inst->setCallingConv(CallingConv::AMDGPU_Gfx);
                }

                if (Function* called_function = call_inst->getCalledFunction()) {
                    fix_calling_conv(m, called_function, traversed_functions);
                }
            }
        }
    }
}

PreservedAnalyses PalPlatformFixCallingConventionPass::run(Module& M, ModuleAnalysisManager&) {
    std::unordered_set<Function*> traversed_functions = {};
    for (Function& entrypoint_fn : M) {
        fix_calling_conv(&M, &entrypoint_fn, traversed_functions);
    }
    return PreservedAnalyses::all();
}
