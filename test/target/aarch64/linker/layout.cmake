# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The default AArch64 layout and the target-independent options: without
# a script, ld lays out the objects of each assembler as GNU ld does without
# RELRO (-z norelro), also with --gc-sections and --eh-frame-hdr, in the
# same sections, contents, and program headers; -m and --oformat name the
# AArch64 output; -r output links to the same image; -z max-page-size,
# -s, and -Map apply.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()

function(same_headers label first second)
    foreach(image first second)
        run_checked("headers of ${${image}}" "${PYTHON}" "${images}" headers "${${image}}")
        set(${image}_headers "${run_stdout}")
    endforeach()
    if(NOT first_headers STREQUAL second_headers)
        message(FATAL_ERROR "${label}: program headers differ\n${first_headers}\n${second_headers}")
    endif()
endfunction()

set(assemblers cross mc)
if(GNU_AS)
    list(APPEND assemblers gnu)
endif()
foreach(assembler IN LISTS assemblers)
    set(object "${OUTPUT}/layout-${assembler}.o")
    assemble(${assembler} "${sources}/layout.s" "${object}")
    set(variant 0)
    foreach(options "" "--gc-sections" "--eh-frame-hdr" "--gc-sections;--eh-frame-hdr")
        set(base "${OUTPUT}/${assembler}-${variant}")
        math(EXPR variant "${variant} + 1")
        run_checked("ld ${options} of the ${assembler} object" "${LD}" ${options} "${object}" -o "${base}.elf")
        if(GNU_LD)
            run_checked("GNU ld ${options}" "${GNU_LD}" -z norelro ${options} "${object}" -o "${base}-gnu.elf")
            same_images("ld ${options} and GNU ld for the ${assembler} object" "${base}.elf" "${base}-gnu.elf")
            same_headers("ld ${options} and GNU ld for the ${assembler} object" "${base}.elf" "${base}-gnu.elf")
        endif()
    endforeach()
endforeach()
set(object "${OUTPUT}/layout-cross.o")
set(image "${OUTPUT}/cross-0.elf")

# --gc-sections removes the unused function and its unwind entry.
run_checked("llvm-readelf -s" "${LLVM_READELF}" -s -W "${OUTPUT}/cross-1.elf")
if(run_stdout MATCHES " unused\n")
    message(FATAL_ERROR "--gc-sections kept 'unused'\n${run_stdout}")
endif()
run_checked("ld --gc-sections --print-gc-sections" "${LD}" --gc-sections --print-gc-sections "${object}"
            -o "${OUTPUT}/printed.elf")
expect("--print-gc-sections" "${run_stderr}" "removing unused section '\\.text\\.unused'")

# The emulations and the output format name the default output.
foreach(option "-m;aarch64linux" "-m;aarch64elf" "--oformat=elf64-littleaarch64" "-EL")
    run_checked("ld ${option}" "${LD}" ${option} "${object}" -o "${OUTPUT}/format.elf")
    same_images("ld ${option}" "${image}" "${OUTPUT}/format.elf")
endforeach()

# A relocatable link of two objects links to the image of both.
assemble(cross "${sources}/symbols.s" "${OUTPUT}/symbols.o")
run_checked("ld -r" "${LD}" -r "${object}" "${OUTPUT}/symbols.o" -o "${OUTPUT}/combined.o")
run_checked("ld of the -r output" "${LD}" "${OUTPUT}/combined.o" -o "${OUTPUT}/combined.elf")
run_checked("ld of both objects" "${LD}" "${object}" "${OUTPUT}/symbols.o" -o "${OUTPUT}/both.elf")
same_images("the image of the -r output" "${OUTPUT}/combined.elf" "${OUTPUT}/both.elf")

# Page size, stripping, and the map.
run_checked("ld -z max-page-size" "${LD}" -z max-page-size=0x1000 "${object}" -o "${OUTPUT}/pages.elf")
run_checked("llvm-readelf -l" "${LLVM_READELF}" -l -W "${OUTPUT}/pages.elf")
expect("-z max-page-size=0x1000" "${run_stdout}" "\n +LOAD [^\n]* 0x1000\n")
run_checked("ld -s" "${LD}" -s "${object}" -o "${OUTPUT}/stripped.elf")
run_checked("llvm-readelf -S" "${LLVM_READELF}" -S -W "${OUTPUT}/stripped.elf")
if(run_stdout MATCHES "symtab")
    message(FATAL_ERROR "-s kept the symbol table\n${run_stdout}")
endif()
run_checked("ld -Map" "${LD}" -Map "${OUTPUT}/layout.map" "${object}" -o "${OUTPUT}/mapped.elf")
file(READ "${OUTPUT}/layout.map" map)
expect("the map" "${map}" "\n\\.text +0x0+400[0-9a-f]+ +0x[0-9a-f]+\n" " \\.text\\.helper +0x0+400[0-9a-f]+ "
       "\n +0x0+400[0-9a-f]+ +_start\n" "\n\\.tdata +0x0+41[0-9a-f]+ " "\n\\.bss +0x0+41[0-9a-f]+ ")
