# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC CPP AS OUTPUT)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
file(WRITE "${OUTPUT}/source.x" "global u32 identity(u32 value) { return value; }\n")
file(WRITE "${OUTPUT}/nop.s" ".text\nlazy_nop\n")
set(model "${OUTPUT}/mixed.xm")
file(WRITE "${model}" [=[
# Strings and comments containing braces do not end declarations: } {
mangling "lazy-link" { entity = concat("lazy_{", name, "}"); label = name; generic = name; }
profile "lazy-mips" {
    target = "mips-unknown-elf";
    m.arch = "lazy-mips";
    optimization = "lazy-opt";
}
profile "lazy-arm" {
    target = "aarch64-none-elf";
    m.arch = "lazy-arm";
}
cpu "lazy-mips" {
    instruction "nop" { latency = 7; }
    inherits = "vr4300";
    architecture = "mips";
}
cpu "lazy-arm" {
    architecture = "aarch64";
    inherits = "armv8-a";
    instruction "nop" { latency = 9; }
}
instruction "lazy-mips-nop" {
    mnemonic = "lazy_nop";
    alias = "nop";
    architecture = "mips";
}
instruction "lazy-arm-nop" {
    architecture = "aarch64";
    mnemonic = "lazy_nop";
    alias = "nop";
}
]=])
file(WRITE "${OUTPUT}/later.xm" [=[
optimization "lazy-opt" { inherits = "O2"; }
]=])

function(run label)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE status
        OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${stdout}\n${stderr}")
    endif()
    set(stdout "${stdout}" PARENT_SCOPE)
endfunction()

# A profile selects its architecture before CPU and instruction validation,
# including forward references, nested blocks, and a late architecture key.
foreach(profile lazy-mips lazy-arm)
    set(flags "--model=${model}" "--model=${OUTPUT}/later.xm" -mprofile=${profile})
    run(${profile}-costs "${CC}" ${flags} --print-costs)
    if(NOT stdout MATCHES "\nnop  latency: (7|9)  issue: 1  available: yes\n")
        message(FATAL_ERROR "${profile} did not resolve the user's CPU/instruction\n${stdout}")
    endif()
    run(${profile}-as "${AS}" ${flags} "${OUTPUT}/nop.s" -o "${OUTPUT}/${profile}.o")
    run(${profile}-cpp "${CPP}" ${flags} "${OUTPUT}/source.x" -o "${OUTPUT}/${profile}.i")
endforeach()
run(mips-compile "${CC}" "--model=${model}" "--model=${OUTPUT}/later.xm"
    -mprofile=lazy-mips -S "${OUTPUT}/source.x" -o "${OUTPUT}/mips.s")
run(explicit-target "${CC}" "--model=${model}" "--model=${OUTPUT}/later.xm"
    -mprofile=lazy-arm -target x86_64-unknown-linux-gnu -march=generic
    -S "${OUTPUT}/source.x" -o "${OUTPUT}/x86.s")

# A bad instruction of an unselected architecture is deferred. The exact
# same entry is diagnosed when selected, and by architecture-wide queries.
file(WRITE "${OUTPUT}/deferred.xm" [=[
instruction "bad" {
    architecture = "aarch64";
    encoding = "0";
}
]=])
foreach(tool CC CPP)
    set(compile_flags -S)
    if(tool STREQUAL "CPP")
        set(compile_flags)
    endif()
    run(${tool}-deferred "${${tool}}" -target x86_64-unknown-linux-gnu
        "--model=${OUTPUT}/deferred.xm" ${compile_flags} "${OUTPUT}/source.x"
        -o "${OUTPUT}/${tool}-deferred.out")
endforeach()
foreach(selection "-target;aarch64-none-elf;--print-instructions"
                  --print-targets --print-models --print-profiles --print-cpus
                  --print-manglings --print-optimizations --print-data-bases --print-init-tables)
    execute_process(COMMAND "${CC}" "--model=${OUTPUT}/deferred.xm" ${selection}
        RESULT_VARIABLE status ERROR_VARIABLE stderr OUTPUT_QUIET)
    if(status EQUAL 0 OR NOT stderr MATCHES "deferred[.]xm:3: error: the encoding has 1 bits")
        message(FATAL_ERROR "${selection} did not validate the deferred entry\n${stderr}")
    endif()
endforeach()
