# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Output depends only on the inputs as written: not on the working directory,
# the tree's location, or the environment; one unit's definitions leave the
# other units' symbols and sections unchanged.
foreach(required CC CPP OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
find_program(LLVM_OBJDUMP NAMES llvm-objdump REQUIRED)
find_program(LLVM_NM NAMES llvm-nm REQUIRED)
file(REMOVE_RECURSE "${OUTPUT}")

set(api "i32 bee_helper(in i32 x);\n#define TWICE(x) ((x) + (x))\n")
string(CONCAT macro_definition
    "[[macro]] static $::meta::tokens hidden_cell(in $::meta::tokens input) {\n"
    "    $::meta::tokens name = $::meta::gensym(\"cell\");\n"
    "    return $::quote { static u32 $::unquote(name) = 5u32; };\n"
    "}\n")
string(CONCAT unit_a
    "#include \"api.h\"\n"
    "static i32 a_counter = 1;\n"
    "static const u8 *a_text = \"alpha\";\n"
    "global const u8 a_file[] = $::source::file;\n"
    "static i32 a_scale<i32 N>(in i32 x) { return x * N; }\n"
    "static i32 a_late(in i32 x);\n"
    "global i32 a_first(in i32 x) { return bee_helper(x) + a_scale::<3>(x) + a_text[0]; }\n"
    "global i32 a_second(in i32 x) { a_counter = a_counter + x; return a_late(a_counter); }\n"
    "static i32 a_late(in i32 x) { return TWICE(x) - 1; }\n")
string(CONCAT unit_b
    "#include \"api.h\"\n"
    "${macro_definition}"
    "hidden_cell! {}\n"
    "static i32 bee_counter = 2;\n"
    "static i64 bee_zero;\n"
    "static const u8 *bee_text = \"beta\";\n"
    "static i32 bee_add<i32 N>(in i32 x) { return x + N; }\n"
    "static i32 bee_bump(in i32 x) { bee_counter = bee_counter * x; return bee_counter; }\n"
    "i32 bee_helper(in i32 x) { return bee_bump(x) + bee_add::<9>(x) + (i32)bee_zero; }\n"
    "global i32 bee_entry(in i32 x) { return bee_helper(x) + bee_text[1]; }\n")
string(CONCAT unit_c
    "static i64 sea_zero;\n"
    "static i64 sea_data = 7;\n"
    "static const u8 *sea_text = \"gamma\";\n"
    "static i32 sea_helper(in i32 x) { return x + 1; }\n"
    "global i32 sea_entry(in i32 x) { return sea_helper(x) + (i32)sea_data + (i32)sea_zero + sea_text[2]; }\n")
# The same group with definitions added before unit a's own.
string(CONCAT added_a
    "${macro_definition}"
    "hidden_cell! {}\n"
    "static i32 extra_counter = 4;\n"
    "static const u8 *extra_text = \"extra\";\n"
    "static i32 extra_sub<i32 N>(in i32 x) { return x - N; }\n"
    "global i32 extra_entry(in i32 x) { return extra_sub::<4>(x) + extra_counter + extra_text[0]; }\n"
    "${unit_a}")

foreach(tree one/proj two/deeper/proj added/proj)
    file(WRITE "${OUTPUT}/${tree}/include/api.h" "${api}")
    file(WRITE "${OUTPUT}/${tree}/src/b.x" "${unit_b}")
    file(WRITE "${OUTPUT}/${tree}/src/c.x" "${unit_c}")
endforeach()
file(WRITE "${OUTPUT}/one/proj/src/a.x" "${unit_a}")
file(WRITE "${OUTPUT}/two/deeper/proj/src/a.x" "${unit_a}")
file(WRITE "${OUTPUT}/added/proj/src/a.x" "${added_a}")

set(flags -target x86_64-unknown-linux-gnu -O2 -ffunction-sections -fdata-sections -I include)
set(units src/a.x src/b.x src/c.x)

function(run name directory)
    execute_process(COMMAND ${ARGN} WORKING_DIRECTORY "${directory}"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name} failed\n${out}\n${err}")
    endif()
endfunction()

function(expect_same left right what)
    file(SHA256 "${left}" left_hash)
    file(SHA256 "${right}" right_hash)
    if(NOT left_hash STREQUAL right_hash)
        message(FATAL_ERROR "${what} differ: ${left} ${right}")
    endif()
endfunction()

# Relative paths from two tree locations.
foreach(tree one/proj two/deeper/proj added/proj)
    set(directory "${OUTPUT}/${tree}")
    run("${tree} -S" "${directory}" "${CC}" -S ${flags} ${units} -o group.s)
    run("${tree} -c" "${directory}" "${CC}" -c ${flags} ${units} -o group.o)
    run("${tree} cpp" "${directory}" "${CPP}" -I include ${units} -o group.i)
    run("${tree} -M" "${directory}" "${CC}" -M -MP -I include ${units} -o group.d)
endforeach()
foreach(output group.s group.o group.i group.d)
    expect_same("${OUTPUT}/one/proj/${output}" "${OUTPUT}/two/deeper/proj/${output}"
                "outputs of one group in two directories")
endforeach()
file(READ "${OUTPUT}/one/proj/group.i" preprocessed)
if(NOT preprocessed MATCHES "#[$]::source::unit \"src/a[.]x\"" OR
   NOT preprocessed MATCHES "a_file\\[\\] = \"src/a[.]x\";")
    message(FATAL_ERROR "unit identity is not the written path\n${preprocessed}")
endif()

# Absolute paths mapped back to the written spelling, from elsewhere.
foreach(tree one/proj two/deeper/proj)
    set(root "${OUTPUT}/${tree}")
    set(absolute)
    foreach(unit IN LISTS units)
        list(APPEND absolute "${root}/${unit}")
    endforeach()
    string(REPLACE "/" "_" stem "${tree}")
    set(mapped -target x86_64-unknown-linux-gnu -O2 -ffunction-sections -fdata-sections
        -I "${root}/include" "-ffile-prefix-map=${root}/=")
    run("${tree} absolute -S" "${OUTPUT}" "${CC}" -S ${mapped} ${absolute} -o "${stem}.s")
    run("${tree} absolute -c" "${OUTPUT}" "${CC}" -c ${mapped} ${absolute} -o "${stem}.o")
    expect_same("${OUTPUT}/${stem}.s" "${OUTPUT}/one/proj/group.s" "mapped absolute and relative assembly")
    expect_same("${OUTPUT}/${stem}.o" "${OUTPUT}/one/proj/group.o" "mapped absolute and relative objects")
    run("${tree} absolute cpp" "${OUTPUT}" "${CPP}" "-ffile-prefix-map=${root}/=" -I "${root}/include"
        ${absolute} -o "${stem}.i")
    file(READ "${OUTPUT}/${stem}.i" mapped_stream)
    if(NOT mapped_stream MATCHES "#[$]::source::unit \"src/b[.]x\"" OR
       NOT mapped_stream MATCHES "a_file\\[\\] = \"src/a[.]x\";")
        message(FATAL_ERROR "-ffile-prefix-map did not reach the unit spelling\n${mapped_stream}")
    endif()
endforeach()

# A different environment. cc is single-threaded, so there is no thread count
# to vary.
set(directory "${OUTPUT}/one/proj")
run("environment -S" "${directory}" "${CMAKE_COMMAND}" -E env --unset=PATH --unset=TMP
    --unset=TEMP --unset=TMPDIR --unset=HOME LC_ALL=tr_TR.UTF-8 LANG=tr_TR.UTF-8 TZ=UTC-14
    "${CC}" -S ${flags} ${units} -o env.s)
run("environment -c" "${directory}" "${CMAKE_COMMAND}" -E env LC_ALL=tr_TR.UTF-8
    LANG=tr_TR.UTF-8 TZ=UTC-14 "${CC}" -c ${flags} ${units} -o env.o)
expect_same("${directory}/env.s" "${directory}/group.s" "assembly under another environment")
expect_same("${directory}/env.o" "${directory}/group.o" "objects under another environment")

# Definitions added to unit a change no section or symbol of units b and c.
file(READ "${OUTPUT}/one/proj/group.s" base_assembly)
file(READ "${OUTPUT}/added/proj/group.s" added_assembly)
string(REGEX MATCH "__cross_static_([0-9]+)_bee_counter" ignored "${base_assembly}")
set(bee_static "__cross_static_${CMAKE_MATCH_1}_")
string(REGEX MATCH "__cross_static_([0-9]+)_sea_helper" ignored "${base_assembly}")
set(sea_static "__cross_static_${CMAKE_MATCH_1}_")
if(bee_static STREQUAL "__cross_static__" OR sea_static STREQUAL "__cross_static__")
    message(FATAL_ERROR "static link names not found\n${base_assembly}")
endif()
set(other "bee_|sea_|${bee_static}|${sea_static}")
function(other_sections output_var assembly)
    string(REGEX MATCHALL "[.]section \"[^\"]*\"" sections "${assembly}")
    set(result)
    foreach(section IN LISTS sections)
        string(REGEX REPLACE "^[.]section \"([^\"]*)\"$" "\\1" name "${section}")
        if(name MATCHES "${other}")
            list(APPEND result "${name}")
        endif()
    endforeach()
    set(${output_var} "${result}" PARENT_SCOPE)
endfunction()
other_sections(base_sections "${base_assembly}")
other_sections(added_sections "${added_assembly}")
list(LENGTH base_sections section_count)
if(section_count LESS 10 OR NOT base_sections STREQUAL added_sections)
    message(FATAL_ERROR "other units' sections changed\n${base_sections}\n${added_sections}")
endif()
function(section_dump output_var object section)
    execute_process(COMMAND "${LLVM_OBJDUMP}" -s -j "${section}" "${object}"
        RESULT_VARIABLE status OUTPUT_VARIABLE contents ERROR_VARIABLE err)
    execute_process(COMMAND "${LLVM_OBJDUMP}" -r -j "${section}" "${object}"
        RESULT_VARIABLE relocation_status OUTPUT_VARIABLE relocations ERROR_VARIABLE err)
    if(NOT status EQUAL 0 OR NOT relocation_status EQUAL 0)
        message(FATAL_ERROR "llvm-objdump failed for ${section}\n${err}")
    endif()
    # Drop the lines that name the object file.
    string(REGEX REPLACE "[^\n]*file format[^\n]*\n" "" dump "${contents}${relocations}")
    set(${output_var} "${dump}" PARENT_SCOPE)
endfunction()
foreach(section IN LISTS base_sections)
    section_dump(base_dump "${OUTPUT}/one/proj/group.o" "${section}")
    section_dump(added_dump "${OUTPUT}/added/proj/group.o" "${section}")
    if(NOT base_dump STREQUAL added_dump)
        message(FATAL_ERROR "section ${section} changed\n${base_dump}\n${added_dump}")
    endif()
endforeach()
function(other_symbols output_var object)
    execute_process(COMMAND "${LLVM_NM}" "${object}"
        RESULT_VARIABLE status OUTPUT_VARIABLE symbols ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "llvm-nm failed\n${err}")
    endif()
    string(REGEX MATCHALL "[^\n]*(${other})[^\n]*" lines "${symbols}")
    set(${output_var} "${lines}" PARENT_SCOPE)
endfunction()
other_symbols(base_symbols "${OUTPUT}/one/proj/group.o")
other_symbols(added_symbols "${OUTPUT}/added/proj/group.o")
if(NOT base_symbols MATCHES "gensym" OR NOT base_symbols STREQUAL added_symbols)
    message(FATAL_ERROR "other units' symbols changed\n${base_symbols}\n${added_symbols}")
endif()

# Without per-entity sections, units follow command-line order, each with its
# functions and then its objects in definition order, not the order of
# declarations.
run("shared sections" "${OUTPUT}/one/proj" "${CC}" -S -O0 -target x86_64-unknown-linux-gnu
    -I include ${units} -o shared.s)
file(READ "${OUTPUT}/one/proj/shared.s" shared)
set(previous -1)
foreach(label a_first a_second _a_late _a_scale_G a_counter a_text a_file
              bee_bump bee_helper bee_entry _bee_add_G gensym bee_counter bee_text
              sea_helper sea_entry sea_data sea_text)
    string(REGEX MATCH "\n[_a-zA-Z0-9]*${label}[_a-zA-Z0-9]*:" found "${shared}")
    string(FIND "${shared}" "${found}" position)
    if(found STREQUAL "" OR position LESS_EQUAL previous)
        message(FATAL_ERROR "definition ${label} is out of order\n${shared}")
    endif()
    set(previous ${position})
endforeach()
