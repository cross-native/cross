# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# pic.s and pic64.s, SVR4 PIC (abicalls) and TLS code, give the relocations
# of pic-o32.txt and pic-n64.txt in objects flagged PIC and CPIC, with TLS
# symbols for their thread-local labels; GNU as, when installed, gives the
# same objects in each PIC mode and byte order. A PIC `jal` to a local
# symbol defined later, which GNU as relaxes, is diagnosed.

foreach(required AS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(ASSEMBLER_TEST_SKIP)
    return()
endif()

function(expected_lines file result)
    file(STRINGS "${file}" lines REGEX "^[^#]")
    set(${result} "${lines}" PARENT_SCOPE)
endfunction()

set(o32 "${CMAKE_CURRENT_LIST_DIR}/pic.s")
set(n64 "${CMAKE_CURRENT_LIST_DIR}/pic64.s")
foreach(abi o32 n64)
    if(abi STREQUAL "o32")
        set(options -target mips-unknown-elf)
        set(flags "Flags: +0x1007, noreorder, pic, cpic, o32")
        set(tls_symbol "00000004 +0 TLS +GLOBAL +DEFAULT +[0-9]+ tls_global")
    else()
        set(options -target mips64-unknown-elf -march=mips3)
        set(flags "Flags: +0x20000007, noreorder, pic, cpic, mips3")
        set(tls_symbol "0+ +0 TLS +LOCAL +DEFAULT +[0-9]+ tls_var")
    endif()
    set(object "${OUTPUT}.${abi}.o")
    run_checked("${abi} assembly" "${AS}" ${options} "${${abi}}" -o "${object}")
    relocation_lines("${object}" actual)
    expected_lines("${CMAKE_CURRENT_LIST_DIR}/pic-${abi}.txt" expected)
    expect_lines("${abi} relocations" "${actual}" "${expected}")
    execute_process(COMMAND "${LLVM_READELF}" -h -s --wide "${object}" OUTPUT_VARIABLE text)
    foreach(pattern "${flags}" "${tls_symbol}")
        if(NOT text MATCHES "${pattern}")
            message(FATAL_ERROR "${abi} object lacks '${pattern}'\n${text}")
        endif()
    endforeach()
endforeach()

compare_with_gnu("o32 PIC" "${o32}" "-target mips-unknown-elf" "-EB -32 -march=mips1")
compare_with_gnu("o32 PIC -mno-shared" "${o32}" "-target mips-unknown-elf -mno-shared"
                 "-EB -32 -march=mips1 -mno-shared")
compare_with_gnu("o32 PIC -call_nonpic" "${o32}" "-target mips-unknown-elf -call_nonpic"
                 "-EB -32 -march=mips1 -call_nonpic")
compare_with_gnu("o32 PIC little-endian VR4300" "${o32}"
                 "-target mipsel-unknown-elf -march=vr4300 -KPIC" "-EL -32 -march=vr4300 -KPIC")
compare_with_gnu("n64 PIC" "${n64}" "-target mips64-unknown-elf -march=mips3"
                 "-EB -64 -march=mips3")
compare_with_gnu("n64 PIC little-endian" "${n64}" "-target mips64el-unknown-elf -march=mips64"
                 "-EL -64 -march=mips64")

set(source "${OUTPUT}.later.s")
file(WRITE "${source}" "        .abicalls\n        .text\n        .ent g\ng:\n        .frame $sp,32,$31\n        .set noreorder\n        .cpload $25\n        .set reorder\n        addiu $sp,$sp,-32\n        .cprestore 16\n        jal later\n        .end g\nlater:  jr $31\n")
execute_process(COMMAND "${AS}" -target mips-unknown-elf "${source}" -o "${OUTPUT}.later.o"
                RESULT_VARIABLE status ERROR_VARIABLE stderr)
if(status EQUAL 0 OR NOT stderr MATCHES
   "later.s:11:[0-9]+: error: a PIC 'jal' to the local symbol 'later' must follow its definition")
    message(FATAL_ERROR "expected the later local PIC jal diagnostic\n${stderr}")
endif()
