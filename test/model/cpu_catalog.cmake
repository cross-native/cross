# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The shipped CPU catalog: every -march name that GCC, LLVM, or GNU as
# accepts for MIPS and x86-64, except 32-bit x86 CPUs;
# other spellings that resolve to their CPU; the features of the CPUs that
# the catalog composes; the costs that composition keeps; and `.set arch=`
# with MIPS64 Release 2 CPUs.
foreach(required CC AS OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

set(mips_present
    r4000 mips1 mips2 mips3 mips4 mips32 mips32r2 mips32r3 mips32r5 mips64
    mips64r2 mips64r3 mips64r5 r3000 r3k 3000 3k r2000 r2k 2000 2k r3900 3900
    r6000 r6k 6000 6k allegrex vr4100 4100 r4100 vr4111 4111 r4111 vr4120 4120
    r4120 vr4130 4130 r4130 vr4300 4300 r4300 r4400 4400 r4600 4600 orion r4650
    4650 r4700 4700 loongson2e loongson2f r8000 r8k 8000 8k r10000 r10k 10000
    10k r12000 r12k 12000 12k r14000 r14k 14000 14k r16000 r16k 16000 16k
    vr5000 vr5k 5000 5k r5000 r5k vr5400 5400 r5400 vr5500 5500 r5500 rm7000
    rm7k 7000 7k r7000 r7k rm9000 rm9k 9000 9k r9000 r9k 4kc r4kc 4km r4km 4kp
    r4kp 4ksc r4ksc m4k m14kc m14k m14ke m14kec 4kec r4kec 4kem r4kem 4kep
    r4kep 4ksd r4ksd 24kc r24kc 24kf2_1 r24kf2_1 24kf r24kf 24kf1_1 r24kf1_1
    24kfx r24kfx 24kx r24kx 24kec r24kec 24kef2_1 r24kef2_1 24kef r24kef
    24kef1_1 r24kef1_1 24kefx r24kefx 24kex r24kex 34kc r34kc 34kf2_1 r34kf2_1
    34kf r34kf 34kf1_1 r34kf1_1 34kfx r34kfx 34kx r34kx 34kn r34kn 74kc r74kc
    74kf2_1 r74kf2_1 74kf r74kf 74kf1_1 r74kf1_1 74kfx r74kfx 74kx r74kx
    74kf3_2 r74kf3_2 1004kc r1004kc 1004kf2_1 r1004kf2_1 1004kf r1004kf
    1004kf1_1 r1004kf1_1 interaptiv p5600 m5100 m5101 5kc r5kc 5kf r5kf 20kc
    r20kc sb1 sb1a sr71000 sr71k xlr loongson3a gs464 gs464e gs264e octeon
    octeon+ octeon2 octeon3 xlp generic mips5 r4010 4010 vr4181 4181 r4181
    rm5200 5200 r5200 rm5230 5230 r5230 rm5231 5231 r5231 rm5261 5261 r5261
    rm5721 5721 r5721 m4kp interaptiv-mr2 25kf r25kf r4k 4000 4k r5900 5900
    mips32r6 mips64r6 i6400 i6500 p6600)
set(mips_absent)
set(x86_present
    nocona core2 nehalem corei7 westmere sandybridge corei7-avx ivybridge
    core-avx-i haswell core-avx2 broadwell skylake skylake-avx512 cannonlake
    icelake-client rocketlake icelake-server cascadelake tigerlake cooperlake
    sapphirerapids emeraldrapids alderlake raptorlake meteorlake graniterapids
    graniterapids-d arrowlake arrowlake-s lunarlake pantherlake diamondrapids
    wildcatlake novalake bonnell atom silvermont slm goldmont goldmont-plus
    tremont gracemont sierraforest grandridge clearwaterforest intel x86-64
    x86-64-v2 x86-64-v3 x86-64-v4 eden-x2 nano nano-1000 nano-2000 nano-3000
    nano-x2 eden-x4 nano-x4 lujiazui yongfeng shijidadao k8 k8-sse3 opteron
    opteron-sse3 athlon64 athlon64-sse3 athlon-fx amdfam10 barcelona bdver1
    bdver2 bdver3 bdver4 znver1 znver2 znver3 znver4 znver5 znver6 btver1
    btver2 c86-4g-m4 c86-4g-m6 c86-4g-m7 generic penryn skx knl knm c86-4g-m8)
set(x86_absent
    i386 i486 i586 pentium lakemont pentium-mmx winchip-c6 winchip2 c3 samuel-2
    c3-2 nehemiah c7 esther i686 pentiumpro pentium2 pentium3 pentium3m
    pentium-m pentium4 pentium4m prescott geode k6 k6-2 k6-3 athlon
    athlon-tbird athlon-4 athlon-xp athlon-mp yonah)

set(mips_spellings
    r3k=r3000 3000=r3000 3k=r3000 r2k=r2000 2000=r2000 2k=r2000 3900=r3900
    r6k=r6000 6000=r6000 6k=r6000 4100=vr4100 r4100=vr4100 4111=vr4111
    r4111=vr4111 4120=vr4120 r4120=vr4120 4130=vr4130 r4130=vr4130 4300=vr4300
    r4300=vr4300 4400=r4400 4600=r4600 4650=r4650 4700=r4700 r8k=r8000
    8000=r8000 8k=r8000 r10k=r10000 10000=r10000 10k=r10000 r12k=r12000
    12000=r12000 12k=r12000 r14k=r14000 14000=r14000 14k=r14000 r16k=r16000
    16000=r16000 16k=r16000 vr5k=vr5000 5000=vr5000 5k=vr5000 r5000=vr5000
    r5k=vr5000 5400=vr5400 r5400=vr5400 5500=vr5500 r5500=vr5500 rm7k=rm7000
    7000=rm7000 7k=rm7000 r7000=rm7000 r7k=rm7000 rm9k=rm9000 9000=rm9000
    9k=rm9000 r9000=rm9000 r9k=rm9000 r4kc=4kc r4km=4km r4kp=4kp r4ksc=4ksc
    r4kec=4kec r4kem=4kem r4kep=4kep r4ksd=4ksd r24kc=24kc r24kf2_1=24kf2_1
    r24kf=24kf r24kf1_1=24kf1_1 r24kfx=24kfx r24kx=24kx r24kec=24kec
    r24kef2_1=24kef2_1 r24kef=24kef r24kef1_1=24kef1_1 r24kefx=24kefx
    r24kex=24kex r34kc=34kc r34kf2_1=34kf2_1 r34kf=34kf r34kf1_1=34kf1_1
    r34kfx=34kfx r34kx=34kx r34kn=34kn r74kc=74kc r74kf2_1=74kf2_1 r74kf=74kf
    r74kf1_1=74kf1_1 r74kfx=74kfx r74kx=74kx r74kf3_2=74kf3_2 r1004kc=1004kc
    r1004kf2_1=1004kf2_1 r1004kf=1004kf r1004kf1_1=1004kf1_1 r5kc=5kc r5kf=5kf
    r20kc=20kc sr71k=sr71000 4010=r4010 4181=vr4181 r4181=vr4181 5200=rm5200
    r5200=rm5200 5230=rm5230 r5230=rm5230 5231=rm5231 r5231=rm5231 5261=rm5261
    r5261=rm5261 5721=rm5721 r5721=rm5721 r25kf=25kf r4k=r4000 4000=r4000
    4k=r4000 5900=r5900)
set(x86_aliases
    intel=generic corei7=nehalem corei7-avx=sandybridge core-avx-i=ivybridge
    core-avx2=haswell skx=skylake-avx512 emeraldrapids=sapphirerapids
    atom=bonnell slm=silvermont raptorlake=alderlake meteorlake=alderlake
    gracemont=alderlake lunarlake=arrowlake-s wildcatlake=pantherlake
    grandridge=sierraforest opteron=k8 athlon64=k8 athlon-fx=k8
    opteron-sse3=k8-sse3 athlon64-sse3=k8-sse3 barcelona=amdfam10
    c86-4g-m6=c86-4g-m4 nano-1000=nano nano-2000=nano nano-x2=nano-3000
    eden-x4=nano-3000 nano-x4=nano-3000 shijidadao=yongfeng)

# Reads the --print-cpus entries of an architecture into <arch>_names and,
# per entry, <arch>_<name>_inherits, _features, and _width.
function(load arch triple)
    execute_process(COMMAND "${CC}" -target ${triple} --print-cpus
        RESULT_VARIABLE status OUTPUT_VARIABLE text ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "--print-cpus failed for ${triple}\n${error}")
    endif()
    string(REGEX REPLACE "\n$" "" text "${text}")
    string(REPLACE "\n" ";" lines "${text}")
    set(names)
    foreach(line IN LISTS lines)
        if(NOT line MATCHES "^([^ ]+)  (inherits: ([^ ]+(, [^ ]+)*)  )?features: ([^ ]+)  issue_width: ([0-9]+)  (function_alignment: [0-9]+  )?source: shipped:")
            message(FATAL_ERROR "malformed --print-cpus line '${line}'")
        endif()
        set(name "${CMAKE_MATCH_1}")
        list(APPEND names "${name}")
        set(${arch}_${name}_inherits "${CMAKE_MATCH_3}" PARENT_SCOPE)
        string(REPLACE "," ";" features "${CMAKE_MATCH_5}")
        set(${arch}_${name}_features "${features}" PARENT_SCOPE)
        set(${arch}_${name}_width "${CMAKE_MATCH_6}" PARENT_SCOPE)
    endforeach()
    set(${arch}_names "${names}" PARENT_SCOPE)
endfunction()
load(mips mips-unknown-elf)
load(x86 x86_64-unknown-linux-gnu)

foreach(arch mips x86)
    foreach(name IN LISTS ${arch}_present)
        if(NOT name IN_LIST ${arch}_names)
            message(FATAL_ERROR "${arch} CPU '${name}' is not shipped")
        endif()
    endforeach()
    foreach(name IN LISTS ${arch}_absent)
        if(name IN_LIST ${arch}_names)
            message(FATAL_ERROR "${arch} CPU '${name}' is shipped")
        endif()
    endforeach()
endforeach()
foreach(case "x86_64-unknown-linux-gnu;i686;unknown x86-64 architecture CPU 'i686'"
             "mips-unknown-elf;i7200;unknown mips architecture CPU 'i7200'")
    list(GET case 0 triple)
    list(GET case 1 cpu)
    list(GET case 2 message)
    execute_process(COMMAND "${CC}" -target ${triple} -march=${cpu}
        --print-options=target RESULT_VARIABLE status ERROR_VARIABLE error
        OUTPUT_QUIET)
    if(status EQUAL 0 OR NOT error MATCHES "${message}")
        message(FATAL_ERROR "-march=${cpu} was not rejected\n${error}")
    endif()
endforeach()

# Another spelling or name of a CPU only inherits it.
foreach(arch mips x86)
    if(arch STREQUAL "mips")
        set(pairs ${mips_spellings})
    else()
        set(pairs ${x86_aliases})
    endif()
    foreach(pair IN LISTS pairs)
        string(REPLACE "=" ";" pair "${pair}")
        list(GET pair 0 name)
        list(GET pair 1 cpu)
        if(NOT "${${arch}_${name}_inherits}" STREQUAL "${cpu}" OR
           NOT "${${arch}_${name}_features}" STREQUAL "${${arch}_${cpu}_features}" OR
           NOT "${${arch}_${name}_width}" STREQUAL "${${arch}_${cpu}_width}")
            message(FATAL_ERROR "${arch} '${name}' is not another name of '${cpu}'")
        endif()
    endforeach()
endforeach()

# The CPUs that predate the composed entries, with the features GCC and
# LLVM list for them.
function(expect_features arch name)
    set(want ${ARGN})
    set(have ${${arch}_${name}_features})
    list(SORT want)
    list(SORT have)
    if(NOT "${have}" STREQUAL "${want}")
        message(FATAL_ERROR "${arch} '${name}' has features '${have}', not '${want}'")
    endif()
endfunction()
expect_features(mips mips1 fp32 hard-float mips1)
expect_features(mips generic fp32 hard-float mips1)
expect_features(mips r3000 fp32 hard-float mips1)
expect_features(mips r2000 fp32 hard-float mips1)
expect_features(mips mips2 branch-likely fp32 hard-float llsc load-interlocks
    mips2)
expect_features(mips r6000 branch-likely fp32 hard-float llsc load-interlocks
    mips2)
expect_features(mips allegrex allegrex branch-likely cond-move fp32
    fpu-transfer-interlocks hard-float hilo-interlocks llsc load-interlocks
    mips2 odd-spreg rotate single-float)
expect_features(mips mips32 branch-likely cond-move fp32
    fpu-compare-interlocks fpu-transfer-interlocks hard-float hilo-interlocks
    llsc load-interlocks mips32 odd-spreg)
expect_features(mips mips32r2 branch-likely cond-move fp32
    fpu-compare-interlocks fpu-transfer-interlocks hard-float hilo-interlocks
    llsc load-interlocks mips32r2 odd-spreg rotate)
expect_features(mips mips3 branch-likely fp32 hard-float llsc load-interlocks
    mips3)
# Release 6 has no branch-likely or conditional moves, misaligned ordinary
# accesses work, and the FPU has 64-bit registers.
foreach(cpu mips32r6 mips64r6 i6400 i6500 p6600)
    if(cpu STREQUAL "mips32r6")
        set(level mips32r6)
    else()
        set(level mips64r6)
    endif()
    expect_features(mips ${cpu} ${level} fp64 fpu-compare-interlocks
        fpu-transfer-interlocks hard-float hilo-interlocks llsc load-interlocks
        odd-spreg rotate unaligned-access)
endforeach()
expect_features(mips r4000 branch-likely fp32 hard-float llsc load-interlocks
    mips3 fix-r4000)
expect_features(mips r4400 branch-likely fp32 hard-float llsc load-interlocks
    mips3 fix-r4400)
expect_features(mips r4600 branch-likely fp32 hard-float llsc load-interlocks
    mips3)
expect_features(mips vr4300 branch-likely fp32 hard-float llsc load-interlocks
    mips3)
expect_features(mips mips4 branch-likely cond-move fp64 fpu-compare-interlocks
    fpu-transfer-interlocks hard-float llsc load-interlocks mips4)
expect_features(mips mips5 branch-likely cond-move fp64 fpu-compare-interlocks
    fpu-transfer-interlocks hard-float llsc load-interlocks mips5)
expect_features(mips mips64 branch-likely cond-move fp64
    fpu-compare-interlocks fpu-transfer-interlocks hard-float hilo-interlocks
    llsc load-interlocks mips64 odd-spreg)
expect_features(x86 generic base)
expect_features(x86 x86-64 base)
expect_features(x86 x86-64-v2 base cx16 popcnt sse3 sse4.1 sse4.2 ssse3)
expect_features(x86 x86-64-v3 avx avx2 base bmi bmi2 cx16 f16c fma lzcnt movbe
    popcnt sse3 sse4.1 sse4.2 ssse3 xsave)
expect_features(x86 x86-64-v4 avx avx2 avx512bw avx512cd avx512dq avx512f
    avx512vl base bmi bmi2 cx16 f16c fma lzcnt movbe popcnt sse3 sse4.1 sse4.2
    ssse3 xsave)
expect_features(x86 haswell avx avx2 base bmi bmi2 cx16 f16c fma fsgsbase
    invpcid lzcnt movbe pclmul popcnt rdrnd rdtscp sse3 sse4.1 sse4.2 ssse3
    xsave xsaveopt)
expect_features(x86 skylake adx aes avx avx2 base bmi bmi2 clflushopt cx16 f16c
    fma fsgsbase invpcid lzcnt movbe pclmul popcnt prefetchw rdrnd rdseed
    rdtscp sse3 sse4.1 sse4.2 ssse3 xsave xsavec xsaveopt xsaves)
expect_features(x86 skylake-avx512 adx aes avx avx2 avx512bw avx512cd avx512dq
    avx512f avx512vl base bmi bmi2 clflushopt clwb cx16 f16c fma fsgsbase
    invpcid lzcnt movbe pclmul pku popcnt prefetchw rdrnd rdseed rdtscp sse3
    sse4.1 sse4.2 ssse3 xsave xsavec xsaveopt xsaves)
expect_features(x86 znver1 adx aes avx avx2 base bmi bmi2 clflushopt cx16 f16c
    fma fsgsbase lzcnt monitor movbe pclmul popcnt prefetchw rdrnd rdseed
    rdtscp sha sse3 sse4.1 sse4.2 ssse3 xsave xsavec xsaveopt xsaves)
expect_features(x86 znver2 adx aes avx avx2 base bmi bmi2 clflushopt clwb cx16
    f16c fma fsgsbase lzcnt monitor movbe pclmul popcnt prefetchw rdpid rdrnd
    rdseed rdtscp sha sse3 sse4.1 sse4.2 ssse3 wbnoinvd xsave xsavec xsaveopt
    xsaves)
expect_features(x86 znver3 adx aes avx avx2 base bmi bmi2 clflushopt clwb cx16
    f16c fma fsgsbase invpcid lzcnt monitor movbe pclmul pku popcnt prefetchw
    rdpid rdrnd rdseed rdtscp sha sse3 sse4.1 sse4.2 ssse3 vaes vpclmulqdq
    wbnoinvd xsave xsavec xsaveopt xsaves)

# Features of composed CPUs.
function(has arch name)
    foreach(feature IN LISTS ARGN)
        if(NOT feature IN_LIST ${arch}_${name}_features)
            message(FATAL_ERROR "${arch} '${name}' lacks '${feature}'")
        endif()
    endforeach()
endfunction()
function(lacks arch name)
    foreach(feature IN LISTS ARGN)
        if(feature IN_LIST ${arch}_${name}_features)
            message(FATAL_ERROR "${arch} '${name}' has '${feature}'")
        endif()
    endforeach()
endfunction()
has(mips r3900 mips1 soft-float load-interlocks)
lacks(mips r3900 branch-likely hard-float)
has(mips vr4100 mips3 soft-float branch-likely)
lacks(mips vr4100 llsc hard-float)
has(mips vr4181 mips3 soft-float)
has(mips r4650 mips3 single-float llsc)
has(mips loongson2f mips3 cond-move hilo-interlocks fpu-compare-interlocks)
has(mips r10000 mips4 fp64 cond-move fix-r10000)
lacks(mips r12000 fix-r10000)
has(mips r5900 mips3 soft-float branch-likely cond-move hilo-interlocks fix-r5900)
lacks(mips r5900 llsc hard-float)
has(mips vr5400 mips4 cond-move rotate)
has(mips vr5500 mips4 rotate hilo-interlocks)
lacks(mips vr5500 cond-move)
has(mips 4kp mips32 soft-float hilo-interlocks)
has(mips 24kc mips32r2 soft-float rotate)
lacks(mips 24kc odd-spreg hard-float)
has(mips 74kf3_2 mips32r2 hard-float odd-spreg)
has(mips mips32r5 mips32r2 hard-float)
has(mips m5100 mips32r2 nan2008)
has(mips m5101 mips32r2 soft-float)
has(mips 5kc mips64 soft-float)
has(mips sr71000 mips64 fp64 rotate)
has(mips mips64r2 mips64r2 fp64 odd-spreg rotate)
has(mips gs464 mips64r2 fp64 fix-loongson3-llsc)
lacks(mips gs464 odd-spreg)
has(mips gs464e mips64r2 fix-loongson3-llsc)
lacks(mips octeon3 fix-loongson3-llsc)
has(mips octeon+ mips64r2 soft-float)
has(mips octeon2 mips64r2 soft-float)
has(mips octeon3 mips64r2 hard-float)
has(mips isa-mips32 mips32 llsc)
lacks(mips isa-mips32 hard-float soft-float)
has(x86 core2 ssse3 cx16)
lacks(x86 core2 sse4.1 popcnt)
has(x86 broadwell adx rdseed prefetchw avx2)
has(x86 cascadelake avx512vnni clwb)
has(x86 cannonlake avx512vbmi sha)
has(x86 knl avx512f avx512cd)
lacks(x86 knl avx512bw avx512dq avx512vl)
has(x86 tremont gfni cldemote movdiri movdir64b waitpkg)
has(x86 alderlake avx2 vaes serialize invpcid)
lacks(x86 alderlake avx512f cldemote enqcmd)
lacks(x86 arrowlake enqcmd)
has(x86 sierraforest cldemote enqcmd)
has(x86 novalake avx512bw avx512vbmi2)
has(x86 btver1 ssse3 popcnt lzcnt)
lacks(x86 btver1 xsave sse4.1)
has(x86 bdver4 avx2 bmi2 fma)
has(x86 znver4 avx512f avx512vbmi2 pku invpcid wbnoinvd)
has(x86 znver5 movdiri movdir64b)
has(x86 lujiazui bmi2 adx)
lacks(x86 lujiazui avx)
lacks(x86 intel sse3)

# Costs: composition keeps the costs of the entries that had them, and a
# CPU without costs of its own has those of the entry it names.
function(cost triple cpu)
    execute_process(COMMAND "${CC}" -target ${triple} -march=${cpu}
        -mtune=${cpu} --print-costs RESULT_VARIABLE status
        OUTPUT_VARIABLE text ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "--print-costs -march=${cpu} failed\n${error}")
    endif()
    foreach(line IN LISTS ARGN)
        if(NOT text MATCHES "\n${line}  available: yes\n")
            string(REGEX MATCH "\n[^ ]+" instruction "\n${line}")
            string(REGEX MATCH "${instruction}  [^\n]*" actual "${text}")
            message(FATAL_ERROR "${cpu} lacks '${line}':${actual}")
        endif()
    endforeach()
endfunction()
foreach(cpu mips1 generic mips2 mips32 mips32r2 allegrex r3900 4kc 24kf)
    cost(mips-unknown-elf ${cpu} "lw  latency: 4  issue: 1")
endforeach()
foreach(cpu mips3 mips4 mips5 mips64 mips64r2 vr4300 r10000 octeon)
    cost(mips-unknown-elf ${cpu} "lw  latency: 3  issue: 1")
endforeach()
cost(mips-unknown-elf r3000 "lw  latency: 2  issue: 1"
    "mult  latency: 12  issue: 12" "div[.]d  latency: 19  issue: 1")
cost(mips-unknown-elf 3k "lw  latency: 2  issue: 1"
    "mult  latency: 12  issue: 12")
cost(mips-unknown-elf r6000 "lw  latency: 4  issue: 1"
    "mult  latency: 17  issue: 17" "div[.]d  latency: 16  issue: 1")
cost(mips-unknown-elf r4400 "mult  latency: 10  issue: 10"
    "div  latency: 69  issue: 69")
cost(mips-unknown-elf orion "lw  latency: 2  issue: 1"
    "dmult  latency: 12  issue: 12" "sqrt[.]d  latency: 60  issue: 1")
cost(mips-unknown-elf r4300 "mult  latency: 5  issue: 5"
    "div[.]d  latency: 58  issue: 58")

# `.set arch=` takes the ISA level of a MIPS64 Release 2 CPU.
set(checked 0)
foreach(case "octeon;rotr $2, $3, 1;yes" "mips64r2;seb $2, $3;yes"
             "gs264e;dclz $2, $3;yes" "r10000;rotr $2, $3, 1;no"
             "5kf;seb $2, $3;no")
    list(GET case 0 cpu)
    list(GET case 1 instruction)
    list(GET case 2 accepted)
    set(input "${OUTPUT}/arch.${checked}.s")
    file(WRITE "${input}" ".set noreorder\n.set arch=${cpu}\n        ${instruction}\n")
    execute_process(COMMAND "${AS}" -target mips-unknown-elf -march=mips1
        "${input}" -o "${OUTPUT}/arch.${checked}.o"
        RESULT_VARIABLE status ERROR_VARIABLE error)
    if((accepted AND NOT status EQUAL 0) OR (NOT accepted AND status EQUAL 0))
        message(FATAL_ERROR "'${instruction}' under .set arch=${cpu}: status ${status}\n${error}")
    endif()
    math(EXPR checked "${checked} + 1")
endforeach()
