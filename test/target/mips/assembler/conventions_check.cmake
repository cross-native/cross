# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Facts of conventions.s that do not need GNU as to check.
execute_process(COMMAND "${LLVM_READELF}" -S -s --wide "${OBJECT}" OUTPUT_VARIABLE text)
foreach(pattern
        "[.]text[.]f +PROGBITS +0+ [0-9a-f]+ 000020 00 +AX +0 +0 +16\n"
        "[.]reginfo +MIPS_REGINFO +0+ [0-9a-f]+ 000018 01 +0 +0 +4\n"
        "[.]sdata[.]small +PROGBITS +0+ [0-9a-f]+ 000008 00 +WAp +0 +0 +4\n"
        "[.]bss +NOBITS +0+ [0-9a-f]+ 00000c 00 +WA +0 +0 +8\n"
        "[.]init_array +INIT_ARRAY +0+ [0-9a-f]+ 000004 04 +WA "
        "[.]pdr +PROGBITS " "[.]gnu[.]attributes "
        "0+ +0 FILE +LOCAL +DEFAULT +ABS conventions[.]c\n"
        "00000004 +0 NOTYPE +LOCAL +DEFAULT +[0-9]+ label1\n"
        "00000004 +0 NOTYPE +LOCAL +DEFAULT +[0-9]+ label2\n"
        "00000000 +12 OBJECT +LOCAL +DEFAULT +[0-9]+ buffer\n")
    if(NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "sections and symbols lack '${pattern}'\n${text}")
    endif()
endforeach()
if(text MATCHES "[$]L2|[.]debug_line")
    message(FATAL_ERROR "a private label or a line table reached the object\n${text}")
endif()
relocation_lines("${OBJECT}" relocations)
list(FILTER relocations INCLUDE REGEX "^[.]rel[.]text[.]f ")
expect_lines(".text.f relocations" "${relocations}"
    ".rel.text.f 0 R_MIPS_HI16 table;.rel.text.f c R_MIPS_LO16 table;.rel.text.f 4 R_MIPS_HI16 table;.rel.text.f 14 R_MIPS_LO16 table")
