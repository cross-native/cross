# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Checks the exception returns, coprocessor 0 selects, and exception
# instructions of naked_forms.x, their encodings, and the diagnostics of
# naked_forms_errors.x.
foreach(required CC SOURCE ERROR_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()
find_program(LLVM_OBJDUMP NAMES llvm-objdump)

function(compile name)
    execute_process(COMMAND "${CC}" -S ${ARGN} "${SOURCE}"
                            -o "${OUTPUT}-${name}.s"
        RESULT_VARIABLE status ERROR_VARIABLE errors)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name} compilation failed\n${errors}")
    endif()
    file(READ "${OUTPUT}-${name}.s" text)
    set(text "${text}" PARENT_SCOPE)
    execute_process(COMMAND "${CC}" -c ${ARGN} "${SOURCE}"
                            -o "${OUTPUT}-${name}.o"
        RESULT_VARIABLE status ERROR_VARIABLE errors)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name} object failed\n${errors}")
    endif()
    set(words "")
    if(LLVM_OBJDUMP)
        execute_process(COMMAND "${LLVM_OBJDUMP}" -d "${OUTPUT}-${name}.o"
                        OUTPUT_VARIABLE dump)
        set(byte "[0-9a-f][0-9a-f] ")
        string(REGEX MATCHALL "\n +[0-9a-f]+: ${byte}${byte}${byte}${byte}"
               lines "${dump}")
        foreach(line IN LISTS lines)
            string(REGEX REPLACE "\n +[0-9a-f]+: " "" bytes "${line}")
            string(REPLACE " " "" bytes "${bytes}")
            string(APPEND words " ${bytes}")
        endforeach()
    endif()
    set(words "${words}" PARENT_SCOPE)
endfunction()

function(expect name text pattern)
    if(NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${name} lacks '${pattern}'\n${text}")
    endif()
endfunction()

foreach(level O0 O2)
    # MIPS I: RFE fills the delay slot of the jump that resumes.
    compile(r3000-${level} -target mips-unknown-elf -march=r3000 -mabi=o32
            -DR3000=1 -${level})
    expect(r3000 "${text}" "\tjr\t[$]k1\n\trfe\n")
    expect(r3000 "${text}" "\tsyscall\n")
    expect(r3000 "${text}" "\tsyscall\t7\n")
    expect(r3000 "${text}" "\tbreak\n")
    expect(r3000 "${text}" "\tbreak\t3\n")
    if(LLVM_OBJDUMP)
        # jr $k1; rfe, and the codes of syscall 7 and break 3.
        foreach(word 03600008 42000010 000001cc 0003000d)
            expect(r3000-words "${words}" " ${word}")
        endforeach()
    endif()

    # MIPS32: register selects and WAIT.
    compile(mips32-${level} -target mips-unknown-elf -march=mips32 -mabi=o32
            -${level})
    expect(mips32 "${text}" "\tmfc0\t[$]k0,[$]16,1\n")
    expect(mips32 "${text}" "\tmtc0\t[$]k0,[$]16,2\n")
    expect(mips32 "${text}" "\twait\n")
    expect(mips32 "${text}" "\tsyscall\t5\n")
    expect(mips32 "${text}" "\tbreak\t4\n")
    if(LLVM_OBJDUMP)
        # mfc0 $k0,$16,1; mtc0 $k0,$16,2; wait; eret.
        foreach(word 401a8001 409a8002 42000020 42000018)
            expect(mips32-words "${words}" " ${word}")
        endforeach()
    endif()
endforeach()

# Each case: its NAKED_ERROR, the CPU, and the diagnostic.
foreach(case
        "0|vr4300|target instruction '[$]::_rfe' is unavailable with feature 'mips3'"
        "0|mips32|target instruction '[$]::_rfe' is unavailable with feature 'mips32'"
        "1|vr4300|target instruction '[$]::_mfc0' requires feature 'mips32'"
        "2|vr4300|target instruction '[$]::_wait' requires feature 'mips32'"
        "3|vr4300|target instruction '[$]::_beq' transfers control and is only available in a naked function"
        "4|vr4300|calls to a MIPS naked function with out or inout parameters are not implemented")
    string(REGEX MATCH "^([0-9]+)[|]([a-z0-9]+)[|](.*)$" ignored "${case}")
    set(index "${CMAKE_MATCH_1}")
    set(cpu "${CMAKE_MATCH_2}")
    set(pattern "${CMAKE_MATCH_3}")
    execute_process(COMMAND "${CC}" -S -O2 -target mips-unknown-elf -march=${cpu}
                            "-DNAKED_ERROR=${index}" "${ERROR_SOURCE}"
                            -o "${OUTPUT}-error-${index}-${cpu}.s"
        RESULT_VARIABLE status ERROR_VARIABLE errors)
    if(status EQUAL 0 OR NOT errors MATCHES "${pattern}" OR
       NOT errors MATCHES "naked_forms_errors[.]x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "error case ${index} (${cpu}) lacks '${pattern}'\n${errors}")
    endif()
endforeach()
