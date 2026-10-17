# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The features of catalog CPUs reach instruction selection: the x86-64 bit
# instructions, FMA, and AVX-512 forms, and on MIPS LL/SC atomics, 64-bit
# multiplies, conditional moves, rotates, branch-likely forms, and the FPU
# a CPU has or lacks.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

file(WRITE "${OUTPUT}/x86.x" [=[
typedef i64 i64x8 [[ext_vector_type(8)]];
typedef f32 f32x16 [[ext_vector_type(16)]];
global u32 leading(in u32 x) { return $::clz(x); }
global u32 trailing(in u64 x) { return $::ctz(x); }
global u32 ones(in u64 x) { return $::popcount(x); }
global f64 fused(in f64 a, in f64 b, in f64 c) { return a * b + c; }
global i64x8 lanes(in i64x8 a, in i64x8 b) { return a * b + a; }
global f32x16 fused16(in f32x16 a, in f32x16 b, in f32x16 c) {
    return a * b + c;
}
]=])
file(WRITE "${OUTPUT}/mips.x" [=[
global u32 [[atomic]] counter;
global u32 bump(in u32 value) {
    return $::atomic_fetch_add(&counter, value, $::memory::seq_cst);
}
global i64 product(in i64 a, in i64 b) { return a * b; }
global i32 pick(in i32 c, in i32 a, in i32 b) { return c != 0 ? a : b; }
global u32 spin(in u32 x, in u32 n) { return $::rotr(x, n); }
global u32 total(in const u32 *p, in u32 n) {
    u32 sum = 0u32;
    for (u32 i = 0u32; i < n; i = i + 1u32) { sum = sum + p[i] * 3u32; }
    return sum;
}
]=])
file(WRITE "${OUTPUT}/single.x" [=[
global f32 half(in f32 x) { return x * 0.5f32; }
]=])
file(WRITE "${OUTPUT}/double.x" [=[
global f64 halve(in f64 x) { return x * 0.5; }
]=])

