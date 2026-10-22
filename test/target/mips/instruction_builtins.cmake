# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Checks the built-ins that every MIPS instruction table entry gives: pure
# forms optimize like operations and fill delay slots, effectful ones stay
# in order, operands that a form reads and writes start as their inputs,
# extension instructions are words, availability follows each CPU, the
# listing names every built-in or why an entry has none, and the forms that
# no built-in can state are diagnosed.
foreach(required CC SOURCE ERROR_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()
get_filename_component(directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${directory}")

function(compile name)
    execute_process(COMMAND "${CC}" -S -O2 ${ARGN} "${SOURCE}"
                            -o "${OUTPUT}-${name}.s"
        RESULT_VARIABLE status ERROR_VARIABLE errors)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name} compilation failed\n${errors}")
    endif()
    file(READ "${OUTPUT}-${name}.s" text)
    set(text "${text}" PARENT_SCOPE)
endfunction()

function(body text name variable)
    string(FIND "${text}" "\n${name}:\n" begin)
    string(FIND "${text}" "\n.end ${name}\n" end)
    if(begin EQUAL -1 OR end EQUAL -1)
        message(FATAL_ERROR "no assembly for ${name}\n${text}")
    endif()
    math(EXPR length "${end} - ${begin}")
    string(SUBSTRING "${text}" ${begin} ${length} result)
    set(${variable} "${result}\n" PARENT_SCOPE)
endfunction()

function(expect label code pattern)
    if(NOT code MATCHES "${pattern}")
        message(FATAL_ERROR "${label} lacks '${pattern}'\n${code}")
    endif()
endfunction()

function(reject label code pattern)
    if(code MATCHES "${pattern}")
        message(FATAL_ERROR "${label} has '${pattern}'\n${code}")
    endif()
endfunction()

compile(mips32r2 -target mips-unknown-elf -march=mips32r2)
# A pure form fills the return's delay slot.
body("${text}" pure_add code)
expect(pure_add "${code}" "\tjr\t[$]ra\n\taddu\t[$]v0,[$]a0,[$]a1\n")
# Equal pure forms merge and an unused one goes.
body("${text}" merged code)
string(REGEX MATCHALL "\tsubu\t" subtractions "${code}")
list(LENGTH subtractions count)
if(NOT count EQUAL 1)
    message(FATAL_ERROR "merged did not merge its SUBU forms\n${code}")
endif()
reject(merged "${code}" "\tnor\t")
# ADD traps: it stays.
body("${text}" trapping_add code)
expect(trapping_add "${code}" "\tadd\t[$][a-z0-9]+,[$]a0,[$]a1\n")
# Memory operands take the lvalue's displacement.
body("${text}" memory_forms code)
expect(memory_forms "${code}" "\tlw\t[$]v0,4[(][$]a0[)]\n")
expect(memory_forms "${code}" "\tlbu\t[$]([a-z0-9]+),2[(][$]a1[)]\n")
expect(memory_forms "${code}" "\tsw\t[$][a-z0-9]+,12[(][$]a0[)]\n")
# A trap orders the memory accesses around it.
body("${text}" ordered_trap code)
expect(ordered_trap "${code}" "\tsw\t[^\n]*\n(\t[^\n]*\n)*\tteq\t[$]a1,[$]a2\n(\t[^\n]*\n)*\tlw\t")
# MOVN and INS start their destination as its input, which the move gives.
body("${text}" kept_destination code)
expect(kept_destination "${code}" "\tmove\t[$]v0,[$]a2\n\tmovn\t[$]v0,[$]a1,[$]a0\n")
body("${text}" insert_field code)
expect(insert_field "${code}" "\tmove\t[$]v0,[$]a0\n\tins\t[$]v0,[$]a1,4,8\n")
# DIVU's literal $zero.
body("${text}" quotient code)
expect(quotient "${code}" "\tdivu\t[$]zero,[$]a0,[$]a1\n\tmflo\t[$]v0\n")
# A naked handler: coprocessor 0, branch-likely, and a jump.
body("${text}" vector code)
expect(vector "${code}" "\tmfc0\t[$]k0,[$]13\n")
expect(vector "${code}" "\tmtc0\t[$]k0,[$]13\n([^\n]*:\n)*\tehb\n\teret\n")

# MIPS I: LWR reads the register that LWL loaded a delay earlier.
compile(r3000 -target mips-unknown-elf -march=r3000)
body("${text}" unaligned_word code)
expect(unaligned_word "${code}" "\tlwl\t[$]v0,0[(][$]a0[)]\n\tnop\n\tlwr\t[$]v0,3[(][$]a0[)]\n")
reject(unaligned_word "${code}" "\tlwr\t[^\n]*\n\taddiu\t")
reject(r3000 "${text}" "\n(movn|ins|teq|vector):")
body("${text}" signed_immediates code)
expect(signed_immediates "${code}" "\taddiu\t[$][a-z0-9]+,[$]a0,-1\n")
expect(signed_immediates "${code}" "\tslti\t[$][a-z0-9]+,[$][a-z0-9]+,-32768\n")
expect(signed_immediates "${code}" "\taddiu\t[$][a-z0-9]+,[$][a-z0-9]+,-5\n")
expect(signed_immediates "${code}" "\tori\t[$][a-z0-9]+,[$][a-z0-9]+,65535\n")
execute_process(COMMAND "${CC}" -c -O2 -target mips-unknown-elf -march=r3000 "${SOURCE}"
                        -o "${OUTPUT}-r3000.o"
    RESULT_VARIABLE status ERROR_VARIABLE errors)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "r3000 object failed\n${errors}")
