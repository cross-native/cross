# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile_assembly level variable)
    execute_process(
        COMMAND "${CC}" -S "-O${level}" -fno-eval-calls
                -target x86_64-unknown-linux-gnu -mabi=sysv_abi "${SOURCE}"
                -o "${OUTPUT}.O${level}.s"
        RESULT_VARIABLE status
        OUTPUT_VARIABLE compile_stdout
        ERROR_VARIABLE compile_stderr
    )
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "-O${level} register-allocation compilation failed\n"
            "${compile_stdout}\n${compile_stderr}")
    endif()
    file(READ "${OUTPUT}.O${level}.s" assembly)
    set(${variable} "${assembly}" PARENT_SCOPE)
endfunction()

compile_assembly(0 o0_assembly)
compile_assembly(2 o2_assembly)

if(NOT o2_assembly MATCHES "%r(8|9)")
    message(FATAL_ERROR
        "-O2 did not assign a leaf integer temporary to r8/r9\n${o2_assembly}")
endif()

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

extract_function("${o2_assembly}" allocated_caller o2_caller)
if(NOT o2_caller MATCHES "[\t ]call[q]?[^\r\n]*allocation_callee" OR
   NOT o2_caller MATCHES "%r(8|9|di|si)")
    message(FATAL_ERROR
        "-O2 did not keep a call-local integer value in a caller-retained register\n${o2_caller}")
endif()

extract_function("${o2_assembly}" allocated_pressure o2_pressure)
if(NOT o2_pressure MATCHES "%r(8|9|10|11)")
    message(FATAL_ERROR
        "-O2 did not use the extended integer palette under pressure\n${o2_pressure}")
endif()

extract_function("${o2_assembly}" allocated_split o2_split)
if(NOT o2_split MATCHES "[\t ]call[q]?[^\r\n]*allocation_callee" OR
   NOT o2_split MATCHES "%r(11|bx)")
    message(FATAL_ERROR
        "-O2 did not keep a call-live value in a register allowed by the exact callee contract\n${o2_split}")
endif()

extract_function("${o2_assembly}" allocated_vla_split o2_vla_split)
if(NOT o2_vla_split MATCHES "[\t ]call[q]?[^\r\n]*allocation_callee" OR
   NOT o2_vla_split MATCHES "movq[\t ]+%rbx,[\t ]+-?[0-9]+\\(%rbp\\)" OR
   NOT o2_vla_split MATCHES "movq[\t ]+-?[0-9]+\\(%rbp\\),[\t ]+%rbx" OR
   NOT o2_vla_split MATCHES "[.]cfi_offset %rbx")
    message(FATAL_ERROR
        "-O2 did not preserve a call-live value in a VLA frame\n${o2_vla_split}")
endif()

extract_function("${o2_assembly}" allocated_aligned_split o2_aligned_split)
if(NOT o2_aligned_split MATCHES "[\t ]call[q]?[^\r\n]*allocation_callee" OR
   NOT o2_aligned_split MATCHES "andq[\t ]+\\$-64,[\t ]+%rsp")
    message(FATAL_ERROR
        "-O2 did not preserve the realigned call frame contract\n"
        "${o2_aligned_split}")
endif()

extract_function("${o2_assembly}" allocated_aligned_float_split o2_aligned_float)
if(NOT o2_aligned_float MATCHES "movdqu[\t ]+%xmm6,[\t ]+-[0-9]+\\(%rbp\\)" OR
   NOT o2_aligned_float MATCHES "movdqu[\t ]+-[0-9]+\\(%rbp\\),[\t ]+%xmm6" OR
   NOT o2_aligned_float MATCHES "[.]cfi_offset %xmm6")
    message(FATAL_ERROR
        "realigned fixed-CFA SIMD allocation is missing\n${o2_aligned_float}")
endif()

extract_function("${o2_assembly}" allocated_float o2_float)
if(NOT o2_float MATCHES "%xmm(3|4|5)")
    message(FATAL_ERROR
        "-O2 did not allocate scalar floating values\n${o2_float}")
endif()

