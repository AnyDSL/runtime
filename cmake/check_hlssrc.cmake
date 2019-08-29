# ${_basename}.hls -> ${_basename}_hls.cpp - > ${_basename}_hls.o
if(EXISTS ${_basename}.hls)
    execute_process(COMMAND cp ${_basename}.hls ${_basename}_hls.cpp)
    execute_process(COMMAND echo "int main() { return 0; }" OUTPUT_FILE ${_basename}_tb.cpp)
    execute_process(COMMAND awk -F "[ (]+" "/void .*;/{ ORS=\";\"; print $2 }" ${_basename}.hls
                    COMMAND awk "{ ORS=\"\"; sub(/;$/,\"\"); print }" OUTPUT_VARIABLE kernels)
    if (NOT SOC)
        foreach(kernel ${kernels})
            string(CONCAT tcl_script "open_project -reset anydsl_fpga\n"
                                    "set_top ${kernel}\n"
                                    "add_files ${_basename}_hls.cpp\n"
                                    "add_files -tb ${_basename}_tb.cpp\n"
                                    "open_solution -reset ${_basename}_${kernel}\n"
                                    "set lower ${FPGA_PART}\n"
                                    "set_part ${FPGA_PART}\n"
                                    "create_clock -period 10 -name default\n"
                                    "csim_design -compiler clang -ldflags {-lrt} -clean\n"
                                    "set lower ${SYNTHESIS}\n"
                                    "if [string match {on} ${SYNTHESIS}] {\n"
                                    "puts { **** HW synthesis is Enabled **** }\n"
                                    "config_bind -effort high\n"
                                    "config_schedule -effort high\n"
                                    "csynth_design\n"
                                    "get_clock_period\n"
                                    "export_design -format ip_catalog -evaluate verilog\n"
                                    "} else {\n"
                                    "puts { **** HW sysnthesis is Disabled **** }\n"
                                    "}\n"
                                    "exit")
            execute_process(COMMAND echo "${tcl_script}" OUTPUT_FILE ${_basename}_${kernel}.tcl)
            execute_process(COMMAND vivado_hls -f ${_basename}_${kernel}.tcl)
        endforeach()
    else()
        #TODO
        # Building Device Support Archive (DSA)
        # Integrating IP and running sds++ by vivado TCL
        string(CONCAT tcl_script "puts {HELLO SOC}\n"
                                "exit")
        list(GET kernels 0 kernel)
        execute_process(COMMAND echo "${tcl_script}" OUTPUT_FILE ${_basename}_${kernel}.tcl)
        execute_process(COMMAND vivado -mode batch -notrace -source ${_basename}_${kernel}.tcl)
    endif()
endif()
