# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Loading checks every model entry, selected or not, and reports each fault at
# its file and line with the same text under cc and cpp.

foreach(required CC CPP OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT}")
set(source "${OUTPUT}/source.x")
file(WRITE "${source}" "global u32 identity(u32 value) {
    return value;
}
")

function(compile label)
    execute_process(
        COMMAND "${CC}" ${ARGN} -target x86_64-unknown-linux-gnu -S
                "${source}" -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    set(status "${status}" PARENT_SCOPE)
    set(stderr "${stderr}" PARENT_SCOPE)
    set(output "${stdout}\n${stderr}" PARENT_SCOPE)
endfunction()

# The model `text` must fail to load with `expected` at `line` of its file.
function(expect_rejected label line expected text)
    set(model "${OUTPUT}/${label}.xm")
    file(WRITE "${model}" "${text}")
    compile(${label} "--model=${model}")
    if(status EQUAL 0 OR
       NOT stderr MATCHES "${label}[.]xm:${line}: error: ${expected}")
        message(FATAL_ERROR
            "${label} did not report '${expected}' at line ${line}\n${output}")
    endif()
endfunction()

function(expect_accepted label text)
    set(model "${OUTPUT}/${label}.xm")
    file(WRITE "${model}" "${text}")
    compile(${label} "--model=${model}")
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} was rejected\n${output}")
    endif()
endfunction()

# Profiles and presets that no compilation selects.
expect_rejected(profile-option 2 "unknown option 'f[.]no-such-option'" [=[
profile "p" {
    f.no-such-option = true;
}
]=])
expect_rejected(profile-target-option 2
    "unknown option 'm[.]no-such-target-option'" [=[
profile "p" {
    m.no-such-target-option = 3;
}
]=])
expect_rejected(profile-other-target-option 3
    "unknown option 'm[.]red-zone' for mips" [=[
profile "p" {
    target = "mips-unknown-elf";
    m.red-zone = true;
}
]=])
expect_rejected(profile-value 2
    "invalid value for option 'f[.]inline-limit': expects an unsigned integer" [=[
profile "p" {
    f.inline-limit = "lots";
}
]=])
expect_rejected(profile-abi 1 "profile 'p' names unknown ABI 'no-such-abi'" [=[
profile "p" {
    abi = "no-such-abi";
}
]=])
expect_rejected(profile-abi-architecture 1
    "profile 'p' names unknown ABI 'o32' for x86-64" [=[
profile "p" {
    target = "x86_64-unknown-linux-gnu";
    abi = "o32";
}
]=])
expect_rejected(profile-mangling 1
    "profile 'p' names unknown mangling 'no-such-mangling'" [=[
profile "p" {
    mangling = "no-such-mangling";
}
]=])
expect_rejected(profile-preset 1
    "profile 'p' names unknown optimization preset 'no-such-preset'" [=[
profile "p" {
    optimization = "no-such-preset";
}
]=])
expect_rejected(profile-target 1
    "profile 'p' names unimplemented target 'arm-none-eabi'" [=[
profile "p" {
    target = "arm-none-eabi";
}
]=])
expect_rejected(preset-parent 1
    "optimization preset 'o' inherits unknown preset 'no-such-parent'" [=[
optimization "o" {
    inherits = "no-such-parent";
}
]=])
expect_rejected(preset-cycle 1
    "optimization inheritance cycle: cycle-a -> cycle-b -> cycle-a" [=[
optimization "cycle-a" {
    inherits = "cycle-b";
}
optimization "cycle-b" {
    inherits = "cycle-a";
}
]=])
expect_rejected(preset-not-presettable 2
    "optimization presets cannot set option 'f[.]function-sections'" [=[
optimization "o" {
    f.function-sections = true;
}
]=])
expect_rejected(preset-option 2 "unknown option 'f[.]bogus'" [=[
optimization "o" {
    f.bogus = 1;
}
]=])
expect_rejected(preset-architecture 1
    "optimization preset 'o' names unknown architecture 'x86_64'" [=[
optimization "o" {
    targets = ["x86_64"];
    m.tune = "znver3";
}
]=])
expect_rejected(preset-target-option 3
    "unknown option 'm[.]prefer-vector-width' for mips" [=[
optimization "o" {
    targets = ["x86-64", "mips"];
    m.prefer-vector-width = "256";
}
]=])

