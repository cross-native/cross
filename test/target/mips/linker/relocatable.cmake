# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Relocatable output (-r) of o32 (REL) and n64 (RELA) objects: the partial
# link keeps the relocations, against section symbols with moved addends,
# keeps common symbols common and one copy of a COMDAT group, and links to
# the same image as the objects themselves, with the Cross ld and with GNU
# ld when available. Then -r with --gc-sections (rooted by -e or by
# --gc-keep-exported), -d, and .eh_frame editing.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()
set(sources "${CMAKE_CURRENT_LIST_DIR}")
set(script "${OUTPUT}/image.ld")
# Merging same-named sections changes their order relative to others, so
# the script fixes an order the direct and partial links share.
file(WRITE "${script}" "ENTRY(_start)\nSECTIONS {\n  . = START;\n  .text : { *(.text.boot) *(.text.pick) *(.text) }\n  .data : { *(.data) }\n  .bss : { *(.bss) *(COMMON) }\n  /DISCARD/ : { *(.reginfo) *(.MIPS.options) *(.MIPS.abiflags) *(.pdr) }\n}\n")
foreach(variant o32-big o32-little n64-big n64-little)
    string(REPLACE "-" ";" fields "${variant}")
    list(GET fields 0 abi)
    list(GET fields 1 order)
    set(start 0x80100000)
    set(gnu_format "")
    if(abi STREQUAL "o32")
        set(triple mips-unknown-elf)
        set(as_abi -mabi=32)
        if(order STREQUAL "little")
            set(triple mipsel-unknown-elf)
        endif()
    else()
        set(triple mips64-unknown-elf)
        set(as_abi -mabi=64)
        set(start 0xffffffff80100000)
        set(gnu_format --oformat=elf64-bigmips)
        if(order STREQUAL "little")
            set(triple mips64el-unknown-elf)
            set(gnu_format --oformat=elf64-littlemips)
        endif()
    endif()
    set(endian -EB)
    if(order STREQUAL "little")
        set(endian -EL)
    endif()
    set(base "${OUTPUT}/${variant}")
    foreach(part a b)
        run_checked("${variant} ${part}" "${AS}" -target ${triple} ${as_abi}
                    "${sources}/relocatable_${part}.s" -o "${base}-${part}.o")
    endforeach()
    run_checked("${variant} -r" "${LD}" -r --defsym marker=0x1234 "${base}-a.o" "${base}-b.o" -o "${base}-ab.o")
    run_checked("${variant} sections" "${LLVM_READELF}" -h -S -s --wide "${base}-ab.o")
    set(listing "${run_stdout}")
    string(REGEX MATCHALL "\n *\\[ *[0-9]+\\] [.]group " group_sections "${listing}")
    list(LENGTH group_sections group_count)
    expect("${variant} -r" "${listing}" "Type: +REL " "[.]text[.]pick "
           "[0-9]+: [0-9a-f]+ +16 OBJECT +GLOBAL DEFAULT +COM shared"
           "[0-9]+: 0*1234 +0 NOTYPE +GLOBAL DEFAULT +ABS marker")
    if(NOT group_count EQUAL 1)
        message(FATAL_ERROR "${variant}: ${group_count} groups in the -r output\n${listing}")
    endif()
    set(arguments -T "${script}" --defsym START=${start})
    run_checked("${variant} direct" "${LD}" ${arguments} --defsym marker=0x1234 "${base}-a.o" "${base}-b.o"
                -o "${base}-direct.elf")
    run_checked("${variant} partial" "${LD}" ${arguments} "${base}-ab.o" -o "${base}-partial.elf")
    set(images partial)
    if(GNU_LD)
        run_checked("${variant} GNU ld" "${GNU_LD}" ${endian} ${gnu_format} ${arguments} "${base}-ab.o"
                    -o "${base}-gnu.elf")
        list(APPEND images gnu)
    endif()
    foreach(section .text .data)
        section_hex("${base}-direct.elf" ${section} direct)
        foreach(image IN LISTS images)
            section_hex("${base}-${image}.elf" ${section} other)
            if(NOT other STREQUAL direct)
                message(FATAL_ERROR "${variant} ${image}: ${section} differs from the direct link\n${other}\n${direct}")
            endif()
        endforeach()
    endforeach()
    run_checked("${variant} symbols" "${LLVM_NM}" --format=posix "${base}-direct.elf")
    set(direct_symbols "${run_stdout}")
    run_checked("${variant} partial symbols" "${LLVM_NM}" --format=posix "${base}-partial.elf")
    if(NOT run_stdout STREQUAL direct_symbols)
        message(FATAL_ERROR "${variant}: symbols differ\n${run_stdout}\n${direct_symbols}")
    endif()
endforeach()
run_failing("script" "${LD}" -r -T "${script}" "${OUTPUT}/o32-big-a.o" -o "${OUTPUT}/x.o")
expect("script" "${run_stderr}" "ld: error: -r does not take a linker script")

# -r --gc-sections --gc-keep-exported -d: exported symbols are the roots,
# hidden and local code goes, commons become .bss and .scommon space.
file(WRITE "${OUTPUT}/gc.s" ".set noreorder\n.section .text.exported,\"ax\",@progbits\n.globl exported\nexported: jal used\nnop\n"
     ".section .text.used,\"ax\",@progbits\nused: jr $31\nnop\n"
     ".section .text.unused,\"ax\",@progbits\nunused: jr $31\nnop\n"
     ".section .text.hidden,\"ax\",@progbits\n.globl hidden\n.hidden hidden\nhidden: jr $31\nnop\n"
     ".comm shared, 16, 8\n")
