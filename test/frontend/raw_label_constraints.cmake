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
elseif(NOT MODE STREQUAL native)
    message(FATAL_ERROR "raw label tests require an x86-64 profile")
endif()
function(check name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags}
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
set(target [=[
    [[naked]] static void target() { done: $::_ret(); }
]=])
foreach(role helper macro expander)
    foreach(kind own own_direct own_parenthesized own_value own_cast foreign conditional unevaluated)
        set(expected "requires a same-function label operand")
        set(body "if (0u32) { $::_jmp(target::done); done: ; }")
        if(kind STREQUAL own)
            # Source-only layout queries may inspect the local label type;
            # neither the raw jump nor the label is executed during expansion.
            set(body "if (0u32) { sizeof((u32)$::_jmp(done)); done: ; }")
            set(expected pass)
        elseif(kind STREQUAL own_direct OR kind STREQUAL own_parenthesized)
            set(operand done)
            if(kind STREQUAL own_parenthesized)
                set(operand "((done))")
            endif()
            set(body "if (0u32) { $::_jmp(${operand}); done: ; }")
            set(expected pass)
        elseif(kind STREQUAL own_cast)
            set(body "if (0u32) { $::_jmp((uptr)done); done: ; }")
            set(expected "cannot take a code label address of a translation-only function")
        elseif(kind STREQUAL own_value)
            set(body "if (0u32) { label address = done; done: ; }")
            set(expected "cannot take a code label address of a translation-only function")
        elseif(kind STREQUAL conditional)
            set(body "if (0u32) $::_je(target::done);")
        elseif(kind STREQUAL unevaluated)
            set(body "sizeof((u32)$::_jmp(target::done));")
        endif()
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
        check(${role}_${kind}_unused "${expected}" "${target} ${definition} global u32 entry() { return 1u32; }")
        check(${role}_${kind}_invoked "${expected}" "${target} ${definition} global u32 entry() { return ${invocation}; }")
    endforeach()
endforeach()
check(reached_direct_label "call has no visible translation-time implementation" [=[
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
        $::_jmp(done); done: ; return input;
    }
    global u32 entry() { return apply!(1u32); }
]=])
check(generic_owner pass [=[
    static $::meta::tokens generic<T>(in $::meta::tokens input) {
        if (0u32) { sizeof((u32)$::_jmp(done)); done: ; }
        return input;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
        return generic<u32>(input);
    }
    global u32 entry() { return apply!(1u32); }
]=])
foreach(form structured projected discarded)
    set(value "$::syntax::node(input, \"value\")")
    if(form STREQUAL projected)
        set(value "$::meta::tokens(${value})")
    endif()
    set(replacement "$::quote { $::unquote(${value}) }")
    set(expected "requires a same-function label operand")
    if(form STREQUAL discarded)
        set(replacement "$::quote {}")
        set(expected pass)
    endif()
    check(copied_${form} "${expected}" "${target}
        [[syntax_expander]] static $::meta::tokens emit(in $::meta::syntax_match input) { return ${replacement}; }
        syntax Emit : item { prefix \"copy_label\"; match \"{\" value:function_def \"}\"; expand emit; }
        syntax Emit;
        copy_label { static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) $::_jmp(target::done); return input;
        } }
        global u32 entry() { return 1u32; }")
endforeach()
