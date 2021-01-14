# Find the HSA includes and library
#
# To set manually the paths, define these environment variables:
#  HSA_DIR          - HSA install directory
#
# Once done this will define
#  HSA_INCLUDE_DIRS - where to find HSA include files
#  HSA_LIBRARIES    - where to find HSA libs
#  HSA_FOUND        - True if HSA is found

find_path(HSA_INCLUDE_DIR hsa.h         HINTS ${HSA_DIR} PATHS /opt/rocm PATH_SUFFIXES include/hsa)
find_library(HSA_LIBRARY  hsa-runtime64 HINTS ${HSA_DIR} PATHS /opt/rocm PATH_SUFFIXES lib)

find_path(ROCM3_BITCODE_PATH ocml.amdgcn.bc HINTS ${HSA_DIR}/lib PATHS /opt/rocm/lib)
find_path(ROCM4_BITCODE_PATH ocml.bc HINTS ${HSA_DIR}/amdgcn/bitcode PATHS /opt/rocm/amdgcn/bitcode)
if(ROCM3_BITCODE_PATH)
    set(AnyDSL_runtime_HSA_BITCODE_PATH ${ROCM3_BITCODE_PATH})
    set(AnyDSL_runtime_HSA_BITCODE_SUFFIX ".amdgcn.bc")
endif()
if(ROCM4_BITCODE_PATH)
    set(AnyDSL_runtime_HSA_BITCODE_PATH ${ROCM4_BITCODE_PATH})
    set(AnyDSL_runtime_HSA_BITCODE_SUFFIX ".bc")
endif()

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HSA DEFAULT_MSG HSA_INCLUDE_DIR HSA_LIBRARY AnyDSL_runtime_HSA_BITCODE_PATH AnyDSL_runtime_HSA_BITCODE_SUFFIX)

set(HSA_INCLUDE_DIRS ${HSA_INCLUDE_DIR})
set(HSA_LIBRARIES    ${HSA_LIBRARY})

mark_as_advanced(HSA_INCLUDE_DIR HSA_LIBRARY ROCM3_BITCODE_PATH ROCM4_BITCODE_PATH)
