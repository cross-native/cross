# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_assembly flags suffix variable)
    separate_arguments(arguments NATIVE_COMMAND "${flags}")
    execute_process(
        COMMAND "${CC}" -S -O2 -fno-eval-calls ${arguments}
                -target x86_64-unknown-linux-gnu "${SOURCE}"
                -o "${OUTPUT}.${suffix}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "${suffix} if-conversion compilation failed\n"
            "${compile_stdout}\n${compile_stderr}")
    endif()
    file(READ "${OUTPUT}.${suffix}.s" assembly)
    set(${variable} "${assembly}" PARENT_SCOPE)
endfunction()

function(extract_function assembly symbol variable)
    string(FIND "${assembly}" "${symbol}:" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "could not find assembly body for ${symbol}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 tail)
    string(FIND "${tail}" ".size ${symbol}," length)
    if(length EQUAL -1)
        string(FIND "${tail}" ".seh_endproc" length)
    endif()
    if(length EQUAL -1)
        message(FATAL_ERROR "could not find assembly end for ${symbol}")
    endif()
    string(SUBSTRING "${tail}" 0 ${length} body)
    set(${variable} "${body}" PARENT_SCOPE)
endfunction()

compile_assembly("-fno-if-conversion" branch strict_assembly)
compile_assembly("-fif-conversion" select selected_assembly)
compile_assembly("-fif-conversion -fno-peephole2"
                 no-peephole no_peephole_assembly)
compile_assembly("-O3 -fif-conversion" scheduled scheduled_assembly)
compile_assembly("-O3 -fif-conversion -fno-peephole2"
                 scheduled-no-peephole scheduled_no_peephole_assembly)
execute_process(
    COMMAND "${CC}" -S -Oz -fif-conversion -fno-eval-calls
            -target x86_64-unknown-linux-gnu "${SOURCE}"
            -o "${OUTPUT}.oz.s"
    RESULT_VARIABLE oz_status
    OUTPUT_VARIABLE oz_stdout
    ERROR_VARIABLE oz_stderr
)
if(NOT oz_status EQUAL 0)
    message(FATAL_ERROR
        "Oz if-conversion compilation failed\n${oz_stdout}\n${oz_stderr}")
endif()
file(READ "${OUTPUT}.oz.s" oz_assembly)
foreach(symbol ifconv_compare ifconv_return ifconv_small)
    extract_function("${strict_assembly}" "${symbol}" "strict_${symbol}")
    extract_function("${selected_assembly}" "${symbol}" "selected_${symbol}")
    if(NOT strict_${symbol} MATCHES "[\t ]j[a-z]+[\t ]")
        message(FATAL_ERROR
            "disabled if-conversion removed ${symbol}'s branch\n"
            "${strict_${symbol}}")
    endif()
    if(NOT selected_${symbol} MATCHES "[\t ]cmov[a-z]+[\t ]" OR
       selected_${symbol} MATCHES "[\t ]j[a-z]+[\t ]")
        message(FATAL_ERROR
            "enabled if-conversion did not make ${symbol} branchless\n"
            "${selected_${symbol}}")
    endif()
endforeach()
extract_function("${selected_assembly}" ifconv_expensive selected_expensive)
extract_function("${selected_assembly}" ifconv_sign_boundary selected_sign)
if(NOT selected_sign MATCHES "testq" OR
   NOT selected_sign MATCHES "cmov(l|ge)q" OR
   selected_sign MATCHES "-9223372036854775808")
    message(FATAL_ERROR
        "unsigned sign-boundary select was not canonicalized to TEST/cmov\n"
        "${selected_sign}")
endif()
if(NOT selected_expensive MATCHES "[\t ]j[a-z]+[\t ]" OR
   selected_expensive MATCHES "[\t ]cmov[a-z]+[\t ]")
    message(FATAL_ERROR
        "costly if-conversion diamond was not kept branchy\n"
        "${selected_expensive}")
endif()
extract_function("${selected_assembly}" ifconv_masked_load selected_masked)
extract_function("${no_peephole_assembly}" ifconv_masked_load
                 no_peephole_masked)
extract_function("${oz_assembly}" ifconv_masked_load oz_masked)
extract_function("${scheduled_assembly}" ifconv_masked_load scheduled_masked)
extract_function("${scheduled_no_peephole_assembly}" ifconv_masked_load
                 scheduled_no_peephole_masked)
if(NOT selected_masked MATCHES "[\t ]cmov[a-z]+[\t ]" OR
   selected_masked MATCHES "[\t ]j[a-z]+[\t ]" OR
   NOT selected_masked MATCHES "[\t ]testb[\t ]+[$]1" OR
   selected_masked MATCHES "[\t ]and[qwlb][\t ]+[$]1")
    message(FATAL_ERROR
        "loaded low-bit condition was not converted to a select\n"
        "${selected_masked}")
endif()
if(NOT selected_masked MATCHES "leaq[\t ]+1" OR
   selected_masked MATCHES "addq[\t ]+[$]1" OR
   NOT no_peephole_masked MATCHES "addq[\t ]+[$]1")
    message(FATAL_ERROR
        "-fpeephole2 did not fuse add-plus-one into an addressed LEA\n"
        "${selected_masked}\n${no_peephole_masked}")
endif()
if(NOT scheduled_masked MATCHES
       "xorq[^\n]*\n[\t ]+testb[\t ]+[$]1[^\n]*\n[\t ]+leaq[^\n]*\n[\t ]+leaq[\t ]+1" OR
   scheduled_masked MATCHES "addq[\t ]+[$]1")
    message(FATAL_ERROR
        "O3 masked select did not schedule TEST ahead of its "
        "flag-preserving LEA chain\n${scheduled_masked}")
endif()
if(NOT scheduled_no_peephole_masked MATCHES
       "addq[\t ]+[$]1[^\n]*\n[\t ]+testb[\t ]+[$]1")
    message(FATAL_ERROR
        "-fno-peephole2 moved TEST across a flag-clobbering add\n"
        "${scheduled_no_peephole_masked}")
endif()
if(NOT oz_masked MATCHES "[\t ]cmov[a-z]+[\t ]" OR
   oz_masked MATCHES "[\t ]j[a-z]+[\t ]")
    message(FATAL_ERROR
        "minimum-size mode did not convert an unpredictable masked diamond\n"
        "${oz_masked}")
endif()
if(selected_ifconv_compare MATCHES "[\t ]set[a-z]+[\t ]" OR
   selected_ifconv_return MATCHES "[\t ]set[a-z]+[\t ]")
    message(FATAL_ERROR
        "comparison select retained a redundant setcc\n"
        "${selected_ifconv_compare}\n${selected_ifconv_return}")
endif()
