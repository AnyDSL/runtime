# ${_basename}.o + ${_basename}_hls.o -> ${_basename}.o
if(EXISTS ${_basename}.hls)
    execute_process(COMMAND awk -F "[ (]+" "/void .*;/{ print $2 }" ${_basename}.hls
                    COMMAND tr "\n" " " OUTPUT_VARIABLE top_funs)
    execute_process(COMMAND cp ${_objfile} ${_objfile}.orig)
    execute_process(COMMAND ld -r ${_objfile}.orig anydsl_fpga/vivado_hls/csim/build/obj/${_basename}_hls.o -o ${_objfile})
endif()