extract_function("${o2_assembly}" allocated_fptr o2_fptr)
if(NOT o2_fptr MATCHES "%xmm(3|4|5)")
    message(FATAL_ERROR
        "-O2 did not preserve fptr's floating-register class\n${o2_fptr}")
endif()

extract_function("${o2_assembly}" allocated_expect o2_expect)
if(o2_expect MATCHES
       "movq[\t ]+%r(8|9|10|11|di|si),[\t ]+%rax[\r\n\t ]+movq[\t ]+%rax,[\t ]+%r(8|9|10|11|di|si)")
    message(FATAL_ERROR
        "copy affinity did not coalesce an expect value\n${o2_expect}")
endif()

# On a target where RCX is an ordinary allocatable color, variable rotate
# counts should be formed directly in RCX/CL. The shared first count remains
# live to derive the second lane, but the remaining four-way group must not
# introduce a late move after each count addition.
execute_process(
    COMMAND "${CC}" -S -O3 -fno-eval-calls -march=x86-64-v3
            -target x86_64-unknown-linux-gnu -mabi=sysv_abi "${SOURCE}"
            -o "${OUTPUT}.v3.O3.s"
    RESULT_VARIABLE v3_status
    OUTPUT_VARIABLE v3_stdout
    ERROR_VARIABLE v3_stderr
)
if(NOT v3_status EQUAL 0)
    message(FATAL_ERROR
        "x86-64-v3 register-allocation compilation failed\n"
        "${v3_stdout}\n${v3_stderr}")
endif()
file(READ "${OUTPUT}.v3.O3.s" v3_assembly)
extract_function("${v3_assembly}" allocated_rotate v3_rotate)
string(REGEX MATCHALL "rolq[\t ]+%cl" variable_rotates "${v3_rotate}")
list(LENGTH variable_rotates variable_rotate_count)
if(NOT variable_rotate_count EQUAL 5 OR
   NOT v3_rotate MATCHES "andl[\t ]+[$]28" OR
   NOT v3_rotate MATCHES "leaq[\t ]+3\\([^\n]+\\),[\t ]+%rcx" OR
   v3_rotate MATCHES
       "addq[\t ]+[$]1,[\t ]+%r(8|9|10|11|dx|si|di)[\r\n]+[\t ]*movq[^\n]+,[\t ]+%rcx")
    message(FATAL_ERROR
        "variable rotate counts were not allocated to RCX before use\n"
        "${v3_rotate}")
endif()

execute_process(
    COMMAND "${CC}" -S -O2 -fno-eval-calls
            -target x86_64-w64-windows-gnu -mabi=ms_abi "${SOURCE}"
            -o "${OUTPUT}.win.O2.s"
    RESULT_VARIABLE win_assembly_status
    OUTPUT_VARIABLE win_assembly_stdout
    ERROR_VARIABLE win_assembly_stderr
)
execute_process(
    COMMAND "${CC}" -c -O2 -fno-eval-calls
            -target x86_64-w64-windows-gnu -mabi=ms_abi "${SOURCE}"
            -o "${OUTPUT}.win.O2.o"
    RESULT_VARIABLE win_object_status
    OUTPUT_VARIABLE win_object_stdout
    ERROR_VARIABLE win_object_stderr
)
if(NOT win_assembly_status EQUAL 0 OR NOT win_object_status EQUAL 0)
    message(FATAL_ERROR
        "Win64 preserved allocation failed\n${win_assembly_stdout}\n"
        "${win_assembly_stderr}\n${win_object_stdout}\n${win_object_stderr}")
endif()
file(READ "${OUTPUT}.win.O2.s" win_assembly)
extract_function("${win_assembly}" allocated_pressure win_pressure)
if(win_pressure MATCHES "[.]seh_pushreg")
    if(NOT win_pressure MATCHES "[.]seh_pushreg %rbx" OR
       NOT win_pressure MATCHES "pushq[\t ]+%rbx" OR
       NOT win_pressure MATCHES "popq[\t ]+%rbx" OR
       win_pressure MATCHES "[.]seh_savereg")
        message(FATAL_ERROR
            "Win64 leaf GPR allocation did not use compact unwindable saves\n"
            "${win_pressure}")
    endif()