endif()

# Allegrex instructions are words with the mnemonic in a comment.
compile(allegrex -mprofile=psp-allegrex)
body("${text}" allegrex_forms code)
expect(allegrex_forms "${code}" "\t[.]word\t0x0085102c\t# max [$]2,[$]4,[$]5\n")
expect(allegrex_forms "${code}" "\t[.]word\t0x0085182d\t# min [$]3,[$]4,[$]5\n")
expect(allegrex_forms "${code}" "\tjr\t[$]31\n\t[.]word\t0x7c021520\t# bitrev [$]2,[$]2\n")
body("${text}" kept_destination code)
expect(allegrex_movn "${code}" "\t[.]word\t0x00a4100b\t# movn [$]2,[$]5,[$]4\n")

# Release 6 forms, by mnemonic.
compile(mips32r6 -target mips-unknown-elf -march=mips32r6)
body("${text}" release6_forms code)
expect(release6_forms "${code}" "\tlsa\t[$]v0,[$]a0,[$]a1,2\n\tjr\t[$]ra\n\tbitswap\t[$]v0,[$]v0\n")
reject(mips32r6 "${text}" "\n(kept_destination|quotient|unaligned_word|vector):")

# Availability: each CPU's forms, the gates of Release 6 and Allegrex.
file(WRITE "${OUTPUT}-query.x"
    "global u32 a() { return $::has_instruction($::_addu); }\n"
    "global u32 b() { return $::has_instruction($::_lwl); }\n"
    "global u32 c() { return $::has_instruction($::_max); }\n"
    "global u32 d() { return $::has_instruction($::_lsa); }\n"
    "global u32 e() { return $::has_instruction($::_li); }\n"
    "global u32 f() { return $::has_instruction($::_add_s); }\n")
foreach(case "-target;mips-unknown-elf;-march=r3000|1 1 0 0 0 1"
             "-target;mips-unknown-elf;-march=mips32r6|1 0 0 1 0 1"
             "-mprofile=psp-allegrex|1 1 1 0 0 1"
             "-target;mips-unknown-elf;-march=vr4300;-msoft-float|1 1 0 0 0 0")
    string(REPLACE "|" ";" parts "${case}")
    list(GET parts -1 expected)
    list(REMOVE_AT parts -1)
    set(flags ${parts})
    execute_process(COMMAND "${CC}" -E ${flags} "${OUTPUT}-query.x" -o -
        RESULT_VARIABLE status OUTPUT_VARIABLE queried ERROR_VARIABLE errors)
    string(REGEX MATCHALL "return ([01])" answers "${queried}")
    string(REPLACE "return " "" answers "${answers}")
    string(REPLACE ";" " " answers "${answers}")
    if(NOT status EQUAL 0 OR NOT answers STREQUAL expected)
        message(FATAL_ERROR "${flags}: queries were '${answers}', not '${expected}'\n${errors}")
    endif()