run_checked("assemble gc" "${AS}" -target mips-unknown-elf "${OUTPUT}/gc.s" -o "${OUTPUT}/gc.o")
run_checked("-r gc" "${LD}" -r -d --gc-sections --gc-keep-exported "${OUTPUT}/gc.o" -o "${OUTPUT}/gc-r.o")
run_checked("-r gc sections" "${LLVM_READELF}" -S -s -W "${OUTPUT}/gc-r.o")
expect("-r gc sections" "${run_stdout}" "[.]text[.]exported " "[.]text[.]used " "[.]bss +NOBITS "
       "[0-9]+: 0+ +16 OBJECT +GLOBAL DEFAULT +[0-9]+ shared")
if(run_stdout MATCHES "[.]text[.]unused" OR run_stdout MATCHES "[.]text[.]hidden")
    message(FATAL_ERROR "-r --gc-sections kept unexported code\n${run_stdout}")
endif()
if(GNU_LD)
    run_checked("-r gc GNU ld" "${GNU_LD}" -r -d --gc-sections --gc-keep-exported "${OUTPUT}/gc.o" -o "${OUTPUT}/gc-gnu.o")
    run_checked("-r gc GNU ld sections" "${LLVM_READELF}" -S -W "${OUTPUT}/gc-gnu.o")
    string(REGEX MATCHALL "\n +\\[ *[0-9]+\\] [.][^ ]+" gnu_sections "${run_stdout}")
    run_checked("-r gc sections only" "${LLVM_READELF}" -S -W "${OUTPUT}/gc-r.o")
    string(REGEX MATCHALL "\n +\\[ *[0-9]+\\] [.][^ ]+" cross_sections "${run_stdout}")
    list(FILTER gnu_sections EXCLUDE REGEX "[.](rel[.]|symtab|strtab|shstrtab|gnu[.]attributes|reginfo|MIPS|pdr)")
    list(FILTER cross_sections EXCLUDE REGEX "[.](rel[.]|symtab|strtab|shstrtab|gnu[.]attributes|reginfo|MIPS|pdr)")
    list(TRANSFORM gnu_sections REPLACE "\n +\\[ *[0-9]+\\] " "")
    list(TRANSFORM cross_sections REPLACE "\n +\\[ *[0-9]+\\] " "")
    if(NOT gnu_sections STREQUAL cross_sections)
        message(FATAL_ERROR "-r gc sections differ from GNU ld's: ${cross_sections} / ${gnu_sections}")
    endif()
endif()
run_checked("-r gc entry" "${LD}" -r --gc-sections -e exported "${OUTPUT}/gc.o" -o "${OUTPUT}/gc-e.o")
run_failing("-r gc root" "${LD}" -r --gc-sections "${OUTPUT}/gc.o" -o "${OUTPUT}/gc-x.o")
expect("-r gc root" "${run_stderr}" "--gc-sections requires a defined symbol root specified by -e or -u")

# -r edits .eh_frame too: the FDE of collected code goes; the partial link
# matches GNU ld's partial link.
run_checked("assemble frames" "${AS}" -target mips-unknown-elf "${sources}/frames_raw.s" -o "${OUTPUT}/frames.o")
file(WRITE "${OUTPUT}/frames-stub.s" ".set noreorder\n.text\n.globl first\n.globl second\nfirst:\nsecond: jr $31\nnop\n")
run_checked("assemble frame stubs" "${AS}" -target mips-unknown-elf "${OUTPUT}/frames-stub.s" -o "${OUTPUT}/stub.o")
run_checked("-r frames" "${LD}" -r --gc-sections -e caught "${OUTPUT}/frames.o" "${OUTPUT}/stub.o"
            -o "${OUTPUT}/frames-r.o")
find_program(LLVM_DWARFDUMP NAMES llvm-dwarfdump)
if(LLVM_DWARFDUMP)
    run_checked("-r frames contents" "${LLVM_DWARFDUMP}" --eh-frame "${OUTPUT}/frames-r.o")
    string(REGEX MATCHALL " FDE cie=" fdes "${run_stdout}")
    list(LENGTH fdes fde_count)
    if(NOT fde_count EQUAL 1)
        message(FATAL_ERROR "-r kept ${fde_count} FDEs\n${run_stdout}")
    endif()
endif()
if(GNU_LD)
    run_checked("-r frames GNU ld" "${GNU_LD}" -r --gc-sections -e caught "${OUTPUT}/frames.o" "${OUTPUT}/stub.o"
                -o "${OUTPUT}/frames-gnu.o")
    file(WRITE "${OUTPUT}/frames.ld" "ENTRY(caught)\nSECTIONS {\n  . = 0x80100000;\n  .text : { *(.text .text.*) }\n  .eh_frame : { KEEP(*(.eh_frame)) }\n  .gcc_except_table : { *(.gcc_except_table*) }\n  /DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) }\n}\n")
    foreach(partial r gnu)
        run_checked("frames ${partial}" "${LD}" -T "${OUTPUT}/frames.ld" "${OUTPUT}/frames-${partial}.o"
                    -o "${OUTPUT}/frames-${partial}.elf")
    endforeach()
    foreach(section .eh_frame .gcc_except_table .text)
        section_hex("${OUTPUT}/frames-r.elf" ${section} cross)
        section_hex("${OUTPUT}/frames-gnu.elf" ${section} gnu)
        if(NOT cross STREQUAL gnu)
            message(FATAL_ERROR "-r frames: ${section} differs from GNU ld's partial link\n${cross}\n${gnu}")
        endif()
    endforeach()
endif()
