# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# -g emits DWARF 5 line tables with mapped paths, call-frame information in
# .debug_frame (or .eh_frame when the entry asks), and the compile unit with
# its namespaces, subprograms, variables, and types; the code itself does not
# change.
foreach(required CC SOURCE HEADER PROBE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
find_program(LLVM_DWARFDUMP NAMES llvm-dwarfdump REQUIRED)
find_program(LLVM_OBJDUMP NAMES llvm-objdump REQUIRED)
find_program(LLVM_READOBJ NAMES llvm-readobj REQUIRED)

file(REMOVE_RECURSE "${OUTPUT}")
foreach(tree one two)
    file(MAKE_DIRECTORY "${OUTPUT}/${tree}/src")
    configure_file("${SOURCE}" "${OUTPUT}/${tree}/src/debug_info.x" COPYONLY)
    configure_file("${HEADER}" "${OUTPUT}/${tree}/src/debug_info_header.x" COPYONLY)
endforeach()
set(tree "${OUTPUT}/one")
set(input "${tree}/src/debug_info.x")
set(linux x86_64-unknown-linux-gnu)

function(run label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status})\n${stdout}\n${stderr}")
    endif()
    set(stdout "${stdout}" PARENT_SCOPE)
endfunction()

function(compile object)
    run("cc ${object}" "${CC}" -c ${ARGN} -o "${OUTPUT}/${object}")
endfunction()

