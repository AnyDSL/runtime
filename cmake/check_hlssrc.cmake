# ${_basename}.hls -> ${_basename}_hls.cpp - > ${_basename}_hls.o
if(EXISTS ${_basename}.hls)
    execute_process(COMMAND cp ${_basename}.hls ${_basename}_hls.cpp)
    execute_process(COMMAND echo "int main() { return 0; }" OUTPUT_FILE ${_basename}_tb.cpp)
    execute_process(COMMAND awk -F "[ (]+" "/void .*;/{ ORS=\";\"; print $2 }" ${_basename}.hls
                    COMMAND awk "{ ORS=\"\"; sub(/;$/,\"\"); print }" OUTPUT_VARIABLE kernels)
    # Still need to extract all kernels for C-simulation
    foreach(kernel ${kernels})
        string(CONCAT tcl_script "set project_name    \"anydsl_fpga\"\n"
                                "set kernel_name      \"${kernel}\"\n"
                                "set kernel_file      \"${_basename}_hls.cpp\"\n"
                                "set kernel_testbench \"${_basename}_tb.cpp\"\n"
                                "set solution         \"${_basename}_${kernel}\"\n"
                                "set xoflow_path      \"./$project_name/$solution/xoFlow\"\n"
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
        execute_process(COMMAND vivado_hls -f ${_basename}_${kernel}.tcl)
        # Linking, if not SOC, if hls top
    endforeach()

    #TODO: 1) Moving this IF to the kernels extraction loop
    #TODO: 2) Finding appropriate BSP on host, linking BSP and HLS TOP object files
        if (NOT SOC)
            message(STATUS "Integrating to u280 BSP")
            execute_process(COMMAND v++ -l --platform xilinx_u280_xdma_201910_1 ./anydsl_fpga/
                ${_basename}_${kernel}/xoFlow/${kernel}.xo)
        endif()
        string(CONCAT tcl_script "puts {HELLO HPC}\n"
                                "exit")
        list(GET kernels 0 kernel)
        execute_process(COMMAND echo "${tcl_script}" OUTPUT_FILE ${_basename}_${kernel}.tcl)
        execute_process(COMMAND vivado -mode batch -notrace -source ${_basename}_${kernel}.tcl)
endif()
