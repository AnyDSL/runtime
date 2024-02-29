#include "pal_insert_halt_pass.h"
#include "pal_utils.h"

#include <llvm/IR/CallingConv.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InlineAsm.h>

#include <cstdlib>

using namespace llvm;

PreservedAnalyses PalPlatformInsertHaltPass::run(Function& F, FunctionAnalysisManager&) {
    char* halt_immediately = std::getenv("HALT_IMMEDIATELY");
    if (F.getName() != pal_utils::ComputeShaderMainFnName || !halt_immediately
        || strcmp(halt_immediately, "ON") != 0) {
        return PreservedAnalyses::all();
    }
    assert(F.getCallingConv() == CallingConv::AMDGPU_CS);
    LLVMContext& Ctx = F.getParent()->getContext();
    BasicBlock& EntryBlock = *F.begin();
    IRBuilder<> Builder(&(*EntryBlock.getFirstInsertionPt()));
    ArrayRef<Value*> inline_asm_args;
    InlineAsm* inline_assembly = InlineAsm::get(
        FunctionType::get(Type::getVoidTy(Ctx), false), "s_sethalt 1", "", true, false, InlineAsm::AD_ATT);
    Builder.CreateCall(inline_assembly, inline_asm_args);
    return PreservedAnalyses::none();
}