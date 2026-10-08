# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC OUTPUT MODEL)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")
function(check name expected source)
    if(NOT DEFINED source_diagnostic)
        set(source_diagnostic "error")
    endif()
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${ARGN}
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(expected STREQUAL "accept")
            if(NOT result EQUAL 0)
                message(FATAL_ERROR "${name}/${level}: unexpected rejection\n${out}\n${err}")
            endif()
        elseif(NOT result EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (${source_diagnostic}):")
            message(FATAL_ERROR "${name}/${level}: missing '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()
function(check_expansion name expected source)
    # A retained node can report at <syntax-splice>, with the original source
    # identified by its token/expansion ancestry notes.
    set(source_diagnostic "error|note")
    check("${name}" "${expected}" "${source}" ${ARGN})
endfunction()
function(reject_body name body)
    set(helper "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { ${body} } return input; }")
    foreach(use unused called)
        set(suffix "global u32 entry() { return 9u32; }")
        if(use STREQUAL called)
            set(suffix "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\nglobal u32 entry() { return apply!(9u32); }")
        endif()
        check(${name}_${use} "unresolved name 'missing'" "${helper}\n${suffix}")
    endforeach()
endfunction()
reject_body(value "missing + 1u32;")
reject_body(function "missing();")
reject_body(initializer "u32 value = missing;")
reject_body(assignment "missing = 1u32;")
reject_body(update "++missing;")
reject_body(cast "(u32)missing;")
reject_body(condition "if (missing) return input;")
reject_body(index "u32 values[2] = {}; values[missing];")
reject_body(argument "helper(missing);")
reject_body(return "return missing;")
reject_body(member_base "missing.field;")
reject_body(sibling "{ u32 missing = 1u32; } { missing; }")
reject_body(for_scope "for (u32 missing = 0u32; missing < 1u32; ++missing) {} missing;")
reject_body(unevaluated "sizeof(missing);")
reject_body(unquote "$::quote { $::unquote(missing) };")
reject_body(goto "goto missing;")
reject_body(goto_parenthesized "goto (missing);")
foreach(case duplicate duplicate_nested global)
    if(case STREQUAL duplicate)
        set(body "repeated: ; repeated: ;")
        set(expected "duplicate label 'repeated'")
    elseif(case STREQUAL duplicate_nested)
        set(body "{ repeated: ; } while (0u32) { repeated: ; }")
        set(expected "duplicate label 'repeated'")
    else()
        set(body "global label exported: ;")
        set(expected "cannot export a code label from a translation-only function")
    endif()
    foreach(use unused called generic)
        set(parameters "")
        set(suffix "global u32 entry() { return 9u32; }")
        if(use STREQUAL generic)
            set(parameters "<T>")
            set(suffix "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper<u32>(input); }\nglobal u32 entry() { return apply!(9u32); }")
        elseif(use STREQUAL called)
            set(suffix "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\nglobal u32 entry() { return apply!(9u32); }")
        endif()
        check(label_${case}_${use} "${expected}"
            "static $::meta::tokens helper${parameters}(in $::meta::tokens input) { if (0u32) { ${body} } return input; }\n${suffix}")
    endforeach()
endforeach()
check(label_global_eval_only "cannot export a code label from a translation-only function" [=[
[[eval_only]] global u32 helper() { if (0u32) { global label exported: ; } return 9u32; }
global u32 entry() { return 9u32; }
]=])
check(label_duplicate_eval_only "duplicate label 'repeated'" [=[
[[eval_only]] static u32 helper() { if (0u32) { repeated: ; repeated: ; } return 9u32; }
global u32 entry() { return 9u32; }
]=])
check_expansion(label_duplicate_fresh "duplicate label" [=[
[[macro]] static $::meta::tokens labels(in $::meta::tokens input) {
    $::meta::tokens name = $::meta::gensym("repeated");
    return $::quote { $::unquote(name): ; $::unquote(name): ; };
}
static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { labels!() } return input; }
global u32 entry() { return 9u32; }
]=])
foreach(case scalar pointer meta parenthesized conditional)
    if(case STREQUAL scalar)
        set(body "u32 destination = 0u32; goto destination;")
    elseif(case STREQUAL pointer)
        set(body "u32 *destination = (u32 *)0uptr; goto destination;")
    elseif(case STREQUAL meta)
        set(body "goto input;")
    elseif(case STREQUAL parenthesized)
        set(body "u32 destination = 0u32; goto (destination); destination: ;")
    else()
        set(body "goto (1u32 ? 0u32 : 1u32);")
    endif()
    foreach(use unused called)
        set(suffix "global u32 entry() { return 9u32; }")
        if(use STREQUAL called)
            set(suffix "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\nglobal u32 entry() { return apply!(9u32); }")
        endif()
        check(goto_${case}_${use} "goto target does not name a visible label or label-valued expression"
            "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { ${body} } return input; }\n${suffix}")
    endforeach()
endforeach()
check(goto_generic "goto target does not name a visible label or label-valued expression" [=[
static $::meta::tokens helper<T>(in T destination, in $::meta::tokens input) {
    if (0u32) goto destination;
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(1u32, input); }
global u32 entry() { return apply!(9u32); }
]=])
# Direct role bodies follow the same source-name rules as ordinary helpers.
check(goto_macro "unresolved name 'missing'" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    if (0u32) goto missing;
    return input;
}
global u32 entry() { return apply!(9u32); }
]=])
check(goto_expander "unresolved name 'missing'" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    if (0u32) goto missing;
    return $::quote { 9u32 };
}
syntax Form : expression { prefix "form"; match "(" ")"; expand expand; }
syntax Form;
global u32 entry() { return form (); }
]=])

