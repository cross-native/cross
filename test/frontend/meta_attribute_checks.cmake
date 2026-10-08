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
                message(FATAL_ERROR "${name}/${level}: expected success\n${out}\n${err}")
            endif()
        elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):")
            message(FATAL_ERROR "${name}/${level}: missing located '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()

# Required local alignment cannot disappear with a helper or an untaken block.
# Keep target layout queries and ordinary runtime values separate: sizeof uses
# local types, but reading an automatic cell is not a constant expression.
foreach(argument "0uptr" "3uptr" "-4i32" "0x100000000u64")
    string(MAKE_C_IDENTIFIER "${argument}" name)
    foreach(use unused called)
        set(suffix "")
        if(use STREQUAL "called")
            set(suffix "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\n$::static_assert(apply!(7u32) == 7u32, \"helper result\");")
        endif()
        check(local_alignment_${name}_${use} "aligned argument must be a positive power-of-two integer constant"
            "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { [[aligned(${argument})]] u32 value; } return input; }\n${suffix}")
    endforeach()
endforeach()
foreach(arguments "" "4uptr, 8uptr")
    string(MAKE_C_IDENTIFIER "count_${arguments}" name)
    check(local_alignment_${name} "aligned on a local object requires one integer argument"
        "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { [[aligned(${arguments})]] u32 value; } return input; }")
endforeach()
check(local_alignment_runtime_value "constant|uninitialized|runtime" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    uptr count = 8uptr;
    if (0u32) { [[aligned(count)]] u32 value; }
    return input;
}
]=])
check(local_alignment_noninteger "integer|constant" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) { [[aligned(4.0f64)]] u32 value; }
    return input;
}
]=])
check(local_alignment_generic_invalid "aligned argument must be a positive power-of-two integer constant" [=[
static $::meta::tokens helper<uptr N>(in $::meta::tokens input) {
    if (0u32) { [[aligned(N)]] u32 value; }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper<3uptr>(input); }
$::static_assert(apply!(7u32) == 7u32, "helper result");
]=])
check(local_alignment_lexical_generic pass [=[
static uptr alignment<T>() { return sizeof(T) * 2uptr; }
static $::meta::tokens helper<T>(in $::meta::tokens input) {
    T seed;
    if (0u32) {
        [[aligned(sizeof(seed)), aligned(alignment<T>())]] u8 value;
        [[aligned(sizeof(value))]] u8 own;
    }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper<uptr>(input); }
$::static_assert(apply!(7u32) == 7u32, "helper result");
]=])
foreach(mode discard keep project)
    set(body "return $::quote {};")
    set(expected pass)
    if(mode STREQUAL "keep")
        set(body "return $::quote { $::unquote($::syntax::node(input, \"function\")) };")
    elseif(mode STREQUAL "project")
        set(body "return $::meta::tokens($::syntax::node(input, \"function\"));")
    endif()
    if(NOT mode STREQUAL "discard")
        set(expected "aligned argument must be a positive power-of-two integer constant")
    endif()
    check(local_alignment_${mode} "${expected}"
        "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { ${body} }\nsyntax Handle : item { prefix \"handle\"; match function:function_def; expand expand; } syntax Handle;\nhandle static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { [[aligned(3uptr)]] u32 value; } return input; }")
endforeach()

