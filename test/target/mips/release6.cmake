# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Release 6 CPUs select the three-operand multiply and divide, SELEQZ,
# SELNEZ, and SEL.fmt selects, CMP.cond.fmt comparisons with BC1EQZ and
# BC1NEZ, ordinary misaligned accesses, and RDHWR at their own level, and
# none of the instructions Release 6 removes; earlier CPUs select none of
# the Release 6 forms. The objects record the Release 6 ISA, the 2008 NaN
# encoding, and the FPU register model, which o32 takes 64 bits wide from
# the CPU. Options that Release 6 cannot honor are rejected, and GNU as,
# when installed, encodes the assembly as cc -c does.

foreach(required CC SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")
set(runtime_source "${SOURCE}")

# The mnemonics, and the comments of encoded words, of SOURCE's assembly.
function(mnemonics label options result)
    separate_arguments(split UNIX_COMMAND "${options}")
    execute_process(COMMAND "${CC}" ${split} -fno-eval-calls -S "${SOURCE}"
                            -o "${OUTPUT}.${label}.s"
                    RESULT_VARIABLE status ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label}: cc ${options} failed\n${error}")
    endif()
    file(STRINGS "${OUTPUT}.${label}.s" lines)
    set(found "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^\t([a-z][a-z0-9.]*)(\t|$)")
            list(APPEND found "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^\t\\.word\t0x[0-9a-f]+\t# ([a-z0-9.]+) ")
            list(APPEND found "${CMAKE_MATCH_1}")
        elseif(line MATCHES "^(\\.set mips[0-9a-z]+|\\.nan 2008)$")
            list(APPEND found "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES found)
    set(${result} "${found}" PARENT_SCOPE)
endfunction()

function(expect label options present absent)
    mnemonics(${label} "${options}" found)
    foreach(name IN LISTS present)
        if(NOT name IN_LIST found)
            message(FATAL_ERROR "${label}: ${options} selects no '${name}'\n${found}")
        endif()
    endforeach()
    foreach(name IN LISTS absent)
        if(name IN_LIST found)
            message(FATAL_ERROR "${label}: ${options} selects '${name}'")
        endif()
    endforeach()
endfunction()

# The instructions Release 6 removes, and the ones it adds that this source
# selects.
set(removed mult multu dmult dmultu mfhi mflo mthi mtlo madd maddu msub msubu
    movn movz movt movf movn.s movn.d movz.s movz.d movt.s movt.d movf.s movf.d
    c.eq.s c.eq.d c.lt.s c.lt.d c.le.s c.le.d c.olt.s c.olt.d c.un.s c.un.d
    bc1t bc1f bc1tl bc1fl beql bnel beqzl bnezl bltzl bgezl blezl bgtzl
    lwl lwr swl swr ldl ldr sdl sdr)
set(added_words mul muhu mod modu seleqz selnez sel.s sel.d cmp.slt.s
    cmp.slt.d cmp.sle.d cmp.eq.s cmp.eq.d cmp.lt.d cmp.un.d bc1eqz bc1nez)
set(added_doublewords dmul dmuhu ddiv dmod ddivu dmodu)
set(o32 "-target mips-unknown-elf -mabi=o32")
set(o32el "-target mipsel-unknown-elf -mabi=o32")
set(n64 "-target mips64-unknown-elf -mabi=n64")
# Without optimization, selects are branches and multiply-high a product.
set(added_unoptimized mul mulu muhu div mod divu modu bc1eqz bc1nez)
foreach(level O0 O2)
    if(level STREQUAL "O0")
        set(words ${added_unoptimized})
        set(doublewords dmul ddiv dmod ddivu dmodu)
    else()
        set(words ${added_words})
        set(doublewords ${added_doublewords})
    endif()
    expect(mips32r6-${level} "${o32} -march=mips32r6 -${level}"
           "${words};.set mips32r6;.nan 2008" "${removed};${added_doublewords}")
    expect(mips32r6-el-${level} "${o32el} -march=mips32r6 -${level}"
           "${words};.set mips32r6" "${removed}")
    # With 64-bit registers a word's multiply-high is a doubleword product,
    # and with 64-bit addresses an index scales by doublewords.
    set(wide_words ${words})
    list(REMOVE_ITEM wide_words muh muhu mulu)
    expect(mips64r6-o32-${level} "${o32} -march=mips64r6 -${level}"
           "${wide_words};${doublewords};.set mips64r6" "${removed}")
    expect(mips64r6-${level} "${n64} -march=mips64r6 -${level}"
           "${wide_words};${doublewords};.set mips64r6;.nan 2008" "${removed}")
endforeach()
set(wide_words ${added_words})
list(REMOVE_ITEM wide_words muh muhu mulu)
foreach(cpu i6400 i6500 p6600)
    expect(${cpu} "-target mips64el-unknown-elf -march=${cpu} -O2"
           "${wide_words};${added_doublewords};.set mips64r6" "${removed}")
endforeach()
expect(mips32r6-x32 "-target mips-unknown-elf -march=mips32r6 -O2"
       "${added_words}" "${removed}")
# Before Release 6 none of the mnemonics that only Release 6 has is
# selected (MUL, DIV, DIVU, DDIV, and DDIVU name forms of every release).
set(release6_only ${added_words} ${added_doublewords} .set\ mips32r6 .set\ mips64r6)
list(REMOVE_ITEM release6_only mul ddiv ddivu)
foreach(cpu mips32r2 vr4300 mips4)
    expect(${cpu} "${o32} -march=${cpu} -O2" "" "${release6_only}")
endforeach()
expect(mips64r2 "${n64} -march=mips64r2 -O2" "" "${release6_only}")

# Misaligned members: ordinary loads and stores on Release 6, bytes with
# -mno-unaligned-access and before Release 6.
file(WRITE "${OUTPUT}.packed.x" "struct r [[packed]] { u8 tag; u32 word; f64 value; };\nglobal u32 get(in struct r *p) { return p->word; }\nglobal f64 value(in struct r *p) { return p->value; }\nglobal void put(in struct r *p, in u32 x) { p->word = x; }\n")
set(SOURCE "${OUTPUT}.packed.x")
expect(packed "${o32} -march=mips32r6 -O2" "lw;sw;ldc1" "lbu;sb")
expect(packed-bytes "${o32} -march=mips32r6 -mno-unaligned-access -O2" "lbu;sb" "")
expect(packed-r2 "${o32} -march=mips32r2 -O2" "lbu;sb" "")
expect(packed-option "${o32} -march=mips32r2 -munaligned-access -O2" "lw;sw" "lbu;sb")

# The re-encoded CACHE takes a 9-bit displacement: a constant offset folds
# into it only within that range.
file(WRITE "${OUTPUT}.cache.x" "global void flush(in u8 *line) {\n    $::_cache(0x19, line[16]);\n    $::_cache(0x19, line[300]);\n}\n")
foreach(level O0 O2)
    execute_process(COMMAND "${CC}" -target mips-unknown-elf -march=mips32r6 -${level} -c
                            "${OUTPUT}.cache.x" -o "${OUTPUT}.cache-${level}.o"
                    RESULT_VARIABLE status ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "cache-${level}: cc -c failed\n${error}")
    endif()
    execute_process(COMMAND "${CC}" -target mips-unknown-elf -march=mips32r6 -${level} -S
                            "${OUTPUT}.cache.x" -o "${OUTPUT}.cache-${level}.s")
    file(STRINGS "${OUTPUT}.cache-${level}.s" cache_lines REGEX "^\tcache\t")
    list(LENGTH cache_lines cache_count)
    if(NOT cache_count EQUAL 2 OR cache_lines MATCHES "300")
        message(FATAL_ERROR "cache-${level}: unexpected cache operations\n${cache_lines}")
    endif()
endforeach()

# RDHWR reads the thread pointer at the Release 6 level, not under the
# `.set mips32r2` that earlier ISAs need.
file(WRITE "${OUTPUT}.tls.x" "[[thread_local]] global u32 counter;\nglobal u32 bump(in u32 by) { counter += by; return counter; }\n")
foreach(case "o32|${o32} -march=mips32r6" "o32-pic|${o32} -march=mips32r6 -fpic"
             "n64-pic|${n64} -march=mips64r6 -fpic")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 label)
    list(GET fields 1 options)
    separate_arguments(options UNIX_COMMAND "${options}")
    execute_process(COMMAND "${CC}" ${options} -O2 -S "${OUTPUT}.tls.x"
                            -o "${OUTPUT}.tls-${label}.s"
                    RESULT_VARIABLE status ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "tls-${label}: cc failed\n${error}")
    endif()
    file(READ "${OUTPUT}.tls-${label}.s" text)
    if(NOT text MATCHES "\trdhwr\t[^\n]*,\\$29\n" OR text MATCHES "\\.set mips32r2")
        message(FATAL_ERROR "tls-${label}: unexpected thread pointer read\n${text}")
    endif()
endforeach()

# FPXX code keeps 64-bit values, comparison results among them, in even
# registers.
separate_arguments(fpxx_options UNIX_COMMAND "${o32} -march=mips32r6 -mfpxx -O2 -fno-eval-calls")
execute_process(COMMAND "${CC}" ${fpxx_options} -c "${runtime_source}" -o "${OUTPUT}.fpxx.o"
                RESULT_VARIABLE status ERROR_VARIABLE error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "fpxx: cc -c failed\n${error}")
endif()
execute_process(COMMAND "${CC}" ${fpxx_options} -S "${runtime_source}" -o "${OUTPUT}.fpxx.s")
file(READ "${OUTPUT}.fpxx.s" text)
if(NOT text MATCHES "\tcmp\\.[a-z]+\\.d\t\\$f4," OR
   text MATCHES "\\$f([13579]|[12][13579]|3[1])[^0-9]")
    message(FATAL_ERROR "fpxx: odd FPRs or no comparison into $f4\n${text}")
endif()

# A comparison's result register is saved where the ABI preserves it: a
# user ABI copied from n64 without f1 among its clobbers.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../../../model/mips.xm" shipped)
string(FIND "${shipped}" "abi \"n64\" {" n64_start)
string(SUBSTRING "${shipped}" ${n64_start} -1 n64_text)
string(FIND "${n64_text}" "\n}\n" n64_end)
math(EXPR n64_end "${n64_end} + 3")
string(SUBSTRING "${n64_text}" 0 ${n64_end} n64_text)
string(REPLACE "abi \"n64\" {" "abi \"n64_f1\" {" n64_text "${n64_text}")
string(REGEX REPLACE "aliases = \\[[^]]*\\];" "" n64_text "${n64_text}")
string(REPLACE "\"f0\", \"f1\", " "\"f0\", " n64_text "${n64_text}")
file(WRITE "${OUTPUT}.keep-f1.xm" "${n64_text}")
file(WRITE "${OUTPUT}.keep-f1.x" "global bool less(f64 a, f64 b) { return a < b; }\n")
foreach(level -O0 -O2)
    execute_process(COMMAND "${CC}" "--model=${OUTPUT}.keep-f1.xm" -target mips64-unknown-elf
                            -mabi=n64_f1 -march=mips64r6 ${level} -S "${OUTPUT}.keep-f1.x"
                            -o "${OUTPUT}.keep-f1${level}.s"
                    RESULT_VARIABLE status ERROR_VARIABLE error)
    file(READ "${OUTPUT}.keep-f1${level}.s" text)
    if(NOT status EQUAL 0 OR NOT text MATCHES "\tsdc1\t\\$f1," OR
       NOT text MATCHES "\tldc1\t\\$f1,")
        message(FATAL_ERROR "keep-f1 ${level}: $f1 is not saved\n${error}\n${text}")
    endif()
endforeach()

# The objects' ISA, NaN encoding, and FPU register model.
find_program(LLVM_READELF NAMES llvm-readelf)
if(NOT LLVM_READELF)
    message(FATAL_ERROR "llvm-readelf is required")
endif()
set(empty "${OUTPUT}.empty.x")
file(WRITE "${empty}" "global f64 twice(in f64 x) { return x + x; }\n")
set(integer "${OUTPUT}.integer.x")
file(WRITE "${integer}" "global u32 twice(in u32 x) { return x + x; }\n")
foreach(case "o32|${o32} -march=mips32r6|0x9000[0-9a-f]+, .*nan2008, o32, mips32r6|MIPS32r6|Hard float [(]32-bit CPU, 64-bit FPU[)]"
             "o32-fpxx|${o32} -march=mips32r6 -mfpxx|mips32r6|MIPS32r6|Hard float [(]32-bit CPU, Any FPU[)]"
             "o32-soft|${o32} -march=mips32r6 -msoft-float|mips32r6|MIPS32r6|Soft float"
             "x32|-target mips-unknown-elf -march=mips32r6|mips32r6|MIPS32r6|Hard float [(]32-bit CPU, 64-bit FPU[)]"
             "o32-mips64r6|${o32} -march=i6400|0x[aA]000[0-9a-fA-F]+, .*32bitmode, nan2008, o32, mips64r6|MIPS64r6|Hard float [(]32-bit CPU, 64-bit FPU[)]"
             "n64|${n64} -march=mips64r6|0x[aA]000[0-9a-fA-F]+, .*nan2008, mips64r6|MIPS64r6|Hard float [(]double precision[)]"
             "r2-fp64|${o32} -march=mips32r2 -mfp64|mips32r2|MIPS32r2|Hard float [(]32-bit CPU, 64-bit FPU[)]")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 label)
    list(GET fields 1 options)
    list(GET fields 2 flags)
    list(GET fields 3 isa)
    list(GET fields 4 fp_abi)
    separate_arguments(options UNIX_COMMAND "${options}")
    set(input "${empty}")
    if(label MATCHES "soft")
        set(input "${integer}")
    endif()
    execute_process(COMMAND "${CC}" ${options} -O2 -c "${input}" -o "${OUTPUT}.${label}.o"
                    RESULT_VARIABLE status ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label}: cc -c failed\n${error}")
    endif()
    execute_process(COMMAND "${LLVM_READELF}" -h -A "${OUTPUT}.${label}.o" OUTPUT_VARIABLE text)
    if(NOT text MATCHES "Flags: +[^\n]*${flags}" OR NOT text MATCHES "ISA: ${isa}\n" OR
       NOT text MATCHES "FP ABI: ${fp_abi}\n")
        message(FATAL_ERROR "${label}: unexpected header or ABI flags\n${text}")
    endif()
