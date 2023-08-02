set(RUNTIME_PLATFORMS)
set(RUNTIME_JIT_LIBRARIES)

if(MSVC)
    add_compile_options("/experimental:external" "/external:anglebrackets" "/external:W0")
else()
    add_compile_options("-Wall" "-Wextra")
endif()

# Necessary to specify whether or not to build shared libraries on a per-project basis
option(AnyDSL_runtime_BUILD_SHARED "Builds a shared library for the runtime (and JIT, if enabled)" ${BUILD_SHARED_LIBS})
if(AnyDSL_runtime_BUILD_SHARED)
    set(AnyDSL_runtime_BUILD "SHARED")
else()
    set(AnyDSL_runtime_BUILD "STATIC")
endif()

set(AnyDSL_runtime_CONFIG_FILE ${CMAKE_BINARY_DIR}/include/anydsl_runtime_config.h)
add_library(runtime_base STATIC
    ${AnyDSL_runtime_CONFIG_FILE}
    runtime.cpp
    runtime.h
    platform.h
    cpu_platform.cpp
    cpu_platform.h
    dummy_platform.h
    log.h)

# look for CUDA
if(${CMAKE_VERSION} VERSION_LESS "3.18.0")
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
        add_library(runtime_cuda STATIC cuda_platform.cpp cuda_platform.h)
        target_include_directories(runtime_cuda PRIVATE ${CUDA_INCLUDE_DIRS} "${CUDA_TOOLKIT_ROOT_DIR}/nvvm/include")
        target_link_libraries(runtime_cuda PRIVATE runtime_base ${CUDA_LIBRARY} ${CUDA_NVVM_LIBRARY} ${CUDA_NVRTC_LIBRARY})
        list(APPEND RUNTIME_PLATFORMS runtime_cuda)
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
        mark_as_advanced(AnyDSL_runtime_LIBDEVICE_DIR AnyDSL_runtime_NVCC_BIN AnyDSL_runtime_NVCC_INC)
    endif()
    set(AnyDSL_runtime_HAS_CUDA_SUPPORT ${CUDA_FOUND} CACHE INTERNAL "enables CUDA/NVVM support")
else()
    find_package(CUDAToolkit QUIET)
    if(CUDAToolkit_FOUND)
        # this is more robust in case CUDAToolkit_LIBRARY_ROOT is not defined
        find_path(CUDAToolkit_LIBRARY_ROOT
            NAMES version.txt version.json bin/nvcc
            HINTS ${CUDAToolkit_LIBRARY_ROOT} ${CUDAToolkit_ROOT} ${CUDAToolkit_BIN_DIR}/../)
        find_library(CUDAToolkit_NVVM_LIBRARY nvvm
            HINTS ${CUDAToolkit_LIBRARY_ROOT}/nvvm
            PATH_SUFFIXES lib lib64 lib/x64)
        add_library(runtime_cuda STATIC cuda_platform.cpp cuda_platform.h)
        target_include_directories(runtime_cuda PRIVATE "${CUDAToolkit_LIBRARY_ROOT}/nvvm/include")
        target_link_libraries(runtime_cuda PRIVATE runtime_base CUDA::cuda_driver CUDA::nvrtc ${CUDAToolkit_NVVM_LIBRARY})
        list(APPEND RUNTIME_PLATFORMS runtime_cuda)
        # TODO: would be nice to reference directly the file
        find_path(AnyDSL_runtime_LIBDEVICE_DIR
            NAMES libdevice.10.bc
            HINTS ${CUDAToolkit_LIBRARY_ROOT} ${CUDAToolkit_LIBRARY_DIR}
            PATH_SUFFIXES cuda/nvvm/libdevice nvvm/libdevice)
        # using find_*() in favor of set() here
        # allows to fix issues that may happen on some system configuration
        find_program(AnyDSL_runtime_NVCC_BIN nvcc
            HINTS ${CUDAToolkit_NVCC_EXECUTABLE} ${CUDAToolkit_BIN_DIR}
            PATH_SUFFIXES bin)
        find_path(AnyDSL_runtime_NVCC_INC NAMES cuda.h nvrtc.h
            HINTS ${CUDAToolkit_INCLUDE_DIRS} ${CUDAToolkit_LIBRARY_ROOT}
            PATH_SUFFIXES include)
        set(AnyDSL_runtime_CUDA_NVRTC TRUE)
        mark_as_advanced(AnyDSL_runtime_LIBDEVICE_DIR AnyDSL_runtime_NVCC_BIN AnyDSL_runtime_NVCC_INC)
    endif()
    set(AnyDSL_runtime_HAS_CUDA_SUPPORT ${CUDAToolkit_FOUND} CACHE INTERNAL "enables CUDA/NVVM support")
endif()

if(AnyDSL_runtime_HAS_CUDA_SUPPORT)
    set(AnyDSL_runtime_CUDA_CXX_STANDARD 11 CACHE STRING "C++ version to use for CUDA code")
