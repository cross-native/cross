# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# -g on MIPS describes line rows, subprograms with their ranges and frame
# bases, variables in their frame homes, inlined calls, and call-frame
# information in .debug_frame under 32- and 64-bit ABIs in both byte orders;
# the instructions do not change.
foreach(required CC SOURCE HEADER FRAMES INLINE ENUMERATIONS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
find_program(LLVM_DWARFDUMP NAMES llvm-dwarfdump REQUIRED)
find_program(LLVM_READOBJ NAMES llvm-readobj REQUIRED)

file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}/src")
configure_file("${SOURCE}" "${OUTPUT}/src/debug_info.x" COPYONLY)
configure_file("${HEADER}" "${OUTPUT}/src/debug_info_header.x" COPYONLY)
set(input "${OUTPUT}/src/debug_info.x")

function(run label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status})\n${stdout}\n${stderr}")
    endif()
    set(stdout "${stdout}" PARENT_SCOPE)
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

function(count label text pattern expected)
    string(REGEX MATCHALL "${pattern}" matches "${text}")
    list(LENGTH matches found)
    if(NOT found EQUAL expected)
        message(FATAL_ERROR "${label}: ${found} matches of '${pattern}', not ${expected}\n${text}")
    endif()
endfunction()

function(verify object)
    run("verify ${object}" "${LLVM_DWARFDUMP}" --verify "${object}")
    expect("verify ${object}" "${stdout}" "No errors[.]")
endfunction()

# The instruction lines of an assembly file, before its debugging sections.
function(code_lines output path)
    file(READ "${path}" text)
    string(FIND "${text}" "\n.section .debug_" cut)
    if(NOT cut EQUAL -1)
        string(SUBSTRING "${text}" 0 ${cut} text)
    endif()
    string(REGEX MATCHALL "\n\t[^\n]*" lines "${text}")
    set(${output} "${lines}" PARENT_SCOPE)
endfunction()

# -g changes no instruction of `source` under `flags`.
function(same_code name source)
    foreach(level -O0 -O2 -Os)
        set(base "${OUTPUT}/${name}${level}")
        run("cc -S ${name} ${level}" "${CC}" -S ${level} ${ARGN} "${source}" -o "${base}.s")
        run("cc -S -g ${name} ${level}" "${CC}" -S -g ${level} ${ARGN} "${source}" -o "${base}-g.s")
        code_lines(plain "${base}.s")
        code_lines(described "${base}-g.s")
        if(NOT plain STREQUAL described)
            message(FATAL_ERROR "-g changed the ${name} ${level} code")
        endif()
    endforeach()
endfunction()

