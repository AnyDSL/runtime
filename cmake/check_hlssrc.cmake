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

# ${_basename}.hls -> ${_basename}_hls.cpp
if(EXISTS ${_basename}.hls)
    execute_process(COMMAND cp ${_basename}.hls ${_basename}_hls.cpp)
    execute_process(COMMAND echo "int main() { return 0; }" OUTPUT_FILE ${_basename}_tb.cpp)
    execute_process(COMMAND awk -F "[ (]+" "/void .*;/{ ORS=\";\"; print $2 }" ${_basename}.hls
                    COMMAND awk "{ ORS=\"\"; sub(/;$/,\"\"); print }" OUTPUT_VARIABLE kernels)

    list(FIND kernels "hls_top" top_module)
    list(GET kernels ${top_module} kernel)
    #TODO opening a Vitis solution in TCL for SoC
    string(CONCAT tcl_script "set project_name    \"${PROJECT_NAME}\"\n"
                            "set lower ${SYNTHESIS}\n"
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
                            "add_files -cflags {-DNO_SYNTH} $kernel_file\n"
                            "add_files -tb $kernel_testbench\n"
                            "open_solution -flow_target vitis -reset $solution\n"
                            "set lower $kernel_platform\n"
                            "set_part $kernel_platform\n"
                            "create_clock -period 10 -name default\n"
                            "#csim_design -ldflags {-lrt} -clean\n"
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
                            "            config_compile -name_max_length 256 -pipeline_loops 64\n"
                            "            config_schedule -enable_dsp_full_reg\n"
                            "            csynth_design\n"
                            "            export_design -flow syn -format xo \\\n"
                            "            -output $xoflow_path/$kernel_name.xo\n"
                            "        }\n"
                            "    }\n"
                            "exit\n"
                            "} else {\n"
                            "    puts { **** HW sysnthesis is Disabled **** }\n"
                            "    exit\n"
                            "}\n")

    set(kernel_workspace "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}/${_basename}_${kernel}/xoFlow")

    set(VPP_opt "-O3")
    set(VPP_kernel "-k")
    set(VPP_platform "-f")
    set(VPP_target "-t")
    set(VPP_link "--link")
    set(VPP_out "-o")
    set(VPP_compile "-c")
    set(VPP_input "--input_files")
    set(VPP_config "--config")
    set(VPP_profile "--profile_kernel")
    set(VPP_debug "-g")
    set(PROFILE_TYPE "data:all:all:all")

    STRING(TOLOWER ${HW_EMULATION} HW_EMULATION)
    if((${SYNTHESIS} STREQUAL "on"))

        if((${HW_EMULATION} STREQUAL "on"))
            STRING(APPEND VPP_target "hw_emu")
        else()
            STRING(APPEND VPP_target "hw")
        endif()

    else()
        STRING(APPEND VPP_target "sw_emu")
    endif()

    if(PROFILER)
        file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/xrt.ini "[Debug]\n
                                                        profile=true\n
                                                        power_profile=true\n
                                                        timeline_trace=true\n
                                                        data_transfer_trace=coarse\n
                                                        stall_trace=all\n")
        execute_process(COMMAND ${Xilinx_VPP} ${VPP_debug} ${VPP_target} ${VPP_opt} ${VPP_platform} ${PLATFORM_NAME}
                                ${VPP_compile} ${VPP_kernel} ${kernel} ${VPP_input} ${_basename}_hls.cpp
                                ${VPP_out} ${kernel_workspace}/${kernel}.xo ${VPP_profile} ${PROFILE_TYPE})
    elseif(NOT SOC)
        execute_process(COMMAND ${Xilinx_VPP} ${VPP_debug} ${VPP_target} ${VPP_opt} ${VPP_platform} ${PLATFORM_NAME}
                                ${VPP_compile} ${VPP_kernel} ${kernel} ${VPP_input} ${_basename}_hls.cpp
                                ${VPP_out} ${kernel_workspace}/${kernel}.xo)
    else()
        execute_process(COMMAND echo "${tcl_script}" OUTPUT_FILE ${_basename}_${kernel}.tcl)
        # Compiling with vivado/Vitis HLS (only when synthesis is enabled and no profiling is requested)
        # TCL script skips any compilation withous synthesis
        execute_process(COMMAND ${Xilinx_HLS} -f ${_basename}_${kernel}.tcl)

    endif()
    if(Xilinx_KERNEL_INFO)
        execute_process(COMMAND ${Xilinx_KERNEL_INFO} ${kernel_workspace}/${kernel}.xo OUTPUT_VARIABLE KERNEL_INFO)
        STRING(REGEX MATCHALL "${kernel}_[0-9]+" arg_matches "${KERNEL_INFO}")
    endif()

    if(NOT SOC AND BOARD_PART AND (${kernel} STREQUAL "hls_top"))
        message(STATUS "Integrating ${kernel} into ${PLATFORM_NAME}")

        STRING(APPEND VPP_platform "${PLATFORM_NAME}")
        file(WRITE ${kernel_workspace}/config.cfg "[connectivity]\n")
        #set(arg_num 0)
        #foreach (arg ${arg_matches})
        #file(APPEND ${kernel_workspace}/config.cfg "sp=${kernel}_1.${arg}:DDR[${arg_num}]\n")
            file(APPEND ${kernel_workspace}/config.cfg "")
            #   MATH(EXPR arg_num "${arg_num}+1")
            # endforeach()
        set(EM_CONFIG_FILE "")

        if(NOT PROFILER)
            unset(VPP_profile)
            unset(VPP_debug)
            unset(PROFILE_TYPE)
        endif()

        if(${SYNTHESIS} STREQUAL "on")
            if(${HW_EMULATION} STREQUAL "on")
                execute_process(COMMAND ${Xilinx_EMU_CONFIG} --platform ${PLATFORM_NAME})
                set(EM_CONFIG_FILE ${CMAKE_CURRENT_BINARY_DIR}/emconfig.json)
                STRING(APPEND VPP_out "${kernel_workspace}/${kernel}_hw_emu.xclbin")
            elseif((${HW_EMULATION} STREQUAL "off"))
                STRING(APPEND VPP_out "${kernel_workspace}/${kernel}_hw.xclbin")
            endif()
        elseif(${SYNTHESIS} STREQUAL "off")
            execute_process(COMMAND ${Xilinx_EMU_CONFIG} --platform ${PLATFORM_NAME})
            set(EM_CONFIG_FILE ${CMAKE_CURRENT_BINARY_DIR}/emconfig.json)
            STRING(APPEND VPP_out "${kernel_workspace}/${kernel}.xo")
            set(VPP_flags ${VPP_target} ${VPP_opt} ${VPP_platform} ${VPP_out})
            # Vitis compiling (just for software emulation)
            execute_process(COMMAND ${Xilinx_VPP} ${VPP_flags} ${VPP_compile} -DNO_SYNTH ${VPP_kernel} ${kernel}
                                    ${VPP_input} ${_basename}_hls.cpp ${VPP_config} ${kernel_workspace}/config.cfg)
            set(VPP_out "-o${kernel_workspace}/${kernel}_sw_emu.xclbin")
        endif()

        # Vitis linking
        set(VPP_flags ${VPP_target} ${VPP_opt} ${VPP_platform} ${VPP_out})
        execute_process(COMMAND ${Xilinx_VPP} ${VPP_flags} ${VPP_debug} ${VPP_link} ${kernel_workspace}/${kernel}.xo
                                ${VPP_kernel} ${kernel}
                                ${VPP_profile} ${PROFILE_TYPE} ${VPP_config} ${kernel_workspace}/config.cfg)
        if(NOT WIN32)
            file(MAKE_DIRECTORY "${kernel}_xlbin")
            file(GLOB XCLBIN_FILE ${kernel_workspace}/${kernel}_*.xclbin)
            file(COPY ${EM_CONFIG_FILE} ${XCLBIN_FILE} DESTINATION "${kernel}_xlbin")

            if(${VPP_target} STREQUAL "-tsw_emu")
                list(FILTER XCLBIN_FILE INCLUDE REGEX "sw_emu")
            elseif(${VPP_target} STREQUAL "-thw_emu")
                list(FILTER XCLBIN_FILE INCLUDE REGEX "hw_emu")
            else()
                list(FILTER XCLBIN_FILE INCLUDE REGEX "hw")
            endif()
            message(STATUS ${XCLBIN_FILE})
            execute_process(COMMAND ln -sf "${XCLBIN_FILE}" "${_basename}.xclbin")
        else()
            file(RENAME ${XCLBIN_FILE} "${_basename}.xclbin")
        endif()

    endif()

    STRING(TOLOWER ${SYNTHESIS} SYNTHESIS)
    if(NOT SOC AND (${SYNTHESIS} STREQUAL "off"))
        message(STATUS "${BoldWhite}XCL_EMULATION_MODE=sw_emu is required${ColourReset}")
    else()
        STRING(REGEX REPLACE "-t([^ ]*)" "\\1"  VPP_target ${VPP_target})
        if(NOT ${VPP_target} STREQUAL "hw")
            message(STATUS "${BoldWhite}XCL_EMULATION_MODE=${VPP_target} is required${ColourReset}")
        endif()
        if(PROFILER)
            message(STATUS "${BoldWhite} Runtime profiling: vitis_analyzer timeline_trace.csv${ColourReset}")
        endif()
    endif()

endif()
