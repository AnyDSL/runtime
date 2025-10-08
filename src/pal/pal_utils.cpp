#include "pal_utils.h"

#include "../runtime.h"

#include <g_palPipelineAbiMetadata.h>
#include <gfx9_plus_merged_offset.h>
#include <palMetroHash.h>
#include <palPipelineAbi.h>

#include <llvm/IR/Constants.h>
#include <llvm/IRReader/IRReader.h>

namespace pal_utils {

std::string llvm_diagnostic_to_string(const llvm::SMDiagnostic& diagnostic_err) {
    std::string stream;
    llvm::raw_string_ostream llvm_stream(stream);
    diagnostic_err.print("", llvm_stream);
    llvm_stream.flush();
    return stream;
}

const char* ComputeShaderMainFnName =
    Util::Abi::PipelineAbiSymbolNameStrings[static_cast<int>(Util::Abi::PipelineSymbolType::CsMainEntry)];

// clang-format off
    // Meaning of the different bits taken from: https://llvm.org/docs/AMDGPUUsage.html#amdgpu-amdhsa-compute-pgm-rsrc1-gfx6-gfx11-table
    // PGM_RSRC1 Register:
    /*        
        [5:0]   - GRANULATED_WORKITEM_VGPR_COUNT    -> set to vgpr count / 4 (if wave64) or 8 (if wave32)
        [9:6]   - GRANULATED_WAVEFRONT_SGPR_COUNT   -> gfx10 and up: must be 0;
        [11:10] - PRIORITY                          -> must be 0
        [13:12] - FLOAT_ROUND_MODE_32               -> see: https://llvm.org/docs/AMDGPUUsage.html#amdgpu-amdhsa-floating-point-rounding-mode-enumeration-values-table
        [15:14] - FLOAT_ROUND_MODE_16_64            -"-
        [17:16] - FLOAT_DENORM_MODE_32              -> see: -> see: https://llvm.org/docs/AMDGPUUsage.html#amdgpu-amdhsa-floating-point-denorm-mode-enumeration-values-table
        [19:18] - FLOAT_DENORM_MODE_16_64           -"-
        [20]    - PRIV                              -> must be 0
        [21]    - ENABLE_DX10_CLAMP                 -> Dx10 clamp mode on or off
        [22]    - DEBUG_MODE                        -> must be 0
        [23]    - ENABLE_IEEE_MODE                  -> IEEE Mode flag
        [24]    - BULKY                             -> must be 0
        [25]    - CDBG_USER                         -> must be 0
        [26]    - FP16_OVFL    // GFX9+             -> overflow mode for fp16
        [28:27] - RESERVED0                         -> must be 0
        [29]    - WGP_MODE     // GFX10+            -> 0: waves in CU-mode; 1: waves in WGP-mode
        [30]    - MEM_ORDERED // GFX10+             -> 0: reports completion of load and atomic out of order; 1: in order
        [31]    - FWD_PROGRESS // GFX10+            -> 0: SIMD wavefronts oldest first; 1: some forward progress
    */
// clang-format on
struct PGM_RSRC1 {
    union {
        struct {
            uint32_t GRANULATED_WORKITEM_VGPR_COUNT : 6;
            uint32_t GRANULATED_WAVEFRONT_SGPR_COUNT : 4;
            uint32_t PRIORITY : 2;
            uint32_t FLOAT_ROUND_MODE_32 : 2;
            uint32_t FLOAT_ROUND_MODE_16_64 : 2;
            uint32_t FLOAT_DENORM_MODE_32 : 2;
            uint32_t FLOAT_DENORM_MODE_16_64 : 2;
            uint32_t PRIV : 1;
            uint32_t ENABLE_DX10_CLAMP : 1;
            uint32_t DEBUG_MODE : 1;
            uint32_t ENABLE_IEEE_MODE : 1;
            uint32_t BULKY : 1;
            uint32_t CDBG_USER : 1;
            uint32_t FP16_OVFL : 1;
            uint32_t RESERVED0 : 2;
            uint32_t WGP_MODE : 1;
            uint32_t MEM_ORDERED : 1;
            uint32_t FWD_PROGRESS : 1;
        } bits;
        uint32_t u32All = 0;
    };
};

// clang-format off
    // PGM_RSRC2 Register:
    /*        
        [0]     - ENABLE_PRIVATE_SEGMENT                            -> hsa specifics...     
        [5:1]   - USER_SGPR_COUNT                                   -> total number of requested user data sgprs
        [6]     - ENABLE_TRAP_HANDLER                               -> must be 0
        [7]     - ENABLE_SGPR_WORKGROUP_ID_X                        -> enable setup of system sgpr register for x dimension
        [8]     - ENABLE_SGPR_WORKGROUP_ID_Y                        -> enable setup of system sgpr register for y dimension
        [9]     - ENABLE_SGPR_WORKGROUP_ID_Z                        -> enable setup of system sgpr register for z dimension
        [10]    - ENABLE_SGPR_WORKGROUP_INFO                        -> enable setup of system sgpr register for x dimension
        [12:11] - ENABLE_VGPR_WORKITEM_ID                           -> enable setup of vgprs with workitem id (0: only x, 1: x&y, 2: x&y&z)
        [13]    - ENABLE_EXCEPTION_ADDRESS_WATCH                    -> must be 0 
        [14]    - ENABLE_EXCEPTION_MEMORY                           -> must be 0
        [23:15] - GRANULATED_LDS_SIZE                               -> must be 0
        [24]    - ENABLE_EXCEPTION_IEEE_754_FP_INVALID_OPERATION    -> wavefront starts with specified exceptions enabled
        [25]    - ENABLE_EXCEPTION_FP_DENORMAL_SOURCE               
        [26]    - ENABLE_EXCEPTION_IEEE_754_FP_DIVISION_BY_ZERO
        [27]    - ENABLE_EXCEPTION_IEEE_754_FP_OVERFLOW
        [28]    - ENABLE_EXCEPTION_IEEE_754_FP_UNDERFLOW
        [29]    - ENABLE_EXCEPTION_IEEE_754_FP_INEXACT
        [30]    - ENABLE_EXCEPTION_INT_DIVIDE_BY_ZERO
        [31]    - RESERVED0                                         -> must be 0
    */
// clang-format on
struct PGM_RSRC2 {
    union {
        struct {
            uint32_t ENABLE_PRIVATE_SEGMENT : 1;
            uint32_t USER_SGPR_COUNT : 5;
            uint32_t ENABLE_TRAP_HANDLER : 1;
            uint32_t ENABLE_SGPR_WORKGROUP_ID_X : 1;
            uint32_t ENABLE_SGPR_WORKGROUP_ID_Y : 1;
            uint32_t ENABLE_SGPR_WORKGROUP_ID_Z : 1;
            uint32_t ENABLE_SGPR_WORKGROUP_INFO : 1;
            uint32_t ENABLE_VGPR_WORKITEM_ID : 2;
            uint32_t ENABLE_EXCEPTION_ADDRESS_WATCH : 1;
            uint32_t ENABLE_EXCEPTION_MEMORY : 1;
            uint32_t GRANULATED_LDS_SIZE : 9;
            uint32_t ENABLE_EXCEPTION_IEEE_754_FP_INVALID_OPERATION : 1;
            uint32_t ENABLE_EXCEPTION_FP_DENORMAL_SOURCE : 1;
            uint32_t ENABLE_EXCEPTION_IEEE_754_FP_DIVISION_BY_ZERO : 1;
            uint32_t ENABLE_EXCEPTION_IEEE_754_FP_OVERFLOW : 1;
            uint32_t ENABLE_EXCEPTION_IEEE_754_FP_UNDERFLOW : 1;
            uint32_t ENABLE_EXCEPTION_IEEE_754_FP_INEXACT : 1;
            uint32_t ENABLE_EXCEPTION_INT_DIVIDE_BY_ZERO : 1;
            uint32_t RESERVED0 : 1;
        } bits;
        uint32_t u32All = 0;
    };
};

llvm::StringRef get_metadata_string(const llvm::Function* func, const char* key) {
    return llvm::cast<llvm::MDString>(func->getMetadata(key)->getOperand(0))->getString();
}

uint64_t get_metadata_uint(const llvm::Function* func, const char* key, int index) {
    llvm::MDNode* node = func->getMetadata(key);

    llvm::Metadata* the_data = node->getOperand(index).get();

    // Level of indirection due to how llvm uniques metadata nodes
    if (llvm::MDTuple::classof(the_data)) {
        const llvm::MDTuple* md_tuple = static_cast<llvm::MDTuple*>(the_data);
        assert(md_tuple->getNumOperands() == 1);
        the_data = md_tuple->getOperand(0).get();
    }

    assert(llvm::ConstantAsMetadata::classof(the_data));
    // First cast to the appropriate Metadata type
    const llvm::ConstantAsMetadata* md_constant = static_cast<llvm::ConstantAsMetadata*>(the_data);
    // Then cast to the constant type (which we know to be unsigned int)
    const llvm::ConstantInt* constant = static_cast<llvm::ConstantInt*>(md_constant->getValue());
    // Return the constant's value as an unsigned int
    return constant->getZExtValue();
}

llvm::MDNode* get_metadata_mdnode(const llvm::Function* func, const char* key, int index) {
    return llvm::cast<llvm::MDNode>(func->getMetadata(key)->getOperand(index));
}

llvm::msgpack::Document build_metadata(const ShaderSrc& shader_src, Pal::GfxIpLevel gfx_level,
    const std::array<uint64_t, 3>& thread_group_dimensions, uint32_t wavefront_size) {
    llvm::msgpack::Document document;

    // Add the metadata version number.
    auto versionNode =
        document.getRoot().getMap(true)[Util::PalAbi::CodeObjectMetadataKey::Version].getArray(true);
    versionNode[0] = Util::PalAbi::PipelineMetadataMajorVersion;
    versionNode[1] = Util::PalAbi::PipelineMetadataMinorVersion;

    auto pipelinesNode =
        document.getRoot().getMap(true)[Util::PalAbi::CodeObjectMetadataKey::Pipelines].getArray(true);
    auto pipeline_node = pipelinesNode[0].getMap(true);
    pipeline_node[Util::PalAbi::PipelineMetadataKey::Api].fromString("Vulkan");
    pipeline_node[Util::PalAbi::PipelineMetadataKey::Type].fromString("Cs");
    // ".cs" taken from "pal/inc/core/g_palPipelineAbiMetadataImpl.h" SerializeEnum()
    auto compute_hwstage_node =
        pipeline_node[Util::PalAbi::PipelineMetadataKey::HardwareStages].getMap(true)[".cs"].getMap(true);
    auto tg_dims_node =
        compute_hwstage_node[Util::PalAbi::HardwareStageMetadataKey::ThreadgroupDimensions].getArray(true);
    tg_dims_node[0] = thread_group_dimensions[0];
    tg_dims_node[1] = thread_group_dimensions[1];
    tg_dims_node[2] = thread_group_dimensions[2];

    compute_hwstage_node[Util::PalAbi::HardwareStageMetadataKey::WavefrontSize] = wavefront_size;
    // Values can be found in llpc/lgc/state/TargetInfo.cpp
    // (https://github.com/GPUOpen-Drivers/llpc/blob/dev/lgc/state/TargetInfo.cpp)
    compute_hwstage_node[Util::PalAbi::HardwareStageMetadataKey::SgprLimit] = 102;
    compute_hwstage_node[Util::PalAbi::HardwareStageMetadataKey::VgprLimit] = 256;

    Util::MetroHash::Hash hash = {};
    Util::MetroHash64::Hash(reinterpret_cast<const uint8_t*>(shader_src.src_code.c_str()), shader_src.src_code.length(), hash.bytes);

    uint64_t hash_compacted =
        Util::MetroHash::Compact64(reinterpret_cast<const Util::MetroHash::Hash*>(&hash));
    uint32_t hash_lower = 0xFFFFFFFF & hash_compacted;
    uint32_t hash_upper = hash_compacted >> 32;

    auto pipelineHash =
        pipeline_node.getMap(true)[Util::PalAbi::PipelineMetadataKey::InternalPipelineHash].getArray(true);
    pipelineHash[0] = hash_lower;
    pipelineHash[1] = hash_upper;

    auto registers_node = pipeline_node[".registers"].getMap(true);

    auto getKeyAsDocNode = [&document](unsigned int key) {
        auto key_doc_node = document.getEmptyNode();
        key_doc_node = key;
        return key_doc_node;
    };

    registers_node[getKeyAsDocNode(Pal::Gfx9::Chip::mmCOMPUTE_NUM_THREAD_X)] = thread_group_dimensions[0];
    registers_node[getKeyAsDocNode(Pal::Gfx9::Chip::mmCOMPUTE_NUM_THREAD_Y)] = thread_group_dimensions[1];
    registers_node[getKeyAsDocNode(Pal::Gfx9::Chip::mmCOMPUTE_NUM_THREAD_Z)] = thread_group_dimensions[2];

    // Setup the resource registers according to:
    // https://github.com/GPUOpen-Drivers/llpc/blob/65016996d7c9ad421b4d04a2364dbb1d8f1f7f34/lgc/patch/Gfx9ConfigBuilder.cpp#L1880
    PGM_RSRC1 register_value_pgm_rsrc1 = {};
    register_value_pgm_rsrc1.bits.ENABLE_DX10_CLAMP = 1; // Follow PAL setting
    register_value_pgm_rsrc1.bits.MEM_ORDERED = 1;       // Follow LLPC setting

    PGM_RSRC2 register_value_pgm_rsrc2 = {};
    // PAL SGPR layout:
    // s0-1: PAL reserved data
    // s2-3: pointer to pal kernel args
    // s4-5: pointer to NumWorkGroups struct (i.e., nblk)
    // s6-12: reserved for future use
    // s13: block id x
    // s14: block id y
    // s15: block id z
    register_value_pgm_rsrc2.bits.USER_SGPR_COUNT = 13; // Don't count s13-15 as these are set up by hardware

    register_value_pgm_rsrc2.bits.ENABLE_SGPR_WORKGROUP_ID_X = 1;
    register_value_pgm_rsrc2.bits.ENABLE_SGPR_WORKGROUP_ID_Y = 1;
    register_value_pgm_rsrc2.bits.ENABLE_SGPR_WORKGROUP_ID_Z = 1;

    register_value_pgm_rsrc2.bits.ENABLE_VGPR_WORKITEM_ID = 2; // 0 = X, 1 = XY, 2 = XYZ

    registers_node[getKeyAsDocNode(Pal::Gfx9::Chip::mmCOMPUTE_PGM_RSRC1)] = register_value_pgm_rsrc1.u32All;
    registers_node[getKeyAsDocNode(Pal::Gfx9::Chip::mmCOMPUTE_PGM_RSRC2)] = register_value_pgm_rsrc2.u32All;

    if (gfx_level > Pal::GfxIpLevel::GfxIp9) {
        // RSRC3 doesn't contain values we are interested in for now, can all be 0 for us.
        uint32_t register_value_pgm_rsrc3 = 0;
        registers_node[getKeyAsDocNode(Pal::Gfx9::Chip::Gfx10Plus::mmCOMPUTE_PGM_RSRC3)] =
            register_value_pgm_rsrc3;

        uint32_t shader_checksum = hash_upper ^ hash_lower;
        registers_node[getKeyAsDocNode(Pal::Gfx9::Chip::Gfx10Plus::mmCOMPUTE_SHADER_CHKSUM)] =
            shader_checksum;
    }

    // Default SGPR setup
    // Let PAL set up user data 0 & 1-> PAL Global Table Pointer
    registers_node[getKeyAsDocNode(Pal::Gfx9::Chip::mmCOMPUTE_USER_DATA_0)] =
        static_cast<uint32_t>(Util::Abi::UserDataMapping::GlobalTable);

    // PALPlatform set up pointer to a struct containing the actual kernel arguments
    registers_node[getKeyAsDocNode(Pal::Gfx9::Chip::mmCOMPUTE_USER_DATA_2)] = 0;
    registers_node[getKeyAsDocNode(Pal::Gfx9::Chip::mmCOMPUTE_USER_DATA_3)] = 1;

    // Let PAL set up user data 4 & 5 -> PAL Num Work Group Pointer
    registers_node[getKeyAsDocNode(Pal::Gfx9::Chip::mmCOMPUTE_USER_DATA_4)] =
        static_cast<uint32_t>(Util::Abi::UserDataMapping::Workgroup);

    return document;
}

// This function is taken from llvm-project/llvm/lib/Target/AMDGPU/Utils/AMDGPUBaseInfo.cpp
bool isAMDGPUEntryFunctionCC(llvm::CallingConv::ID CC) {
    switch (CC) {
        case llvm::CallingConv::AMDGPU_KERNEL:
        case llvm::CallingConv::SPIR_KERNEL:
        case llvm::CallingConv::AMDGPU_VS:
        case llvm::CallingConv::AMDGPU_GS:
        case llvm::CallingConv::AMDGPU_PS:
        case llvm::CallingConv::AMDGPU_CS:
        case llvm::CallingConv::AMDGPU_ES:
        case llvm::CallingConv::AMDGPU_HS:
        case llvm::CallingConv::AMDGPU_LS: return true;
        default: return false;
    }
}

inline void check_pal_error(Pal::Result err, const char* name, const char* file, const int line) {
    if (err != Pal::Result::Success) {
        error("PAL API function % [file %, line %]: %", name, file, line, static_cast<int32_t>(err));
    }
}

void write_to_memory(
    Pal::IGpuMemory* dst_memory, int64_t dst_memory_offset, const void* src_data, int64_t size) {
    void* mapped_dst = nullptr;
    Pal::Result result = dst_memory->Map(&mapped_dst);
    CHECK_PAL(result, "write_to_memory | Map()");

    std::memcpy(static_cast<uint8_t*>(mapped_dst) + dst_memory_offset, src_data, size);

    result = dst_memory->Unmap();
    CHECK_PAL(result, "write_to_memory | Unmap()");
}

void read_from_memory(void* dst_buffer, Pal::IGpuMemory* src_memory, int64_t src_memory_offset, int64_t size) {
    void* mapped_src = nullptr;
    Pal::Result result = src_memory->Map(&mapped_src);
    CHECK_PAL(result, "read_to_buffer | Map()");

    std::memcpy(dst_buffer, static_cast<uint8_t*>(mapped_src) + src_memory_offset, size);

    result = src_memory->Unmap();
    CHECK_PAL(result, "read_to_buffer | Unmap()");
}

const char* get_gpu_name(const Pal::AsicRevision asic_revision) {
    // We don't support cards prior to the "Navi" generations
    switch (asic_revision) {
        case Pal::AsicRevision::Unknown: return "Unknown";
        case Pal::AsicRevision::Navi10: return "Navi10";
#if PAL_BUILD_NAVI12
        case Pal::AsicRevision::Navi12: return "Navi12";
#endif
#if PAL_BUILD_NAVI14
        case Pal::AsicRevision::Navi14: return "Navi14";
#endif
        case Pal::AsicRevision::Navi21: return "Navi21";
        case Pal::AsicRevision::Navi22: return "Navi22";
        case Pal::AsicRevision::Navi23: return "Navi23";
#if PAL_BUILD_NAVI24
        case Pal::AsicRevision::Navi24: return "Navi24";
#endif
#if PAL_BUILD_NAVI31
        case Pal::AsicRevision::Navi31: return "Navi31";
#endif
        default: return "Unknown";
    }
}

const char* get_gfx_isa_id(const Pal::GfxIpLevel gfxip_level) {
    // We can only deal with gfxip capable devices!
    assert(gfxip_level != Pal::GfxIpLevel::_None);

    // We don't support cards prior to gfx10
    switch (gfxip_level) {
        case Pal::GfxIpLevel::GfxIp10_1: return "gfx1010";
        case Pal::GfxIpLevel::GfxIp10_3: return "gfx1030";
#if PAL_BUILD_GFX11
        case Pal::GfxIpLevel::GfxIp11_0: return "gfx1100";
#endif
        default: assert(false);
    }
}

Pal::GpuHeap find_gpu_local_heap(const Pal::IDevice* device, Pal::gpusize memory_size) {
    Pal::GpuMemoryHeapProperties heap_properties[Pal::GpuHeapCount] = {};
    Pal::Result result = device->GetGpuMemoryHeapProperties(heap_properties);
    CHECK_PAL(result, "GetGpuMemoryHeapProperties()");

    if (heap_properties[Pal::GpuHeap::GpuHeapInvisible].logicalSize >= memory_size) {
        return Pal::GpuHeap::GpuHeapInvisible;
    } else if (heap_properties[Pal::GpuHeap::GpuHeapLocal].logicalSize >= memory_size) {
        return Pal::GpuHeap::GpuHeapLocal;
    }

    assert(
        false && "Memory could not be allocated on either local or invisible heap of GPU. (Heaps to small)");
    return Pal::GpuHeap::GpuHeapCount;
}

bool allocation_is_host_visible(Pal::IGpuMemory* gpu_allocation) {
    const Pal::GpuMemoryDesc& memory_desc = gpu_allocation->Desc();
    for (Pal::uint32 i = 0; i < memory_desc.heapCount; ++i) {
        if (memory_desc.heaps[i] == Pal::GpuHeap::GpuHeapInvisible) {
            return false;
        }
    }

    return true;
}

ShaderSrc::ShaderSrc(const std::string& filename, const std::string& src_code, const std::string& kernelname)
    : kernelname(kernelname)
    , src_code(src_code)
    , filename(filename)
    , function(nullptr) {
    this->llvm_module = llvm::parseIR(llvm::MemoryBuffer::getMemBuffer(src_code)->getMemBufferRef(), diagnostic_err, llvm_context);
    if (!llvm_module)
        error("Parsing kernel % from IR file %:\n%", kernelname, filename,
            llvm_diagnostic_to_string(diagnostic_err));
    for (const llvm::Function& func : *llvm_module) {
        const auto& funcname = func.getName();
        if (funcname == kernelname) {
            this->function = &func;
        }
    }
    assert(this->function != nullptr && "Can't find kernel function");
}

// Finds the given shader entry point and renames it to match PAL's fixed naming requirements for pipeline
// functions. It further sets all other functions that the AMDGPU LLVM backend would recognize as entry points
// to a calling convention that's not recognized as an entry point.
// Returns false if no entry point could be found.
bool ShaderSrc::rename_entry_point() {
    bool entry_point_found = false;
    for (llvm::Function& func : *llvm_module) {
        const auto& funcname = func.getName();
        if (funcname == kernelname) {
            assert(!entry_point_found && "Mustn't have two entry points with the same name in one module!");
            assert(pal_utils::isAMDGPUEntryFunctionCC(func.getCallingConv()));
            func.setName(pal_utils::ComputeShaderMainFnName);
            entry_point_found = true;
        } else if (pal_utils::isAMDGPUEntryFunctionCC(func.getCallingConv())) {
            // Any other function that the AMDGPU backend would detect as an entry function must get a
            // different calling convention, so that only the actual entry point for the current kernel has
            // the according calling convention.
            func.setCallingConv(llvm::CallingConv::AMDGPU_Gfx);
            assert(!pal_utils::isAMDGPUEntryFunctionCC(func.getCallingConv()));
        }
    }
    return entry_point_found;
}

} // namespace pal_utils