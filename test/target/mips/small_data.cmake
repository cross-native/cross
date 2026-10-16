# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# MIPS small data: placement by size and kind, section opt-in and opt-out,
# declarations with and without a data-base section, -fdata-sections names,
# %gp_rel loads, stores, and addresses under o32 and n64, the reserved $gp,
# and the serializers' placement.
foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT}")

function(compile label)
    execute_process(COMMAND "${CC}" -S ${ARGN} "${SOURCE}"
            -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status})\n${stderr}")
    endif()
    file(READ "${OUTPUT}/${label}.s" text)
    set(text "${text}" PARENT_SCOPE)
endfunction()

function(expect label text)
    foreach(pattern IN LISTS ARGN)
        if(NOT text MATCHES "${pattern}")
            message(FATAL_ERROR "${label} lacks '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

function(reject label text)
    foreach(pattern IN LISTS ARGN)
        if(text MATCHES "${pattern}")
            message(FATAL_ERROR "${label} contains '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

# The section an object is defined in, as the line that opens it.
function(expect_section label text section object)
    if(NOT text MATCHES "(^|\n)${section}\n[.]p2align [0-9]+\n([.](globl|local) ${object}\n)?[.]type ${object},@object\n")
        message(FATAL_ERROR "${label}: '${object}' is not in '${section}'\n${text}")
    endif()
endfunction()

set(sdata "[.]section \"[.]sdata\",\"aw\",@progbits")
set(sbss "[.]section \"[.]sbss\",\"aw\",@nobits")
set(hidden "__cross_static_[0-9]+_hidden")
set(string "__cross_static_[0-9]+__string_[0-9]+_0")

# The shipped mips-gp entry places writable objects of at most eight bytes:
# initialized ones in .sdata, zero ones in .sbss.
compile(o32 -O2 -mprofile=vr4300-o32 -mdata-bases=mips-gp)
expect_section(o32 "${text}" "${sbss}" counter)
expect_section(o32 "${text}" "${sdata}" seed)
expect_section(o32 "${text}" "${sbss}" total)
expect_section(o32 "${text}" "${sdata}" "${hidden}")
expect_section(o32 "${text}" "[.]bss" nine)
expect_section(o32 "${text}" "[.]data" initialized_nine)
expect_section(o32 "${text}" "[.]section \"[.]rodata\",\"a\",@progbits" limit)
expect_section(o32 "${text}" "${sdata}" table)
expect_section(o32 "${text}" "[.]data" kept)
expect_section(o32 "${text}" "[.]section \"[.]rodata\",\"a\",@progbits" "${string}")
expect(o32 "${text}"
    "\tlw\t[$][a-z0-9]+,%gp_rel[(]counter[)][(][$]gp[)]\n"
    "\tsw\t[$][a-z0-9]+,%gp_rel[(]counter[)][(][$]gp[)]\n"
    "\tld\t[$][a-z0-9]+,%gp_rel[(]total[)][(][$]gp[)]\n"
    "\tsd\t[$][a-z0-9]+,%gp_rel[(]total[)][(][$]gp[)]\n"
    "\tlw\t[$][a-z0-9]+,%gp_rel[(]${hidden}[)][(][$]gp[)]\n"
    "\tlw\t[$][a-z0-9]+,%gp_rel[(]outside_small[)][(][$]gp[)]\n"
    "\tlw\t[$][a-z0-9]+,%gp_rel[(]outside_zero[)][(][$]gp[)]\n"
    "\tlui\t[$][a-z0-9]+,%hi[(]outside[)]\n"
    "\tlui\t[$][a-z0-9]+,%hi[(]kept[)]\n"
    "\tlui\t[$][a-z0-9]+,%hi[(]nine[)]\n"
    "\tlui\t[$][a-z0-9]+,%hi[(]limit[)]\n"
    "seed_address:\n[^\n]*\n[^\n]*\n\tjr\t[$]ra\n\taddiu\t[$]v0,[$]gp,%gp_rel[(]seed[)]\n"
    "second:\n[^\n]*\n[^\n]*\n\tjr\t[$]ra\n\tlw\t[$]v0,%gp_rel[(]table[+]4[)][(][$]gp[)]\n"
    "\taddiu\t[$][a-z0-9]+,[$]gp,%gp_rel[(]table[)]\n")
reject(o32 "${text}"
    "%(hi|lo)[(](counter|seed|total|table|outside_small|outside_zero|${hidden})"
    "[$]gp,-?[0-9]+[(][$]sp[)]")

compile(o32-O0 -O0 -mprofile=vr4300-o32 -mdata-bases=mips-gp)
expect(o32-O0 "${text}"
    "\tlw\t[$][a-z0-9]+,%gp_rel[(]counter[)][(][$]gp[)]\n"
    "\tsw\t[$][a-z0-9]+,%gp_rel[(]counter[)][(][$]gp[)]\n"
    "\taddiu\t[$][a-z0-9]+,[$]gp,%gp_rel[(]seed[)]\n"
    "\taddiu\t[$][a-z0-9]+,[$]gp,%gp_rel[(]table[)]\n")
reject(o32-O0 "${text}" "%(hi|lo)[(](counter|seed|total|table|outside_small)")

# Before MIPS III a doubleword is a word pair off one address.
compile(r3000 -O2 -mprofile=r3000-o32 -mdata-bases=mips-gp)
expect(r3000 "${text}" "\taddiu\t[$]t0,[$]gp,%gp_rel[(]total[)]\n")

# 64-bit addresses use the doubleword forms and numeric register names.
compile(n64 -O2 -mprofile=mips64-n64 -mdata-bases=mips-gp)
expect(n64 "${text}"
    "\tlw\t[$][0-9]+,%gp_rel[(]counter[)][(][$]28[)]\n"
    "\tld\t[$][0-9]+,%gp_rel[(]total[)][(][$]28[)]\n"
    "\tdaddiu\t[$]2,[$]28,%gp_rel[(]seed[)]\n"
    "\tlw\t[$]2,%gp_rel[(]table[+]4[)][(][$]28[)]\n")
compile(n64-large -O2 -mprofile=mips64-n64 -mcmodel=large -mdata-bases=mips-gp)
expect(n64-large "${text}"
    "\tlw\t[$][0-9]+,%gp_rel[(]counter[)][(][$]28[)]\n"
    "\tdaddiu\t[$]2,[$]28,%gp_rel[(]seed[)]\n"
    "%highest[(]outside[)]")

# -fdata-sections splits the placed objects as GCC does; an explicit section
# stays whole.
compile(sections -O2 -mprofile=vr4300-o32 -mdata-bases=mips-gp -fdata-sections)
expect_section(sections "${text}" "[.]section \"[.]sbss[.]counter\",\"aw\",@nobits" counter)
expect_section(sections "${text}" "[.]section \"[.]sdata[.]seed\",\"aw\",@progbits" seed)
expect_section(sections "${text}" "[.]section \"[.]sbss[.]total\",\"aw\",@nobits" total)
expect_section(sections "${text}" "[.]section \"[.]sdata[.]${hidden}\",\"aw\",@progbits" "${hidden}")
expect_section(sections "${text}" "${sdata}" table)
expect_section(sections "${text}" "[.]section \"[.]bss[.]nine\",\"aw\",@nobits" nine)

# Without a selected entry nothing is placed and nothing uses $gp.
compile(none -O2 -mprofile=vr4300-o32)
expect_section(none "${text}" "[.]bss" counter)
expect_section(none "${text}" "${sdata}" table)
expect(none "${text}" "\tlui\t[$][a-z0-9]+,%hi[(]outside_small[)]\n")
reject(none "${text}" "%gp_rel" "[.]sbss")

# User entries: the first listed entry that admits an object places it.
set(model "${OUTPUT}/entries.xm")
file(WRITE "${model}" [=[
data_base "gp-ro" {
    base = "gp";
    sections = [".srodata"];
    max_size = 4;
    kinds = ["readonly"];
}

data_base "gp-all" {
    base = "gp";
    sections = [".sdata"];
    max_size = 16;
    kinds = ["writable", "readonly", "literal"];
}
]=])
compile(readonly -O2 "--model=${model}" -mprofile=vr4300-o32
        -mdata-bases=mips-gp,gp-ro)
expect_section(readonly "${text}" "[.]section \"[.]srodata\",\"a\",@progbits" limit)
expect_section(readonly "${text}" "${sdata}" seed)
expect(readonly "${text}"
    "limit_address:\n[^\n]*\n[^\n]*\n\tjr\t[$]ra\n\taddiu\t[$]v0,[$]gp,%gp_rel[(]limit[)]\n")

# A one-section entry holds zero objects too; literal admits the
# compiler's constants: a string and, at -Oz, a pooled double.
compile(literal -Oz "--model=${model}" -mprofile=vr4300-o32 -mdata-bases=gp-all)
expect_section(literal "${text}" "${sdata}" nine)
expect_section(literal "${text}" "${sdata}" counter)
expect_section(literal "${text}" "${sdata}" limit)
expect_section(literal "${text}" "${sdata}" "${string}")
expect(literal "${text}"
    "\taddiu\t[$][a-z0-9]+,[$]gp,%gp_rel[(]${string}[)]\n")
if(NOT text MATCHES
   "\tldc1\t[$]f[0-9]+,%gp_rel[(]([.]L[^)]+[.]float[.]0)[)][(][$]gp[)]\n")
    message(FATAL_ERROR "literal loads no pooled double off $gp\n${text}")
endif()
expect(literal "${text}" "${sdata}\n[.]p2align 3\n${CMAKE_MATCH_1}:\n")
reject(literal "${text}" "[.]sbss" "[.]bss\n")

# An o32 function that calls a Cross function no longer saves the $gp that
# the Cross ABI may clobber: the base register is reserved.
set(bridge "${OUTPUT}/bridge.x")
file(WRITE "${bridge}" "global u32 counter;
[[abi(\"cross\")]] global u32 cross_callee(u32 x);

global u32 o32_caller(u32 x) {
    counter = counter + cross_callee(x);
    return counter;
}
")
foreach(mode base sdata)
    set(flags "")
    if(mode STREQUAL "sdata")
        set(flags -mdata-bases=mips-gp)
    endif()
    execute_process(COMMAND "${CC}" -S -O2 -mprofile=vr4300-o32 ${flags}
            "${bridge}" -o "${OUTPUT}/bridge-${mode}.s"
        RESULT_VARIABLE status ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "bridge-${mode} failed\n${stderr}")
    endif()
    file(READ "${OUTPUT}/bridge-${mode}.s" text)
    if(mode STREQUAL "base")
        expect(bridge-base "${text}" "\tsd\t[$]gp,[0-9]+[(][$]sp[)]\n")
    else()
        reject(bridge-sdata "${text}" "[$]gp,[0-9]+[(][$]sp[)]")
    endif()
endforeach()

# A naked function reaches small data through $gp as well.
set(naked "${OUTPUT}/naked.x")
file(WRITE "${naked}" "global u32 flag = 1u32;

[[naked, clobber(\"t0\")]]
global u32 peek() -> \"v0\" {
    register u32 result \"v0\";
    register void *link \"ra\";
    result = flag;
    $::_jr(link);
}
")
execute_process(COMMAND "${CC}" -S -O2 -mprofile=vr4300-o32 -mdata-bases=mips-gp
        "${naked}" -o "${OUTPUT}/naked.s"
    RESULT_VARIABLE status ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "naked failed\n${stderr}")
endif()
file(READ "${OUTPUT}/naked.s" text)
expect(naked "${text}" "\tlw\t[$]v0,%gp_rel[(]flag[)][(][$]gp[)]\n")

# The base register carries no ABI value and no manual location names it.
function(expect_error label expected text)
    file(WRITE "${OUTPUT}/${label}.x" "${text}")
    execute_process(COMMAND "${CC}" -S -O2 "--model=${OUTPUT}/abi.xm"
            -mprofile=vr4300-o32 -mdata-bases=mips-gp "${OUTPUT}/${label}.x"
            -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "${label}[.]x:${expected}")
        message(FATAL_ERROR "${label} did not report '${expected}'\n${stderr}")
    endif()
endfunction()
file(WRITE "${OUTPUT}/abi.xm" [=[
abi "gp_args" {
    architecture = "mips";
    address_bits = 32;
    argument_register_failure = "partial";
    result_register_failure = "error";
    stack_layout = "packed";
    argument_stack_base = 0;
    stack_alignment = 8;
    stack_slot_bytes = 4;
    return_address_bytes = 0;
    stack_order = ["arguments"];
    call_clobbers = ["at", "v0", "v1", "a0", "a1", "ra", "memory"];
    bank "integer" {
        class = "integer";
        register_bits = 32;
        arguments = ["a0", "gp"];
        results = ["v0", "v1"];
    }
    rule "integer" {
        match = ["integer", "pointer"];
        action = "split";
        bank = "integer";
        max_bits = 64;
        unit_bits = 32;
    }
    rule "argument-memory" {
        match = ["any"];
        action = "stack";
        applies_to = ["arguments"];
    }
}
]=])
expect_error(function-abi
    "1:[0-9]+: error: ABI 'gp_args' carries arguments or results in 'gp', the base register of data_base 'mips-gp'"
    "[[abi(\"gp_args\")]] global u32 two(u32 a, u32 b) {
    return a + b;
}
")
expect_error(location
    "2:[0-9]+: error: the parameter location names 'gp', the base register of data_base 'mips-gp'"
    "[[naked, clobber(\"t0\")]]
global void take(in u32 value \"gp\") {
    register void *link \"ra\";
    $::_jr(link);
}
")
expect_error(binding
    "4:[0-9]+: error: the register binding names 'gp', the base register of data_base 'mips-gp'"
    "[[naked, clobber(\"t0\")]]
global u32 base() -> \"v0\" {
    register u32 result \"v0\";
    register u32 small \"gp\";
    register void *link \"ra\";
    result = small;
    $::_jr(link);
}
")

# The serializers place objects where native emission does.
if(LLVM_TEXT)
    execute_process(COMMAND "${CC}" -emit-llvm -O2 -mprofile=vr4300-o32
            -mdata-bases=mips-gp "${SOURCE}" -o "${OUTPUT}/llvm.ll"
        RESULT_VARIABLE status ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "LLVM serialization failed\n${stderr}")
    endif()
    file(READ "${OUTPUT}/llvm.ll" text)
    expect(llvm "${text}"
        "\n@\"counter\" = global i32 zeroinitializer, section \"[.]sbss\", align 4\n"
        "\n@\"seed\" = global i32 7, section \"[.]sdata\", align 4\n"
        "\n@\"kept\" = global i32 9, section \"[.]data\", align 4\n")
    reject(llvm "${text}" "@\"nine\" = [^\n]*section")
endif()
if(GIMPLE_TEXT)
    execute_process(COMMAND "${CC}" -emit-gimple -O2 -mprofile=vr4300-o32
            -mdata-bases=mips-gp "${SOURCE}" -o "${OUTPUT}/gimple.c"
        RESULT_VARIABLE status ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "GIMPLE serialization failed\n${stderr}")
    endif()
    file(READ "${OUTPUT}/gimple.c" text)
    expect(gimple "${text}"
        "__asm__[(]\"counter\"[)] __attribute__[(][(]section[(]\"[.]sbss\"[)]"
        "__asm__[(]\"seed\"[)] __attribute__[(][(]section[(]\"[.]sdata\"[)]")
endif()
