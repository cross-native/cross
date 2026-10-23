# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# AArch64 has models but no code generator: every output that needs code
# fails with one diagnostic that says so, whatever the source holds, and
# preprocessing and dependency output work.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

set(valid "${OUTPUT}/valid.x")
file(WRITE "${valid}" "global i32 add(in i32 a, in i32 b) {
    return a + b;
}

global f64 scale(in f64 x) {
    return x * 2.0;
}
")
set(invalid "${OUTPUT}/invalid.x")
file(WRITE "${invalid}" "global i32 broken( {\n")

function(unavailable label triple)
    execute_process(COMMAND "${CC}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(expected "^cc: error: code generation for aarch64 is not implemented: for target '${triple}', cc supports only -E, -M, and the --print-[*] queries\n$")
    if(status EQUAL 0 OR NOT err MATCHES "${expected}")
        message(FATAL_ERROR "${label} did not report one unavailable-code-generation diagnostic\n${out}\n${err}")
    endif()
endfunction()

unavailable(assembly aarch64-unknown-linux-gnu
    -target aarch64-unknown-linux-gnu -S "${valid}" -o "${OUTPUT}/valid.s")
unavailable(object aarch64-none-elf
    -target aarch64-none-elf -O2 -c "${valid}" -o "${OUTPUT}/valid.o")
unavailable(switch aarch64-none-elf
    -mprofile=switch -S "${valid}" -o "${OUTPUT}/switch.s")
unavailable(invalid-source aarch64-none-elf
    -target aarch64-none-elf -S "${invalid}" -o "${OUTPUT}/invalid.s")
unavailable(dependencies-and-assembly aarch64-none-elf
    -target aarch64-none-elf -MD -S "${valid}" -o "${OUTPUT}/valid-md.s")
if(EXISTS "${OUTPUT}/valid.s" OR EXISTS "${OUTPUT}/valid.o" OR EXISTS "${OUTPUT}/switch.s")
    message(FATAL_ERROR "an output file was written")
endif()

execute_process(COMMAND "${CC}" -target aarch64-none-elf -E "${valid}" -o -
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT out MATCHES "global i32 add[(]in i32 a, in i32 b[)]")
    message(FATAL_ERROR "-E failed for aarch64\n${out}\n${err}")
endif()
execute_process(COMMAND "${CC}" -target aarch64-none-elf -M "${valid}"
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT out MATCHES "^valid[.]o: [^\n]*valid[.]x\n$")
    message(FATAL_ERROR "-M failed for aarch64\n${out}\n${err}")
endif()
