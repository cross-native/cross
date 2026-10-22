# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Diagnostics of [[constructor]] and [[destructor]]: their subjects, their
# priorities, the entry type of the object form, and profiles without tables.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

set(case_count 0)
# LABEL, the expected "line:column: error: message" pattern, the source,
# then further cc options.
function(expect_error label expected source)
    set(path "${OUTPUT}/${label}.x")
    file(WRITE "${path}" "${source}")
    execute_process(COMMAND "${CC}" -target x86_64-unknown-linux-gnu ${ARGN}
                    -S "${path}" -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "${label}[.]x:${expected}")
        message(FATAL_ERROR "${label} did not report '${expected}'\n${stderr}")
    endif()
endfunction()

set(entry "[[abi(\"sysv_abi\")]] typedef void (*entry)();
[[abi(\"sysv_abi\")]] static void run() {}
")
expect_error(declaration "1:3: error: attribute 'constructor' requires a function definition"
    "[[constructor]] void set_up();\n")
expect_error(parameters "1:3: error: a function with attribute 'destructor' takes no parameters and returns void"
    "[[destructor]] static void tear_down(in u32 code) {}\n")
expect_error(result "1:3: error: a function with attribute 'constructor' takes no parameters and returns void"
    "[[constructor]] static u32 set_up() { return 0u32; }\n")
expect_error(generic "1:3: error: attribute 'constructor' is not valid on a generic function"
    "[[constructor]] static void set_up<T>() {}\n")
expect_error(eval-only "1:14: error: attribute 'constructor' requires runtime symbol, ABI transport or entry/exit machinery; it is not valid on an eval_only function"
    "[[eval_only, constructor]] static void set_up() {}\n")
expect_error(repeated "1:16: error: attribute 'constructor' appears more than once"
    "[[constructor, constructor(101)]] static void set_up() {}\n")
expect_error(two-priorities "1:3: error: attribute 'destructor' takes at most one priority"
    "[[destructor(101, 102)]] static void tear_down() {}\n")
expect_error(priority-range "1:3: error: constructor priority must be an integer from 0 to 65535"
    "[[constructor(65536)]] static void set_up() {}\n")
expect_error(negative-priority "1:3: error: destructor priority must be an integer from 0 to 65535"
    "[[destructor(-1)]] static void tear_down() {}\n")
expect_error(object-type "2:3: error: an object with attribute 'constructor' must have the entry type of table 'x86_64-elf-init', a pointer to a function without parameters or result under ABI 'sysv_abi', or be an array of such pointers"
    "static void set_up() {}\n[[constructor]] static void (*pointer)() = set_up;\n")
expect_error(object-initializer "3:3: error: an object with attribute 'constructor' requires an initializer"
    "${entry}[[constructor]] static entry pointer;\n")
expect_error(object-section "3:16: error: attribute 'section' is not valid with attribute 'constructor'"
    "${entry}[[constructor, section(\".init\")]] static entry pointer = run;\n")
expect_error(object-both "3:16: error: an object cannot be both a constructor and a destructor entry"
    "${entry}[[constructor, destructor]] static entry pointer = run;\n")
expect_error(local "1:25: error: attribute 'constructor' is not valid on a local object"
    "global void outer() { [[constructor]] static u32 x = 1u32; (void)x; }\n")
expect_error(record "1:14: error: attribute 'destructor' is not valid on a record definition"
    "struct Box [[destructor]] { u32 value; };\n")

# A profile that names no tables makes both attributes errors.
file(WRITE "${OUTPUT}/bare.xm" [=[
profile "bare-elf" {
    target = "x86_64-unknown-linux-gnu";
    abi = "x";
    mangling = "cross";
}
]=])
expect_error(no-table "1:3: error: the selected profile names no constructor table"
    "[[constructor]] static void set_up() {}\n" "--model=${OUTPUT}/bare.xm" -mprofile=bare-elf)
expect_error(no-table-object "3:3: error: the selected profile names no destructor table"
    "${entry}[[destructor]] static entry pointer = run;\n" "--model=${OUTPUT}/bare.xm"
    -mprofile=bare-elf)
