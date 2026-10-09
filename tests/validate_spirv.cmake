# Compiles INPUT to SPIR-V with fluxc, then runs spirv-val on the result.
execute_process(COMMAND ${FLUXC} ${INPUT} ${FLAG} --emit spirv -o ${OUTPUT} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "fluxc failed on ${INPUT}")
endif()
execute_process(COMMAND ${SPIRV_VAL} --target-env vulkan1.0 ${OUTPUT} RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "spirv-val rejected ${OUTPUT}")
endif()
