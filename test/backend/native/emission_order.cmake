# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Output follows definition order per source unit: functions with their jump
# tables and literals, naked functions in place, and clones after their
# origin. The output is the same from any tree location, and growing one unit
# leaves the other units' assembly and sections unchanged. With RUNNER and
# HOST_CXX, the grown group instead runs on the host at several levels.
foreach(required CC OUTPUT TRIPLE)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUTPUT}")

if(TRIPLE MATCHES "^mips")
    # Pools hold 64-bit constants under size goals; MIPS has no jump tables
    # and does not clone.
    set(flags -target ${TRIPLE} -Os)
    set(naked_body "register void *link \"ra\";\n    $::_jr(link);")
    set(a_scale "global f64 a_scale(in f64 v) { return v * 3.14159265358979; }\n")
    set(a_scale_added "global f64 a_scale(in f64 v) { return v * 3.14159265358979 + 1.41421356237309; }\n")
    set(a_extra "global u64 a_mask(in u64 v) { return v ^ 0x123456789abcdef1u64; }\n")
    string(CONCAT b_constants
        "global f64 b_scale(in f64 v) { return v * 2.71828182845904 + 1.73205080756887; }\n"
        "global u64 b_mask(in u64 v) { return v + 0x0fedcba987654321u64; }\n")
    set(c_constants "global f64 c_scale(in f64 v) { return v * 0.57721566490153; }\n")
    set(clones OFF)
else()
    set(flags -target ${TRIPLE} -O3)
    set(naked_body "$::_ret();")
    set(a_scale "global f64 a_scale(in f64 v) { return v * 3.25; }\n")
    set(a_scale_added "global f64 a_scale(in f64 v) { return v * 3.25 + 9.75; }\n")
    string(CONCAT a_extra
        "global i32 a_switch(in i32 x) {\n"
        "    switch (x) {\n"
        "    case 0: return 11; case 1: return 2; case 2: return 33; case 3: return 4;\n"
        "    case 4: return 55; case 5: return 6; case 6: return 77; case 7: return 8;\n"
        "    case 8: return 99; case 9: return 10;\n"
        "    default: return 70;\n"
        "    }\n"
        "}\n")
    string(CONCAT b_constants
        "global f64 b_scale(in f64 v) { return v * 7.5 + 0.125; }\n"
        "global i32 b_switch(in i32 x) {\n"
        "    switch (x) {\n"
        "    case 0: return 1; case 1: return 22; case 2: return 3; case 3: return 44;\n"
        "    case 4: return 5; case 5: return 66; case 6: return 7; case 7: return 88;\n"
        "    case 8: return 9; case 9: return 100;\n"
        "    default: return 7;\n"
        "    }\n"
        "}\n")
    set(c_constants "global f64 c_scale(in f64 v) { return v * 1.5 + 2.5; }\n")
    set(clones ON)
endif()

string(CONCAT unit_a_head
    "static i32 a_counter = 1;\n")
string(CONCAT unit_a_tail
    "[[naked]] global void a_naked() {\n    ${naked_body}\n}\n"
    "global i32 a_last(in i32 x) { a_counter = a_counter + x; return a_counter; }\n")
set(unit_a "${unit_a_head}${a_scale}${unit_a_tail}")
# The same unit with one more literal in a_scale and a new function with a
# jump table (x86-64) or literals (MIPS).
set(added_a "${unit_a_head}${a_scale_added}${a_extra}${unit_a_tail}")
string(CONCAT unit_b
    "global void b_begin() {}\n"
    "[[noinline]]\n"
    "static i64 b_target(in i64 selector, in i64 value) {\n"
    "    if (selector == 1i64) { return value + 10i64; }\n"
    "    return value - 3i64;\n"
    "}\n"
    "global i64 b_first(in i64 selector, in i64 value) {\n"
    "    return $::runtime(b_target(1i64, value) + b_target(selector, value));\n"
    "}\n"
    "${b_constants}"
    "[[naked]] global void b_naked() {\n    ${naked_body}\n}\n"
    "static const u32 b_table[4] = {5u32, 6u32, 7u32, 8u32};\n"
    "global u32 b_pick(in u32 i) { return b_table[i & 3u32]; }\n")
string(CONCAT unit_c
    "static i64 c_data = 7;\n"
    "${c_constants}"
    "global i64 c_entry(in i64 x) { return x + c_data; }\n")

foreach(tree one/proj two/deeper/proj added/proj)
    file(WRITE "${OUTPUT}/${tree}/src/b.x" "${unit_b}")
    file(WRITE "${OUTPUT}/${tree}/src/c.x" "${unit_c}")