# Local/statement attributes are public grammar during capture, not executed
# source. Keeping or projecting the function restores ordinary validation,
# including in untaken branches of an unused translation-only helper.
foreach(case local_unknown local_used return_unknown return_arguments return_duplicate return_noncall return_empty statement)
    if(case STREQUAL local_unknown)
        set(statement "[[unknown_local_attribute]] u32 value;")
        set(diagnostic "attribute 'unknown_local_attribute' is not valid on a local object")
    elseif(case STREQUAL local_used)
        set(statement "u32 value [[used]];")
        set(diagnostic "attribute 'used' is not valid on a local object")
    elseif(case STREQUAL return_unknown)
        set(statement "[[unknown_return_attribute]] return input;")
        set(diagnostic "attribute 'unknown_return_attribute' is not valid on a return statement")
    elseif(case STREQUAL return_arguments)
        set(statement "[[musttail(1u32)]] return helper(input);")
        set(diagnostic "musttail does not take arguments")
    elseif(case STREQUAL return_duplicate)
        set(statement "[[musttail, musttail]] return helper(input);")
        set(diagnostic "a return statement has at most one musttail attribute")
    elseif(case STREQUAL return_noncall)
        set(statement "[[musttail]] return (input);")
        set(diagnostic "musttail requires returning a call expression directly")
    elseif(case STREQUAL return_empty)
        set(statement "[[musttail]] return;")
        set(diagnostic "musttail requires returning a call expression directly")
    else()
        set(statement "[[cold]] if (0u32) ;")
        set(diagnostic "attribute 'cold' is not valid on this statement")
    endif()
    foreach(mode discard keep project)
        set(body "return $::quote {};")
        set(expected pass)
        if(mode STREQUAL keep)
            set(body "return $::quote { $::unquote($::syntax::node(input, \"function\")) };")
            set(expected "${diagnostic}")
        elseif(mode STREQUAL project)
            set(body "return $::meta::tokens($::syntax::node(input, \"function\"));")
            set(expected "${diagnostic}")
        endif()
        check(captured_${case}_${mode} "${expected}"
            "[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) { ${body} }\nsyntax Handle : item { prefix \"handle\"; match function:function_def; expand expand; } syntax Handle;\nhandle static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { ${statement} } return input; }")
    endforeach()
endforeach()

# Tail-return source shape must be checked before helper/template erasure and
# before evaluating a macro. It is independent of physical target transport.
set(tail_shape "musttail requires returning a call expression directly")
foreach(role helper macro expander generic)
    set(attributes "")
    set(parameter "$::meta::tokens")
    set(parameters "")
    set(result "input")
    set(use "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\n$::static_assert(apply!(7u32) == 7u32, \"result\");")
    if(role STREQUAL macro)
        set(attributes "[[macro]]")
        set(use "$::static_assert(helper!(7u32) == 7u32, \"result\");")
    elseif(role STREQUAL expander)
        set(attributes "[[syntax_expander]]")
        set(parameter "$::meta::syntax_match")
        set(result "$::quote { 7u32 }")
        set(use "syntax Form : expression { prefix \"form\"; match \"(\" \")\"; expand helper; } syntax Form; $::static_assert(form () == 7u32, \"result\");")
    elseif(role STREQUAL generic)
        set(parameters "<T>")
        set(parameter "T")
    endif()
    foreach(state unused called)
        set(suffix "")
        if(state STREQUAL called)
            set(suffix "${use}")
        endif()
        check(tail_noncall_${role}_${state} "${tail_shape}"
            "${attributes} static $::meta::tokens helper${parameters}(in ${parameter} input) { if (0u32) { [[musttail]] return ${result}; } return ${result}; }\n${suffix}")
    endforeach()
endforeach()
foreach(operand "7u32" "(7u32)" "callee() + 1u32" "(u16)callee()" "1u32 ? callee() : callee()")
    string(MAKE_C_IDENTIFIER "${operand}" key)
    foreach(role runtime eval_only)
        set(attributes "")
        if(role STREQUAL eval_only)
            set(attributes "[[eval_only]]")
        endif()
        check(tail_noncall_${role}_${key} "${tail_shape}"
            "static u32 callee() { return 7u32; }\n${attributes} static u32 helper() { if (0u32) { [[musttail]] return ${operand}; } return 7u32; }")
    endforeach()
