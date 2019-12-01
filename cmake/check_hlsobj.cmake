# ${_basename}.o + ${_basename}_hls.o -> ${_basename}.o
if(EXISTS "${_basename}.hls" AND EXISTS "./anydsl_fpga/")
    execute_process(COMMAND awk -F "[ (]+" "/void .*;/{ ORS=\";\"; print $2 }" ${_basename}.hls
                    COMMAND awk "{ ORS=\"\"; sub(/;$/,\"\"); print }" OUTPUT_VARIABLE kernels)
    # all kernels are contained in each library; take the last one
    list(GET kernels -1 kernel)
    execute_process(COMMAND cp ${_objfile} ${_objfile}.orig)
    execute_process(COMMAND ld -r ${_objfile}.orig anydsl_fpga/${_basename}_${kernel}/csim/build/obj/${_basename}_hls.o -o ${_objfile})
endif()
