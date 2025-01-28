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
add_library(${AnyDSL_runtime_TARGET_NAME}_base STATIC
    ${AnyDSL_runtime_CONFIG_FILE}
    runtime.cpp
    runtime.h
    platform.h
    cpu_platform.cpp
    cpu_platform.h
    dummy_platform.h
    log.h)

# look for CUDA
find_package(CUDAToolkit QUIET)
if(CUDAToolkit_FOUND)
    # this is more robust in case CUDAToolkit_LIBRARY_ROOT is not defined
    find_path(CUDAToolkit_LIBRARY_ROOT
        NAMES version.txt version.json bin/nvcc
        HINTS ${CUDAToolkit_LIBRARY_ROOT} ${CUDAToolkit_ROOT} ${CUDAToolkit_BIN_DIR}/../)
    find_library(CUDAToolkit_NVVM_LIBRARY nvvm
        HINTS ${CUDAToolkit_LIBRARY_ROOT}/nvvm
        PATH_SUFFIXES lib lib64 lib/x64)
    add_library(${AnyDSL_runtime_TARGET_NAME}_cuda STATIC cuda_platform.cpp cuda_platform.h)
    target_include_directories(${AnyDSL_runtime_TARGET_NAME}_cuda PRIVATE "${CUDAToolkit_LIBRARY_ROOT}/nvvm/include")

    target_link_libraries(${AnyDSL_runtime_TARGET_NAME}_cuda PRIVATE ${AnyDSL_runtime_TARGET_NAME}_base CUDA::cuda_driver CUDA::nvrtc ${CUDAToolkit_NVVM_LIBRARY})

    if ((${CUDAToolkit_VERSION_MAJOR} GREATER_EQUAL 12) OR ((${CUDAToolkit_VERSION_MAJOR} EQUAL 11) AND (${CUDAToolkit_VERSION_MINOR} GREATER_EQUAL 1)))
        if (TARGET CUDA::nvptxcompiler_static)
            target_link_libraries(${AnyDSL_runtime_TARGET_NAME}_cuda PRIVATE CUDA::nvptxcompiler_static)
        else()
            find_library(CUDAToolkit_NVPTXCOMPILER_LIBRARY nvptxcompiler_static
                HINTS ${CUDAToolkit_LIBRARY_ROOT}
                PATH_SUFFIXES lib lib64 lib/x64)
            target_link_libraries(${AnyDSL_runtime_TARGET_NAME}_cuda PRIVATE ${CUDAToolkit_NVPTXCOMPILER_LIBRARY})
        endif()
    endif()

    list(APPEND RUNTIME_PLATFORMS ${AnyDSL_runtime_TARGET_NAME}_cuda)
    # TODO: would be nice to reference directly the file
    find_file(AnyDSL_runtime_LIBDEVICE_LIB
        NAMES libdevice.10.bc
        HINTS ${CUDAToolkit_LIBRARY_ROOT} ${CUDAToolkit_LIBRARY_DIR}
        PATH_SUFFIXES cuda/nvvm/libdevice nvvm/libdevice
        REQUIRED)
    set(AnyDSL_runtime_NVCC_INC ${CUDAToolkit_INCLUDE_DIRS})
    set(AnyDSL_runtime_CUDA_CXX_STANDARD 11 CACHE STRING "C++ version to use for CUDA code")
    mark_as_advanced(AnyDSL_runtime_LIBDEVICE_LIB AnyDSL_runtime_NVCC_INC)
endif()
set(AnyDSL_runtime_HAS_CUDA_SUPPORT ${CUDAToolkit_FOUND} CACHE INTERNAL "enables CUDA/NVVM support")

# look for OpenCL
find_package(OpenCL)
if(OpenCL_FOUND)
    add_library(${AnyDSL_runtime_TARGET_NAME}_opencl STATIC opencl_platform.cpp opencl_platform.h)
    target_link_libraries(${AnyDSL_runtime_TARGET_NAME}_opencl PRIVATE ${AnyDSL_runtime_TARGET_NAME}_base OpenCL::OpenCL)
    list(APPEND RUNTIME_PLATFORMS ${AnyDSL_runtime_TARGET_NAME}_opencl)

    # look for Xilinx-HLS
    find_package(XHLS)
    #if(XHLS_FOUND)
    #    target_include_directories(${AnyDSL_runtime_TARGET_NAME}_opencl PRIVATE ${Xilinx_INCLUDE_DIRS})
    #endif()
