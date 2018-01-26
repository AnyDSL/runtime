# .cl -> .gcn
if(EXISTS ${_basename}.cl)
    execute_process(COMMAND /opt/rocm/opencl/bin/x86_64/clang -cl-std=CL2.0 -include /opt/rocm/opencl/include/opencl-c.h ${_basename}.cl -Xclang -mlink-bitcode-file -Xclang /opt/rocm/opencl/lib/x86_64/bitcode/opencl.amdgcn.bc -Xclang -mlink-bitcode-file -Xclang /opt/rocm/opencl/lib/x86_64/bitcode/ockl.amdgcn.bc -Xclang -mlink-bitcode-file -Xclang /opt/rocm/opencl/lib/x86_64/bitcode/irif.amdgcn.bc -target amdgcn-amd-amdhsa -mcpu=fiji -o ${_basename}.gcn)
endif()

# .amdgpu -> .gcn
if(EXISTS ${_basename}.amdgpu)
    string(CONCAT OCML_CONFIG "define i32 @__oclc_finite_only_opt() { ret i32 0 }\n"
                              "define i32 @__oclc_unsafe_math_opt() { ret i32 0 }\n"
                              "define i32 @__oclc_daz_opt() { ret i32 0 }\n"
                              "define i32 @__oclc_amd_opt() { ret i32 1 }\n"
                              "define i32 @__oclc_correctly_rounded_sqrt32() { ret i32 1 }\n"
                              "define i32 @__oclc_ISA_version() { ret i32 803 }")
    execute_process(COMMAND echo "${OCML_CONFIG}" OUTPUT_FILE ${_basename}.ocml.ll)
    execute_process(COMMAND /opt/rocm/hcc/bin/llvm-link ${_basename}.amdgpu ${_basename}.ocml.ll /opt/rocm/lib/ocml.amdgcn.bc /opt/rocm/lib/irif.amdgcn.bc -o ${_basename}.amdgpu.linked)
    execute_process(COMMAND /opt/rocm/hcc/bin/llc -mtriple=amdgcn-amd-amdhsa -mcpu=fiji -filetype=obj ${_basename}.amdgpu.linked -o ${_basename}.gcn.rel)
    execute_process(COMMAND /opt/rocm/hcc/bin/ld.lld -shared ${_basename}.gcn.rel -o ${_basename}.gcn)
endif()
