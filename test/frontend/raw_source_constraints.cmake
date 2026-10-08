# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC OUTPUT MODE MODEL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
set(flags)
if(MODE STREQUAL custom)
    list(APPEND flags "--model=${MODEL}" -mabi=odd_abi)
elseif(MODE MATCHES "^mips")
    list(APPEND flags -target "${MODE}-unknown-linux-gnu")
endif()
function(check name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags} ${ARGN}
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(expected STREQUAL pass)
            if(NOT status EQUAL 0)
                message(FATAL_ERROR "${name}/${level}: unexpected rejection\n${out}\n${err}")
            endif()
        elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):")
            message(FATAL_ERROR "${name}/${level}: missing located '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()
function(body_case name expected body)
    set(unused_expected "${expected}")
    if(ARGC GREATER 3)
        set(unused_expected "${ARGV3}")
    endif()
    foreach(role helper macro expander)
        if(role STREQUAL helper)
            set(definition "static $::meta::tokens helper(in $::meta::tokens input) { ${body} return input; }
                [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }")
            set(invocation "apply!(1u32)")
        elseif(role STREQUAL macro)
            set(definition "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { ${body} return input; }")
            set(invocation "apply!(1u32)")
        else()
            set(definition "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
                ${body} return $::quote { 1u32 }; }
                syntax Apply : expression { prefix \"apply\"; match \"(\" \")\"; expand expand; }
                syntax Apply;")
            set(invocation "apply()")
        endif()
        check(${name}_${role}_unused "${unused_expected}" "${definition} global u32 entry() { return 1u32; }")
        check(${name}_${role}_invoked "${expected}" "${definition} global u32 entry() { return ${invocation}; }")
    endforeach()
endfunction()
body_case(unknown "not registered for the selected target" "if (0u32) $::_not_a_registered_instruction();")
if(MODE MATCHES "^mips")
    body_case(wrong_target "not registered for the selected target" "if (0u32) $::_ret();")
else()
    body_case(output_literal "no typed form" "if (0u32) $::_movabs(1u64, $::patch(1u64));")
    body_case(output_width "no typed form" "if (0u32) $::_movabs($::reg::eax, $::patch(1u64));")
    body_case(output_class "no typed form" "if (0u32) $::_movabs($::reg::xmm0, $::patch(1u64));")
    body_case(output_unknown "no typed form" "if (0u32) $::_movabs($::reg::absent, $::patch(1u64));")
    body_case(output_protected "no typed form" "if (0u32) $::_movabs($::reg::rsp, $::patch(1u64));")
    body_case(output_const "no typed form" "const u64 value = 0u64; if (0u32) $::_movabs(value, $::patch(1u64));")
    body_case(output_local_width "no typed form" "u32 value; if (0u32) $::_movabs(value, $::patch(1u64));")
    body_case(output_binding "no typed form" "register u64 value \"eax\"; if (0u32) $::_movabs(value, $::patch(1u64));")
    body_case(arity "no form accepting" "if (0u32) $::_movabs($::reg::r10);")
    body_case(immediate_range "no typed form" "if (0u32) $::_add($::reg::r10, 0x80000000u64);")
    body_case(memory_width "no typed form" "u32 *p; if (0u32) $::_mov(*p, $::reg::r10);")
    body_case(memory_const "no typed form" "const u64 *p; if (0u32) $::_mov(*p, $::reg::r10);")
    body_case(memory_atomic "no typed form" "[[atomic]] u64 *p; if (0u32) $::_mov(*p, $::reg::r10);")
    body_case(address_scale "no encodable target scale" "typedef u64 V [[ext_vector_type(2)]];
        register V *base \"rdi\"; register uptr index \"rsi\";
        if (0u32) $::_prefetcht0(base[index]);")
    body_case(address_large_positive "displacement does not fit" "u64 *base;
        if (0u32) $::_prefetcht0(base[0xffffffffffffffffu64]);")
    body_case(address_wide "displacement does not fit" "u64 *base;
        if (0u32) $::_prefetcht0(base[0x10000000000000000u128]);")
    body_case(address_negative "displacement does not fit" "u64 *base;
        if (0u32) $::_prefetcht0(base[-268435457i64]);")
    body_case(address_narrow_index "encodable 64-bit integer register" "u64 *base; u32 index;
        if (0u32) $::_prefetcht0(base[index]);")
    body_case(address_index_class "encodable 64-bit integer register" "u64 *base;
        register u64 index \"xmm0\"; if (0u32) $::_prefetcht0(base[index]);")
    body_case(address_index_reserved "encodable 64-bit integer register" "u64 *base;
        register u64 index \"rsp\"; if (0u32) $::_prefetcht0(base[index]);")
    body_case(address_base_class "encodable address register" "register u64 *base \"xmm0\";
        if (0u32) $::_prefetcht0(*base);")
    body_case(address_base_expression "base must name a typed pointer object" "u64 *base;
        if (0u32) $::_prefetcht0(*(base + 1u32));")
    body_case(address_unsigned_negation "displacement does not fit" "u64 *base;
        if (0u32) $::_prefetcht0(base[-1u64]);")
    body_case(address_context_scale "no encodable target scale" "typedef u64 V [[ext_vector_type($::meta::len($::quote { a b }))]];
        register V *base \"rdi\"; register uptr index \"rsi\";
        if (0u32) $::_prefetcht0(base[index]);" pass)
    # An unused definition defers its genuinely unknown scale; invoking it
    # must diagnose even though the memory operation is not executed.
    check(context_scale_unused pass [=[
        static $::meta::tokens helper(in $::meta::tokens input) {
            typedef u64 V [[ext_vector_type($::meta::len($::quote { a b }))]];
            V *base; uptr index; if (0u32) $::_prefetcht0(base[index]); return input;
        }
    ]=])
    body_case(address_context_base "encodable address register" "typedef u64 V [[ext_vector_type($::meta::len($::quote { a b }))]];
        register V *base \"xmm0\"; uptr index;
        if (0u32) $::_prefetcht0(base[index]);")
    body_case(address_valid pass "if (0u32) { u64 *base; register uptr index \"r12\";
            $::_prefetcht0(base[index]); $::_prefetcht0(base[-1i8]);
            $::_prefetcht0(base[-268435456i64]); $::_prefetcht0(base[268435455u64]);
            $::_prefetcht0(base[(u64)$::meta::len($::quote { a b })]); }")
    body_case(valid pass "u64 value; u64 *p; if (0u32) {
        $::_movabs(value, ($::patch((u64)1u128)));
        $::_add($::reg::r10, -1i8); $::_mov(*p, $::reg::r10); $::_ret(); }")
    body_case(context_independent_output "no typed form" "if (0u32)
        $::_movabs(1u64, $::patch((u64)$::meta::len($::quote { a b })));")
    body_case(context_immediate pass "if (0u32) $::_add($::reg::r10, (u64)$::meta::len($::quote { a b }));")
    body_case(parenthesized_patch "accepting exactly u128" "if (0u32) $::_movabs($::reg::r10, ($::patch(1u128)));")
    check(feature_off "requires feature 'popcnt'" [=[
        static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) $::_popcnt($::reg::r10, $::reg::r11); return input;
        }
    ]=] -mno-popcnt)
    check(feature_on pass [=[
        static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) $::_popcnt($::reg::r10, $::reg::r11); return input;
        }
    ]=] -mpopcnt)
    check(required "no typed form" "$::static_assert(1u32 || (u32)$::_movabs(1u64, 1u64), \"unselected\");")
    check(unevaluated "no typed form" "$::static_assert(sizeof((u32)$::_movabs(1u64, 1u64)) == sizeof(u32), \"unevaluated\");")
    check(generic "no typed form" [=[
        static u32 helper<T>() { T value; if (0u32) $::_movabs(value, $::patch(1u64)); return 1u32; }
        [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
            helper<u32>(); return input;
        }
        global u32 entry() { return apply!(1u32); }
    ]=])
    foreach(form structured projected discarded)
        set(value "$::syntax::node(input, \"value\")")
        if(form STREQUAL projected)
            set(value "$::meta::tokens(${value})")
        endif()
        set(replacement "$::quote { $::unquote(${value}) }")
        set(expected "no encodable target scale")
        if(form STREQUAL discarded)
            set(replacement "$::quote {}")
            set(expected pass)
        endif()
        check(copied_address_${form} "${expected}" "
            [[syntax_expander]] static $::meta::tokens emit(in $::meta::syntax_match input) { return ${replacement}; }
            syntax Emit : item { prefix \"copy_address\"; match \"{\" value:function_def \"}\"; expand emit; }
            syntax Emit;
            copy_address { static $::meta::tokens helper(in $::meta::tokens input) {
                typedef u64 V [[ext_vector_type(2)]];
                register V *base \"rdi\"; register uptr index \"rsi\";
                if (0u32) $::_prefetcht0(base[index]); return input;
            } }
            global u32 entry() { return 1u32; }")
    endforeach()
endif()
set(capture [=[
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote { 1u32 }; }
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "value")) };
}
syntax Drop : expression { prefix "drop"; match "(" value:expr ")"; expand drop; }
syntax Keep : expression { prefix "keep"; match "(" value:expr ")"; expand keep; }
syntax Drop, Keep;
]=])
check(discard pass "${capture} global u32 entry() { return drop($::_not_registered()); }")
check(surviving "not registered for the selected target" "${capture}
    static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) keep($::_not_registered()); return input; }")
