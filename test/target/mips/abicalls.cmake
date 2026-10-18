# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# -mabicalls code of SOURCE: the o32, n64, and x64 PIC and -mno-shared
# assembly sets up $gp, reaches objects, functions, and labels through the
# GOT, calls through $t9, and reloads $gp after calls where the callee may
# change it; thread-local objects use initial-exec or local-exec accesses
# off `rdhwr $3,$29`. -mno-abicalls code is the default and uses neither.
# cc -c and GNU as, when installed, encode each assembly alike with the same
# relocations. MODEL's ABIs show that pic_call_clobbers decides whether
# callers reload $gp, and invalid combinations are diagnosed.

foreach(required CC SOURCE MODEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
find_program(LLVM_READELF NAMES llvm-readelf REQUIRED)
find_program(GNU_AS NAMES mips64-elf-as)
find_program(GNU_OBJDUMP NAMES mips64-elf-objdump)
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")

function(run_cc label)
    execute_process(COMMAND "${CC}" ${ARGN} RESULT_VARIABLE status OUTPUT_VARIABLE stdout
                    ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${stdout}\n${stderr}")
    endif()
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
            message(FATAL_ERROR "${label} has '${pattern}'\n${text}")
        endif()
    endforeach()
endfunction()

# The instruction words and the relocations of an object, through GNU
# objdump.
function(object_listing object result)
    execute_process(COMMAND "${GNU_OBJDUMP}" -d -r "${object}" OUTPUT_VARIABLE text
                    RESULT_VARIABLE status)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "cannot disassemble ${object}")
    endif()
    string(REPLACE ";" "," text "${text}")
    string(REPLACE "\n" ";" lines "${text}")
    set(listing "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^ *[0-9a-f]+:\t([0-9a-f]+)[ \t]")
            string(APPEND listing " ${CMAKE_MATCH_1}")
        elseif(line MATCHES "^\t+([0-9a-f]+): (R_MIPS_[A-Z0-9_]+)\t(.*)$")
            string(APPEND listing "\n${CMAKE_MATCH_1} ${CMAKE_MATCH_2} ${CMAKE_MATCH_3}\n")
        endif()
    endforeach()
    set(${result} "${listing}" PARENT_SCOPE)
endfunction()

# variant(NAME "GNU-AS-OPTIONS" CC-OPTION...): the assembly in NAME.s, its
# cc -c object, which -mabicalls flags PIC and CPIC, and GNU as's object
# of the assembly with the same words and relocations.
function(variant name gnu_options)
    set(base "${OUTPUT}/${name}")
    run_cc("${name} assembly" -S -O2 ${ARGN} "${SOURCE}" -o "${base}.s")
    run_cc("${name} object" -c -O2 ${ARGN} "${SOURCE}" -o "${base}.o")
    execute_process(COMMAND "${LLVM_READELF}" -h "${base}.o" OUTPUT_VARIABLE header)
    if(NOT header MATCHES "Flags: +0x[0-9a-f]+, noreorder, pic, cpic")
        message(FATAL_ERROR "${name} object is not flagged PIC and CPIC\n${header}")
    endif()
    if(GNU_AS AND GNU_OBJDUMP)
        separate_arguments(gnu_options UNIX_COMMAND "${gnu_options}")
        execute_process(COMMAND "${GNU_AS}" ${gnu_options} --fatal-warnings "${base}.s"
                                -o "${base}.gnu.o"
                        RESULT_VARIABLE status ERROR_VARIABLE stderr)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "GNU as rejected ${name}\n${stderr}")
        endif()
        object_listing("${base}.o" cross)
        object_listing("${base}.gnu.o" gnu)
        if(NOT cross STREQUAL gnu)
            message(FATAL_ERROR "cc -c and GNU as encode ${name} differently\n"
                                "cc -c:${cross}\nGNU as:${gnu}")
        endif()
    endif()
    file(READ "${base}.s" assembly)
    set(assembly "${assembly}" PARENT_SCOPE)
endfunction()

set(static "__cross_static_[0-9]+_")
variant(o32-pic "-EB -32 -march=vr4300" -target mips-unknown-elf -march=vr4300 -fPIC)
expect(o32-pic "${assembly}"
    "^\\.abicalls\n\\.option pic2\n"
    "\tlui\t\\$gp,%hi\\(_gp_disp\\)\n\taddiu\t\\$gp,\\$gp,%lo\\(_gp_disp\\)\n\taddu\t\\$gp,\\$gp,\\$t9\n"
    "\tlw\t\\$t9,%call16\\(scramble_gp\\)\\(\\$gp\\)\n\tjalr\t\\$t9\n[^\n]*\n\tlw\t\\$gp,[0-9]+\\(\\$sp\\)\n"
    "\tlw\t\\$t9,%got\\(${static}square\\)\\(\\$gp\\)\n\taddiu\t\\$t9,\\$t9,%lo\\(${static}square\\)\n\tjalr\t\\$t9\n"
    "\tlw\t\\$[a-z0-9]+,%got\\(exported\\)\\(\\$gp\\)\n"
    "%got\\(${static}hidden\\)\\(\\$gp\\)\n\tlw\t\\$[a-z0-9]+,%lo\\(${static}hidden\\)"
    "\tlw\t\\$[a-z0-9]+,%got\\(twice\\)\\(\\$gp\\)\n"
    "%got\\(\\.Lcross\\.mips\\.[0-9_]+\\.bb\\.[0-9]+\\)\\(\\$gp\\)\n\taddiu"
    "\tlw\t\\$at,%gottprel\\(tls_counter\\)\\(\\$gp\\)\n\\.set push\n\\.set mips32r2\n\trdhwr\t\\$v1,\\$29\n\\.set pop\n"
    "%gottprel\\(partner_tls\\)" "%gottprel\\(${static}tls_bytes\\)"
    "\tlui\t\\$at,%tprel_hi\\(tls_table\\)\n" "%tprel_lo\\(tls_table\\)"
    "%tprel_hi\\(${static}tls_seed\\)"
    "\\.section \"\\.tdata\",\"awT\",@progbits" "\\.section \"\\.tbss\",\"awT\",@nobits")
