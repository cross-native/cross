# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Inheritance of cpu and optimization entries from several bases: the union
# of features, the nearest bases' values and blocks, diamonds, conflicts that
# the entry resolves, ambiguities, cycles, and bases used only for
# composition.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

file(MAKE_DIRECTORY "${OUTPUT}")

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

set(model "${OUTPUT}/composed.xm")
file(WRITE "${model}" [=[
cpu "isa" {
    architecture = "mips";
    features = ["mips3", "llsc", "branch-likely", "load-interlocks"];
}

cpu "fpu" {
    architecture = "mips";
    features = ["hard-float", "fp32"];
    instruction "div.d" { latency = 58; issue = 58; }
}

cpu "board" {
    architecture = "mips";
    inherits = ["isa", "fpu"];
    instruction "mult" { latency = 5; issue = 5; }
}

cpu "top" {
    architecture = "mips";
    inherits = "isa";
    issue_width = 2;
    instruction "mult" { latency = 9; }
}

cpu "left" {
    architecture = "mips";
    inherits = "top";
}

cpu "right" {
    architecture = "mips";
    inherits = "top";
    issue_width = 3;
    instruction "mult" { latency = 7; }
}

cpu "diamond" {
    architecture = "mips";
    inherits = ["left", "top"];
}

cpu "nearer" {
    architecture = "mips";
    inherits = ["left", "right"];
}

cpu "settled" {
    architecture = "mips";
    inherits = ["board", "right"];
    issue_width = 4;
    instruction "mult" { latency = 6; }
}

cpu "own-features" {
    architecture = "mips";
    inherits = ["board", "right"];
    features = ["mips3", "hard-float", "fp64"];
    instruction "mult" { latency = 6; }
}

optimization "unroll" {
    f.unroll-loops = true;
    f.unroll-factor = 8;
}

optimization "tuning" {
    m.tune = "znver3";
}

optimization "speedy" {
    inherits = ["O1", "unroll"];
}

optimization "tuned" {
    inherits = ["O2", "tuning"];
    targets = ["x86-64"];
}

optimization "agreed-a" {
    f.peel-limit = 40;
}

optimization "agreed-b" {
    f.peel-limit = 40;
}

optimization "agreed" {
    inherits = ["O1", "agreed-a", "agreed-b"];
}
]=])

# A union of features in base order, and the blocks of both bases.
run(print-cpus "--model=${model}" -target mips-unknown-elf --print-cpus)
expect(print-cpus "${stdout}"
    "\nboard  inherits: isa, fpu  features: mips3,llsc,branch-likely,load-interlocks,hard-float,fp32  issue_width: 1  source: [^\n]*composed[.]xm:12\n"
    "\ndiamond  inherits: left, top  features: mips3,llsc,branch-likely,load-interlocks  issue_width: 2  "
    "\nown-features  inherits: board, right  features: mips3,hard-float,fp64  issue_width: 3  ")
run(board-costs "--model=${model}" -target mips-unknown-elf -march=board
    --print-costs)
expect(board-costs "${stdout}"
    "^cpu: board  tune: board  issue_width: 1\n"
    "\ndiv[.]d  latency: 58  issue: 58  available: yes\n"
    "\nmult  latency: 5  issue: 5  available: yes\n")
run(board-options "--model=${model}" -target mips-unknown-elf -march=board
    --print-options=target)
expect(board-options "${stdout}"
    "\nm[.]mips3  type: boolean  value: on  "
    "\nm[.]fp32  type: boolean  value: on  ")

# A base reached twice counts once, at its nearest depth: diamond takes top's
# values through left and directly. In nearer, right sets issue_width and
# mult at depth 1, which wins over top's at depth 2.
run(diamond-costs "--model=${model}" -target mips-unknown-elf -march=diamond
    --print-costs)
expect(diamond-costs "${stdout}" "issue_width: 2\n"
    "\nmult  latency: 9  issue: 1  available: yes\n")
run(nearer-costs "--model=${model}" -target mips-unknown-elf -march=nearer
    --print-costs)
expect(nearer-costs "${stdout}" "issue_width: 3\n"
    "\nmult  latency: 7  issue: 1  available: yes\n")
# The entry settles what its nearest bases disagree on.
run(settled-costs "--model=${model}" -target mips-unknown-elf -march=settled
    --print-costs)
expect(settled-costs "${stdout}" "issue_width: 4\n"
    "\ndiv[.]d  latency: 58  issue: 58  available: yes\n"
    "\nmult  latency: 6  issue: 1  available: yes\n")

# Presets: the nearest base that sets an option wins, bases that agree are
# no conflict, and a base used only for composition may omit targets.
run(speedy "--model=${model}" -target mips-unknown-elf -O=speedy
    --print-options=optimization)
expect(speedy "${stdout}"
    "\nf[.]unroll-factor  type: [^\n]*  value: 8  origin: preset  "
    "\nf[.]unroll-loops  type: boolean  value: on  origin: preset  "
    "\nf[.]inline-limit  type: [^\n]*  value: 32  origin: preset  ")
run(agreed "--model=${model}" -target mips-unknown-elf -O=agreed
    --print-options=optimization)
expect(agreed "${stdout}" "\nf[.]peel-limit  type: [^\n]*  value: 40  origin: preset  ")
run(tuned "--model=${model}" -target x86_64-unknown-linux-gnu -O=tuned
    --print-options=all)
