# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

set(floating_bits_regex
    "movabsq[\t ]+[$](4593671619917905920|4602678819172646912|4604930618986332160|4608308318706860032)")

foreach(level O2 O3 Os Oz)
    execute_process(
        COMMAND "${CC}" -S "-${level}" -funwind-tables -mavx2 -target
                x86_64-w64-windows-gnu "${SOURCE}"
                -o "${OUTPUT}-${level}.s"
        RESULT_VARIABLE assembly_status
        OUTPUT_VARIABLE assembly_stdout
        ERROR_VARIABLE assembly_stderr
    )
    execute_process(
        COMMAND "${CC}" -c "-${level}" -funwind-tables -mavx2 -target
                x86_64-w64-windows-gnu "${SOURCE}"
                -o "${OUTPUT}-${level}.o"
        RESULT_VARIABLE object_status
        OUTPUT_VARIABLE stdout
        ERROR_VARIABLE stderr
    )
    if(NOT assembly_status EQUAL 0 OR NOT object_status EQUAL 0)
        message(FATAL_ERROR
            "floating reinterpret allocation failed at ${level}\n"
            "${assembly_stdout}\n${assembly_stderr}\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}-${level}.s" assembly)
    if(NOT assembly MATCHES "[.]Lcross[.]machine[.][0-9_]+[.]float[.][0-9]+" OR
       assembly MATCHES "${floating_bits_regex}")
        message(FATAL_ERROR
            "${level} did not pool scalar floating literals\n${assembly}")
    endif()
    if(NOT assembly MATCHES
           "vbroadcastsd[\t ]+[.]Lcross[.]machine[.][0-9_]+[.]float[.][0-9]+[(]%rip[)]")
        message(FATAL_ERROR
            "${level} did not select a memory floating broadcast\n${assembly}")
    endif()
    if((level STREQUAL "Os" OR level STREQUAL "Oz") AND
       assembly MATCHES "[.]seh_savexmm %xmm(6|7|8)")
        message(FATAL_ERROR
            "${level} retained a nonvolatile SIMD save for pooled constants\n"
            "${assembly}")
    endif()
    if(level STREQUAL "Oz")
        string(FIND "${assembly}" "floating_literal_polynomial:" polynomial_start)
        if(polynomial_start EQUAL -1)
            message(FATAL_ERROR "Oz polynomial function is missing\n${assembly}")
        endif()
        string(SUBSTRING "${assembly}" ${polynomial_start} -1 polynomial)
        string(FIND "${polynomial}" ".seh_endproc" polynomial_length)
        if(polynomial_length EQUAL -1)
            message(FATAL_ERROR "Oz polynomial terminator is missing\n${polynomial}")
        endif()
        string(SUBSTRING "${polynomial}" 0 ${polynomial_length} polynomial)
        string(REGEX MATCHALL
            "movsd[\t ]+[.]Lcross[.]machine[.][0-9_]+[.]float[.][0-9]+[(]%rip[)],[\t ]+%xmm[0-5]"
            cached_literals "${polynomial}")
        list(LENGTH cached_literals cached_literal_count)
        if(cached_literal_count LESS 2)
            message(FATAL_ERROR
                "Oz did not cache spare volatile floating literals\n${polynomial}")
        endif()
        if(NOT polynomial MATCHES
           "cmpq[^\r\n]*\r?\n[\t ]+j(ae|e)[^\r\n]*\r?\n[.]Lcross[.]machine[^:]*:\r?\n[\t ]+movsd")
            message(FATAL_ERROR
                "nested polynomial loop body is not the fallthrough edge\n"
                "${polynomial}")
        endif()
    endif()
endforeach()