endforeach()
check(tail_parenthesized_call pass [=[
[[noinline, runtime_only]] global u32 callee(in u32 value) { return value; }
global u32 helper(in u32 value) { [[musttail]] return ((callee(value))); }
]=])
set(tail_constant_call [=[
[[noinline, link_name("tail_callee")]] global u32 callee(in u32 value) { return value + 1u32; }
[[noinline, link_name("tail_argument")]] global u32 argument() { return 41u32; }
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax Keep : item { prefix "keep"; match body:function_def; expand keep; }
syntax Keep;
keep global u32 helper() { [[musttail]] return ((callee($::eval(argument())))); }
]=])
foreach(evaluation eval no_eval)
    set(flag -feval-calls)
    if(evaluation STREQUAL no_eval)
        set(flag -fno-eval-calls)
    endif()
    check(tail_constant_${evaluation} pass "${tail_constant_call}" ${flag})
    string(REPLACE "$::eval(argument())" "argument()" optional_tail_argument "${tail_constant_call}")
    check(tail_optional_${evaluation} pass "${optional_tail_argument}" ${flag} -fno-inline)
    foreach(level O0 O2)
        file(READ "${OUTPUT}/tail_optional_${evaluation}-${level}.s" assembly)
        if(NOT assembly MATCHES "[\t ](jmpq?|j)[\t ]+[^\r\n]*tail_callee")
            message(FATAL_ERROR "${evaluation}/${level}: mandatory returned call was not retained\n${assembly}")
        endif()
        if(evaluation STREQUAL eval AND assembly MATCHES "[\t ](callq?|jal)[\t ]+[^\r\n]*tail_argument")
            message(FATAL_ERROR "${evaluation}/${level}: preserving the tail call suppressed optional argument folding\n${assembly}")
        elseif(evaluation STREQUAL no_eval AND NOT assembly MATCHES "[\t ](callq?|jal)[\t ]+[^\r\n]*tail_argument")
            message(FATAL_ERROR "${evaluation}/${level}: disabled optional argument evaluation was not respected\n${assembly}")
        endif()
    endforeach()
    string(REPLACE "$::eval(argument())" "$::eval(1u32 / 0u32)" bad_tail_argument "${tail_constant_call}")
    check(tail_argument_${evaluation} "division by zero" "${bad_tail_argument}" ${flag})
endforeach()

# Constraints on written function declarations cannot disappear with erased
# meta helpers, uninstantiated generic bodies, or unused runtime definitions.
foreach(attribute noinit thread_local "tls_model(\"local_exec\")" musttail)
    string(REGEX REPLACE "[(].*" "" name "${attribute}")
    set(expected "attribute '${name}' is not valid on a function")
    check(${name}_meta_unused "${expected}"
        "[[${attribute}]] static $::meta::tokens helper(in $::meta::tokens input) { return input; }")
    check(${name}_meta_called "${expected}"
        "static $::meta::tokens helper(in $::meta::tokens input) [[${attribute}]] { return input; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\n$::static_assert(apply!(7u32) == 7u32, \"helper result\");")
    check(${name}_generic_unused "${expected}"
        "[[${attribute}]] static T helper<T>(in T input) { return input; }")
    check(${name}_eval_unused "${expected}"
        "[[eval_only, ${attribute}]] static u32 helper(in u32 input) { return input; }")
    check(${name}_runtime "${expected}"
        "[[${attribute}]] global u32 helper(in u32 input) { return input; }")
    check(${name}_prototype "${expected}"
        "[[${attribute}]] global u32 helper(in u32 input);")
    check(${name}_discarded pass
        "[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; }\nsyntax Discard : item { prefix \"discard\"; match function:function_def; expand drop; } syntax Discard;\ndiscard [[${attribute}]] static u32 helper(in u32 input) { return input; }")
    check(${name}_surviving "${expected}"
        "[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) { return $::meta::tokens($::syntax::node(input, \"function\")); }\nsyntax Keep : item { prefix \"keep\"; match function:function_def; expand keep; } syntax Keep;\nkeep [[${attribute}]] static u32 helper(in u32 input) { return input; }")
endforeach()

foreach(pair "always_inline, noinline" "eval_only, runtime_only" "raw_inline, naked")
    string(MAKE_C_IDENTIFIER "${pair}" name)
    if(pair MATCHES "always_inline")
        set(expected "cannot be both always_inline and noinline")
    elseif(pair MATCHES "eval_only")
        set(expected "cannot be both eval_only and runtime_only")
    else()
        set(expected "raw_inline function is managed and cannot also be naked")
    endif()
    check(${name}_meta_unused "${expected}"
        "[[${pair}]] static $::meta::tokens helper(in $::meta::tokens input) { return input; }")
    check(${name}_meta_called "${expected}"
        "[[${pair}]] static $::meta::tokens helper(in $::meta::tokens input) { return input; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\n$::static_assert(apply!(7u32) == 7u32, \"helper result\");")
    check(${name}_generic_unused "${expected}"
        "[[${pair}]] static T helper<T>(in T input) { return input; }")
    check(${name}_scalar "${expected}"
        "[[${pair}]] static u32 helper(in u32 input) { return input; }")
endforeach()