expect(tuned "${stdout}"
    "\nm[.]tune  type: text  value: znver3  origin: preset  "
    "\nf[.]inline-limit  type: [^\n]*  value: 96  origin: preset  ")
run(print-optimizations "--model=${model}" --print-optimizations)
expect(print-optimizations "${stdout}" "\ntuned  inherits: O2, tuning  targets: x86-64\n")
execute_process(COMMAND "${CC}" "--model=${model}"
        -target x86_64-unknown-linux-gnu -O=tuning --print-options
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(status EQUAL 0 OR NOT stderr MATCHES
   "composed[.]xm:66: error: optimization preset 'tuning' sets 'm[.]tune', so it or a base needs a targets restriction")
    message(FATAL_ERROR "a selected preset without targets was accepted\n${stderr}")
endif()
execute_process(COMMAND "${CC}" "--model=${model}" -target mips-unknown-elf
        -O=tuned --print-options
    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
if(status EQUAL 0 OR NOT stderr MATCHES
   "optimization preset 'tuned' is unavailable for target 'mips-unknown-elf'")
    message(FATAL_ERROR "a preset was selected for an unlisted target\n${stderr}")
endif()

# Load-time errors report the file and line of the entry or property.
set(source "${OUTPUT}/source.x")
file(WRITE "${source}" "global u32 identity(u32 value) {
    return value;
}
")
function(expect_rejected label line expected text)
    set(rejected "${OUTPUT}/${label}.xm")
    file(WRITE "${rejected}" "${text}")
    execute_process(COMMAND "${CC}" "--model=${rejected}"
            -target x86_64-unknown-linux-gnu -S "${source}"
            -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR
       NOT stderr MATCHES "${label}[.]xm:${line}: error: ${expected}")
        message(FATAL_ERROR
            "${label} did not report '${expected}' at line ${line}\n${stderr}")
    endif()
endfunction()
expect_rejected(ambiguous-width 9
    "cpu 'c' inherits different values of 'issue_width' from 'a' and 'b'" [=[
cpu "a" {
    architecture = "mips";
    issue_width = 2;
}
cpu "b" {
    architecture = "mips";
    issue_width = 3;
}
cpu "c" {
    architecture = "mips";
    inherits = ["a", "b"];
}
]=])
expect_rejected(ambiguous-block 9
    "cpu 'c' inherits different values of instruction 'mult' from 'a' and 'b'" [=[
cpu "a" {
    architecture = "mips";
    instruction "mult" { latency = 5; }
}
cpu "b" {
    architecture = "mips";
    instruction "mult" { latency = 6; }
}
cpu "c" {
    architecture = "mips";
    inherits = ["a", "b"];
}
]=])
expect_rejected(ambiguous-depth 11
    "cpu 'c' inherits different values of 'issue_width' from 'p' and 'q'" [=[
cpu "p" {
    architecture = "mips";
    issue_width = 2;
}
cpu "q" {
    architecture = "mips";
    issue_width = 3;
}
cpu "a" { architecture = "mips"; inherits = "p"; }
cpu "b" { architecture = "mips"; inherits = "q"; }
cpu "c" {
    architecture = "mips";
    inherits = ["a", "b"];
}
]=])
expect_rejected(ambiguous-option 7
    "optimization preset 'r' inherits different values of 'f[.]inline-limit' from 'p' and 'q'" [=[
optimization "p" {
    f.inline-limit = 10;
}
optimization "q" {
    f.inline-limit = 20;
}
optimization "r" {
    inherits = ["p", "q"];
}
]=])
expect_rejected(conflicting-features 9
    "cpu 'c' inherits conflicting features: cpu features '(fp32|fp64)' and '(fp32|fp64)' conflict" [=[
cpu "a" {
    architecture = "mips";
    features = ["mips3", "hard-float", "fp32"];
}
cpu "b" {
    architecture = "mips";
    features = ["fp64"];
}
cpu "c" {
    architecture = "mips";
    inherits = ["a", "b"];
}
]=])
expect_rejected(base-twice 3 "model property 'inherits' lists 'a' twice" [=[
cpu "c" {
    architecture = "mips";
    inherits = ["a", "a"];
}
cpu "a" {
    architecture = "mips";
}
]=])
expect_rejected(unknown-base 1 "cpu 'c' inherits unknown cpu 'r9000'" [=[
cpu "c" {
    architecture = "mips";
    inherits = ["vr4300", "r9000"];
}
]=])
expect_rejected(unknown-preset 1 "optimization preset 'o' inherits unknown preset 'O9'" [=[
optimization "o" {
    inherits = ["O2", "O9"];
}
]=])
expect_rejected(cycle 1 "cpu inheritance cycle: x -> z -> x" [=[
cpu "x" {
    architecture = "mips";
    inherits = ["y", "z"];
}
cpu "y" {
    architecture = "mips";
}
cpu "z" {
    architecture = "mips";
    inherits = "x";
}
]=])
expect_rejected(preset-cycle 1 "optimization inheritance cycle: a -> c -> b -> a" [=[
optimization "a" {
    inherits = ["O1", "c"];
}
optimization "b" {
    inherits = "a";
}
optimization "c" {
    inherits = ["O2", "b"];
}
]=])
expect_rejected(inherits-value 3 "model property 'inherits' must be text or a list" [=[
cpu "c" {
    architecture = "mips";
    inherits = 3;
}
]=])
