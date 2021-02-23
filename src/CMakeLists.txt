set(RUNTIME_LIBRARIES)
set(CONF_RUNTIME_LIBRARIES)
set(CONF_RUNTIME_ARTIC_JIT_LIBRARIES)
set(CONF_RUNTIME_IMPALA_JIT_LIBRARIES)

# look for CUDA
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
    set_source_files_properties(${OPENCL_PLATFORM} PROPERTIES INCLUDE_DIRECTORIES "${OpenCL_INCLUDE_DIRS}")
    list(APPEND CONF_RUNTIME_LIBRARIES ${OpenCL_LIBRARIES})
endif()
set(AnyDSL_runtime_HAS_OPENCL_SUPPORT ${OpenCL_FOUND} CACHE INTERNAL "enables OpenCL support")

# look for HSA
find_package(HSA)
if(HSA_FOUND)
    set(HSA_PLATFORM hsa_platform.cpp hsa_platform.h)
    set_source_files_properties(${HSA_PLATFORM} PROPERTIES INCLUDE_DIRECTORIES "${HSA_INCLUDE_DIRS}")
    list(APPEND CONF_RUNTIME_LIBRARIES ${HSA_LIBRARIES})
endif()
set(AnyDSL_runtime_HAS_HSA_SUPPORT ${HSA_FOUND} CACHE INTERNAL "enables HSA support")

# look for Threads
find_package(Threads REQUIRED)
list(APPEND CONF_RUNTIME_LIBRARIES ${CMAKE_THREAD_LIBS_INIT})

# look for TBB
find_package(TBB)
if(TBB_FOUND)
    set_source_files_properties(runtime.cpp PROPERTIES INCLUDE_DIRECTORIES "${TBB_INCLUDE_DIRS}")
    list(APPEND CONF_RUNTIME_LIBRARIES ${TBB_LIBRARIES})
endif()
set(AnyDSL_runtime_HAS_TBB_SUPPORT ${TBB_FOUND} CACHE INTERNAL "enables parallel using TBB")

# look for LLVM for nvptx and gcn
find_package(LLVM)
if(LLVM_FOUND)
    add_definitions(${LLVM_DEFINITIONS})
    include_directories(SYSTEM ${LLVM_INCLUDE_DIRS})
    if(AnyDSL_runtime_HAS_HSA_SUPPORT)
        find_library(LLD_COMMON_LIBRARY lldCommon HINTS ${LLVM_LIBRARY_DIRS})
        find_library(LLD_CORE_LIBRARY   lldCore   HINTS ${LLVM_LIBRARY_DIRS})
        find_library(LLD_DRIVER_LIBRARY lldDriver HINTS ${LLVM_LIBRARY_DIRS})
        find_library(LLD_ELF_LIBRARY    lldELF    HINTS ${LLVM_LIBRARY_DIRS})
        list(APPEND CONF_RUNTIME_LIBRARIES ${LLD_CORE_LIBRARY} ${LLD_ELF_LIBRARY} ${LLD_DRIVER_LIBRARY} ${LLD_COMMON_LIBRARY})
    endif()
    set(AnyDSL_runtime_LLVM_COMPONENTS irreader support ${LLVM_TARGETS_TO_BUILD})
    set(AnyDSL_runtime_JIT_LLVM_COMPONENTS "${AnyDSL_runtime_LLVM_COMPONENTS};mcjit")
endif()
set(AnyDSL_runtime_HAS_LLVM_SUPPORT ${LLVM_FOUND} CACHE INTERNAL "enables nvptx / gcn support")

option(AnyDSL_runtime_BUILD_SHARED "whether to build AnyDSL_runtime as shared or static library" ${BUILD_SHARED_LIBS})
if(AnyDSL_runtime_BUILD_SHARED)
    set(AnyDSL_runtime_BUILD "SHARED")
else()
    set(AnyDSL_runtime_BUILD "STATIC")
endif()

if(MSVC)
    add_compile_options("/experimental:external" "/external:anglebrackets" "/external:W0")
else()
    add_compile_options("-Wall" "-Wextra")