endif()

# look for OpenCL
find_package(OpenCL)
if(OpenCL_FOUND)
    add_library(runtime_opencl STATIC opencl_platform.cpp opencl_platform.h)
    target_link_libraries(runtime_opencl PRIVATE runtime_base OpenCL::OpenCL)
    list(APPEND RUNTIME_PLATFORMS runtime_opencl)

    # look for Xilinx-HLS
    find_package(XHLS)
    #if(XHLS_FOUND)
    #    target_include_directories(runtime_opencl PRIVATE ${Xilinx_INCLUDE_DIRS})
    #endif()
endif()
set(AnyDSL_runtime_HAS_OPENCL_SUPPORT ${OpenCL_FOUND} CACHE INTERNAL "enables OpenCL support")

# look for HSA
find_package(hsa-runtime64 PATHS /opt/rocm)
if(hsa-runtime64_FOUND)
    add_library(runtime_hsa STATIC hsa_platform.cpp hsa_platform.h)
    target_link_libraries(runtime_hsa PRIVATE runtime_base hsa-runtime64::hsa-runtime64)
    list(APPEND RUNTIME_PLATFORMS runtime_hsa)

    find_package(AMDDeviceLibs PATHS /opt/rocm)
    get_target_property(ocml_LOCATION ocml LOCATION)
    get_filename_component(AnyDSL_runtime_HSA_BITCODE_PATH ${ocml_LOCATION} DIRECTORY)
    get_filename_component(AnyDSL_runtime_HSA_BITCODE_SUFFIX ${ocml_LOCATION} EXT)
endif()
set(AnyDSL_runtime_HAS_HSA_SUPPORT ${hsa-runtime64_FOUND} CACHE INTERNAL "enables HSA support")

# look for LLVM for nvptx and gcn
find_package(LLVM)
if(LLVM_FOUND)
    if(NOT DEFINED AnyDSL_LLVM_LINK_SHARED)
        if(LLVM_LINK_LLVM_DYLIB)
            set(AnyDSL_LLVM_LINK_SHARED "USE_SHARED")
        endif()
    endif()
    add_definitions(${LLVM_DEFINITIONS})
    include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
    set(AnyDSL_runtime_LLVM_COMPONENTS irreader support ${LLVM_TARGETS_TO_BUILD})
    set(AnyDSL_runtime_JIT_LLVM_COMPONENTS ${AnyDSL_runtime_LLVM_COMPONENTS} mcjit)
    if(AnyDSL_runtime_HAS_HSA_SUPPORT AND RUNTIME_JIT)
        find_package(LLD REQUIRED)
        target_link_libraries(runtime_hsa PRIVATE lldELF lldCommon)
        llvm_config(runtime_hsa ${AnyDSL_LLVM_LINK_SHARED} lto option ${LLVM_TARGETS_TO_BUILD})
    endif()
endif()
set(AnyDSL_runtime_HAS_LLVM_SUPPORT ${LLVM_FOUND} CACHE INTERNAL "enables nvptx / gcn support")

