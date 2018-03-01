find_package(Thorin REQUIRED)
include_directories(${Thorin_INCLUDE_DIRS})
find_package(Impala REQUIRED)
include_directories(${Impala_INCLUDE_DIR})

if(LLVM_FOUND)
    add_definitions(${LLVM_DEFINITIONS})
    add_definitions("-DLLVM_SUPPORT")
    include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
endif()

add_library(jit jit.cpp)
