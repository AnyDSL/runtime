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
        set(AnyDSL_runtime_CUDA_NVRTC TRUE)
    else()
        set(CUDA_NVRTC_LIBRARY "")
    endif()
    find_library(CUDA_LIBRARY cuda
        HINTS ${CUDA_TOOLKIT_ROOT_DIR}
        PATH_SUFFIXES lib lib64 lib/x64)
    include_directories(${CUDA_INCLUDE_DIRS} ${CUDA_TOOLKIT_ROOT_DIR}/nvvm/include)
    set(CUDA_PLATFORM cuda_platform.cpp cuda_platform.h)
    set(AnyDSL_runtime_LIBDEVICE_DIR ${CUDA_TOOLKIT_ROOT_DIR}/nvvm/libdevice/)
    set(AnyDSL_runtime_NVCC_BIN      ${CUDA_TOOLKIT_ROOT_DIR}/bin/nvcc)
    set(AnyDSL_runtime_NVCC_INC      ${CUDA_INCLUDE_DIRS})
    list(APPEND CONF_RUNTIME_LIBRARIES ${CUDA_LIBRARY} ${CUDA_NVVM_LIBRARY} ${CUDA_NVRTC_LIBRARY})
endif()
set(AnyDSL_runtime_HAS_CUDA_SUPPORT ${CUDA_FOUND} CACHE INTERNAL "enables CUDA/NVVM support")

# look for OpenCL
find_package(OpenCL)
if(OpenCL_FOUND)
    include_directories(BEFORE ${OpenCL_INCLUDE_DIRS}) # use header from specified path instead of CUDA's
    set(OPENCL_PLATFORM opencl_platform.cpp opencl_platform.h)
    list(APPEND CONF_RUNTIME_LIBRARIES ${OpenCL_LIBRARIES})
endif()
set(AnyDSL_runtime_HAS_OPENCL_SUPPORT ${OpenCL_FOUND} CACHE INTERNAL "OpenCL support enabled")

# look for HSA
find_package(HSA)
if(HSA_FOUND)
    include_directories(${HSA_INCLUDE_DIRS})
    set(HSA_PLATFORM hsa_platform.cpp hsa_platform.h)
    list(APPEND CONF_RUNTIME_LIBRARIES ${HSA_LIBRARIES})
endif()
set(AnyDSL_runtime_HAS_HSA_SUPPORT ${HSA_FOUND} CACHE INTERNAL "HSA support enabled")

# look for Threads
find_package(Threads REQUIRED)
list(APPEND CONF_RUNTIME_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})

# look for TBB
find_package(TBB)
if(TBB_FOUND)
    include_directories(${TBB_INCLUDE_DIRS})
    list(APPEND CONF_RUNTIME_LIBRARIES ${TBB_LIBRARIES})
endif()
set(AnyDSL_runtime_HAS_TBB_SUPPORT ${TBB_FOUND} CACHE INTERNAL "enables parallel using TBB")

if(MSVC)
    add_compile_options("/experimental:external" "/external:anglebrackets" "/external:W0")
else()
    add_compile_options("-Wall -Wextra")
endif()

if(RUNTIME_JIT)
    set(RUNTIME_SOURCES_INC_FILE ${CMAKE_CURRENT_BINARY_DIR}/runtime_srcs.inc)
    set(RUNTIME_JIT_SRC jit.cpp ${RUNTIME_SOURCES_INC_FILE})
    find_package(Impala REQUIRED)
    find_package(Thorin REQUIRED)
    if(NOT Thorin_HAS_LLVM_SUPPORT)
        message(FATAL_ERROR "RUNTIME_JIT requires Thorin to be built with LLVM")
    endif()
    include_directories(SYSTEM ${Impala_INCLUDE_DIR} ${Thorin_INCLUDE_DIRS})
    include_directories(${CMAKE_CURRENT_BINARY_DIR})

    find_package(LLVM REQUIRED)
    add_definitions(${LLVM_DEFINITIONS})
    include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
    #llvm_map_components_to_libnames(LLVM_JIT_LIBRARIES all)
    list(APPEND CONF_RUNTIME_LIBRARIES ${Thorin_LIBRARIES} ${Impala_LIBRARY})

    find_package(PythonInterp REQUIRED)
    set(RUNTIME_IMPALA_SRCS
        ../platforms/intrinsics_rv.impala
        ../platforms/intrinsics_cpu.impala
        ../platforms/intrinsics_hls.impala
        ../platforms/intrinsics_cuda.impala
        ../platforms/intrinsics_nvvm.impala
        ../platforms/intrinsics_amdgpu.impala
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
set(AnyDSL_runtime_HAS_JIT_SUPPORT ${RUNTIME_JIT} CACHE INTERNAL "enables anydsl_compile() api")

option(AnyDSL_runtime_BUILD_SHARED "whether to build AnyDSL_runtime as shared or static library" ${BUILD_SHARED_LIBS})
if(AnyDSL_runtime_BUILD_SHARED)
    set(AnyDSL_runtime_BUILD "SHARED")
else()
    set(AnyDSL_runtime_BUILD "STATIC")
endif()

include_directories(${CMAKE_BINARY_DIR}/include)
set(AnyDSL_runtime_CONFIG_FILE ${CMAKE_BINARY_DIR}/include/anydsl_runtime_config.h)
configure_file(anydsl_runtime_config.h.in ${AnyDSL_runtime_CONFIG_FILE} @ONLY)
set_source_files_properties(${AnyDSL_runtime_CONFIG_FILE} PROPERTIES GENERATED TRUE)

add_library(runtime ${AnyDSL_runtime_BUILD}
            runtime.cpp
            runtime.h
            anydsl_runtime.h
            anydsl_runtime.hpp
            platform.h
            cpu_platform.h
            dummy_platform.h
            log.h
            ${AnyDSL_runtime_CONFIG_FILE}
            ${RUNTIME_JIT_SRC}
            ${CUDA_PLATFORM}
            ${OPENCL_PLATFORM}
            ${HSA_PLATFORM})

target_link_libraries(runtime PRIVATE ${CONF_RUNTIME_LIBRARIES})
set_target_properties(runtime PROPERTIES DEFINE_SYMBOL "AnyDSL_runtime_EXPORTS")

if(RUNTIME_JIT AND Thorin_HAS_LLVM_SUPPORT AND LLVM_FOUND)
    set(AnyDSL_runtime_LLVM_COMPONENTS mcjit irreader support ${LLVM_TARGETS_TO_BUILD})
    if(AnyDSL_LLVM_LINK_SHARED)
        set(_specify_use_shared "USE_SHARED")
    else()
        unset(_specify_use_shared)
    endif()
    llvm_config(runtime ${_specify_use_shared} ${AnyDSL_runtime_LLVM_COMPONENTS})
endif()

