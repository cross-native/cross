# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Floating values take FPRs on every hard-float CPU. FR=0 doubles and the
# singles of MIPS I-IV and FPXX MIPS32 code use even registers; a
# single-precision FPU also uses the odd ones unless -mno-odd-spreg. MIPS I
# saves both halves of a preserved pair, separates a transfer into the odd
# half from the next use of the pair, and moves f64 words directly; floating
# comparisons branch directly into blocks with phis.

foreach(required CC SOURCE SINGLE_SOURCE BRANCH_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(compile name source)
    execute_process(
        COMMAND "${CC}" -S -O2 -fno-eval-calls ${ARGN} "${source}"
                -o "${OUTPUT}.${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name} compilation failed\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}.${name}.s" assembly)
    set(assembly "${assembly}" PARENT_SCOPE)
endfunction()

function(function_body assembly symbol result)
    string(FIND "${assembly}" "\n${symbol}:" start)
    if(start EQUAL -1)
        message(FATAL_ERROR "the assembly does not define ${symbol}")
    endif()
    string(SUBSTRING "${assembly}" ${start} -1 tail)
    string(FIND "${tail}" ".size ${symbol}," length)
    string(SUBSTRING "${tail}" 0 ${length} body)
    set(${result} "${body}" PARENT_SCOPE)
endfunction()

# An odd FPR named by a floating arithmetic, comparison, conversion, or move.
set(odd_arithmetic
    "\n\t[a-z]+[.][a-z.]+\t[^\n]*[$]f([0-9]*[13579])[,\n]")

foreach(cpu r3000 mips2 mips32 mips32r2 vr4300)
    foreach(triple mips mipsel)
        set(name "${cpu}-${triple}")
        compile(${name} "${SOURCE}"
                -target ${triple}-unknown-elf -mabi=o32 -march=${cpu})
        set(even "${assembly}")
        compile(${name}-single "${SINGLE_SOURCE}"
                -target ${triple}-unknown-elf -mabi=o32 -march=${cpu})
        string(APPEND even "${assembly}")
        compile(${name}-branch "${BRANCH_SOURCE}"
                -target ${triple}-unknown-elf -mabi=o32 -march=${cpu})
        string(APPEND even "${assembly}")
        function_body("${even}" fma32 body)
        if(body MATCHES "[\t ](lwc1|swc1)[\t ]" OR
           NOT body MATCHES "mul[.]s" OR NOT body MATCHES "add[.]s")
            message(FATAL_ERROR
                "${name}: f32 operands left their registers\n${body}")
        endif()
        if(even MATCHES "${odd_arithmetic}")
            message(FATAL_ERROR
                "${name}: an FR=0 FPU computes with odd register $f${CMAKE_MATCH_1}\n${even}")
        endif()
        function_body("${even}" from_u16 body)
        if(body MATCHES "bgez")
            message(FATAL_ERROR
                "${name}: a u16 conversion took the unsigned fix-up\n${body}")
        endif()
        function_body("${even}" contacts body)
        if(body MATCHES "bc1[tf]l?\t[^\n]*tmp")
            message(FATAL_ERROR
                "${name}: a floating comparison materialized a boolean for a branch\n${body}")
        endif()
    endforeach()
endforeach()

compile(mips1 "${SOURCE}" -target mips-unknown-elf -mabi=o32 -march=r3000)
# A preserved double's odd half has its own save.
function_body("${assembly}" across_o32 body)
string(REGEX MATCHALL "\n\t[a-z]+[.]d\t[$]f(2[02468]|30)," doubles "${body}")
if(NOT doubles)
    message(FATAL_ERROR "MIPS I across_o32 keeps no double in a preserved pair\n${body}")
endif()
foreach(double IN LISTS doubles)
    string(REGEX REPLACE ".*[$]f([0-9]+),.*" "\\1" even "${double}")
    math(EXPR odd "${even} + 1")
    if(NOT body MATCHES "swc1\t[$]f${odd},")
        message(FATAL_ERROR
            "MIPS I does not save $f${odd}, the odd half of $f${even}\n${body}")
    endif()
endforeach()
# A transfer into an odd half is separated from the next use of its pair,
# and o32 words reach the pair directly.
string(REGEX MATCHALL "mtc1\t[$][a-z0-9]+,[$]f[0-9]*[13579]\n\t[^\n]*"
       transfers "${assembly}")
if(NOT transfers)
    message(FATAL_ERROR "MIPS I moved no word into an odd half\n${assembly}")
endif()
foreach(transfer IN LISTS transfers)
    string(REGEX MATCH "^mtc1\t[$][a-z0-9]+,[$]f([0-9]+)" ignored "${transfer}")
    set(odd "${CMAKE_MATCH_1}")
    math(EXPR even "${odd} - 1")
    if(transfer MATCHES "\n\t[^\n]*[$]f(${even}|${odd})[,\n]" AND
       NOT transfer MATCHES "\n\t(mtc1|lwc1)\t")
        message(FATAL_ERROR "MIPS I uses $f${even} right after a transfer\n${transfer}")
    endif()
endforeach()
function_body("${assembly}" mixed body)
if(NOT body MATCHES "mtc1\t[$]t1,[$]f[0-9]*[13579]\n")
    message(FATAL_ERROR "MIPS I did not move an o32 high word into the odd half\n${body}")
endif()

# MIPS32 Release 2 moves a double's high word with MTHC1/MFHC1; MIPS II
# code, which may not name the odd half, goes through the value's home.
compile(r2 "${SOURCE}" -target mips-unknown-elf -mabi=o32 -march=mips32r2)
function_body("${assembly}" mixed body)
if(NOT body MATCHES "mthc1")
    message(FATAL_ERROR "MIPS32R2 did not move a high word with MTHC1\n${body}")
endif()
compile(mips2 "${SOURCE}" -target mips-unknown-elf -mabi=o32 -march=mips2)
function_body("${assembly}" call_mixed body)
if(NOT body MATCHES "sdc1" OR body MATCHES "mfhc1|mthc1")
    message(FATAL_ERROR "MIPS II did not pass double words through memory\n${body}")
endif()

# The single-precision Allegrex FPU colors odd registers too, and declares
# no MIPS II ISA, under which assemblers reject them in arithmetic.
compile(allegrex "${SINGLE_SOURCE}" -mprofile=psp-allegrex)
if(NOT assembly MATCHES "${odd_arithmetic}" OR
   assembly MATCHES "[.]set mips2")
    message(FATAL_ERROR "Allegrex did not color odd registers\n${assembly}")
endif()
function_body("${assembly}" single_fma body)
if(body MATCHES "[\t ](lwc1|swc1)[\t ]")
    message(FATAL_ERROR "Allegrex f32 operands left their registers\n${body}")
endif()
compile(allegrex-even "${SINGLE_SOURCE}" -mprofile=psp-allegrex
        -mno-odd-spreg)
if(assembly MATCHES "${odd_arithmetic}" OR
   NOT assembly MATCHES "[.]set mips2")
    message(FATAL_ERROR "-mno-odd-spreg Allegrex used odd registers\n${assembly}")
endif()

# -fno-compare-branch-fusion keeps the materialized boolean.
compile(unfused "${BRANCH_SOURCE}" -mprofile=vr4300-o32
        -fno-compare-branch-fusion)
function_body("${assembly}" ge32 body)
if(NOT body MATCHES "bc1[tf]\t[^\n]*tmp")
    message(FATAL_ERROR "-fno-compare-branch-fusion fused a comparison\n${body}")
endif()
