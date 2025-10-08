#include "pal_lower_builtins_pass.h"
#include "pal_utils.h"

#include <array>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <llvm/IR/CallingConv.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalValue.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/InlineAsm.h>
#include <llvm/IR/MDBuilder.h>
#include <llvm/Target/TargetMachine.h>

using namespace llvm;

namespace {
// anonymous namespace to avoid name clashes

enum Builtins : int8_t {
    workitem_id_x = 0,
    workitem_id_y,
    workitem_id_z,
    workgroup_id_x,
    workgroup_id_y,
    workgroup_id_z,
    nblk_x,
    nblk_y,
    nblk_z,
    // Dynamic builtins (i.e., inlined code based on supplied metadata)
    workgroup_size_x,
    workgroup_size_y,
    workgroup_size_z,
    count
};

constexpr const char* BuiltinNames[] = {
    "anydsl.amdpal.workitem.id.x",
    "anydsl.amdpal.workitem.id.y",
    "anydsl.amdpal.workitem.id.z",
    "anydsl.amdpal.workgroup.id.x",
    "anydsl.amdpal.workgroup.id.y",
    "anydsl.amdpal.workgroup.id.z",
    "anydsl.amdpal.nblk.x",
    "anydsl.amdpal.nblk.y",
    "anydsl.amdpal.nblk.z",
    // Dynamic builtins (i.e., inlined code based on supplied metadata)
    "anydsl.amdpal.workgroup.size.x",
    "anydsl.amdpal.workgroup.size.y",
    "anydsl.amdpal.workgroup.size.z",
};

struct BuiltinAssemblyInfo {
    const char* asmString;
    const char* asmConstraints;
};

// PAL SGPR layout:
// s0-1:   PAL reserved data -> set up by PAL because of pipeline register configuration in PALPlatform
// s2-3:   pointer to pal kernel args (for compute shader)
//         -> set up by AnyDSL PALPlatform
// s4-5:   pointer to NumWorkGroups struct (i.e., nblk)
// s6-12:  reserved for future use
// s13-15: work group id x, y, and z -> set up by AnyDSL PALPlatform by supplying pgm_rsrc2
//         ENABLE_SGPR_WORKGROUP_ID_<x/y/z> to PAL pipeline setup

const BuiltinAssemblyInfo BuiltinAssemblyInfos[]{
    // workitem_id_x
    {
        .asmString = "; local thread id x is in v0",
        .asmConstraints = "={v0}",
    },
    // workitem_id_y
    {
        .asmString = "; local thread id y is in v1",
        .asmConstraints = "={v1}",
    },
    // workitem_id_z
    {
        .asmString = "; local thread id z is in v2",
        .asmConstraints = "={v2}",
    },
    // workgroup_id_x
    {
        .asmString = "; workgroup id x is in s13",
        .asmConstraints = "={s13}",
    },
    // workgroup_id_y
    {
        .asmString = "; workgroup id y is in s14",
        .asmConstraints = "={s14}",
    },
    // workgroup_id_z
    {
        .asmString = "; workgroup id z is in s15",
        .asmConstraints = "={s15}",
    },
    // nblk_x
    {
        .asmString = "s_load_dword $0, s[4:5], 0x00",
        .asmConstraints = "=s",
    },
    // nblk_y
    {
        .asmString = "s_load_dword $0, s[4:5], 0x04",
        .asmConstraints = "=s",
    },
    // nblk_z
    {
        .asmString = "s_load_dword $0, s[4:5], 0x08",
        .asmConstraints = "=s",
    },
};

typedef std::array<std::vector<CallInst*>, static_cast<size_t>(Builtins::count)> BuiltinsCallInstMap;

Builtins GetBuiltinID(Function* f) {
    const StringRef f_name = f->getName();
    for (int8_t i = 0; i < Builtins::count; ++i) {
        if (f_name == BuiltinNames[i]) {
            return Builtins(i);
        }
    }
    return Builtins::count;
}

const BuiltinAssemblyInfo& GetAssemblyInfo(Builtins builtinID) {
    return BuiltinAssemblyInfos[static_cast<int>(builtinID)];
}

bool IsBuiltin(Function* f) { return GetBuiltinID(f) < Builtins::count; }

void find_builtins_calls(Module* m, Function* f, BuiltinsCallInstMap& builtins_call_instances,
    std::unordered_set<Function*>& traversed_functions) {
    if (traversed_functions.find(f) != traversed_functions.end()) {
        // already visited this function -> prevent recursive loop
        return;
    }

    traversed_functions.insert(f);

    // Find and inspect all function calls inside of this function
    for (auto& bb : *f) {
        for (auto& instruction : bb) {
            CallInst* callInst = dyn_cast<CallInst>(&instruction);
            if (!callInst) {
                continue;
            }
            
            Function* calledFunction = callInst->getCalledFunction();
            if (!calledFunction) {
                continue;
            }

            if (IsBuiltin(calledFunction)) {
                // If the call we found is calling a builtin, record the builtins usage
                Builtins builtinID = GetBuiltinID(calledFunction);
                builtins_call_instances[static_cast<int>(builtinID)].push_back(callInst);
            } else if (calledFunction->getParent() == m) {
                // If the called function is within this module, recursively search it for
                // builtins used
                find_builtins_calls(m, calledFunction, builtins_call_instances, traversed_functions);
            }
        }
    }
}

Function* find_entrypoint(Module& M) {
    for (Function& F : M) {
        const auto name = F.getName();
        if (name.equals(pal_utils::ComputeShaderMainFnName))
            return &F;
    }

    return nullptr;
}

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

CallInst* insert_asm(
    IRBuilder<>& Builder, LLVMContext& Ctx, const char* asm_string, const char* asm_constraint) {
    ArrayRef<Value*> inline_asm_args;
    InlineAsm* inline_assembly = InlineAsm::get(FunctionType::get(Type::getInt32Ty(Ctx), false), asm_string,
        asm_constraint, true, false, InlineAsm::AD_ATT);
    return Builder.CreateCall(inline_assembly, inline_asm_args);
}

// Inserts assembly code to split the local thread id from v0 into v0(x), v1(y) and v2(z).
// This is only applicable for GPUs >= gfx 11.
void insert_asm_to_split_local_thread_id(IRBuilder<>& Builder, LLVMContext& Ctx,
    const BuiltinsCallInstMap& builtins_call_instances, Pal::GfxIpLevel gfx_level) {
    assert(gfx_level >= Pal::GfxIpLevel::GfxIp11_0);
    // Write local thread id z into v2.
    if (!builtins_call_instances[Builtins::workitem_id_z].empty()) {
        insert_asm(Builder, Ctx,
            "; def v2 local thread id z is in v0[29:20] (v0[31:30] set to 0 by hardware)\n\t"
            "V_LSHRREV_B32 v2 20 v0",
            "={v2}");
    }
    // Write local thread id y into v1.
    if (!builtins_call_instances[Builtins::workitem_id_y].empty()) {
        insert_asm(Builder, Ctx,
            "; def v1 local thread id y is in v0[19:10]\n\t"
            "V_LSHRREV_B32 v1 10 v0\n\t"
            "V_AND_B32 v1 v1 0x3FF",
            "={v1}");
    }
    // Write local thread id x into v0 last to make sure v0 is not overwritten yet.
    if (!builtins_call_instances[Builtins::workitem_id_x].empty()) {
        insert_asm(Builder, Ctx,
            "; def v0 local thread id x is in v0[9:0]\n\t"
            "V_AND_B32 v0 v0 0x3FF",
            "={v0}");
    }
}

} // namespace

