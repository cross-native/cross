# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Under -fipa-reference-addressable, internal objects that no emitted code
# reads are absent from native assembly, objects, and LLVM text, together
# with their stores and what only their initializers name; every kind of
# root stays, and nothing refers to what went, debugging information
# included.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
find_program(LLVM_NM NAMES llvm-nm REQUIRED)

set(removed removed_written removed_never_named removed_group_written
            removed_cells removed_folded removed_target removed_holder
            removed_by_table removed_table)
set(kept kept_escaped kept_loaded kept_used kept_retained kept_exported
         kept_volatile kept_sectioned kept_noinit kept_atomic kept_target
         kept_holder)

# Compiles with the given options and reads the output, or its symbol table
# for an object.
function(compile name)
    set(output "${OUTPUT}-${name}")
    execute_process(COMMAND "${CC}" ${ARGN} "${SOURCE}" -o "${output}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name} failed\n${stdout}\n${stderr}")
    endif()
    if("-c" IN_LIST ARGN)
        execute_process(COMMAND "${LLVM_NM}" "${output}"
            RESULT_VARIABLE status OUTPUT_VARIABLE text ERROR_VARIABLE stderr)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "llvm-nm failed for ${name}\n${stderr}")
        endif()
        if(text MATCHES "(^|\n)[ ]+U ")
            message(FATAL_ERROR "${name} refers to an undefined symbol\n${text}")
        endif()
    else()
        file(READ "${output}" text)
    endif()
    set(text "${text}" PARENT_SCOPE)
endfunction()

function(check name removes)
    compile(${name} ${ARGN})
    foreach(symbol IN LISTS removed)
        if(removes AND text MATCHES "${symbol}")
            message(FATAL_ERROR "${name} emitted ${symbol}\n${text}")
        elseif(NOT removes AND NOT text MATCHES "${symbol}")
            message(FATAL_ERROR "${name} lacks ${symbol}\n${text}")
        endif()
    endforeach()
    foreach(symbol IN LISTS kept)
        if(NOT text MATCHES "${symbol}")
            message(FATAL_ERROR "${name} lacks ${symbol}\n${text}")
        endif()
    endforeach()
endfunction()

set(x86 -target x86_64-unknown-linux-gnu)
set(mips -mprofile=vr4300-o32)
# From -O2, memory forwarding removes the loads of the stored values.
foreach(level O2 O3 Os)
    check(x86_64-${level} TRUE -S -${level} ${x86})
    check(vr4300-${level} TRUE -S -${level} ${mips})
endforeach()
check(x86_64-object TRUE -c -g -O2 ${x86})
check(vr4300-object TRUE -c -g -O2 ${mips})
check(x86_64-O0 FALSE -S -O0 ${x86})
check(x86_64-kept FALSE -S -O2 -fno-ipa-reference-addressable ${x86})
check(vr4300-kept FALSE -S -O2 -fno-ipa-reference-addressable ${mips})
if(LLVM_TEXT)
    check(llvm TRUE -S -emit-llvm -O2 ${x86})
endif()
