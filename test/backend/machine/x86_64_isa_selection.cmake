# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_isa name)
    execute_process(
        COMMAND "${CC}" -S -O2 ${ARGN} "${SOURCE}"
                -o "${OUTPUT}.${name}.s"
        RESULT_VARIABLE assembly_status
        OUTPUT_VARIABLE assembly_stdout
        ERROR_VARIABLE assembly_stderr
    )
    if(NOT assembly_status EQUAL 0)
        message(FATAL_ERROR
            "${name} ISA selection failed\n${assembly_stdout}\n${assembly_stderr}")
    endif()
    execute_process(
        COMMAND "${CC}" -c -O2 ${ARGN} "${SOURCE}"
                -o "${OUTPUT}.${name}.o"
        RESULT_VARIABLE object_status
        OUTPUT_VARIABLE object_stdout
        ERROR_VARIABLE object_stderr
    )
    if(NOT object_status EQUAL 0)
        message(FATAL_ERROR
            "${name} ISA object failed\n${object_stdout}\n${object_stderr}")
    endif()
    file(READ "${OUTPUT}.${name}.s" ${name})
    set(${name} "${${name}}" PARENT_SCOPE)
endfunction()

compile_isa(base -ffast-math -mno-bmi2 -mno-fma)
compile_isa(bmi2 -mbmi2)
compile_isa(fma -ffast-math -mfma)
compile_isa(contract -ffp-contract=fast -mfma)
compile_isa(no_contract -ffast-math -ffp-contract=off -mfma)
compile_isa(strict -mfma)
compile_isa(haswell -ffast-math -march=haswell)

if(NOT base MATCHES "shlq[\t ]+%cl, %rax" OR
   base MATCHES "(shlx|sarx)q" OR
   base MATCHES "vfmadd")
    message(FATAL_ERROR "baseline ISA selection is not conservative\n${base}")
endif()
if(NOT bmi2 MATCHES "shlxq[^\n]*, %rax" OR
   NOT bmi2 MATCHES "sarxq[^\n]*, %rax")
    message(FATAL_ERROR "BMI2 scalar shifts were not selected\n${bmi2}")
endif()
if(NOT fma MATCHES "vfmadd132sd[\t ]+%xmm2, %xmm1, %xmm0")
    message(FATAL_ERROR "FMA contraction was not selected\n${fma}")
endif()
if(NOT contract MATCHES "vfmadd132sd[\t ]+%xmm2, %xmm1, %xmm0")
    message(FATAL_ERROR
        "explicit fast contraction did not select FMA\n${contract}")
endif()
if(no_contract MATCHES "vfmadd")
    message(FATAL_ERROR
        "explicit disabled contraction was ignored\n${no_contract}")
endif()
if(strict MATCHES "vfmadd" OR
   NOT strict MATCHES "vmulsd[^\n]*\n[\t ]*vaddsd[^\n]*, %xmm0")
    message(FATAL_ERROR "strict floating selection contracted an operation\n${strict}")
endif()
if(NOT haswell MATCHES "shlxq" OR NOT haswell MATCHES "vfmadd132sd")
    message(FATAL_ERROR "Haswell baseline did not reach ISA selection\n${haswell}")
endif()
