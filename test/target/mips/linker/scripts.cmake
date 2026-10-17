# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Linker-script features: ENTRY, INCLUDE, PHDRS with FLAGS, PROVIDE, KEEP
# with --gc-sections, SIZEOF, ADDR, ALIGN, AT and LOADADDR, data commands,
# NOLOAD, /DISCARD/, ASSERT, file-name patterns for -b binary input, orphan
# placement, and MEMORY regions with AT>; then script diagnostics.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()
set(sources "${CMAKE_CURRENT_LIST_DIR}")
run_checked("assemble" "${AS}" -target mips-unknown-elf "${sources}/scripts.s" -o "${OUTPUT}/scripts.o")
file(WRITE "${OUTPUT}/blob.bin" "BLOB")
execute_process(COMMAND "${LD}" -T "${sources}/scripts.ld" -L "${sources}" --gc-sections
                        scripts.o -b binary blob.bin -o scripts.elf
                WORKING_DIRECTORY "${OUTPUT}" RESULT_VARIABLE status ERROR_VARIABLE stderr)
if(NOT status EQUAL 0 OR NOT stderr STREQUAL "")
    message(FATAL_ERROR "link failed or warned\n${stderr}")
endif()
set(image "${OUTPUT}/scripts.elf")
run_checked("symbols" "${LLVM_NM}" --format=posix "${image}")
set(symbols "${run_stdout}")
expect("symbols" "${symbols}" "(^|\n)used T 80100038" "(^|\n)kept t 80100040"
       "(^|\n)provided A 12345678" "(^|\n)included_symbol A 42 "
       "(^|\n)_binary_blob_bin_start D 80100104" "(^|\n)_binary_blob_bin_size A 4 ")
foreach(absent "(^|\n)unused " "(^|\n)unused_provided ")
    if(symbols MATCHES "${absent}")
        message(FATAL_ERROR "the image keeps '${absent}'\n${symbols}")
    endif()
endforeach()
run_checked("headers" "${LLVM_READELF}" -h -l -S --wide "${image}")
set(headers "${run_stdout}")
# A NOLOAD section without :PHDR joins no segment.
expect("headers" "${headers}" "Entry point address: +0x80100000"
       "LOAD +0x[0-9a-f]+ 0x80100000 0x80100000 0x00058 0x00058 R E"
       "LOAD +0x[0-9a-f]+ 0x80100100 0x80200000 0x00008 0x00008 RW "
       "[.]text +PROGBITS +80100000 [0-9a-f]+ 000048 "
       "[.]rodata +PROGBITS +80100048 [0-9a-f]+ 000009 "
       "[.]rodata[.]orphan +PROGBITS +80100054 [0-9a-f]+ 000004 "
       "[.]bss +NOBITS +80100108 ")
if(headers MATCHES "[.]comment")
    message(FATAL_ERROR "/DISCARD/ kept .comment\n${headers}")
endif()
foreach(name text_size data_start data_load _end)
    symbol_value("${image}" ${name} ${name})
endforeach()
if(NOT text_size STREQUAL "48" OR NOT data_start STREQUAL "80100100" OR
   NOT data_load STREQUAL "80200000" OR NOT _end STREQUAL "80100118")
    message(FATAL_ERROR "script symbols: ${text_size} ${data_start} ${data_load} ${_end}")
endif()
section_hex("${image}" .rodata rodata)
if(NOT rodata STREQUAL "00000001deadbeef07")
    message(FATAL_ERROR ".rodata is ${rodata}")
endif()
section_hex("${image}" .data data)
if(NOT data STREQUAL "00000003424c4f42")
    message(FATAL_ERROR ".data is ${data}")
endif()
if(GNU_LD)
    # GNU ld reads the script when it meets -T, so -L comes first.
    execute_process(COMMAND "${GNU_LD}" --oformat=elf32-bigmips -L "${sources}" -T "${sources}/scripts.ld"
                            --gc-sections scripts.o -b binary blob.bin -o gnu.elf
                    WORKING_DIRECTORY "${OUTPUT}" RESULT_VARIABLE status ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "GNU ld failed\n${stderr}")
    endif()
    run_checked("GNU ld symbols" "${LLVM_NM}" --format=posix "${OUTPUT}/gnu.elf")
    if(NOT run_stdout STREQUAL symbols)
        message(FATAL_ERROR "symbols differ from GNU ld's\n${run_stdout}\n${symbols}")
    endif()
    foreach(section .text .rodata .rodata.orphan .data)
        section_hex("${image}" ${section} cross)
        section_hex("${OUTPUT}/gnu.elf" ${section} gnu)
        if(NOT cross STREQUAL gnu)
            message(FATAL_ERROR "${section} differs from GNU ld's: ${cross} ${gnu}")
        endif()
    endforeach()
