# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Checks the MIPS machine-instruction registry: availability per ISA, the
# effects of the selected forms in managed code, coprocessor 0 hazard
# spacing, naked functions, and their diagnostics.
foreach(required CC SOURCE ERROR_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()

function(compile output)
    execute_process(COMMAND "${CC}" -S -target mips-unknown-elf ${ARGN}
                            "${SOURCE}" -o "${output}"
        RESULT_VARIABLE status ERROR_VARIABLE errors)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "compilation failed: ${ARGN}\n${errors}")
    endif()
endfunction()

function(body text name variable)
    string(FIND "${text}" "\n${name}:\n" begin)
    string(FIND "${text}" "\n.end ${name}\n" end)
    if(begin EQUAL -1 OR end EQUAL -1)
        message(FATAL_ERROR "no assembly for ${name}\n${text}")
    endif()
    math(EXPR length "${end} - ${begin}")
    string(SUBSTRING "${text}" ${begin} ${length} result)
    set(${variable} "${result}" PARENT_SCOPE)
endfunction()

function(expect name code pattern)
    if(NOT code MATCHES "${pattern}")
        message(FATAL_ERROR "${name} lacks '${pattern}'\n${code}")
    endif()
endfunction()

foreach(level O0 O2)
    compile("${OUTPUT}-${level}.s" -march=vr4300 -${level})
    file(READ "${OUTPUT}-${level}.s" text)

    # Count changes on its own, so two reads are never merged.
    body("${text}" count_reads code)
    string(REGEX MATCHALL "\tmfc0\t[$][a-z0-9]+,[$]9\n" reads "${code}")
    list(LENGTH reads read_count)
    if(NOT read_count EQUAL 2)
        message(FATAL_ERROR "count_reads merged its Count reads\n${code}")
    endif()

    # Cache maintenance and SYNC stay between the store and the load.
    body("${text}" invalidate code)
    expect(invalidate "${code}" "\tsw\t[^\n]*\n(\t[^\n]*\n)*\tcache\t17,[^\n]*\n(\t[^\n]*\n)*\tlw\t")
    body("${text}" synced code)
    expect(synced "${code}" "\tsw\t[^\n]*\n(\t[^\n]*\n)*\tsync\n(\t[^\n]*\n)*\tlw\t")

    # EPC two and Status three instructions before ERET; the Status write
    # also needs four before a coprocessor instruction such as ERET.
    body("${text}" resume code)
    expect(resume "${code}" "\tmtc0\t[$]a0,[$]14\n\t[^\n]*\n\t[^\n]*\n")
    expect(resume "${code}" "\tmtc0\t[$]k0,[$]12\n\tnop\n\tnop\n\tnop\n\tnop\n\teret$")
    if(code MATCHES "addiu\t[$]sp|[$]fp|\tsw\t|\tlw\t")
        message(FATAL_ERROR "naked resume uses a frame\n${code}")
    endif()

    # An exception vector uses only k0 and k1.
    body("${text}" exception_vector code)
    expect(exception_vector "${code}"
        "\tlui\t[$]k0,%hi[(]exception_handler[)]\n\taddiu\t[$]k0,[$]k0,%lo[(]exception_handler[)]\n\tjr\t[$]k0\n\tnop$")
endforeach()

file(READ "${OUTPUT}-O2.s" text)
# A store and a cache operation on its line are two non-load, non-cache
# instructions apart.
body("${text}" invalidate code)
expect(invalidate "${code}" "\tsw\t[^\n]*\n\tnop\n\tnop\n\tcache\t17,")
# Status write: four instructions before a coprocessor use, so three before
# the return jump and its delay slot.
body("${text}" set_status code)
expect(set_status "${code}" "\tmtc0\t[$]a0,[$]12\n\tnop\n\tnop\n\tnop\n\tjr\t[$]ra\n")
# TLB write: three instructions before a load.
body("${text}" after_tlb code)
expect(after_tlb "${code}" "\ttlbwi\n\tnop\n\tnop\n\tnop\n\tlw\t")
# A pure form inlines and merges.
body("${text}" inline_root code)
string(REGEX MATCHALL "\tsqrt[.]s\t" roots "${code}")
list(LENGTH roots root_count)
if(NOT root_count EQUAL 1 OR code MATCHES "\tjal\t")
    message(FATAL_ERROR "inline_root did not inline one sqrt.s\n${code}")
