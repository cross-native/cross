# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC HOST_CXX SOURCE RUNNER OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -S -O2 -fno-tree-loop-vectorize
            -fno-unroll-loops -fno-if-conversion -fno-eval-calls
            -target x86_64-unknown-linux-gnu "${SOURCE}"
            -o "${OUTPUT}.s"
    RESULT_VARIABLE assembly_status
    OUTPUT_VARIABLE assembly_stdout
    ERROR_VARIABLE assembly_stderr
)
if(NOT assembly_status EQUAL 0)
    message(FATAL_ERROR
        "block-layout compilation failed\n${assembly_stdout}\n${assembly_stderr}")
endif()
file(STRINGS "${OUTPUT}.s" lines)
file(READ "${OUTPUT}.s" assembly)
string(REGEX MATCH
    "dense_choice:[^#]*[.]size dense_choice,"
    dense_body "${assembly}")
string(REGEX MATCH
    "[.]Lcross[.]machine[.][0-9]+[.]jump[.]table[.]0:"
    dense_table "${assembly}")
string(REGEX MATCHALL
    "[.]long[^\r\n]*[.]jump[.]table[.]0"
    dense_table_entries "${assembly}")
list(LENGTH dense_table_entries dense_table_entry_count)
if(dense_body STREQUAL "" OR dense_table STREQUAL "" OR
   NOT dense_table_entry_count EQUAL 8 OR
   NOT dense_body MATCHES "jmp[q]?[\t ]+[*]%" OR
   dense_body MATCHES "cmpq[\t ]+[$](6|7)")
    message(FATAL_ERROR
        "bounded dense choice did not form an exhaustive eight-entry table\n"
        "${assembly}")
endif()
set(previous_jump "")
set(has_conditional false)
foreach(line IN LISTS lines)
    if(line MATCHES "^[\t ]*j(e|ne)[\t ]+[.]Lcross[.]machine[.]")
        set(has_conditional true)
    endif()
    if(line MATCHES "^[\t ]*jmp[q]?[\t ]+([.]Lcross[.]machine[.][^\t ]+)$")
        set(previous_jump "${CMAKE_MATCH_1}")
    elseif(line MATCHES "^([.]Lcross[.]machine[.][^:]+):$")
        if(NOT previous_jump STREQUAL "" AND
           CMAKE_MATCH_1 STREQUAL previous_jump)
            message(FATAL_ERROR
                "redundant jump to the next laid-out block\n${previous_jump}")
        endif()
        set(previous_jump "")
    else()
        set(previous_jump "")
    endif()
endforeach()
if(NOT has_conditional)
    message(FATAL_ERROR "block-layout test did not retain conditional control")
endif()
string(REGEX MATCH
    "nested_layout:[^#]*[.]size nested_layout"
    nested_body "${assembly}")
string(REGEX MATCHALL
    "j(ae|b)[\t ]+[.]Lcross[.]machine[.][^\r\n]+[\r\n]+[.]Lcross[.]machine[.]"
    nested_fallthroughs "${nested_body}")
list(LENGTH nested_fallthroughs nested_fallthrough_count)
if(nested_body STREQUAL "" OR NOT nested_fallthrough_count EQUAL 2 OR
   nested_body MATCHES "j(ae|b)[^\r\n]*[\r\n]+[\t ]*jmp")
    message(FATAL_ERROR
        "nested rotated-loop exits did not use their laid-out fallthroughs\n"
        "${nested_body}")
endif()

execute_process(
    COMMAND "${CC}" -S -O3 -fno-eval-calls
            -target x86_64-unknown-linux-gnu "${SOURCE}"
            -o "${OUTPUT}.O3.s"
    RESULT_VARIABLE o3_status
    OUTPUT_VARIABLE o3_stdout
    ERROR_VARIABLE o3_stderr
)
if(NOT o3_status EQUAL 0)
    message(FATAL_ERROR
        "cross-block scheduling compilation failed\n${o3_stdout}\n${o3_stderr}")
endif()
file(READ "${OUTPUT}.O3.s" o3_assembly)
string(REGEX MATCH
    "dense_choice:[^#]*[.]size dense_choice,"
    o3_dense_body "${o3_assembly}")
string(REGEX MATCHALL "cmpq[\t ]+[$][0-6]"
    o3_dense_compares "${o3_dense_body}")
list(LENGTH o3_dense_compares o3_dense_compare_count)
if(o3_dense_body STREQUAL "" OR
   NOT o3_dense_compare_count EQUAL 7 OR
   o3_dense_body MATCHES "jmp[q]?[\t ]+[*]%")
    message(FATAL_ERROR
        "O3 small dense choice did not select direct compare dispatch\n"
        "${o3_dense_body}")
endif()
string(REGEX MATCH
    "dense_choice_wide:[^#]*[.]size dense_choice_wide,"
    o3_wide_dense_body "${o3_assembly}")
if(o3_wide_dense_body STREQUAL "" OR
   NOT o3_wide_dense_body MATCHES "jmp[q]?[\t ]+[*]%" OR
   NOT o3_wide_dense_body MATCHES "cmpq[\t ]+[$]7")
    message(FATAL_ERROR
        "O3 nine-destination choice did not retain bounded table dispatch\n"
        "${o3_wide_dense_body}")
endif()
string(REGEX MATCH
    "__cross_static_[^:]*scheduled_choice:[^#]*retq"
    scheduled_body "${o3_assembly}")
string(FIND "${scheduled_body}" "imulq" multiply_position)
string(REGEX MATCH "j(e|ne)[\t ]" conditional "${scheduled_body}")
string(FIND "${scheduled_body}" "${conditional}" branch_position)
if(multiply_position EQUAL -1 OR branch_position EQUAL -1 OR
   NOT multiply_position LESS branch_position)
    message(FATAL_ERROR
        "O3 did not schedule a safe integer multiply across the trace branch\n"
        "${scheduled_body}")
endif()

if(WIN32)
    set(HOST_ABI ms_abi)
else()
    set(HOST_ABI sysv_abi)
endif()
execute_process(
    COMMAND "${CC}" "-mabi=${HOST_ABI}" -c -O2 -fno-eval-calls
            "${SOURCE}" -o "${OUTPUT}.o"
    RESULT_VARIABLE object_status
    OUTPUT_VARIABLE object_stdout
    ERROR_VARIABLE object_stderr
)
execute_process(
    COMMAND "${HOST_CXX}" -DCROSS_ENTRY=block_layout_entry "${RUNNER}"
            "${OUTPUT}.o" -o "${OUTPUT}.exe"
    RESULT_VARIABLE link_status
    OUTPUT_VARIABLE link_stdout
    ERROR_VARIABLE link_stderr
)
if(NOT object_status EQUAL 0 OR NOT link_status EQUAL 0)
    message(FATAL_ERROR
        "block-layout runtime build failed\n${object_stdout}\n${object_stderr}\n"
        "${link_stdout}\n${link_stderr}")
endif()
execute_process(COMMAND "${OUTPUT}.exe" RESULT_VARIABLE run_status)
if(NOT run_status EQUAL 1)
    message(FATAL_ERROR "block-layout runtime returned ${run_status}, expected 1")
endif()
