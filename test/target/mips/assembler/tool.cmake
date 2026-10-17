# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The `as` tool: target, profile, ISA, ABI, byte-order, and floating-point
# options select the object's class and MIPS flags; inputs come from files,
# standard input, .include, and .incbin; command errors are diagnosed.

foreach(required AS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(ASSEMBLER_TEST_SKIP)
    return()
endif()
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}/include")
set(source "${OUTPUT}/main.s")
file(WRITE "${source}" "        .set noreorder\n        .text\n        .globl main\nmain:\n        jr $31\n        nop\n")

function(expect label text)
    foreach(pattern IN LISTS ARGN)
        if(NOT text MATCHES "${pattern}")
            message(FATAL_ERROR "${label} lacks '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

# selection(LABEL "AS-OPTIONS" PATTERN...): the readelf header and MIPS
# attributes of main.s assembled with the options match each pattern.
function(selection label options)
    separate_arguments(options UNIX_COMMAND "${options}")
    run_checked("${label}" "${AS}" ${options} "${source}" -o "${OUTPUT}/${label}.o")
    execute_process(COMMAND "${LLVM_READELF}" -h -A "${OUTPUT}/${label}.o" OUTPUT_VARIABLE text)
    expect("${label}" "${text}" ${ARGN})
endfunction()

selection(default "-target mips-unknown-elf" "Class: +ELF32" "big endian"
          "Flags: +0x1001, noreorder, o32" "ISA: MIPS1" "FP ABI: Hard float [(]double precision[)]")
selection(little "-target mips-unknown-elf -EL" "little endian" "Flags: +0x1001")
selection(big "-target mipsel-unknown-elf -EB" "big endian")
selection(wide "-target mips-unknown-elf -64" "Class: +ELF64" "Flags: +0x60000001, noreorder, mips64"
          "CPR1 size: 64")
selection(narrow "-target mips64el-unknown-elf -32" "Class: +ELF32" "little endian" "o32")
selection(vr4300 "-mprofile=vr4300-o32" "Flags: +0x20001101, noreorder, 32bitmode, o32, mips3"
          "GPR size: 32" "FP ABI: Hard float [(]double precision[)]" "FLAGS 1: 00000000")
selection(gnu-isa "-target mips-unknown-elf -mips3" "Flags: +0x20001101" "ISA: MIPS3")
selection(fpxx "-target mips-unknown-elf -march=vr4300 -mfpxx" "FP ABI: Hard float [(]32-bit CPU, Any FPU[)]")
selection(fp64 "-target mips-unknown-elf -march=vr4300 -mfp64"
          "FP ABI: Hard float [(]32-bit CPU, 64-bit FPU[)]" "CPR1 size: 64" "FLAGS 1: 00000001")
selection(soft "-target mips-unknown-elf -msoft-float" "FP ABI: Soft float" "CPR1 size: 0")
selection(nan2008 "-target mips-unknown-elf -march=mips32r2 -mnan=2008"
          "Flags: +0x70001401, noreorder, nan2008, o32, mips32r2" "FLAGS 1: 00000001")
selection(eabi "-target mips-unknown-elf -march=mips32 -mabi=eabi" "Flags: +0x50003001, noreorder, eabi32")
selection(psp "-mprofile=psp-allegrex" "little endian" "Flags: +0x10003001, noreorder, eabi32, mips2"
          "FP ABI: Hard float [(]single precision[)]")
selection(n64 "-mprofile=mips64el-n64" "Class: +ELF64" "little endian" "Flags: +0x60000001")

# The default output is a.out in the working directory.
execute_process(COMMAND "${AS}" -target mips-unknown-elf "${source}" WORKING_DIRECTORY "${OUTPUT}"
                RESULT_VARIABLE status ERROR_VARIABLE stderr)
if(NOT status EQUAL 0 OR NOT EXISTS "${OUTPUT}/a.out")
    message(FATAL_ERROR "as did not write a.out\n${stderr}")
endif()

# Several inputs form one object; - reads standard input.
file(WRITE "${OUTPUT}/second.s" "        .data\n        .globl second\nsecond:\n        .word main\n")
run_checked(inputs "${AS}" -target mips-unknown-elf "${source}" "${OUTPUT}/second.s"
            -o "${OUTPUT}/inputs.o")
execute_process(COMMAND "${LLVM_READELF}" -s -r "${OUTPUT}/inputs.o" OUTPUT_VARIABLE text)
expect(inputs "${text}" "GLOBAL +DEFAULT +[0-9]+ main" "GLOBAL +DEFAULT +[0-9]+ second" "R_MIPS_32 +[0-9a-f]+ +main")
execute_process(COMMAND "${AS}" -target mips-unknown-elf - -o "${OUTPUT}/stdin.o"
                INPUT_FILE "${source}" RESULT_VARIABLE status ERROR_VARIABLE stderr)
if(NOT status EQUAL 0 OR NOT EXISTS "${OUTPUT}/stdin.o")
    message(FATAL_ERROR "as did not read standard input\n${stderr}")
endif()

# .include searches -I directories; .incbin copies a file.
file(WRITE "${OUTPUT}/include/macros.inc" "        .macro twice value\n        .word \\value, \\value\n        .endm\n")
file(WRITE "${OUTPUT}/payload.bin" "PAYLOAD")
file(WRITE "${OUTPUT}/include.s"
     "        .include \"macros.inc\"\n        .data\n        twice 0x01020304\n        .incbin \"payload.bin\"\n")
execute_process(COMMAND "${AS}" -target mips-unknown-elf -I "${OUTPUT}/include" include.s -o include.o
                WORKING_DIRECTORY "${OUTPUT}" RESULT_VARIABLE status ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "include failed\n${stderr}")
endif()
section_words("${OUTPUT}/include.o" .data big words)
list(SUBLIST words 0 3 words)
expect_lines(include "${words}" "01020304;01020304;5041594c")

# expect_failure(LABEL PATTERN ARGUMENT...): as fails with PATTERN on stderr.
function(expect_failure label pattern)
    execute_process(COMMAND "${AS}" ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE stdout
                    ERROR_VARIABLE stderr WORKING_DIRECTORY "${OUTPUT}")
    if(status EQUAL 0 OR NOT stderr MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: expected failure matching '${pattern}'\n${stdout}${stderr}")
    endif()
endfunction()
expect_failure(no-input "as: error: no input files" -target mips-unknown-elf)
expect_failure(unknown "as: error: unknown argument '-frob'" -frob "${source}")
expect_failure(unimplemented-target "is not implemented" -target sparc-unknown-elf "${source}")
expect_failure(gnu-x86 "-EB, -EL, -32, and -64 select MIPS variants" -target x86_64-unknown-linux-gnu -EB "${source}")
expect_failure(pic "MIPS PIC objects [(]-mabicalls[)] are not supported" -target mips-unknown-elf -mabicalls "${source}")
expect_failure(missing "cannot open input file" -target mips-unknown-elf missing.s)

execute_process(COMMAND "${AS}" --help OUTPUT_VARIABLE text RESULT_VARIABLE status)
expect(help "${text}" "^Usage: as " "-EB, -EL")
execute_process(COMMAND "${AS}" --version OUTPUT_VARIABLE text RESULT_VARIABLE status)
expect(version "${text}" "^Cross toolchain")