endif()
set(AnyDSL_runtime_HAS_OPENCL_SUPPORT ${OpenCL_FOUND} CACHE INTERNAL "enables OpenCL support")

# look for oneAPI Level Zero
find_package(LevelZero)
if(LevelZero_FOUND)
    add_library(runtime_levelzero STATIC levelzero_platform.cpp levelzero_platform.h)
    target_link_libraries(runtime_levelzero PRIVATE runtime_base LevelZero::LevelZero)
    list(APPEND RUNTIME_PLATFORMS runtime_levelzero)
endif()
set(AnyDSL_runtime_HAS_LEVELZERO_SUPPORT ${LevelZero_FOUND} CACHE INTERNAL "enables Level Zero support")

# look for HSA
find_package(hsa-runtime64 PATHS /opt/rocm)
if(hsa-runtime64_FOUND)
    add_library(${AnyDSL_runtime_TARGET_NAME}_hsa STATIC hsa_platform.cpp hsa_platform.h)
    target_link_libraries(${AnyDSL_runtime_TARGET_NAME}_hsa PRIVATE ${AnyDSL_runtime_TARGET_NAME}_base hsa-runtime64::hsa-runtime64)
    list(APPEND RUNTIME_PLATFORMS ${AnyDSL_runtime_TARGET_NAME}_hsa)

    find_package(AMDDeviceLibs PATHS /opt/rocm)
    get_target_property(ocml_LOCATION ocml LOCATION)
    get_filename_component(AnyDSL_runtime_HSA_BITCODE_PATH ${ocml_LOCATION} DIRECTORY)
    get_filename_component(AnyDSL_runtime_HSA_BITCODE_SUFFIX ${ocml_LOCATION} EXT)
endif()
set(AnyDSL_runtime_HAS_HSA_SUPPORT ${hsa-runtime64_FOUND} CACHE INTERNAL "enables HSA support")

# look for PAL
find_package(pal CONFIG)
if(pal_FOUND)
    add_library(${AnyDSL_runtime_TARGET_NAME}_pal STATIC
        pal_platform.h
        pal_platform.cpp
        pal/pal_lower_kernel_arguments_pass.h
        pal/pal_lower_kernel_arguments_pass.cpp
        pal/pal_fix_calling_convention_pass.h
        pal/pal_fix_calling_convention_pass.cpp
        pal/pal_lower_builtins_pass.h
        pal/pal_lower_builtins_pass.cpp
        pal/pal_insert_halt_pass.h
        pal/pal_insert_halt_pass.cpp
        pal/pal_utils.h
        pal/pal_utils.cpp
        pal/pal_device.h
        pal/pal_device.cpp)
    target_link_libraries(${AnyDSL_runtime_TARGET_NAME}_pal PRIVATE ${AnyDSL_runtime_TARGET_NAME}_base pal::pal)
    list(APPEND RUNTIME_PLATFORMS ${AnyDSL_runtime_TARGET_NAME}_pal)

    find_file(AnyDSL_runtime_ROCM_OCML_LIB
        NAMES ocml.bc
        HINTS ${CMAKE_SOURCE_DIR}/../rocm-device-libs
        PATH_SUFFIXES build/amdgcn/bitcode
        REQUIRED)
    get_filename_component(AnyDSL_runtime_PAL_BITCODE_PATH ${AnyDSL_runtime_ROCM_OCML_LIB} DIRECTORY)
    get_filename_component(AnyDSL_runtime_PAL_BITCODE_SUFFIX ${AnyDSL_runtime_ROCM_OCML_LIB} EXT)
endif()
set(AnyDSL_runtime_HAS_PAL_SUPPORT ${pal_FOUND} CACHE INTERNAL "enables PAL support")