foreach(config
        "o32-be;mips-unknown-elf;o32;4;BigEndian;32-bit"
        "o32-le;mipsel-unknown-elf;o32;4;LittleEndian;32-bit"
        "cross32-be;mips-unknown-elf;cross32;4;BigEndian;32-bit"
        "cross32-le;mipsel-unknown-elf;cross32;4;LittleEndian;32-bit"
        "n64-be;mips64-unknown-elf;n64;8;BigEndian;64-bit"
        "n64-le;mips64el-unknown-elf;n64;8;LittleEndian;64-bit"
        "cross-n64-be;mips64-unknown-elf;cross-n64;8;BigEndian;64-bit"
        "cross-n64-le;mips64el-unknown-elf;cross-n64;8;LittleEndian;64-bit")
    list(GET config 0 name)
    list(GET config 1 triple)
    list(GET config 2 abi)
    list(GET config 3 address)
    list(GET config 4 order)
    list(GET config 5 class)
    set(flags -target ${triple} -mabi=${abi})
    set(object "${OUTPUT}/${name}.o")
    run("cc ${name}" "${CC}" -c -g -O0 ${flags} "${input}" -o "${object}")
    verify("${object}")
    run("headers ${name}" "${LLVM_READOBJ}" --file-headers --sections "${object}")
    expect("headers ${name}" "${stdout}" "Class: ${class}" "DataEncoding: ${order}"
        "Name: [.]debug_frame" "Name: [.]debug_line")
    reject("headers ${name}" "${stdout}" "Name: [.]eh_frame")

    # A row for every line that produces code, the first row after the
    # parameters are stored marked as the end of the prologue, and a row of
    # its own for each delay-slot instruction.
    run("lines ${name}" "${LLVM_DWARFDUMP}" --debug-line "${object}")
    set(lines "${stdout}")
    string(REGEX MATCHALL "\n0x[0-9a-f]+ +[0-9]+ +[0-9]+ +[0-9]+ [^\n]*" rows "${lines}")
    set(seen "")
    foreach(row IN LISTS rows)
        string(REGEX MATCH "0x[0-9a-f]+ +([0-9]+) +[0-9]+ +([0-9]+)" ignored "${row}")
        list(APPEND seen "${CMAKE_MATCH_2}:${CMAKE_MATCH_1}")
    endforeach()
    foreach(line 4 5 6 10 11 14 15 16 17 19 22 23 24 26 27 28 46 47 48)
        if(NOT "0:${line}" IN_LIST seen)
            message(FATAL_ERROR "${name}: no row for debug_info.x:${line}\n${lines}")
        endif()
    endforeach()
    if(NOT "1:2" IN_LIST seen)
        message(FATAL_ERROR "${name}: no row for debug_info_header.x:2\n${lines}")
    endif()
    expect("lines ${name}" "${lines}" "\n0x[0-9a-f]+ +5 +[0-9]+ +0 [^\n]*prologue_end")
    reject("lines ${name}" "${lines}" "\n0x[0-9a-f]+ +(4|45) +[0-9]+ +0 [^\n]*prologue_end")

    # Subprograms with ranges and frame bases; variables in their frame
    # homes, given from the CFA.
    run("info ${name}" "${LLVM_DWARFDUMP}" --debug-info "${object}")
    set(info "${stdout}")
    expect("info ${name}" "${info}" "addr_size = 0x0${address}"
        "DW_AT_frame_base\t[(]DW_OP_call_frame_cfa[)]\n[^\n]*DW_AT_linkage_name\t[(]\"outer::inner::twice\"[)]"
        "DW_TAG_formal_parameter\n[^\n]*DW_AT_name\t[(]\"index\"[)]\n[^\n]*DW_AT_decl_file[^\n]*\n[^\n]*DW_AT_decl_line\t[(]45[)]\n[^\n]*DW_AT_type\t[(]0x[0-9a-f]+ \"i32\"[)]\n[^\n]*DW_AT_location\t[(]DW_OP_fbreg -[0-9]+[)]"
        "DW_TAG_variable\n[^\n]*DW_AT_name\t[(]\"local\"[)]\n[^\n]*DW_AT_decl_file[^\n]*\n[^\n]*DW_AT_decl_line\t[(]46[)]\n[^\n]*DW_AT_type\t[(]0x[0-9a-f]+ \"cell\"[)]\n[^\n]*DW_AT_location\t[(]DW_OP_fbreg -[0-9]+[)]"
        "DW_TAG_variable\n[^\n]*DW_AT_name\t[(]\"state\"[)]\n[^\n]*DW_AT_decl_file[^\n]*\n[^\n]*DW_AT_decl_line\t[(]43[)]\n[^\n]*DW_AT_type\t[(]0x[0-9a-f]+ \"mode\"[)]\n[^\n]*DW_AT_external\t[(]true[)]\n[^\n]*DW_AT_location\t[(]DW_OP_addr 0x0[)]")
    count("subprograms ${name}" "${info}" "DW_TAG_subprogram" 6)
    count("ranges ${name}" "${info}"
        "DW_TAG_subprogram\n[^\n]*DW_AT_low_pc[^\n]*\n[^\n]*DW_AT_high_pc" 6)
    count("frame bases ${name}" "${info}" "DW_AT_frame_base" 6)
    run("frames ${name}" "${LLVM_DWARFDUMP}" --debug-frame "${object}")
    count("frames ${name}" "${stdout}" " FDE cie=" 6)
    expect("frames ${name}" "${stdout}" "Address size: +${address}" "Return address column: 31")

    same_code(${name} "${input}" ${flags})
    same_code(${name}-frames "${FRAMES}" ${flags})
