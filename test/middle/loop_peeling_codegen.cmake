# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A loop with a constant trip count is completely peeled under -fpeel-loops
# within -fpeel-limit, and its reads of values stored before it become those
# values, on x86-64 and MIPS.

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
    string(FIND "${assembly}" "\nweighted:\n" start)
    string(FIND "${assembly}" "\n.size weighted," end)
    if(end EQUAL -1)
        string(FIND "${assembly}" "\n.end weighted" end)
    endif()
    if(start EQUAL -1 OR end EQUAL -1)
        message(FATAL_ERROR "${label}: no function weighted in\n${assembly}")
    endif()
    math(EXPR length "${end} - ${start}")
    string(SUBSTRING "${assembly}" ${start} ${length} text)
    set(text "${text}" PARENT_SCOPE)
endfunction()

function(expect label pattern present)
    if(present AND NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: weighted lacks '${pattern}'\n${text}")
    elseif(NOT present AND text MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: weighted has '${pattern}'\n${text}")
    endif()
endfunction()

# Address arithmetic is not a read.
function(expect_count label pattern count)
    string(REGEX REPLACE "\tlea[a-z]*\t[^\n]*" "" reads "${text}")
    string(REGEX MATCHALL "${pattern}" matches "${reads}")
    list(LENGTH matches found)
    if(NOT found EQUAL count)
        message(FATAL_ERROR
            "${label}: weighted has ${found} '${pattern}', not ${count}\n${text}")
    endif()
endfunction()

set(x86 -target x86_64-unknown-linux-gnu)
set(x86_branch "\tj[a-z]+\t")
set(x86_read "\t[a-z]+\t-?[0-9]*[(]%[a-z0-9]+(,%[a-z0-9]+,[0-9])?[)], ")
foreach(mode O2 O3)
    compile(x86-${mode} -${mode} ${x86})
    expect(x86-${mode} "${x86_branch}" FALSE)
    expect_count(x86-${mode} "${x86_read}" 4)
    expect(x86-${mode} "\timull\t[$]11, " TRUE)
endforeach()
# O1 keeps values in memory and does not forward them.
compile(x86-O1-peel -O1 -fpeel-loops -fpeel-limit=96 ${x86})
expect(x86-O1-peel "${x86_branch}" FALSE)
foreach(mode "O2;-fno-peel-loops" "O2;-fpeel-limit=8" "Os")
    string(REPLACE ";" "" label "x86-${mode}")
    compile(${label} -${mode} ${x86})
    expect(${label} "${x86_branch}" TRUE)
endforeach()

set(mips -target mips-unknown-elf -march=vr4300 -mabi=o32)
set(mips_branch "\tb[a-z]+\t")
foreach(mode "O2" "O3")
    compile(mips-${mode} -${mode} ${mips})
    expect(mips-${mode} "${mips_branch}" FALSE)
    expect_count(mips-${mode} "\tlw\t" 4)
endforeach()
compile(mips-no-peel -O2 -fno-peel-loops ${mips})
expect(mips-no-peel "${mips_branch}" TRUE)
