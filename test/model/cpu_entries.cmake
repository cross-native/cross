# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# CPU entries: the shipped CPUs and their inspection, user CPUs selected by
# -march, -mtune, and profiles, unavailable instructions, the multiply cost
# that decides a constant multiplication, and the load-time checks.
foreach(required CC CPP OUTPUT ROTATE_SOURCE)
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

function(reject label text)
    foreach(pattern IN LISTS ARGN)
        if(text MATCHES "${pattern}")
            message(FATAL_ERROR "${label} contains '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

# Every shipped CPU is a model entry that -march and -mtune select.
foreach(cpu generic mips1 r2000 r3000 mips2 r6000 allegrex mips3 r4000 r4400
            r4600 vr4300 mips4 mips5 mips32 mips32r2 mips64)
    run(mips-${cpu} -target mips-unknown-elf -march=${cpu} -mtune=${cpu}
        --print-options=target)
endforeach()
foreach(cpu generic x86-64 x86-64-v2 x86-64-v3 x86-64-v4 haswell skylake
            skylake-avx512 znver1 znver2 znver3)
    run(x86-${cpu} -target x86_64-unknown-linux-gnu -march=${cpu}
        -mtune=${cpu} --print-options=target)
endforeach()
run(allegrex-features -target mipsallegrexel-sony-psp-elf -march=allegrex
    --print-options=target)
expect(allegrex-features "${stdout}"
    "\nm[.]cond-move  type: boolean  value: on  "
    "\nm[.]rotate  type: boolean  value: on  "
    "\nm[.]single-float  type: boolean  value: on  "
    "\nm[.]mips3  type: boolean  value: off  ")

run(print-cpus -target mips-unknown-elf --print-cpus)
expect(print-cpus "${stdout}"
    "(^|\n)mips1  features: mips1,hard-float,fp32  issue_width: 1  source: shipped:mips[.]xm:[0-9]+\n"
    "\nvr4300  inherits: mips3  features: mips3,hard-float,fp32,llsc,branch-likely,load-interlocks  issue_width: 1  source: shipped:mips[.]xm:[0-9]+\n")
reject(print-cpus "${stdout}" "haswell")

# Costs: the -mtune entry's latency and issue, the -march entry's
# availability, one parseable line per instruction.
run(print-costs -mprofile=vr4300-o32 --print-costs)
expect(print-costs "${stdout}"
    "^cpu: vr4300  tune: vr4300  issue_width: 1\n"
    "\ndiv[.]s  latency: 29  issue: 29  available: yes\n"
    "\nmul[.]s  latency: 5  issue: 5  available: yes\n"
    "\nmult  latency: 5  issue: 5  available: yes\n"
    "\ndiv  latency: 37  issue: 37  available: yes\n"
    "\ncvt[.]d[.]s  latency: 1  issue: 1  available: yes\n"
    "\naddu  latency: 1  issue: 1  available: yes\n"
    "\ntlbp  latency: 1  issue: 1  available: yes\n")
string(REGEX REPLACE "\n$" "" cost_lines "${stdout}")
string(REPLACE "\n" ";" cost_lines "${cost_lines}")
list(POP_FRONT cost_lines)
foreach(line IN LISTS cost_lines)
    if(NOT line MATCHES "^[a-z0-9.]+  latency: [0-9]+  issue: [0-9]+  available: (yes|no)$")
        message(FATAL_ERROR "malformed --print-costs line '${line}'")
    endif()
endforeach()
run(print-costs-tune -target mips-unknown-elf -march=mips1 -mtune=r3000
    --print-costs)
expect(print-costs-tune "${stdout}"
    "^cpu: mips1  tune: r3000  issue_width: 1\n"
    "\nmult  latency: 12  issue: 12  available: yes\n"
    "\nlw  latency: 2  issue: 1  available: yes\n")
run(print-costs-x86 -target x86_64-unknown-linux-gnu --print-costs)
expect(print-costs-x86 "${stdout}"
    "^cpu: generic  tune: generic  issue_width: 1\n"
    "\nimulq  latency: 3  issue: 1  available: yes\n"
    "\ndivq  latency: 40  issue: 1  available: yes\n"
    "\nlzcntq  latency: 3  issue: 1  available: yes\n")

# User CPUs: names are per architecture, entries inherit, and -march,
# -mtune, and profiles select them.
set(model "${OUTPUT}/cpus.xm")
file(WRITE "${model}" [=[
cpu "fastmul" {
    architecture = "mips";
    inherits = "vr4300";
    issue_width = 2;
    instruction "mult" { latency = 1; issue = 1; }
    instruction "multu" { latency = 1; }
}

cpu "fastmul" {
    architecture = "x86-64";
    inherits = "x86-64-v3";
    instruction "imull" { latency = 1; }
}

cpu "nomovn" {
    architecture = "mips";
    inherits = "allegrex";
    instruction "movn" { available = false; }
}

cpu "norotate" {
    architecture = "mips";
    inherits = "allegrex";
    instruction "rotrv" { available = false; }
}

cpu "nolikely" {
    architecture = "mips";
    inherits = "vr4300";
    instruction "bnel" { available = false; }
}

cpu "nodoubleword" {
    architecture = "mips";
    inherits = "mips2";
    instruction "ldc1" { available = false; }
    instruction "sdc1" { available = false; }
}

cpu "nomulthigh" {
    architecture = "mips";
    inherits = "vr4300";
    instruction "multu" { available = false; }
}

cpu "nomult" {
    architecture = "mips";
    inherits = "vr4300";
    instruction "mult" { available = false; }
}

cpu "nosqrt" {
    architecture = "mips";
    inherits = "vr4300";
    instruction "sqrt.s" { available = false; }
    instruction "sqrt.d" { available = false; }
}

cpu "noshlx" {
    architecture = "x86-64";
    inherits = "x86-64-v3";
    instruction "shlxl" { available = false; }
}

cpu "nolzcnt" {
    architecture = "x86-64";
    inherits = "x86-64-v3";
    instruction "lzcntq" { available = false; }
}

profile "fastmul-o32" {
    target = "mips-unknown-elf";
    abi = "o32";
    m.arch = "fastmul";
}
]=])
run(user-cpus "--model=${model}" -target mips-unknown-elf --print-cpus)
expect(user-cpus "${stdout}"
    "\nfastmul  inherits: vr4300  features: mips3,[^\n]*  issue_width: 2  source: [^\n]*cpus[.]xm:1\n")
run(user-cpus-x86 "--model=${model}" -target x86_64-unknown-linux-gnu
    --print-cpus)
expect(user-cpus-x86 "${stdout}"
    "\nfastmul  inherits: x86-64-v3  features: base,sse3,[^\n]*  source: [^\n]*cpus[.]xm:9\n")
run(user-march "--model=${model}" -target mips-unknown-elf -march=fastmul
    --print-costs)
expect(user-march "${stdout}"
    "^cpu: fastmul  tune: fastmul  issue_width: 2\n"
    "\nmult  latency: 1  issue: 1  available: yes\n"
    "\nmultu  latency: 1  issue: 1  available: yes\n"
    "\ndmult  latency: 8  issue: 8  available: yes\n")
run(user-mtune "--model=${model}" -mprofile=vr4300-o32 -mtune=fastmul
    --print-costs)
expect(user-mtune "${stdout}" "^cpu: vr4300  tune: fastmul  issue_width: 2\n")
run(user-profile "--model=${model}" -mprofile=fastmul-o32
    --print-options=target)
expect(user-profile "${stdout}"
    "(^|\n)m[.]arch  type: text  value: fastmul  origin: profile  "
    "\nm[.]tune  type: text  value: fastmul  origin: profile  "
    "\nm[.]mips3  type: boolean  value: on  ")

# The multiply's -mtune latency decides x * 7: shifts beat a five-cycle
# MULT, and a one-cycle one stays.
set(multiply "${OUTPUT}/multiply.x")
file(WRITE "${multiply}" "global i32 times7(i32 x) {
    return x * 7;
}

global u32 times7u(u32 x) {
    return x * 7u32;
}
")
run(times7-vr4300 -S -O2 -mprofile=vr4300-o32 "${multiply}"
    -o "${OUTPUT}/times7-vr4300.s")
file(READ "${OUTPUT}/times7-vr4300.s" assembly)
expect(times7-vr4300 "${assembly}" "[\t ]sll[\t ][^\n]*,3\n" "[\t ]subu[\t ]")
reject(times7-vr4300 "${assembly}" "[\t ]mult[\t ]")
run(times7-fastmul -S -O2 "--model=${model}" -mprofile=vr4300-o32
    -mtune=fastmul "${multiply}" -o "${OUTPUT}/times7-fastmul.s")
file(READ "${OUTPUT}/times7-fastmul.s" assembly)
string(REGEX MATCHALL "[\t ]mult[\t ]" multiplies "${assembly}")
list(LENGTH multiplies multiply_count)
if(NOT multiply_count EQUAL 2)
    message(FATAL_ERROR "a one-cycle multiply did not keep both MULTs\n${assembly}")
endif()
expect(times7-fastmul "${assembly}" "[\t ]mflo[\t ]")
run(times7-x86 -S -O2 -mrisc-cisc-balance=0 "--model=${model}"
    -target x86_64-unknown-linux-gnu -mtune=fastmul "${multiply}"
    -o "${OUTPUT}/times7-x86.s")
file(READ "${OUTPUT}/times7-x86.s" assembly)
expect(times7-x86 "${assembly}" "[\t ]imull[\t ]")

# An unavailable instruction leaves selection; its alternative replaces it.
function(compile_assembly label)
    run(${label} -S -O2 "--model=${model}" ${ARGN}
        -o "${OUTPUT}/${label}.s")
    file(READ "${OUTPUT}/${label}.s" assembly)
    set(assembly "${assembly}" PARENT_SCOPE)
endfunction()
compile_assembly(allegrex -mprofile=psp-allegrex "${ROTATE_SOURCE}")
expect(allegrex "${assembly}" "[.]word[^\n]*# movn" "[.]word[^\n]*# rotrv")
compile_assembly(nomovn -mprofile=psp-allegrex -march=nomovn "${ROTATE_SOURCE}")
expect(nomovn "${assembly}" "[.]word[^\n]*# rotrv" "[\t ](beq|bne)l?[\t ]")
reject(nomovn "${assembly}" "# movn")
compile_assembly(norotate -mprofile=psp-allegrex -march=norotate "${ROTATE_SOURCE}")
expect(norotate "${assembly}" "[.]word[^\n]*# movn" "[\t ]srlv[\t ]")
reject(norotate "${assembly}" "# rotrv")

set(branches "${OUTPUT}/branches.x")
file(WRITE "${branches}" "global u32 pick(u32 c, u32 a, u32 b) {
    return c != 0u32 ? a : b;
}

global f64 product(f64* p) {
    return p[0] * p[1];
}

global u32 tenth(u32 x) {
    return x / 10u32;
}
")
compile_assembly(likely -mprofile=vr4300-o32 "${branches}")
expect(likely "${assembly}" "[\t ](beq|bne|beqz|bnez|bltz|bgez|blez|bgtz|bc1t|bc1f)l[\t ]")
compile_assembly(nolikely -mprofile=vr4300-o32 -march=nolikely "${branches}")
reject(nolikely "${assembly}" "[\t ](beq|bne|beqz|bnez|bltz|bgez|blez|bgtz|bc1t|bc1f)l[\t ]")
compile_assembly(doubleword -target mips-unknown-elf -march=mips2 "${branches}")
expect(doubleword "${assembly}" "[\t ]ldc1[\t ]")
compile_assembly(nodoubleword -target mips-unknown-elf -march=nodoubleword
                 "${branches}")
expect(nodoubleword "${assembly}" "[\t ]lwc1[\t ]")
reject(nodoubleword "${assembly}" "[\t ](ldc1|sdc1)[\t ]")
compile_assembly(multiply-high -mprofile=vr4300-o32 "${branches}")
expect(multiply-high "${assembly}" "[\t ]multu[\t ]")
reject(multiply-high "${assembly}" "[\t ]divu[\t ]")
compile_assembly(nomulthigh -mprofile=vr4300-o32 -march=nomulthigh "${branches}")
expect(nomulthigh "${assembly}" "[\t ]divu[\t ]")
reject(nomulthigh "${assembly}" "[\t ]multu[\t ]")

set(shift "${OUTPUT}/shift.x")
file(WRITE "${shift}" "global u32 shift(u32 x, u32 n) {
    return x << (n & 31u32);
}
")
compile_assembly(shlx -target x86_64-unknown-linux-gnu -march=x86-64-v3 "${shift}")
expect(shlx "${assembly}" "[\t ]shlxl[\t ]")
compile_assembly(noshlx -target x86_64-unknown-linux-gnu -march=noshlx "${shift}")
expect(noshlx "${assembly}" "[\t ]shll[\t ]+%cl,")
reject(noshlx "${assembly}" "[\t ]shlx")

# A machine-instruction built-in, an intrinsic, or an operation that needs
# an unavailable instruction is an error; the queries report it missing.
function(expect_error label expected source)
    execute_process(COMMAND "${CC}" -S -O2 "--model=${model}" ${ARGN}
            "${source}" -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "${expected}")
        message(FATAL_ERROR "${label} did not report '${expected}'\n${stderr}")
    endif()
endfunction()
set(builtins "${OUTPUT}/builtins.x")
file(WRITE "${builtins}" "global f32 root(f32 x) {
    f32 result;
    $::_sqrt(result, x);
    return result;
}
")
expect_error(builtin-sqrt
    "builtins[.]x:3:[0-9]+: error: target instruction '[$]::_sqrt' uses 'sqrt[.]s', which CPU 'nosqrt' does not have"
    "${builtins}" -mprofile=vr4300-o32 -march=nosqrt)
set(intrinsics "${OUTPUT}/intrinsics.x")
file(WRITE "${intrinsics}" "global f32 root(f32 x) {
    return $::sqrt(x);
}
")
expect_error(intrinsic-sqrt
    "intrinsics[.]x:2:[0-9]+: error: [$]::sqrt uses 'sqrt[.]s', which CPU 'nosqrt' does not have"
    "${intrinsics}" -mprofile=vr4300-o32 -march=nosqrt)
set(lzcnt "${OUTPUT}/lzcnt.x")
file(WRITE "${lzcnt}" "global u64 leading(u64 x) {
    u64 result;
    $::_lzcnt(result, x);
    return result;
}
")
expect_error(builtin-lzcnt
    "error: target instruction '[$]::_lzcnt' uses 'lzcntq', which CPU 'nolzcnt' does not have"
    "${lzcnt}" -target x86_64-unknown-linux-gnu -march=nolzcnt)
set(product "${OUTPUT}/product.x")
file(WRITE "${product}" "global i32 product(i32 x, i32 y) {
    return x * y;
}
")
expect_error(operation-mult
    "product[.]x:1:[0-9]+: error: function needs instruction 'mult', which CPU 'nomult' does not have"
    "${product}" -mprofile=vr4300-o32 -march=nomult)

set(queries "${OUTPUT}/queries.x")
file(WRITE "${queries}" "global u32 has_sqrt = $::has_instruction($::_sqrt);
global u32 has_root = $::has_intrinsic($::sqrt);
")
foreach(cpu vr4300 nosqrt)
    execute_process(COMMAND "${CPP}" "--model=${model}" -mprofile=vr4300-o32
            -march=${cpu} "${queries}"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "queries for ${cpu} failed\n${stderr}")
    endif()
    set(value 1)
    if(cpu STREQUAL "nosqrt")
        set(value 0)
    endif()
    expect(queries-${cpu} "${stdout}" "has_sqrt = ${value};" "has_root = ${value};")
endforeach()

# Load-time checks report the file and line of the entry or property.
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
expect_rejected(cpu-architecture 1 "missing required model property 'architecture'" [=[
cpu "c" {
    features = ["mips1"];
}
]=])
expect_rejected(cpu-unknown-architecture 2 "unknown architecture 'sparc'" [=[
cpu "c" {
    architecture = "sparc";
}
]=])
expect_rejected(cpu-feature 3 "unknown mips feature 'avx2'" [=[
cpu "c" {
    architecture = "mips";
    features = ["mips3", "avx2"];
}
]=])
expect_rejected(cpu-feature-twice 3 "cpu features list 'mips3' twice" [=[
cpu "c" {
    architecture = "mips";
    features = ["mips3", "mips3"];
}
]=])
expect_rejected(cpu-feature-conflict 3 "cpu features 'mips32' and 'mips3' conflict" [=[
cpu "c" {
    architecture = "mips";
    features = ["mips32", "fp64"];
}
]=])
expect_rejected(cpu-instruction 4 "unknown x86-64 instruction 'mult'" [=[
cpu "c" {
    architecture = "x86-64";
    features = ["base"];
    instruction "mult" { latency = 5; }
}
]=])
expect_rejected(cpu-instruction-twice 4 "duplicate instruction 'div[.]s'" [=[
cpu "c" {
    architecture = "mips";
    instruction "div.s" { latency = 29; }
    instruction "div.s" { issue = 29; }
}
]=])
expect_rejected(cpu-instruction-property 3 "unknown model property 'throughput'" [=[
cpu "c" {
    architecture = "mips";
    instruction "div.s" { throughput = 29; }
}
]=])
expect_rejected(cpu-latency 3 "model property 'latency' must be from 0 to 65535" [=[
cpu "c" {
    architecture = "mips";
    instruction "div.s" { latency = 65536; }
}
]=])
expect_rejected(cpu-issue 3 "model property 'issue' must be from 1 to 65535" [=[
cpu "c" {
    architecture = "mips";
    instruction "div.s" { issue = 0; }
}
]=])
expect_rejected(cpu-available 3 "model property 'available' must be boolean" [=[
cpu "c" {
    architecture = "mips";
    instruction "div.s" { available = "no"; }
}
]=])
expect_rejected(cpu-issue-width 3 "model property 'issue_width' must be from 1 to 65535" [=[
cpu "c" {
    architecture = "mips";
    issue_width = 0;
}
]=])
expect_rejected(cpu-property 3 "unknown model property 'pipeline'" [=[
cpu "c" {
    architecture = "mips";
    pipeline = "five";
}
]=])
expect_rejected(cpu-duplicate 5 "duplicate mips cpu model 'vr4300'" [=[
cpu "c" {
    architecture = "mips";
}

cpu "vr4300" {
    architecture = "mips";
}
]=])
expect_rejected(cpu-parent 1 "cpu 'c' inherits unknown cpu 'r9000'" [=[
cpu "c" {
    architecture = "mips";
    inherits = "r9000";
}
]=])
expect_rejected(cpu-parent-architecture 1
    "cpu 'c' inherits 'haswell' of architecture x86-64, not mips" [=[
cpu "c" {
    architecture = "mips";
    inherits = "haswell";
}
]=])
expect_rejected(cpu-cycle 1 "cpu inheritance cycle: a -> b -> a" [=[
cpu "a" {
    architecture = "mips";
    inherits = "b";
}

cpu "b" {
    architecture = "mips";
    inherits = "a";
}
]=])
expect_rejected(cpu-profile-arch 3 "unknown cpu 'r9000' for option 'm[.]arch'" [=[
profile "p" {
    target = "mips-unknown-elf";
    m.arch = "r9000";
}
]=])
expect_rejected(cpu-preset-tune 4 "unknown cpu 'vr4300' for option 'm[.]tune'" [=[
optimization "o" {
    inherits = "O2";
    targets = ["x86-64"];
    m.tune = "vr4300";
}
]=])
