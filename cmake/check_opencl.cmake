if(EXISTS ${_basename}.cl)
    execute_process(COMMAND /opt/rocm/opencl/bin/x86_64/clang -cl-std=CL2.0 -include /opt/rocm/opencl/include/opencl-c.h -Xclang -mlink-bitcode-file -Xclang /opt/rocm/opencl/lib/x86_64/bitcode/opencl.amdgcn.bc -Xclang -mlink-bitcode-file -Xclang /opt/rocm/opencl/lib/x86_64/bitcode/ockl.amdgcn.bc -target amdgcn-amd-amdhsa -mcpu=fiji ${_basename}.cl -o ${_basename}.gcn)
endif()
