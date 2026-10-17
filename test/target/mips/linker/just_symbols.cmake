# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# --just-symbols in o32: symbol values read from an ELF32 file and linker
# script addresses are both 32-bit architectural values, so a jal at
# 0x80300000 reaches a symbol file's 0x80000400 in the same 256 MiB region
# and encodes 0x0c000100 (the N64 case GNU ld once rejected as a region
# overflow). The symbol file contributes addresses, never contents.

include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(LINKER_TEST_SKIP)
    return()
endif()
set(sources "${CMAKE_CURRENT_LIST_DIR}")
foreach(order big little)
    set(triple mips-unknown-elf)
    if(order STREQUAL "little")
        set(triple mipsel-unknown-elf)
    endif()
    set(base "${OUTPUT}/${order}")
    run_checked("provider" "${AS}" -target ${triple} "${sources}/just_symbols_provider.s"
                -o "${base}-provider.o")
    run_checked("consumer" "${AS}" -target ${triple} "${sources}/just_symbols_consumer.s"
                -o "${base}-consumer.o")
    run_checked("symbol file" "${LD}" -T "${sources}/just_symbols.ld" --defsym TEXT_START=0x80000400
                "${base}-provider.o" -o "${base}-provider.elf")
    foreach(form joined separate short)
        if(form STREQUAL "joined")
            set(option "--just-symbols=${base}-provider.elf")
        elseif(form STREQUAL "separate")
            set(option --just-symbols "${base}-provider.elf")
        else()
            set(option -R "${base}-provider.elf")
        endif()
        run_checked("image (${form})" "${LD}" -T "${sources}/just_symbols.ld"
                    --defsym TEXT_START=0x80300000 ${option} "${base}-consumer.o"
                    -o "${base}-${form}.elf")
        section_words("${base}-${form}.elf" .text ${order} words)
        if(NOT words STREQUAL "0c000100;00000000")
            message(FATAL_ERROR "${order} ${form}: .text is ${words}, not jal 0x80000400 and nop")
        endif()
        symbol_value("${base}-${form}.elf" imported value)
        if(NOT value STREQUAL "80000400")
            message(FATAL_ERROR "${order} ${form}: imported is ${value}")
        endif()
    endforeach()
    if(GNU_LD)
        # GNU ld with the region fix gives the same image.
        string(SUBSTRING "${order}" 0 1 letter)
        string(TOUPPER "${letter}" letter)
        run_checked("GNU ld symbol file" "${GNU_LD}" -E${letter} -T "${sources}/just_symbols.ld"
                    --defsym TEXT_START=0x80000400 "${base}-provider.o" -o "${base}-gnu-provider.elf")
        execute_process(COMMAND "${GNU_LD}" -E${letter} -T "${sources}/just_symbols.ld"
                        --defsym TEXT_START=0x80300000 --just-symbols=${base}-gnu-provider.elf
                        "${base}-consumer.o" -o "${base}-gnu.elf"
                        RESULT_VARIABLE status ERROR_VARIABLE stderr)
        if(status EQUAL 0)
            section_hex("${base}-gnu.elf" .text gnu)
            section_hex("${base}-joined.elf" .text cross)
            if(NOT gnu STREQUAL cross)
                message(FATAL_ERROR "${order}: GNU ld wrote ${gnu}, Cross ld ${cross}")
            endif()
        else()
            message(STATUS "GNU ld without the --just-symbols region fix: ${stderr}")
        endif()
    endif()
endforeach()

# A jump that does leave its region is an error naming file and symbol.
run_failing("region" "${LD}" -T "${sources}/just_symbols.ld" --defsym TEXT_START=0x90000000
            --just-symbols=${OUTPUT}/big-provider.elf "${OUTPUT}/big-consumer.o"
            -o "${OUTPUT}/region.elf")
expect("region error" "${run_stderr}"
       "big-consumer.o:[(][.]text[+]0x0[)]: jump to 'imported' [(]0x80000400[)] leaves the 256 MiB region")
