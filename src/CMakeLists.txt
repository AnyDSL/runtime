set(RUNTIME_DEFINITIONS)
set(CONF_RUNTIME_LIBRARIES)

# Look for CUDA
find_package(CUDA QUIET)
if(CUDA_FOUND)
    find_library(CUDA_NVVM_LIBRARY nvvm
        HINTS ${CUDA_TOOLKIT_ROOT_DIR}/nvvm
        PATH_SUFFIXES lib lib64 lib/x64)
    find_library(CUDA_NVRTC_LIBRARY nvrtc
        HINTS ${CUDA_TOOLKIT_ROOT_DIR}
        PATH_SUFFIXES lib lib64 lib/x64)
    if(CUDA_NVRTC_LIBRARY)
        set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} -DCUDA_NVRTC)
    else()
        set(CUDA_NVRTC_LIBRARY "")
    endif()
    find_library(CUDA_LIBRARY cuda
        HINTS ${CUDA_TOOLKIT_ROOT_DIR}
        PATH_SUFFIXES lib lib64 lib/x64)
    include_directories(${CUDA_INCLUDE_DIRS} "${CUDA_TOOLKIT_ROOT_DIR}/nvvm/include")
    set(CUDA_PLATFORM cuda_platform.cpp cuda_platform.h)
    set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} -DRUNTIME_ENABLE_CUDA)
    set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} "-DLIBDEVICE_DIR=\"${CUDA_TOOLKIT_ROOT_DIR}/nvvm/libdevice/\"")
    set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} "-DNVCC_BIN=\"${CUDA_TOOLKIT_ROOT_DIR}/bin/nvcc\"")
    set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} "-DNVCC_INC=\"${CUDA_INCLUDE_DIRS}\"")
    list(APPEND CONF_RUNTIME_LIBRARIES ${CUDA_LIBRARY};${CUDA_NVVM_LIBRARY};${CUDA_NVRTC_LIBRARY})
else()
    set(CUDA_PLATFORM "")
endif()

# look for OpenCL
find_package(OpenCL)
if(OpenCL_FOUND)
    include_directories(${OpenCL_INCLUDE_DIRS})
    set(OPENCL_PLATFORM opencl_platform.cpp opencl_platform.h)
    set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} -DRUNTIME_ENABLE_OPENCL)
    list(APPEND CONF_RUNTIME_LIBRARIES ${OpenCL_LIBRARIES})
else()
    set(OPENCL_PLATFORM "")
endif()

# look for HSA
find_package(HSA)
if(HSA_FOUND)
    include_directories(${HSA_INCLUDE_DIRS})
    set(HSA_PLATFORM hsa_platform.cpp hsa_platform.h)
    set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} -DRUNTIME_ENABLE_HSA)
    list(APPEND CONF_RUNTIME_LIBRARIES ${HSA_LIBRARIES})
else()
    set(HSA_PLATFORM "")
endif()

# look for Threads
find_package(Threads REQUIRED)
list(APPEND CONF_RUNTIME_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})

# look for TBB
find_package(TBB)
if(TBB_FOUND)
    include_directories(${TBB_INCLUDE_DIRS})
    set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} -DRUNTIME_ENABLE_TBB)
    list(APPEND CONF_RUNTIME_LIBRARIES ${TBB_LIBRARIES})
endif()

if(NOT MSVC)
    add_definitions("-Wall -Wextra")
endif()

if(RUNTIME_JIT)
    set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} -DRUNTIME_ENABLE_JIT -DANYDSL_RUNTIME_DIR=\"${PROJECT_SOURCE_DIR}\")
    set(RUNTIME_SOURCES_INC_FILE ${CMAKE_CURRENT_BINARY_DIR}/runtime_srcs.inc)
    set(RUNTIME_JIT_SRC jit.cpp ${RUNTIME_SOURCES_INC_FILE})
    find_package(Impala REQUIRED)
    find_package(Thorin REQUIRED)
    include_directories(SYSTEM ${Impala_INCLUDE_DIR} ${Thorin_INCLUDE_DIRS})
    include_directories(${CMAKE_CURRENT_BINARY_DIR})

    find_package(LLVM REQUIRED)
    add_definitions(${LLVM_DEFINITIONS})
    add_definitions("-DLLVM_SUPPORT")
    include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
    llvm_map_components_to_libnames(LLVM_JIT_LIBRARIES core executionengine object mcjit runtimedyld support target native)
    list(APPEND CONF_RUNTIME_LIBRARIES ${Thorin_LIBRARIES};${Impala_LIBRARY};${LLVM_JIT_LIBRARIES})

    find_package(PythonInterp REQUIRED)
    set(RUNTIME_IMPALA_SRCS
        ../platforms/intrinsics_amdgpu.impala
        ../platforms/intrinsics_cpu.impala
        ../platforms/intrinsics_cuda.impala
        ../platforms/intrinsics_nvptx.impala
        ../platforms/intrinsics_opencl.impala
        ../platforms/intrinsics_thorin.impala
        ../platforms/intrinsics.impala
        runtime.impala)
    add_custom_command(OUTPUT ${RUNTIME_SOURCES_INC_FILE}
        COMMAND ${PYTHON_EXECUTABLE} extract_runtime_srcs.py ${RUNTIME_IMPALA_SRCS} > ${RUNTIME_SOURCES_INC_FILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS extract_runtime_srcs.py ${RUNTIME_IMPALA_SRCS})
    set_source_files_properties(${RUNTIME_SOURCES_INC_FILE} PROPERTIES GENERATED TRUE)
endif()

add_definitions(${RUNTIME_DEFINITIONS})

add_library(runtime
            runtime.cpp
            runtime.h
            anydsl_runtime.h
            anydsl_runtime.hpp
            platform.h
            cpu_platform.h
            dummy_platform.h
            log.h
            ${RUNTIME_JIT_SRC}
            ${CUDA_PLATFORM}
            ${OPENCL_PLATFORM}
            ${HSA_PLATFORM})

target_link_libraries(runtime ${CONF_RUNTIME_LIBRARIES})
