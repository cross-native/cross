# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# AArch64 instruction entries in a user model: as assembles instructions
# that the shipped table lacks, with encoded immediates in slices, a
# relocated page address, a field that holds a negated value, and an
# operand of one value without a field; loading rejects encoders whose
# slices do not tile their values and relocated values that a linker would
# not find, at their file and line.

foreach(required AS CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
find_program(LLVM_OBJCOPY NAMES llvm-objcopy)
find_program(LLVM_READELF NAMES llvm-readelf)

set(model "${OUTPUT}/extra.xm")
file(WRITE "${model}" [=[
# Armv8.4-A's CFINV, with no feature.
instruction "cfinv" {
    architecture = "aarch64";
    encoding = "11010101000000000100 0000 000 11111";
    writes = ["nzcv"];
}

# orr from the zero register with the logical immediate in slices.
instruction "setmask" {
    architecture = "aarch64";
    operands = ["rd:xsp", "imm:bitmask64"];
    encoding = "1 01 100100 imm[12] imm[11:6] imm[5:0] 11111 rd:5";
    writes = ["rd"];
}

# adrp under another name.
instruction "page" {
    architecture = "aarch64";
    operands = ["rd:x", "target:adrp21"];
    encoding = "1 target[1:0] 10000 target[20:2] rd:5";
    writes = ["rd"];
}

# lsl by an immediate as ubfm: immr holds the negated amount.
instruction "shl" {
    architecture = "aarch64";
    operands = ["rd:x", "rn:x", "amount:0..63"];
    encoding = "1 10 100110 1 -amount:6 63-amount:6 rn:5 rd:5";
    reads = ["rn"];
    writes = ["rd"];
}

# fcmp with zero, whose #0.0 needs no field.
instruction "fcmpzero" {
    architecture = "aarch64";
    operands = ["rn:d", "zero:fzero"];
    encoding = "00011110 01 1 00000 00 1000 rn:5 01000";
    features = ["fp"];
    reads = ["rn"];
    writes = ["nzcv"];
}
]=])
set(source "${OUTPUT}/extra.s")
file(WRITE "${source}" [=[
	cfinv
	setmask sp, #0xff00ff00ff00ff00
	page x3, symbol
	shl x4, x5, #3
	fcmpzero d6, #0.0
]=])
execute_process(COMMAND "${AS}" -target aarch64-none-elf "--model=${model}" "${source}"
                        -o "${OUTPUT}/extra.o"
                RESULT_VARIABLE status ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "as with the model failed\n${stderr}")
endif()
if(LLVM_OBJCOPY AND LLVM_READELF)
    execute_process(COMMAND "${LLVM_OBJCOPY}" -O binary --only-section=.text
                            "${OUTPUT}/extra.o" "${OUTPUT}/extra.text")
    file(READ "${OUTPUT}/extra.text" text HEX)
    # cfinv; orr sp, xzr, #0xff00ff00ff00ff00; adrp x3, 0; lsl x4, x5, #3;
    # fcmp d6, #0.0 (little-endian words)
    if(NOT text STREQUAL "1f4000d5ff9f08b203000090a4f07dd3c820601e")
        message(FATAL_ERROR "the model's instructions encode as ${text}")
    endif()
    execute_process(COMMAND "${LLVM_READELF}" -r "${OUTPUT}/extra.o" OUTPUT_VARIABLE relocations)
    if(NOT relocations MATCHES "000000000008 +[0-9a-f]+ R_AARCH64_ADR_PREL_PG_HI21 +[0-9a-f]+ symbol [+] 0")
        message(FATAL_ERROR "page did not take the adrp relocation\n${relocations}")
    endif()
endif()

# The model `text` must fail to load with `expected` at `line` of its file.
set(cases 0)
function(expect_rejected label line expected text)
    set(file "${OUTPUT}/${label}.xm")
    file(WRITE "${file}" "${text}")
    execute_process(COMMAND "${CC}" -target aarch64-none-elf "--model=${file}"
                            --print-instructions
                    RESULT_VARIABLE status OUTPUT_QUIET ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "${label}[.]xm:${line}: error: ${expected}")
        message(FATAL_ERROR "${label} did not report '${expected}' at line ${line}\n${stderr}")
    endif()
    math(EXPR count "${cases} + 1")
    set(cases ${count} PARENT_SCOPE)
endfunction()

expect_rejected(operand-type 3 "unknown aarch64 operand type 'xreg'" [=[
instruction "frob" {
    architecture = "aarch64";
    operands = ["rd:xreg"];
    encoding = "11010101000000000100 0000 000 rd:5";
}
]=])
expect_rejected(bitmask-slices 4 "the slices of operand 'imm' do not hold each of its 13 bits once" [=[
instruction "frob" {
    architecture = "aarch64";
    operands = ["rd:x", "imm:bitmask64"];
    encoding = "1 01 100100 0 imm[11:6] imm[5:0] 11111 rd:5";
}
]=])
expect_rejected(offset-slices 4 "the slices of operand 'address' do not hold each of its 9 bits once" [=[
instruction "frob" {
    architecture = "aarch64";
    operands = ["rt:x", "address:mem64_reg"];
    encoding = "11 111 0 00 01 1 0 address.offset[7:0] 10 address.base:5 rt:5";
}
]=])
expect_rejected(page-placement 4 "operand 'target' needs fields of its own that hold bits 1-0 of its value in bits 30-29 and bits 20-2 in bits 23-5" [=[
instruction "frob" {
    architecture = "aarch64";
    operands = ["rd:x", "target:adrp21"];
    encoding = "1 target:21 10000 rd:5";
}
]=])
expect_rejected(branch-placement 4 "operand 'target' needs a field of its own in bits 23-5" [=[
instruction "frob" {
    architecture = "aarch64";
    operands = ["target:branch19"];
    encoding = "01010100 00000 target:19";
}
]=])
expect_rejected(immediate-placement 4 "operand 'imm' needs a field of its own in bits 21-10" [=[
instruction "frob" {
    architecture = "aarch64";
    operands = ["rd:xsp", "imm:uimm12_lsl12"];
    encoding = "1 0 0 100010 00000 imm:13 rd:5";
}
]=])
expect_rejected(field-overflow 4 "field '-amount:5' cannot hold the values -63 to 0 of operand 'amount'" [=[
instruction "frob" {
    architecture = "aarch64";
    operands = ["rd:x", "amount:0..63"];
    encoding = "1 10 100110 1 0 -amount:5 111111 11111 rd:5";
}
]=])
message(STATUS "${cases} faults reported")
