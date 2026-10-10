# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Runs each entry of SOURCE under -fbounds-trap: bounds_ok must return zero
# and every other entry must stop at the inline trap. Without the option the
# in-range entry runs unchanged, no preset enables the checks, the checks call
# no helper, and a naked function is not instrumented.
foreach(required CC HOST_CXX SOURCE RUNNER OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
if(WIN32)
    set(HOST_ABI ms_abi)
else()
    set(HOST_ABI sysv_abi)
endif()

function(run_entry stem entry)
    execute_process(COMMAND "${HOST_CXX}" "-DCROSS_ENTRY=${entry}" "${RUNNER}"
            "${stem}.o" -o "${stem}-${entry}.exe"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${stem} ${entry} link failed\n${out}\n${err}")
    endif()
    execute_process(COMMAND "${stem}-${entry}.exe"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(run_status "${status}" PARENT_SCOPE)
endfunction()

foreach(mode O0-trap O2-trap O3-trap O2-plain)
    string(REGEX REPLACE "-.*" "" level "${mode}")
    set(flags -${level})
    if(mode MATCHES "trap$")
        list(APPEND flags -fbounds-trap)
    endif()
    set(stem "${OUTPUT}-${mode}")
    execute_process(COMMAND "${CC}" "-mabi=${HOST_ABI}" ${flags} -c "${SOURCE}" -o "${stem}.o"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${mode} compilation failed\n${out}\n${err}")
    endif()
    run_entry("${stem}" bounds_ok)
    if(NOT run_status EQUAL 0)
        message(FATAL_ERROR "${mode} bounds_ok returned ${run_status}")
    endif()
    if(NOT mode MATCHES "trap$")
        continue()
    endif()
    foreach(entry bounds_past bounds_negative bounds_member bounds_inner
                  bounds_outer bounds_write bounds_one_past bounds_vla)
        run_entry("${stem}" ${entry})
        if(NOT run_status MATCHES "0xc000001d|[Ii]llegal")
            message(FATAL_ERROR "${mode} ${entry} did not trap: ${run_status}")
        endif()
    endforeach()
endforeach()

# The function bodies of read_table: checked with ud2 and no call under the
# option, unchecked under -O3 alone.
function(read_table_body flags variable)
    execute_process(COMMAND "${CC}" -target x86_64-unknown-linux-gnu ${flags} -S "${SOURCE}"
            -o "${OUTPUT}-body.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "assembly for ${flags} failed\n${out}\n${err}")
    endif()
    file(READ "${OUTPUT}-body.s" assembly)
    string(REGEX MATCH "\nread_table:.*\\.size read_table," body "${assembly}")
    string(REGEX MATCH "\nunchecked_offset:.*\\.size unchecked_offset," unchecked "${assembly}")
    set(${variable} "${body}" PARENT_SCOPE)
    set(${variable}_unchecked "${unchecked}" PARENT_SCOPE)
endfunction()
read_table_body("-O2;-fbounds-trap" checked)
if(NOT checked MATCHES "[\t ]ud2" OR checked MATCHES "[\t ](call|jmp)[a-z]*[\t ]+[A-Za-z_]")
    message(FATAL_ERROR "-fbounds-trap check is not an inline trap\n${checked}")
endif()
if(checked_unchecked MATCHES "[\t ]ud2")
    message(FATAL_ERROR "no_sanitize(\"bounds\") kept the check\n${checked_unchecked}")
endif()
read_table_body("-O3" preset)
if(preset MATCHES "[\t ]ud2")
    message(FATAL_ERROR "-O3 enabled -fbounds-trap\n${preset}")
endif()

# A naked MIPS function keeps its exact body.
get_filename_component(source_dir "${SOURCE}" DIRECTORY)
execute_process(COMMAND "${CC}" -target mips-unknown-elf -march=vr4300 -mabi=o32 -O2
        -fbounds-trap -S "${source_dir}/bounds_trap_naked.x" -o "${OUTPUT}-naked.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "naked MIPS compilation failed\n${out}\n${err}")
endif()
file(READ "${OUTPUT}-naked.s" assembly)
string(REGEX MATCH "\nnaked_load:.*\\.size naked_load," naked "${assembly}")
string(REGEX MATCH "\nmanaged_load:.*\\.size managed_load," managed "${assembly}")
if(naked STREQUAL "" OR naked MATCHES "[\t ]break" OR NOT managed MATCHES "[\t ]break")
    message(FATAL_ERROR "-fbounds-trap instrumented a naked function\n${assembly}")
endif()
