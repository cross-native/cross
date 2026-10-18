# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The MIPS errata workarounds: each is on for the CPU entries that need it,
# and their other spellings, off otherwise, and -mfix-NAME and -mno-fix-NAME
# select it. -mfix-r4000 keeps each multiply's MFLO next to it; divides are
# always next to their MFLO or MFHI, which is the R4400 workaround as well;
# -mfix-r10000 retries LL/SC loops with BEQL; -mfix-loongson3-llsc puts a
# SYNC before every attempt's LL and at the exit of a compare-exchange; and
# -mfix-r5900 leaves a NOP in the delay slot of every branch that ends a
# loop of six instructions or fewer in LOOPS_SOURCE.

foreach(required CC SOURCE LOOPS_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()
get_filename_component(output_directory "${OUTPUT}" DIRECTORY)
file(MAKE_DIRECTORY "${output_directory}")

set(checks 0)
macro(count_check)
    math(EXPR checks "${checks} + 1")
endmacro()

# The assembly of SOURCE, or of the file that `input` names, for OPTIONS,
# a space-separated list.
function(compile label options result)
    if(NOT DEFINED input)
        set(input "${SOURCE}")
    endif()
    separate_arguments(split UNIX_COMMAND "${options}")
    execute_process(COMMAND "${CC}" -target mips-unknown-elf -mabi=o32 ${split}
                            -S "${input}" -o "${OUTPUT}.${label}.s"
                    RESULT_VARIABLE status ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label}: cc ${options} failed\n${error}")
    endif()
    file(READ "${OUTPUT}.${label}.s" text)
    set(${result} "${text}" PARENT_SCOPE)
endfunction()

# The instruction lines of function NAME, each "mnemonic operands".
function(body text name result)
    string(REPLACE ";" "," text "${text}")
    string(REPLACE "\n" ";" lines "${text}")
    set(inside FALSE)
    set(found "")
    foreach(line IN LISTS lines)
        if(line STREQUAL "${name}:")
            set(inside TRUE)
        elseif(inside AND line MATCHES "^\\.end ")
            break()
        elseif(inside AND line MATCHES "^\t([a-z][a-z0-9.]*)(\t(.*))?$")
            list(APPEND found "${CMAKE_MATCH_1} ${CMAKE_MATCH_3}")
        elseif(inside AND line MATCHES "^([A-Za-z0-9_.]+):$")
            list(APPEND found "${CMAKE_MATCH_1}:")
        endif()
    endforeach()
    set(${result} "${found}" PARENT_SCOPE)
endfunction()

# Whether each instruction whose mnemonic matches FIRST is followed by one
# whose mnemonic matches NEXT.
function(always_followed lines first next result)
    set(previous "")
    set(value TRUE)
    set(seen FALSE)
    foreach(line IN LISTS lines)
        if(previous MATCHES "^(${first}) ")
            set(seen TRUE)
            if(NOT line MATCHES "^(${next}) ")
                set(value FALSE)
            endif()
        endif()
        set(previous "${line}")
    endforeach()
    if(NOT seen)
        set(value FALSE)
    endif()
    set(${result} ${value} PARENT_SCOPE)
endfunction()

# The R5900 short loops of an assembly file: conditional branches back to a
# label at most four instruction words before them with no branch or jump
# between, and those of them with an instruction other than NOP in their
# delay slot.
function(short_loops text loops filled)
    string(REPLACE ";" "," text "${text}")
    string(REPLACE "\n" ";" lines "${text}")
    set(count 0)
    set(last_transfer -1)
    set(pending "")
    set(found 0)
    set(bad 0)
    foreach(line IN LISTS lines)
        if(line MATCHES "^([A-Za-z0-9_.]+):$")
            set("label_${CMAKE_MATCH_1}" ${count})
            continue()
        endif()
        if(NOT line MATCHES "^\t([a-z][a-z0-9.]*)(\t(.*))?$")
            continue()
        endif()
        set(mnemonic "${CMAKE_MATCH_1}")
        set(operands "${CMAKE_MATCH_3}")
        if(NOT pending STREQUAL "")
            if(NOT mnemonic STREQUAL "nop")
                math(EXPR bad "${bad} + 1")
            endif()
            set(pending "")
        endif()
        if(mnemonic MATCHES "^(beq|bne|beqz|bnez|blez|bgtz|bltz|bgez|bc1t|bc1f)l?$")
            string(REGEX REPLACE ".*," "" target "${operands}")
            if(DEFINED "label_${target}")
                set(start "${label_${target}}")
                math(EXPR span "${count} - ${start}")
                if(last_transfer LESS start AND span LESS_EQUAL 4)
                    math(EXPR found "${found} + 1")
                    set(pending "${target}")
                endif()
            endif()
        endif()
        if((mnemonic MATCHES "^b" AND NOT mnemonic STREQUAL "break") OR
           mnemonic MATCHES "^j")
            set(last_transfer ${count})
        endif()
        set(words 1)
        if(mnemonic STREQUAL "li")
            string(REGEX REPLACE ".*," "" value "${operands}")
            math(EXPR low "${value} & 65535")
            if(NOT (value GREATER_EQUAL -32768 AND value LESS_EQUAL 65535) AND
               NOT low EQUAL 0)
                set(words 2)
            endif()
        endif()
        math(EXPR count "${count} + ${words}")
    endforeach()
    set(${loops} ${found} PARENT_SCOPE)
    set(${filled} ${bad} PARENT_SCOPE)
endfunction()

function(expect_feature label options feature value)
    separate_arguments(split UNIX_COMMAND "${options}")
    execute_process(COMMAND "${CC}" -target mips-unknown-elf ${split}
                            --print-options=target
                    RESULT_VARIABLE status OUTPUT_VARIABLE listing
                    ERROR_VARIABLE error)
    if(NOT status EQUAL 0 OR
       NOT listing MATCHES "\nm\\.${feature}  type: boolean  value: ${value} ")
        message(FATAL_ERROR "${label}: ${options} does not turn ${feature} ${value}\n${error}")
    endif()
endfunction()

function(expect_error label options pattern)
    separate_arguments(split UNIX_COMMAND "${options}")
    execute_process(COMMAND "${CC}" -target mips-unknown-elf -mabi=o32 ${split}
                            -S "${SOURCE}" -o "${OUTPUT}.${label}.s"
                    RESULT_VARIABLE status ERROR_VARIABLE error)
    if(status EQUAL 0 OR NOT error MATCHES "${pattern}")
        message(FATAL_ERROR "${label}: ${options} was not rejected with '${pattern}'\n${error}")
    endif()
endfunction()

# The CPUs that enable each fix, their spellings, and the fixes they do not.
foreach(case "r4000|fix-r4000" "r4k|fix-r4000" "4000|fix-r4000" "4k|fix-r4000"
             "r4400|fix-r4400" "4400|fix-r4400" "r10000|fix-r10000"
             "r10k|fix-r10000" "10000|fix-r10000" "10k|fix-r10000"
             "gs464|fix-loongson3-llsc" "loongson3a|fix-loongson3-llsc"
             "gs464e|fix-loongson3-llsc" "gs264e|fix-loongson3-llsc"
             "r5900|fix-r5900" "5900|fix-r5900")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 cpu)
    list(GET fields 1 fix)
    expect_feature(${cpu} "-march=${cpu}" ${fix} on)
    expect_feature(${cpu}-off "-march=${cpu} -mno-${fix}" ${fix} off)
    count_check()
