# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# AArch64 assembler errors carry the line and column of the operand at
# fault: immediates out of range, invalid logical and floating-point
# immediates, misaligned and out-of-range offsets, operands that suit no
# form, features that the CPU lacks, and fixups that layout cannot settle.

foreach(required AS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

set(cases 0)
# Assembling `text` fails with each of the `line:column: error: message`
# diagnostics that ARGN lists, in order and nothing else.
function(expect_errors label options text)
    set(source "${OUTPUT}/${label}.s")
    file(WRITE "${source}" "${text}")
    separate_arguments(options UNIX_COMMAND "${options}")
    execute_process(COMMAND "${AS}" -target aarch64-none-elf ${options} "${source}"
                            -o "${OUTPUT}/${label}.o"
                    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    string(REGEX MATCHALL "[^\n]*: error: [^\n]*" errors "${err}")
    set(expected "")
    foreach(item IN LISTS ARGN)
        list(APPEND expected "${source}:${item}")
    endforeach()
    if(status EQUAL 0 OR NOT errors STREQUAL expected)
        string(REPLACE ";" "\n  " shown_expected "${expected}")
        string(REPLACE ";" "\n  " shown_errors "${errors}")
        message(FATAL_ERROR "${label}: expected\n  ${shown_expected}\nbut as reported\n  ${shown_errors}\n${err}")
    endif()
    math(EXPR count "${cases} + 1")
    set(cases ${count} PARENT_SCOPE)
endfunction()

expect_errors(immediates "" [=[
	add x0, x1, #4097
	add x0, x1, #0x1001000
	cmp x0, #-4097
	movz x0, #0x10000
	movz w0, #1, lsl #32
	lsl x0, x1, #64
	tbz w0, #32, .
	ccmp x0, #32, #0, eq
	svc #65536
	mov x0, #0x123456789
	ubfx x0, x1, #60, #8
	add x0, x1, x2, lsl #64
]=]
    "1:14: error: immediate 4097 is not a 12-bit unsigned value, optionally shifted left by 12"
    "2:14: error: immediate 16781312 is not a 12-bit unsigned value, optionally shifted left by 12"
    "3:10: error: immediate -4097 is not a 12-bit unsigned value, optionally shifted left by 12"
    "4:11: error: immediate 65536 is out of range [0, 65535]"
    "5:15: error: the shift of a 32-bit move is lsl #0 or lsl #16"
    "6:14: error: immediate 64 is out of range [0, 63]"
    "7:10: error: immediate 32 is out of range [0, 31]"
    "8:11: error: immediate 32 is out of range [0, 31]"
    "9:6: error: immediate 65536 is out of range [0, 65535]"
    "10:10: error: immediate 0x123456789 has no single-instruction move encoding"
    "11:20: error: the operands of 'ubfx' must satisfy lsb+width <= 64"
    "12:18: error: shift amount 64 is out of range [0, 63]")

expect_errors(bitmasks "" [=[
	and x0, x1, #0x1234
	orr w0, w1, #0x100000000
	eor x0, x1, #0
	tst w2, #-1
	mov w0, #0x12345
	fmov d0, #0.1
	fmov s0, #32.5
]=]
    "1:14: error: immediate 0x1234 is not a 64-bit logical immediate"
    "2:14: error: immediate 0x100000000 is not a 32-bit logical immediate"
    "3:14: error: immediate 0x0 is not a 64-bit logical immediate"
    "4:10: error: immediate 0xffffffffffffffff is not a 32-bit logical immediate"
    "5:10: error: immediate 0x12345 has no single-instruction move encoding"
    "6:11: error: floating-point constant #0.1 has no 8-bit encoding"
    "7:11: error: floating-point constant #32.5 has no 8-bit encoding")

expect_errors(offsets "" [=[
	ldr x0, [x1, #4097]
	ldr x0, [x1, #32768]
	ldrh w0, [x1, #8191]
	ldp x0, x1, [sp, #4]
	stp w0, w1, [x2, #256]
	ldp q0, q1, [x3, #-1040]!
	ldr x0, [x1], #256
	str x0, [x1, #-257]!
	ldr x0, [x1, x2, lsl #2]
	ldrb w0, [x1, w2, uxtw #1]
	ldr x0, [x1, w2]
	ldur x0, [x1, #256]
]=]
    "1:15: error: offset 4097 is not a multiple of 8 in [0, 32760]"
    "2:15: error: offset 32768 is not a multiple of 8 in [0, 32760]"
    "3:16: error: offset 8191 is not a multiple of 2 in [0, 8190]"
    "4:19: error: offset 4 is not a multiple of 8 in [-512, 504]"
    "5:19: error: offset 256 is not a multiple of 4 in [-256, 252]"
    "6:19: error: offset -1040 is not a multiple of 16 in [-1024, 1008]"
    "7:16: error: offset 256 is out of range [-256, 255]"
    "8:15: error: offset -257 is out of range [-256, 255]"
    "9:19: error: the shift amount is 0 or 3"
    "10:20: error: the shift amount is 0"
    "11:10: error: expected uxtw or sxtw after a 32-bit index register"
    "12:16: error: offset 256 is out of range [-256, 255]")

expect_errors(operands "" [=[
	add x0, x1
	add x0, x1, x2, ror #3
	adds sp, x0, #1
	fadd s0, s1, d2
	csel x0, x1, x2, foo
	cset w0, al
	mrs x0, nosuch_el0
	b x0
	frob x0
	ret x0, x1
	ldr x0, [x1, :got_lo12:sym]!
	ldr w0, [x1, :got_lo12:sym]
	movz w0, #:abs_g2:sym
	add x0, x1, :foo:sym
	add x0, x1, :lo12:sym, lsl #12
	movz x0, #:abs_g1:sym, lsl #16
	ldr x0, =0x12345678
]=]
    "1:6: error: 'add' needs 3 operands"
    "2:18: error: expected lsl, lsr, or asr"
    "3:7: error: expected a 64-bit general register, x0-x30 or xzr"
    "4:15: error: expected a 32-bit SIMD and floating-point register, s0-s31"
    "5:19: error: expected a condition code such as eq or lt"
    "6:11: error: expected a condition code other than al and nv"
    "7:10: error: unknown system register 'nosuch_el0'"
    "8:4: error: expected a label"
    "9:2: error: unknown instruction 'frob'"
    "10:10: error: 'ret' takes 1 operand"
    "11:15: error: this memory operand takes no relocation operator"
    "12:15: error: relocation operator ':got_lo12:' does not apply to this operand"
    "13:11: error: relocation operator ':abs_g2:' needs a 64-bit register"
    "14:14: error: unknown relocation operator ':foo:'"
    "15:14: error: relocation operator ':lo12:' takes no lsl #12"
    "16:25: error: a relocation operator gives the shift"
    "17:10: error: literal pool loads (ldr REGISTER, =VALUE) are not supported")

# Features: the CPU's, and those that .arch and .arch_extension select.
expect_errors(features "-march=armv8-a+nofp" [=[
	fadd d0, d1, d2
	ldr q0, [x0]
	crc32b w0, w1, w2
	.arch armv8.1-a
	ldadd x0, x1, [x2]
	crc32b w0, w1, w2
	.arch_extension nolse
	ldadd x0, x1, [x2]
	.arch_extension frob
	.arch armv99-a
	.cpu cortex-a57+nocrc
	crc32b w0, w1, w2
	fadd d0, d1, d2
]=]
    "1:2: error: instruction 'fadd' needs feature 'fp'"
    "2:2: error: instruction 'ldr' needs feature 'fp'"
    "3:2: error: instruction 'crc32b' needs feature 'crc'"
    "8:2: error: instruction 'ldadd' needs feature 'lse'"
    "9:18: error: unknown AArch64 extension 'frob'"
    "10:8: error: unknown AArch64 architecture or CPU 'armv99-a'"
    "12:2: error: instruction 'crc32b' needs feature 'crc'")

# Fixups that layout settles: misaligned and distant branch targets and
# literals, and immediates that do not fit.
expect_errors(fixups "" [=[
	b .+2
	b.eq far
	cbz x0, far
	tbz x0, #1, near
	adr x0, far
	ldr x0, .+6
	add x0, x1, #(end - start)
	movz x0, #(end - start), lsl #16
	ldr x0, [x1, #(end - start)]
start:
	.zero 0x8000
near:
	.zero 0x100000
far:
end:
]=]
    "1:4: error: the target of a branch or literal is not word aligned"
    "2:7: error: the branch target is out of range"
    "3:10: error: the branch target is out of range"
    "4:14: error: the branch target is out of range"
    "5:10: error: the adr target is out of range"
    "6:10: error: the target of a branch or literal is not word aligned"
    "7:14: error: immediate 1081344 is out of range [0, 4095]"
    "8:11: error: immediate 1081344 is out of range [0, 65535]"
    "9:15: error: offset 1081344 is not a multiple of 8 in [0, 32760]")

message(STATUS "${cases} diagnostic cases passed")
