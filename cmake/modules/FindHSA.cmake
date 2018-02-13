# Find the HSA includes and library
#
# To set manually the paths, define these environment variables:
#  HSA_DIR           - Include path
#
# Once done this will define
#  HSA_INCLUDE_DIRS  - where to find HSA include files
#  HSA_LIBRARIES     - where to find HSA libs
#  HSA_FOUND         - True if HSA is found

find_path(HSA_INCLUDE_DIR hsa.h         PATHS ${HSA_DIR} PATH_SUFFIXES include/hsa)
find_library(HSA_LIBRARY  hsa-runtime64 PATHS ${HSA_DIR} PATH_SUFFIXES lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HSA DEFAULT_MSG HSA_INCLUDE_DIR HSA_LIBRARY)

set(HSA_INCLUDE_DIRS ${HSA_INCLUDE_DIR})
set(HSA_LIBRARIES    ${HSA_LIBRARY})

mark_as_advanced(HSA_INCLUDE_DIR HSA_LIBRARY)
