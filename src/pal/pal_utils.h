#ifndef PAL_UTILS_H
#define PAL_UTILS_H

#include <pal.h>
#include <palDevice.h>

#include <llvm/BinaryFormat/MsgPackDocument.h>
#include <llvm/IR/CallingConv.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/SourceMgr.h>

namespace pal_utils {

std::string llvm_diagnostic_to_string(const llvm::SMDiagnostic& diagnostic_err);

struct ShaderSrc {
    const std::string kernelname;
    const std::string src_code;
    const std::string filename;
    const llvm::Function* function;
    llvm::LLVMContext llvm_context;
    std::unique_ptr<llvm::Module> llvm_module;
    llvm::SMDiagnostic diagnostic_err;

    ShaderSrc(const std::string& filename, const std::string& src_code, const std::string& kernelname);
    bool rename_entry_point();
};

// Create the metadata that PAL expects to be attached to a kernel/shader binary.
llvm::msgpack::Document build_metadata(const ShaderSrc& shader_src, Pal::GfxIpLevel gfx_level,
    const std::array<uint64_t, 3>& thread_group_dimensions, uint32_t wavefront_size);

const char* get_gpu_name(const Pal::AsicRevision asic_revision);

const char* get_gfx_isa_id(const Pal::GfxIpLevel gfxip_level);

bool isAMDGPUEntryFunctionCC(llvm::CallingConv::ID CC);

void write_to_memory(
    Pal::IGpuMemory* dst_memory, int64_t dst_memory_offset, const void* src_data, int64_t size);
void read_from_memory(void* dst_buffer, Pal::IGpuMemory* src_memory, int64_t src_memory_offset, int64_t size);

// Returns a gpu-local memory heap that fits memory_size.
// Order of importance: 1.GpuHeapInvisible, 2.GpuHeapLocal
// Returns Pal::GpuHeap::GpuHeapCount if no appropriate heap can be found.
Pal::GpuHeap find_gpu_local_heap(const Pal::IDevice* device, Pal::gpusize memory_size);

bool allocation_is_host_visible(Pal::IGpuMemory* gpu_allocation);

llvm::MDNode* get_metadata_mdnode(const llvm::Function* func, const char* key, int index = 0);
llvm::StringRef get_metadata_string(const llvm::Function* func, const char* key);
uint64_t get_metadata_uint(const llvm::Function* func, const char* key, int index = 0);

extern const char* ComputeShaderMainFnName;

} // namespace pal_utils

#define CHECK_PAL(err, name) { if (err != Pal::Result::Success) { error("PAL API function % [file %, line %]: %", name, __FILE__, __LINE__, static_cast<int32_t>(err)); } }

#endif
