#ifndef PAL_PLATFORM_FIX_CALLING_CONVENTION_H
#define PAL_PLATFORM_FIX_CALLING_CONVENTION_H

#include <llvm/IR/PassManager.h>

/// This pass sets the calling convention to AMDGPU_Gfx for all calls in the given module and sets the AlwaysInline
/// Attribute on every called function in the module to avoid the LLVM AMDGPU backend throwing errors.
struct PalPlatformFixCallingConventionPass : llvm::PassInfoMixin<PalPlatformFixCallingConventionPass> {
    llvm::PreservedAnalyses run(llvm::Module& M, llvm::ModuleAnalysisManager&);
};

#endif // PAL_PLATFORM_FIX_CALLING_CONVENTION_H