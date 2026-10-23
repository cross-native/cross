# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Static position-independent executables (-pie): the dynamic section, its
# relative relocations, and the program headers, as llvm-readelf shows
# them; code, data, GOT, and relocations equal ld.lld's static PIE (-pie
# --no-dynamic-linker --apply-dynamic-relocs); the default layout starts
# at address 0; and with qemu-system-aarch64, a flat binary of the image
# relocates itself and runs at two load addresses.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()
find_program(LLVM_OBJCOPY NAMES llvm-objcopy)
set(objects "")
foreach(assembler cross mc)
    set(objects_${assembler} "")
    foreach(source pie pie_symbols)
        assemble(${assembler} "${sources}/${source}.s" "${OUTPUT}/${source}-${assembler}.o")
        list(APPEND objects_${assembler} "${OUTPUT}/${source}-${assembler}.o")
    endforeach()
    run_checked("ld -pie of the ${assembler} objects" "${LD}" -pie -T "${sources}/pie.ld" ${objects_${assembler}}
                -o "${OUTPUT}/pie-${assembler}.elf")
endforeach()
same_images("the images of the cross and mc objects" "${OUTPUT}/pie-cross.elf" "${OUTPUT}/pie-mc.elf")
set(image "${OUTPUT}/pie-cross.elf")

run_checked("llvm-readelf" "${LLVM_READELF}" -h -l -d -r -W "${image}")
set(text "${run_stdout}")
expect("the PIE's headers" "${text}"
       "Type: +DYN"
       "\n +LOAD +0x[0-9a-f]+ 0x0+ 0x0+ "
       "\n +DYNAMIC +0x013000 0x0+3000 0x0+3000 0x0000b0 0x0000b0 RW "
       "\n +TLS "
       "\\(FLAGS_1\\) +PIE"
       "\\(DEBUG\\) +0x0\n"
       "\\(RELA\\) +0x1100\n"
       "\\(RELASZ\\) +120 \\(bytes\\)"
       "\\(RELAENT\\) +24 \\(bytes\\)"
       "\\(RELACOUNT\\) +5\n"
       "\\(SYMTAB\\) +0x1000\n"
       "\\(SYMENT\\) +24 \\(bytes\\)"
       "\\(STRTAB\\) +0x1018\n"
       "\\(STRSZ\\) +1 \\(bytes\\)"
       "contains 5 entries"
       # .data.rel.ro, the GOT entry of target, and data: target, target + 8,
       # and the function pointer; none for the absolute and undefined weak
       # symbols, in .data or in the GOT.
       "\n0+2000 +0+403 R_AARCH64_RELATIVE +190\n"
       "\n0+2018 +0+403 R_AARCH64_RELATIVE +190\n"
       "\n0+2020 +0+403 R_AARCH64_RELATIVE +190\n"
       "\n0+2028 +0+403 R_AARCH64_RELATIVE +198\n"
       "\n0+2040 +0+403 R_AARCH64_RELATIVE +190\n")
if(text MATCHES "INTERP")
    message(FATAL_ERROR "the PIE names a dynamic linker\n${text}")
endif()
run_checked("llvm-readelf -s" "${LLVM_READELF}" -s -W "${image}")
expect("the PIE's symbols" "${run_stdout}" "0+3000 +0 OBJECT +GLOBAL HIDDEN +[0-9]+ _DYNAMIC\n")

if(LLD)
    run_checked("ld.lld -pie" "${LLD}" -pie --no-dynamic-linker --no-relax --apply-dynamic-relocs -z norelro
                -T "${sources}/pie.ld" ${objects_cross} -o "${OUTPUT}/pie-lld.elf")
    same_images("the PIEs of ld and ld.lld" "${image}" "${OUTPUT}/pie-lld.elf"
                .text .rela.dyn .data.rel.ro .got .data .tdata .dynsym .dynstr)
else()
    message(STATUS "ld.lld not found")
endif()

# The default layout: from address 0, with the headers in the first
# segment, and the dynamic sections where GNU ld's PIE script puts them.
run_checked("ld -pie with the default layout" "${LD}" -pie --defsym tls_start=__tdata_start ${objects_cross}
            -o "${OUTPUT}/default.elf")
run_checked("llvm-readelf of the default layout" "${LLVM_READELF}" -h -S -l -d -W "${OUTPUT}/default.elf")
set(text "${run_stdout}")
expect("the default PIE layout" "${text}" "Type: +DYN"
       "\n +LOAD +0x000000 0x0+ 0x0+ "
       " \\.dynsym +DYNSYM +0+120 "
       " \\.dynstr +STRTAB "
       " \\.rela\\.dyn +RELA "
       " \\.text +PROGBITS "
       " \\.dynamic +DYNAMIC "
       "\n +DYNAMIC "
       "\\(RELACOUNT\\) +5\n")
if(text MATCHES "INTERP")
    message(FATAL_ERROR "the default PIE names a dynamic linker\n${text}")
endif()

if(QEMU AND LLVM_OBJCOPY)
    run_checked("llvm-objcopy" "${LLVM_OBJCOPY}" -O binary "${image}" "${OUTPUT}/pie.bin")
    foreach(address 0x40200000 0x40480000)
        qemu_exits_zero("the PIE at ${address}"
                        -device "loader,file=${OUTPUT}/pie.bin,addr=${address},cpu-num=0,force-raw=on")
    endforeach()
else()
    message(STATUS "qemu-system-aarch64 or llvm-objcopy not found; the image was not run")
endif()