endforeach()
foreach(case "r4400|fix-r4000" "vr4300|fix-r4000" "r4600|fix-r4000"
             "r12000|fix-r10000" "mips4|fix-r10000" "mips64r2|fix-loongson3-llsc"
             "octeon3|fix-loongson3-llsc" "vr4300|fix-r5900" "r4000|fix-r4400")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 cpu)
    list(GET fields 1 fix)
    expect_feature(${cpu} "-march=${cpu}" ${fix} off)
    expect_feature(${cpu}-on "-march=${cpu} -m${fix}" ${fix} on)
    count_check()
endforeach()
# Tuning for a CPU does not take on its errata.
expect_feature(tune "-march=vr4300 -mtune=r4000" fix-r4000 off)
count_check()

# R4000: MFLO follows each multiply at once, so no shift runs while one is
# in flight; without the fix the scheduler fills the multiply's latency.
compile(r4000 "-march=r4000 -O2" r4000)
body("${r4000}" errata_product lines)
always_followed("${lines}" "mult|multu" "mflo|mfhi" adjacent)
if(NOT adjacent)
    message(FATAL_ERROR "r4000: a multiply runs apart from its MFLO\n${lines}")
endif()
compile(r4000-off "-march=r4000 -mno-fix-r4000 -O2" r4000_off)
body("${r4000_off}" errata_product lines)
always_followed("${lines}" "mult|multu" "mflo|mfhi" adjacent)
if(adjacent)
    message(FATAL_ERROR "r4000-off: the multiply was not scheduled\n${lines}")
