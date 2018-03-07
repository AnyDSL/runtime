add_library(jit jit.cpp)
target_link_libraries(jit ${Thorin_LIBRARIES} ${Impala_LIBRARY} ${LLVM_JIT_LIBRARIES})