endforeach()

# Every frame is described at every instruction: a frame program's epilogue
# moves the CFA from $fp to $sp before $fp is reloaded, a block a
# shrink-wrapped frame does not cover has the entry state and the epilogue
# the body state, a sibling call's epilogue is bracketed, a variable-length
# array keeps the CFA on $fp, and a realigned frame keeps it on $sp.
file(READ "${OUTPUT}/o32-be-O0-g.s" assembly)
expect("o32 -O0 epilogue" "${assembly}"
    "\n\tmove\t[$]sp,[$]fp\n[.]cfi_def_cfa_register 29\n\tlw\t[$]fp,[0-9]+[(][$]sp[)]\n\tnop\n[.]cfi_restore 30\n")
file(READ "${OUTPUT}/o32-be-frames-O2-g.s" assembly)
string(REGEX MATCH "\nearly:\n.*\n[.]end early\n" early "${assembly}")
expect("shrink-wrapped early" "${early}"
    "\n[.]Lcross[.]mips[.][0-9_]+[.]bb[.][0-9]+:\n[.]cfi_def_cfa_offset 0\n[.]cfi_restore 31\n"
    "\n[.]Lcross[.]mips[.][0-9_]+[.]return:\n[.]cfi_def_cfa_offset 32\n[.]cfi_offset 31, -[0-9]+\n")
string(REGEX MATCH "\nrelay:\n.*\n[.]end relay\n" relay "${assembly}")
expect("sibling call" "${relay}"
    "\n[.]cfi_remember_state\n(.loc[^\n]*\n)?\tlw\t[$]ra,[0-9]+[(][$]sp[)]\n\tnop\n[.]cfi_restore 31\n\tj\tsink\n\taddiu\t[$]sp,[$]sp,[0-9]+\n[.]cfi_restore_state\n")
string(REGEX MATCH "\ndynamic:\n.*\n[.]end dynamic\n" dynamic "${assembly}")
expect("dynamic frame" "${dynamic}"
    "\n\tmove\t[$]fp,[$]sp\n[.]cfi_def_cfa_register 30\n"
    "\n\tmove\t[$]sp,[$]fp\n[.]cfi_def_cfa_register 29\n")
reject("dynamic frame" "${dynamic}" "[.]cfi_offset 29," "[.]cfi_restore 29\n")
string(REGEX MATCH "\naligned:\n.*\n[.]end aligned\n" aligned "${assembly}")
expect("realigned frame" "${aligned}" "\n\tand\t[$]fp,[$]fp,[$]at\n" "[.]cfi_restore 30\n")
reject("realigned frame" "${aligned}" "[.]cfi_def_cfa_register")

# A shrink-wrapped frame's local is described from the CFA, a realigned
# frame's from the realigned $fp.
set(object "${OUTPUT}/frames.o")
run("cc frames" "${CC}" -c -g -O2 -target mips-unknown-elf -mabi=o32 "${FRAMES}" -o "${object}")
verify("${object}")
run("frame homes" "${LLVM_DWARFDUMP}" --debug-info "${object}")
string(REGEX MATCH "DW_AT_name\t[(]\"shrunk\"[)].*" shrunk "${stdout}")
expect("shrink-wrapped home" "${shrunk}"
    "DW_TAG_variable\n[^\n]*DW_AT_name\t[(]\"cell\"[)]\n[^\n]*\n[^\n]*\n[^\n]*\n[^\n]*DW_AT_location\t[(]DW_OP_fbreg -[0-9]+[)]")
string(REGEX MATCH "DW_AT_name\t[(]\"aligned\"[)].*" aligned "${stdout}")
expect("realigned home" "${aligned}"
    "DW_TAG_variable\n[^\n]*DW_AT_name\t[(]\"cell\"[)]\n[^\n]*\n[^\n]*\n[^\n]*\n[^\n]*DW_AT_location\t[(]DW_OP_breg30 FP_64[+][0-9]+[)]")

# With -funwind-tables the frames are in both sections; dwarf-lines
# describes no variables or types.
run("cc unwind" "${CC}" -c -g -funwind-tables -target mips-unknown-elf -mabi=o32
    "${input}" -o "${OUTPUT}/unwind.o")
