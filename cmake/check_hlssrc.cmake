if(NOT WIN32)
  string(ASCII 27 Esc)
  set(ColourReset "${Esc}[m")
  set(BoldWhite   "${Esc}[1;37m")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/modules")
find_package(XHLS)

# Extracting FPGA platform and Board PART from FPGA_PART macro
if(Xilinx_PLATFORM_INFO)
    execute_process(COMMAND ${Xilinx_PLATFORM_INFO} --list OUTPUT_VARIABLE PLATFORM_LIST)
    STRING(REGEX MATCHALL "\"baseName\": [^, \t]+" matches "${PLATFORM_LIST}")
    STRING(REGEX REPLACE "\"baseName\": |[\"]" "" PLATFORMS_ "${matches}")
    STRING(TOLOWER ${FPGA_PART} FPGA_PART)
    set(PLATFORM_NAME)
    foreach(PLATFORM_ ${PLATFORMS_})
        if(${PLATFORM_} MATCHES ${FPGA_PART})
            message(STATUS "Platform ${PLATFORM_} found for ${FPGA_PART}.")
            execute_process(COMMAND ${Xilinx_PLATFORM_INFO} --json=hardwarePlatform.board.part --platform ${PLATFORM_} OUTPUT_VARIABLE BOARD_PART)
            STRING(REPLACE "\n" "" BOARD_PART ${BOARD_PART})
            set(FPGA_PART ${BOARD_PART})
            set(PLATFORM_NAME ${PLATFORM_})
            break()
        endif()
    endforeach()
    if(NOT BOARD_PART)
        message(STATUS "No platform found!")
    else()
        message(STATUS "BOARD_PART: ${BOARD_PART} \n ")
    endif()
endif()

set(PROJECT_NAME "anydsl_fpga")