endif()
# A naked leaf computes into v0 and returns through ra.
body("${text}" naked_leaf code)
expect(naked_leaf "${code}" "\taddiu\t[$]v0,[$]a0,42\n\tjr\t[$]ra\n\tnop$")

# Release 2 clears the Status hazard with one EHB.
compile("${OUTPUT}-mips32r2.s" -march=mips32r2 -O2)
file(READ "${OUTPUT}-mips32r2.s" text)
body("${text}" set_status code)
expect(set_status "${code}" "\tmtc0\t[$]a0,[$]12\n\tehb\n\tjr\t[$]ra\n")

# Availability per ISA.
file(WRITE "${OUTPUT}-query.x"
    "global u32 cache_query() { return $::has_instruction($::_cache); }\n"
    "global u32 sqrt_query() { return $::has_instruction($::_sqrt); }\n"
    "global u32 dmfc0_query() { return $::has_instruction($::_dmfc0); }\n")
foreach(case "vr4300|1 1 1" "r3000|0 0 0" "mips2|0 1 0" "mips32|1 1 0")
    string(REPLACE "|" ";" parts "${case}")
    list(GET parts 0 cpu)
    list(GET parts 1 expected)
    execute_process(COMMAND "${CC}" -E -target mips-unknown-elf -march=${cpu}
                            "${OUTPUT}-query.x" -o "${OUTPUT}-query-${cpu}.i"
        RESULT_VARIABLE status ERROR_VARIABLE errors)
    file(READ "${OUTPUT}-query-${cpu}.i" queried)
    string(REGEX MATCHALL "return ([01])" answers "${queried}")
    string(REPLACE "return " "" answers "${answers}")
    string(REPLACE ";" " " answers "${answers}")
    if(NOT status EQUAL 0 OR NOT answers STREQUAL expected)
        message(FATAL_ERROR "-march=${cpu} instruction queries were '${answers}', not '${expected}'\n${errors}")
    endif()
endforeach()

execute_process(COMMAND "${CC}" -target mips-unknown-elf --print-instructions
    RESULT_VARIABLE status OUTPUT_VARIABLE registry ERROR_VARIABLE errors)
foreach(row "$::_mfc0 instruction [mips] privileged volatile"
            "$::_cache instruction [mips3] privileged volatile"
            "$::_sqrt instruction [mips2,hard-float,!single-float]"
            "$::_jr instruction [mips]")
    string(FIND "${registry}" "${row}" position)
    if(NOT status EQUAL 0 OR position EQUAL -1)
        message(FATAL_ERROR "--print-instructions lacks '${row}'\n${registry}${errors}")
    endif()
endforeach()

# Diagnostics: ISA gates, typed forms, raw exits, and naked rules.
set(error_cases
    "r3000|target instruction '[$]::_cache' requires feature 'mips3'"
    "r3000|target instruction '[$]::_sqrt' requires feature 'mips2'"
    "vr4300|no typed form of target instruction '[$]::_mfc0' matches these operands"
    "vr4300|'[$]::_eret' transfers control and is only available in a naked function"
    "vr4300|ordinary return is not permitted in a naked function"
    "vr4300|ordinary calls are not permitted in a naked function"
    "vr4300|reachable end of naked function requires an explicit target control transfer"
    "vr4300|ordinary automatic and stack objects are not permitted in a naked function"
    "vr4300|a naked function value needs a register. declare more registers in its clobber contract"
    "vr4300|naked function writes '[$]hi' in 'mult [$]a0,[$]a1', outside its clobber contract"
    "allegrex|target instruction '[$]::_sqrt' is unavailable with feature 'single-float'")
set(index 0)
foreach(case IN LISTS error_cases)
    string(FIND "${case}" "|" split)
    string(SUBSTRING "${case}" 0 ${split} cpu)
    math(EXPR start "${split} + 1")
    string(SUBSTRING "${case}" ${start} -1 pattern)
    execute_process(COMMAND "${CC}" -S -O2 -target mips-unknown-elf -march=${cpu}
                            "-DMACHINE_ERROR=${index}" "${ERROR_SOURCE}"
                            -o "${OUTPUT}-error-${index}.s"
        RESULT_VARIABLE status ERROR_VARIABLE errors)
    if(status EQUAL 0 OR NOT errors MATCHES "${pattern}" OR
       NOT errors MATCHES "machine_instructions_errors[.]x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "error case ${index} lacks '${pattern}'\n${errors}")
    endif()
    math(EXPR index "${index} + 1")
endforeach()
