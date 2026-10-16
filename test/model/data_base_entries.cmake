# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Data-base entries: the shipped mips-gp entry and its inspection, selection
# by -mdata-bases and profiles, and the checks of entries and selections.
foreach(required CC CPP OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT}")
set(source "${OUTPUT}/source.x")
file(WRITE "${source}" "global u32 counter;

global u32 bump() {
    counter = counter + 1u32;
    return counter;
}
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

run(print-data-bases --print-data-bases)
expect(print-data-bases "${stdout}"
    "(^|\n)mips-gp  base: gp  sections: [.]sdata,[.]sbss  max_size: 8  kinds: writable  source: shipped:mips[.]xm:[0-9]+\n")

set(model "${OUTPUT}/data-bases.xm")
file(WRITE "${model}" [=[
data_base "gp-all" {
    base = "gp";
    sections = [".sdata"];
    max_size = 16;
    kinds = ["writable", "readonly", "literal"];
}

data_base "gp-ro" {
    base = "gp";
    sections = [".srodata"];
    max_size = 4;
    kinds = ["readonly"];
}

data_base "gp-none" {
    base = "gp";
    sections = [".gpdata", ".gpbss"];
    kinds = [];
}

profile "small-data" {
    target = "mips-unknown-elf";
    abi = "o32";
    data_bases = ["mips-gp"];
}

profile "no-data-bases" {
    target = "mips-unknown-elf";
    data_bases = [];
}

profile "any-small-data" {
    data_bases = ["mips-gp"];
}
]=])
run(user-print "--model=${model}" --print-data-bases)
expect(user-print "${stdout}"
    "\ngp-all  base: gp  sections: [.]sdata  max_size: 16  kinds: writable,readonly,literal  source: [^\n]*data-bases[.]xm:1\n"
    "\ngp-ro  base: gp  sections: [.]srodata  max_size: 4  kinds: readonly  source: [^\n]*data-bases[.]xm:8\n"
    "\ngp-none  base: gp  sections: [.]gpdata,[.]gpbss  max_size: 0  kinds: none  source: [^\n]*data-bases[.]xm:15\n")
run(user-profiles "--model=${model}" --print-profiles)
expect(user-profiles "${stdout}" "\nsmall-data  target: mips-unknown-elf  abi: o32\n    m[.]data-bases = mips-gp\n")

# --print-options reports the selection and where it comes from.
function(expect_selection label value origin)
    run(${label} "--model=${model}" -target mips-unknown-elf ${ARGN}
        --print-options=target)
    if(NOT stdout MATCHES "(^|\n)m[.]data-bases  type: text  value: ${value}  origin: ${origin}  ")
        message(FATAL_ERROR "${label}: expected m.data-bases = '${value}' (${origin})\n${stdout}")
    endif()
    set(stdout "${stdout}" PARENT_SCOPE)
endfunction()
expect_selection(default none default)
expect_selection(command mips-gp command-line -mdata-bases=mips-gp)
expect_selection(last none command-line -mdata-bases=mips-gp -mdata-bases=none)
expect_selection(two "mips-gp,gp-ro" command-line -mdata-bases=mips-gp,gp-ro)
expect_selection(profile mips-gp profile -mprofile=small-data)
expect(profile "${stdout}" "source: [^\n]*data-bases[.]xm:24\n")
expect_selection(profile-override none command-line -mprofile=small-data
                 -mdata-bases=none)
expect_selection(profile-empty none profile -mprofile=no-data-bases)

# cpp resolves the same selection.
execute_process(COMMAND "${CPP}" -target mips-unknown-elf -mdata-bases=mips-gp
        "${source}"
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "cpp rejected -mdata-bases\n${stderr}")
endif()

# A selection error names the option or profile property that made it.
function(expect_error label expected)
    foreach(tool CC CPP)
        set(arguments ${ARGN})
        if(tool STREQUAL "CC")
            list(APPEND arguments -S -o "${OUTPUT}/${label}.s")
        endif()
        execute_process(COMMAND "${${tool}}" "--model=${model}" ${arguments}
                "${source}"
            RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
        if(status EQUAL 0 OR NOT stderr MATCHES "${expected}")
            message(FATAL_ERROR "${label} (${tool}) did not report '${expected}'\n${stderr}")
        endif()
    endforeach()
endfunction()
expect_error(unknown "error: unknown data_base entry 'nope' from -mdata-bases=nope"
    -target mips-unknown-elf -mdata-bases=nope)
expect_error(twice "error: -mdata-bases lists 'mips-gp' twice from -mdata-bases=mips-gp,mips-gp"
    -target mips-unknown-elf -mdata-bases=mips-gp,mips-gp)
expect_error(empty "error: -mdata-bases expects 'none' or data_base entry names separated by commas from -mdata-bases="
    -target mips-unknown-elf -mdata-bases=)
expect_error(none-mixed "error: -mdata-bases expects 'none' or data_base entry names"
    -target mips-unknown-elf -mdata-bases=none,mips-gp)
expect_error(shared-section
    "error: data_base entries 'mips-gp' and 'gp-all' both name section '[.]sdata' from -mdata-bases=mips-gp,gp-all"
    -target mips-unknown-elf -mdata-bases=mips-gp,gp-all)
expect_error(x86 "error: target x86-64 has no base-relative data addressing; data_base 'mips-gp' from -mdata-bases=mips-gp"
    -target x86_64-unknown-linux-gnu -mdata-bases=mips-gp)
expect_error(x86-profile "error: target x86-64 has no base-relative data addressing; data_base 'mips-gp' from [^\n]*data-bases[.]xm:33"
    -target x86_64-unknown-linux-gnu -mprofile=any-small-data)

# Load-time checks report the file and line of the entry or property.
function(expect_rejected label line expected text)
    set(rejected "${OUTPUT}/${label}.xm")
    file(WRITE "${rejected}" "${text}")
    execute_process(COMMAND "${CC}" "--model=${rejected}"
            -target mips-unknown-elf ${ARGN} -S "${source}"
            -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR
       NOT stderr MATCHES "${label}[.]xm:${line}: error: ${expected}")
        message(FATAL_ERROR
            "${label} did not report '${expected}' at line ${line}\n${stderr}")
    endif()
endfunction()
expect_rejected(base 1 "missing required model property 'base'" [=[
data_base "d" {
    sections = [".sdata"];
}
]=])
expect_rejected(register 2 "unknown register 'r99'" [=[
data_base "d" {
    base = "r99";
    sections = [".sdata"];
}
]=])
expect_rejected(sections 1 "missing required model property 'sections'" [=[
data_base "d" {
    base = "gp";
}
]=])
expect_rejected(no-sections 3 "data_base sections must name one or two sections" [=[
data_base "d" {
    base = "gp";
    sections = [];
}
]=])
expect_rejected(three-sections 3 "data_base sections must name one or two sections" [=[
data_base "d" {
    base = "gp";
    sections = [".a", ".b", ".c"];
}
]=])
expect_rejected(section-twice 3 "data_base sections list '[.]a' twice" [=[
data_base "d" {
    base = "gp";
    sections = [".a", ".a"];
}
]=])
expect_rejected(empty-section 3 "data_base section names cannot be empty" [=[
data_base "d" {
    base = "gp";
    sections = [".a", ""];
}
]=])
expect_rejected(kind 5 "unknown data_base kind 'pointer'; the kinds are writable, readonly, and literal" [=[
data_base "d" {
    base = "gp";
    sections = [".a"];
    max_size = 8;
    kinds = ["writable", "pointer"];
}
]=])
expect_rejected(kind-twice 4 "data_base kinds list 'readonly' twice" [=[
data_base "d" {
    base = "gp";
    sections = [".a"];
    kinds = ["readonly", "readonly"];
}
]=])
expect_rejected(max-size 4 "model property 'max_size' must be an unsigned integer" [=[
data_base "d" {
    base = "gp";
    sections = [".a"];
    max_size = "8";
}
]=])
expect_rejected(property 4 "unknown model property 'window'" [=[
data_base "d" {
    base = "gp";
    sections = [".a"];
    window = 65536;
}
]=])
expect_rejected(duplicate 6 "duplicate data_base model 'mips-gp'" [=[
data_base "d" {
    base = "gp";
    sections = [".a"];
}

data_base "mips-gp" {
    base = "gp";
    sections = [".b"];
}
]=])
expect_rejected(profile-twice 3 "profile sets both data_bases and m[.]data-bases" [=[
profile "p" {
    target = "mips-unknown-elf";
    data_bases = ["mips-gp"];
    m.data-bases = "mips-gp";
}
]=])
expect_rejected(profile-name 3 "profile data_bases must list entry names" [=[
profile "p" {
    target = "mips-unknown-elf";
    data_bases = [""];
}
]=])
expect_rejected(preset 4 "optimization presets cannot set option 'm[.]data-bases'" [=[
optimization "o" {
    inherits = "O2";
    targets = ["mips"];
    m.data-bases = "mips-gp";
}
]=])
# The target decides which registers can hold a base; a selected entry
# whose register cannot, or whose register carries ABI arguments, is
# reported at its base property.
expect_rejected(k0 2 "data_base 'd' base register 'k0' cannot hold a data base on mips; it uses 'gp'" [=[
data_base "d" {
    base = "k0";
    sections = [".kdata"];
}
]=] -mdata-bases=d)
expect_rejected(rbx 2 "data_base 'd' base register 'rbx' cannot hold a data base on mips; it uses 'gp'" [=[
data_base "d" {
    base = "rbx";
    sections = [".kdata"];
}
]=] -mdata-bases=d)
set(abi [=[
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

data_base "d" {
    base = "gp";
    sections = [".d"];
}
]=])
expect_rejected(abi 34 "data_base 'd' base register 'gp' carries arguments or results in ABI 'gp_args'"
    "${abi}" -mabi=gp_args -mdata-bases=d)
