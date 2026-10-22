# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# A typedef that lowers alignment: accesses through it use the target's
# unaligned-safe sequences, as packed members do, in both MIPS byte orders,
# and the alignment-sensitive checks see the lowered alignment.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile name source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}\n")
    execute_process(COMMAND "${CC}" -S -O2 ${ARGN} "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(status "${status}" PARENT_SCOPE)
    set(diagnostics "${out}${err}" PARENT_SCOPE)
endfunction()

set(accesses [=[
typedef u32 u32_any [[aligned(1)]];
typedef u64 u64_any [[aligned(1)]];
struct framed { u8 tag; u32_any value; };
global void put32(in u8 *p, in u32 v) { *(u32_any *)p = v; }
global u32 get32(in const u8 *p) { return *(const u32_any *)p; }
global void put64(in u8 *p, in u64 v) { *(u64_any *)p = v; }
global u64 get64(in const u8 *p) { return *(const u64_any *)p; }
global u32_any *member(in struct framed *f) { return &f->value; }
global u8 lead;
global u32_any word;
global u32 getglobal() { return word; }
global void putglobal(in u32 v) { word = v; }
]=])

# Extract one function's assembly.
function(function_body assembly name result)
    string(FIND "${assembly}" "\n${name}:\n" begin)
    if(begin EQUAL -1)
        message(FATAL_ERROR "missing ${name}\n${assembly}")
    endif()
    string(SUBSTRING "${assembly}" ${begin} -1 rest)
    string(FIND "${rest}" "\n.size ${name}," end)
    string(SUBSTRING "${rest}" 0 ${end} body)
    set(${result} "${body}" PARENT_SCOPE)
endfunction()

# Each word access through the pointer is a byte sequence, as for a packed
# member; only frame-relative carriers use word-sized accesses.
function(require_unaligned target body)
    string(REGEX MATCHALL "\t(lw|sw|ld|sd|lh|lhu|sh)\t[^\n]*" accesses "${body}")
    foreach(access IN LISTS accesses)
        if(NOT access MATCHES "\\(\\$(sp|fp|29|30)\\)$")
            message(FATAL_ERROR "${target}: aligned access through the pointer: ${access}\n${body}")
        endif()
    endforeach()
    string(REGEX MATCHALL "\t(lbu|sb)\t" bytes "${body}")
    list(LENGTH bytes count)
    if(count LESS 4)
        message(FATAL_ERROR "${target}: no byte sequence\n${body}")
    endif()
endfunction()

foreach(target mips-unknown-elf mipsel-unknown-elf "mips64-unknown-elf -mabi=n64" "mips64el-unknown-elf -mabi=n64")
    separate_arguments(flags NATIVE_COMMAND "-target ${target}")
    string(REGEX REPLACE "[^A-Za-z0-9]+" "_" name "${target}")
    compile(${name} "${accesses}" ${flags})
    if(NOT status EQUAL 0 OR diagnostics MATCHES "warning")
        message(FATAL_ERROR "${target}: unexpected diagnostics\n${diagnostics}")
    endif()
    file(READ "${OUTPUT}/${name}.s" assembly)
    foreach(function put32 get32 put64 get64 getglobal putglobal)
        function_body("${assembly}" ${function} body)
        require_unaligned("${target} ${function}" "${body}")
    endforeach()
endforeach()

compile(x86_64 "${accesses}" -target x86_64-unknown-linux-gnu)
if(NOT status EQUAL 0 OR diagnostics MATCHES "warning")
    message(FATAL_ERROR "x86-64: unexpected diagnostics\n${diagnostics}")
endif()

# An atomic access needs the natural alignment that the typedef gave up.
compile(atomic [=[
typedef u32 u32_any [[aligned(1)]];
global u32 load(in u32_any [[atomic]] *p) { return *p; }
]=] -target x86_64-unknown-linux-gnu)
if(status EQUAL 0 OR NOT diagnostics MATCHES "atomic\\.x:[0-9]+:[0-9]+: error: an atomic object cannot be accessed through an under-aligned")
    message(FATAL_ERROR "atomic access through a lowered typedef was not diagnosed\n${diagnostics}")
endif()