endif()

# MEMORY regions: .data runs in ram and loads after the code in rom.
file(WRITE "${OUTPUT}/memory.s" ".text\n.globl _start\n_start: nop\n.data\n.word 5\n.bss\n.balign 16\n.space 16\n")
run_checked("memory input" "${AS}" -target mips-unknown-elf "${OUTPUT}/memory.s" -o "${OUTPUT}/memory.o")
run_checked("memory" "${LD}" -T "${sources}/memory.ld" --defsym EXTRA=0 "${OUTPUT}/memory.o"
            -o "${OUTPUT}/memory.elf")
run_checked("memory headers" "${LLVM_READELF}" -l -S --wide "${OUTPUT}/memory.elf")
# .bss inherits .data's AT> region, so it would load after .data's image.
expect("memory" "${run_stdout}" "[.]data +PROGBITS +80200000 " "[.]bss +NOBITS +80200010 "
       "LOAD +0x[0-9a-f]+ 0x80200000 0x80100004 0x00004 0x00004 RW "
       "LOAD +0x[0-9a-f]+ 0x80200010 0x80100008 0x00000 0x00010 RW ")
if(GNU_LD)
    set(memory_headers "${run_stdout}")
    run_checked("GNU ld memory" "${GNU_LD}" -T "${sources}/memory.ld" --defsym EXTRA=0
                "${OUTPUT}/memory.o" -o "${OUTPUT}/memory-gnu.elf")
    run_checked("GNU ld memory headers" "${LLVM_READELF}" -l "${OUTPUT}/memory-gnu.elf")
    string(REGEX MATCHALL "LOAD[^\n]*" gnu_loads "${run_stdout}")
    string(REGEX MATCHALL "LOAD[^\n]*" cross_loads "${memory_headers}")
    string(REGEX REPLACE "LOAD +0x[0-9a-f]+" "LOAD" gnu_loads "${gnu_loads}")
    string(REGEX REPLACE "LOAD +0x[0-9a-f]+" "LOAD" cross_loads "${cross_loads}")
    if(NOT gnu_loads STREQUAL cross_loads)
        message(FATAL_ERROR "segments differ from GNU ld's\n${cross_loads}\n${gnu_loads}")
    endif()
endif()
run_failing("region overflow" "${LD}" -T "${sources}/memory.ld" --defsym EXTRA=4
            "${OUTPUT}/memory.o" -o "${OUTPUT}/overflow.elf")
expect("region overflow" "${run_stderr}" "memory[.]ld:17:5: error: section '[.]bss' will not fit in region 'ram'")

# failing_script(LABEL TEXT PATTERN): linking with the script TEXT fails
# with a diagnostic matching PATTERN.
function(failing_script label text pattern)
    file(WRITE "${OUTPUT}/${label}.ld" "${text}")
    run_failing("${label}" "${LD}" -T "${OUTPUT}/${label}.ld" "${OUTPUT}/memory.o"
                -o "${OUTPUT}/${label}.elf")
    expect("${label}" "${run_stderr}" "${pattern}")
endfunction()
failing_script(assert "SECTIONS {\n  .text : { *(.text*) }\n  ASSERT(SIZEOF(.text) < 4, \"text too big\")\n}\n"
               "assert[.]ld:3:3: error: text too big")
failing_script(syntax "SECTIONS {\n  .text { *(.text) }\n}\n" "syntax[.]ld:2:9: error: expected ':'")
failing_script(overlay "SECTIONS {\n  OVERLAY : { .a { *(.a) } }\n}\n"
               "overlay[.]ld:2:3: error: OVERLAY is not supported")
failing_script(subalign "SECTIONS {\n  .text : SUBALIGN(16) { *(.text*) }\n}\n"
               "subalign[.]ld:2:11: error: unsupported output section attribute")
failing_script(command "STARTUP(crt0.o)\n" "command[.]ld:1:1: error: unsupported linker script command 'STARTUP'")
failing_script(undefined "SECTIONS {\n  .text : { *(.text*) }\n  x = missing + 1;\n}\n"
               "undefined[.]ld:3:7: error: undefined symbol 'missing' referenced in expression")
failing_script(backwards "SECTIONS {\n  .text : { *(.text*) . = 0; }\n}\n"
               "backwards[.]ld:2:23: error: cannot move the location counter backwards")
failing_script(phdr "SECTIONS {\n  .text : { *(.text*) } :code\n}\n"
               "phdr[.]ld:2:3: error: program header 'code' is not defined")
