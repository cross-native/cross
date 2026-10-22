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
expect_rejected(profile-fp-trap 2
    "profile fp_traps names unknown exception 'denormal'" [=[
profile "p" {
    fp_traps = ["invalid", "denormal"];
}
]=])
expect_rejected(profile-fp-trap-twice 2
    "profile fp_traps lists 'invalid' twice" [=[
profile "p" {
    fp_traps = ["invalid", "overflow", "invalid"];
}
]=])
expect_rejected(profile-fp-trap-list 2
    "model property 'fp_traps' must be a list" [=[
profile "p" {
    fp_traps = "invalid";
}
]=])
expect_rejected(profile-fp-operand 3
    "profile fp_denormal_operand must be 'ieee' or 'trap'" [=[
profile "p" {
    fp_traps = [];
    fp_denormal_operand = "flush";
}
]=])
expect_rejected(profile-fp-result 2
    "profile fp_denormal_result must be 'ieee' or 'flush'" [=[
profile "p" {
    fp_denormal_result = "trap";
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
# Member properties of rules.
expect_rejected(abi-member-banks-coerce 7
    "ABI model 'a' rule 'records' uses flatten-only merge/alignment properties" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    rule "records" {
        match = ["structure"];
        action = "coerce";
        bank = "integer";
        member_banks = ["integer"];
    }
}
]=])
expect_rejected(abi-member-bank 7
    "ABI model 'a' rule 'records' has an empty, duplicate, or unknown member bank" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    rule "records" {
        match = ["structure"];
        action = "flatten";
        member_banks = ["simd"];
    }
}
]=])
expect_rejected(abi-members 7 "ABI rule members must be 'any', 'only', or 'none'" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    rule "records" {
        match = ["union"];
        action = "flatten";
        members = "nested";
    }
}
]=])
expect_rejected(abi-padding-bank 7
    "ABI model 'a' rule 'records' names a padding bank without merge_banks or an unknown one" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    rule "records" {
        match = ["structure"];
        action = "flatten";
        padding_bank = "integer";
    }
}
]=])
expect_rejected(abi-alignment-limit 1
    "ABI natural_alignment_limit must be a power of two" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    natural_alignment_limit = 12;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    rule "integer" { match = ["integer"]; action = "direct"; bank = "integer"; }
}
]=])
expect_accepted(abi-member-rules [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi", "rsi"]; results = ["rax", "rdx"]; }
    rule "members" {
        match = ["union", "array"];
        action = "coerce";
        bank = "integer";
        members = "only";
    }
    rule "records" {
        match = ["structure"];
        action = "flatten";
        max_elements = 2;
        member_banks = ["integer"];
        cursor_alignment = "value";
        members = "none";
    }
    rule "chunks" {
        match = ["aggregate"];
        action = "flatten";
        unit_bits = 64;
        merge_banks = ["integer"];
        padding_bank = "integer";
    }
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
# A cursor aligns to a power of two of positions or to the value.
expect_rejected(abi-cursor-alignment 11
    "ABI rule cursor_alignment must be a power of two or \"value\"" [=[
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
        cursor_alignment = "even";
    }
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
    abi = "x";
    m.cmodel = "medium";
}
]=])

# --print-profiles shows a declared floating environment; the defaults are
# the masked IEEE environment.
file(WRITE "${OUTPUT}/environment.xm" [=[
profile "trapping" {
    fp_traps = ["invalid", "divide-by-zero"];
    fp_denormal_operand = "trap";
    fp_denormal_result = "flush";
}
profile "masked" {
    fp_traps = [];
    fp_denormal_operand = "ieee";
    fp_denormal_result = "ieee";
}
]=])
execute_process(
    COMMAND "${CC}" "--model=${OUTPUT}/environment.xm" --print-profiles
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status EQUAL 0 OR NOT stdout MATCHES
   "\ntrapping\n    fp_traps = invalid, divide-by-zero\n    fp_denormal_operand = trap\n    fp_denormal_result = flush\nmasked\n")
    message(FATAL_ERROR
        "--print-profiles does not show the floating environment\n${stdout}\n${stderr}")
endif()

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