if(RUNTIME_JIT)
    function(add_runtime_jit frontend)
        find_package(Python3 COMPONENTS Interpreter REQUIRED)
        set(RUNTIME_FRONTEND_SRCS
            ../platforms/${frontend}/intrinsics_rv.impala
            ../platforms/${frontend}/intrinsics_cpu.impala
            ../platforms/${frontend}/intrinsics_hls.impala
            ../platforms/${frontend}/intrinsics_cuda.impala
            ../platforms/${frontend}/intrinsics_nvvm.impala
            ../platforms/${frontend}/intrinsics_amdgpu.impala
            ../platforms/${frontend}/intrinsics_opencl.impala
            ../platforms/${frontend}/intrinsics_thorin.impala
            ../platforms/${frontend}/intrinsics.impala
            ../platforms/${frontend}/runtime.impala)

        # Add artic-only math intrinsics
        if("${frontend}" STREQUAL "artic")
            set(RUNTIME_FRONTEND_SRCS
                ${RUNTIME_FRONTEND_SRCS}
                ../platforms/${frontend}/intrinsics_math.impala)
        endif()

        set(RUNTIME_JIT_SRC jit.cpp)
        set(RUNTIME_SOURCES_FRONTEND_INC_FILE ${CMAKE_CURRENT_BINARY_DIR}/${frontend}/runtime_srcs.inc)
        add_custom_command(OUTPUT ${RUNTIME_SOURCES_FRONTEND_INC_FILE}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/${frontend}
            COMMAND ${Python3_EXECUTABLE} extract_runtime_srcs.py ${RUNTIME_FRONTEND_SRCS} > ${RUNTIME_SOURCES_FRONTEND_INC_FILE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            DEPENDS extract_runtime_srcs.py ${RUNTIME_FRONTEND_SRCS})
        add_library(runtime_jit_${frontend} ${AnyDSL_runtime_BUILD} ${RUNTIME_JIT_SRC} ${RUNTIME_SOURCES_FRONTEND_INC_FILE})
        target_include_directories(runtime_jit_${frontend} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${frontend} ${Thorin_INCLUDE_DIRS})
        set_target_properties(runtime_jit_${frontend} PROPERTIES CXX_VISIBILITY_PRESET hidden)
        set_source_files_properties(${RUNTIME_SOURCES_FRONTEND_INC_FILE} PROPERTIES GENERATED TRUE)
    endfunction()

    find_package(Artic QUIET)
    find_package(Impala QUIET)
    if(NOT (Artic_FOUND OR Impala_FOUND))
        message(FATAL_ERROR "Enabling RUNTIME_JIT requires artic or impala.\nPlease specify either Artic_DIR or Impala_DIR or both.")
    endif()

    find_package(Thorin REQUIRED)
    if(NOT Thorin_HAS_LLVM_SUPPORT)
        message(FATAL_ERROR "Enabling RUNTIME_JIT requires Thorin to be built with LLVM")
    endif()

    if(NOT LLVM_FOUND)
        message(FATAL_ERROR "Enabling RUNTIME_JIT requires runtime to be built with LLVM")
    endif()

    if(Artic_FOUND)
        message(STATUS "Found Artic: ${Artic_DIR}")
        add_runtime_jit(artic)
        list(APPEND RUNTIME_JIT_LIBRARIES runtime_jit_artic)
        target_link_libraries(runtime_jit_artic PRIVATE ${Thorin_LIBRARIES} ${Artic_LIBRARY})
        llvm_config(runtime_jit_artic ${AnyDSL_LLVM_LINK_SHARED} ${AnyDSL_runtime_JIT_LLVM_COMPONENTS})
    endif()
    if(Impala_FOUND)
        message(STATUS "Found Impala: ${Impala_DIR}")
        add_runtime_jit(impala)
        list(APPEND RUNTIME_JIT_LIBRARIES runtime_jit_impala)
        target_link_libraries(runtime_jit_impala PRIVATE ${Thorin_LIBRARIES} ${Impala_LIBRARY})
        llvm_config(runtime_jit_impala ${AnyDSL_LLVM_LINK_SHARED} ${AnyDSL_runtime_JIT_LLVM_COMPONENTS})
    endif()
endif()
set(AnyDSL_runtime_HAS_JIT_SUPPORT ${RUNTIME_JIT_LIBRARIES} CACHE INTERNAL "enables anydsl_compile() API")

include_directories(${CMAKE_BINARY_DIR}/include)
configure_file(anydsl_runtime_config.h.in ${AnyDSL_runtime_CONFIG_FILE} @ONLY)
set_source_files_properties(${AnyDSL_runtime_CONFIG_FILE} PROPERTIES GENERATED TRUE)

add_library(runtime
    ${AnyDSL_runtime_BUILD}
    ${AnyDSL_runtime_CONFIG_FILE}
    anydsl_runtime.cpp
    anydsl_runtime.h
    anydsl_runtime.hpp)

# System threads are required to use either TBB or C++11 threads
find_package(Threads REQUIRED)
target_link_libraries(runtime PRIVATE Threads::Threads)

# TBB is optional, C++11 threads are used when it is not available
find_package(TBB QUIET)
if(TBB_FOUND)
    message(STATUS "Found TBB in ${TBB_DIR}")
    target_link_libraries(runtime PRIVATE TBB::tbb)
endif()
set(AnyDSL_runtime_HAS_TBB_SUPPORT ${TBB_FOUND} CACHE INTERNAL "enables parallel using TBB")

# If the runtime is built as a shared library, individual
# dependencies must be built with position independent code.
if(AnyDSL_runtime_BUILD_SHARED)
    set_property(TARGET runtime_base ${RUNTIME_PLATFORMS} PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

target_link_libraries(runtime PRIVATE runtime_base ${RUNTIME_PLATFORMS})
set_target_properties(runtime PROPERTIES DEFINE_SYMBOL "AnyDSL_runtime_EXPORTS")
set_target_properties(runtime PROPERTIES CXX_VISIBILITY_PRESET hidden)

foreach(jit_lib ${AnyDSL_runtime_HAS_JIT_SUPPORT})
    target_link_libraries(${jit_lib} PRIVATE runtime runtime_base)
    set_target_properties(${jit_lib} PROPERTIES DEFINE_SYMBOL "AnyDSL_runtime_jit_EXPORTS")
endforeach()

if(LLVM_FOUND)
    llvm_config(runtime ${AnyDSL_LLVM_LINK_SHARED} ${AnyDSL_runtime_LLVM_COMPONENTS})
endif()

set(RUNTIME_LIBRARIES runtime ${RUNTIME_JIT_LIBRARIES} PARENT_SCOPE)
