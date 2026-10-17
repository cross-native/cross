# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Under -fthread-jumps, a branch that the edges dominating it decide is
# folded at every preset that enables it, and an edge into a join is
# threaded past the branches its path decides when the copies cost at most
# -fthread-jumps-limit, on x86-64 and MIPS.

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
endfunction()

function(expect_count label function pattern count)
    file(READ "${OUTPUT}/${label}.s" assembly)
    string(REPLACE ";" "," assembly "${assembly}")
    string(FIND "${assembly}" "\n${function}:\n" start)
    string(FIND "${assembly}" "\n.size ${function}," end)
    if(end EQUAL -1)
        string(FIND "${assembly}" "\n.end ${function}" end)
    endif()
    if(start EQUAL -1 OR end EQUAL -1)
        message(FATAL_ERROR "${label}: no function ${function} in\n${assembly}")
    endif()
    math(EXPR length "${end} - ${start}")
    string(SUBSTRING "${assembly}" ${start} ${length} text)
    string(REGEX MATCHALL "${pattern}" matches "${text}")
    list(LENGTH matches found)
    if(NOT found EQUAL count)
        message(FATAL_ERROR
            "${label}: ${function} has ${found} '${pattern}', not ${count}\n${text}")
    endif()
endfunction()

set(x86 -target x86_64-unknown-linux-gnu)
set(x86_branch "\tj(e|ne|b|be|a|ae|l|le|g|ge|s|ns)\t")
set(mips -target mips-unknown-elf -march=vr4300 -mabi=o32)
set(mips_branch "\tb(eq|ne|eql|nel|gez|gtz|lez|ltz|gezl|gtzl|lezl|ltzl)\t")

foreach(target x86 mips)
    foreach(mode O2 O3 Os Oz)
        compile(${target}-${mode} -${mode} ${${target}})
        expect_count(${target}-${mode} dominated "${${target}_branch}" 1)
    endforeach()
    compile(${target}-no-thread -O2 -fno-thread-jumps ${${target}})
    expect_count(${target}-no-thread dominated "${${target}_branch}" 2)
endforeach()

# x86-64 selects t instead.
foreach(mode O2 O3)
    expect_count(mips-${mode} joined "${mips_branch}" 1)
    expect_count(mips-${mode} release "\tandi\t[^\n]*,63\n" 2)
    expect_count(mips-${mode} loaded "\tlw\t" 2)
endforeach()
# Size goals copy nothing: t passes to code after the join in a new phi.
foreach(label mips-Oz mips-Os mips-no-thread)
    expect_count(${label} joined "${mips_branch}" 2)
    expect_count(${label} release "\tandi\t[^\n]*,63\n" 1)
endforeach()
compile(mips-limit -O2 -fthread-jumps-limit=64 ${mips})
expect_count(mips-limit loaded "\tlw\t" 3)

# The copies hold only the operations the limit counts, not the decided
# conditions, so a copy-free thread adds no instruction.
function(instructions label function variable)
    file(READ "${OUTPUT}/${label}.s" assembly)
    string(FIND "${assembly}" "\n${function}:\n" start)
    string(FIND "${assembly}" "\n.end ${function}" end)
    math(EXPR length "${end} - ${start}")
    string(SUBSTRING "${assembly}" ${start} ${length} text)
    string(REGEX MATCHALL "\n\t[a-z]" matches "${text}")
    list(LENGTH matches count)
    set(${variable} ${count} PARENT_SCOPE)
endfunction()
compile(mips-Oz-no-thread -Oz -fno-thread-jumps ${mips})
instructions(mips-Oz release threaded)
instructions(mips-Oz-no-thread release unthreaded)
if(threaded GREATER unthreaded)
    message(FATAL_ERROR
        "a copy-free thread grew release from ${unthreaded} to ${threaded} instructions")
endif()
