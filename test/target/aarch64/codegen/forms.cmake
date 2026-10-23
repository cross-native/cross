# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The assembly of forms.x: selected instruction forms, frames, call
# sequences, symbol and section directives, and call-frame information.
# The same output assembles to identical objects with the Cross as and
# llvm-mc, which cc -c reproduces, and GNU as accepts it where installed.

foreach(required CC AS SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile name)
    execute_process(COMMAND "${CC}" -target aarch64-none-elf ${ARGN} -S "${SOURCE}"
                            -o "${OUTPUT}/${name}.s"
                    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name}: cc ${ARGN} failed\n${out}${err}")
    endif()
    file(READ "${OUTPUT}/${name}.s" text)
    set(${name} "${text}" PARENT_SCOPE)
endfunction()

# The text of `function` from its label to its .size directive.
function(body text function out)
    string(FIND "${text}" "\n${function}:\n" begin)
    if(begin EQUAL -1)
        message(FATAL_ERROR "no function ${function}")
    endif()
    string(SUBSTRING "${text}" ${begin} -1 rest)
    string(FIND "${rest}" ".size ${function}," end)
    string(SUBSTRING "${rest}" 0 ${end} result)
    set(${out} "${result}" PARENT_SCOPE)
endfunction()

function(expect name function pattern)
    body("${${name}}" ${function} text)
    if(NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${name}: ${function} lacks /${pattern}/\n${text}")
    endif()
endfunction()

function(reject name function pattern)
    body("${${name}}" ${function} text)
    if(text MATCHES "${pattern}")
        message(FATAL_ERROR "${name}: ${function} has /${pattern}/\n${text}")
    endif()
endfunction()

set(r "[0-9]+")
compile(O2 -O2)
compile(O0 -O0)
compile(pic -O2 -fpic)
compile(unwind -O2 -funwind-tables)
compile(sections -O0 -ffunction-sections -fdata-sections)

# Addressing: a scaled index, a multiplied one and a member displacement,
# a signed byte, and a negative displacement.
expect(O2 index_scaled "ldr w${r}, \\[x${r}, x${r}, lsl #2\\]")
expect(O2 index_multiplied "madd x${r}, x${r}, x${r}, x${r}")
expect(O2 index_multiplied "ldr w${r}, \\[x${r}, #4\\]")
expect(O2 load_signed_byte "ldrsb w${r}, \\[x${r}, #3\\]")
expect(O2 store_half "sturh w${r}, \\[x${r}, #-4\\]")
# Division and remainder.
expect(O2 unsigned_remainder "udiv w${r}, w${r}, w${r}\n.*msub w${r}, w${r}, w${r}, w${r}\n")
expect(O2 signed_remainder "sdiv x${r}, x${r}, x${r}\n.*msub x${r}")
# Constants.
expect(O2 wide_constant
       "movz x${r}, #0xdef0\n\tmovk x${r}, #0x9abc, lsl #16\n\tmovk x${r}, #0x5678, lsl #32\n\tmovk x${r}, #0x1234, lsl #48\n")
expect(O2 inverted_constant "movn x${r}, #0xedcb\n")
expect(O2 bitmask_constant "orr x${r}, xzr, #0xff00ff00ff00ff\n")
# Comparisons and selects.
expect(O2 compare_negative "cmn w${r}, #5\n\tcset w${r}, eq\n")
expect(O2 select_less "cmp w${r}, w${r}\n.*csel w${r}, w${r}, w${r}, lo\n")
expect(O2 shifts "lsl w${r}, w${r}, #3\n")
expect(O2 shifts "lsrv w${r}, w${r}, w${r}\n")
expect(O2 extend "sxth x${r}, w${r}\n")
# Globals: a page and its low 12 bits, or the GOT for a preemptible
# symbol of position-independent code.
expect(O2 load_globals "adrp x${r}, g_word\n.*ldr w${r}, \\[x${r}, :lo12:g_word\\]")
expect(O2 load_globals "ldrh w${r}, \\[x${r}, :lo12:[^]]*g_half\\]")
expect(pic load_globals "adrp x${r}, :got:g_word\n.*ldr x${r}, \\[x${r}, :got_lo12:g_word\\]")
expect(pic load_globals "ldrh w${r}, \\[x${r}, :lo12:[^]]*g_half\\]")
# Frames: a frame record and the outgoing area; none for a leaf without
# frame storage.
expect(O2 index_scaled "^\nindex_scaled:\n\tstp x29, x30, \\[sp, #-16\\]!\n\tmov x29, sp\n\tsub sp, sp, #${r}\n")
expect(O2 index_scaled "add sp, sp, #${r}\n\tldp x29, x30, \\[sp\\], #16\n\tret\n")
expect(O0 leaf "^\nleaf:\n[^\n]*\n?\tret\n$")
expect(O2 large_frame "sub sp, sp, #69632\n\tsub sp, sp, #${r}\n")
# aapcs64 passes the ninth and tenth arguments on the stack.
expect(O2 call_c_ten "str x${r}, \\[sp\\]\n.*str x${r}, \\[sp, #8\\]\n.*bl c_ten\n")
# x passes the seventeenth and eighteenth in x19 and x20, which the caller
# saves for its own caller.
expect(O2 call_x_eighteen "stp x29, x30, \\[sp, #-32\\]!\n\tmov x29, sp\n\tstp x19, x20, \\[sp, #16\\]\n")
expect(O2 call_x_eighteen "ldr w19, [^\n]*\n\tldr w20, [^\n]*\n\tbl x_eighteen\n")
expect(O2 call_x_eighteen "ldp x19, x20, \\[sp, #16\\]\n\tldp x29, x30, \\[sp\\], #32\n\tret\n")
# A record result in memory: the caller passes its address in x8, and the
# callee takes it at entry; the argument travels by reference in x0.
expect(O2 call_c_block "ldr x0, [^\n]*\n\tldr x8, [^\n]*\n\tbl c_block\n")
expect(O2 c_block "^\nc_block:\n[^\n]*\n[^\n]*\n[^\n]*\n[^\n]*\n\tstr x8, \\[sp")
# Indirect calls go through x17; tail calls leave the frame first.
expect(O2 call_pointer "blr x17\n")
expect(O2 tail_pointer "ldp x29, x30, \\[sp\\], #16\n\tbr x17\n")
expect(O2 tail_direct "ldp x29, x30, \\[sp\\], #16\n\tb tail_target\n")
reject(O2 tail_direct "\tret\n")
# A function that never returns keeps no incoming register under -O2.
reject(O2 never "stp x29")
expect(O2 never "bl spin\n")
expect(O0 never "stp x29, x30, \\[sp, #-16\\]!")
# Symbols and sections.
if(NOT O2 MATCHES "\n\\.weak weak_function\n" OR NOT O2 MATCHES "\n\\.hidden hidden_function\n" OR
   NOT O2 MATCHES "\n\\.type index_scaled,@function\n")
    message(FATAL_ERROR "symbol directives missing\n${O2}")
endif()
if(NOT sections MATCHES "\\.section \"?\\.text\\.index_scaled\"?,\"ax\",@progbits\n" OR
   NOT sections MATCHES "\\.section \"?\\.data\\.g_word\"?,\"aw\",@progbits\n")
    message(FATAL_ERROR "-ffunction-sections and -fdata-sections sections missing\n${sections}")
endif()
# Call-frame information follows the frame program.
expect(unwind index_scaled
       "\\.cfi_startproc\n\tstp x29, x30, \\[sp, #-16\\]!\n\\.cfi_def_cfa_offset 16\n\\.cfi_offset 29, -16\n\\.cfi_offset 30, -8\n\tmov x29, sp\n\\.cfi_def_cfa_register 29\n")
expect(unwind call_x_eighteen "\\.cfi_offset 19, -16\n\\.cfi_offset 20, -8\n")
expect(unwind index_scaled "\tldp x29, x30, \\[sp\\], #16\n\\.cfi_def_cfa 31, 0\n")

# Assemblers agree on the output, and cc -c reproduces the Cross as object.
find_program(PYTHON NAMES python3 python)
find_program(LLVM_MC NAMES llvm-mc)
find_program(GNU_AS NAMES aarch64-linux-gnu-as aarch64-none-elf-as)
foreach(name O2 O0 pic unwind sections)
    execute_process(COMMAND "${AS}" -target aarch64-none-elf "${OUTPUT}/${name}.s"
                            -o "${OUTPUT}/${name}.o"
                    RESULT_VARIABLE status ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "as rejected the ${name} assembly\n${err}")
    endif()
    if(PYTHON AND LLVM_MC)
        execute_process(COMMAND "${LLVM_MC}" --triple=aarch64 --filetype=obj "${OUTPUT}/${name}.s"
                                -o "${OUTPUT}/${name}.mc.o"
                        RESULT_VARIABLE status ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "llvm-mc rejected the ${name} assembly\n${err}")
        endif()
        execute_process(COMMAND "${PYTHON}" "${CMAKE_CURRENT_LIST_DIR}/../assembler/compare.py"
                                "${OUTPUT}/${name}.o" "${OUTPUT}/${name}.mc.o"
                        RESULT_VARIABLE status OUTPUT_VARIABLE differences)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "as and llvm-mc objects of ${name} differ:\n${differences}")
        endif()
    endif()
    if(GNU_AS)
        execute_process(COMMAND "${GNU_AS}" "${OUTPUT}/${name}.s" -o "${OUTPUT}/${name}.gnu.o"
                        RESULT_VARIABLE status ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "GNU as rejected the ${name} assembly\n${err}")
        endif()
    endif()
endforeach()
execute_process(COMMAND "${CC}" -target aarch64-none-elf -O2 -c "${SOURCE}"
                        -o "${OUTPUT}/direct.o"
                RESULT_VARIABLE status ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cc -c failed\n${err}")
endif()
file(SHA256 "${OUTPUT}/direct.o" direct)
file(SHA256 "${OUTPUT}/O2.o" assembled)
if(NOT direct STREQUAL assembled)
    message(FATAL_ERROR "cc -c and as of cc -S give different objects")
endif()
