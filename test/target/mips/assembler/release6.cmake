# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Release 6 in the assembler: the instructions Release 6 removes are
# rejected under `.set mips32r6` and `.set mips64r6` and kept below them,
# its own instructions are rejected below them; compact branches reject
# the registers their encodings reserve; and in `.set reorder` code a
# control transfer never takes a conditional compact branch's forbidden
# slot, which GNU as fills with a nop, also before data, an alignment, and
# the end of the input. GNU as, when installed, produces the same objects.

foreach(required AS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(ASSEMBLER_TEST_SKIP)
    return()
endif()

set(checked 0)
set(cpu -target mips64-unknown-elf -march=mips64r6 -mabi=n64)
# level(INSTRUCTION ACCEPTED REJECTED): INSTRUCTION assembles under each
# `.set` level of ACCEPTED and fails under each of REJECTED.
function(level instruction accepted rejected)
    foreach(kind accepted rejected)
        foreach(isa IN LISTS ${kind})
            set(input "${OUTPUT}.${checked}.s")
            file(WRITE "${input}" ".set noreorder\n.set ${isa}\n        ${instruction}\n1:\n")
            execute_process(COMMAND "${AS}" ${cpu} "${input}" -o "${OUTPUT}.${checked}.o"
                            RESULT_VARIABLE status ERROR_VARIABLE stderr)
            if(kind STREQUAL "accepted" AND NOT status EQUAL 0)
                message(FATAL_ERROR "'${instruction}' is rejected under ${isa}\n${stderr}")
            endif()
            if(kind STREQUAL "rejected" AND
               (status EQUAL 0 OR NOT stderr MATCHES "is not available in|need"))
                message(FATAL_ERROR "'${instruction}' is not rejected under ${isa}\n${stderr}")
            endif()
            math(EXPR checked "${checked} + 1")
        endforeach()
    endforeach()
    set(checked ${checked} PARENT_SCOPE)
endfunction()

set(before "mips32r2;mips64r2")
set(release6 "mips32r6;mips64r6")
foreach(instruction
        "movn $2, $3, $4" "movz $2, $3, $4" "movt $2, $3, $fcc0" "movf $2, $3, $fcc1"
        "movn.s $f0, $f2, $4" "movz.d $f0, $f2, $4" "movt.s $f0, $f2, $fcc0"
        "movf.d $f0, $f2, $fcc0" "mult $4, $5" "multu $4, $5"
        "mfhi $2" "mflo $2" "mthi $2" "mtlo $2" "madd $4, $5"
        "maddu $4, $5" "msub $4, $5" "msubu $4, $5" "lwl $2, 1($4)" "lwr $2, 1($4)"
        "swl $2, 1($4)" "swr $2, 1($4)" "bltzal $4, 1f" "bgezal $4, 1f" "bc1t 1f"
        "bc1f $fcc1, 1f" "beql $4, $5, 1f" "bnel $4, $5, 1f" "beqzl $4, 1f"
        "bnezl $4, 1f" "bltzl $4, 1f" "bgezl $4, 1f" "blezl $4, 1f" "bgtzl $4, 1f"
        "bc1tl 1f" "bc1fl 1f" "c.eq.s $f0, $f2" "c.lt.d $f0, $f2"
        "c.un.s $fcc1, $f0, $f2" "c.ngt.d $f0, $f2" "addi $2, $3, 4" "teqi $4, 5"
        "tnei $4, 5" "tgei $4, 5" "tgeiu $4, 5" "tlti $4, 5" "tltiu $4, 5"
        "mul $2, $3, 100")
    level("${instruction}" "${before}" "${release6}")
endforeach()
foreach(instruction "dmult $4, $5" "dmultu $4, $5" "ldl $2, 1($4)" "ldr $2, 1($4)" "sdl $2, 1($4)"
        "sdr $2, 1($4)" "daddi $2, $3, 4")
    level("${instruction}" "mips64r2" "mips64r6")
endforeach()
foreach(instruction
        "muh $2, $3, $4" "mulu $2, $3, $4" "muhu $2, $3, $4"
        "div $2, $3, $4" "mod $2, $3, $4" "divu $2, $3, $4" "modu $2, $3, $4"
        "seleqz $2, $3, $4" "selnez $2, $3, $4" "sel.d $f0, $f2, $f4"
        "seleqz.s $f0, $f2, $f4" "cmp.lt.s $f0, $f2, $f4" "cmp.sune.d $f0, $f2, $f4"
        "bc1eqz $f2, 1f" "bc1nez $f2, 1f" "maddf.d $f0, $f2, $f4" "rint.s $f0, $f2"
        "class.d $f0, $f2" "min.s $f0, $f2, $f4" "maxa.d $f0, $f2, $f4"
        "lsa $2, $3, $4, 2" "aui $2, $3, 1" "align $2, $3, $4, 1" "bitswap $2, $3" "sigrie 5"
        "auipc $2, 1" "aluipc $2, 1" "addiupc $2, 8" "lapc $2, 1f" "lwpc $2, 1f"
        "bc 1f" "balc 1f" "beqzc $4, 1f" "bnezc $4, 1f" "jic $25, 0" "jialc $25, 0"
        "jrc $31" "beqc $4, $5, 1f" "bnec $4, $5, 1f" "bovc $4, $5, 1f"
        "bnvc $4, $5, 1f" "bltc $4, $5, 1f" "bgec $4, $5, 1f" "bltuc $4, $5, 1f"
        "bgeuc $4, $5, 1f" "bltzc $4, 1f" "bgezc $4, 1f" "blezc $4, 1f"
        "bgtzc $4, 1f" "beqzalc $4, 1f" "bnezalc $4, 1f" "blezalc $4, 1f"
        "bgezalc $4, 1f" "bgtzalc $4, 1f" "bltzalc $4, 1f")
    level("${instruction}" "${release6}" "${before}")
endforeach()
foreach(instruction "dmul $2, $3, $4" "dmuh $2, $3, $4" "ddiv $2, $3, $4"
        "dmod $2, $3, $4" "dlsa $2, $3, $4, 1" "daui $2, $3, 1" "dahi $2, $2, 1"
        "dati $2, $2, 1" "dalign $2, $3, $4, 1" "dbitswap $2, $3" "lwupc $2, 1f"
        "ldpc $2, 1f")
    level("${instruction}" "mips64r6" "mips64r2;mips32r6")
endforeach()

# Registers that a compact branch's encoding reserves for another one.
function(rejects label text pattern)
    set(input "${OUTPUT}.${label}.s")
    file(WRITE "${input}" ".set noreorder\n1:\n        ${text}\n")
    execute_process(COMMAND "${AS}" ${cpu} "${input}" -o "${OUTPUT}.${label}.o"
                    RESULT_VARIABLE status ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: '${text}' is not rejected with '${pattern}'\n${stderr}")
    endif()
    if(GNU_AS)
        execute_process(COMMAND "${GNU_AS}" -march=mips64r6 -mabi=64 "${input}"
                                -o "${OUTPUT}.${label}.gnu.o" RESULT_VARIABLE status
                        ERROR_QUIET)
        if(status EQUAL 0)
            message(FATAL_ERROR "${label}: GNU as accepts '${text}'")
        endif()
    endif()
    math(EXPR count "${checked} + 1")
    set(checked ${count} PARENT_SCOPE)
endfunction()
rejects(beqc-same "beqc $4, $4, 1b" "must satisfy")
rejects(beqc-zero "beqc $0, $4, 1b" "must satisfy")
rejects(bnec-zero "bnec $4, $0, 1b" "must satisfy")
rejects(bltc-same "bltc $4, $4, 1b" "must satisfy")
rejects(bgec-zero "bgec $0, $4, 1b" "must satisfy")
rejects(bltzc-zero "bltzc $0, 1b" "must satisfy")
rejects(beqzc-zero "beqzc $0, 1b" "must satisfy")
rejects(beqzalc-zero "beqzalc $0, 1b" "must satisfy")
rejects(daui-zero "daui $2, $0, 1" "must satisfy")
rejects(dahi-registers "dahi $2, $3, 1" "must satisfy")
rejects(lsa-range "lsa $2, $3, $4, 5" "out of range")
rejects(addiupc-word "addiupc $2, 6" "not a multiple of 4")
# The re-encoded LL, SC, CACHE, and PREF take 9-bit offsets, which GNU as
# extends through a register.
foreach(instruction "ll $2, 1000($4)" "sc $2, -257($4)" "lld $2, 256($4)"
        "cache 1, 1000($4)" "pref 1, -1000($4)")
    set(input "${OUTPUT}.offset-${checked}.s")
    file(WRITE "${input}" ".set noreorder\n        ${instruction}\n")
    execute_process(COMMAND "${AS}" ${cpu} "${input}" -o "${OUTPUT}.offset-${checked}.o"
                    RESULT_VARIABLE status ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "is out of range \\[-256, 255\\]")
        message(FATAL_ERROR "'${instruction}' is not rejected for its offset\n${stderr}")
    endif()
    math(EXPR checked "${checked} + 1")
endforeach()

# The forbidden slot in `.set reorder` code.
set(cases 0)
function(reorder_case label text)
    string(REPLACE "|" "\n        " text "        .text\n${text}")
    set(source "${OUTPUT}.reorder-${label}.s")
    file(WRITE "${source}" "${text}\n")
    run_checked("${label}" "${AS}" -target mips-unknown-elf -march=mips32r6 -O2 "${source}"
                -o "${OUTPUT}.reorder-${label}.o")
    compare_with_gnu("${label}" "${source}" "-target mips-unknown-elf -march=mips32r6 -O2"
                     "-EB -march=mips32r6 -mabi=32 -O2")
    math(EXPR count "${cases} + 1")
    set(cases ${count} PARENT_SCOPE)
endfunction()
reorder_case(transfers "a:|beqzc $4, a|beqzc $5, a|bc a|addiu $2, $2, 1|beqc $4, $5, a|jrc $31|beq $4, $5, a|addiu $3, $3, 1|jr $31|nop")
reorder_case(swaps "a:|beqzc $4, a|addiu $2, $2, 1|beq $5, $6, a|bc a|addiu $3, $3, 1|beq $5, $6, a|balc a|addiu $3, $3, 1|jr $31|bc1eqz $f2, a|beqzc $4, a")
reorder_case(flushes "a:|beqzc $4, a|b:|addiu $2, $2, 1|beqzc $4, a|.word 0|beqzc $4, a|.set noreorder|addiu $2, $2, 1|.set reorder|beqzc $4, a|.align 3|addiu $2, $2, 1|bc a")
message(STATUS "${checked} Release 6 assembler checks and ${cases} reorder cases matched")