function(expect label text)
    foreach(pattern IN LISTS ARGN)
        if(NOT text MATCHES "${pattern}")
            message(FATAL_ERROR "${label} lacks '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

function(reject label text)
    foreach(pattern IN LISTS ARGN)
        if(text MATCHES "${pattern}")
            message(FATAL_ERROR "${label} has '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

function(verify object)
    run("verify ${object}" "${LLVM_DWARFDUMP}" --verify "${OUTPUT}/${object}")
    expect("verify ${object}" "${stdout}" "No errors[.]")
endfunction()

function(sections output object)
    run("sections ${object}" "${LLVM_READOBJ}" --sections "${OUTPUT}/${object}")
    set(${output} "${stdout}" PARENT_SCOPE)
endfunction()

# Line rows: every line that produces code at -O0 has a row, and the paths
# keep the mapped spelling.
set(map "-ffile-prefix-map=${tree}=/work")
compile(lines.o -g -O0 -target ${linux} "${map}" "${input}")
verify(lines.o)
run("line table" "${LLVM_DWARFDUMP}" --debug-line "${OUTPUT}/lines.o")
set(lines "${stdout}")
expect("line table" "${lines}"
    "include_directories\\[ *0\\] = \"[.]\""
    "name: \"/work/src/debug_info[.]x\""
    "name: \"debug_info_header[.]x\"")
string(REGEX MATCHALL "include_directories\\[ *[0-9]+\\] = \"[^\"]*\"" directories "${lines}")
foreach(directory IN LISTS directories)
    if(NOT directory MATCHES "= \"([.]|/work[^\"]*)\"$")
        message(FATAL_ERROR "line table directory outside the mapped spelling: ${directory}")
    endif()
endforeach()
file(STRINGS "${OUTPUT}/lines.o" leaked REGEX "${tree}")
if(leaked)
    message(FATAL_ERROR "the object contains the unmapped path: ${leaked}")
endif()
string(REGEX MATCHALL "\n0x[0-9a-f]+ +[0-9]+ +[0-9]+ +[0-9]+ " rows "${lines}")
set(seen "")
foreach(row IN LISTS rows)
    string(REGEX MATCH "0x[0-9a-f]+ +([0-9]+) +[0-9]+ +([0-9]+)" ignored "${row}")
    list(APPEND seen "${CMAKE_MATCH_2}:${CMAKE_MATCH_1}")
endforeach()
foreach(line 4 5 6 10 11 14 15 16 17 19 22 23 24 26 27 28)
    if(NOT "0:${line}" IN_LIST seen)
        message(FATAL_ERROR "no row for debug_info.x:${line}\n${lines}")
    endif()
endforeach()
if(NOT "1:2" IN_LIST seen)
    message(FATAL_ERROR "no row for debug_info_header.x:2\n${lines}")
endif()

# The compile unit, namespaces, and subprograms with their ranges.
run("info" "${LLVM_DWARFDUMP}" --debug-info "${OUTPUT}/lines.o")
set(info "${stdout}")
expect("info" "${info}"
    "DW_TAG_compile_unit\n[^\n]*DW_AT_producer[^\n]*\n[^\n]*DW_AT_language\t[(]DW_LANG_C_plus_plus_14[)]\n[^\n]*DW_AT_name\t[(]\"/work/src/debug_info[.]x\"[)]\n[^\n]*DW_AT_comp_dir\t[(]\"[.]\"[)]"
    "DW_AT_ranges"
    "DW_TAG_namespace\n[^\n]*DW_AT_name\t[(]\"outer\"[)]\n\n[^\n]*DW_TAG_namespace\n[^\n]*DW_AT_name\t[(]\"inner\"[)]\n\n[^\n]*DW_TAG_subprogram\n[^\n]*DW_AT_low_pc[^\n]*\n[^\n]*DW_AT_high_pc[^\n]*\n[^\n]*DW_AT_frame_base\t[(]DW_OP_call_frame_cfa[)]\n[^\n]*DW_AT_linkage_name\t[(]\"outer::inner::twice\"[)]\n[^\n]*DW_AT_name\t[(]\"twice\"[)]\n[^\n]*DW_AT_decl_file\t[(]\"/work/src/debug_info[.]x\"[)]\n[^\n]*DW_AT_decl_line\t[(]4[)]\n[^\n]*DW_AT_type\t[(]0x[0-9a-f]+ \"i32\"[)]\n[^\n]*DW_AT_external\t[(]true[)]"
    "DW_AT_linkage_name\t[(]\"__cross_static_[0-9]+_sum_to\"[)]\n[^\n]*DW_AT_name\t[(]\"sum_to\"[)]"
    "DW_AT_linkage_name\t[(]\"__cross_static_[0-9]+_pick_G[0-9]+\"[)]\n[^\n]*DW_AT_name\t[(]\"pick\"[)]"
    "DW_AT_name\t[(]\"header_add\"[)]\n[^\n]*DW_AT_decl_file\t[(]\"/work/src/debug_info_header[.]x\"[)]"
    "DW_AT_name\t[(]\"exits\"[)]\n[^\n]*DW_AT_decl_file[^\n]*\n[^\n]*DW_AT_decl_line\t[(]22[)]\n[^\n]*DW_AT_type[^\n]*\n[^\n]*DW_AT_external\t[(]true[)]")
string(REGEX MATCHALL "DW_TAG_subprogram" subprograms "${info}")
string(REGEX MATCHALL "DW_AT_high_pc" ranges "${info}")
list(LENGTH subprograms subprogram_count)
list(LENGTH ranges range_count)
if(NOT subprogram_count EQUAL 6 OR NOT range_count EQUAL 6)
    message(FATAL_ERROR "expected six subprograms with ranges\n${info}")
endif()

# Variables and types: parameters and locals in their frame homes at -O0,
# globals at their symbols, records with members and bit-fields, and
# enumerations with their enumerators.
expect("variables" "${info}"
    "DW_TAG_formal_parameter\n[^\n]*DW_AT_name\t[(]\"index\"[)]\n[^\n]*DW_AT_decl_file[^\n]*\n[^\n]*DW_AT_decl_line\t[(]45[)]\n[^\n]*DW_AT_type\t[(]0x[0-9a-f]+ \"i32\"[)]\n[^\n]*DW_AT_location\t[(]DW_OP_breg7 RSP[+][0-9]+[)]"
    "DW_TAG_variable\n[^\n]*DW_AT_name\t[(]\"local\"[)]\n[^\n]*DW_AT_decl_file[^\n]*\n[^\n]*DW_AT_decl_line\t[(]46[)]\n[^\n]*DW_AT_type\t[(]0x[0-9a-f]+ \"cell\"[)]\n[^\n]*DW_AT_location\t[(]DW_OP_breg7 RSP[+][0-9]+[)]"
    "DW_TAG_variable\n[^\n]*DW_AT_name\t[(]\"y\"[)]\n[^\n]*DW_AT_decl_file[^\n]*\n[^\n]*DW_AT_decl_line\t[(]5[)]\n[^\n]*DW_AT_type[^\n]*\n[^\n]*DW_AT_location\t[(]DW_OP_breg7 RSP[+][0-9]+[)]"
    "DW_TAG_variable\n[^\n]*DW_AT_name\t[(]\"state\"[)]\n[^\n]*DW_AT_decl_file[^\n]*\n[^\n]*DW_AT_decl_line\t[(]43[)]\n[^\n]*DW_AT_type\t[(]0x[0-9a-f]+ \"mode\"[)]\n[^\n]*DW_AT_external\t[(]true[)]\n[^\n]*DW_AT_location\t[(]DW_OP_addr 0x0[)]"
    "DW_TAG_namespace\n[^\n]*DW_AT_name\t[(]\"store\"[)]\n\n[^\n]*DW_TAG_variable\n[^\n]*DW_AT_name\t[(]\"slots\"[)]"
    "DW_TAG_structure_type\n[^\n]*DW_AT_name\t[(]\"cell\"[)]\n[^\n]*DW_AT_byte_size\t[(]8[)]"
    "DW_AT_name\t[(]\"low\"[)]\n[^\n]*DW_AT_type\t[(]0x[0-9a-f]+ \"u32\"[)]\n[^\n]*DW_AT_bit_size\t[(]3[)]\n[^\n]*DW_AT_data_bit_offset\t[(]32[)]"
    "DW_AT_name\t[(]\"high\"[)]\n[^\n]*DW_AT_type[^\n]*\n[^\n]*DW_AT_bit_size\t[(]5[)]\n[^\n]*DW_AT_data_bit_offset\t[(]35[)]"
    "DW_TAG_enumeration_type\n[^\n]*DW_AT_name\t[(]\"mode\"[)]\n[^\n]*DW_AT_byte_size\t[(]1[)]\n[^\n]*DW_AT_type\t[(]0x[0-9a-f]+ \"u8\"[)]\n\n[^\n]*DW_TAG_enumerator\n[^\n]*DW_AT_name\t[(]\"idle\"[)]\n[^\n]*DW_AT_const_value\t[(]0[)]\n\n[^\n]*DW_TAG_enumerator\n[^\n]*DW_AT_name\t[(]\"busy\"[)]\n[^\n]*DW_AT_const_value\t[(]4[)]"
    "DW_TAG_array_type\n[^\n]*DW_AT_type\t[(]0x[0-9a-f]+ \"cell\"[)]\n\n[^\n]*DW_TAG_subrange_type\n[^\n]*DW_AT_count\t[(]2[)]")
# dwarf-lines describes no variables or types.
compile(lines-only.o -g=dwarf-lines -target ${linux} "${input}")
run("dwarf-lines info" "${LLVM_DWARFDUMP}" --debug-info "${OUTPUT}/lines-only.o")
expect("dwarf-lines info" "${stdout}" "DW_TAG_subprogram")
reject("dwarf-lines info" "${stdout}" "DW_TAG_variable" "DW_TAG_formal_parameter"
    "DW_TAG_base_type" "DW_AT_type")

# Frames: one FDE per function in .debug_frame, no .eh_frame without
# -funwind-tables, both with it, and .eh_frame alone for an eh_frame entry.
run("frames" "${LLVM_DWARFDUMP}" --debug-frame "${OUTPUT}/lines.o")
string(REGEX MATCHALL " FDE cie=" fdes "${stdout}")
list(LENGTH fdes fde_count)
if(NOT fde_count EQUAL 6)
    message(FATAL_ERROR "expected six FDEs\n${stdout}")
endif()
sections(listing lines.o)
expect("-g sections" "${listing}" "Name: [.]debug_frame")
reject("-g sections" "${listing}" "Name: [.]eh_frame")
compile(unwind.o -g -funwind-tables -target ${linux} "${input}")
sections(listing unwind.o)
expect("-g -funwind-tables sections" "${listing}" "Name: [.]debug_frame" "Name: [.]eh_frame")
set(model "${OUTPUT}/eh.xm")
file(WRITE "${model}" "debug \"loaded\" {\n    format = \"dwarf\";\n    frame_section = \"eh_frame\";\n}\n")
compile(loaded.o "--model=${model}" -g=loaded -target ${linux} "${input}")
sections(listing loaded.o)
expect("eh_frame entry sections" "${listing}" "Name: [.]eh_frame")
reject("eh_frame entry sections" "${listing}" "Name: [.]debug_frame")
compile(plain.o -target ${linux} "${input}")
sections(listing plain.o)
reject("no -g sections" "${listing}" "Name: [.]debug_" "Name: [.]eh_frame")

# At -O1 `exits` returns early and continues in a later block: the call there
# is described with the body's CFA, not the state after the first return.
compile(exits.o -g -O1 -target ${linux} "${input}")
run("disassembly" "${LLVM_OBJDUMP}" -d --no-show-raw-insn "${OUTPUT}/exits.o")
if(NOT stdout MATCHES "\n +([0-9a-f]+):[ \t]+callq[ \t]+[^\n]*_sum_to>")
    message(FATAL_ERROR "no call to sum_to\n${stdout}")
endif()
math(EXPR call_address "0x${CMAKE_MATCH_1}")
if(NOT stdout MATCHES "\n([0-9a-f]+) <exits>:")
    message(FATAL_ERROR "no function exits\n${stdout}")
endif()
math(EXPR exits_address "0x${CMAKE_MATCH_1}")
run("exits frames" "${LLVM_DWARFDUMP}" --debug-frame "${OUTPUT}/exits.o")
string(REGEX MATCHALL "\n  0x[0-9a-f]+: CFA=[A-Z0-9]+[+-][0-9]+" rows "${stdout}")
set(body "")
set(at_call "")
foreach(row IN LISTS rows)
    string(REGEX MATCH "0x([0-9a-f]+): (CFA=[A-Z0-9]+[+-][0-9]+)" ignored "${row}")
    math(EXPR address "0x${CMAKE_MATCH_1}")
    if(address GREATER exits_address AND body STREQUAL "")
        set(body "${CMAKE_MATCH_2}")
    endif()
    if(address GREATER_EQUAL exits_address AND NOT address GREATER call_address)
        set(at_call "${CMAKE_MATCH_2}")
    endif()
endforeach()
if(body STREQUAL "" OR body STREQUAL "CFA=RSP+8" OR NOT at_call STREQUAL body)
    message(FATAL_ERROR
        "the call in exits is described with '${at_call}', not the body's '${body}'\n${stdout}")
endif()

# A frame without a frame pointer follows RSP through a stack probe loop
# (through R11, which holds the final RSP) and through its epilogue.
run("probe assembly" "${CC}" -S -g -O2 -target ${linux} "${PROBE}"
    -o "${OUTPUT}/probe.s")
file(READ "${OUTPUT}/probe.s" probe)
expect("probe assembly" "${probe}"
    "\n\tpushq\t%r11\n[.]cfi_def_cfa_offset 24\n"
    "\n[.]cfi_def_cfa %r11, [0-9]+\n"
    "\n\tmovq\t%r11, %rsp\n[.]cfi_def_cfa %rsp, [0-9]+\n"
    "\n\taddq\t[$][0-9]+, %rsp\n[.]cfi_def_cfa_offset 8\n")

# -g changes no instruction.
foreach(level -O0 -O2)
    compile(code${level}.o ${level} -target ${linux} "${input}")
    compile(code${level}-g.o -g ${level} -target ${linux} "${input}")
    foreach(object code${level}.o code${level}-g.o)
        run("disassemble ${object}" "${LLVM_OBJDUMP}" -d "${OUTPUT}/${object}")
        string(REGEX REPLACE "^[^\n]*\n" "" ${object} "${stdout}")
    endforeach()
    if(NOT "${code${level}.o}" STREQUAL "${code${level}-g.o}")
        message(FATAL_ERROR "-g changed the ${level} code")
    endif()
endforeach()

# The same command in another tree, with relative paths, gives the same object.
foreach(name one two)
    execute_process(COMMAND "${CC}" -g -c -target ${linux} src/debug_info.x
            -o "${OUTPUT}/${name}.o"
        WORKING_DIRECTORY "${OUTPUT}/${name}" RESULT_VARIABLE status
        ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "relative compilation in ${name} failed\n${stderr}")
    endif()
endforeach()
file(SHA256 "${OUTPUT}/one.o" first)
file(SHA256 "${OUTPUT}/two.o" second)
if(NOT first STREQUAL second)
    message(FATAL_ERROR "-g objects differ between source trees")
endif()

# COFF describes frames in .debug_frame beside the SEH records; Mach-O
# emits lines and the compile unit; MIPS emits the compile unit.
compile(coff.o -g -funwind-tables -target x86_64-w64-windows-gnu "${input}")
verify(coff.o)
sections(listing coff.o)
expect("COFF sections" "${listing}" "Name: [.]debug_info" "Name: [.]debug_frame"
    "Name: [.]debug_line" "Name: [.]pdata")
compile(macho.o -g -target x86_64-apple-darwin "${input}")
verify(macho.o)
sections(listing macho.o)
expect("Mach-O sections" "${listing}" "Name: __debug_info" "Name: __debug_line")
compile(mips.o -g -target mips-unknown-elf "${input}")
verify(mips.o)
run("MIPS info" "${LLVM_DWARFDUMP}" --debug-info "${OUTPUT}/mips.o")
expect("MIPS info" "${stdout}" "DW_TAG_compile_unit" "addr_size = 0x04")
