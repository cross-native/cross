# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT HOST_CXX)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -O2 -felide-noreturn-saves -fno-unwind-tables
            -fno-asynchronous-unwind-tables "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE result OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT result EQUAL 0)
    message(FATAL_ERROR "noreturn save-elision compile failed (${result})\n${stdout}\n${stderr}")
endif()
file(READ "${OUTPUT}.s" assembly)
if(assembly MATCHES "(memcpy|memset|__chkstk|__main)")
    message(FATAL_ERROR "noreturn test introduced a hidden runtime reference")
endif()
if(NOT assembly MATCHES "never:")
    message(FATAL_ERROR "noreturn test is missing its function")
endif()
set(save "(pushq[\t ]+%(rbx|r12|r13|r14|r15)|movq[\t ]+%(rbx|r12|r13|r14|r15),[\t ]*[0-9]+[(]%rsp[)])")
if(assembly MATCHES "${save}")
    message(FATAL_ERROR "eligible noreturn function still saves a preserved GPR")
endif()

execute_process(
    COMMAND "${CC}" -S -O2 -fno-elide-noreturn-saves -fno-unwind-tables
            -fno-asynchronous-unwind-tables "${SOURCE}" -o "${OUTPUT}.baseline.s"
    RESULT_VARIABLE baseline_result OUTPUT_VARIABLE baseline_stdout
            ERROR_VARIABLE baseline_stderr)
if(NOT baseline_result EQUAL 0)
    message(FATAL_ERROR "noreturn baseline compile failed")
endif()
file(READ "${OUTPUT}.baseline.s" baseline)
if(NOT baseline MATCHES "never:")
    message(FATAL_ERROR "noreturn baseline is missing its function")
endif()
if(NOT baseline MATCHES "${save}")
    message(FATAL_ERROR "control build did not retain a preserved GPR save")
endif()

execute_process(
    COMMAND "${CC}" -S -O2 -felide-noreturn-saves -funwind-model=platform
            -fno-unwind-tables -fno-asynchronous-unwind-tables "${SOURCE}"
            -o "${OUTPUT}.platform.s"
    RESULT_VARIABLE platform_result)
if(NOT platform_result EQUAL 0)
    message(FATAL_ERROR "platform unwind-model compile failed")
endif()
file(READ "${OUTPUT}.platform.s" platform)
if(NOT platform MATCHES "${save}")
    message(FATAL_ERROR "platform unwind model incorrectly elided preserved save")
endif()

execute_process(
    COMMAND "${CC}" -S -O2 -felide-noreturn-saves -funwind-model=none
            -funwind-tables "${SOURCE}" -o "${OUTPUT}.metadata.s"
    RESULT_VARIABLE metadata_result)
if(NOT metadata_result EQUAL 0)
    message(FATAL_ERROR "unwind metadata compile failed")
endif()
file(READ "${OUTPUT}.metadata.s" metadata)
if(NOT metadata MATCHES "${save}")
    message(FATAL_ERROR "unwind metadata incorrectly elided preserved save")
endif()
foreach(mode elide-noreturn-saves no-elide-noreturn-saves)
  foreach(fp omit-frame-pointer no-omit-frame-pointer)
    set(run_output "${OUTPUT}-${mode}-${fp}")
    execute_process(COMMAND "${CC}" -O2 -f${mode} -f${fp} -c "${SOURCE}"
        -o "${run_output}.o" RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "noreturn runtime compilation failed")
    endif()
    execute_process(COMMAND "${HOST_CXX}" "${CMAKE_CURRENT_LIST_DIR}/driver.cpp"
        "${run_output}.o" -o "${run_output}.exe" RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "noreturn runtime linking failed")
    endif()
    execute_process(COMMAND "${run_output}.exe" RESULT_VARIABLE status TIMEOUT 10)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "noreturn runtime lost a call-live value")
    endif()
    execute_process(COMMAND "${CC}" -O2 -f${mode} -f${fp} -S "${SOURCE}"
        -o "${run_output}.s" RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "noreturn frame-pointer assembly failed")
    endif()
    file(READ "${run_output}.s" assembly)
    if(fp STREQUAL no-omit-frame-pointer)
        if(NOT assembly MATCHES "movq[\t ]+%rsp,[\t ]*%rbp")
            message(FATAL_ERROR "required working frame pointer was removed")
        endif()
        if(mode STREQUAL elide-noreturn-saves AND assembly MATCHES "pushq[\t ]+%rbp")
            message(FATAL_ERROR "noreturn retained the unobservable incoming RBP save")
        elseif(mode STREQUAL no-elide-noreturn-saves AND NOT assembly MATCHES "pushq[\t ]+%rbp")
            message(FATAL_ERROR "disabled elision lost the incoming RBP save")
        endif()
    endif()
  endforeach()
endforeach()
