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
            "${suffix} SLP compilation failed\n"
            "${compile_stdout}\n${compile_stderr}")
    endif()
    file(READ "${OUTPUT}.${suffix}.s" assembly)
    set(${variable} "${assembly}" PARENT_SCOPE)
endfunction()

compile_assembly("-fno-tree-slp-vectorize" scalar scalar_assembly)
compile_assembly("-ftree-slp-vectorize" vector vector_assembly)
if(scalar_assembly MATCHES "[	 ]divpd[	 ]" OR
   NOT vector_assembly MATCHES "[	 ]divpd[	 ]")
    message(FATAL_ERROR
        "-ftree-slp-vectorize did not form packed f64 arithmetic\n"
        "${vector_assembly}")
endif()
string(REGEX MATCHALL "[	 ]divsd[	 ]" scalar_divides
       "${scalar_assembly}")
string(REGEX MATCHALL "[	 ]divsd[	 ]" vector_divides
       "${vector_assembly}")
list(LENGTH scalar_divides scalar_divide_count)
list(LENGTH vector_divides vector_divide_count)
if(NOT vector_divide_count LESS scalar_divide_count)
    message(FATAL_ERROR
        "SLP did not reduce scalar divides "
        "(${vector_divide_count} versus ${scalar_divide_count})")
endif()
