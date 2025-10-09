#ifndef PAL_PLATFORM_INSERT_HALT_PASS_H
#define PAL_PLATFORM_INSERT_HALT_PASS_H

#include <llvm/IR/PassManager.h>
#include <llvm/Pass.h>

/// Pass that inserts RDNA specific assembly to halt a shader as soon as it starts if the environment variable
/// "HALT_IMMEDIATELY" is set to the value "ON"
struct PalPlatformInsertHaltPass : llvm::PassInfoMixin<PalPlatformInsertHaltPass> {
    llvm::PreservedAnalyses run(llvm::Function& F, llvm::FunctionAnalysisManager& FAM);
};

#endif // PAL_PLATFORM_INSERT_HALT_PASS_H