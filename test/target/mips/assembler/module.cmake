# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# `.module` sets the file's ISA, register widths, odd single-precision
# registers, and floating-point ABI before any code, and the ELF flags and
# .MIPS.abiflags record them; GNU as, when installed, records the same for
# each case.

foreach(required AS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
include("${CMAKE_CURRENT_LIST_DIR}/common.cmake")
if(ASSEMBLER_TEST_SKIP)
    return()
endif()

# The ELF flags and MIPS attributes llvm-readelf prints for OBJECT.
function(mips_attributes object result)
    execute_process(COMMAND "${LLVM_READELF}" -h -A "${object}" OUTPUT_VARIABLE text)
    string(REPLACE ";" "," text "${text}")
    string(REPLACE "\n" ";" text_lines "${text}")
    set(lines "")
    foreach(line IN LISTS text_lines)
        if(line MATCHES "^ *(Flags:|ISA:|GPR size:|CPR1 size:|FP ABI:|FLAGS 1:)")
            string(STRIP "${line}" line)
            string(REGEX REPLACE " +" " " line "${line}")
            list(APPEND lines "${line}")
        endif()
    endforeach()
    set(${result} "${lines}" PARENT_SCOPE)
endfunction()

set(cases 0)
# module_case(LABEL "DIRECTIVES" "OPTIONS" PATTERN...): DIRECTIVES, separated
# by '|', precede a nop; the object assembled with OPTIONS matches each
# PATTERN, and GNU as given the same options agrees.
function(module_case label directives options)
    string(REPLACE "|" "\n        .module " text ".module ${directives}")
    set(source "${OUTPUT}.${label}.s")
    file(WRITE "${source}" "        ${text}\n        nop\n")
    separate_arguments(options UNIX_COMMAND "${options}")
    run_checked("${label}" "${AS}" -target mips-unknown-elf ${options} "${source}"
                -o "${OUTPUT}.${label}.o")
    mips_attributes("${OUTPUT}.${label}.o" cross)
    foreach(pattern IN LISTS ARGN)
        if(NOT cross MATCHES "${pattern}")
            string(REPLACE ";" "\n  " shown "${cross}")
            message(FATAL_ERROR "${label} lacks '${pattern}'\n  ${shown}")
        endif()
    endforeach()
    if(GNU_AS)
        run_checked("${label} (GNU as)" "${GNU_AS}" ${options} "${source}"
                    -o "${OUTPUT}.${label}.gnu.o")
        mips_attributes("${OUTPUT}.${label}.gnu.o" gnu)
        expect_lines("${label}: attributes of as and GNU as" "${cross}" "${gnu}")
    endif()
    math(EXPR count "${cases} + 1")
    set(cases ${count} PARENT_SCOPE)
endfunction()

set(vr4300 "-march=vr4300 -mabi=32")
# The preamble of GCC's VR4300 output.
module_case(gcc "fp=32|nooddspreg|arch=vr4300" "${vr4300} -mgp32"
            "Flags: 0x20001100, 32bitmode, o32, mips3" "ISA: MIPS3" "GPR size: 32"
            "CPR1 size: 32" "FP ABI: Hard float [(]double precision[)]" "FLAGS 1: 00000000")
module_case(fpxx "fp=xx" "${vr4300}"
            "FP ABI: Hard float [(]32-bit CPU, Any FPU[)]" "FLAGS 1: 00000000")
module_case(fp64 "fp=64" "${vr4300} -W"
            "CPR1 size: 64" "FP ABI: Hard float [(]32-bit CPU, 64-bit FPU[)]" "FLAGS 1: 00000001")
module_case(fp64a "fp=64|nooddspreg" "${vr4300} -W"
            "FP ABI: Hard float compat [(]32-bit CPU, 64-bit FPU[)]" "FLAGS 1: 00000000")
module_case(soft "softfloat" "${vr4300}" "CPR1 size: 0" "FP ABI: Soft float")
module_case(single "singlefloat" "${vr4300}" "FP ABI: Hard float [(]single precision[)]")
module_case(arch "arch=mips32r2" "-march=mips1 -mabi=32"
            "Flags: 0x70001000, o32, mips32r2" "ISA: MIPS32r2" "FLAGS 1: 00000001")
module_case(level "mips3" "-march=mips1 -mabi=32" "Flags: 0x20001100, 32bitmode, o32, mips3"
            "GPR size: 32")
message(STATUS "${cases} .module cases matched")