endif()

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

        set(RUNTIME_JIT_SRC jit.cpp)
        set(RUNTIME_SOURCES_FRONTEND_INC_FILE ${CMAKE_CURRENT_BINARY_DIR}/${frontend}/runtime_srcs.inc)
        add_custom_command(OUTPUT ${RUNTIME_SOURCES_FRONTEND_INC_FILE}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/${frontend}
            COMMAND ${Python3_EXECUTABLE} extract_runtime_srcs.py ${RUNTIME_FRONTEND_SRCS} > ${RUNTIME_SOURCES_FRONTEND_INC_FILE}
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
            DEPENDS extract_runtime_srcs.py ${RUNTIME_FRONTEND_SRCS})
        add_library(runtime_jit_${frontend} ${AnyDSL_runtime_BUILD} ${RUNTIME_JIT_SRC} ${RUNTIME_SOURCES_FRONTEND_INC_FILE})
        target_include_directories(runtime_jit_${frontend} PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/${frontend})
        set_target_properties(runtime_jit_${frontend} PROPERTIES CXX_VISIBILITY_PRESET hidden)
        set_source_files_properties(${RUNTIME_JIT_SRC} PROPERTIES INCLUDE_DIRECTORIES "${Thorin_INCLUDE_DIRS}")
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
        list(APPEND RUNTIME_LIBRARIES runtime_jit_artic)
        list(APPEND CONF_RUNTIME_ARTIC_JIT_LIBRARIES ${Thorin_LIBRARIES} ${Artic_LIBRARY})
        target_link_libraries(runtime_jit_artic PRIVATE ${CONF_RUNTIME_ARTIC_JIT_LIBRARIES})
        llvm_config(runtime_jit_artic ${AnyDSL_LLVM_LINK_SHARED} ${AnyDSL_runtime_JIT_LLVM_COMPONENTS})
    endif()
    if(Impala_FOUND)
        message(STATUS "Found Impala: ${Impala_DIR}")
        add_runtime_jit(impala)
        list(APPEND RUNTIME_LIBRARIES runtime_jit_impala)
        list(APPEND CONF_RUNTIME_IMPALA_JIT_LIBRARIES ${Thorin_LIBRARIES} ${Impala_LIBRARY})
        target_link_libraries(runtime_jit_impala PRIVATE ${CONF_RUNTIME_IMPALA_JIT_LIBRARIES})
        llvm_config(runtime_jit_impala ${AnyDSL_LLVM_LINK_SHARED} ${AnyDSL_runtime_JIT_LLVM_COMPONENTS})
    endif()
endif()
set(AnyDSL_runtime_HAS_JIT_SUPPORT ${RUNTIME_LIBRARIES} CACHE INTERNAL "enables anydsl_compile() API")

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
            ${CUDA_PLATFORM}
            ${OPENCL_PLATFORM}
            ${HSA_PLATFORM})

target_link_libraries(runtime PRIVATE ${CONF_RUNTIME_LIBRARIES})
set_target_properties(runtime PROPERTIES DEFINE_SYMBOL "AnyDSL_runtime_EXPORTS")
set_target_properties(runtime PROPERTIES CXX_VISIBILITY_PRESET hidden)
list(APPEND RUNTIME_LIBRARIES runtime)

foreach(jit_lib ${AnyDSL_runtime_HAS_JIT_SUPPORT})
    target_link_libraries(${jit_lib} PRIVATE runtime)
	set_target_properties(${jit_lib} PROPERTIES DEFINE_SYMBOL "AnyDSL_runtime_EXPORTS")
endforeach()

set(RUNTIME_LIBRARIES ${RUNTIME_LIBRARIES} PARENT_SCOPE)
set(CONF_RUNTIME_LIBRARIES ${CONF_RUNTIME_LIBRARIES} PARENT_SCOPE)
set(CONF_RUNTIME_ARTIC_JIT_LIBRARIES ${CONF_RUNTIME_ARTIC_JIT_LIBRARIES} PARENT_SCOPE)
set(CONF_RUNTIME_IMPALA_JIT_LIBRARIES ${CONF_RUNTIME_IMPALA_JIT_LIBRARIES} PARENT_SCOPE)
