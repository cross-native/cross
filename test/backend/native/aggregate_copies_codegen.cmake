# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# At -O2, record copies and initializers move register-wide units without a
# temporary: a copy between pointers loads its words and stores them, a
# larger one does so in address order, in a loop on MIPS, without comparing
# the addresses, a literal assigned to an object initializes it in place, a
# derived literal reads its base and stores the destination, a literal whose
# base is its destination stores only what it changes, and an initializer
# whose entries cover the object stores only them. A volatile record keeps
# its byte accesses, and -fno-tree-sra keeps the copy through a home, in
# words.

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile label)
    execute_process(COMMAND "${CC}" -S -O2 ${ARGN} "${SOURCE}" -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed to compile\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}/${label}.s" text)
    string(REPLACE ";" "," text "${text}")
    set(assembly "${text}" PARENT_SCOPE)
    set(label "${label}" PARENT_SCOPE)
endfunction()

# Sets `body` to the text of function `name`.
function(function_body name)
    string(FIND "${assembly}" "\n${name}:\n" start)
    string(FIND "${assembly}" "\n.size ${name}," end)
    if(start EQUAL -1 OR end EQUAL -1)
        message(FATAL_ERROR "${label}: no function ${name}\n${assembly}")
    endif()
    math(EXPR length "${end} - ${start}")
    string(SUBSTRING "${assembly}" ${start} ${length} text)
    set(body "${text}" PARENT_SCOPE)
endfunction()

# Function `name` has `count` instructions that match `pattern`.
function(expect_count name pattern count)
    function_body(${name})
    string(REGEX MATCHALL "\n\t${pattern}" found "${body}")
    list(LENGTH found actual)
    if(NOT actual EQUAL count)
        message(FATAL_ERROR
            "${label}: ${name} has ${actual} instructions matching '${pattern}', not ${count}\n${body}")
    endif()
endfunction()

foreach(profile vr4300-o32 n64)
    compile(${profile} -mprofile=${profile})
    foreach(name copy_row with_dash update_harm literal_row)
        expect_count(${name} "(lbu|sb|addiu[\t ]+[$]sp)[\t ]" 0)
    endforeach()
    expect_count(copy_row "lw[\t ]" 8)
    expect_count(copy_row "sw[\t ]" 8)
    expect_count(copy_big "(beq|bne|beqz|bnez|bgez|bltz|blez|bgtz)[\t ]" 1)
    expect_count(copy_big "sltu[\t ]" 0)
    expect_count(copy_big "lw[\t ]" 4)
    expect_count(copy_big "sw[\t ]" 4)
    expect_count(with_dash "lw[\t ]" 7)
    expect_count(with_dash "sw[\t ]" 8)
    expect_count(literal_row "(lw|bne|beq)[\t ]" 0)
    expect_count(literal_row "sw[\t ]" 8)
    expect_count(update_harm "lw[\t ]" 0)
    expect_count(update_harm "sw[\t ]" 1)
    expect_count(pair_entries "(sb|sw[\t ]+[$]zero)[\t ,]" 0)
    expect_count(store_device "sb[\t ]" 8)
    compile(${profile}-no-tree-sra -mprofile=${profile} -fno-tree-sra)
    expect_count(copy_row "addiu[\t ]+[$]sp,[$]sp,-" 1)
    expect_count(copy_row "lbu[\t ]" 0)
endforeach()

compile(x86_64 -target x86_64-unknown-linux-gnu)
foreach(name copy_row literal_row with_dash update_harm)
    expect_count(${name} "[a-z]+[\t ][^\n]*%rsp" 0)
endforeach()
expect_count(copy_row "movdqu[\t ]" 4)
expect_count(copy_big "(cmp[a-z]*|j[a-z]+)[\t ]" 0)
expect_count(copy_big "movdqu[\t ]" 32)
expect_count(literal_row "(movb|jb|jne)[\t ]" 0)
expect_count(literal_row "movdqu[\t ]" 2)
expect_count(update_harm "mov[lq][\t ][^\n]*[(]" 1)
expect_count(pair_entries "movb[\t ]" 0)
