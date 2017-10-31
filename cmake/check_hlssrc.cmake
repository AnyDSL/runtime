# ${_basename}.hls -> ${_basename}_hls.cpp - > ${_basename}_hls.o
if(EXISTS ${_basename}.hls)
    execute_process(COMMAND cp ${_basename}.hls ${_basename}_hls.cpp)
    execute_process(COMMAND echo "int main() { return 0; }" OUTPUT_FILE ${_basename}_tb.cpp)
    execute_process(COMMAND awk -F "[ (]+" "/void .*;/{ print $2 }" ${_basename}.hls
                    COMMAND tr "\n" " " OUTPUT_VARIABLE top_funs)
    string(CONCAT TCL_SCRIPT "open_project anydsl_fpga\n"
                             "set_top ${top_funs}\n"
                             "add_files ${_basename}_hls.cpp\n"
                             "add_files -tb ${_basename}_tb.cpp\n"
                             "open_solution vivado_hls\n"
                             "set_part {xc7k70tfbv676-2}\n"
                             "create_clock -period 10.0 -name default\n"
                             "csim_design -ldflags {-lrt} -clean\n"
                             "#csynth_design\n"
                             "#export_design -format ip_catalog -evaluate verilog\n"
                             "exit")
    execute_process(COMMAND echo "${TCL_SCRIPT}" OUTPUT_FILE ${_basename}.tcl)
    execute_process(COMMAND vivado_hls -f ${_basename}.tcl)
endif()