# compile(LABEL SOURCE TRIPLE CPU [OPTION...]) sets `assembly`, or `errors`
# when the compilation fails.
function(compile label source triple cpu)
    execute_process(
        COMMAND "${CC}" -S -O2 -target ${triple} -march=${cpu} ${ARGN}
                "${OUTPUT}/${source}" -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    set(errors "" PARENT_SCOPE)
    set(assembly "" PARENT_SCOPE)
    if(status EQUAL 0)
        file(READ "${OUTPUT}/${label}.s" text)
        set(assembly "${text}" PARENT_SCOPE)
    else()
        set(errors "${stderr}" PARENT_SCOPE)
    endif()
endfunction()

macro(x86 cpu)
    compile(x86-${cpu} x86.x x86_64-unknown-linux-gnu ${cpu}
        -ffp-contract=fast)
endmacro()
macro(mips source cpu)
    compile(mips-${cpu}-${source} ${source} mips-unknown-elf ${cpu} -mabi=o32)
endmacro()

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

# x86-64.
foreach(cpu k8 opteron core2 nocona)
    x86(${cpu})
    expect(x86-${cpu} "${assembly}" "[\t ]bsrl[\t ]" "[\t ]bsfq[\t ]")
    reject(x86-${cpu} "${assembly}" "lzcnt" "tzcnt" "popcnt" "vfmadd"
        "%[yz]mm")
endforeach()
foreach(cpu nehalem corei7 silvermont goldmont)
    x86(${cpu})
    expect(x86-${cpu} "${assembly}" "[\t ]popcntq[\t ]" "[\t ]bsrl[\t ]")
    reject(x86-${cpu} "${assembly}" "lzcnt" "tzcnt" "vfmadd")
endforeach()
foreach(cpu amdfam10 barcelona btver1)
    x86(${cpu})
    expect(x86-${cpu} "${assembly}" "[\t ]lzcntl[\t ]" "[\t ]popcntq[\t ]"
        "[\t ]bsfq[\t ]")
    reject(x86-${cpu} "${assembly}" "tzcnt" "vfmadd")
endforeach()
foreach(cpu btver2 lujiazui)
    x86(${cpu})
    expect(x86-${cpu} "${assembly}" "[\t ]lzcntl[\t ]" "[\t ]tzcntq[\t ]"
        "[\t ]popcntq[\t ]")
    reject(x86-${cpu} "${assembly}" "vfmadd")
endforeach()
foreach(cpu bdver2 bdver4 broadwell core-avx2 alderlake arrowlake-s
            sierraforest yongfeng c86-4g-m4)
    x86(${cpu})
    expect(x86-${cpu} "${assembly}" "[\t ]lzcntl[\t ]" "[\t ]tzcntq[\t ]"
        "[\t ]vfmadd[0-9]+sd[\t ]" "[\t ]vfmadd[0-9]+ps[\t ]+%ymm")
    reject(x86-${cpu} "${assembly}" "%zmm")
endforeach()
# Xeon Phi has AVX-512F without AVX-512DQ's VPMULLQ.
foreach(cpu knl knm)
    x86(${cpu})
    expect(x86-${cpu} "${assembly}" "[\t ]vfmadd[0-9]+ps[\t ]+%zmm")
    reject(x86-${cpu} "${assembly}" "vpmullq")
endforeach()
foreach(cpu skx cascadelake cooperlake cannonlake icelake-client tigerlake
            sapphirerapids graniterapids novalake znver4 znver5 c86-4g-m7)
    x86(${cpu})
    expect(x86-${cpu} "${assembly}" "[\t ]vfmadd[0-9]+ps[\t ]+%zmm"
        "[\t ]vpmullq[\t ]+%zmm")
endforeach()

# MIPS, o32.
set(atomic "[\t ]ll[\t ]+[$]t1")
set(likely "[\t ]b(eq|ne)z?l[\t ]")
set(rotate "rotrv [$]")
set(movn "[\t ]mov[nz][\t ]")
foreach(cpu r3900 vr4100 vr4181 4100)
    mips(mips.x ${cpu})
    expect(mips-${cpu} "${errors}" "error: [^\n]*atomic")
endforeach()
foreach(cpu r4650 r4700 orion loongson2e)
    mips(mips.x ${cpu})
    expect(mips-${cpu} "${assembly}" "${atomic}" "[\t ]dmult[\t ]" "${likely}")
    if(cpu STREQUAL "loongson2e")
        expect(mips-${cpu} "${assembly}" "${movn}")
    else()
        reject(mips-${cpu} "${assembly}" "${movn}")
    endif()
endforeach()
foreach(cpu r10000 r12k 5000 rm7000 rm9000 sb1 5kf)
    mips(mips.x ${cpu})
    expect(mips-${cpu} "${assembly}" "${atomic}" "[\t ]dmult[\t ]" "${movn}"
        "${likely}")
    reject(mips-${cpu} "${assembly}" "${rotate}")
endforeach()
foreach(cpu vr5400 sr71000 loongson3a octeon octeon2 octeon3 gs464e xlp
            mips64r2)
    mips(mips.x ${cpu})
    expect(mips-${cpu} "${assembly}" "${atomic}" "[\t ]dmult[\t ]" "${movn}"
        "${rotate}" "${likely}")
endforeach()
# The VR5500's conditional moves are not used.
mips(mips.x vr5500)
expect(mips-vr5500 "${assembly}" "${rotate}" "[\t ]dmult[\t ]")
reject(mips-vr5500 "${assembly}" "${movn}")
foreach(cpu 4kc 4kp 4ksc m4k m14k 24kc 24kec 34kn 74kc 24kf 1004kf 74kf3_2
            interaptiv p5600 m5101)
    mips(mips.x ${cpu})
    expect(mips-${cpu} "${assembly}" "${atomic}" "${movn}" "${likely}"
        "[\t ]multu[\t ]")
    reject(mips-${cpu} "${assembly}" "dmult")
    if(cpu MATCHES "^4k[cps]")
        reject(mips-${cpu} "${assembly}" "${rotate}")
    else()
        expect(mips-${cpu} "${assembly}" "${rotate}")
    endif()
endforeach()
# The FPU: none, single precision, or double precision.
foreach(cpu r3900 vr4120 4kec 24kc 1004kc m14kc 5kc xlr octeon octeon+)
    mips(single.x ${cpu})
    expect(mips-${cpu}-single "${errors}" "forbids FPU instructions")
endforeach()
mips(double.x r4650)
expect(mips-r4650-double "${errors}" "single-precision-only FPU")
foreach(cpu r4650 24kf m5100 r10000 octeon3 gs464 74kfx)
    mips(single.x ${cpu})
    expect(mips-${cpu}-single "${assembly}" "[\t ]mul[.]s[\t ]")
endforeach()
