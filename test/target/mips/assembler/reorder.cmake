# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# `.set reorder`: the hazard nops the CPU model's missing interlocks need,
# each branch's delay slot filled by the instruction before it when the two
# may trade places and by a nop otherwise, nops that a following
# `.set noreorder` does not need dropped, labels moved past hazard nops, and
# -O0 and -O1. Where the model's interlocks are GNU as's, GNU as produces
# the same object.

foreach(required AS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(ASSEMBLER_TEST_SKIP)
    return()
endif()

set(cases 0)
# reorder_case(LABEL "OPTIONS" "GNU-OPTIONS" TEXT WORDS): TEXT, with lines
# separated by '|', assembles with OPTIONS to the .text WORDS; GNU as given
# GNU-OPTIONS, unless they are empty, produces the same object.
function(reorder_case label options gnu_options text words)
    string(REPLACE "|" "\n        " text "        .text\n        ${text}")
    set(source "${OUTPUT}.${label}.s")
    file(WRITE "${source}" "${text}\n")
    separate_arguments(split UNIX_COMMAND "${options}")
    run_checked("${label}" "${AS}" ${split} "${source}" -o "${OUTPUT}.${label}.o")
    text_words("${OUTPUT}.${label}.o" big actual)
    expect_lines("${label} .text words" "${actual}" "${words}")
    if(NOT gnu_options STREQUAL "")
        compare_with_gnu("${label}" "${source}" "${options}" "${gnu_options}")
    endif()
    math(EXPR count "${cases} + 1")
    set(cases ${count} PARENT_SCOPE)
endfunction()

set(r3000 "-target mips-unknown-elf -march=r3000")
set(gnu_r3000 "-EB -march=r3000 -mabi=32")
# MIPS I: load, HI/LO, coprocessor-move, and compare delays.
reorder_case(load "${r3000}" "${gnu_r3000}" "lw $2, 0($4)|addu $3, $2, $2|addu $5, $6, $7"
             "8c820000;00000000;00421821;00c72821")
reorder_case(hilo "${r3000}" "${gnu_r3000}" "mfhi $2|mult $3, $4|mflo $5|addu $6, $6, $6|divu $0, $3, $4"
             "00001010;00000000;00000000;00640018;00002812;00c63021;00000000;0064001b")
reorder_case(coprocessor "${r3000}" "${gnu_r3000}"
             "mfc1 $2, $f0|addu $3, $2, $2|mtc0 $2, $12|mfc0 $3, $12|c.lt.s $f0, $f2|1:|bc1t 1b"
             "44020000;00000000;00421821;40826000;00000000;40036000;4602003c;00000000;4501ffff;00000000")
# A branch takes the place of the instruction before it unless that would
# feed it, fill its target's hazard, or move a labelled instruction.
reorder_case(swap "${r3000}" "${gnu_r3000}"
             "addu $5, $6, $7|jr $31|lw $5, 0($6)|jr $31|addu $31, $6, $7|jr $31|addu $5, $6, $7|2:|j 2b|addu $5, $6, $7|jal 2b"
             "03e00008;00c72821;8cc50000;03e00008;00000000;00c7f821;03e00008;00000000;00c72821;08000009;00000000;0c000009;00c72821")
# A nop `.set noreorder` adds for an earlier hazard stays only if the code
# after it needs it.
reorder_case(tentative "${r3000}" "${gnu_r3000}"
             "lw $2, 0($4)|.set noreorder|addu $3, $5, $5|.set reorder|lw $2, 0($4)|.set noreorder|addu $3, $2, $2|.set reorder"
             "8c820000;00a51821;8c820000;00000000;00421821")
# Data after an instruction gets its hazard nops first.
reorder_case(flush "${r3000}" "${gnu_r3000}" "mfhi $2|.word 0x12345678"
             "00001010;00000000;00000000;12345678")
reorder_case(flush-leb "${r3000}" "${gnu_r3000}" "mfhi $2|.uleb128 1000|.byte 0, 0"
             "00001010;00000000;00000000;e8070000")
# -O1 keeps the delay-slot nops; -O0 also keeps every possible hazard nop.
reorder_case(no-swap "${r3000} -O1" "${gnu_r3000} -O1" "addu $5, $6, $7|jr $31"
             "00c72821;03e00008;00000000")
reorder_case(no-optimize "${r3000} -O0" "${gnu_r3000} -O0" "lw $2, 0($4)|addu $5, $6, $7|jr $31"
             "8c820000;00000000;00c72821;03e00008;00000000")

# The VR4300 interlocks loads and coprocessor 0 moves but, in the shipped
# model, not FPU transfers or compares, which get a nop where GNU as, whose
# tables treat the R4300 as interlocked, puts none.
set(vr4300 "-target mips-unknown-elf -march=vr4300")
set(gnu_vr4300 "-EB -march=vr4300 -mabi=32")
reorder_case(vr4300 "${vr4300}" "${gnu_vr4300}"
             "lw $2, 0($4)|addu $3, $2, $2|mfhi $2|mult $3, $4|mfc0 $2, $9|jr $31"
             "8c820000;00421821;00001010;00000000;00000000;00640018;03e00008;40024800")
reorder_case(vr4300-fpu "${vr4300}" "" "mtc1 $2, $f0|add.s $f2, $f0, $f0|c.lt.s $f0, $f2|3:|bc1t 3b"
             "44820000;00000000;46000080;4602003c;00000000;4501ffff;00000000")
# MIPS32 interlocks every hazard: an mfhi fills the delay slot.
reorder_case(mips32 "-target mips-unknown-elf -march=mips32" "-EB -march=mips32 -mabi=32"
             "mfhi $2|jr $31|mflo $3|mult $4, $5"
             "03e00008;00001010;00001812;00850018")

# -mfix-r5900, which -march=r5900 enables: a conditional branch back over at
# most five instructions, none a branch, keeps a nop in its delay slot; a
# loop of six instructions, one with another branch, and an unconditional
# branch still take the instruction before them.
set(r5900_loops "1:|addiu \$4, \$4, -1|addiu \$2, \$2, 1|bne \$4, \$5, 1b|2:|addiu \$5, \$5, 1|addiu \$6, \$6, 1|addiu \$7, \$7, 1|addiu \$8, \$8, 1|addiu \$9, \$9, 1|bne \$5, \$6, 2b|3:|addiu \$5, \$5, 1|addiu \$6, \$6, 1|addiu \$7, \$7, 1|addiu \$8, \$8, 1|addiu \$9, \$9, 1|addiu \$10, \$10, 1|bne \$5, \$6, 3b|4:|addiu \$5, \$5, 1|beq \$5, \$7, 5f|addiu \$7, \$7, 1|bne \$5, \$6, 4b|5:|addiu \$10, \$10, 1|b 5b")
set(r5900_fixed "2484ffff;24420001;1485fffd;00000000;24a50001;24c60001;24e70001;25080001;25290001;14a6fffa;00000000;24a50001;24c60001;24e70001;25080001;25290001;14a6fffa;254a0001;24a50001;10a70003;00000000;14a6fffc;24e70001;1000ffff;254a0001")
set(r5900_unfixed "2484ffff;1485fffe;24420001;24a50001;24c60001;24e70001;25080001;14a6fffb;25290001;24a50001;24c60001;24e70001;25080001;25290001;14a6fffa;254a0001;24a50001;10a70003;00000000;14a6fffc;24e70001;1000ffff;254a0001")
# GNU as records extension facts for the R5900 and the Loongson 3 that the
# Cross model does not, so it is compared on MIPS III with the options.
reorder_case(r5900 "-target mips-unknown-elf -march=r5900" "" "${r5900_loops}" "${r5900_fixed}")
reorder_case(r5900-off "-target mips-unknown-elf -march=r5900 -mno-fix-r5900" ""
             "${r5900_loops}" "${r5900_unfixed}")
reorder_case(fix-r5900 "-target mips-unknown-elf -march=mips3 -mfix-r5900"
             "-EB -march=mips3 -mfix-r5900 -mabi=32" "${r5900_loops}" "${r5900_fixed}")
reorder_case(no-fix-r5900 "-target mips-unknown-elf -march=mips3" "-EB -march=mips3 -mabi=32"
             "${r5900_loops}" "${r5900_unfixed}")
# -mfix-loongson3-llsc, which the Loongson 3 CPUs enable: a SYNC before an
# LL or LLD that does not follow one, and at a local label that a branch
# between an LL and the SC after it targets, in either reorder mode.
set(llsc ".set noreorder|1:|ll \$2, 0(\$4)|bne \$2, \$5, 2f|move \$3, \$6|sc \$3, 0(\$4)|beqz \$3, 1b|nop|2:|jr \$31|nop|.set reorder|sync|3:|ll \$2, 0(\$4)|addu \$3, \$2, \$5|sc \$3, 0(\$4)|beqz \$3, 3b|lld \$3, 0(\$4)|.Lexit:|ll \$2, 0(\$4)|bne \$2, \$5, .Lout|sc \$3, 0(\$4)|.Lout:|addu \$3, \$3, \$3|jr \$31")
set(llsc_fixed "0000000f;c0820000;14450004;00c01825;e0830000;1060fffa;00000000;0000000f;03e00008;00000000;0000000f;c0820000;00451821;e0830000;1060fffc;00000000;0000000f;d0830000;0000000f;c0820000;14450002;00000000;e0830000;0000000f;03e00008;00631821")
set(llsc_unfixed "c0820000;14450004;00c01825;e0830000;1060fffb;00000000;03e00008;00000000;0000000f;c0820000;00451821;e0830000;1060fffc;00000000;d0830000;c0820000;14450002;00000000;e0830000;03e00008;00631821")
reorder_case(loongson3 "-target mips-unknown-elf -march=gs464" "" "${llsc}" "${llsc_fixed}")
reorder_case(loongson3-off "-target mips-unknown-elf -march=gs464 -mno-fix-loongson3-llsc" ""
             "${llsc}" "${llsc_unfixed}")
reorder_case(fix-loongson3 "-target mips-unknown-elf -march=mips3 -mfix-loongson3-llsc"
             "-EB -march=mips3 -mfix-loongson3-llsc -mabi=32" "${llsc}" "${llsc_fixed}")
reorder_case(no-fix-loongson3 "-target mips-unknown-elf -march=mips3" "-EB -march=mips3 -mabi=32"
             "${llsc}" "${llsc_unfixed}")

# A label before an instruction that needs hazard nops moves past them.
set(source "${OUTPUT}.label.s")
file(WRITE "${source}" "        .text\n        lw $2, 0($4)\nafter:\n        addu $3, $2, $2\n")
separate_arguments(options UNIX_COMMAND "${r3000}")
run_checked("label" "${AS}" ${options} "${source}" -o "${OUTPUT}.label.o")
execute_process(COMMAND "${LLVM_READELF}" -s --wide "${OUTPUT}.label.o" OUTPUT_VARIABLE symbols)
if(NOT symbols MATCHES "00000008 +0 NOTYPE +LOCAL +DEFAULT +[0-9]+ after\n")
    message(FATAL_ERROR "the label did not move past the nop\n${symbols}")
endif()
message(STATUS "${cases} reorder cases matched")
