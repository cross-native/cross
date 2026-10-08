# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC SOURCE POSITIVE_SOURCE OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a path")
    endif()
endforeach()

function(reject_label_expression case expected source)
    file(WRITE "${OUTPUT}.${case}.x" "${source}")
    execute_process(COMMAND "${CC}" -S ${ARGN} "${OUTPUT}.${case}.x" -o "${OUTPUT}.${case}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
       NOT err MATCHES "${case}.x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "${case}: missing located label-expression diagnostic\n${out}\n${err}")
    endif()
endfunction()
set(label_prefix [=[
static void owner() { first: ; second: ; }
[[generic(label Address), noinline]] static label identity() { return Address; }
static label choose(in bool select, in label first, in label second) { return select ? first : second; }
]=])
foreach(level O0 O2)
    reject_label_expression(eval_only_label_owner_${level} "cannot take a code label address of a translation-only function"
        "${label_prefix} [[eval_only]] static label unavailable() { return point; point: ; } global label saved = unavailable();"
        -${level} -fno-eval-calls)
    reject_label_expression(eval_only_label_generic_${level} "cannot take a code label address of a translation-only function"
        "${label_prefix} [[eval_only]] static void unavailable() { point: ; } global label entry() { return identity::<unavailable::point>(); }"
        -${level} -fno-eval-calls)
    reject_label_expression(eval_only_instance_label_${level} "cannot take a code label address of a translation-only function"
        "${label_prefix} [[generic(T), eval_only]] static label unavailable() { return point; point: ; } global label saved = unavailable::<u32>();"
        -${level} -fno-eval-calls)
    reject_label_expression(eval_only_untaken_label_${level} "cannot take a code label address of a translation-only function"
        "${label_prefix} [[eval_only]] static label unavailable(in bool select) { if (select) return point; return owner::first; point: ; } global label saved = unavailable(0);"
        -${level} -fno-eval-calls)
    reject_label_expression(eval_only_unused_label_${level} "cannot take a code label address of a translation-only function"
        "${label_prefix} [[eval_only]] static label unavailable() { return owner::first; label values[1] = {point}; point: ; } global u32 entry() { return 1u32; }"
        -${level} -fno-eval-calls)
    reject_label_expression(eval_only_untaken_argument_${level} "cannot take a code label address of a translation-only function"
        "${label_prefix} [[eval_only]] static void unavailable() { point: ; } global label entry() { return identity::<(1u32 ? owner::first : unavailable::point)>(); }"
        -${level} -fno-eval-calls)
    reject_label_expression(static_parameter_label_${level} "an ordinary value name cannot select a same-spelled label"
        "${label_prefix} [[generic(T)]] static label use(in label point) { static label saved = identity::<point>(); point: return saved; } global label entry() { return use::<u32>(owner::first); }"
        -${level} -fno-eval-calls)
    reject_label_expression(static_inline_label_${level} "taking a label address conflicts with always_inline"
        "${label_prefix} [[generic(T), always_inline]] static label use() { static label saved = point; point: return saved; } global label entry() { return use::<u32>(); }"
        -${level} -fno-eval-calls)
    reject_label_expression(missing_untaken_${level} "unresolved name 'owner::missing'"
        "${label_prefix} global label entry() { return identity::<(1u32 ? owner::first : owner::missing)>(); }" -${level} -fno-eval-calls)
    reject_label_expression(invalid_untaken_${level} "scalar operator requires numeric operands"
        "${label_prefix} global label entry() { return identity::<(1u32 ? owner::first : owner::second + 1uptr)>(); }" -${level} -fno-eval-calls)
    reject_label_expression(mixed_conditional_${level} "conditional label operands must both have label type"
        "${label_prefix} global label entry() { return identity::<(1u32 ? owner::first : 0uptr)>(); }" -${level} -fno-eval-calls)
    reject_label_expression(runtime_condition_${level} "not a translation-time value"
        "${label_prefix} global label entry(in bool select) { return identity::<(select ? owner::first : owner::second)>(); }" -${level} -fno-eval-calls)
    reject_label_expression(runtime_helper_${level} "call to runtime-only function"
        "${label_prefix} [[runtime_only]] static label select() { return owner::first; } global label entry() { return identity::<select()>(); }" -${level} -fno-eval-calls)
    reject_label_expression(missing_instance_label_${level} "generic function 'instance' has no label 'absent'"
        "${label_prefix} [[generic(T)]] static void instance() { present: ; } global label entry() { return instance::<u32>::absent; }"
        -${level} -fno-eval-calls)
    reject_label_expression(runtime_taken_${level} "runtime is invalid where a translation-time value is required"
        "${label_prefix} global label entry() { return identity::<(0u32 ? owner::first : $::runtime(owner::second))>(); }" -${level} -fno-eval-calls)
    # An unselected $::runtime only has to be well formed.
    file(WRITE "${OUTPUT}.runtime_untaken_${level}.x"
        "${label_prefix} global label entry() { return identity::<(1u32 ? owner::first : $::runtime(owner::second))>(); }")
    execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls "${OUTPUT}.runtime_untaken_${level}.x"
                            -o "${OUTPUT}.runtime_untaken_${level}.s"
        RESULT_VARIABLE status ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "runtime_untaken_${level}: unselected $::runtime was rejected\n${err}")
    endif()
    reject_label_expression(distinct_addresses_${level} "comparison of distinct code labels depends on emitted addresses"
        "${label_prefix} $::static_assert(owner::first != owner::second, \"cannot assume separate addresses\");" -${level} -fno-eval-calls)
    reject_label_expression(inspect_address_${level} "emitted code addresses cannot be inspected"
        "${label_prefix} $::static_assert((uptr)choose((bool)1u32, owner::first, owner::second), \"not emitted bits\");" -${level} -fno-eval-calls)
    reject_label_expression(numeric_symbolic_comparison_${level} "comparison of distinct code labels depends on emitted addresses"
        "${label_prefix} $::static_assert(owner::first != (label)0uptr, \"not emitted bits\");" -${level} -fno-eval-calls)
    reject_label_expression(label_non_uptr_cast_${level} "code labels permit only same-type or explicit uptr conversions"
        "${label_prefix} $::static_assert((u64)(label)0uptr == 0u64, \"uptr is explicit\");" -${level} -fno-eval-calls)
    reject_label_expression(inline_owner_${level} "taking a label address conflicts with always_inline"
        "${label_prefix} [[always_inline]] static void inline_owner() { point: ; } global label entry() { return identity::<choose((bool)1u32, inline_owner::point, owner::second)>(); }" -${level} -fno-eval-calls)
    reject_label_expression(recursion_budget_${level} "translation-time instruction budget exceeded"
        "${label_prefix} static label recurse(in u32 depth) { return depth ? recurse(depth - 1u32) : owner::first; } global label entry() { return identity::<recurse(100u32)>(); }"
        -${level} -fno-eval-calls -feval-step-limit=64)
    reject_label_expression(label_byte_read_${level} "opaque code label representation cannot be inspected"
        "${label_prefix} static u8 inspect() { label value = owner::first; return *((u8 *)&value); } $::static_assert(inspect() == 0u8, \"opaque label\");"
        -${level} -fno-eval-calls)
    reject_label_expression(label_scalar_read_${level} "opaque code label representation cannot be inspected"
        "${label_prefix} static uptr inspect() { label value = owner::first; return *((uptr *)&value); } $::static_assert(inspect() == 0uptr, \"opaque label\");"
        -${level} -fno-eval-calls)
    reject_label_expression(label_union_read_${level} "opaque code label representation cannot be inspected"
        "${label_prefix} union Box { label value; uptr bits; }; static uptr inspect() { union Box value = {.value = owner::first}; union Box copy = value; return copy.bits; } $::static_assert(inspect() == 0uptr, \"opaque union label\");"
        -${level} -fno-eval-calls)
    reject_label_expression(label_pointer_read_${level} "violates aggregate effective type"
        "${label_prefix} static label inspect() { label value = owner::first; u8 *invalid = *((u8 **)&value); return value; } global label entry() { return identity::<inspect()>(); }"
        -${level} -fno-eval-calls)
    reject_label_expression(label_partial_write_${level} "read of unassigned buffer byte"
        "${label_prefix} static label damaged() { label value = owner::first; *((u8 *)&value) = 0u8; return value; } global label entry() { return identity::<damaged()>(); }"
        -${level} -fno-eval-calls)
    reject_label_expression(label_uninitialized_${level} "read of unassigned buffer byte"
        "${label_prefix} static label uninitialized() { label values[2]; values[0] = owner::first; return values[1]; } global label entry() { return identity::<uninitialized()>(); }"
        -${level} -fno-eval-calls)
    reject_label_expression(label_lifetime_${level} "outside its lifetime"
        "${label_prefix} static label *expired() { label value = owner::first; return &value; } static label use() { return *expired(); } global label entry() { return identity::<use()>(); }"
        -${level} -fno-eval-calls)
    reject_label_expression(label_inline_image_${level} "taking a label address conflicts with always_inline"
        "${label_prefix} [[always_inline]] static void inline_owner() { point: ; } struct Box { label value; }; static struct Box make() { struct Box value = {inline_owner::point}; return value; } global struct Box image = make();"
        -${level} -fno-eval-calls)
