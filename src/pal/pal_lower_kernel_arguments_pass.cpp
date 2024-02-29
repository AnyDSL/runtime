#include "pal_lower_kernel_arguments_pass.h"
#include "pal_utils.h"

#include <optional>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CallingConv.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/Support/Alignment.h>

using namespace llvm;

namespace {
// anonymous namespace to avoid name clashes

// Function taken from llvm-project/llvm/lib/Target/AMDGPU/AMDGPULowerKernelArguments.cpp
BasicBlock::iterator getInsertPt(BasicBlock& BB) {
    BasicBlock::iterator InsPt = BB.getFirstInsertionPt();
    for (BasicBlock::iterator E = BB.end(); InsPt != E; ++InsPt) {
        AllocaInst* AI = dyn_cast<AllocaInst>(&*InsPt);

        // If this is a dynamic alloca, the value may depend on the loaded kernargs,
        // so loads will need to be inserted before it.
        if (!AI || !AI->isStaticAlloca())
            break;
    }

    return InsPt;
}

// Function based on AMDGPUSubtarget::getExplicitKernArgSize(const Function &F, Align &MaxAlign)
// Taken from llvm-project/llvm/lib/Target/AMDGPU/AMDGPUSubtarget.cpp
uint64_t getExplicitKernArgSize(const Function& F, Align& MaxAlign) {
    assert(F.getCallingConv() == CallingConv::AMDGPU_CS);

    const DataLayout& DL = F.getParent()->getDataLayout();
    uint64_t ExplicitArgBytes = 0;
    MaxAlign = Align(1);

    for (const Argument& Arg : F.args()) {
        const bool IsByRef = Arg.hasByRefAttr();
        Type* ArgTy = IsByRef ? Arg.getParamByRefType() : Arg.getType();
        MaybeAlign ParamAlign = IsByRef ? Arg.getParamAlign() : std::nullopt;
        Align ABITypeAlign = DL.getValueOrABITypeAlignment(ParamAlign, ArgTy);

        uint64_t AllocSize = DL.getTypeAllocSize(ArgTy);
        ExplicitArgBytes = alignTo(ExplicitArgBytes, ABITypeAlign) + AllocSize;
        MaxAlign = std::max(MaxAlign, ABITypeAlign);
    }

    return ExplicitArgBytes;
}

// Function based on AMDGPUSubtarget::getKernArgSegmentSize(const Function &F, Align &MaxAlign)
// Taken from llvm-project/llvm/lib/Target/AMDGPU/AMDGPUSubtarget.cpp
unsigned getKernArgSegmentSize(const Function& F, Align& MaxAlign) {
    uint64_t ExplicitArgBytes = getExplicitKernArgSize(F, MaxAlign);
    unsigned ExplicitOffset = 0;
    // Being able to dereference past the end is useful for emitting scalar loads.
    return alignTo(ExplicitOffset + ExplicitArgBytes, 4);
}
} // namespace

