# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

file(MAKE_DIRECTORY "${OUTPUT}")
file(WRITE "${OUTPUT}/leaf.x" [=[
global u32 input;
global u32 output;
[[abi("views")]] global void leaf() { output = input * 3u32; }
]=])

# Overlap and omission spell the same contract on every native target.
foreach(config "x86-64;x86_64-unknown-linux-gnu;r12d;r12;64"
               "mips;mipsel-unknown-elf;s0;s0;32"
               "aarch64;aarch64-unknown-linux-gnu;w19;x19;64")
    list(GET config 0 architecture)
    list(GET config 1 triple)
    list(GET config 2 view)
    list(GET config 3 storage)
    list(GET config 4 bits)
    foreach(overlap FALSE TRUE)
        set(clobbers "")
        if(overlap)
            set(clobbers "\"${storage}\"")
        endif()
        set(model "${OUTPUT}/${architecture}-${overlap}.xm")
        file(WRITE "${model}" "abi \"views\" {
    architecture = \"${architecture}\";
    address_bits = ${bits};
    stack_alignment = 16;
    stack_slot_bytes = 8;
    call_clobbers = [${clobbers}];
    call_preserved_views = [\"${view}\"];
    bank \"integer\" {
        class = \"integer\";
        register_bits = ${bits};
        arguments = [\"${storage}\"];
        results = [\"${storage}\"];
    }
    rule \"integer\" { match = [\"integer\"]; action = \"direct\"; bank = \"integer\"; }
}
")
        execute_process(COMMAND "${CC}" "--model=${model}" -target "${triple}"
            -S -O2 "${OUTPUT}/leaf.x" -o -
            RESULT_VARIABLE status OUTPUT_VARIABLE assembly_${overlap} ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "${architecture} ${overlap}: ${err}")
        endif()
    endforeach()
    if(NOT assembly_FALSE STREQUAL assembly_TRUE)
        message(FATAL_ERROR "${architecture}: preserved-view overlap changes assembly")
    endif()
endforeach()