reject(o32-pic "${assembly}" "\\.option pic0" "\tjal\t" "%hi\\(exported\\)")

variant(o32-no-shared "-EL -32 -march=r3000" -target mipsel-unknown-elf -march=r3000 -mabicalls
        -mno-shared)
expect(o32-no-shared "${assembly}"
    "\tlui\t\\$gp,%hi\\(__gnu_local_gp\\)\n\taddiu\t\\$gp,\\$gp,%lo\\(__gnu_local_gp\\)\n"
    "\\.option pic0\n\tjal\t__cross_group_triple\n\\.option pic2\n"
    "\tlw\t\\$t9,%call16\\(partner_add\\)\\(\\$gp\\)\n"
    "\tlw\t\\$[a-z0-9]+,%got\\(partner_value\\)\\(\\$gp\\)\n"
    "%hi\\(exported\\)")
reject(o32-no-shared "${assembly}" "_gp_disp" "%got\\(exported\\)")

variant(n64-pic "-EB -64 -march=mips3" -target mips64-unknown-elf -march=mips3 -mabi=n64 -fPIC)
expect(n64-pic "${assembly}"
    "\tsd\t\\$28,[0-9]+\\(\\$29\\)\n\tlui\t\\$28,%hi\\(%neg\\(%gp_rel\\(runtime_check\\)\\)\\)\n\tdaddu\t\\$28,\\$28,\\$25\n\tdaddiu\t\\$28,\\$28,%lo\\(%neg\\(%gp_rel\\(runtime_check\\)\\)\\)\n"
    "\tld\t\\$25,%call16\\(partner_add\\)\\(\\$28\\)\n"
    "\tld\t\\$[0-9]+,%got_disp\\(exported\\)\\(\\$28\\)\n"
    "\tld\t\\$[0-9]+,%got_page\\(${static}hidden\\)\\(\\$28\\)\n"
    "%got_ofst\\(${static}hidden\\)"
    "\tld\t\\$1,%gottprel\\(tls_counter\\)\\(\\$28\\)\n\\.set push\n\\.set mips32r2\n\trdhwr\t\\$3,\\$29\n"
    "\tlui\t\\$1,%tprel_hi\\(tls_table\\)\n")

variant(n64-no-shared "-EL -64 -march=mips64r2" -target mips64el-unknown-elf -march=mips64r2
        -mabi=n64 -mabicalls -mno-shared)
expect(n64-no-shared "${assembly}" "%hi\\(%neg\\(%gp_rel\\(runtime_check\\)\\)\\)"
       "\trdhwr\t\\$3,\\$29\n")
reject(n64-no-shared "${assembly}" "\\.set mips32r2\n\trdhwr")

variant(o32-pie "-EB -32 -march=mips32r2" -target mips-unknown-elf -march=mips32r2 -fpie)
expect(o32-pie "${assembly}" "_gp_disp" "%call16\\(partner_add\\)")

# Under x64, which lets calls change $gp, a function keeps no incoming $gp
# and reloads its own after calls.
variant(x64-pic "-EB -64 -march=mips3" -target mips64-unknown-elf -march=mips3 -mabi=x64 -fPIC)
expect(x64-pic "${assembly}"
    "\nruntime_check:\n[^\n]*\n\tlui\t\\$28,%hi\\(%neg\\(%gp_rel\\(runtime_check\\)\\)\\)\n"
    "\tld\t\\$25,%call16\\(scramble_gp\\)\\(\\$28\\)\n\tjalr\t\\$25\n[^\n]*\n\tld\t\\$28,[0-9]+\\(\\$29\\)\n")

# -mno-abicalls code is the default: absolute addresses, direct calls, and
# local-exec accesses only.
run_cc(absolute -S -O2 -target mips-unknown-elf -march=vr4300 "${SOURCE}"
       -o "${OUTPUT}/absolute.s")
run_cc(no-abicalls -S -O2 -target mips-unknown-elf -march=vr4300 -mno-abicalls "${SOURCE}"
       -o "${OUTPUT}/no-abicalls.s")