endforeach()
file(WRITE "${OUTPUT}/one/proj/src/a.x" "${unit_a}")
file(WRITE "${OUTPUT}/two/deeper/proj/src/a.x" "${unit_a}")
file(WRITE "${OUTPUT}/added/proj/src/a.x" "${added_a}")
set(units src/a.x src/b.x src/c.x)
set(sections -ffunction-sections -fdata-sections)

function(run name directory)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name} failed\n${out}\n${err}")
    endif()
endfunction()

if(DEFINED RUNNER)
    if(WIN32)
        set(host_abi ms_abi)
    else()
        set(host_abi sysv_abi)
    endif()
    string(CONCAT c_check
        "global i32 c_check() {\n"
        "    a_naked();\n"
        "    b_naked();\n"
        "    if ($::runtime(a_last(1)) != 2) { return 1; }\n"
        "    if ($::runtime(a_scale(2.0f64)) != 16.25f64) { return 2; }\n"
        "    if ($::runtime(a_switch(4)) != 55 || $::runtime(a_switch(12)) != 70) { return 3; }\n"
        "    if ($::runtime(b_first(2i64, 5i64)) != 17i64) { return 4; }\n"
        "    if ($::runtime(b_scale(2.0f64)) != 15.125f64) { return 5; }\n"
        "    if ($::runtime(b_switch(3)) != 44 || $::runtime(b_switch(-1)) != 7) { return 6; }\n"
        "    if ($::runtime(b_pick(6u32)) != 7u32) { return 7; }\n"
        "    if ($::runtime(c_scale(2.0f64)) != 5.5f64) { return 8; }\n"
        "    if ($::runtime(c_entry(1i64)) != 8i64) { return 9; }\n"
        "    return 0;\n"
        "}\n")
    set(directory "${OUTPUT}/added/proj")
    file(APPEND "${directory}/src/c.x" "${c_check}")
    foreach(level O0 O2 O3 Os Oz)
        run("${level} -c" "${directory}" "${CC}" -c -target ${TRIPLE} -mabi=${host_abi}
            -${level} ${units} -o run-${level}.o)
        run("${level} link" "${directory}" "${HOST_CXX}" -DCROSS_ENTRY=c_check
            "${RUNNER}" run-${level}.o -o run-${level}.exe)
        execute_process(COMMAND "${directory}/run-${level}.exe"
            RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT result EQUAL 0)
            message(FATAL_ERROR "c_check returned ${result} at ${level}\n${out}\n${err}")
        endif()
    endforeach()
    return()
endif()
find_program(LLVM_OBJDUMP NAMES llvm-objdump REQUIRED)

function(expect_same left right what)
    file(SHA256 "${left}" left_hash)
    file(SHA256 "${right}" right_hash)
    if(NOT left_hash STREQUAL right_hash)
        message(FATAL_ERROR "${what} differ: ${left} ${right}")
    endif()
endfunction()

foreach(tree one/proj two/deeper/proj added/proj)
    set(directory "${OUTPUT}/${tree}")
    run("${tree} -S" "${directory}" "${CC}" -S ${flags} ${units} -o group.s)
    run("${tree} -c" "${directory}" "${CC}" -c ${flags} ${units} -o group.o)
    run("${tree} sections -S" "${directory}" "${CC}" -S ${flags} ${sections} ${units}
        -o sections.s)
    run("${tree} sections -c" "${directory}" "${CC}" -c ${flags} ${sections} ${units}
        -o sections.o)
endforeach()

# The same files in two tree locations, and as absolute paths mapped back to
# the written spelling from another directory.
foreach(output group.s group.o sections.s sections.o)
    expect_same("${OUTPUT}/one/proj/${output}" "${OUTPUT}/two/deeper/proj/${output}"
                "outputs of one group in two directories")
endforeach()
set(root "${OUTPUT}/two/deeper/proj")
set(absolute)
foreach(unit IN LISTS units)
    list(APPEND absolute "${root}/${unit}")
endforeach()
run("absolute -S" "${OUTPUT}" "${CC}" -S ${flags} "-ffile-prefix-map=${root}/="
    ${absolute} -o mapped.s)
run("absolute -c" "${OUTPUT}" "${CC}" -c ${flags} "-ffile-prefix-map=${root}/="
    ${absolute} -o mapped.o)
expect_same("${OUTPUT}/mapped.s" "${OUTPUT}/one/proj/group.s" "mapped and relative assembly")
expect_same("${OUTPUT}/mapped.o" "${OUTPUT}/one/proj/group.o" "mapped and relative objects")

