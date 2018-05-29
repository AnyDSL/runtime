if(EXISTS ${_basename}.nvvm)
    execute_process(COMMAND ${LLVM_AS_BIN} ${_basename}.nvvm)
endif()
