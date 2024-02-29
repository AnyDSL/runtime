#ifndef PAL_PLATFORM_LOWER_BUILTINS_H
#define PAL_PLATFORM_LOWER_BUILTINS_H

#include <llvm/CodeGen/TargetPassConfig.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/IR/PassManager.h>
#include <llvm/Pass.h>
#include <llvm/Transforms/IPO/PassManagerBuilder.h>

#include <pal.h>
#include <palDevice.h>

/// This pass takes care of replacing calls to so-called "builtins" (i.e. local/global thread indices, and similar
/// compute shader builtin values) with the appropriate amdgpu inline assembly that extracts the values from
/// prepopulated SGPRs according to the RDNA2 or RDNA3 ABI. This pass only supports gfx-levels 10 and 11.
struct PalPlatformLowerBuiltinsPass : llvm::PassInfoMixin<PalPlatformLowerBuiltinsPass> {
    PalPlatformLowerBuiltinsPass(
        Pal::GfxIpLevel gfx_level, std::array<uint64_t, 3> tg_dims)
        : gfx_level_(gfx_level)
        , tg_dims_(tg_dims) {}

    PalPlatformLowerBuiltinsPass(const PalPlatformLowerBuiltinsPass& other) = default;
    PalPlatformLowerBuiltinsPass& operator=(const PalPlatformLowerBuiltinsPass& other) = default;
    PalPlatformLowerBuiltinsPass(PalPlatformLowerBuiltinsPass&& other) = default;
    PalPlatformLowerBuiltinsPass& operator=(PalPlatformLowerBuiltinsPass&& other) = default;
    ~PalPlatformLowerBuiltinsPass() = default;

    llvm::PreservedAnalyses run(llvm::Module& M, llvm::ModuleAnalysisManager&);

private:
    Pal::GfxIpLevel gfx_level_;
    std::array<uint64_t, 3> tg_dims_;
};

#endif // PAL_PLATFORM_LOWER_BUILTINS_H