run("unwind sections" "${LLVM_READOBJ}" --sections "${OUTPUT}/unwind.o")
expect("unwind sections" "${stdout}" "Name: [.]debug_frame" "Name: [.]eh_frame")
run("cc lines-only" "${CC}" -c -g=dwarf-lines -target mipsel-unknown-elf
    "${input}" -o "${OUTPUT}/lines-only.o")
run("lines-only info" "${LLVM_DWARFDUMP}" --debug-info "${OUTPUT}/lines-only.o")
expect("lines-only info" "${stdout}" "DW_TAG_subprogram")
reject("lines-only info" "${stdout}" "DW_TAG_variable" "DW_TAG_formal_parameter"
    "DW_AT_type")

# Calls inlined into calls name their callee and call, nested as the calls
# were, whether their code is one range or several.
foreach(config "mips-unknown-elf;o32" "mips64el-unknown-elf;n64")
    list(GET config 0 triple)
    list(GET config 1 abi)
    set(object "${OUTPUT}/inline-${abi}.o")
    run("cc inline ${abi}" "${CC}" -c -g -O2 -target ${triple} -mabi=${abi}
        "${INLINE}" -o "${object}")
    verify("${object}")
    run("inline ${abi}" "${LLVM_DWARFDUMP}" --debug-info "${object}")
    expect("inline ${abi}" "${stdout}"
        "DW_AT_name\t[(]\"square\"[)]\n[^\n]*DW_AT_decl_file[^\n]*\n[^\n]*DW_AT_decl_line\t[(]10[)]\n[^\n]*DW_AT_type[^\n]*\n[^\n]*DW_AT_inline\t[(]DW_INL_inlined[)]"
        "DW_TAG_inlined_subroutine\n[^\n]*DW_AT_abstract_origin\t[(]0x[0-9a-f]+ \"[^\"]*_twice_square\"[)]\n[^\n]*DW_AT_(low_pc|ranges)[^\n]*\n([^\n]*(DW_AT_high_pc|0x[0-9a-f]+, 0x)[^\n]*\n)+[^\n]*DW_AT_call_file[^\n]*\n[^\n]*DW_AT_call_line\t[(]20[)]\n[^\n]*DW_AT_call_column\t[(]13[)]"
        "DW_AT_abstract_origin\t[(]0x[0-9a-f]+ \"[^\"]*_square\"[)]\n[^\n]*DW_AT_(low_pc|ranges)[^\n]*\n([^\n]*(DW_AT_high_pc|0x[0-9a-f]+, 0x)[^\n]*\n)+[^\n]*DW_AT_call_file[^\n]*\n[^\n]*DW_AT_call_line\t[(]16[)]\n[^\n]*DW_AT_call_column\t[(]24[)]")
    count("inline ${abi}" "${stdout}" "DW_TAG_inlined_subroutine" 3)
    run("inline lines ${abi}" "${LLVM_DWARFDUMP}" --debug-line "${object}")
    expect("inline lines ${abi}" "${stdout}" "\n0x[0-9a-f]+ +11 +[0-9]+ +0 ")
endforeach()

# 128-bit enumerators are their bytes in target order.
run("cc enumerators" "${CC}" -c -g -target mips-unknown-elf "${ENUMERATIONS}"
    -o "${OUTPUT}/enum.o")
verify("${OUTPUT}/enum.o")
run("enumerators" "${LLVM_DWARFDUMP}" --debug-info "${OUTPUT}/enum.o")
expect("enumerators" "${stdout}"
    "\"negative\"[)]\n[^\n]*DW_AT_const_value\t[(]-1[)]"
    "\"lowest\"[)]\n[^\n]*DW_AT_const_value\t[(]-128[)]"
    "\"minus\"[)]\n[^\n]*DW_AT_const_value\t[(]-2[)]"
    "\"large\"[)]\n[^\n]*DW_AT_const_value\t[(]<0x10> 01 23 45 67 89 ab cd ef 01 23 45 67 89 ab cd ef [)]"
    "\"back\"[)]\n[^\n]*DW_AT_const_value\t[(]-3[)]")
