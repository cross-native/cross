# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The AArch64 models: the architecture and its registers, the features and
# their implications, -march modifiers, every CPU name of GCC and LLVM with
# its features and issue width, the ABIs, and the profiles.
foreach(required CC CPP CATALOG OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

# Runs cc with ARGN; it must succeed. Leaves its output in `stdout`.
function(run label)
    execute_process(COMMAND "${CC}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${out}\n${err}")
    endif()
    set(stdout "${out}" PARENT_SCOPE)
endfunction()

# Runs cc with ARGN; it must fail with exactly one diagnostic matching
# `expected`.
function(reject label expected)
    execute_process(COMMAND "${CC}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    string(REGEX MATCHALL "error:" errors "${err}")
    list(LENGTH errors count)
    if(status EQUAL 0 OR NOT count EQUAL 1 OR NOT err MATCHES "${expected}")
        message(FATAL_ERROR "${label} did not report '${expected}' once\n${out}\n${err}")
    endif()
endfunction()

function(expect label text pattern)
    if(NOT text MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: no match for '${pattern}' in\n${text}")
    endif()
endfunction()

function(expect_not label text pattern)
    if(text MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: unexpected '${pattern}' in\n${text}")
    endif()
endfunction()

# The enabled features of -march=ARGN, sorted, as a comma-separated list.
function(enabled_features variable)
    run(features -target aarch64-none-elf ${ARGN} --print-options=target)
    string(REPLACE "\n" ";" lines "${stdout}")
    set(names)
    foreach(line IN LISTS lines)
        if(line MATCHES "^m[.]([^ ]+)  type: boolean  value: on  ")
            list(APPEND names "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    list(SORT names)
    string(REPLACE ";" "," names "${names}")
    set(${variable} "${names}" PARENT_SCOPE)
endfunction()

# Whether -march=ARGN leaves `on` and `off` (lists) as they say.
function(features label on off)
    enabled_features(names ${ARGN})
    string(REPLACE "," ";" names "${names}")
    foreach(name IN LISTS on)
        if(NOT name IN_LIST names)
            message(FATAL_ERROR "${label}: '${name}' is not enabled: ${names}")
        endif()
    endforeach()
    foreach(name IN LISTS off)
        if(name IN_LIST names)
            message(FATAL_ERROR "${label}: '${name}' is enabled: ${names}")
        endif()
    endforeach()
endfunction()

# The architecture, its registers, and the target options.
run(targets --print-targets)
expect(targets "${stdout}" "(^|\n)aarch64  triples: aarch64-[*]\n")
run(registers -target aarch64-none-elf --print-registers)
foreach(line
        "  x0  storage: x0  bits: 64  class: integer  feature: aarch64  address"
        "  w30  storage: x30  bits: 32  class: integer  feature: aarch64"
        "  sp  storage: sp  bits: 64  class: integer  feature: aarch64  address"
        "  wsp  storage: sp  bits: 32  class: integer  feature: aarch64"
        "  xzr  storage: xzr  bits: 64  class: integer  feature: aarch64"
        "  wzr  storage: xzr  bits: 32  class: integer  feature: aarch64"
        "  v31  storage: v31  bits: 128  class: simd  feature: fp"
        "  q8  storage: v8  bits: 128  class: simd  feature: fp"
        "  d8  storage: v8  bits: 64  class: simd  feature: fp"
        "  s8  storage: v8  bits: 32  class: simd  feature: fp"
        "  h8  storage: v8  bits: 16  class: simd  feature: fp"
        "  b8  storage: v8  bits: 8  class: simd  feature: fp"
        "  nzcv  storage: nzcv  bits: 64  class: special  feature: aarch64"
        "  fpcr  storage: fpcr  bits: 64  class: special  feature: fp"
        "  fpsr  storage: fpsr  bits: 64  class: special  feature: fp")
    string(FIND "${stdout}" "${line}\n" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "register line '${line}' is missing\n${stdout}")
    endif()
endforeach()
string(REGEX MATCHALL "\n  [^ ]+  storage:" registers "${stdout}")
list(LENGTH registers count)
if(NOT count EQUAL 261)
    message(FATAL_ERROR "expected 261 registers, got ${count}\n${stdout}")
endif()
run(options -target aarch64-none-elf --print-options=target)
expect(options "${stdout}"
    "(^|\n)m[.]arch  type: text  value: generic  origin: default  state: partial")
expect(options "${stdout}" "\nm[.]armv9[.]6-a  type: boolean  value: off")
expect(options "${stdout}" "\nm[.]sve2-aes  type: boolean  value: off")
run(language-features -target aarch64-none-elf --print-features)
expect(language-features "${stdout}"
    "\n[$]::feature::simd target extension [[]enabled[]]\n")

# ABIs and the profiles that the triples select.
run(abis -target aarch64-unknown-linux-gnu --print-abis)
expect(abis "${stdout}" "\n  x  aliases:  compilation-selectable: yes  function-selectable: yes  banks: 2  rules: 12\n")
expect(abis "${stdout}" "\n  aapcs64  aliases: lp64  compilation-selectable: yes  function-selectable: yes  banks: 3  rules: 12\n")
expect(abis "${stdout}" "\n  default: x\n")
set(source "${OUTPUT}/profile.x")
file(WRITE "${source}" "profile $::target::profile abi $::target::abi\n")
foreach(case
        "aarch64-unknown-linux-gnu;aarch64-linux" "aarch64-linux-gnu;aarch64-linux"
        "aarch64-none-elf;aarch64-elf" "aarch64-unknown-elf;aarch64-elf")
    list(GET case 0 triple)
    list(GET case 1 profile)
    execute_process(COMMAND "${CPP}" -target ${triple} "${source}"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0 OR NOT out MATCHES "profile \"${profile}\" abi \"x\"")
        message(FATAL_ERROR "${triple} does not select ${profile}\n${out}\n${err}")
    endif()
endforeach()
run(profiles --print-profiles)
expect(profiles "${stdout}" "(^|\n)aarch64-linux  target: aarch64-unknown-linux-gnu  abi: x  mangling: cross  constructors: aarch64-elf-init  destructors: aarch64-elf-fini\n    m[.]risc-cisc-balance = 0\n")
expect(profiles "${stdout}" "\naarch64-elf  target: aarch64-none-elf  abi: x  mangling: cross  constructors: aarch64-elf-init  destructors: aarch64-elf-fini\n")
expect(profiles "${stdout}" "\nswitch  target: aarch64-none-elf  abi: x  mangling: cross  constructors: aarch64-elf-init  destructors: aarch64-elf-fini\n    f[.]pie = on\n    m[.]arch = armv8-a[+]crc[+]crypto\n    m[.]risc-cisc-balance = 0\n    m[.]tune = cortex-a57\n")
run(init-tables --print-init-tables)
expect(init-tables "${stdout}" "(^|\n)aarch64-elf-init  section: [.]init_array  priority_section: [.]init_array[.]  abi: aapcs64  source: shipped:aarch64[.]xm:[0-9]+\n")
features(switch "crc;crypto;aes;sha2;fp;simd;armv8-a" "lse;rdma;sm4;sha3;armv8.1-a" -mprofile=switch)
run(switch-tune -mprofile=switch --print-costs)
expect(switch-tune "${stdout}" "^cpu: armv8-a  tune: cortex-a57  issue_width: 3\n")
run(switch-pie -mprofile=switch --print-options=common)
expect(switch-pie "${stdout}" "(^|\n)f[.]pie  type: boolean  value: on  origin: profile")

# Levels imply their extensions; disabling one keeps the level, as in GCC.
features(level "armv8.2-a;armv8.1-a;armv8-a;lse;crc;rdma;fp;simd" "dotprod;fp16;rcpc" -march=armv8.2-a)
features(nolse "armv8.1-a;crc;rdma" "lse" -march=armv8.2-a+nolse)
features(nofp "armv8-a" "fp;simd" -march=armv8-a+nofp)
features(nosimd "armv8.3-a;fp;jscvt" "simd;fcma" -march=armv8.3-a+nosimd)
features(nosve "armv9-a;fp16;fcma;simd" "sve;sve2" -march=armv9-a+nosve)
features(level84 "fp16fml;dotprod;flagm;rcpc2" "fp16" -march=armv8.4-a)
features(fp16fml "fp16fml;fp16" "" -march=armv8.2-a+fp16fml)
features(nofp16 "dotprod" "fp16;fp16fml" -march=armv8.4-a+nofp16)
features(sve "sve;simd;fp;fp16;fcma" "" -march=armv8-a+nofp+sve)
features(sve-nofp "" "sve;simd;fp" -march=armv8-a+sve+nofp)
features(crypto "crypto;aes;sha2" "sm4;sha3" -march=cortex-a57+crypto)
features(nocrypto "fp;simd;crc" "crypto;aes;sha2;sm4" -march=thunderx -mno-crypto)
features(nocrypto-modifier "sha2" "crypto;aes" -march=thunderx+noaes+sha2)
features(alias "sve2-aes;sve2;sve-aes;aes" "" -march=armv8-a+sve2-aes)
features(noalias "sve2" "sve2-aes;sve-aes" -march=armv9-a+sve2-aes+nosve2-aes)
features(explicit-off "rdma;fp" "simd" -march=armv8-a -mrdma -mno-simd)
features(option "sha3;sha2;simd" "" -march=armv8-a -msha3)
features(fact-free "armv8.4-a;armv8.3-a" "" -march=armv8-a -marmv8.4-a)
reject(modifier "error: unknown aarch64 feature 'bogus' in 'armv8-a[+]bogus'"
       -target aarch64-none-elf -march=armv8-a+bogus --print-options=target)
reject(empty-modifier "error: missing feature after '[+]' in 'cortex-a57[+]'"
       -target aarch64-none-elf -march=cortex-a57+ --print-options=target)
reject(cpu "error: unknown aarch64 architecture CPU 'cortex-a99'"
       -target aarch64-none-elf -march=cortex-a99+crc --print-options=target)
reject(tune "error: unknown aarch64 tuning CPU 'cortex-a57[+]crc'"
       -target aarch64-none-elf -mtune=cortex-a57+crc --print-options=target)
reject(explicit "error: target option '-msha3' requires '-msha2', which explicit '-mno-crypto' disables"
       -target aarch64-none-elf -march=thunderx -mno-crypto -msha3 --print-options=target)

# -march and -mtune select CPU entries independently.
run(tune -target aarch64-none-elf -march=armv8-a -mtune=neoverse-v2 --print-costs)
expect(tune "${stdout}" "^cpu: armv8-a  tune: neoverse-v2  issue_width: 6\n")
run(march -target aarch64-none-elf -march=cortex-a57 --print-costs)
expect(march "${stdout}" "^cpu: cortex-a57  tune: cortex-a57  issue_width: 3\n")

# Every architecture level and CPU name: its features, issue width, and,
# for the Cortex-A57 line, the function alignment of its tuning.
run(cpus -target aarch64-none-elf --print-cpus)
set(cpus "${stdout}")
expect(cpus "${cpus}" "(^|\n)cortex-a57  inherits: armv8-a, ext-cortex-a57, pipeline-cortex-a57  features: armv8-a,crc  issue_width: 3  function_alignment: 16  source: shipped:aarch64[.]xm:[0-9]+\n")
expect(cpus "${cpus}" "\ncortex-a76  inherits: armv8[.]2-a, ext-cortex-a76, pipeline-neoverse-n1  features: armv8[.]2-a,dotprod,fp16,rcpc  issue_width: 3  source:")
file(STRINGS "${CATALOG}" entries REGEX "^[^#]")
set(listed)
foreach(entry IN LISTS entries)
    string(REPLACE " " ";" fields "${entry}")
    list(GET fields 0 name)
    list(GET fields 1 width)
    list(GET fields 2 expected)
    list(APPEND listed "${name}")
    enabled_features(names -march=${name})
    if(NOT names STREQUAL expected)
        message(FATAL_ERROR "-march=${name} enables\n  ${names}\nnot\n  ${expected}")
    endif()
    string(REGEX REPLACE "([.+])" "[\\1]" pattern "${name}")
    if(NOT cpus MATCHES "(^|\n)${pattern}  [^\n]*issue_width: ${width}  ")
        message(FATAL_ERROR "cpu '${name}' lacks issue width ${width}\n${cpus}")
    endif()
endforeach()
# The other entries are the composition bases.
string(REPLACE "\n" ";" lines "${cpus}")
foreach(line IN LISTS lines)
    string(REGEX MATCH "^[^ ]+" name "${line}")
    if(name AND NOT name IN_LIST listed AND NOT name MATCHES "^(ext|pipeline)-")
        message(FATAL_ERROR "cpu entry '${name}' is neither listed nor a base")
    endif()
endforeach()
