# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The `as` tool: target selection, default output a.out, multiple inputs,
# standard input, and command diagnostics for x86-64.

foreach(required AS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
find_program(LLVM_READOBJ NAMES llvm-readobj)
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")

set(a "${OUTPUT}/a.s")
set(b "${OUTPUT}/b.s")
file(WRITE "${a}" "	.text\n	.globl a\na:\n	retq\n")
file(WRITE "${b}" "	.text\n	.globl b\nb:\n	movl $1, %eax\n	retq\n")

# A named target and -o.
execute_process(COMMAND "${AS}" -target x86_64-unknown-linux-gnu "${a}" -o "${OUTPUT}/a.o"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "basic assembly failed:\n${out}${err}")
endif()
if(LLVM_READOBJ)
    execute_process(COMMAND "${LLVM_READOBJ}" -h "${OUTPUT}/a.o" OUTPUT_VARIABLE header)
    if(NOT header MATCHES "Machine: EM_X86_64" AND NOT header MATCHES "64-bit")
        message(FATAL_ERROR "object is not x86-64:\n${header}")
    endif()
endif()

# COFF and Mach-O targets select the object format.
execute_process(COMMAND "${AS}" -target x86_64-w64-windows-gnu "${a}" -o "${OUTPUT}/a.coff.o"
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "COFF assembly failed")
endif()
execute_process(COMMAND "${AS}" -target x86_64-apple-darwin "${a}" -o "${OUTPUT}/a.macho.o"
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "Mach-O assembly failed")
endif()

# Multiple inputs combine into one object.
execute_process(COMMAND "${AS}" -target x86_64-unknown-linux-gnu "${a}" "${b}" -o "${OUTPUT}/ab.o"
                RESULT_VARIABLE rc)
if(NOT rc EQUAL 0)
    message(FATAL_ERROR "multiple-input assembly failed")
endif()

# Default output name a.out.
execute_process(COMMAND "${AS}" -target x86_64-unknown-linux-gnu "${a}"
                WORKING_DIRECTORY "${OUTPUT}" RESULT_VARIABLE rc)
if(NOT rc EQUAL 0 OR NOT EXISTS "${OUTPUT}/a.out")
    message(FATAL_ERROR "default output a.out not produced")
endif()

# A missing input is a command error.
execute_process(COMMAND "${AS}" -target x86_64-unknown-linux-gnu "${OUTPUT}/missing.s"
                RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(rc EQUAL 0)
    message(FATAL_ERROR "missing input was not diagnosed")
endif()
message(STATUS "as tool checks passed")
