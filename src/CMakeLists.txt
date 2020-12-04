set(CONF_RUNTIME_LIBRARIES)
set(CONF_RUNTIME_JIT_LIBRARIES)

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
    set(CUDA_PLATFORM cuda_platform.cpp cuda_platform.h)
    set_source_files_properties(${CUDA_PLATFORM} PROPERTIES INCLUDE_DIRECTORIES "${CUDA_INCLUDE_DIRS};${CUDA_TOOLKIT_ROOT_DIR}/nvvm/include")
    # TODO: would be nice to reference directly the file
    find_path(AnyDSL_runtime_LIBDEVICE_DIR
        NAMES libdevice.10.bc libdevice.compute_50.10.bc libdevice.compute_35.10.bc libdevice.compute_30.10.bc libdevice.compute_20.10.bc
        HINTS ${CUDA_TOOLKIT_ROOT_DIR}
        PATH_SUFFIXES nvvm/libdevice)
    find_program(AnyDSL_runtime_NVCC_BIN nvcc
        HINTS ${CUDA_NVCC_EXECUTABLE} ${CUDA_TOOLKIT_ROOT_DIR}
        PATH_SUFFIXES bin)
    find_path(AnyDSL_runtime_NVCC_INC NAMES cuda.h nvrtc.h
        HINTS ${CUDA_INCLUDE_DIRS} ${CUDA_TOOLKIT_INCLUDE} ${CUDA_TOOLKIT_ROOT_DIR}
        PATH_SUFFIXES include)
    list(APPEND CONF_RUNTIME_LIBRARIES ${CUDA_LIBRARY} ${CUDA_NVVM_LIBRARY} ${CUDA_NVRTC_LIBRARY})
    mark_as_advanced(AnyDSL_runtime_LIBDEVICE_DIR AnyDSL_runtime_NVCC_BIN AnyDSL_runtime_NVCC_INC)
endif()
set(AnyDSL_runtime_HAS_CUDA_SUPPORT ${CUDA_FOUND} CACHE INTERNAL "enables CUDA/NVVM support")

# look for OpenCL
find_package(OpenCL)
if(OpenCL_FOUND)
    set(OPENCL_PLATFORM opencl_platform.cpp opencl_platform.h)
    set_source_files_properties(${OPENCL_PLATFORM} PROPERTIES INCLUDE_DIRECTORIES ${OpenCL_INCLUDE_DIRS})
    list(APPEND CONF_RUNTIME_LIBRARIES ${OpenCL_LIBRARIES})
endif()
set(AnyDSL_runtime_HAS_OPENCL_SUPPORT ${OpenCL_FOUND} CACHE INTERNAL "OpenCL support enabled")

# look for HSA
find_package(HSA)
if(HSA_FOUND)
    set(HSA_PLATFORM hsa_platform.cpp hsa_platform.h)
    set_source_files_properties(${HSA_PLATFORM} PROPERTIES INCLUDE_DIRECTORIES ${HSA_INCLUDE_DIRS})
    list(APPEND CONF_RUNTIME_LIBRARIES ${HSA_LIBRARIES})
endif()
set(AnyDSL_runtime_HAS_HSA_SUPPORT ${HSA_FOUND} CACHE INTERNAL "HSA support enabled")

# look for Threads
find_package(Threads REQUIRED)
list(APPEND CONF_RUNTIME_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})

# look for TBB
find_package(TBB)
if(TBB_FOUND)
    set_source_files_properties(runtime.cpp PROPERTIES INCLUDE_DIRECTORIES ${TBB_INCLUDE_DIRS})
    list(APPEND CONF_RUNTIME_LIBRARIES ${TBB_LIBRARIES})
endif()
set(AnyDSL_runtime_HAS_TBB_SUPPORT ${TBB_FOUND} CACHE INTERNAL "enables parallel using TBB")

if(MSVC)
    add_compile_options("/experimental:external" "/external:anglebrackets" "/external:W0")
else()
    add_compile_options("-Wall" "-Wextra")
endif()

