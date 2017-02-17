if(EXISTS ${_irfile})
    execute_process(COMMAND ${LLVM_AS_BIN} ${_irfile})
endif()