elseif(NOT win_pressure MATCHES "%r(10|11)" OR
       win_pressure MATCHES "[.]seh_savereg")
    message(FATAL_ERROR
        "Win64 leaf GPR allocation neither avoided nor compactly encoded preserved saves\n"
        "${win_pressure}")
endif()
extract_function("${win_assembly}" allocated_split win_split)
if(NOT win_split MATCHES "[\t ]call[q]?[^\r\n]*allocation_callee")
    message(FATAL_ERROR
        "Win64 call-live allocation test lost its call\n${win_split}")
elseif(win_split MATCHES "[.]seh_savereg %rbx")
    if(NOT win_split MATCHES "movq[\t ]+%rbx,[\t ]+[0-9]+\\(%rsp\\)" OR
       NOT win_split MATCHES "movq[\t ]+[0-9]+\\(%rsp\\),[\t ]+%rbx")
        message(FATAL_ERROR
            "Win64 preserved GPR save/unwind is incomplete\n${win_split}")
    endif()
elseif(NOT win_split MATCHES "%r11")
    message(FATAL_ERROR
        "Win64 call-live value used neither an exact-contract register nor an unwindable save\n${win_split}")
endif()
extract_function("${win_assembly}" allocated_float_split win_float_split)
if(NOT win_float_split MATCHES
       "movsd[\t ]+%xmm[0-5],[\t ]+[0-9]+\\(%rsp\\)" OR
   NOT win_float_split MATCHES
       "movsd[\t ]+[0-9]+\\(%rsp\\),[\t ]+%xmm[0-5]")
    message(FATAL_ERROR
        "Win64 call-local SIMD split spill is missing\n${win_float_split}")
endif()
extract_function("${win_assembly}" allocated_vla_split win_vla_split)
if(NOT win_vla_split MATCHES "[.]seh_savereg %rbx" OR
   NOT win_vla_split MATCHES "movq[\t ]+%rbx,[\t ]+-?[0-9]+\\(%rbp\\)" OR
   NOT win_vla_split MATCHES
       "movq[\t ]+-?[0-9]+\\(%rbp\\),[\t ]+%rbx")
    message(FATAL_ERROR
        "Win64 VLA preserved-register unwind is missing\n${win_vla_split}")
endif()
extract_function("${win_assembly}" allocated_aligned_split win_aligned_split)
if(NOT win_aligned_split MATCHES "[.]seh_savereg %rbx" OR
   NOT win_aligned_split MATCHES "[.]seh_setframe %rbp, [0-9]+" OR
   NOT win_aligned_split MATCHES "movq[\t ]+%rbx,[\t ]+-[0-9]+\\(%rbp\\)" OR
   NOT win_aligned_split MATCHES
       "movq[\t ]+-[0-9]+\\(%rbp\\),[\t ]+%rbx")
    message(FATAL_ERROR
        "Win64 realigned fixed-CFA save/unwind is missing\n${win_aligned_split}")
endif()
extract_function("${win_assembly}" allocated_aligned_float_split win_aligned_float)
if(NOT win_aligned_float MATCHES "[.]seh_savexmm %xmm6" OR
   NOT win_aligned_float MATCHES
       "movdqu[\t ]+%xmm6,[\t ]+-[0-9]+\\(%rbp\\)" OR
   NOT win_aligned_float MATCHES
       "movdqu[\t ]+-[0-9]+\\(%rbp\\),[\t ]+%xmm6")
    message(FATAL_ERROR
        "Win64 realigned fixed-CFA SIMD unwind is missing\n${win_aligned_float}")
endif()

string(REGEX MATCHALL "[-+]?[0-9]+\\(%rsp\\)" o0_stack_refs
       "${o0_assembly}")
string(REGEX MATCHALL "[-+]?[0-9]+\\(%rsp\\)" o2_stack_refs
       "${o2_assembly}")
list(LENGTH o0_stack_refs o0_stack_count)
list(LENGTH o2_stack_refs o2_stack_count)
if(NOT o2_stack_count LESS o0_stack_count)
    message(FATAL_ERROR
        "-O2 allocation did not reduce stack traffic "
        "(${o0_stack_count} -> ${o2_stack_count})\n${o2_assembly}")
endif()