endforeach()

# Options Release 6 cannot honor; the o32 ABI's FPU register model yields
# to the CPU's; 64-bit FPRs need 64-bit GPRs or MTHC1, and the 2008 NaN
# encoding Release 2.
function(rejects label options pattern)
    separate_arguments(split UNIX_COMMAND "${options}")
    execute_process(COMMAND "${CC}" ${split} -S "${empty}" -o "${OUTPUT}.${label}.s"
                    RESULT_VARIABLE status ERROR_VARIABLE error)
    if(status EQUAL 0 OR NOT error MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: '${options}' is not rejected with '${pattern}'\n${error}")
    endif()
endfunction()
rejects(fp32 "${o32} -march=mips32r6 -mfp32" "feature 'mips32r6' of CPU 'mips32r6' conflicts with '-mfp32'")
rejects(branch-likely "${n64} -march=i6400 -mbranch-likely"
        "feature 'mips64r6' of CPU 'i6400' conflicts with '-mbranch-likely'")
rejects(cond-move "${o32} -march=mips32r6 -mcond-move" "conflicts with '-mcond-move'")
rejects(nan-legacy "${o32} -march=mips32r6 -mno-nan2008" "requires 'nan2008'")
rejects(fp64-mips2 "${o32} -march=mips2 -mfp64" "'-mfp64' requires '-mmips3' or '-mmips32r2'")
rejects(fp64-mips32 "${o32} -march=mips32 -mfp64" "'-mfp64' requires '-mmips3' or '-mmips32r2'")
string(REPLACE " " ";" o32_split "${o32}")
execute_process(COMMAND "${CC}" ${o32_split} -march=mips32r6 --print-options=target
                OUTPUT_VARIABLE options_text)
if(NOT options_text MATCHES "m[.]fp64  type: boolean  value: on" OR
   NOT options_text MATCHES "m[.]fp32  type: boolean  value: off")
    message(FATAL_ERROR "o32 on mips32r6 does not keep the CPU's 64-bit FPRs\n${options_text}")
endif()
execute_process(COMMAND "${CC}" ${o32_split} -march=mips64r2 -mnan2008 -S "${empty}"
                        -o "${OUTPUT}.nan.s" RESULT_VARIABLE status ERROR_VARIABLE error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "-mnan2008 on mips64r2 is rejected\n${error}")
endif()

# GNU as encodes the Release 6 assembly as cc -c does. On Release 6 it
# keeps the relocations of branches within their section, so the objects
# are compared once linked at one address: o32 objects by GNU ld, n64 ones
# by LLD, as the runtime tests link them.
find_program(GNU_AS NAMES mips64-elf-as)
find_program(GNU_LD NAMES mips64-elf-ld)
find_program(LLD NAMES ld.lld)
find_program(LLVM_OBJCOPY NAMES llvm-objcopy)
if(NOT GNU_AS OR NOT GNU_LD OR NOT LLD OR NOT LLVM_OBJCOPY)
    message(STATUS "skipping the GNU as comparison: mips64-elf-as, mips64-elf-ld, ld.lld, and llvm-objcopy are required")
    return()
endif()
set(SOURCE "${runtime_source}")
# MIPS32 Release 2 with 64-bit FPRs, which shares the word-pair transfers of
# MIPS32 Release 6, selects only instructions GNU as accepts there.
execute_process(COMMAND "${CC}" ${o32_split} -march=mips32r2 -mfp64 -O2 -fno-eval-calls -S
                        "${SOURCE}" -o "${OUTPUT}.r2-fp64.s"
                RESULT_VARIABLE status ERROR_VARIABLE error)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "r2-fp64: cc failed\n${error}")
