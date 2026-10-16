# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Under -ftree-dse, a store into a local union or record whose address is
# taken disappears when forwarding has replaced every load of it and the
# address does not escape; a call that receives the address keeps it.

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile label)
    execute_process(COMMAND "${CC}" -S ${ARGN} "${SOURCE}"
                            -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed to compile\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}/${label}.s" assembly)
    string(REPLACE ";" "," assembly "${assembly}")
    set(assembly "${assembly}" PARENT_SCOPE)
endfunction()

function(expect label function pattern present)
    string(FIND "${assembly}" "\n${function}:\n" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "${label}: no function ${function}\n${assembly}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 text)
    string(FIND "${text}" "\n.size ${function}," end)
    string(SUBSTRING "${text}" 0 ${end} text)
    if(present AND NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: ${function} lacks '${pattern}'\n${text}")
    elseif(NOT present AND text MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: ${function} has '${pattern}'\n${text}")
    endif()
endfunction()

set(x86 -target x86_64-unknown-linux-gnu -fno-eval-calls)
set(x86_store "\tmov[a-z]*\t%[a-z0-9]+, -?[0-9]*[(]")
compile(x86-O2 -O2 ${x86})
expect(x86-O2 float_bits "${x86_store}" FALSE)
expect(x86-O2 pair_sum "${x86_store}" FALSE)
expect(x86-O2 escaped "${x86_store}" TRUE)
compile(x86-no-dse -O2 -fno-tree-dse ${x86})
expect(x86-no-dse float_bits "${x86_store}" TRUE)

set(mips -target mips-unknown-elf -march=vr4300 -mabi=o32 -fno-eval-calls)
set(mips_store "\ts[wdbh]c?1?\t[$][a-z0-9]+,-?[0-9]+[(][$][a-z0-9]+[)]")
compile(mips-O2 -O2 ${mips})
expect(mips-O2 float_bits "${mips_store}" FALSE)
expect(mips-O2 pair_sum "${mips_store}" FALSE)
compile(mips-no-dse -O2 -fno-tree-dse ${mips})
expect(mips-no-dse float_bits "${mips_store}" TRUE)
