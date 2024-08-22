# AMD AIE tools for scripting mode

find_path(XILINX_SEARCH_PATH v++ PATHS ENV XILINX_OPENCL ENV XILINX_VITIS PATH_SUFFIXES bin)
get_filename_component(VITIS_ROOT_DIR ${XILINX_SEARCH_PATH} DIRECTORY)

find_program(Xilinx_AIE_COMPILER aiecompiler PATH ${VITIS_ROOT_DIR}/aietools/bin NO_DEFAULT_PATH)
find_program(Xilinx_AIE_SIMULATOR aiesimulator PATH ${VITIS_ROOT_DIR}/aietools/bin NO_DEFAULT_PATH)
find_program(Xilinx_AIE_X86SIMULATOR x86simulator PATH ${VITIS_ROOT_DIR}/aietools/bin NO_DEFAULT_PATH)

get_filename_component(VITIS_VERSION "${VITIS_ROOT_DIR}" NAME)
string(REGEX REPLACE "([0-9]+)\\.[0-9]+" "\\1" VITIS_MAJOR_VERSION "${VITIS_VERSION}")
string(REGEX REPLACE "[0-9]+\\.([0-9]+)" "\\1" VITIS_MINOR_VERSION "${VITIS_VERSION}")

set(VITIS_MAJOR_VERSION ${VITIS_MAJOR_VERSION} CACHE INTERNAL "")
set(VITIS_MINOR_VERSION ${VITIS_MINOR_VERSION} CACHE INTERNAL "")


find_path(Xilinx_AIE_INCLUDE_DIRS adf.h PATHS ${VITIS_ROOT_DIR}/aietools/include NO_DEFAULT_PATH)
set(Xilinx_AIE_INCLUDE_DIRS ${Xilinx_AIE_INCLUDE_DIRS})

mark_as_advanced(
    Xilinx_AIE_COMPILER
    Xilinx_AIE_SIMULATOR
    Xilinx_AIE_X86SIMULATOR
    Xilinx_AIE_INCLUDE_DIRS)

include(FindPackageHandleStandardArgs)

find_package_handle_standard_args(AIE DEFAULT_MSG
    Xilinx_AIE_COMPILER
    Xilinx_AIE_SIMULATOR
    Xilinx_AIE_X86SIMULATOR
    Xilinx_AIE_INCLUDE_DIRS
)