# Definition order, with each naked function in place and each clone after
# the function it specializes.
file(READ "${OUTPUT}/one/proj/group.s" base_assembly)
set(order a_scale a_naked a_last b_begin b_target)
if(clones)
    list(APPEND order "b_target[.]const[.]0_1")
endif()
list(APPEND order b_first b_scale b_naked b_pick c_scale c_entry)
set(previous -1)
foreach(name IN LISTS order)
    string(REGEX MATCH "\n[_a-zA-Z0-9]*${name}:" found "${base_assembly}")
    string(FIND "${base_assembly}" "${found}" position)
    if(found STREQUAL "" OR position LESS_EQUAL previous)
        message(FATAL_ERROR "definition ${name} is out of order\n${base_assembly}")
    endif()
    set(previous ${position})
endforeach()

# Units b and c assemble to the same text after unit a grew.
file(READ "${OUTPUT}/added/proj/group.s" added_assembly)
foreach(assembly base_assembly added_assembly)
    string(FIND "${${assembly}}" "b_begin" start)
    string(SUBSTRING "${${assembly}}" ${start} -1 ${assembly}_tail)
endforeach()
if(NOT base_assembly_tail STREQUAL added_assembly_tail)
    message(FATAL_ERROR "units b and c changed\n${base_assembly_tail}\n${added_assembly_tail}")
endif()
string(FIND "${added_assembly}" "b_begin" start)
string(SUBSTRING "${added_assembly}" 0 ${start} added_head)
if(NOT added_head MATCHES "[.]float[.]1:" OR
   (NOT TRIPLE MATCHES "^mips" AND NOT added_head MATCHES "jump[.]table"))
    message(FATAL_ERROR "unit a did not gain a literal and a jump table\n${added_head}")
endif()

# With a section per definition, the sections of units b and c, their
# jump tables and literals included, keep their bytes and relocations.
if(TRIPLE MATCHES "darwin|apple")
    return()
endif()
file(READ "${OUTPUT}/one/proj/sections.s" base_sections_assembly)
string(REGEX MATCH "__cross_static_([0-9]+)_b_target" ignored "${base_sections_assembly}")
set(b_static "__cross_static_${CMAKE_MATCH_1}_")
string(REGEX MATCH "__cross_static_([0-9]+)_c_data" ignored "${base_sections_assembly}")
set(c_static "__cross_static_${CMAKE_MATCH_1}_")
if(b_static STREQUAL "__cross_static__" OR c_static STREQUAL "__cross_static__")
    message(FATAL_ERROR "static link names not found\n${base_sections_assembly}")
endif()
string(REGEX MATCHALL "[.]section \"?[.](text|rodata|rdata|data)[.$][^\",\n]*"
       directives "${base_sections_assembly}")
set(other_sections)
foreach(directive IN LISTS directives)
    string(REGEX REPLACE "^[.]section \"?" "" name "${directive}")
    if(name MATCHES "[.$](b_|c_|${b_static}|${c_static})")
        list(APPEND other_sections "${name}")
    endif()
endforeach()
list(REMOVE_DUPLICATES other_sections)
if(NOT other_sections MATCHES "[.]r(o)?data[.$]b_scale(;|$)")
    message(FATAL_ERROR "no literal section for b_scale: ${other_sections}")
endif()
if(NOT TRIPLE MATCHES "^mips" AND
   NOT other_sections MATCHES "[.]r(o)?data[.$]b_switch(;|$)")
    message(FATAL_ERROR "no jump-table section for b_switch: ${other_sections}")
endif()
function(section_dump output_var object section)
    execute_process(COMMAND "${LLVM_OBJDUMP}" -s -r -j "${section}" "${object}"
        RESULT_VARIABLE status OUTPUT_VARIABLE contents ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "llvm-objdump failed for ${section}\n${err}")
    endif()
    string(REGEX REPLACE "[^\n]*file format[^\n]*\n" "" contents "${contents}")
    set(${output_var} "${contents}" PARENT_SCOPE)
endfunction()
foreach(section IN LISTS other_sections)
    section_dump(base_dump "${OUTPUT}/one/proj/sections.o" "${section}")
    section_dump(added_dump "${OUTPUT}/added/proj/sections.o" "${section}")
    if(NOT base_dump STREQUAL added_dump)
        message(FATAL_ERROR "section ${section} changed\n${base_dump}\n${added_dump}")
    endif()
endforeach()