endif()
execute_process(COMMAND "${GNU_AS}" -EB -march=mips32r2 -mfp64 -mabi=32 "${OUTPUT}.r2-fp64.s"
                        -o "${OUTPUT}.r2-fp64.o" RESULT_VARIABLE status ERROR_VARIABLE error)
if(NOT status EQUAL 0 OR NOT error STREQUAL "")
    message(FATAL_ERROR "r2-fp64: GNU as does not accept the assembly\n${error}")
endif()
# The assemblers align sections differently, so each starts at an address
# of its own.
set(o32_layout "-Ttext=0x80100000 --section-start=.rodata=0x80180000 -Tdata=0x80200000 -Tbss=0x80300000")
set(n64_layout "-Ttext=0xffffffff80100000 --section-start=.rodata=0xffffffff80180000 -Tdata=0xffffffff80200000 -Tbss=0xffffffff80300000")
foreach(case "o32|${o32} -march=mips32r6 -O2|-EB -march=mips32r6 -mabi=32|${GNU_LD} -EB -m elf32ebmip ${o32_layout}"
             "o32-el-O0|${o32el} -march=mips32r6 -O0|-EL -march=mips32r6 -mabi=32|${GNU_LD} -EL -m elf32ebmip ${o32_layout}"
             "o32-mips64r6|${o32} -march=mips64r6 -O2|-EB -march=mips64r6 -mabi=32|${GNU_LD} -EB -m elf32ebmip ${o32_layout}"
             "n64|${n64} -march=mips64r6 -O2|-EB -march=mips64r6 -mabi=64|${LLD} -m elf64btsmip ${n64_layout}"
             "n64-O0|${n64} -march=i6500 -O0|-EB -march=i6500 -mabi=64|${LLD} -m elf64btsmip ${n64_layout}")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 label)
    list(GET fields 1 options)
    list(GET fields 2 gnu_options)
    list(GET fields 3 link_options)
    separate_arguments(options UNIX_COMMAND "${options}")
    separate_arguments(gnu_options UNIX_COMMAND "${gnu_options}")
    separate_arguments(link_options UNIX_COMMAND "${link_options}")
    set(stem "${OUTPUT}.gnu-${label}")
    execute_process(COMMAND "${CC}" ${options} -fno-eval-calls -c "${SOURCE}"
                            -o "${stem}.cross.o" RESULT_VARIABLE status ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label}: cc -c failed\n${error}")
    endif()
    execute_process(COMMAND "${CC}" ${options} -fno-eval-calls -S "${SOURCE}"
                            -o "${stem}.s" RESULT_VARIABLE status ERROR_VARIABLE error)
    execute_process(COMMAND "${GNU_AS}" ${gnu_options} "${stem}.s" -o "${stem}.gnu.o"
                    RESULT_VARIABLE status ERROR_VARIABLE error)
    if(NOT status EQUAL 0 OR NOT error STREQUAL "")
        message(FATAL_ERROR "${label}: GNU as does not accept the assembly\n${error}")
    endif()
    foreach(origin cross gnu)
        execute_process(COMMAND ${link_options} -e 0 "${stem}.${origin}.o"
                                -o "${stem}.${origin}.elf"
                        RESULT_VARIABLE status ERROR_VARIABLE error)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "${label}: the ${origin} object does not link\n${error}")
        endif()
        execute_process(COMMAND "${LLVM_OBJCOPY}" -O binary --only-section=.text
                                "${stem}.${origin}.elf" "${stem}.${origin}.text"
                        RESULT_VARIABLE status ERROR_VARIABLE error)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "${label}: cannot extract ${origin} .text\n${error}")
        endif()
        file(READ "${stem}.${origin}.text" ${origin}_text HEX)
    endforeach()
    if(NOT cross_text STREQUAL gnu_text)
        message(FATAL_ERROR "${label}: GNU as encodes the assembly differently")
    endif()
endforeach()
message(STATUS "Release 6 selections checked")
