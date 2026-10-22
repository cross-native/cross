# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Initializer-table entries: the shipped tables and their inspection, user
# tables that profiles select, and the checks of entries and references.
foreach(required CC CPP OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
set(source "${OUTPUT}/source.x")
file(WRITE "${source}" "global u32 state;

[[constructor]] global void set_up() { state = 1u32; }
[[constructor(101)]] static void early() { state = 2u32; }
")

function(run label)
    execute_process(COMMAND "${CC}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed (${status})\n${stdout}\n${stderr}")
    endif()
    set(stdout "${stdout}" PARENT_SCOPE)
endfunction()

function(expect label text)
    foreach(pattern IN LISTS ARGN)
        if(NOT text MATCHES "${pattern}")
            message(FATAL_ERROR "${label} lacks '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

run(print-init-tables --print-init-tables)
expect(print-init-tables "${stdout}"
    "(^|\n)x86_64-elf-init  section: [.]init_array  priority_section: [.]init_array[.]  abi: sysv_abi  source: shipped:x86_64[.]xm:[0-9]+\n"
    "\nx86_64-mingw-fini  section: [.]dtors  priority_section: [.]dtors[.] [(]inverted[)]  abi: ms_abi  source: shipped:x86_64[.]xm:[0-9]+\n"
    "\nx86_64-darwin-init  section: __DATA,__mod_init_func,mod_init_funcs  abi: sysv_abi  source: shipped:x86_64[.]xm:[0-9]+\n"
    "\nmips-elf-fini  section: [.]fini_array  priority_section: [.]fini_array[.]  source: shipped:mips[.]xm:[0-9]+\n")
run(print-profiles --print-profiles)
expect(print-profiles "${stdout}"
    "(^|\n)x86_64-windows  target: x86_64-w64-windows-gnu  abi: x  mangling: cross  constructors: x86_64-mingw-init  destructors: x86_64-mingw-fini\n"
    "\nn64  target: mips-unknown-elf  abi: o32  mangling: cross  constructors: mips-elf-init  destructors: mips-elf-fini\n")
file(WRITE "${OUTPUT}/query.x" "global u32 attributes = $::has_attribute(\"constructor\") + $::has_attribute(\"destructor\");\n")
execute_process(COMMAND "${CPP}" "${OUTPUT}/query.x" -o "${OUTPUT}/query.i"
    RESULT_VARIABLE status ERROR_VARIABLE stderr)
file(READ "${OUTPUT}/query.i" preprocessed)
if(NOT status EQUAL 0 OR NOT preprocessed MATCHES "attributes = 1 [+] 1;")
    message(FATAL_ERROR "cpp does not know the table attributes\n${stderr}\n${preprocessed}")
endif()

# A user table that MinGW's __main walks backward, with inverted priorities,
# and one that the compilation's ABI calls, so a global function of that ABI
# is its own entry.
set(model "${OUTPUT}/tables.xm")
file(WRITE "${model}" [=[
init_table "mingw-ctors" {
    section = ".ctors";
    priority_section = ".ctors.";
    invert_priority = true;
    abi = "ms_abi";
}

init_table "own-runtime" {
    section = ".runtime_init";
}

profile "mingw-ctors" {
    target = "x86_64-w64-windows-gnu";
    abi = "x";
    constructors = "mingw-ctors";
}

profile "own-runtime" {
    target = "x86_64-unknown-linux-gnu";
    abi = "x";
    constructors = "own-runtime";
}
]=])
run(user-ctors "--model=${model}" -mprofile=mingw-ctors -S "${source}" -o "${OUTPUT}/ctors.s")
file(READ "${OUTPUT}/ctors.s" assembly)
expect(user-ctors "${assembly}"
    "\n[.]section \"[.]ctors\",\"dw\"\n[^\n]*\n[^\n]*\n[^\n]*\n\t[.]quad [^\n]*_adapter_"
    "\n[.]section \"[.]ctors[.]65434\",\"dw\"\n")
file(WRITE "${OUTPUT}/own.x" "global u32 state;\n[[constructor]] global void set_up() { state = 1u32; }\n")
run(user-own "--model=${model}" -mprofile=own-runtime -S "${OUTPUT}/own.x" -o "${OUTPUT}/own.s")
file(READ "${OUTPUT}/own.s" assembly)
expect(user-own "${assembly}"
    "\n[.]section \"[.]runtime_init\",\"aw\",@progbits\n[^\n]*\n[^\n]*\n[^\n]*\n[^\n]*\n\t[.]quad set_up\n")
execute_process(COMMAND "${CC}" "--model=${model}" -mprofile=own-runtime -S "${source}"
                -o "${OUTPUT}/own-priority.s"
    RESULT_VARIABLE status ERROR_VARIABLE stderr)
if(status EQUAL 0 OR NOT stderr MATCHES
   "source[.]x:4:3: error: the constructor table 'own-runtime' has no priorities")
    message(FATAL_ERROR "a priority of a table without priorities was accepted\n${stderr}")
endif()

# LABEL, the line, the expected message, and the model text.
function(expect_rejected label line expected text)
    set(path "${OUTPUT}/${label}.xm")
    file(WRITE "${path}" "${text}")
    execute_process(COMMAND "${CC}" "--model=${path}" -S "${source}" -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "${label}[.]xm:${line}: error: ${expected}")
        message(FATAL_ERROR "${label} did not report '${expected}' at line ${line}\n${stderr}")
    endif()
endfunction()
expect_rejected(missing-section 1 "missing required model property 'section'" [=[
init_table "t" {
    priority_section = ".x.";
}
]=])
expect_rejected(empty-section 1 "init_table section, priority_section, and abi cannot be empty" [=[
init_table "t" {
    section = "";
}
]=])
expect_rejected(invert-alone 3 "init_table invert_priority requires priority_section" [=[
init_table "t" {
    section = ".x";
    invert_priority = true;
}
]=])
expect_rejected(unknown-property 3 "unknown model property 'order'" [=[
init_table "t" {
    section = ".x";
    order = "forward";
}
]=])
expect_rejected(unknown-abi 1 "init_table 't' names unknown or not function-selectable ABI 'no_such_abi'" [=[
init_table "t" {
    section = ".x";
    abi = "no_such_abi";
}
]=])
expect_rejected(duplicate 1 "duplicate init_table model 'x86_64-elf-init'" [=[
init_table "x86_64-elf-init" {
    section = ".x";
}
]=])
expect_rejected(unknown-table 1 "profile 'p' names unknown init_table 'missing'" [=[
profile "p" {
    destructors = "missing";
}
]=])
expect_rejected(foreign-abi 6 "profile 'p' names init_table 't', whose ABI 'o32' is not an ABI of x86-64" [=[
init_table "t" {
    section = ".x";
    abi = "o32";
}

profile "p" {
    target = "x86_64-unknown-linux-gnu";
    constructors = "t";
}
]=])
