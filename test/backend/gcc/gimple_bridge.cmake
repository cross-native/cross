if(NOT DEFINED CC OR NOT DEFINED GCC OR NOT DEFINED SOURCE OR
   NOT DEFINED HARNESS OR NOT DEFINED OUTPUT)
    message(FATAL_ERROR "CC, GCC, SOURCE, HARNESS, and OUTPUT are required")
endif()

foreach(mode gimple rtl)
    if(mode STREQUAL "rtl")
        set(emit "-emit-gimple=rtl")
    else()
        set(emit "-emit-gimple")
    endif()
    set(gimple_source "${OUTPUT}.${mode}.gimple.c")
    set(object "${OUTPUT}.${mode}.o")
    set(harness_object "${OUTPUT}.${mode}.harness.o")
    set(executable "${OUTPUT}.${mode}.exe")
    execute_process(
        COMMAND "${CC}" "${emit}" -O3 -march=x86-64-v3
                "${SOURCE}" -o "${gimple_source}"
        RESULT_VARIABLE cross_result
        ERROR_VARIABLE cross_error)
    if(NOT cross_result EQUAL 0)
        message(FATAL_ERROR
                "Cross ${mode} serialization failed:\n${cross_error}")
    endif()
    file(READ "${gimple_source}" text)
    if(mode STREQUAL "rtl")
        if(NOT text MATCHES "startwith\\(\"optimized\"\\)")
            message(FATAL_ERROR "RTL bridge omitted its GCC start pass")
        endif()
    elseif(text MATCHES "startwith")
        message(FATAL_ERROR "GIMPLE bridge unexpectedly skips tree passes")
    endif()
    if(NOT text MATCHES "cross_gimple_fdiv_")
        message(FATAL_ERROR "floating division grammar shim was not emitted")
    endif()
    if(NOT text MATCHES "cross_gimple_signed_v")
        message(FATAL_ERROR "signed vector operation domain was not emitted")
    endif()
    if(NOT text MATCHES "cross_gimple_rotl_[0-9]+" OR
       NOT text MATCHES "gimple_bridge_rotate")
        message(FATAL_ERROR "MIR rotate helper was not emitted")
    endif()
    execute_process(
        COMMAND "${GCC}" -c -O3 -fgimple -march=x86-64-v3
                -ffreestanding -fno-builtin -fno-stack-protector
                -fno-unwind-tables -fno-asynchronous-unwind-tables
                "${gimple_source}" -o "${object}"
        RESULT_VARIABLE gcc_result
        ERROR_VARIABLE gcc_error)
    if(NOT gcc_result EQUAL 0)
        message(FATAL_ERROR
                "GCC rejected Cross ${mode} GIMPLE:\n${gcc_error}")
    endif()
    execute_process(
        COMMAND "${GCC}" -c -O2 "${HARNESS}" -o "${harness_object}"
        RESULT_VARIABLE harness_result
        ERROR_VARIABLE harness_error)
    if(NOT harness_result EQUAL 0)
        message(FATAL_ERROR
                "GCC rejected the ${mode} bridge harness:\n${harness_error}")
    endif()
    execute_process(
        COMMAND "${GCC}" "${object}" "${harness_object}" -o "${executable}"
        RESULT_VARIABLE link_result
        ERROR_VARIABLE link_error)
    if(NOT link_result EQUAL 0)
        message(FATAL_ERROR
                "GCC could not link the ${mode} bridge test:\n${link_error}")
    endif()
    execute_process(
        COMMAND "${executable}"
        RESULT_VARIABLE run_result)
    if(NOT run_result EQUAL 0)
        message(FATAL_ERROR
                "Cross ${mode} GIMPLE result check failed (${run_result})")
    endif()
endforeach()