endif()
foreach(spelling r4k 4000 4k)
    compile(${spelling} "-march=${spelling} -O2" text)
    if(NOT text STREQUAL r4000)
        message(FATAL_ERROR "-march=${spelling} differs from -march=r4000")
    endif()
endforeach()
count_check()

# R4000 and R4400: a divide's MFLO or MFHI follows it, so neither a shift
# nor a delay slot follows the divide.
foreach(cpu r4000 r4400)
    compile(${cpu}-divide "-march=${cpu} -O2" text)
    body("${text}" errata_quotient lines)
    always_followed("${lines}" "div|divu|ddiv|ddivu" "mflo|mfhi" adjacent)
    if(NOT adjacent)
        message(FATAL_ERROR "${cpu}: a divide is not followed by its MFLO\n${lines}")
    endif()
endforeach()
count_check()

# R10000: the SC's retry branch is a branch-likely form.
foreach(case "r10000|-march=r10000|beql" "r10k|-march=r10k|beql"
             "r10000-off|-march=r10000 -mno-fix-r10000|beq"
             "mips4-fix|-march=mips4 -mfix-r10000|beql" "r12000|-march=r12000|beq")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 label)
    list(GET fields 1 options)
    list(GET fields 2 branch)
    compile(${label} "${options} -O2" text)
    foreach(name errata_bump errata_swap)
        body("${text}" ${name} lines)
        always_followed("${lines}" "sc" "${branch}" retried)
        if(NOT retried)
            message(FATAL_ERROR "${label}: ${name} does not retry with ${branch}\n${lines}")
        endif()
    endforeach()
endforeach()
expect_error(r10000-no-likely "-march=r10000 -mno-branch-likely -O2"
             "-mfix-r10000 requires branch-likely instructions")
expect_error(mips1-fix "-march=mips1 -mfix-r10000 -O2"
             "-mfix-r10000 requires branch-likely instructions")
count_check()

# Loongson 3: every attempt starts with the SYNC, and the compare-exchange
# exit reaches a SYNC before its store.
foreach(case "gs464|-march=gs464|ON" "gs464e|-march=gs464e|ON"
             "gs464-off|-march=gs464 -mno-fix-loongson3-llsc|OFF"
             "mips64r2-fix|-march=mips64r2 -mfix-loongson3-llsc|ON"
             "mips64r2|-march=mips64r2|OFF")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 label)
    list(GET fields 1 options)
    list(GET fields 2 fixed)
    compile(${label} "${options} -O2" text)
    foreach(name errata_bump errata_swap)
        body("${text}" ${name} lines)
        # The retry branch names the label of the attempt's first line.
        set(attempt "")
        set(previous "")
        foreach(line IN LISTS lines)
            if(previous MATCHES "^sc " AND line MATCHES "^beql? [^,]+,[^,]+,(.+)$")
                set(target "${CMAKE_MATCH_1}")
            endif()
            set(previous "${line}")
        endforeach()
        list(FIND lines "${target}:" at)
        math(EXPR after "${at} + 1")
        list(GET lines ${after} attempt)
        if(fixed AND NOT attempt MATCHES "^sync ")
            message(FATAL_ERROR "${label}: ${name}'s attempts do not start with SYNC\n${lines}")
        elseif(NOT fixed AND NOT attempt MATCHES "^ll ")
            message(FATAL_ERROR "${label}: ${name}'s attempts start with '${attempt}'\n${lines}")
        endif()
    endforeach()
    body("${text}" errata_swap lines)
    set(exit "")
    foreach(line IN LISTS lines)
        if(line MATCHES "^bne \\$t1,\\$t4,(.+)$")
            set(exit "${CMAKE_MATCH_1}")
        endif()
    endforeach()
    list(FIND lines "${exit}:" at)
    math(EXPR after "${at} + 1")
    list(GET lines ${after} first)
    if(fixed AND NOT first MATCHES "^sync ")
        message(FATAL_ERROR "${label}: the compare-exchange exit lacks a SYNC\n${lines}")
    elseif(NOT fixed AND first MATCHES "^sync ")
        message(FATAL_ERROR "${label}: the compare-exchange exit has a SYNC\n${lines}")
    endif()
