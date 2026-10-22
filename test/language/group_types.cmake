# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Inputs of one compilation group that include the same type-defining header
# share its types; differing definitions of a tag stay errors.
foreach(required CC HOST_CXX SOURCE_DIR OUTPUT LEVEL)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path or level")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
if(WIN32)
    set(ABI ms_abi)
else()
    set(ABI sysv_abi)
endif()
set(one "${SOURCE_DIR}/group_types_one.x")
set(two "${SOURCE_DIR}/group_types_two.x")
set(three "${SOURCE_DIR}/group_types_three.x")

function(compile name)
    execute_process(COMMAND "${CC}" -mabi=${ABI} -${LEVEL} ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name}: compilation failed\n${out}\n${err}")
    endif()
endfunction()

foreach(order "one;two;three" "three;one;two" "two;three;one")
    string(REPLACE ";" "_" stem "${order}")
    set(inputs)
    foreach(unit IN LISTS order)
        list(APPEND inputs "${${unit}}")
    endforeach()
    compile(${stem} -c ${inputs} -o "${OUTPUT}/${stem}.o")
    execute_process(COMMAND "${HOST_CXX}" "${SOURCE_DIR}/group_types_driver.cpp"
        "${OUTPUT}/${stem}.o" -o "${OUTPUT}/${stem}.exe"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${stem}: link failed\n${out}\n${err}")
    endif()
    execute_process(COMMAND "${OUTPUT}/${stem}.exe" RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${stem}: shared types produced wrong values")
    endif()
endforeach()
compile(mips -target mips-unknown-elf -mprofile=r3000-o32 -mabi=o32 -S ${one} ${two} ${three}
    -o "${OUTPUT}/mips.s")

# The example of a header shared by two inputs: link names, under any
# mangling, are those of the inputs compiled separately.
file(WRITE "${OUTPUT}/h.x" "#pragma once\nenum { K0, K1 };\nstruct P { i32 x; };\ntypedef struct P P;\ni32 shared(P *p);\n")
file(WRITE "${OUTPUT}/a.x" "#include \"h.x\"\nglobal i32 fa(P *p) { return K1 + shared(p); }\n")
file(WRITE "${OUTPUT}/b.x" "#include \"h.x\"\ni32 shared(P *p) { return p->x; }\nglobal i32 fb() { return K0; }\n")
foreach(mangling cross itanium msvc)
    compile(group_${mangling} -mmangling=${mangling} -S "${OUTPUT}/a.x" "${OUTPUT}/b.x"
        -o "${OUTPUT}/ab-${mangling}.s")
    file(STRINGS "${OUTPUT}/ab-${mangling}.s" grouped REGEX "^[.]globl ")
    set(separate)
    foreach(unit a b)
        compile(${unit}_${mangling} -mmangling=${mangling} -S "${OUTPUT}/${unit}.x"
            -o "${OUTPUT}/${unit}-${mangling}.s")
        file(STRINGS "${OUTPUT}/${unit}-${mangling}.s" names REGEX "^[.]globl ")
        list(APPEND separate ${names})
    endforeach()
    if(NOT grouped STREQUAL separate OR grouped STREQUAL "")
        message(FATAL_ERROR "${mangling}: group link names ${grouped} differ from ${separate}")
    endif()
endforeach()

# One type, so one debugging entry for it.
compile(debug -g -c "${OUTPUT}/a.x" "${OUTPUT}/b.x" -o "${OUTPUT}/ab-debug.o")
find_program(LLVM_DWARFDUMP NAMES llvm-dwarfdump)
if(LLVM_DWARFDUMP)
    execute_process(COMMAND "${LLVM_DWARFDUMP}" --debug-info "${OUTPUT}/ab-debug.o"
        OUTPUT_VARIABLE dump RESULT_VARIABLE status)
    string(REGEX MATCHALL "DW_TAG_structure_type[^\n]*\n[^\n]*DW_AT_name[^\n]*\"P\"" records "${dump}")
    list(LENGTH records count)
    if(NOT status EQUAL 0 OR NOT count EQUAL 1)
        message(FATAL_ERROR "struct P has ${count} debugging entries\n${dump}")
    endif()
else()
    message(STATUS "skipping the debugging-entry check: llvm-dwarfdump unavailable")
endif()

function(reject name expected first second)
    file(WRITE "${OUTPUT}/${name}-1.x" "${first}")
    file(WRITE "${OUTPUT}/${name}-2.x" "${second}")
    execute_process(COMMAND "${CC}" -S "${OUTPUT}/${name}-1.x" "${OUTPUT}/${name}-2.x"
        -o "${OUTPUT}/${name}.s" RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 1 OR NOT err MATCHES "${name}-2[.]x:1:[0-9]+: error: ${expected}" OR
       NOT err MATCHES "${name}-1[.]x:1:[0-9]+: note: the other")
        message(FATAL_ERROR "${name}: missing '${expected}' with a note\n${out}\n${err}")
    endif()
endfunction()
reject(record_differs "record 'P' is defined differently in another source unit"
    "struct P { i32 x; };\n" "struct P { i64 x; };\n")
reject(attribute_differs "record 'P' is defined differently in another source unit"
    "struct P { u8 a; u32 b; };\n" "struct P [[packed]] { u8 a; u32 b; };\n")
reject(enumeration_differs "enumeration 'E' is defined differently in another source unit"
    "enum E { A0, A1 };\n" "enum E { A0, A1, A2 };\n")
reject(enumerator_conflict "enumerator 'K0' is declared more than once"
    "enum { K0, K1 };\n" "enum { K0, K2 };\n")
file(WRITE "${OUTPUT}/same_unit.x" "struct P { i32 x; };\nstruct P { i32 x; };\n")
execute_process(COMMAND "${CC}" -S "${OUTPUT}/same_unit.x" -o "${OUTPUT}/same_unit.s"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 1 OR NOT err MATCHES "duplicate definition of record 'P'")
    message(FATAL_ERROR "a repeated definition in one unit was accepted\n${out}\n${err}")
endif()