# Meta signatures imply eval_only even without the explicit attribute.
check(implicit_runtime_only "cannot be both eval_only and runtime_only" [=[
[[runtime_only]] static $::meta::tokens helper(in $::meta::tokens input) { return input; }
]=])
check(implicit_runtime_only_called "cannot be both eval_only and runtime_only" [=[
[[runtime_only]] static $::meta::tokens helper(in $::meta::tokens input) { return input; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
$::static_assert(apply!(7u32) == 7u32, "helper result");
]=])
foreach(attribute always_inline noinline)
    check(single_${attribute} pass
        "[[${attribute}]] static $::meta::tokens helper(in $::meta::tokens input) { return input; }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\n$::static_assert(apply!(7u32) == 7u32, \"helper result\");")
endforeach()

foreach(role macro syntax_expander)
    if(role STREQUAL macro)
        set(parameter "$::meta::tokens")
        set(body "return input;")
        set(use "$::static_assert(apply!(7u32) == 7u32, \"macro result\");")
    else()
        set(parameter "$::meta::syntax_match")
        set(body "return $::syntax::capture(input, \"value\");")
        set(use "syntax Apply : expression { prefix \"apply\"; match value:literal; expand apply; } syntax Apply; $::static_assert(apply 7u32 == 7u32, \"expander result\");")
    endif()
    foreach(attribute always_inline noinline hot cold no_stack_protector "no_sanitize(\"bounds\")")
        string(MAKE_C_IDENTIFIER "${attribute}" attribute_name)
        check(${role}_${attribute_name} pass
            "[[${role}, ${attribute}]] static $::meta::tokens apply(${parameter} const (input)) { ${body} }\n${use}")
    endforeach()
    check(${role}_combined_hints pass
        "[[${role}, eval_only, cold, noinline, no_stack_protector, no_sanitize(\"bounds\")]] static $::meta::tokens apply(in ${parameter} input) { ${body} }\n${use}")
    foreach(attributes "hot, cold" "hot(1u32)" "cold(1u32)" "no_stack_protector(1u32)"
                       "noreturn(1u32)" "no_sanitize()" "no_sanitize(1u32)"
                       "no_sanitize(\"\")" "no_sanitize(\"a\", \"b\")")
        string(MAKE_C_IDENTIFIER "${attributes}" name)
        if(attributes STREQUAL "hot, cold")
            set(expected "cannot be both hot and cold")
        elseif(attributes MATCHES "^no_sanitize")
            set(expected "no_sanitize requires one (nonempty )?instrumentation-name string")
        else()
            set(expected "does not take arguments")
        endif()
        foreach(state unused called)
            set(suffix "")
            if(state STREQUAL called)
                set(suffix "${use}")
            endif()
            check(${role}_${name}_${state} "${expected}"
                "[[${role}, ${attributes}]] static $::meta::tokens apply(in ${parameter} input) { ${body} }\n${suffix}")
        endforeach()
    endforeach()
    # No resolved profile can give an erased expansion a physical boundary.
    foreach(attribute "abi(\"cross\")" "alias(\"other\")" "aligned(16)" "clobber(\"r8\")"
                      "interrupt(\"irq\")" "link_name(\"other\")" naked raw_inline retain returns_twice
                      "section(\".text\")" "stack_cleanup(\"caller\")" used
                      "variadic(uptr state \"gp\")" "visibility(\"hidden\")" weak "weakref(\"other\")")
        string(MAKE_C_IDENTIFIER "${attribute}" name)
        check(${role}_runtime_${name} "requires runtime symbol, ABI transport or entry/exit machinery"
            "[[${role}, ${attribute}]] static $::meta::tokens apply(in ${parameter} input) { ${body} }")
    endforeach()
    check(${role}_noreturn_unused pass
        "[[${role}, noreturn]] static $::meta::tokens apply(in ${parameter} input) { ${body} }")
    foreach(control return fallthrough)
        set(return_body "")
        if(control STREQUAL return)
            set(return_body "${body}")
        endif()
        check(${role}_noreturn_${control} "noreturn function 'apply' returned normally during translation-time evaluation"
            "[[${role}, noreturn]] static $::meta::tokens apply(in ${parameter} input) { ${return_body} }\n${use}")
    endforeach()
    check(${role}_noreturn_failure "static_assert failed: deliberate failure"
        "[[${role}, noreturn]] static $::meta::tokens apply(in ${parameter} input) { $::static_assert(0u32, \"deliberate failure\"); ${body} }\n${use}")
    check(${role}_alias_signature pass
        "typedef ${parameter} Input; typedef $::meta::tokens Output; [[${role}]] static Output apply(in const Input input) { ${body} }\n${use}")
    check(${role}_alias_suffix pass
        "typedef ${parameter} Input; typedef $::meta::tokens Output; static Output (apply)(const Input input) [[${role}, noinline]] { ${body} }\n${use}")
    check(${role}_opaque_owner pass
        "[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote {}; } syntax Drop : item { prefix \"drop\"; match tokens:tokens_until(\";\") \";\"; expand drop; } syntax Drop; drop [[${role}, invalid]] static $::meta::tokens discarded(${parameter} input); global u32 entry() { return 7u32; }")
    foreach(attributes "always_inline, noinline" "runtime_only" "noinit" "eval_only(1u32)" "missing_attribute")
        string(MAKE_C_IDENTIFIER "${attributes}" name)
        if(attributes MATCHES "always_inline")
            set(expected "cannot be both always_inline and noinline")
        elseif(attributes MATCHES "runtime_only")
            set(expected "cannot be both eval_only and runtime_only")
        elseif(attributes MATCHES "noinit")
            set(expected "attribute 'noinit' is not valid on a function")
        elseif(attributes MATCHES "eval_only")
            set(expected "eval_only does not take arguments")
        else()
            set(expected "unknown attribute")
        endif()
        check(${role}_${name}_unused "${expected}"
            "[[${role}, ${attributes}]] static $::meta::tokens apply(in ${parameter} input) { ${body} }")
    endforeach()
endforeach()

# Semantic contracts use the same evaluator boundary for ordinary/meta/generic
# helpers. Untaken calls and unevaluated operands cannot violate noreturn.
foreach(return_form value void empty)
    set(result "u32")
    set(body "return 7u32;")
    if(return_form STREQUAL void)
        set(result "void")
        set(body "return;")
    elseif(return_form STREQUAL empty)
        set(result "void")
        set(body "")
    endif()
    check(noreturn_helper_${return_form} "noreturn function 'helper' returned normally during translation-time evaluation"
        "[[noreturn]] static ${result} helper(in $::meta::tokens input) { ${body} }\n[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { helper(input); return input; }\n$::static_assert(apply!(7u32) == 7u32, \"result\");")
endforeach()
check(noreturn_helper_unselected pass [=[
[[noreturn]] static u32 helper(in $::meta::tokens input) { return 7u32; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    if (0u32) helper(input);
    $::static_assert(sizeof(helper(input)) == sizeof(u32), "unevaluated call");
    return input;
}
$::static_assert(apply!(7u32) == 7u32, "result");
]=])
check(noreturn_helper_prototype "noreturn function 'helper' returned normally during translation-time evaluation" [=[
[[noreturn]] static u32 helper(in $::meta::tokens input);
static u32 helper(in $::meta::tokens input) { return 7u32; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { helper(input); return input; }
$::static_assert(apply!(7u32) == 7u32, "result");
]=])
check(noreturn_generic_helper "noreturn function '.*helper.*' returned normally during translation-time evaluation" [=[
[[noreturn]] static T helper<T>(in T value) { return value; }
global u32 entry() { return $::eval(helper<u32>(7u32)); }
]=])
check(noreturn_generic_prototype "noreturn function '.*helper.*' returned normally during translation-time evaluation" [=[
[[noreturn]] static T helper<T>(in T value);
static T helper<T>(in T value) { return value; }
global u32 entry() { return $::eval(helper<u32>(7u32)); }
]=])
check(noreturn_generic_meta_prototype "noreturn function '.*helper.*' returned normally during translation-time evaluation" [=[
[[noreturn]] static T helper<T>(in T value);
static T helper<T>(in T value) { return value; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
$::static_assert(apply!(7u32) == 7u32, "result");
]=])
check(noreturn_void_forwarding "noreturn function 'helper' returned normally during translation-time evaluation" [=[
static void done() { return; }
[[noreturn]] static void helper(in $::meta::tokens input) { return done(); }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { helper(input); return input; }
$::static_assert(apply!(7u32) == 7u32, "result");
]=])
