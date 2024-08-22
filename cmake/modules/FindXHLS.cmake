# Xilinx Runtime library (XRT) and HLS tools for scripting mode

find_path(XILINX_SEARCH_PATH v++ PATHS ENV XILINX_OPENCL ENV XILINX_VITIS PATH_SUFFIXES bin)
get_filename_component(VITIS_ROOT_DIR ${XILINX_SEARCH_PATH} DIRECTORY)

find_program(Xilinx_VPP v++ PATHS ${VITIS_ROOT_DIR}/bin NO_DEFAULT_PATH)
find_program(Xilinx_PLATFORM_INFO platforminfo PATH ${VITIS_ROOT_DIR}/bin NO_DEFAULT_PATH)
find_program(Xilinx_KERNEL_INFO kernelinfo PATH ${VITIS_ROOT_DIR}/bin NO_DEFAULT_PATH)
find_program(Xilinx_EMU_CONFIG emconfigutil PATH ${VITIS_ROOT_DIR}/bin NO_DEFAULT_PATH)
find_program(Xilinx_AIE_COMPILER aiecompiler PATH ${VITIS_ROOT_DIR}/aietools/bin NO_DEFAULT_PATH)
find_program(Xilinx_AIE_SIMULATOR aiesimulator PATH ${VITIS_ROOT_DIR}/aietools/bin NO_DEFAULT_PATH)
find_program(Xilinx_AIE_X86SIMULATOR x86simulator PATH ${VITIS_ROOT_DIR}/aietools/bin NO_DEFAULT_PATH)

get_filename_component(VITIS_VERSION "${VITIS_ROOT_DIR}" NAME)
string(REGEX REPLACE "([0-9]+)\\.[0-9]+" "\\1" VITIS_MAJOR_VERSION "${VITIS_VERSION}")
string(REGEX REPLACE "[0-9]+\\.([0-9]+)" "\\1" VITIS_MINOR_VERSION "${VITIS_VERSION}")

set(VITIS_MAJOR_VERSION ${VITIS_MAJOR_VERSION} CACHE INTERNAL "")
set(VITIS_MINOR_VERSION ${VITIS_MINOR_VERSION} CACHE INTERNAL "")

find_program(Xilinx_HLS NAMES vitis_hls PATHS ${VITIS_ROOT_DIR}/../../Vitis_HLS/${VITIS_VERSION}/bin ${VITIS_ROOT_DIR}/bin NO_DEFAULT_PATH)

find_path(Xilinx_HLS_INCLUDE_DIR hls_stream.h PATHS ${VITIS_ROOT_DIR}/../../Vitis_HLS/${VITIS_VERSION}/include NO_DEFAULT_PATH)

find_path(XRT_SEARCH_PATH libxilinxopencl.so PATHS /opt/xilinx/xrt ENV XILINX_XRT PATH_SUFFIXES lib)
get_filename_component(XILINX_RUNTIME_DIR ${XRT_SEARCH_PATH} DIRECTORY)
file(GLOB Xilinx_LIBRARIES ${XILINX_RUNTIME_DIR}/lib/libxilinxopencl.so)

find_path(Xilinx_OPENCL_EXTENSIONS_INCLUDE_DIR cl_ext.h PATHS ${XILINX_RUNTIME_DIR}/include PATH_SUFFIXES CL NO_DEFAULT_PATH)
get_filename_component(Xilinx_OPENCL_EXTENSIONS_INCLUDE_DIR ${Xilinx_OPENCL_EXTENSIONS_INCLUDE_DIR} DIRECTORY)
set(Xilinx_INCLUDE_DIRS ${Xilinx_HLS_INCLUDE_DIR} ${Xilinx_OPENCL_EXTENSIONS_INCLUDE_DIR})


find_path(Xilinx_AIE_INCLUDE_DIRS adf.h PATHS ${VITIS_ROOT_DIR}/aietools/include NO_DEFAULT_PATH)
set(Xilinx_AIE_INCLUDE_DIRS ${Xilinx_AIE_INCLUDE_DIRS})

mark_as_advanced(
    XRT_SEARCH_PATH
    XILINX_RUNTIME_DIR
    XILINX_SEARCH_PATH
    Xilinx_HLS
    Xilinx_VPP
    Xilinx_HLS_INCLUDE_DIR
    Xilinx_OPENCL_EXTENSIONS_INCLUDE_DIR
    Xilinx_PLATFORM_INFO
    Xilinx_KERNEL_INFO
    Xilinx_EMU_CONFIG
    Xilinx_AIE_COMPILER
    Xilinx_AIE_SIMULATOR
    Xilinx_AIE_X86SIMULATOR
    Xilinx_AIE_INCLUDE_DIRS
    Xilinx_LIBRARIES
    Xilinx_INCLUDE_DIRS)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(XHLS DEFAULT_MSG
    Xilinx_HLS
    Xilinx_VPP
    Xilinx_LIBRARIES
    Xilinx_INCLUDE_DIRS
    Xilinx_PLATFORM_INFO
    Xilinx_KERNEL_INFO
    Xilinx_EMU_CONFIG
)

find_package_handle_standard_args(AIE DEFAULT_MSG
    Xilinx_AIE_COMPILER
    Xilinx_AIE_SIMULATOR
    Xilinx_AIE_X86SIMULATOR
    Xilinx_AIE_INCLUDE_DIRS
)
