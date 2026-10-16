# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A symbol address is formed in the register its value takes, not staged
# through RAX, and with RAX and RCX as colors the physics loop keeps every
# value in a register.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
execute_process(
    COMMAND "${CC}" -S -O2 -fno-eval-calls -target x86_64-unknown-linux-gnu
            "${SOURCE}" -o "${OUTPUT}.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "scratch-color compilation failed\n${stdout}\n${stderr}")
endif()
file(READ "${OUTPUT}.s" assembly)
string(FIND "${assembly}" "\nphysics:\n" start)
string(SUBSTRING "${assembly}" ${start} -1 physics)
string(FIND "${physics}" "\n.size physics," end)
string(SUBSTRING "${physics}" 0 ${end} physics)
string(REGEX MATCHALL "leaq[\t ]+[A-Za-z0-9_]+[(]%rip[)], %r" addresses "${physics}")
list(LENGTH addresses address_count)
if(NOT address_count EQUAL 4 OR
   physics MATCHES "leaq[\t ]+[A-Za-z0-9_]+[(]%rip[)], %rax[\r\n]+[\t ]+movq[\t ]+%rax, ")
    message(FATAL_ERROR "physics stages its array addresses through RAX\n${physics}")
endif()
if(physics MATCHES "[(]%rsp[)]")
    message(FATAL_ERROR "physics keeps values in frame homes\n${physics}")
endif()