# ${_basename}.hls -> ${_basename}_hls.cpp - > ${_basename}_hls.o
if(EXISTS ${_basename}.hls)
    execute_process(COMMAND cp ${_basename}.hls ${_basename}_hls.cpp)
    execute_process(COMMAND echo "int main() { return 0; }" OUTPUT_FILE ${_basename}_tb.cpp)
    execute_process(COMMAND awk -F "[ (]+" "/void .*;/{ ORS=\";\"; print $2 }" ${_basename}.hls
                    COMMAND awk "{ ORS=\"\"; sub(/;$/,\"\"); print }" OUTPUT_VARIABLE kernels)
    # Still need to extract all kernels for C-simulation but only hls_top is synthesized
    foreach(kernel ${kernels})
        string(CONCAT tcl_script "set project_name    \"${PROJECT_NAME}\"\n"
                                "set kernel_name      \"${kernel}\"\n"
                                "set kernel_file      \"${_basename}_hls.cpp\"\n"
                                "set kernel_testbench \"${_basename}_tb.cpp\"\n"
                                "set solution         \"${_basename}_${kernel}\"\n"
                                "set xoflow_path      \"${CMAKE_CURRENT_BINARY_DIR}/$project_name/$solution/xoFlow\"\n"
                                "set kernel_platform  \"${FPGA_PART}\"\n"
                                "\n"
                                "switch $kernel_platform {\n"
                                "   u50 {\n"
                                "       set kernel_platform \"xcu50-fsvh2104-2-e\"\n"
                                "   }\n"
                                "   u200 {\n"
                                "       set kernel_platform \"xcu200-fsgd2104-2-e\"\n"
                                "   }\n"
                                "   u250 {\n"
                                "       set kernel_platform \"xcu250-figd2104-2L-e\"\n"
                                "   }\n"
                                "   u280 {\n"
                                "       set kernel_platform \"xcu280-fsvh2892-2L-e\"\n"
                                "   }\n"
                                "   default {\n"
                                "       set kernel_platform $kernel_platform\n"
                                "   }\n"
                                "}\n"
                                "\n"
                                "open_project -reset $project_name\n"
                                "set_top $kernel_name\n"
                                "add_files $kernel_file\n"
                                "add_files -tb $kernel_testbench\n"
                                "open_solution -reset $solution\n"
                                "set lower $kernel_platform\n"
                                "set_part $kernel_platform\n"
                                "create_clock -period 10 -name default\n"
                                "csim_design -compiler clang -ldflags {-lrt} -clean\n"
                                "\n"
                                "set lower ${SYNTHESIS}\n"
                                "if [string match {on} ${SYNTHESIS}] {\n"
                                "    if [string match {hls_top} $kernel_name] {\n"
                                "        if [string match {on} ${SOC}] {\n"
                                "            puts { **** HW synthesis for SoC **** }\n"
                                "            config_bind -effort high\n"
                                "            config_schedule -effort high\n"
                                "            csynth_design\n"
                                "            get_clock_period\n"
                                "            export_design -format ip_catalog -evaluate verilog\n"
                                "        } else {\n"
                                "            puts { **** HW synthesis for HPC **** }\n"
                                "            file mkdir xoFlow\n"
                                "            config_sdx -optimization_level none -target xocc\n"
                                "            config_export -vivado_optimization_level 0 -vivado_phys_opt none\n"
                                "            config_compile -name_max_length 256 -pipeline_loops 64\n"
                                "            config_schedule -enable_dsp_full_reg\n"
                                "            csynth_design\n"
                                "            export_design -rtl verilog -format ip_catalog -xo \\\n"
                                "            $xoflow_path/$kernel_name.xo\n"
                                "        }\n"
                                "    }\n"
                                "exit\n"
                                "} else {\n"
                                "    puts { **** HW sysnthesis is Disabled **** }\n"
                                "    exit\n"
                                "}\n")

        execute_process(COMMAND echo "${tcl_script}" OUTPUT_FILE ${_basename}_${kernel}.tcl)
                execute_process(COMMAND ${Xilinx_HLS} -f ${_basename}_${kernel}.tcl)

                if (NOT SOC AND BOARD_PART AND (${kernel} STREQUAL "hls_top"))
                    message(STATUS "Integrating ${kernel} into ${PLATFORM_NAME}")
                    set(kernel_workspace "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}/${_basename}_${kernel}/xoFlow")

                    set(VPP_opt "-O3")
                    set(VPP_platform "-f")
                    set(VPP_target "-t")
                    set(VPP_link "--link")
                    set(VPP_out "-o")
                    set(VPP_config "--config")

                    STRING(APPEND VPP_platform "${PLATFORM_NAME}")
                    file(APPEND ${kernel_workspace}/config.cfg "")
                    STRING(TOLOWER ${HW_EMULATION} HW_EMULATION)
                    set(EM_CONFIG_FILE "")
                    if(${HW_EMULATION} STREQUAL "on")
                        execute_process(COMMAND ${Xilinx_EMU_CONFIG} --platform ${PLATFORM_NAME} OUTPUT_FILE emconfig.json)
                        set(EM_CONFIG_FILE ${CMAKE_CURRENT_BINARY_DIR}/emconfig.json)
                        STRING(APPEND VPP_target "hw_emu")
                        STRING(APPEND VPP_out "${kernel_workspace}/${kernel}_hw_emu.xclbin")
                    elseif(${HW_EMULATION} STREQUAL "off")
                        STRING(APPEND VPP_target "hw")
                        STRING(APPEND VPP_out "${kernel_workspace}/${kernel}_hw.xclbin")
                    endif()
                    set(VPP_flags ${VPP_target} ${VPP_opt} ${VPP_platform} ${VPP_out})
                    execute_process(COMMAND ${Xilinx_VPP} ${VPP_flags} ${VPP_link} ${kernel_workspace}/${kernel}.xo ${VPP_config} ${kernel_workspace}/config.cfg)
                    file(MAKE_DIRECTORY "${kernel}_xlbin")
                    #add host code
                    file(GLOB XCLBIN_FILE ${kernel_workspace}/${kernel}_*.xclbin)
                    file(COPY ${EM_CONFIG_FILE} ${XCLBIN_FILE} DESTINATION "${kernel}_bin")
                endif()
    endforeach()
    STRING(TOLOWER ${SYNTHESIS} SYNTHESIS)
    if(${SYNTHESIS} STREQUAL "off")
        message(STATUS "${BoldWhite}XCL_EMULATION_MODE=sw_emu is required${ColourReset}")
    else()
        STRING(REGEX REPLACE "-t([^ ]*)" "\\1"  VPP_target ${VPP_target})
        message(STATUS "${BoldWhite}XCL_EMULATION_MODE=${VPP_target} is required${ColourReset}")
    endif()
        # execute_process(COMMAND vivado -mode batch -notrace -source ${_basename}_${kernel}.tcl)
endif()
