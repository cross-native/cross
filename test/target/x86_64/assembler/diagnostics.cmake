# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The x86-64 assembler diagnoses malformed instructions and operands with a
# source-located message, and never writes an object.

foreach(required AS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

# case(LABEL "assembly" PATTERN): the input must be rejected with a message
# matching PATTERN.
# An optional fourth argument names another target triple.
function(case label text pattern)
    set(triple x86_64-unknown-linux-gnu)
    if(ARGC GREATER 3)
        set(triple "${ARGV3}")
    endif()
    set(src "${OUTPUT}/${label}.s")
    file(WRITE "${src}" "${text}\n")
    execute_process(COMMAND "${AS}" -target ${triple} "${src}" -o "${OUTPUT}/${label}.o"
                    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc EQUAL 0)
        message(FATAL_ERROR "${label}: expected an error, assembled successfully")
    endif()
    if(NOT "${out}${err}" MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: message lacks '${pattern}'\n${out}${err}")
    endif()
endfunction()

case(unknown_insn "	frobnicate %rax, %rbx" "unknown instruction")
case(bad_register "	movq %rax, %rqx" "unknown register")
case(bad_operands "	addq %xmm0, %rax" "invalid operands")
case(ambiguous_size "	add $1, (%rax)" "ambiguous operand size")
case(bad_scale "	movq (%rax,%rbx,3), %rcx" "scale factor")
case(bad_specifier "	movq x@NOPE(%rip), %rax" "unknown relocation specifier")
case(seh_outside "	.seh_pushreg %rbp" "seh_proc")
case(bad_mask "vaddps %zmm1, %zmm2, %zmm3 {%k0}" "mask register")
# A Mach-O label the linker sees would start an atom inside the frame.
case(macho_frame_label "_f:\n\t.cfi_startproc\n\t.globl _g\n_g:\n\tretq\n\t.cfi_endproc"
     "macho_frame_label.s:4:1: error: non-private labels cannot appear between"
     x86_64-apple-darwin)
message(STATUS "diagnostics checks passed")