file(READ "${OUTPUT}/absolute.s" absolute)
file(READ "${OUTPUT}/no-abicalls.s" no_abicalls)
if(NOT absolute STREQUAL no_abicalls)
    message(FATAL_ERROR "-mno-abicalls changed the default assembly")
endif()
expect(absolute "${absolute}" "\tjal\t" "%hi\\(exported\\)" "%tprel_hi\\(tls_counter\\)"
       "%tprel_hi\\(partner_tls\\)")
reject(absolute "${absolute}" "abicalls" "%got" "%call16" "%gottprel" "\\$gp")

# pic_call_clobbers: callers of gp_reload64 functions reload $gp under
# -mabicalls, and those functions keep no incoming $gp, unlike n64 ones.
set(model_source "${OUTPUT}/model.x")
file(WRITE "${model_source}" "u64 other(u64 x);\nglobal u64 value;\nglobal u64 model_call(u64 x) { return other(x) + value; }\n")
run_cc(gp_reload64 -S -O2 --model=${MODEL} -target mips64-unknown-elf -march=mips3
       -mabi=gp_reload64 -fPIC "${model_source}" -o "${OUTPUT}/gp_reload64.s")
file(READ "${OUTPUT}/gp_reload64.s" assembly)
expect(gp_reload64 "${assembly}"
       "\nmodel_call:\n[^\n]*\n\tlui\t\\$28,%hi\\(%neg\\(%gp_rel\\(model_call\\)\\)\\)\n"
       "\tjalr\t\\$25\n[^\n]*\n\tld\t\\$28,[0-9]+\\(\\$29\\)\n\tld\t\\$[0-9]+,%got_disp\\(value\\)")
run_cc(n64-model -S -O2 -target mips64-unknown-elf -march=mips3 -mabi=n64 -fPIC
       "${model_source}" -o "${OUTPUT}/n64-model.s")
file(READ "${OUTPUT}/n64-model.s" assembly)
expect(n64-model "${assembly}" "\tjalr\t\\$25\n[^\n]*\n\tld\t\\$[0-9]+,%got_disp\\(value\\)"
       "\tld\t\\$28,[0-9]+\\(\\$29\\)\n\tjr\t\\$31\n")

# expect_error(LABEL PATTERN SOURCE-TEXT CC-OPTION...)
function(expect_error label pattern text)
    set(source "${OUTPUT}/${label}.x")
    file(WRITE "${source}" "${text}")
    execute_process(COMMAND "${CC}" -S ${ARGN} "${source}" -o "${OUTPUT}/${label}.s"
                    RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: expected '${pattern}'\n${stdout}${stderr}")
    endif()
endfunction()
set(plain "u32 g;\nglobal u32 read(u32 x) { return x + g; }\n")
expect_error(no-abicalls "MIPS position-independent code needs -mabicalls" "${plain}"
             -target mips-unknown-elf -fPIC -mno-abicalls)
expect_error(eabi "-mabicalls code is not defined for the 'eabi32' ABI" "${plain}"
             -target mips-unknown-elf -march=mips32 -mabi=eabi -fPIC)
expect_error(data-base "data_base 'mips-gp' uses \\$gp, which holds the GOT pointer" "${plain}"
             -target mips-unknown-elf -fPIC -mdata-bases=mips-gp)
expect_error(keep32 "keep32.x:2:8: error: the ABI preserves \\$gp, which -mabicalls code with 32-bit addresses sets up at entry"
             "${plain}" --model=${MODEL} -target mips-unknown-elf -mabi=gp_keep32 -fPIC)
expect_error(global-dynamic "TLS model 'global-dynamic' requires a hidden resolver call"
             "[[thread_local, tls_model(\"global-dynamic\")]] u32 tls;\nglobal u32 read() { return tls; }\n"
             -target mips-unknown-elf -fPIC)
expect_error(naked "naked.x:[0-9]+:[0-9]+: error: a naked function cannot reach symbols through the GOT"
             "u32 g;\n[[naked, clobber(\"t0\")]]\nglobal u32 bare(in u32 base \"a0\") -> \"v0\" {\n    register u32 result \"v0\";\n    register void *link \"ra\";\n    result = base + g;\n    $::_jr(link);\n}\n"
             -target mips-unknown-elf -fPIC)
set(wide_call "u32 sum(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g, u32 h, u32 i, u32 j, u32 k, u32 l, u32 m, u32 n);\n")
expect_error(t9-call "the call passes a value in \\$t9, which an -mabicalls call takes"
             "${wide_call}global u32 call() { return sum(1u32, 2u32, 3u32, 4u32, 5u32, 6u32, 7u32, 8u32, 9u32, 10u32, 11u32, 12u32, 13u32, 14u32); }\n"
             -target mips-unknown-elf -mabi=x32 -fPIC)
expect_error(t9-function "the function takes a value in \\$t9, which -mabicalls callers set"
             "global u32 sum(u32 a, u32 b, u32 c, u32 d, u32 e, u32 f, u32 g, u32 h, u32 i, u32 j, u32 k, u32 l, u32 m, u32 n) { return a + n; }\n"
             -target mips-unknown-elf -mabi=x32 -fPIC)