# look for LLVM for nvptx and gcn
find_package(LLVM CONFIG)
if(LLVM_FOUND)
    if(NOT DEFINED AnyDSL_LLVM_LINK_SHARED)
        if(LLVM_LINK_LLVM_DYLIB)
            set(AnyDSL_LLVM_LINK_SHARED "USE_SHARED")
        endif()
    endif()
    add_definitions(${LLVM_DEFINITIONS})
    include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
    set(AnyDSL_runtime_LLVM_COMPONENTS irreader support passes ${LLVM_TARGETS_TO_BUILD})
    set(AnyDSL_runtime_JIT_LLVM_COMPONENTS ${AnyDSL_runtime_LLVM_COMPONENTS} mcjit)
    if(AnyDSL_runtime_HAS_HSA_SUPPORT)
        find_package(LLD REQUIRED)
        target_link_libraries(${AnyDSL_runtime_TARGET_NAME}_hsa PRIVATE lldELF lldCommon)
        llvm_config(${AnyDSL_runtime_TARGET_NAME}_hsa ${AnyDSL_LLVM_LINK_SHARED} lto option ${LLVM_TARGETS_TO_BUILD})
    endif()
    if(AnyDSL_runtime_HAS_PAL_SUPPORT)
        find_package(LLD REQUIRED)
        target_link_libraries(${AnyDSL_runtime_TARGET_NAME}_pal PRIVATE lldELF lldCommon)
        llvm_config(${AnyDSL_runtime_TARGET_NAME}_pal ${AnyDSL_LLVM_LINK_SHARED} lto option ${LLVM_TARGETS_TO_BUILD})
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
            ../platforms/${frontend}/intrinsics_wmma.impala
            ../platforms/${frontend}/intrinsics_amdgpu.impala
            ../platforms/${frontend}/intrinsics_opencl.impala
            ../platforms/${frontend}/intrinsics_thorin.impala
            ../platforms/${frontend}/intrinsics.impala
            ../platforms/${frontend}/runtime.impala)

        # Add artic-only math intrinsics
        if("${frontend}" STREQUAL "artic")
            set(RUNTIME_FRONTEND_SRCS
                ${RUNTIME_FRONTEND_SRCS}
                ../platforms/${frontend}/intrinsics_math.impala
                ../platforms/${frontend}/intrinsics_spirv.impala
                ../platforms/${frontend}/intrinsics_levelzero.impala)
        endif()

        set(RUNTIME_JIT_SRC jit.cpp)
        set(RUNTIME_SOURCES_FRONTEND_INC_FILE ${CMAKE_CURRENT_BINARY_DIR}/${frontend}/runtime_srcs.inc)
        add_custom_command(OUTPUT ${RUNTIME_SOURCES_FRONTEND_INC_FILE}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/${frontend}
            COMMAND ${Python3_EXECUTABLE} extract_runtime_srcs.py ${RUNTIME_FRONTEND_SRCS} > ${RUNTIME_SOURCES_FRONTEND_INC_FILE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            DEPENDS extract_runtime_srcs.py ${RUNTIME_FRONTEND_SRCS})
        add_library(${AnyDSL_runtime_TARGET_NAME}_jit_${frontend} ${AnyDSL_runtime_BUILD} ${RUNTIME_JIT_SRC} ${RUNTIME_SOURCES_FRONTEND_INC_FILE})
        target_include_directories(${AnyDSL_runtime_TARGET_NAME}_jit_${frontend} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/${frontend} ${Thorin_INCLUDE_DIRS})
        set_target_properties(${AnyDSL_runtime_TARGET_NAME}_jit_${frontend} PROPERTIES CXX_VISIBILITY_PRESET hidden)
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
        list(APPEND RUNTIME_JIT_LIBRARIES ${AnyDSL_runtime_TARGET_NAME}_jit_artic)
        target_link_libraries(${AnyDSL_runtime_TARGET_NAME}_jit_artic PRIVATE ${Thorin_LIBRARIES} ${Artic_LIBRARY})
        llvm_config(${AnyDSL_runtime_TARGET_NAME}_jit_artic ${AnyDSL_LLVM_LINK_SHARED} ${AnyDSL_runtime_JIT_LLVM_COMPONENTS})
    endif()
    if(Impala_FOUND)
        message(STATUS "Found Impala: ${Impala_DIR}")
        add_runtime_jit(impala)
        list(APPEND RUNTIME_JIT_LIBRARIES ${AnyDSL_runtime_TARGET_NAME}_jit_impala)
        target_link_libraries(${AnyDSL_runtime_TARGET_NAME}_jit_impala PRIVATE ${Thorin_LIBRARIES} ${Impala_LIBRARY})
        llvm_config(${AnyDSL_runtime_TARGET_NAME}_jit_impala ${AnyDSL_LLVM_LINK_SHARED} ${AnyDSL_runtime_JIT_LLVM_COMPONENTS})
    endif()
endif()
set(AnyDSL_runtime_HAS_JIT_SUPPORT ${RUNTIME_JIT_LIBRARIES} CACHE INTERNAL "enables anydsl_compile() API")

include_directories(${CMAKE_BINARY_DIR}/include)
configure_file(anydsl_runtime_config.h.in ${AnyDSL_runtime_CONFIG_FILE} @ONLY)
set_source_files_properties(${AnyDSL_runtime_CONFIG_FILE} PROPERTIES GENERATED TRUE)

add_library(${AnyDSL_runtime_TARGET_NAME}
    ${AnyDSL_runtime_BUILD}
    ${AnyDSL_runtime_CONFIG_FILE}
    anydsl_runtime.cpp
    anydsl_runtime.h
    anydsl_runtime.hpp)

# System threads are required to use either TBB or C++11 threads
find_package(Threads REQUIRED)
target_link_libraries(${AnyDSL_runtime_TARGET_NAME} PRIVATE Threads::Threads)

# TBB is optional, C++11 threads are used when it is not available
find_package(TBB QUIET)
if(TBB_FOUND)
    message(STATUS "Found TBB in ${TBB_DIR}")
    target_link_libraries(${AnyDSL_runtime_TARGET_NAME} PRIVATE TBB::tbb)
endif()
set(AnyDSL_runtime_HAS_TBB_SUPPORT ${TBB_FOUND} CACHE INTERNAL "enables parallel using TBB")

# If the runtime is built as a shared library, individual
# dependencies must be built with position independent code.
if(AnyDSL_runtime_BUILD_SHARED)
    set_property(TARGET ${AnyDSL_runtime_TARGET_NAME}_base ${RUNTIME_PLATFORMS} PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

target_link_libraries(${AnyDSL_runtime_TARGET_NAME} PRIVATE ${AnyDSL_runtime_TARGET_NAME}_base ${RUNTIME_PLATFORMS})
set_target_properties(${AnyDSL_runtime_TARGET_NAME} PROPERTIES DEFINE_SYMBOL "AnyDSL_runtime_EXPORTS")
set_target_properties(${AnyDSL_runtime_TARGET_NAME} PROPERTIES CXX_VISIBILITY_PRESET hidden)

foreach(jit_lib ${AnyDSL_runtime_HAS_JIT_SUPPORT})
    target_link_libraries(${jit_lib} PRIVATE ${AnyDSL_runtime_TARGET_NAME} ${AnyDSL_runtime_TARGET_NAME}_base)
    set_target_properties(${jit_lib} PROPERTIES DEFINE_SYMBOL "AnyDSL_runtime_jit_EXPORTS")
endforeach()

if(LLVM_FOUND)
    llvm_config(${AnyDSL_runtime_TARGET_NAME} ${AnyDSL_LLVM_LINK_SHARED} ${AnyDSL_runtime_LLVM_COMPONENTS})
endif()

set(RUNTIME_LIBRARIES ${AnyDSL_runtime_TARGET_NAME} ${RUNTIME_JIT_LIBRARIES})
# If the runtime is built as a static library, individual
# dependencies must be exported via CMake packaging
if(NOT AnyDSL_runtime_BUILD_SHARED)
    set(RUNTIME_LIBRARIES ${RUNTIME_LIBRARIES} ${AnyDSL_runtime_TARGET_NAME}_base ${RUNTIME_PLATFORMS})
endif()
set(RUNTIME_LIBRARIES ${RUNTIME_LIBRARIES} PARENT_SCOPE)
