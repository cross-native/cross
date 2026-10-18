# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The CPUs that select the Release 2 instructions: MIPS32 Release 2 CPUs
# select SEB, SEH, WSBH, EXT, INS, and ROTR, MIPS64 Release 2 CPUs also
# DSBH, DSHD, DEXT, DINS, and their forms, and DROTR, DROTR32, and DROTRV,
# MIPS32 and MIPS64 CPUs MUL, and Allegrex its SEB, SEH, EXT, and INS words;
# earlier CPUs select none of them. GNU as, when installed, encodes the
# MIPS64 Release 2 assembly as cc -c does.

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")

# The mnemonics, and the comments of encoded words, of SOURCE's assembly.
function(mnemonics label options result)
    separate_arguments(split UNIX_COMMAND "${options}")
    execute_process(COMMAND "${CC}" ${split} -fno-eval-calls -S "${SOURCE}"
                            -o "${OUTPUT}.${label}.s"
                    RESULT_VARIABLE status ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label}: cc ${options} failed\n${error}")
    endif()
    file(STRINGS "${OUTPUT}.${label}.s" lines)
    set(found "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^\t([a-z][a-z0-9.]*)(\t|$)")
            list(APPEND found "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^\t\\.word\t0x[0-9a-f]+\t# ([a-z0-9.]+) ")
            list(APPEND found "word:${CMAKE_MATCH_1}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES found)
    set(${result} "${found}" PARENT_SCOPE)
endfunction()

function(expect label options present absent)
    mnemonics(${label} "${options}" found)
    foreach(name IN LISTS present)
        if(NOT name IN_LIST found)
            message(FATAL_ERROR "${label}: ${options} selects no '${name}'\n${found}")
        endif()
    endforeach()
    foreach(name IN LISTS absent)
        if(name IN_LIST found)
            message(FATAL_ERROR "${label}: ${options} selects '${name}'")
        endif()
    endforeach()
endfunction()

set(word_forms seb seh wsbh ext ins mul)
set(doubleword_forms dsbh dshd dext dextm dextu dins dinsm dinsu drotr drotr32 drotrv)
set(o32 "-target mips-unknown-elf -mabi=o32")
set(n64 "-target mips64-unknown-elf -mabi=n64")
set(psp "-target mipsallegrexel-sony-psp-elf")
expect(mips32r2 "${o32} -march=mips32r2 -O2" "${word_forms};rotr" "${doubleword_forms};mult")
expect(24kc "${o32} -march=24kc -O2" "${word_forms}" "${doubleword_forms}")
expect(mips32r2-el "-target mipsel-unknown-elf -mabi=o32 -march=mips32r2 -O2"
       "${word_forms}" "${doubleword_forms}")
expect(mips64r2-o32 "${o32} -march=mips64r2 -O2"
       "${word_forms};dsbh;dshd;dext;dextm;dextu;drotr;drotr32;drotrv" "")
expect(mips64r2 "${n64} -march=mips64r2 -O2" "${word_forms};${doubleword_forms}" "")
expect(octeon3 "${n64} -march=octeon3 -O2" "${word_forms};${doubleword_forms}" "")
# SEB, SEH, and the byte swaps need no combining pass.
expect(mips64r2-O0 "${n64} -march=mips64r2 -O0" "seb;seh;wsbh;dsbh;dshd;drotrv;mul" "")
expect(allegrex "${psp} -march=allegrex -O2" "word:seb;word:seh;word:ext;word:ins"
       "${word_forms};${doubleword_forms}")
expect(mips32 "${o32} -march=mips32 -O2" "mul" "seb;seh;wsbh;ext;ins;${doubleword_forms}")
expect(mips64 "${n64} -march=mips64 -O2" "mul" "seb;seh;wsbh;ext;ins;${doubleword_forms}")
foreach(cpu r3000 mips2 vr4300 r4000 mips4 r10000 vr5400)
    expect(${cpu} "${o32} -march=${cpu} -O2" "" "${word_forms};${doubleword_forms}")
endforeach()
# The cost model decides: a CPU whose EXT and INS cost more than the shift
# and mask they replace keeps those.
file(WRITE "${OUTPUT}.slow.xm" "cpu \"slow-fields\" {\n    architecture = \"mips\";\n    inherits = \"mips32r2\";\n    instruction \"ext\" { latency = 5; }\n    instruction \"ins\" { latency = 5; }\n    instruction \"seb\" { latency = 5; }\n    instruction \"seh\" { latency = 5; }\n    instruction \"mul\" { latency = 20; }\n}\n")
expect(slow "${o32} --model=${OUTPUT}.slow.xm -march=mips32r2 -mtune=slow-fields -O2"
       "wsbh;mult" "seb;seh;ext;ins;mul")

# GNU as encodes the MIPS64 Release 2 forms as cc -c does.
find_program(GNU_AS NAMES mips64-elf-as)
find_program(LLVM_OBJCOPY NAMES llvm-objcopy)
if(NOT GNU_AS OR NOT LLVM_OBJCOPY)
    message(STATUS "skipping the GNU as comparison: mips64-elf-as and llvm-objcopy are required")
    return()
endif()
foreach(case "n64|${n64} -march=mips64r2 -O2|-EB -march=mips64r2 -mabi=64"
             "o32|${o32} -march=mips64r2 -O2|-EB -march=mips64r2 -mabi=32")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 label)
    list(GET fields 1 options)
    list(GET fields 2 gnu_options)
    separate_arguments(options UNIX_COMMAND "${options}")
    separate_arguments(gnu_options UNIX_COMMAND "${gnu_options}")
    set(stem "${OUTPUT}.gnu-${label}")
    execute_process(COMMAND "${CC}" ${options} -fno-eval-calls -c "${SOURCE}"
                            -o "${stem}.cross.o" RESULT_VARIABLE status ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label}: cc -c failed\n${error}")
    endif()
    execute_process(COMMAND "${CC}" ${options} -fno-eval-calls -S "${SOURCE}"
                            -o "${stem}.s" RESULT_VARIABLE status ERROR_VARIABLE error)
    execute_process(COMMAND "${GNU_AS}" ${gnu_options} "${stem}.s" -o "${stem}.gnu.o"
                    RESULT_VARIABLE status ERROR_VARIABLE error)
    if(NOT status EQUAL 0 OR NOT error STREQUAL "")
        message(FATAL_ERROR "${label}: GNU as does not accept the assembly\n${error}")
    endif()
    foreach(origin cross gnu)
        execute_process(COMMAND "${LLVM_OBJCOPY}" -O binary --only-section=.text
                                "${stem}.${origin}.o" "${stem}.${origin}.text"
                        RESULT_VARIABLE status ERROR_VARIABLE error)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "${label}: cannot extract ${origin} .text\n${error}")
        endif()
        file(READ "${stem}.${origin}.text" ${origin}_text HEX)
    endforeach()
    if(NOT cross_text STREQUAL gnu_text)
        message(FATAL_ERROR "${label}: GNU as encodes the assembly differently")
    endif()
endforeach()
message(STATUS "Release 2 selections checked")