// Largely based on the function AMDGPULowerKernelArguments::runOnFunction(Function &F)
// taken from llvm-project/llvm/lib/Target/AMDGPU/AMDGPULowerKernelArguments.cpp
// Minor adaptations added to satisfy the AnyDSL PALPlatform requirements.
PreservedAnalyses PalPlatformLowerKernelArgumentsPass::run(Function& F, FunctionAnalysisManager&) {
    const auto& funcname = F.getName();
    if (funcname != pal_utils::ComputeShaderMainFnName || F.arg_empty()) {
        // Only the entry point function's parameters are kernel arguments that need to be lowered.
        return PreservedAnalyses::all();
    }
    assert(F.getCallingConv() == CallingConv::AMDGPU_CS);

    LLVMContext& Ctx = F.getParent()->getContext();
    const DataLayout& DL = F.getParent()->getDataLayout();
    BasicBlock& EntryBlock = *F.begin();
    IRBuilder<> Builder(&*getInsertPt(EntryBlock));

    const Align KernArgBaseAlign(16); // FIXME: Increase if necessary
    const uint64_t BaseOffset = 0;    // We don't have any data preceding the kernel arguments

    Align MaxAlign;
    // TODO: We have to extract that from the Function arguments ourselves!
    const uint64_t TotalKernArgSize = getKernArgSegmentSize(F, MaxAlign);
    if (TotalKernArgSize == 0)
        return PreservedAnalyses::all();

    // Generate Our own ISA to get the pointer to the buffer containing the kernel arguments
    // PALPlatform ensures that registers s[2:3] contain this address when the kernel starts execution
    std::string asmString = std::string("; def $0 pointer to buffer containing the kernel args is set up in s[2:3]");
    // Constraints reference: https://llvm.org/docs/LangRef.html#inline-asm-constraint-string
    // This constraint states that our inline assembly returns ("="-prefix indicates constraint for output)
    // its result in sgprs 2-3
    StringRef constraints = "={s[2:3]}";
    ArrayRef<Value*> inline_asm_args = std::nullopt;

    // Value taken from AMDGPU.h (namespace AMDGPUAS)
    // global address space pointing to memory that won't change during execution
    unsigned CONSTANT_ADDRESS = 4;
    InlineAsm* inline_assembly =
        InlineAsm::get(FunctionType::get(Type::getInt8PtrTy(Ctx, CONSTANT_ADDRESS), false), asmString.c_str(),
            constraints, true, false, InlineAsm::AD_ATT);
    CallInst* KernArgSegment = Builder.CreateCall(inline_assembly, inline_asm_args);

    KernArgSegment->addRetAttr(Attribute::NonNull);
    KernArgSegment->addRetAttr(Attribute::getWithDereferenceableBytes(Ctx, TotalKernArgSize));
    unsigned AS = KernArgSegment->getType()->getPointerAddressSpace();

    uint64_t ExplicitArgOffset = 0;

    for (Argument& Arg : F.args()) {
        const bool IsByRef = Arg.hasByRefAttr();
        Type* ArgTy = IsByRef ? Arg.getParamByRefType() : Arg.getType();
        MaybeAlign ParamAlign = IsByRef ? Arg.getParamAlign() : std::nullopt;
        Align ABITypeAlign = DL.getValueOrABITypeAlignment(ParamAlign, ArgTy);

        uint64_t Size = DL.getTypeSizeInBits(ArgTy);
        uint64_t AllocSize = DL.getTypeAllocSize(ArgTy);

        uint64_t EltOffset = alignTo(ExplicitArgOffset, ABITypeAlign) + BaseOffset;
        ExplicitArgOffset = alignTo(ExplicitArgOffset, ABITypeAlign) + AllocSize;

        if (Arg.use_empty())
            continue;

        // If this is byval, the loads are already explicit in the function. We just
        // need to rewrite the pointer values.
        if (IsByRef) {
            Value* ArgOffsetPtr = Builder.CreateConstInBoundsGEP1_64(
                Builder.getInt8Ty(), KernArgSegment, EltOffset, Arg.getName() + ".byval.kernarg.offset");

            Value* CastOffsetPtr = Builder.CreatePointerBitCastOrAddrSpaceCast(ArgOffsetPtr, Arg.getType());
            Arg.replaceAllUsesWith(CastOffsetPtr);
            continue;
        }

        if (PointerType* PT = dyn_cast<PointerType>(ArgTy)) {
            // FIXME: Hack. We rely on AssertZext to be able to fold DS addressing
            // modes on SI to know the high bits are 0 so pointer adds don't wrap. We
            // can't represent this with range metadata because it's only allowed for
            // integer types.

            // Values taken from AMDGPU.h (namespace AMDGPUAS)
            const unsigned REGION_ADDRESS = 2; ///< Address space for region memory. (GDS)
            const unsigned LOCAL_ADDRESS = 3;  ///< Address space for local memory.
            if ((PT->getAddressSpace() == LOCAL_ADDRESS || PT->getAddressSpace() == REGION_ADDRESS))
                continue;

            // FIXME: We can replace this with equivalent alias.scope/noalias
            // metadata, but this appears to be a lot of work.
            if (Arg.hasNoAliasAttr())
                continue;
        }

        auto* VT = dyn_cast<FixedVectorType>(ArgTy);
        bool IsV3 = VT && VT->getNumElements() == 3;
        bool DoShiftOpt = Size < 32 && !ArgTy->isAggregateType();

        VectorType* V4Ty = nullptr;

        int64_t AlignDownOffset = alignDown(EltOffset, 4);
        int64_t OffsetDiff = EltOffset - AlignDownOffset;
        Align AdjustedAlign = commonAlignment(KernArgBaseAlign, DoShiftOpt ? AlignDownOffset : EltOffset);

        Value* ArgPtr;
        Type* AdjustedArgTy;
        if (DoShiftOpt) { // FIXME: Handle aggregate types
            // Since we don't have sub-dword scalar loads, avoid doing an extload by
            // loading earlier than the argument address, and extracting the relevant
            // bits.
            //
            // Additionally widen any sub-dword load to i32 even if suitably aligned,
            // so that CSE between different argument loads works easily.
            ArgPtr = Builder.CreateConstInBoundsGEP1_64(Builder.getInt8Ty(), KernArgSegment, AlignDownOffset,
                Arg.getName() + ".kernarg.offset.align.down");
            AdjustedArgTy = Builder.getInt32Ty();
        } else {
            ArgPtr = Builder.CreateConstInBoundsGEP1_64(
                Builder.getInt8Ty(), KernArgSegment, EltOffset, Arg.getName() + ".kernarg.offset");
            AdjustedArgTy = ArgTy;
        }

        if (IsV3 && Size >= 32) {
            V4Ty = FixedVectorType::get(VT->getElementType(), 4);
            // Use the hack that clang uses to avoid SelectionDAG ruining v3 loads
            AdjustedArgTy = V4Ty;
        }

        ArgPtr = Builder.CreateBitCast(ArgPtr, AdjustedArgTy->getPointerTo(AS), ArgPtr->getName() + ".cast");
        LoadInst* Load = Builder.CreateAlignedLoad(AdjustedArgTy, ArgPtr, AdjustedAlign);
        Load->setMetadata(LLVMContext::MD_invariant_load, MDNode::get(Ctx, {}));

        MDBuilder MDB(Ctx);

        if (isa<PointerType>(ArgTy)) {
            if (Arg.hasNonNullAttr())
                Load->setMetadata(LLVMContext::MD_nonnull, MDNode::get(Ctx, {}));

            uint64_t DerefBytes = Arg.getDereferenceableBytes();
            if (DerefBytes != 0) {
                Load->setMetadata(LLVMContext::MD_dereferenceable,
                    MDNode::get(Ctx, MDB.createConstant(ConstantInt::get(Builder.getInt64Ty(), DerefBytes))));
            }

            uint64_t DerefOrNullBytes = Arg.getDereferenceableOrNullBytes();
            if (DerefOrNullBytes != 0) {
                Load->setMetadata(LLVMContext::MD_dereferenceable_or_null,
                    MDNode::get(
                        Ctx, MDB.createConstant(ConstantInt::get(Builder.getInt64Ty(), DerefOrNullBytes))));
            }

            auto ParamMaybeAlign = Arg.getParamAlign();
            if (ParamMaybeAlign.has_value()) {
                Load->setMetadata(LLVMContext::MD_align,
                    MDNode::get(Ctx, MDB.createConstant(ConstantInt::get(
                                         Builder.getInt64Ty(), ParamMaybeAlign.valueOrOne().value()))));
            }
        }

        // TODO: Convert noalias arg to !noalias

        if (DoShiftOpt) {
            Value* ExtractBits = OffsetDiff == 0 ? Load : Builder.CreateLShr(Load, OffsetDiff * 8);

            IntegerType* ArgIntTy = Builder.getIntNTy(Size);
            Value* Trunc = Builder.CreateTrunc(ExtractBits, ArgIntTy);
            Value* NewVal = Builder.CreateBitCast(Trunc, ArgTy, Arg.getName() + ".load");
            Arg.replaceAllUsesWith(NewVal);
        } else if (IsV3) {
            Value* Shuf = Builder.CreateShuffleVector(Load, ArrayRef<int>{0, 1, 2}, Arg.getName() + ".load");
            Arg.replaceAllUsesWith(Shuf);
        } else {
            Load->setName(Arg.getName() + ".load");
            Arg.replaceAllUsesWith(Load);
        }
    }

    KernArgSegment->addRetAttr(Attribute::getWithAlignment(Ctx, std::max(KernArgBaseAlign, MaxAlign)));

    return PreservedAnalyses::none();
}