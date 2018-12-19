# ${_basename}.hls -> ${_basename}_hls.cpp - > ${_basename}_hls.o
if(EXISTS ${_basename}.hls)
    execute_process(COMMAND cp ${_basename}.hls ${_basename}_hls.cpp)
    execute_process(COMMAND echo "int main() { return 0; }" OUTPUT_FILE ${_basename}_tb.cpp)
    execute_process(COMMAND awk -F "[ (]+" "/void .*;/{ ORS=\";\"; print $2 }" ${_basename}.hls
                    COMMAND awk "{ ORS=\"\"; sub(/;$/,\"\"); print }" OUTPUT_VARIABLE kernels)
    foreach(kernel ${kernels})
            string(CONCAT tcl_script "open_project anydsl_fpga\n"
                                     "set_top ${kernel}\n"
                                     "add_files ${_basename}_hls.cpp\n"
                                     "add_files -tb ${_basename}_tb.cpp\n"
                                     "open_solution ${_basename}_${kernel}\n"
                                     "set_part {xc7z020clg484-1}\n"
                                     "create_clock -period 10 -name default\n"
                                     "csim_design -compiler clang -ldflags {-lrt} -clean\n"
                                     "set lower ${SYNTHESIS}\n"
                                     "if [string match {on} ${SYNTHESIS}] {\n"
                                     "puts { **** HW synthesis is Enabled **** }\n"
                                     "csynth_design\n"
                                     "export_design -format ip_catalog -evaluate verilog\n"
                                     "} else {\n"
                                     "puts { **** HW sysnthesis is Disabled **** }\n"
                                     "}\n"
                                     "exit")

            execute_process(COMMAND echo "${tcl_script}" OUTPUT_FILE ${_basename}_${kernel}.tcl)
            execute_process(COMMAND vivado_hls -f ${_basename}_${kernel}.tcl)
    endforeach()
endif()
