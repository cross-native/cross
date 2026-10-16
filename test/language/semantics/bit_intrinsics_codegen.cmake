# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# The instructions or inline sequences the bit intrinsics select: by the
# features and the -march CPU's instructions, by the -mtune costs of a user
# CPU, on x86-64 and on the MIPS CPUs.
foreach(required CC OUTPUT MODEL)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

set(source "${OUTPUT}.x")
file(WRITE "${source}" [=[
global u32 leading32(in u32 x) { return $::clz(x); }
global u32 leading64(in u64 x) { return $::clz(x); }
global u32 trailing32(in u32 x) { return $::ctz(x); }
global u32 trailing64(in u64 x) { return $::ctz(x); }
global u32 ones32(in u32 x) { return $::popcount(x); }
global u32 ones64(in u64 x) { return $::popcount(x); }
global u16 swap16(in u16 x) { return $::bswap(x); }
global u32 swap32(in u32 x) { return $::bswap(x); }
global u64 swap64(in u64 x) { return $::bswap(x); }
global u8 left8(in u8 x, in i32 n) { return $::rotl(x, n); }
global u32 left32(in u32 x, in u32 n) { return $::rotl(x, n); }
global u64 right64(in u64 x, in u32 n) { return $::rotr(x, n); }
global u32 left32_by_5(in u32 x) { return $::rotl(x, 5); }
global u32 leading_ones32(in u32 x) { return $::clz(~x); }
global u32 leading_ones64(in u64 x) { return $::clz(~x); }
]=])

function(compile label)
    execute_process(
        COMMAND "${CC}" -S -O2 "--model=${MODEL}" ${ARGN} "${source}"
                -o "${OUTPUT}-${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${stdout}\n${stderr}")
    endif()
    file(READ "${OUTPUT}-${label}.s" assembly)
    set(assembly "${assembly}" PARENT_SCOPE)
endfunction()

function(expect label)
    foreach(pattern IN LISTS ARGN)
        if(NOT assembly MATCHES "${pattern}")
            message(FATAL_ERROR "${label} lacks '${pattern}'\n${assembly}")
        endif()
    endforeach()
endfunction()

function(reject label)
    foreach(pattern IN LISTS ARGN)
        if(assembly MATCHES "${pattern}")
            message(FATAL_ERROR "${label} contains '${pattern}'\n${assembly}")
        endif()
    endforeach()
endfunction()

set(x86 -target x86_64-unknown-linux-gnu)
set(scan "[\t ]bsrl[\t ]" "[\t ]bsrq[\t ]" "[\t ]bsfl[\t ]" "[\t ]bsfq[\t ]"
         "[\t ]cmovnel[\t ]")
set(swaps "[\t ]bswapl[\t ]" "[\t ]bswapq[\t ]" "[\t ]rolw[\t ]+[$]8,"
          "[\t ]rolb[\t ]" "[\t ]roll[\t ]" "[\t ]rorq[\t ]")
set(counts "[\t ]lzcntl[\t ]" "[\t ]lzcntq[\t ]" "[\t ]tzcntl[\t ]"
           "[\t ]tzcntq[\t ]" "[\t ]popcntl[\t ]" "[\t ]popcntq[\t ]")

# The baseline has BSR and BSF; POPCNT, LZCNT, and TZCNT need features.
compile(x86-baseline ${x86})
expect(x86-baseline ${scan} ${swaps} "[\t ]imull[\t ]")
reject(x86-baseline "lzcnt" "tzcnt" "popcnt")
compile(x86-features ${x86} -mlzcnt -mbmi -mpopcnt)
expect(x86-features ${counts} ${swaps})
reject(x86-features "[\t ]bs[fr][lq][\t ]")
compile(x86-v3 ${x86} -march=x86-64-v3)
expect(x86-v3 ${counts})
compile(x86-disabled ${x86} -march=haswell -mno-lzcnt -mno-bmi -mno-popcnt)
expect(x86-disabled ${scan})
reject(x86-disabled "lzcnt" "tzcnt" "popcnt")
# Costs choose: a slow LZCNT loses to BSR, a slow POPCNT to the sequence.
compile(x86-slow ${x86} -march=x86-64-v3 -mtune=x86-slow-counts)
expect(x86-slow "[\t ]bsrl[\t ]" "[\t ]tzcntl[\t ]")
reject(x86-slow "lzcnt" "popcnt")
# Without BSR, BSF, BSWAP, ROL, and ROR every operation is a sequence.
compile(x86-none ${x86} -march=x86-no-bit-instructions)
reject(x86-none "[\t ]bs[fr][lq][\t ]" "[\t ]bswap" "[\t ]ro[lr][bwlq][\t ]")

# MIPS I and III have no instruction for any of them but the rotations.
foreach(profile vr4300-o32 r3000-o32)
    compile(${profile} -mprofile=${profile})
    expect(${profile} "[\t ]sltu[\t ]")
    reject(${profile} "[\t ](clz|clo|dclz|wsbh|rotr)[\t ]" "[.]word")
endforeach()
set(mips -target mips-unknown-elf -mabi=o32)
compile(mips32 ${mips} -march=mips32)
expect(mips32 "[\t ]clz[\t ]" "[\t ]clo[\t ]")
reject(mips32 "[\t ](wsbh|rotr)[\t ]")
compile(mips32r2 ${mips} -march=mips32r2)
expect(mips32r2 "[\t ]clz[\t ]" "[\t ]wsbh[\t ]" "[\t ]rotr[\t ]" "# rotrv"
       "# rotr [^\n]*,27\n")
compile(mips64 -mprofile=mips64-n64)
expect(mips64 "[\t ]clz[\t ]" "[\t ]dclz[\t ]" "[\t ]clo[\t ]" "[\t ]dclo[\t ]")
# Allegrex's CLZ, WSBH, WSBW, and ROTRV are words, CLZ as SPECIAL 0x16.
compile(allegrex -mprofile=psp-allegrex)
expect(allegrex "[.]word[\t ]+0x0[0-9a-f]+16[\t ]+# clz"
       "[.]word[\t ]+0x0[0-9a-f]+17[\t ]+# clo"
       "[.]word[\t ]+0x7c0[0-9a-f]+a0[\t ]+# wsbh"
       "[.]word[\t ]+0x7c0[0-9a-f]+e0[\t ]+# wsbw" "# rotrv"
       "[.]word[\t ]+0x002[0-9a-f]+c2[\t ]+# rotr [^\n]*,27\n")
reject(allegrex "\n[\t ]+(clz|clo|wsbh|wsbw|rotr|rotrv)[\t ]")
# A CPU without CLZ, or whose -mtune costs make it slow, uses sequences.
compile(mips32-noclz ${mips} -march=mips32-noclz)
reject(mips32-noclz "[\t ](clz|wsbh)[\t ]")
expect(mips32-noclz "[\t ]sltu[\t ]")
compile(mips32-slowclz ${mips} -march=mips32r2 -mtune=mips32-slowclz)
reject(mips32-slowclz "[\t ]clz[\t ]")
expect(mips32-slowclz "[\t ]wsbh[\t ]")
