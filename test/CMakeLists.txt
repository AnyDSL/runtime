add_executable(jit_test main.cpp)
target_include_directories(jit_test PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(jit_test jit ${Thorin_LIBRARIES} ${Impala_LIBRARY} ${LLVM_JIT_LIBRARIES})