# Integers are unsigned except where a property allows a sign: variadic home
# bases and state offsets.
expect_rejected(negative-unsigned 4 "model property 'stack_alignment' must be an unsigned integer" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = -16;
    stack_slot_bytes = 8;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    rule "integer" { match = ["integer"]; action = "direct"; bank = "integer"; }
}
]=])
expect_rejected(negative-option 2 "compiler option property 'f[.]inline-limit' cannot be negative" [=[
profile "p" {
    f.inline-limit = -3;
}
]=])
expect_rejected(state-stride-without-cursor 8 "ABI variadic state with a cursor requires a nonzero stride, and one without a cursor no stride" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    variadic_supported = true;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    variadic_state "top" { type = "void*"; kind = "cursor_address"; stride = 8; }
    rule "integer" { match = ["integer"]; action = "direct"; bank = "integer"; }
}
]=])
expect_rejected(state-limit-without-cursor 8 "ABI variadic state limit requires a cursor" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    variadic_supported = true;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    variadic_state "offset" { type = "u32"; kind = "cursor_offset"; base = 8; limit = 1; }
    rule "integer" { match = ["integer"]; action = "direct"; bank = "integer"; }
}
]=])
expect_rejected(home-base-without-bank 1 "a negative variadic_home_base requires a home bank" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    variadic_supported = true;
    variadic_home_base = -16;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi", "rsi"]; results = ["rax"]; }
    rule "integer" { match = ["integer"]; action = "direct"; bank = "integer"; }
}
]=])
expect_rejected(home-straddling 8 "ABI model 'a' places variadic home slots on both sides of the incoming arguments" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    variadic_supported = true;
    variadic_home_bank = "integer";
    variadic_home_base = -8;
    variadic_home_stride = 8;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi", "rsi"]; results = ["rax"]; }
    rule "integer" { match = ["integer"]; action = "direct"; bank = "integer"; }
}
]=])
expect_accepted(signed-variadic-offsets [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    variadic_supported = true;
    variadic_home_bank = "integer";
    variadic_home_base = -16;
    variadic_home_stride = 8;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi", "rsi"]; results = ["rax"]; }
    variadic_state "remaining" { type = "u32"; kind = "cursor_offset"; cursor = "integer"; base = 16; stride = -8; limit = 2; }
    variadic_state "top" { type = "void*"; kind = "cursor_address"; }
    rule "integer" { match = ["integer"]; action = "direct"; bank = "integer"; }
}
]=])
# A record_address state names fields, other states of its entry, once
# each; it has a pointer type and none of the cursor properties.
set(record_abi_head [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    variadic_supported = true;
    variadic_save_banks = ["integer"];
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi", "rsi"]; results = ["rax"]; }
    rule "integer" { match = ["integer"]; action = "direct"; bank = "integer"; }
    variadic_state "offset" { type = "u32"; kind = "cursor_offset"; cursor = "integer"; stride = 8; }
    variadic_state "area" { type = "void*"; kind = "register_save_address"; }
]=])
set(record_fields "ABI variadic state fields are required by, and only valid for, a record_address state")
set(record_shape "ABI variadic record_address state requires a pointer type and takes no cursor, base, stride, limit, or llvm_va_list_offset")
set(record_members "ABI variadic state 'list' fields must name other states once, none of kind record_address")
function(expect_record_rejected label message state)
    expect_rejected(${label} 12 "${message}" "${record_abi_head}    ${state}\n}\n")
endfunction()
expect_record_rejected(record-without-fields "${record_fields}" [=[variadic_state "list" { type = "void*"; kind = "record_address"; }]=])
expect_record_rejected(fields-without-record "${record_fields}" [=[variadic_state "list" { type = "void*"; kind = "stack_address"; fields = ["offset"]; }]=])
expect_record_rejected(record-empty-fields "${record_fields}" [=[variadic_state "list" { type = "void*"; kind = "record_address"; fields = []; }]=])
expect_record_rejected(record-scalar "${record_shape}" [=[variadic_state "list" { type = "u64"; kind = "record_address"; fields = ["offset"]; }]=])
expect_record_rejected(record-cursor "${record_shape}" [=[variadic_state "list" { type = "void*"; kind = "record_address"; cursor = "integer"; stride = 8; fields = ["offset"]; }]=])
expect_record_rejected(record-unknown-field "${record_members}" [=[variadic_state "list" { type = "void*"; kind = "record_address"; fields = ["offset", "missing"]; }]=])
expect_record_rejected(record-repeated-field "${record_members}" [=[variadic_state "list" { type = "void*"; kind = "record_address"; fields = ["offset", "offset"]; }]=])
expect_record_rejected(record-of-record "${record_members}" [=[variadic_state "list" { type = "void*"; kind = "record_address"; fields = ["list"]; }]=])
expect_accepted(record-state "${record_abi_head}    variadic_state \"list\" { type = \"void*\"; kind = \"record_address\"; fields = [\"area\", \"offset\"]; alignment = 16; }\n}\n")