endforeach()
count_check()

# R5900: no short loop has an instruction in its delay slot.
set(input "${LOOPS_SOURCE}")
foreach(case "r5900|-march=r5900|0" "5900|-march=5900|0"
             "vr4300-fix|-march=vr4300 -mfix-r5900|0"
             "r5900-off|-march=r5900 -mno-fix-r5900|1")
    string(REPLACE "|" ";" fields "${case}")
    list(GET fields 0 label)
    list(GET fields 1 options)
    list(GET fields 2 expect_filled)
    foreach(level O2 O3 Os Oz)
        compile(${label}-${level} "${options} -${level}" text)
        short_loops("${text}" loops filled)
        if(loops EQUAL 0)
            message(FATAL_ERROR "${label} -${level}: no short loop to check")
        endif()
        if(expect_filled EQUAL 0 AND NOT filled EQUAL 0)
            message(FATAL_ERROR "${label} -${level}: ${filled} of ${loops} short loops fill their delay slot")
        elseif(expect_filled EQUAL 1 AND filled EQUAL 0 AND level STREQUAL "O2")
            message(FATAL_ERROR "${label} -${level}: no short loop fills its delay slot")
        endif()
    endforeach()
endforeach()
count_check()
# The measuring passes leave the function as they found it: a function with
# an indirect result and a loop gets the same frame with and without the
# fix (the legacy frame path that -funwind-tables selects).
file(WRITE "${OUTPUT}.sret.x" [=[
struct wide { u32 words[8]; };
[[noinline]] global struct wide fill(in u32 seed, in u32 count) {
    struct wide result;
    for (u32 index = 0u32; index < count; index = index + 1u32) {
        result.words[index & 7u32] = seed;
    }
    return result;
}
]=])
set(input "${OUTPUT}.sret.x")
foreach(level O2 Os)
    compile(sret-${level} "-march=r5900 -${level} -funwind-tables" fixed)
    compile(sret-${level}-off "-march=r5900 -mno-fix-r5900 -${level} -funwind-tables" unfixed)
    string(REGEX MATCH "\\.frame\t[^\n]*" fixed_frame "${fixed}")
    string(REGEX MATCH "\\.frame\t[^\n]*" unfixed_frame "${unfixed}")
    if(NOT fixed_frame STREQUAL unfixed_frame)
        message(FATAL_ERROR "sret -${level}: '${fixed_frame}' with -mfix-r5900, '${unfixed_frame}' without")
    endif()
endforeach()
unset(input)
count_check()
# The R5900 has neither LL/SC, doubleword multiplies, nor (in the model) an
# FPU, so code that needs them is rejected.
expect_error(r5900-atomic "-march=r5900 -O2" "no runtime-free atomic operation for u32")
file(WRITE "${OUTPUT}.multiply.x"
     "global u64 product(in u64 a, in u64 b) { return a * b; }\n")
execute_process(COMMAND "${CC}" -target mips-unknown-elf -mabi=o32 -march=r5900 -O2
                        -S "${OUTPUT}.multiply.x" -o "${OUTPUT}.multiply.s"
                RESULT_VARIABLE status ERROR_VARIABLE error)
if(status EQUAL 0 OR NOT error MATCHES "needs instruction 'dmult', which CPU 'r5900' does not have")
    message(FATAL_ERROR "r5900: a doubleword multiply was not rejected\n${error}")
endif()
expect_feature(r5900-float "-march=r5900" soft-float on)
count_check()
message(STATUS "${checks} MIPS errata checks passed")