# Roles share source legality and reached-effect checks. In particular, an
# unused role definition is not an escape hatch from type checking.
foreach(role helper macro syntax_expander)
    set(attributes "")
    set(parameter "$::meta::tokens")
    set(result "return input;")
    set(invoke "[[macro]] static $::meta::tokens run(in $::meta::tokens input) { return apply(input); }\nglobal u32 entry() { return run!(9u32); }")
    if(role STREQUAL macro)
        set(attributes "[[macro]]")
        set(invoke "global u32 entry() { return apply!(9u32); }")
    elseif(role STREQUAL syntax_expander)
        set(attributes "[[syntax_expander]]")
        set(parameter "$::meta::syntax_match")
        set(result "return $::quote { 9u32 };")
        set(invoke "syntax Form : expression { prefix \"form\"; match \"(\" \")\"; expand apply; } syntax Form; global u32 entry() { return form (); }")
    endif()
    foreach(effect static volatile atomic goto physical)
        if(effect STREQUAL static)
            set(body "static u32 state = 1u32;")
            set(expected "runtime/static storage cannot be used")
        elseif(effect STREQUAL volatile)
            set(body "volatile u32 cell = 1u32;")
            set(expected "volatile or atomic access is not permitted")
        elseif(effect STREQUAL atomic)
            set(body "u32 [[atomic]] cell = 1u32;")
            set(expected "volatile or atomic access is not permitted")
        elseif(effect STREQUAL physical)
            set(body "register u32 cell \"eax\" = 1u32;")
            set(expected "physical local storage cannot be used")
        else()
            set(body "goto point; point: ;")
            set(expected "labels and goto are not permitted")
        endif()
        foreach(condition 0u32 1u32)
            set(outcome "${expected}")
            if(condition STREQUAL 0u32)
                set(outcome accept)
            endif()
            check(${role}_effect_${effect}_${condition} "${outcome}"
                "${attributes} static $::meta::tokens apply(in ${parameter} input) { if (${condition}) { ${body} } ${result} }\n${invoke}")
        endforeach()
    endforeach()
    foreach(body "missing;" "goto missing;" "u32 value = input;"
                 "static $::meta::tokens value;" "$::meta::tokens values[2];")
        string(MD5 key "${body}")
        if(body MATCHES missing)
            set(expected "unresolved name 'missing'")
        elseif(body MATCHES "u32 value")
            set(expected "incompatible.*initializer|meta.*runtime")
        else()
            set(expected "meta (cells require|values cannot have)|opaque meta values cannot appear inside runtime")
        endif()
        foreach(use unused invoked)
            set(suffix "global u32 entry() { return 9u32; }")
            if(use STREQUAL invoked)
                set(suffix "${invoke}")
            endif()
            check(${role}_invalid_${key}_${use} "${expected}"
                "${attributes} static $::meta::tokens apply(in ${parameter} input) { if (0u32) { ${body} } ${result} }\n${suffix}")
        endforeach()
    endforeach()
