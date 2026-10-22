# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Instruction entries: a user model adds an instruction that as assembles,
# that a naked function uses through its built-in, and that a cpu entry
# costs; loading reports each fault of an entry or of the table at its file
# and line, and rejects a model that changes an instruction the compiler
# selects.

foreach(required CC AS FIXTURES OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/../target/mips/assembler/common.cmake")
file(MAKE_DIRECTORY "${OUTPUT}")
set(model "${FIXTURES}/vmix.xm")
set(mips -target mips-unknown-elf -march=vr4300)

function(run_output label variable)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE stdout
                    ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${stdout}\n${stderr}")
    endif()
    set(${variable} "${stdout}" PARENT_SCOPE)
endfunction()

function(expect_text label text pattern)
    string(FIND "${text}" "${pattern}" position)
    if(position EQUAL -1)
        message(FATAL_ERROR "${label} lacks '${pattern}'\n${text}")
    endif()
endfunction()

# as assembles the instruction, its macro, its alias, and a form that a
# value selects.
if(NOT ASSEMBLER_TEST_SKIP)
    commented_words("${FIXTURES}/vmix.s" expected)
    run_checked("as with the model" "${AS}" ${mips} "--model=${model}"
                "${FIXTURES}/vmix.s" -o "${OUTPUT}/vmix.o")
    text_words("${OUTPUT}/vmix.o" big words)
    expect_lines("vmix words" "${words}" "${expected}")
endif()
execute_process(COMMAND "${AS}" ${mips} "${FIXTURES}/vmix.s" -o "${OUTPUT}/without.o"
                RESULT_VARIABLE status ERROR_VARIABLE stderr)
if(status EQUAL 0 OR NOT stderr MATCHES "unknown instruction 'vmix'")
    message(FATAL_ERROR "as without the model accepted vmix\n${stderr}")
endif()

# A naked function and managed code use it as $::_vmix.
run_output("cc -S" assembly "${CC}" ${mips} -O2 "--model=${model}" -S
           "${FIXTURES}/vmix.x" -o -)
expect_text("naked mix" "${assembly}" "\tvmix\t$v0,$a0,$a1\n\tjr\t$ra\n")
string(REGEX MATCHALL "\tvmix\t" uses "${assembly}")
list(LENGTH uses count)
if(NOT count EQUAL 2)
    message(FATAL_ERROR "expected vmix in mix and mixed\n${assembly}")
endif()
if(NOT ASSEMBLER_TEST_SKIP)
    run_checked("cc -c" "${CC}" ${mips} -O2 "--model=${model}" -c "${FIXTURES}/vmix.x"
                -o "${OUTPUT}/vmix-cc.o")
    text_words("${OUTPUT}/vmix-cc.o" big words)
    # vmix $v0, $a0, $a1
    if(NOT "4a042880" IN_LIST words)
        message(FATAL_ERROR "cc -c did not encode vmix: ${words}")
    endif()
endif()
run_output("cc -E" preprocessed "${CC}" ${mips} "--model=${model}" -E "${FIXTURES}/vmix.x"
           -o -)
expect_text("$::has_instruction" "${preprocessed}" "return 1;")

# The listings show it, and cpu entries cost it.
run_output("--print-instructions" listing "${CC}" ${mips} "--model=${model}"
           --print-instructions)
expect_text("--print-instructions" "${listing}" "$::_vmix instruction [mips] volatile\n")
expect_text("--print-instructions" "${listing}"
            "vmix rd:gpr32, rs:gpr32, rt:gpr32  [mips]  $::_vmix  source: ${model}:4\n")
expect_text("--print-instructions" "${listing}"
            "vblend rd:gpr32, rs:gpr32, rt:gpr32  [mips]  alias of vmix  source: ${model}:24\n")
run_output("--print-costs" costs "${CC}" ${mips} "--model=${model}" --print-costs)
expect_text("default costs" "${costs}" "vmix  latency: 4  issue: 1  available: yes\n")
run_output("--print-costs" costs "${CC}" -target mips-unknown-elf -march=vr4300-cop2
           "--model=${model}" --print-costs)
expect_text("cpu costs" "${costs}" "vmix  latency: 6  issue: 2  available: yes\n")

# The model `text` must fail to load with `expected` at `line` of its file.
set(cases 0)
function(expect_rejected label line expected text)
    set(file "${OUTPUT}/${label}.xm")
    file(WRITE "${file}" "${text}")
    execute_process(COMMAND "${CC}" -target mips-unknown-elf "--model=${file}"
                            --print-instructions
                    RESULT_VARIABLE status OUTPUT_QUIET ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "${label}[.]xm:${line}: error: ${expected}")
        message(FATAL_ERROR "${label} did not report '${expected}' at line ${line}\n${stderr}")
    endif()
    math(EXPR count "${cases} + 1")
    set(cases ${count} PARENT_SCOPE)
endfunction()

expect_rejected(operand-type 3 "unknown mips operand type 'gprx'" [=[
instruction "frob" {
    architecture = "mips";
    operands = ["rd:gprx"];
    encoding = "000000 00000 00000 rd:5 00000 111111";
}
]=])
expect_rejected(encoding-width 4 "the encoding has 31 bits; mips instructions have 32" [=[
instruction "frob" {
    architecture = "mips";
    operands = ["rd:gpr"];
    encoding = "000000 00000 0000 rd:5 00000 111111";
}
]=])
expect_rejected(field-overflow 4
    "field 'sa:5' cannot hold the values 0 to 63 of operand 'sa'" [=[
instruction "frob" {
    architecture = "mips";
    operands = ["sa:uimm6"];
    encoding = "000000 00000 00000 00000 sa:5 111111";
}
]=])
expect_rejected(field-slices 4 "the slices of operand 'sa' do not hold each of its 6 bits once" [=[
instruction "frob" {
    architecture = "mips";
    operands = ["sa:uimm6"];
    encoding = "000000 00000 00000 00000 sa[4:0] 11 sa[4] 111";
}
]=])
expect_rejected(field-missing 4 "the encoding does not place operand 'rt'" [=[
instruction "frob" {
    architecture = "mips";
    operands = ["rd:gpr", "rt:gpr"];
    encoding = "000000 00000 00000 rd:5 00000 111111";
}
]=])
expect_rejected(field-relocated 4 "operand 'imm' needs a field of its own in bits 15-0" [=[
instruction "frob" {
    architecture = "mips";
    operands = ["imm:simm16"];
    encoding = "011100 imm:16 0000000000";
}
]=])
expect_rejected(register-field 3 "'rx' is neither an operand nor a register" [=[
instruction "frob" {
    architecture = "mips";
    encoding = "000000 rx:5 00000 00000 00000 111111";
}
]=])
expect_rejected(duplicate-name 1 "duplicate mips instruction 'addu'" [=[
instruction "addu" {
    architecture = "mips";
    operands = ["rd:gpr32", "rs:gpr32"];
    encoding = "000000 rs:5 00000 rd:5 00000 100001";
}
]=])
expect_rejected(duplicate-form 6 "instruction 'frob/b' duplicates the form of 'frob' that 'frob' defines" [=[
instruction "frob" {
    architecture = "mips";
    operands = ["rd:gpr"];
    encoding = "011100 00000 00000 rd:5 00000 111110";
}
instruction "frob/b" {
    architecture = "mips";
    mnemonic = "frob";
    operands = ["rd:gpr"];
    encoding = "011100 00000 00000 rd:5 00000 111111";
}
]=])
# Redefining an instruction that the compiler selects.
expect_rejected(selected-form 1
    "instruction 'mult/three' adds a form to 'mult', which the compiler selects with its shipped forms" [=[
instruction "mult/three" {
    architecture = "mips";
    mnemonic = "mult";
    operands = ["rd:gpr32", "rs:gpr32", "rt:gpr32"];
    encoding = "011100 rs:5 rt:5 rd:5 00000 000010";
    reads = ["rs", "rt"];
    writes = ["rd", "hi", "lo"];
}
]=])
expect_rejected(selected-name 1 "duplicate mips instruction 'mult'" [=[
instruction "mult" {
    architecture = "mips";
    operands = ["rs:gpr", "rt:gpr"];
    encoding = "000000 rs:5 rt:5 00000 00000 011000";
}
]=])
expect_rejected(feature 5 "unknown mips feature 'mips9'" [=[
instruction "frob" {
    architecture = "mips";
    operands = ["rd:gpr"];
    encoding = "011100 00000 00000 rd:5 00000 111111";
    features = ["mips9"];
}
]=])
expect_rejected(hazard 5 "unknown mips hazard class 'slow'" [=[
instruction "frob" {
    architecture = "mips";
    operands = ["rd:gpr"];
    encoding = "011100 00000 00000 rd:5 00000 111111";
    hazards = ["slow"];
}
]=])
expect_rejected(control 4 "control must be 'jump', 'branch', 'exit', or 'trap'" [=[
instruction "frob" {
    architecture = "mips";
    encoding = "011100 00000 00000 00000 00000 111111";
    control = "return";
}
]=])
expect_rejected(alias-property 4 "an alias takes only 'architecture' and 'mnemonic', not 'latency'" [=[
instruction "frob" {
    architecture = "mips";
    alias = "addu";
    latency = 2;
}
]=])
expect_rejected(alias-target 3 "alias of unknown mips instruction 'frobnicate'" [=[
instruction "frob" {
    architecture = "mips";
    alias = "frobnicate";
}
]=])
expect_rejected(expansion 4
    "no mips instruction 'lui' takes the operands of an expansion line of 'frob'" [=[
instruction "frob" {
    architecture = "mips";
    operands = ["rd:gpr", "rs:gpr"];
    expands = ["lui rd, rs"];
}
]=])
expect_rejected(expansion-scratch 5 "'otherwise' loads the one 'imm' operand into [$]at, which it must use" [=[
instruction "frob" {
    architecture = "mips";
    operands = ["rd:gpr", "value:imm"];
    expands = ["addiu rd, rd, value"];
    otherwise = ["addu rd, rd, rd"];
}
]=])
expect_rejected(builtin 6 "operand 'target:jump' cannot be an operand of a machine instruction built-in" [=[
instruction "frob" {
    architecture = "mips";
    operands = ["target:jump"];
    encoding = "011101 target:26";
    control = "jump";
    builtin = "frob";
}
]=])
expect_rejected(condition 5 "condition must be 'equal', 'not-equal', 'less-than-zero', 'greater-equal-zero', 'less-equal-zero', or 'greater-than-zero', on a 'branch'" [=[
instruction "frob" {
    architecture = "mips";
    encoding = "011100 00000 00000 00000 00000 111111";
    control = "jump";
    condition = "equal";
}
]=])
expect_rejected(costs 11 "instruction 'frob/b' states costs, which 'frob' takes from its first form 'frob'" [=[
instruction "frob" {
    architecture = "mips";
    operands = ["rd:gpr"];
    encoding = "011100 00000 00000 rd:5 00000 111110";
}
instruction "frob/b" {
    architecture = "mips";
    mnemonic = "frob";
    operands = ["rd:gpr", "rs:gpr"];
    encoding = "011100 rs:5 00000 rd:5 00000 111111";
    latency = 3;
}
]=])
expect_rejected(constraint 5 "constraint 'target <= 3' names an operand that is neither an immediate nor a register" [=[
instruction "frob" {
    architecture = "mips";
    operands = ["target:branch"];
    encoding = "000100 00000 00000 target:16";
    constraints = ["target <= 3"];
}
]=])
expect_rejected(architecture 2 "architecture 'x86-64' takes no instruction entries" [=[
instruction "frob" {
    architecture = "x86-64";
}
]=])
expect_rejected(cpu-instruction 4 "unknown mips instruction 'frob'" [=[
cpu "c" {
    architecture = "mips";
    inherits = "vr4300";
    instruction "frob" { latency = 2; }
}
]=])
message(STATUS "${cases} faults reported")