# ABI entries: required properties, architecture names, rule features, and
# ELF ABI tags.
expect_rejected(abi-missing-class 6 "missing required model property 'class'"
    [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    bank "integer" { register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    rule "integer" { match = ["integer"]; action = "direct"; bank = "integer"; }
}

profile "after" {
}
]=])
expect_rejected(abi-architecture 2 "unknown architecture 'x86_64'" [=[
abi "a" {
    architecture = "x86_64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    rule "integer" { match = ["integer"]; action = "direct"; bank = "integer"; }
}
]=])
expect_rejected(abi-feature 7
    "ABI model 'a' rule 'integer' names unknown x86-64 feature 'avx-typo'" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    rule "integer" {
        match = ["integer"];
        action = "direct";
        bank = "integer";
        requires_features = ["avx-typo"];
    }
}
]=])
expect_rejected(abi-forbidden-feature 7
    "ABI model 'a' rule 'integer' names unknown mips feature 'mips99'" [=[
abi "a" {
    architecture = "mips";
    address_bits = 32;
    stack_alignment = 8;
    stack_slot_bytes = 4;
    bank "integer" { class = "integer"; register_bits = 32; arguments = ["a0"]; results = ["v0"]; }
    rule "integer" {
        match = ["integer"];
        action = "direct";
        bank = "integer";
        forbids_features = ["mips99"];
    }
}
]=])
expect_rejected(abi-tag 4
    "ABI model 'a' requests ELF ABI tag 'eabi32', which x86-64 does not define" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    elf_abi_tag = "eabi32";
    stack_alignment = 16;
    stack_slot_bytes = 8;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    rule "integer" { match = ["integer"]; action = "direct"; bank = "integer"; }
}
]=])

# Valid entries load whether or not they apply: entries of another
# architecture, a parent declared later, and a profile without a target whose
# option suits only some architectures.
expect_accepted(unselected [=[
abi "user-mips" {
    architecture = "mips";
    address_bits = 32;
    elf_abi_tag = "eabi32";
    stack_alignment = 8;
    stack_slot_bytes = 4;
    bank "integer" { class = "integer"; register_bits = 32; arguments = ["a0"]; results = ["v0"]; }
    rule "integer" {
        match = ["integer"];
        action = "direct";
        bank = "integer";
        requires_features = ["mips2"];
        forbids_features = ["soft-float"];
    }
}
optimization "mips-child" {
    inherits = "mips-parent";
    targets = ["mips"];
    m.tune = "vr4300";
}
optimization "mips-parent" {
    inherits = "O2";
}
profile "user-mips" {
    target = "mips-unknown-elf";
    abi = "user-mips";
    optimization = "mips-child";
    m.arch = "r6000";
}
profile "any-target" {
    abi = "cross";
    m.cmodel = "medium";
}
]=])

# A profile may name a preset of a file loaded after its own.
file(WRITE "${OUTPUT}/first.xm" [=[
profile "first" {
    optimization = "second";
}
]=])
file(WRITE "${OUTPUT}/second.xm" [=[
optimization "second" {
    inherits = "O1";
}
]=])
compile(later-file "--model=${OUTPUT}/first.xm" "--model=${OUTPUT}/second.xm")
if(NOT status EQUAL 0)
    message(FATAL_ERROR "a reference to a later file was rejected\n${output}")
endif()

# Equally specific default_for patterns are an error naming both profiles,
# also when -mprofile names one; a more specific pattern wins.
file(WRITE "${OUTPUT}/ambiguous.xm" [=[
profile "my-x86" {
    default_for = ["x86_64-*"];
    abi = "sysv_abi";
}
]=])
set(ambiguous
    "^cc: error: profiles 'x86_64-elf' [(]shipped:x86_64[.]xm:[0-9]+[)] and 'my-x86' [(][^)]*ambiguous[.]xm:1[)] are equally specific defaults for target 'x86_64-unknown-linux-gnu'")
foreach(selection none -mprofile=default -mprofile=x86_64-elf)
    set(flags "--model=${OUTPUT}/ambiguous.xm")
    if(NOT selection STREQUAL none)
        list(APPEND flags ${selection})
    endif()
    compile(ambiguous ${flags})
    if(status EQUAL 0 OR NOT stderr MATCHES "${ambiguous}")
        message(FATAL_ERROR
            "ambiguous default profiles (${selection}) were not named\n${output}")
    endif()
endforeach()
file(WRITE "${OUTPUT}/specific.xm" [=[
profile "linux-simple" {
    default_for = ["x86_64-unknown-linux-gnu"];
    abi = "sysv_abi";
    mangling = "simple";
}
]=])
compile(specific "--model=${OUTPUT}/specific.xm")
file(READ "${OUTPUT}/specific.s" specific_assembly)
if(NOT status EQUAL 0 OR NOT specific_assembly MATCHES "_XN8identity")
    message(FATAL_ERROR
        "the most specific default profile did not apply\n${output}\n${specific_assembly}")
endif()

# cpp loads the same models and reports the same lines; its own errors name
# cpp.
foreach(tool CC CPP)
    execute_process(
        COMMAND "${${tool}}" "--model=${OUTPUT}/abi-architecture.xm"
                "${source}" -o "${OUTPUT}/tool.out"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0)
        message(FATAL_ERROR "${tool} accepted an invalid model\n${stdout}")
    endif()
    set(${tool}_error "${stderr}")
endforeach()
if(NOT CC_error STREQUAL CPP_error OR
   NOT CPP_error MATCHES "^[^\n]*abi-architecture[.]xm:2: error: ")
    message(FATAL_ERROR
        "cc and cpp report model errors differently\n${CC_error}\n${CPP_error}")
endif()
execute_process(
    COMMAND "${CPP}" "--model=${OUTPUT}/missing.xm" "${source}"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(status EQUAL 0 OR
   NOT stderr MATCHES "^cpp: error: cannot find Cross model file")
    message(FATAL_ERROR "cpp errors do not name cpp\n${stderr}")
endif()