if(RUNTIME_JIT)
    set(RUNTIME_SOURCES_ARTIC_INC_FILE ${CMAKE_CURRENT_BINARY_DIR}/runtime_srcs_artic.cpp)
    set(RUNTIME_JIT_SRC jit.cpp)
    find_package(Artic REQUIRED)
    find_package(Thorin REQUIRED)
    if(NOT Thorin_HAS_LLVM_SUPPORT)
        message(FATAL_ERROR "RUNTIME_JIT requires Thorin to be built with LLVM")
    endif()

    find_package(LLVM REQUIRED)
    add_definitions(${LLVM_DEFINITIONS})
    list(APPEND CONF_RUNTIME_LIBRARIES ${Thorin_LIBRARIES} ${Artic_LIBRARY})
    include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
    if(AnyDSL_runtime_HAS_HSA_SUPPORT)
        find_library(LLD_COMMON_LIBRARY lldCommon HINTS ${LLVM_LIBRARY_DIRS})
        find_library(LLD_CORE_LIBRARY   lldCore   HINTS ${LLVM_LIBRARY_DIRS})
        find_library(LLD_DRIVER_LIBRARY lldDriver HINTS ${LLVM_LIBRARY_DIRS})
        find_library(LLD_ELF_LIBRARY    lldELF    HINTS ${LLVM_LIBRARY_DIRS})
        list(APPEND CONF_RUNTIME_LIBRARIES ${LLD_CORE_LIBRARY} ${LLD_ELF_LIBRARY} ${LLD_DRIVER_LIBRARY} ${LLD_COMMON_LIBRARY})
    endif()

    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(RUNTIME_ARTIC_SRCS
        ../platforms/artic/intrinsics_rv.impala
        ../platforms/artic/intrinsics_cpu.impala
        ../platforms/artic/intrinsics_hls.impala
        ../platforms/artic/intrinsics_cuda.impala
        ../platforms/artic/intrinsics_nvvm.impala
        ../platforms/artic/intrinsics_amdgpu.impala
        ../platforms/artic/intrinsics_opencl.impala
        ../platforms/artic/intrinsics_thorin.impala
        ../platforms/artic/intrinsics.impala
        ../platforms/artic/runtime.impala)
    add_custom_command(OUTPUT ${RUNTIME_SOURCES_ARTIC_INC_FILE}
        COMMAND ${Python3_EXECUTABLE} extract_runtime_srcs.py ${RUNTIME_ARTIC_SRCS} > ${RUNTIME_SOURCES_ARTIC_INC_FILE}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        DEPENDS extract_runtime_srcs.py ${RUNTIME_ARTIC_SRCS})
    set_source_files_properties(${RUNTIME_SOURCES_ARTIC_INC_FILE} PROPERTIES GENERATED TRUE)
    set_source_files_properties(${RUNTIME_JIT_SRC} PROPERTIES INCLUDE_DIRECTORIES ${Thorin_INCLUDE_DIRS})
    add_library(runtime_srcs_artic ${AnyDSL_runtime_BUILD} ${RUNTIME_SOURCES_ARTIC_INC_FILE})
    set_target_properties(runtime_srcs_artic PROPERTIES CXX_VISIBILITY_PRESET default)
    list(APPEND CONF_RUNTIME_JIT_LIBRARIES runtime_srcs_artic)
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
            cpu_platform.cpp
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
set_target_properties(runtime PROPERTIES CXX_VISIBILITY_PRESET hidden)

if(RUNTIME_JIT AND Thorin_HAS_LLVM_SUPPORT AND LLVM_FOUND)
    set(AnyDSL_runtime_LLVM_COMPONENTS mcjit irreader support ${LLVM_TARGETS_TO_BUILD})
    llvm_config(runtime ${AnyDSL_LLVM_LINK_SHARED} ${AnyDSL_runtime_LLVM_COMPONENTS})
endif()

set(CONF_RUNTIME_LIBRARIES ${CONF_RUNTIME_LIBRARIES} PARENT_SCOPE)
set(CONF_RUNTIME_JIT_LIBRARIES ${CONF_RUNTIME_JIT_LIBRARIES} PARENT_SCOPE)
