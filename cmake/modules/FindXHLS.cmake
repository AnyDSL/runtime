# Set Xilinx Runtime library (XRT) and HLS tools in scripting mode
# uses system OpenCL headers

if(DEFINED SDACCEL_ROOT_DIR AND NOT DEFINED VITIS_ROOT_DIR)
    set(VITIS_ROOT_DIR ${SDACCEL_ROOT_DIR})
endif()

if(NOT DEFINED VITIS_ROOT_DIR)
    find_path(XILINX_SEARCH_PATH v++ xocc
        PATHS ENV XILINX_OPENCL ENV XILINX_VITIS ENV XILINX_SDACCEL
        PATH_SUFFIXES bin)
    get_filename_component(VITIS_ROOT_DIR ${XILINX_SEARCH_PATH} DIRECTORY)
else()
    message(STATUS "Using user defined directory: ${VITIS_ROOT_DIR}")
endif()

find_program(Xilinx_XOCC xocc PATHS ${VITIS_ROOT_DIR}/bin NO_DEFAULT_PATH)
find_program(Xilinx_VPP v++ PATHS ${VITIS_ROOT_DIR}/bin NO_DEFAULT_PATH)
find_program(Xilinx_PLATFORM_INFO platforminfo PATH ${VITIS_ROOT_DIR}/bin NO_DEFAULT_PATH)
find_program(Xilinx_EMU_CONFIG emconfigutil PATH ${VITIS_ROOT_DIR}/bin NO_DEFAULT_PATH)

if(Xilinx_XOCC)
    set(XILINX_COMPILER ${Xilinx_XOCC})
    set(Xilint_TOOL_IS_LEGACY TRUE)
endif()

if(Xilinx_VPP)
    set(XILINX_COMPILER ${Xilinx_VPP})
    set(XILINX_TOOL_IS_LEGACY FALSE)
endif()

set(Xilinx_COMPILER ${XILINX_COMPILER} CACHE STRING "Compiler used to build FPGA kernels.")
set(Xilinx_TOOL_IS_LEGACY ${XILINX_TOOL_IS_LEGACY} CACHE STRING "Using legacy version of toolchain (pre-Vitis).")

get_filename_component(VITIS_VERSION "${VITIS_ROOT_DIR}" NAME)
string(REGEX REPLACE "([0-9]+)\\.[0-9]+" "\\1" VITIS_MAJOR_VERSION "${VITIS_VERSION}")
string(REGEX REPLACE "[0-9]+\\.([0-9]+)" "\\1" VITIS_MINOR_VERSION "${VITIS_VERSION}")
set(Vitis_VERSION ${VITIS_VERSION} CACHE INTERNAL "Version of Vitis found")
set(Vitis_MAJOR_VERSION ${VITIS_MAJOR_VERSION} CACHE INTERNAL "Major version of Vitis found")
set(Vitis_MINOR_VERSION ${VITIS_MINOR_VERSION} CACHE INTERNAL "Minor version of Vitis found")

find_program(Xilinx_HLS NAMES vivado_hls vitis_hls PATHS
    ${VITIS_ROOT_DIR}/bin
    ${VITIS_ROOT_DIR}/../../Vivado/${Vitis_VERSION}/bin
    ${VITIS_ROOT_DIR}/Vivado_HLS/bin NO_DEFAULT_PATH)

find_program(Xilinx_VIVADO vivado PATHS
    ${VITIS_ROOT_DIR}/../../Vivado/${Vitis_VERSION}/bin
    ${VITIS_ROOT_DIR}/Vivado/bin NO_DEFAULT_PATH)

find_path(Xilinx_HLS_INCLUDE_DIR hls_stream.h PATHS
    ${VITIS_ROOT_DIR}/../../Vivado/${Vitis_VERSION}/include
    ${VITIS_ROOT_DIR}/include
    ${VITIS_ROOT_DIR}/Vivado_HLS/include
    NO_DEFAULT_PATH)

if(Xilinx_VPP OR (Vitis_MAJOR_VERSION GREATER 2018) OR
    (Vitis_MAJOR_VERSION EQUAL 2018 AND Vitis_MINOR_VERSION GREATER 2))
    set(XILINX_USE_XRT TRUE)
else()
    set(XILINX_USE_XRT FALSE)
endif()
set(Xilinx_USE_XRT ${XILINX_USE_XRT} CACHE STRING "Use XRT as runtime. Otherwise, use SDAccel/SDx OpenCL runtime.")

if(NOT DEFINED XRT_ROOT_DIR)
    find_path(XRT_SEARCH_PATH libxilinxopencl.so
        PATHS /opt/xilinx/xrt /opt/Xilinx/xrt
        /tools/Xilinx/xrt /tools/xilinx/xrt
        ENV XILINX_XRT
        PATH_SUFFIXES lib)
    get_filename_component(XRT_ROOT_DIR ${XRT_SEARCH_PATH} DIRECTORY)
    if(NOT XRT_SEARCH_PATH)
        message(FATAL_ERROR "The Xilinx Runtime (XRT) was not found. You can specify the XRT directory with the XRT_ROOT_DIR variable.")
    endif()
    message(STATUS "Found Xilinx Runtime (XRT): ${XRT_ROOT_DIR}")
endif()
set(XILINX_RUNTIME_DIR ${XRT_ROOT_DIR})

file(GLOB Xilinx_XRT_LIBS
    ${XILINX_RUNTIME_DIR}/lib/libxilinxopencl.so)

if(Xilinx_XRT_LIBS)
    set(Xilinx_LIBRARIES ${Xilinx_XRT_LIBS})
endif()

find_path(Xilinx_OPENCL_EXTENSIONS_INCLUDE_DIR cl_ext.h
    PATHS ${XILINX_RUNTIME_DIR}/include
    PATH_SUFFIXES 1_1/CL 1_2/CL 2_0/CL CL
    NO_DEFAULT_PATH)
get_filename_component(Xilinx_OPENCL_EXTENSIONS_INCLUDE_DIR "${Xilinx_OPENCL_EXTENSIONS_INCLUDE_DIR}" DIRECTORY)

if(Xilinx_HLS_INCLUDE_DIR AND Xilinx_OPENCL_EXTENSIONS_INCLUDE_DIR)
    set(Xilinx_INCLUDE_DIRS ${Xilinx_HLS_INCLUDE_DIR}
        ${Xilinx_OPENCL_INCLUDE_DIR} ${Xilinx_OPENCL_EXTENSIONS_INCLUDE_DIR}
        CACHE STRING "Xilinx include directories.")
endif()
mark_as_advanced(XILINX_SEARCH_PATH
    Xilinx_XOCC
    Xilinx_VPP
    Xilinx_HLS_INCLUDE_DIR
    XILINX_RUNTIME_DIR
    XRT_SEARCH_PATH
    XILINX_RUNTIME_LIB_FOLDER
    Xilinx_OPENCL_EXTENSIONS_INCLUDE_DIR
    XILINX_SEARCH_PATH
    Xilinx_TOOL_IS_LEGACY
    Xilinx_INCLUDE_DIRS
    Xilinx_PLATFORM_INFO
    Xilinx_VIVADO)

set(Xilinx_EXPORTS
    Xilinx_COMPILER
    Xilinx_HLS
    Xilinx_LIBRARIES
    Xilinx_INCLUDE_DIRS)

mark_as_advanced(Xilinx_EXPORTS)

include(FindPackageHandleStandardArgs)
# Sets XHLS_FOUND to TRUE if all listed variables were found.
find_package_handle_standard_args(XHLS DEFAULT_MSG ${Xilinx_EXPORTS})
