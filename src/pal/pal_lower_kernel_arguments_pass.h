#ifndef PAL_PLATFORM_LOWER_KERNEL_ARGUMENTS_H
#define PAL_PLATFORM_LOWER_KERNEL_ARGUMENTS_H

#include <llvm/IR/PassManager.h>

/// This pass replaces accesses to kernel arguments with loads from offsets from a manually supplied buffer
/// containing these arguments. The pointer to this buffer is expected to be prepopulated into specific sgprs
/// by the PALPlatform.
///
/// This pass is an almost 1:1 replicate of the AMDGPULowerKernelArguments pass
/// (llvm-project/llvm/lib/Target/AMDGPU/AMDGPULowerKernelArguments.cpp)
struct PalPlatformLowerKernelArgumentsPass : llvm::PassInfoMixin<PalPlatformLowerKernelArgumentsPass> {
    PalPlatformLowerKernelArgumentsPass(){}

    PalPlatformLowerKernelArgumentsPass(const PalPlatformLowerKernelArgumentsPass& other) = default;
    PalPlatformLowerKernelArgumentsPass& operator=(const PalPlatformLowerKernelArgumentsPass& other) = default;
    PalPlatformLowerKernelArgumentsPass(PalPlatformLowerKernelArgumentsPass&& other) = default;
    PalPlatformLowerKernelArgumentsPass& operator=(PalPlatformLowerKernelArgumentsPass&& other) = default;
    ~PalPlatformLowerKernelArgumentsPass() = default;

    llvm::PreservedAnalyses run(llvm::Function& F, llvm::FunctionAnalysisManager& FAM);
};

#endif // PAL_PLATFORM_LOWER_KERNEL_ARGUMENTS_H