endforeach()

# Numeric labels use pointer-typed LLVM constants, just like label relocations.
# Keep this fixture free of externally named label definitions, which the debug
# serializer intentionally cannot represent.
if(LLVM_TEXT)
    set(numeric_source "${OUTPUT}.numeric.x")
    file(WRITE "${numeric_source}" [=[
static label numeric(in uptr bits) { return (label)bits; }
static label stored = numeric(0x1234uptr);
static label empty = numeric(0uptr);
global uptr numeric_entry(in uptr input) {
    return (uptr)numeric(input) + (uptr)stored + (uptr)empty;
}
]=])
    find_program(LLVM_AS NAMES llvm-as)
    foreach(level O0 O2)
        set(ir "${OUTPUT}.numeric.${level}.ll")
        execute_process(COMMAND "${CC}" -emit-llvm -fno-eval-calls -${level}
            "${numeric_source}" -o "${ir}"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
        if(NOT status EQUAL 0)
            message(FATAL_ERROR "numeric label LLVM serialization failed\n${out}\n${err}")
        endif()
        if(LLVM_AS)
            execute_process(COMMAND "${LLVM_AS}" "${ir}" -o "${ir}.bc"
                RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
            if(NOT status EQUAL 0)
                message(FATAL_ERROR "numeric label LLVM verification failed\n${out}\n${err}")
            endif()
        else()
            message(STATUS "skipping optional numeric label LLVM verification: llvm-as unavailable")
        endif()
    endforeach()
endif()

foreach(case
        "mips-unknown-elf,vr4300,o32,O0"
        "mipsel-unknown-elf,vr4300,o32,O3"
        "mips64-unknown-elf,mips64,n64,O0"
        "mips64el-unknown-elf,mips64,n64,O3")
    string(REPLACE "," ";" fields "${case}")
    list(GET fields 0 triple)
    list(GET fields 1 march)
    list(GET fields 2 abi)
    list(GET fields 3 level)
    execute_process(
        COMMAND "${CC}" -target "${triple}" "-march=${march}"
                "-mabi=${abi}" -DTEST_MIPS -${level} -c
                "${POSITIVE_SOURCE}" -o "${OUTPUT}.${triple}.o"
        RESULT_VARIABLE mips_status
        OUTPUT_VARIABLE mips_stdout
        ERROR_VARIABLE mips_stderr
    )
    if(NOT mips_status EQUAL 0)
        message(FATAL_ERROR "label generic failed on ${triple} ${level}\n${mips_stdout}\n${mips_stderr}")
    endif()
endforeach()

execute_process(
    COMMAND "${CC}" -c "${SOURCE}" -o "${OUTPUT}"
    RESULT_VARIABLE status
    OUTPUT_VARIABLE stdout
    ERROR_VARIABLE stderr
)
if(status EQUAL 0 OR
   NOT stderr MATCHES "generic label argument requires a visible label in a concrete function")
    message(FATAL_ERROR "missing generic label was not diagnosed\n${stdout}\n${stderr}")
endif()

foreach(level O0 O3)
    execute_process(
        COMMAND "${CC}" -target x86_64-unknown-linux-gnu
                -mabi=sysv_abi -${level} -c "${POSITIVE_SOURCE}"
                -o "${OUTPUT}.${level}.sysv.o"
        RESULT_VARIABLE sysv_status
        OUTPUT_VARIABLE sysv_stdout
        ERROR_VARIABLE sysv_stderr
    )
    if(NOT sysv_status EQUAL 0)
        message(FATAL_ERROR "custom-register label generic failed on SysV ${level}\n${sysv_stdout}\n${sysv_stderr}")
    endif()
endforeach()
