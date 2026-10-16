# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# directives.s gives the bytes, symbols, and relocations below: data and
# string forms, alignment and fill, LEB128 values of a forward label
# difference, .rept, symbol assignments, conditionals, macros with default and
# named arguments, numeric labels, common symbols, .ident, weak references,
# aliases, and section types.

foreach(required AS SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(ASSEMBLER_TEST_SKIP)
    return()
endif()

set(object "${OUTPUT}.o")
run_checked(assembly "${AS}" -target mips-unknown-elf "${SOURCE}" -o "${object}")
section_words("${object}" .text big words)
expect_lines(".text words" "${words}"
    "00000000;00000000;00000000;00000000;12341234;12340000;61626364;7809790a;41420073;003fc000;00c01000;00421bf0;8eb00000;00000000;ff7e3f40;03ac027f;7f7f0300;00000500;00000a00;00001400;00001300;000003ff;ffffffff;fffffd00;00000100;00000100;00000100;00000400;00000500;00000100;00000900;00000200;00000300;00000500;00000400;00008b00;00009300;000000")
section_words("${object}" .comment big words)
expect_lines(".comment words" "${words}" "0043726f;73732074;65737400")
section_words("${object}" .note.test big words)
expect_lines(".note.test words" "${words}" "00000001")

execute_process(COMMAND "${LLVM_READELF}" -S -s --wide "${object}" OUTPUT_VARIABLE text)
foreach(pattern
        "\\.note\\.test +NOTE " "\\.init_array +INIT_ARRAY .* WA "
        "\\.comment +PROGBITS .* MS "
        "00000010 +0 NOTYPE +LOCAL +DEFAULT +[0-9]+ here\n"
        "00000005 +0 NOTYPE +LOCAL +DEFAULT +ABS count\n"
        "00000014 +0 NOTYPE +LOCAL +DEFAULT +ABS twice\n"
        "00000000 +12 OBJECT +LOCAL +DEFAULT +[0-9]+ local_thing\n"
        "00000008 +16 OBJECT +GLOBAL +DEFAULT +COM common_thing\n"
        "00000000 +0 NOTYPE +WEAK +DEFAULT +UND weakling\n"
        "00000010 +0 NOTYPE +GLOBAL +DEFAULT +[0-9]+ alias\n")
    if(NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "sections and symbols lack '${pattern}'\n${text}")
    endif()
endforeach()
if(text MATCHES "\\.L")
    message(FATAL_ERROR "a local label reached the symbol table\n${text}")
endif()

relocation_lines("${object}" actual)
expect_lines(relocations "${actual}"
    ".rel.text 8b R_MIPS_32 .text;.rel.text 8f R_MIPS_32 .text;.rel.text 93 R_MIPS_32 weakling;.rel.data 0 R_MIPS_32 alias;.rel.init_array 0 R_MIPS_32 start")
