# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Loading checks of uniform_members, max_members, the exhaust register
# failure, call_preserved_views, and the feature modifiers of m.arch.
foreach(required CC MODEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

execute_process(
    COMMAND "${CC}" "--model=${MODEL}" -target x86_64-unknown-linux-gnu --print-abis
    RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
if(NOT status EQUAL 0 OR NOT out MATCHES "\n  homogeneous_test  aliases: " OR
   NOT out MATCHES "\n  homogeneous_stack  aliases: ")
    message(FATAL_ERROR "the homogeneous ABIs did not load\n${out}\n${err}")
endif()

# The model `text` must fail to load with `expected` at `line` of its file.
function(expect_rejected label line expected text)
    set(model "${OUTPUT}/${label}.xm")
    file(WRITE "${model}" "${text}")
    execute_process(COMMAND "${CC}" "--model=${model}" --print-targets
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(status EQUAL 0 OR NOT err MATCHES "${label}[.]xm:${line}: error: ${expected}")
        message(FATAL_ERROR "${label} did not report '${expected}' at line ${line}\n${out}\n${err}")
    endif()
endfunction()

function(expect_accepted label text)
    set(model "${OUTPUT}/${label}.xm")
    file(WRITE "${model}" "${text}")
    execute_process(COMMAND "${CC}" "--model=${model}" --print-profiles
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} was rejected\n${out}\n${err}")
    endif()
endfunction()

expect_rejected(uniform-coerce 7
    "ABI model 'a' rule 'records' uses uniform_members or max_members without flatten" [=[
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
        uniform_members = true;
    }
}
]=])
expect_rejected(max-members-direct 7
    "ABI model 'a' rule 'records' uses uniform_members or max_members without flatten" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    rule "records" {
        match = ["integer"];
        action = "direct";
        bank = "integer";
        max_members = 2;
    }
}
]=])
expect_rejected(uniform-merge 7
    "ABI model 'a' rule 'records' cannot merge uniform members" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    rule "records" {
        match = ["structure"];
        action = "flatten";
        unit_bits = 64;
        merge_banks = ["integer"];
        uniform_members = true;
    }
}
]=])
expect_rejected(failure 1
    "ABI register failure must be 'stack', 'partial', 'exhaust', or 'error'" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    argument_register_failure = "spill";
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    rule "integer" { match = ["integer"]; action = "direct"; bank = "integer"; }
}
]=])
expect_rejected(max-stack-alignment 7
    "ABI rule max_stack_alignment must be a power of two" [=[
abi "a" {
    architecture = "x86-64";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    bank "integer" { class = "integer"; register_bits = 64; arguments = ["rdi"]; results = ["rax"]; }
    rule "memory" {
        match = ["any"];
        action = "stack";
        max_stack_alignment = 24;
    }
}
]=])
foreach(case "clobbered;xmm0" "unknown;xmm32" "twice;xmm6\", \"ymm6")
    list(GET case 0 label)
    list(GET case 1 views)
    expect_rejected(views-${label} 7
        "ABI model 'a' call_preserved_views names '[^']+', which is not a view of a register that call_clobbers omits, or a second view of one"
        "abi \"a\" {
    architecture = \"x86-64\";
    address_bits = 64;
    stack_alignment = 16;
    stack_slot_bytes = 8;
    call_clobbers = [\"rax\", \"zmm0\"];
    call_preserved_views = [\"${views}\"];
    bank \"integer\" { class = \"integer\"; register_bits = 64; arguments = [\"rdi\"]; results = [\"rax\"]; }
    rule \"integer\" { match = [\"integer\"]; action = \"direct\"; bank = \"integer\"; }
}
")
endforeach()

# m.arch takes feature modifiers; m.tune names a cpu entry only.
expect_accepted(modifiers [=[
profile "board" {
    target = "aarch64-none-elf";
    m.arch = "cortex-a57+crypto+nofp";
    m.tune = "cortex-a72";
}
]=])
expect_rejected(modifier 3
    "unknown aarch64 feature 'nothing' in 'armv8-a[+]nothing' for option 'm[.]arch'" [=[
profile "board" {
    target = "aarch64-none-elf";
    m.arch = "armv8-a+nothing";
}
]=])
expect_rejected(tune-modifier 3
    "unknown cpu 'cortex-a57[+]crc' for option 'm[.]tune'" [=[
profile "board" {
    target = "aarch64-none-elf";
    m.tune = "cortex-a57+crc";
}
]=])
