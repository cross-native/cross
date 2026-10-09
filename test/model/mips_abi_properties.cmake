# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# User MIPS ABIs get object-level facts from the same model properties and
# address width as the shipped entries, and their collisions are diagnosed.

foreach(required CC MODEL OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

find_program(LLVM_READOBJ NAMES llvm-readobj REQUIRED)
find_program(LLVM_READELF NAMES llvm-readelf REQUIRED)
file(MAKE_DIRECTORY "${OUTPUT}")

set(source "${OUTPUT}/sum.x")
file(WRITE "${source}" "global u64 user_sum(in u64 a, in u64 b) {
    return a + b;
}
")

function(run_cc label)
    execute_process(
        COMMAND "${CC}" "--model=${MODEL}" ${ARGN}
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${label} failed\n${stdout}\n${stderr}")
    endif()
endfunction()

function(read_header object variable)
    execute_process(
        COMMAND "${LLVM_READOBJ}" --file-headers "${object}"
        RESULT_VARIABLE status OUTPUT_VARIABLE header ERROR_VARIABLE error)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "cannot inspect '${object}'\n${error}")
    endif()
    set(${variable} "${header}" PARENT_SCOPE)
endfunction()

# elf_abi_tag = "eabi32" on a user entry tags the object as EABI32.
foreach(abi user_eabi user_plain)
    run_cc(${abi}-object -c -O2 -target mips-unknown-elf -march=r3000
           -mabi=${abi} "${source}" -o "${OUTPUT}/${abi}.o")
    run_cc(${abi}-assembly -S -O2 -target mips-unknown-elf -march=r3000
           -mabi=${abi} "${source}" -o "${OUTPUT}/${abi}.s")
    read_header("${OUTPUT}/${abi}.o" header)
    file(READ "${OUTPUT}/${abi}.s" assembly)
    if(abi STREQUAL user_eabi)
        if(NOT header MATCHES "EF_MIPS_ABI_EABI32" OR
           NOT assembly MATCHES "\\.section \\.mdebug\\.eabi32")
            message(FATAL_ERROR
                "user_eabi objects lack the EABI32 tag\n${header}\n${assembly}")
        endif()
    elseif(NOT header MATCHES "EF_MIPS_ABI_O32" OR
           header MATCHES "EF_MIPS_ABI_EABI" OR
           assembly MATCHES "\\.mdebug\\.eabi32")
        message(FATAL_ERROR
            "user_plain objects lost the default tag\n${header}\n${assembly}")
    endif()
endforeach()

# A user ABI with 64-bit addresses produces ELF64 objects: numbered GPRs and
# the 64-bit FPU register model follow from its address width, even on a CPU
# whose features default to FP32.
set(symbolic_gpr
    "[$](zero|at|v[01]|a[0-3]|t[0-9]|s[0-7]|k[01]|gp|sp|fp|ra)[^a-z0-9_]")
set(wide_sum "[\t ]daddu[\t ][$]14,[$]1[23],[$]1[23]\n")
run_cc(user-wide-assembly -S -O2 -target mips64-unknown-elf -march=vr4300
       -mabi=user_wide "${source}" -o "${OUTPUT}/user_wide.s")
file(READ "${OUTPUT}/user_wide.s" wide_text)
if(NOT wide_text MATCHES "${wide_sum}" OR wide_text MATCHES "${symbolic_gpr}")
    message(FATAL_ERROR
        "user_wide assembly does not use numbered t4/t5/t6\n${wide_text}")
endif()
run_cc(user-wide-object -c -O2 -target mips64-unknown-elf -march=vr4300
       -mabi=user_wide "${source}" -o "${OUTPUT}/user_wide.o")
read_header("${OUTPUT}/user_wide.o" wide_header)
execute_process(
    COMMAND "${LLVM_READELF}" -A "${OUTPUT}/user_wide.o"
    RESULT_VARIABLE status OUTPUT_VARIABLE wide_flags ERROR_VARIABLE error)
if(NOT wide_header MATCHES "Class: 64-bit" OR NOT status EQUAL 0 OR
   NOT wide_flags MATCHES "CPR1 size: 64")
    message(FATAL_ERROR
        "user_wide object is not ELF64 with a 64-bit FPU\n${wide_header}\n${wide_flags}\n${error}")
endif()

# The same user ABI selected for one function of a Cross compilation.
set(function_source "${OUTPUT}/function.x")
file(WRITE "${function_source}" "[[abi(\"user_wide\")]]
global u64 user_wide_sum(in u64 a, in u64 b) {
    return a + b;
}
")
run_cc(user-wide-function -S -O2 -mprofile=mips64-n64 "${function_source}"
       -o "${OUTPUT}/function.s")
file(READ "${OUTPUT}/function.s" function_text)
if(NOT function_text MATCHES "${wide_sum}")
    message(FATAL_ERROR
        "[[abi(\"user_wide\")]] does not use t4/t5/t6\n${function_text}")
endif()

# A minimal MIPS entry with the given name, address width, and properties.
function(entry_text variable name address_bits properties)
    set(${variable} "abi \"${name}\" {
    architecture = \"mips\";
    address_bits = ${address_bits};
    stack_alignment = 8;
    stack_slot_bytes = 8;
    ${properties}
    bank \"integer\" {
        class = \"integer\";
        register_bits = 64;
        arguments = [\"a0\"];
        results = [\"v0\"];
    }
    rule \"argument-memory\" {
        match = [\"any\"];
        action = \"stack\";
        applies_to = [\"arguments\"];
    }
}
" PARENT_SCOPE)
endfunction()

# A name may repeat in another address model. Here `cross` gains a 16-bit
# entry, and each triple still resolves the entry of its own address model.
set(shared_model "${OUTPUT}/shared-name.y")
entry_text(shared_text narrow_cross 16 "aliases = [\"cross\"];")
file(WRITE "${shared_model}" "${shared_text}")
foreach(triple mips-unknown-elf mips64-unknown-elf)
    execute_process(
        COMMAND "${CC}" "--model=${shared_model}" -S -O2 -target ${triple}
                -march=vr4300 -mabi=cross "${source}"
                -o "${OUTPUT}/shared-${triple}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR
            "-mabi=cross with a shared name failed for ${triple}\n${stdout}\n${stderr}")
    endif()
endforeach()

# Invalid user entries, one fault per model file.
function(expect_model_error label expected name address_bits properties)
    entry_text(body ${name} ${address_bits} "${properties}")
    set(model "${OUTPUT}/${label}.y")
    file(WRITE "${model}" "${body}")
    execute_process(
        COMMAND "${CC}" "--model=${model}" -S -O2 -target mips-unknown-elf
                -march=r3000 "${source}" -o "${OUTPUT}/${label}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE stdout ERROR_VARIABLE stderr)
    if(status EQUAL 0 OR NOT stderr MATCHES "${expected}")
        message(FATAL_ERROR
            "${label} did not report '${expected}'\n${stdout}\n${stderr}")
    endif()
endfunction()

# Tags are checked at load time, at the line of the property.
expect_model_error(unknown-tag
    "unknown-tag[.]y:6: error: ABI model 'bad_tag' requests ELF ABI tag 'o64', which mips does not define"
    bad_tag 32 "elf_abi_tag = \"o64\";")
expect_model_error(wide-eabi
    "wide-eabi[.]y:6: error: ABI model 'wide_eabi' requests ELF ABI tag 'eabi32', which requires 32-bit addresses"
    wide_eabi 64 "elf_abi_tag = \"eabi32\";")
expect_model_error(empty-tag "ABI elf_abi_tag must not be empty"
    empty_tag 32 "elf_abi_tag = \"\";")
expect_model_error(second-private
    "ABI model 'second_private' duplicates the private convention for 32-bit carriers for mips with 32-bit addresses"
    second_private 32 "private_carrier_bits = 32;")
expect_model_error(zero-private "ABI private_carrier_bits must be nonzero"
    zero_private 32 "private_carrier_bits = 0;")
expect_model_error(same-model-name
    "duplicate ABI model name or alias 'cross' for mips with 64-bit addresses"
    second_cross 64 "aliases = [\"cross\"];")
