# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The function_alignment property of cpu entries: the shipped Skylake,
# Skylake-AVX512, and Zen 3 entries set 32 bytes, which the CPUs that
# inherit them and their other names share; it applies to the -mtune CPU of
# a speed goal, on x86-64 and MIPS alike, and to managed and naked
# functions; -falign-functions and size goals take precedence; and model
# loading checks the value and the agreement of nearest bases.

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

set(x86 -target x86_64-unknown-linux-gnu)
set(source "${OUTPUT}/functions.x")
file(WRITE "${source}" [=[
global i32 managed(in i32 x) { return x + 1i32; }
[[naked]] global void bare() { $::_ret(); }
]=])
set(mips_source "${OUTPUT}/mips.x")
file(WRITE "${mips_source}" "global i32 managed(in i32 x) { return x + 1i32; }\n")

# The .p2align powers of an assembly, one per function.
function(alignments label result)
    execute_process(COMMAND "${CC}" ${ARGN} -S -o "${OUTPUT}/${label}.s"
                    RESULT_VARIABLE status ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label}: cc failed\n${error}")
    endif()
    file(STRINGS "${OUTPUT}/${label}.s" lines REGEX "^\\.p2align ")
    string(REPLACE ".p2align " "" powers "${lines}")
    set(${result} "${powers}" PARENT_SCOPE)
endfunction()

function(expect_alignment label want)
    alignments(${label} powers ${ARGN})
    foreach(power IN LISTS powers)
        if(NOT power STREQUAL want)
            message(FATAL_ERROR "${label}: functions align to 2^${power}, not 2^${want}\n${powers}")
        endif()
    endforeach()
    if(powers STREQUAL "")
        message(FATAL_ERROR "${label}: no function alignment")
    endif()
endfunction()

foreach(cpu skylake skylake-avx512 znver3 cannonlake icelake-client
            icelake-server tigerlake sapphirerapids graniterapids cascadelake
            cooperlake skx znver4 znver5)
    expect_alignment(tune-${cpu} 5 ${x86} -O2 -mtune=${cpu} "${source}")
endforeach()
foreach(cpu haswell broadwell znver1 znver2 alderlake x86-64-v3 generic intel)
    expect_alignment(tune-${cpu} 4 ${x86} -O2 -mtune=${cpu} "${source}")
endforeach()
expect_alignment(march-skylake 5 ${x86} -O2 -march=skylake "${source}")
expect_alignment(size-skylake 3 ${x86} -Os -mtune=skylake "${source}")
expect_alignment(explicit-skylake 6 ${x86} -O2 -mtune=skylake
                 -falign-functions=64 "${source}")
expect_alignment(mips-vr4300 2 -target mips-unknown-elf -O2 -march=vr4300
                 "${mips_source}")

# --print-cpus shows the property where an entry or a base sets it.
execute_process(COMMAND "${CC}" ${x86} --print-cpus OUTPUT_VARIABLE listing)
foreach(cpu skylake cascadelake znver4)
    if(NOT listing MATCHES "\n${cpu}  [^\n]*  issue_width: 6  function_alignment: 32  source: ")
        message(FATAL_ERROR "--print-cpus does not show ${cpu}'s function alignment")
    endif()
endforeach()
if(listing MATCHES "\nhaswell  [^\n]*function_alignment")
    message(FATAL_ERROR "--print-cpus shows a function alignment for haswell")
endif()

# User entries on either target.
set(model "${OUTPUT}/aligned.xm")
file(WRITE "${model}" [=[
cpu "wide-x86" {
    architecture = "x86-64";
    inherits = "haswell";
    function_alignment = 64;
}

cpu "wide-mips" {
    architecture = "mips";
    inherits = "vr4300";
    function_alignment = 16;
}

cpu "both" {
    architecture = "x86-64";
    inherits = ["skylake", "wide-x86"];
    function_alignment = 128;
}
]=])
expect_alignment(user-x86 6 ${x86} "--model=${model}" -O2 -mtune=wide-x86 "${source}")
expect_alignment(user-both 7 ${x86} "--model=${model}" -O2 -mtune=both "${source}")
expect_alignment(user-mips 4 -target mips-unknown-elf "--model=${model}" -O2
                 -march=vr4300 -mtune=wide-mips "${mips_source}")
expect_alignment(user-mips-size 2 -target mips-unknown-elf "--model=${model}" -Os
                 -march=vr4300 -mtune=wide-mips "${mips_source}")

# Model checks.
function(expect_model_error label text pattern)
    file(WRITE "${OUTPUT}/${label}.xm" "${text}")
    execute_process(COMMAND "${CC}" ${x86} "--model=${OUTPUT}/${label}.xm" --print-cpus
                    RESULT_VARIABLE status OUTPUT_QUIET ERROR_VARIABLE error)
    if(status EQUAL 0 OR NOT error MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: the model was not rejected with '${pattern}'\n${error}")
    endif()
endfunction()
foreach(value 0 3 131072)
    expect_model_error(value-${value}
        "cpu \"bad\" { architecture = \"x86-64\"; function_alignment = ${value}; }\n"
        "model property 'function_alignment' must be a power of two from 1 to 65536")
endforeach()
expect_model_error(conflict [=[
cpu "a" { architecture = "x86-64"; function_alignment = 32; }
cpu "b" { architecture = "x86-64"; function_alignment = 64; }
cpu "c" { architecture = "x86-64"; inherits = ["a", "b"]; }
]=] "function_alignment")
message(STATUS "function alignment checks passed")