endforeach()

# The listing names each entry's built-in, or why it has none.
execute_process(COMMAND "${CC}" -target mips-unknown-elf --print-instructions
    RESULT_VARIABLE status OUTPUT_VARIABLE listing ERROR_VARIABLE errors)
foreach(row "$::_addu instruction [mips]"
            "$::_add_s instruction [hard-float]"
            "$::_cvt_d_s instruction [hard-float,!single-float]"
            "$::_max instruction [allegrex]"
            "$::_lsa instruction [mips32r6]"
            "addu rd:gpr32, rs:gpr32, rt:gpr32  [mips]  $::_addu  source: "
            "ror rd:gpr32, rt:gpr32, sa:uimm5  [mips32r2|mips64r2|allegrex]  alias of rotr  $::_ror  source: "
            "li rt:gpr32, value:imm  [mips]  macro  source: "
            "jal target:jump  [mips]  no built-in (it writes 'ra' as it transfers control)  source: "
            "movt rd:gpr, rs:gpr, cc:fcc  [mips4|mips32|mips64,!mips32r6,!mips64r6]  no built-in (its operand 'cc:fcc' cannot be a built-in operand)  source: ")
    string(FIND "${listing}" "${row}" position)
    if(NOT status EQUAL 0 OR position EQUAL -1)
        message(FATAL_ERROR "--print-instructions lacks '${row}'\n${listing}${errors}")
    endif()
endforeach()

# Diagnostics: BUILTIN_ERROR|CPU|pattern.
set(error_cases
    "0|mips32r2|target instruction '[$]::_li' is an assembler macro, not a machine instruction"
    "1|mips32r2|target instruction '[$]::_jal' has no built-in form: it writes 'ra' as it transfers control"
    "2|vr4300|target instruction '[$]::_bc1t' has no built-in form: it branches on a condition that its entry does not state"
    "3|mips32r2|target instruction '[$]::_movt' has no built-in form: its operand 'cc:fcc' cannot be a built-in operand"
    "4|mips32r2|no typed form of target instruction '[$]::_ext' matches these operands"
    "5|mips32r2|target instruction '[$]::_mflo' reads 'lo', which a multiplication, division, call, or branch of the function may change after the built-in that writes it"
    "6|mips32r2|no typed form of target instruction '[$]::_sw' matches these operands"
    "7|mips32r6|target instruction '[$]::_multu' is unavailable with feature 'mips32r6'"
    "8|mips64r6|target instruction '[$]::_daui' has no built-in form: its register operands must satisfy 'rs != 0', which register allocation does not arrange"
    "9|vr4300|naked function writes '[$]hi' in 'mthi [$]a0', outside its clobber contract"
    "10|vr4300|target instruction '[$]::_j' transfers control and is only available in a naked function"
    "11|mips32r2|target instruction '[$]::_max' requires feature 'allegrex'"
    "12|mips32r2|target instruction '[$]::_movabs' is not registered for the selected target")
foreach(case IN LISTS error_cases)
    string(REGEX MATCH "^([0-9]+)[|]([a-z0-9]+)[|](.*)$" ignored "${case}")
    set(index "${CMAKE_MATCH_1}")
    set(cpu "${CMAKE_MATCH_2}")
    set(pattern "${CMAKE_MATCH_3}")
    execute_process(COMMAND "${CC}" -S -O2 -target mips-unknown-elf -march=${cpu}
                            "-DBUILTIN_ERROR=${index}" "${ERROR_SOURCE}"
                            -o "${OUTPUT}-error-${index}.s"
        RESULT_VARIABLE status ERROR_VARIABLE errors)
    if(status EQUAL 0 OR NOT errors MATCHES "${pattern}" OR
       NOT errors MATCHES "instruction_builtins_errors[.]x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "error case ${index} (${cpu}) lacks '${pattern}'\n${errors}")
    endif()
endforeach()
