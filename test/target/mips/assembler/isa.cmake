# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Each instruction is accepted from the ISA levels that have it, as `.set`
# selects them, and rejected below; a CPU extension follows -march.

foreach(required AS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
get_filename_component(directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${directory}")

set(checked 0)
# level(INSTRUCTION ACCEPTED REJECTED [AS-OPTION...]): INSTRUCTION assembles
# under each `.set` level of ACCEPTED and fails under each of REJECTED.
function(level instruction accepted rejected)
    foreach(kind accepted rejected)
        foreach(isa IN LISTS ${kind})
            set(input "${OUTPUT}.${checked}.s")
            file(WRITE "${input}" ".set noreorder\n.set ${isa}\n        ${instruction}\n1:\n")
            execute_process(COMMAND "${AS}" ${ARGN} "${input}" -o "${OUTPUT}.${checked}.o"
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

set(cpu -target mips-unknown-elf -march=mips64)
level("ll $2, 0($3)" "mips2;mips32" "mips1" ${cpu})
level("ldc1 $f2, 0($3)" "mips2;mips5" "mips1" ${cpu})
level("beql $2, $3, 1f" "mips2;mips64" "mips1" ${cpu})
level("trunc.w.s $f0, $f2" "mips2;mips32" "mips1" ${cpu})
level("dsll $2, $3, 1" "mips3;mips64" "mips2;mips32r2" ${cpu})
level("eret" "mips3;mips32" "mips2" ${cpu})
level("movn $2, $3, $4" "mips4;mips32" "mips3" ${cpu})
level("bc1t $fcc1, 1f" "mips4;mips32" "mips3" ${cpu})
level("clz $2, $3" "mips32;mips64" "mips5" ${cpu})
level("cvt.s.l $f0, $f2" "mips3;mips32r2" "mips32" ${cpu})
level("rotr $2, $3, 1" "mips32r2;mips64r2" "mips32;mips64" ${cpu})
level("mfhc1 $2, $f2" "mips32r2" "mips5;mips32" ${cpu})
level("dclz $2, $3" "mips64" "mips32r2;mips5" ${cpu})
level("wsbw $2, $3" "mips2" "" -target mipsallegrexel-sony-psp-elf -march=allegrex)
level("wsbw $2, $3" "" "mips2;mips32r2" ${cpu})
message(STATUS "${checked} ISA checks matched")
