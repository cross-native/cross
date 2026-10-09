# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Checks that the prologue saves of registers bound by hard-register objects
# are described by DWARF CFI and Win64 unwind codes in every frame shape, and
# on a Windows host that the system unwinder restores them through each frame.
foreach(required CC HOST_CXX SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must be defined")
    endif()
endforeach()
find_program(LLVM_MC NAMES llvm-mc REQUIRED)
find_program(LLVM_DWARFDUMP NAMES llvm-dwarfdump REQUIRED)
find_program(LLVM_READOBJ NAMES llvm-readobj REQUIRED)
set(here "${CMAKE_CURRENT_LIST_DIR}")

function(run_checked label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr TIMEOUT 60)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status})\n${stdout}\n${stderr}")
    endif()
    set(stdout "${stdout}" PARENT_SCOPE)
endfunction()

# Functions of the source in order, each with the registers its saves cover.
set(functions fixed win64 dynamic realigned realigned_dynamic large leaf)
set(fixed_registers R12 RBX)
set(win64_registers RSI RDI XMM6 XMM15)
set(dynamic_registers R13)
set(realigned_registers RBX)
set(realigned_dynamic_registers R15)
set(large_registers R14)
set(leaf_registers R14)

# Every .cfi_offset must name the address of the save just before it under
# the CFA rule in effect.
function(check_cfi_offsets assembly)
    file(STRINGS "${assembly}" lines)
    set(cfa_register rsp)
    set(cfa_offset 8)
    set(saved "")
    foreach(line IN LISTS lines)
        if(line STREQUAL ".cfi_startproc")
            set(cfa_register rsp)
            set(cfa_offset 8)
        elseif(line MATCHES "^\\.cfi_def_cfa_offset (-?[0-9]+)$")
            set(cfa_offset ${CMAKE_MATCH_1})
        elseif(line MATCHES "^\\.cfi_def_cfa_register %([a-z0-9]+)$")
            set(cfa_register ${CMAKE_MATCH_1})
        elseif(line MATCHES "^\\.cfi_def_cfa %([a-z0-9]+), (-?[0-9]+)$")
            set(cfa_register ${CMAKE_MATCH_1})
            set(cfa_offset ${CMAKE_MATCH_2})
        elseif(line MATCHES "^\t(movq|movdqu)\t%([a-z0-9]+), (-?[0-9]+)\\(%([a-z0-9]+)\\)$")
            set(saved ${CMAKE_MATCH_2} ${CMAKE_MATCH_3} ${CMAKE_MATCH_4})
        elseif(line MATCHES "^\tpushq\t%([a-z0-9]+)$")
            set(saved ${CMAKE_MATCH_1} 0 rsp)
        elseif(line MATCHES "^\\.cfi_offset %([a-z0-9]+), (-?[0-9]+)$")
            set(register ${CMAKE_MATCH_1})
            set(described ${CMAKE_MATCH_2})
            list(LENGTH saved count)
            if(NOT count EQUAL 3)
                message(FATAL_ERROR "${assembly}: no save precedes '${line}'")
            endif()
            list(GET saved 0 saved_register)
            list(GET saved 1 displacement)
            list(GET saved 2 base)
            if(NOT saved_register STREQUAL register)
                message(FATAL_ERROR "${assembly}: '${line}' follows a save of ${saved_register}")
            endif()
            if(base STREQUAL cfa_register)
                math(EXPR expected "${displacement} - ${cfa_offset}")
                if(NOT described EQUAL expected)
                    message(FATAL_ERROR
                        "${assembly}: '${line}' describes CFA${described}, "
                        "but the save is at CFA${expected}")
                endif()
            elseif(NOT base STREQUAL "rsp")
                message(FATAL_ERROR
                    "${assembly}: '${line}' saves through %${base}, not the CFA register")
            endif()
            set(saved "")
        endif()
    endforeach()
endfunction()

foreach(flags "-O0" "-O2" "-O2;-fno-omit-frame-pointer")
    string(REPLACE ";" "" name "${flags}")
    set(elf "${OUTPUT}${name}-elf")
    run_checked("ELF assembly (${flags})" "${CC}" ${flags} -funwind-tables
        -target x86_64-unknown-linux-gnu -S "${SOURCE}" -o "${elf}.s")
    check_cfi_offsets("${elf}.s")
    run_checked("ELF object (${flags})" "${LLVM_MC}" -triple x86_64-unknown-linux-gnu
        -filetype=obj "${elf}.s" -o "${elf}.o")
    run_checked("ELF unwind dump (${flags})" "${LLVM_DWARFDUMP}" --eh-frame "${elf}.o")
    # One FDE per function, in source order.
    string(REPLACE " FDE cie=" ";" fdes "${stdout}")
    list(REMOVE_AT fdes 0)
    list(LENGTH fdes count)
    list(LENGTH functions expected_count)
    if(NOT count EQUAL expected_count)
        message(FATAL_ERROR "ELF (${flags}) has ${count} FDEs\n${stdout}")
    endif()
    foreach(function IN LISTS functions)
        list(FIND functions ${function} index)
        list(GET fdes ${index} fde)
        foreach(register IN LISTS ${function}_registers)
            if(NOT fde MATCHES " ${register}=\\[CFA-[0-9]+\\]")
                message(FATAL_ERROR
                    "ELF (${flags}): ${function} does not describe ${register}\n${fde}")
            endif()
        endforeach()
    endforeach()

    set(coff "${OUTPUT}${name}-coff")
    run_checked("COFF assembly (${flags})" "${CC}" ${flags} -funwind-tables
        -target x86_64-w64-windows-gnu -S "${SOURCE}" -o "${coff}.s")
    run_checked("COFF object (${flags})" "${LLVM_MC}" -triple x86_64-w64-windows-gnu
        -filetype=obj "${coff}.s" -o "${coff}.o")
    run_checked("COFF unwind dump (${flags})" "${LLVM_READOBJ}" --unwind "${coff}.o")
    string(REPLACE "RuntimeFunction {" ";" entries "${stdout}")
    foreach(function IN LISTS functions)
        set(entry "")
        foreach(candidate IN LISTS entries)
            if(candidate MATCHES "StartAddress: hard_unwind_${function} ")
                set(entry "${candidate}")
            endif()
        endforeach()
        foreach(register IN LISTS ${function}_registers)
            if(NOT entry MATCHES "(SAVE_NONVOL|PUSH_NONVOL|SAVE_XMM128) reg=${register}[,\n]")
                message(FATAL_ERROR
                    "COFF (${flags}): hard_unwind_${function} does not describe ${register}\n${entry}")
            endif()
        endforeach()
    endforeach()
endforeach()

if(NOT WIN32)
    return()
endif()
run_checked("driver compilation" "${HOST_CXX}" -O1 -std=c++20 -c
    "${here}/hard_register_unwind_driver.cpp" -o "${OUTPUT}-driver.o")
run_checked("harness assembly" "${HOST_CXX}" -c
    "${here}/hard_register_unwind_harness.s" -o "${OUTPUT}-harness.o")
foreach(flags "-O0" "-O2" "-O2;-fno-omit-frame-pointer")
    string(REPLACE ";" "" name "${flags}")
    set(run "${OUTPUT}${name}-run")
    run_checked("host object (${flags})" "${CC}" ${flags} -funwind-tables
        -c "${SOURCE}" -o "${run}.o")
    run_checked("link (${flags})" "${HOST_CXX}" "${OUTPUT}-driver.o"
        "${OUTPUT}-harness.o" "${run}.o" -o "${run}.exe")
    run_checked("unwind run (${flags})" "${run}.exe")
endforeach()