endforeach()
foreach(projection structured tokens)
    set(jump "$::unquote(jump)")
    set(block "$::unquote(block)")
    if(projection STREQUAL tokens)
        set(jump "$::unquote($::meta::tokens(jump))")
        set(block "$::unquote($::meta::tokens(block))")
    endif()
    set(prefix "[[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
        $::meta::syntax source = $::syntax::node(input, \"body\");
        $::meta::syntax block = $::meta::child(source, $::meta::child_count(source) - 1uptr);
        $::meta::syntax jump = $::meta::child(block, 1uptr);")
    set(suffix "}
        syntax Move : item { prefix \"move\"; match body:function_def; expand move; }
        syntax Move;
        move static void original() { goto point; point: ; }
        global u32 entry() { return 9u32; }")
    check_expansion(goto_relocated_${projection} "goto target does not name a visible label in its retained source binding"
        "${prefix} return $::quote {
            static $::meta::tokens unused(in $::meta::tokens input) {
                if (0u32) { ${jump} point: ; } return input;
            }
        }; ${suffix}")
    check(goto_pair_${projection} accept
        "${prefix} return $::quote {
            static $::meta::tokens unused(in $::meta::tokens input) {
                if (0u32) ${block} return input;
            }
        }; ${suffix}")
    check_expansion(label_duplicate_pair_${projection} "duplicate label 'point'"
        "${prefix} return $::quote {
            static $::meta::tokens unused(in $::meta::tokens input) {
                if (0u32) { ${block} ${block} } return input;
            }
        }; ${suffix}")
endforeach()
foreach(expression "0u32 && missing" "1u32 || missing" "1u32 ? 0u32 : missing"
                   "0u32 && sizeof(missing)" "1u32 || $::alignof(missing)")
    string(MD5 key "${expression}")
    check(required_${key} "unresolved name 'missing'" "global bool value = ${expression};")
endforeach()
check(eval_only "unresolved name 'missing'"
    "[[eval_only]] static u32 helper() { if (0u32) missing(); return 9u32; }\nglobal u32 entry() { return 9u32; }")
check(direct_macro "unresolved name 'missing'"
    "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { if (0u32) missing; return input; }\nglobal u32 entry() { return apply!(9u32); }")
check(generic "unresolved name 'missing'" [=[
static $::meta::tokens helper<T>(in T value, in $::meta::tokens input) {
    if (0u32) { T local = value; local + missing; }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(1u32, input); }
global u32 entry() { return apply!(9u32); }
]=])
check(direct_expander "unresolved name 'missing'" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    if (0u32) missing;
    return $::quote { 9u32 };
}
syntax Form : expression { prefix "form"; match "(" ")"; expand expand; }
syntax Form;
global u32 entry() { return form (); }
]=])
check(qualified "unresolved name 'Names::missing'"
    "namespace Names { static u32 value = 1u32; }\nstatic $::meta::tokens helper(in $::meta::tokens input) { if (0u32) Names::missing(); return input; }\nglobal u32 entry() { return 9u32; }")
check(member_selector "record has no member named 'missing'"
    "struct Record { u32 field; }; static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) { struct Record value; value.missing; } return input; }\nglobal u32 entry() { return 9u32; }")
check(atomic_expression "unresolved name 'missing'"
    "global bool query() { return $::atomic_is_lock_free(missing); }")

set(valid [=[
namespace Names {
    enum Choice { item = 3u32 };
    static u32 object = 4u32;
    static T identity<T>(in T value) { return value; }
}
struct Record { u32 field; };
[[macro]] static $::meta::tokens private_labels(in $::meta::tokens input) {
    return $::quote { private_point: ; };
}
[[macro]] static $::meta::tokens fresh_labels(in $::meta::tokens input) {
    $::meta::tokens first = $::meta::gensym("same");
    $::meta::tokens second = $::meta::gensym("same");
    return $::quote { $::unquote(first): ; $::unquote(second): ; };
}
static $::meta::tokens helper(in $::meta::tokens input) {
    using Names;
    bool true = 1u32, false = 0u32;
    struct Record value = {item};
    uptr size = sizeof(value.field) + $::alignof(value.field);
    u32 result = identity(value.field);
    { u32 result = 5u32; ++result; }
    if (0u32) {
        private_labels!() private_labels!() fresh_labels!()
        object; Names::object;
        u32 end = 0u32;
        goto end; // A direct label wins over the ordinary scalar object.
        end: ;
        label destination = (label)0uptr;
        goto destination;
        goto (destination);
        goto (1u32 ? destination : destination);
    }
    // Raw tokens and strings need no ordinary declaration until used as code.
    $::meta::tokens raw = $::quote { not_declared() + not_a_value };
    $::meta::tokens parsed = $::meta::parse("still_not_declared");
    $::meta::tokens composed = $::quote { $::unquote(raw) };
    if (!true || false || result != 3u32 || size == 0uptr) return $::quote { 0u32 };
    if (!$::has_attribute(syntax_expander) || !$::has_builtin($::meta::parse)) return $::quote { 0u32 };
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
$::static_assert(apply!(9u32) == 9u32, "ordinary lookup and quotation");
global u32 entry() { goto done; done: return apply!(9u32); }
global bool query(in uptr value) {
    return $::atomic_is_lock_free(uptr) && $::atomic_is_lock_free(value);
}
]=])
foreach(profile host custom o32 n64 n64_le)
    set(flags)
    if(profile STREQUAL custom)
        list(APPEND flags "--model=${MODEL}" -mabi=odd_abi)
    elseif(profile STREQUAL o32)
        list(APPEND flags -mprofile=r3000-o32)
    elseif(profile STREQUAL n64)
        list(APPEND flags -mprofile=mips64-n64)
    elseif(profile STREQUAL n64_le)
        list(APPEND flags -mprofile=mips64el-n64)
    endif()
    check(valid_${profile} accept "${valid}" ${flags})
    check(missing_${profile} "unresolved name 'missing'"
        "static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) missing(); return input; }\nglobal u32 entry() { return 9u32; }" ${flags})
endforeach()

# Bind by declaration identity, not a shipped ABI or variadic-state spelling.
file(READ "${MODEL}" model_source)
string(REPLACE "abi \"odd_abi\" {" [=[abi "odd_abi" {
    variadic_supported = true;
    variadic_state "user_area" { type = "u64*"; kind = "stack_address"; }
]=] model_source "${model_source}")
file(WRITE "${OUTPUT}/variadic-model.xm" "${model_source}")
check(custom_variadic accept [=[
[[abi("odd_abi"), variadic(u64 *state "user_area")]]
global u64 inspect(in u64 tag, ...) { return state[0]; }
[[macro]] static $::meta::tokens define(in $::meta::tokens input) {
    $::meta::tokens name = $::meta::gensym("state");
    return $::quote {
        [[abi("odd_abi"), variadic(u64 *$::unquote(name) "user_area")]]
        global u64 generated(in u64 tag, ...) { return $::unquote(name)[0]; }
    };
}
define!()
]=] "--model=${OUTPUT}/variadic-model.xm" -mabi=odd_abi)
