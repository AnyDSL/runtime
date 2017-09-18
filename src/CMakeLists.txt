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
    set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} -DENABLE_CUDA)
    set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} "-DLIBDEVICE_DIR=\"${CUDA_TOOLKIT_ROOT_DIR}/nvvm/libdevice/\"")
    set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} "-DNVCC_BIN=\"${CUDA_TOOLKIT_ROOT_DIR}/bin/nvcc\"")
    set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} "-DNVCC_INC=\"${CUDA_INCLUDE_DIRS}\"")
    list(APPEND CONF_RUNTIME_LIBRARIES "${CUDA_LIBRARY}" "${CUDA_NVVM_LIBRARY}" "${CUDA_NVRTC_LIBRARY}")
else()
    set(CUDA_PLATFORM "")
endif()

# look for OpenCL
find_package(OpenCL)
if(OpenCL_FOUND)
    include_directories(${OpenCL_INCLUDE_DIRS})
    set(OPENCL_PLATFORM opencl_platform.cpp opencl_platform.h)
    set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} -DENABLE_OPENCL)
    list(APPEND CONF_RUNTIME_LIBRARIES "${OpenCL_LIBRARIES}")
else()
    set(OPENCL_PLATFORM "")
endif()

# look for HSA
find_package(HSA)
if(HSA_FOUND)
    include_directories(${HSA_INCLUDE_DIRS})
    set(HSA_PLATFORM hsa_platform.cpp hsa_platform.h)
    set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} -DENABLE_HSA)
    list(APPEND CONF_RUNTIME_LIBRARIES "${HSA_LIBRARIES}")
else()
    set(HSA_PLATFORM "")
endif()

# look for Threads
find_package(Threads REQUIRED)
list(APPEND CONF_RUNTIME_LIBRARIES "${CMAKE_THREAD_LIBS_INIT}")

# look for TBB
find_package(TBB)
if(TBB_FOUND)
    include_directories(${TBB_INCLUDE_DIRS})
    set(RUNTIME_DEFINITIONS ${RUNTIME_DEFINITIONS} -DENABLE_TBB)
    list(APPEND CONF_RUNTIME_LIBRARIES "${TBB_LIBRARIES}")
endif()

if(NOT MSVC)
    add_definitions("-Wall -Wextra")
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
            ${CUDA_PLATFORM}
            ${OPENCL_PLATFORM}
            ${HSA_PLATFORM})

target_link_libraries(runtime ${CONF_RUNTIME_LIBRARIES})

# generate the Runtime.cmake file from the template
configure_file(../cmake/Runtime.cmake.in ${CMAKE_BINARY_DIR}/cmake/Runtime.cmake @ONLY)