PreservedAnalyses PalPlatformLowerBuiltinsPass::run(Module& M, ModuleAnalysisManager&) {
    Function* entrypoint_fn = find_entrypoint(M);
    assert(entrypoint_fn);

    /*
    Find all calls to builtins and unique them
    -> i.e. every builtin is only called exactly once right at the beginning of the shader.

    for each instruction in entrypoint:
        if call to builtin:
            record builtin (unique set of used_builtins + all separate calls to them!)
        elif call to another function inside this module:
            recursively find all calls of used built_ins
        else: don't care

    for each used_builtin:
        Value* real_builtin = insert inline_asm at beginning of entrypoint
        for each call instance of the builtin:
            replace all uses of call instance with real_builtin
            remove old call instance
    */

    BuiltinsCallInstMap builtins_call_instances;
    std::unordered_set<Function*> traversed_functions = {};
    find_builtins_calls(&M, entrypoint_fn, builtins_call_instances, traversed_functions);

    LLVMContext& Ctx = M.getContext();
    BasicBlock& EntryBlock = *entrypoint_fn->begin();
    IRBuilder<> Builder(&*getInsertPt(EntryBlock));

    if (gfx_level_ >= Pal::GfxIpLevel::GfxIp11_0) {
        insert_asm_to_split_local_thread_id(Builder, Ctx, builtins_call_instances, gfx_level_);
    }

    int builtins_count = static_cast<int>(Builtins::count);
    for (int i = 0; i < builtins_count; ++i) {
        const Builtins builtin_id = Builtins(i);
        const std::vector<CallInst*> builtin_call_instances = builtins_call_instances[i];
        if (builtin_call_instances.empty()) {
            continue;
        }

        CallInst* lowered_unique_builtin = nullptr;
        switch (builtin_id) {
            case Builtins::workgroup_size_x:
                lowered_unique_builtin = insert_asm(Builder, Ctx,
                    ("s_mov_b32 $0, " + std::to_string(tg_dims_[0]) + "; workgroup size x").c_str(), "=s");
                break;
            case Builtins::workgroup_size_y:
                lowered_unique_builtin = insert_asm(Builder, Ctx,
                    ("s_mov_b32 $0, " + std::to_string(tg_dims_[1]) + "; workgroup size y").c_str(), "=s");
                break;
            case Builtins::workgroup_size_z:
                lowered_unique_builtin = insert_asm(Builder, Ctx,
                    ("s_mov_b32 $0, " + std::to_string(tg_dims_[2]) + "; workgroup size z").c_str(), "=s");
                break;
            default:
                const auto& assemblyInfo = GetAssemblyInfo(builtin_id);
                lowered_unique_builtin =
                    insert_asm(Builder, Ctx, assemblyInfo.asmString, assemblyInfo.asmConstraints);
        }

        for (CallInst* call_to_builtin : builtin_call_instances) {
            call_to_builtin->replaceAllUsesWith(lowered_unique_builtin);
        }
    }

    for (int i = 0; i < static_cast<int>(builtins_count); ++i) {
        const std::vector<CallInst*> builtin_call_instances = builtins_call_instances[i];
        for (CallInst* call_to_builtin : builtin_call_instances) {
            call_to_builtin->eraseFromParent();
        }
    }
    // All uncalled functions from the module have to be removed because any kernels other than the one
    // marked as entrypoint may contain calls to builtins which have not been resolved by this pass but
    // may trip up linkers/relocations. Therefore we set all functions to internal linkage, except the
    // known entrypoint. This way, the global dead code elimination pass can remove them for us.
    for (Function& F : M) {
        if (F.getName().startswith("llvm")) {
            // Don't mark llvm intrinsics as internal linkage, otherwise they get
            // altered/removed which breaks backend codegen.
            continue;
        }
        F.setLinkage(GlobalValue::LinkageTypes::InternalLinkage);
    }
    entrypoint_fn->setLinkage(GlobalValue::LinkageTypes::ExternalLinkage);

    return PreservedAnalyses::none();
}