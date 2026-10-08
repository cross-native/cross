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
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${flags}
            "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}.s"
            RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(expected STREQUAL pass)
            if(NOT result EQUAL 0)
                message(FATAL_ERROR "${name}/${level}: expected success\n${out}\n${err}")
            endif()
        elseif(NOT result EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):")
            message(FATAL_ERROR "${name}/${level}: missing located '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()
# No runtime address may be manufactured for an explicit or implicit eval-only
# function, including source that will be erased or never evaluated.
set(eval_definitions [=[
[[eval_only]] static u32 only(in u32 value) { return value; }
static $::meta::tokens meta(in $::meta::tokens input) { return input; }
typedef u32 (*Callback)(in u32);
global void take(in Callback callback);
]=])
foreach(form
        "static Callback saved = only;"
        "Callback saved = &only;"
        "static struct Holder { Callback pointer; } saved = { only };"
        "(Callback)only;"
        "take(only);"
        "sizeof(&only);"
        "sizeof((Callback)only);"
        "sizeof((&only)(1u32));"
        "1u32 ? 0uptr : (uptr)only;"
        "meta;")
    string(MD5 case "${form}")
    foreach(role helper macro expander)
        set(attribute)
        set(parameter "$::meta::tokens")
        if(role STREQUAL macro)
            set(attribute "[[macro]]")
        elseif(role STREQUAL expander)
            set(attribute "[[syntax_expander]]")
            set(parameter "$::meta::syntax_match")
        endif()
        check(no_address_${role}_${case} "eval-only function '.*' has no runtime address"
            "${eval_definitions}
            ${attribute} static $::meta::tokens helper(in ${parameter} input) {
                if (0u32) { ${form} } return $::quote { 1u32 }; }")
    endforeach()
endforeach()
check(no_address_required "eval-only function 'only' has no runtime address"
    "${eval_definitions} global uptr saved = sizeof(&only);")
check(no_address_generic "eval-only function 'only' has no runtime address"
    "${eval_definitions}
    static $::meta::tokens helper<T>(in T value, in $::meta::tokens input) {
        if (0u32) { static Callback saved = only; } return input;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(1u32, input); }
    global u32 entry() { return apply!(7u32); }")
check(eval_direct_and_shadow pass
    "${eval_definitions}
    static u32 runtime(in u32 value) { return value; }
    static $::meta::tokens helper(in $::meta::tokens input) {
        sizeof(((only))(1u32));
        $::static_assert(((only))(3u32) == 3u32, \"direct call\");
        if (0u32) { Callback only = runtime; Callback copied = only; sizeof(&only); }
        return ((meta))(input);
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
    global u32 entry() { return apply!(7u32); }")
check(direct pass [=[
static u32 add(in u32 value) { return value + 2u32; }
static u32 nested() { return (((add)))(3u32); }
[[eval_only]] static u32 required(in u32 value) { return ((add))(value); }
static u32 saved = ((add))(5u32);
$::static_assert(((add))(3u32) == 5u32 && nested() == 5u32, "grouped direct call");
$::static_assert(((required))(4u32) == 6u32, "grouped eval-only call");
$::static_assert(sizeof(((add))(0u32)) == sizeof(u32), "grouped call result type");
global u32 entry() { return saved + $::eval(((required))(6u32)); }
]=])
check(generic pass [=[
static T add<T, u32 N>(in T value) { return value + (T)N; }
static T inferred<T>(in T value) { return value; }
$::static_assert(((add<u16, 2u32>))(3u16) == 5u16, "inner generic arguments");
$::static_assert(((add))<u32, 3u32>(4u32) == 7u32, "outer generic arguments");
$::static_assert(((add))::<u32, 4u32>(5u32) == 9u32, "explicit generic arguments");
$::static_assert(((inferred))(6u16) == 6u16, "grouped exact inference");
static u32 (*pointer)(in u32) = ((add))<u32, 2u32>;
typedef u32 (*Callback)(in u32);
static Callback factory<T>() { return add<T, 2u32>; }
global u32 entry() {
    return pointer(3u32) + $::runtime(((add<u32, 2u32>))(3u32)) + ((factory<u32>()))(4u32);
}
]=])
check(meta_helper pass [=[
namespace Definition {
    static u32 value() { return 7u32; }
    static $::meta::tokens helper(in $::meta::tokens input) {
        $::static_assert((($::meta::len))(input) == 1uptr, "grouped meta intrinsic");
        return $::quote { ((value))() };
    }
}
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return ((Definition::helper))($::quote { token });
}
syntax Value : expression { prefix "value_syntax"; match ";"; expand expand; }
syntax Value;
namespace Invocation {
    static u32 value() { return 99u32; }
    $::static_assert(value_syntax; == 7u32, "meta helper definition lookup");
}
]=])
check(local_hygiene pass [=[
static u32 original() { return 5u32; }
[[syntax_expander]] static $::meta::tokens bind(in $::meta::syntax_match input) {
    $::meta::syntax callee = $::syntax::node(input, "callee");
    return $::quote { {
        u32 $::unquote($::meta::tokens(callee)) = 9u32;
        return (($::unquote(callee)))();
    } };
}
syntax Bind : statement { prefix "bind"; match callee:expr ";"; expand bind; }
syntax Bind;
static u32 selected() { bind original; }
$::static_assert(selected() == 5u32, "grouping must preserve the original binding");
]=])
check(runtime_only "runtime.only" [=[
[[runtime_only]] static u32 runtime(in u32 value) { return value; }
$::static_assert(((runtime))(3u32) == 3u32, "must reject runtime-only evaluation");
]=])
check(runtime_eval_only "eval.only|runtime" [=[
[[eval_only]] static u32 required(in u32 value) { return value; }
global u32 entry() { return $::runtime(((required))(3u32)); }
]=])
check(meta_address "address|direct.*call|translation.only" [=[
static $::meta::tokens helper(in $::meta::tokens input) { return input; }
global u32 entry() { ((helper)); return 0u32; }
]=])
check(indirect_eval "indirect call" [=[
static u32 add(in u32 value) { return value + 2u32; }
static u32 selected() { u32 (*pointer)(in u32) = add; return ((pointer))(3u32); }
$::static_assert(selected() == 5u32, "not a direct function designator");
]=])
check(indirect_unevaluated pass [=[
typedef uptr (*Callback)(in u32);
[[runtime_only]] static Callback factory();
static Callback saved;
static Callback callbacks[2u32];
struct CallbackHolder { Callback callback; };
static struct CallbackHolder holder;
static struct CallbackHolder *holder_pointer;
static Callback *pointer_to_callback;
$::static_assert((0u32 ? ((Callback)0u32)(1u32) : 17uptr) == 17uptr, "unselected call");
$::static_assert((1u32 || ((Callback)0u32)(1u32)) == 1u32, "short circuit call");
$::static_assert(sizeof(((Callback)0u32)(1u32)) == sizeof(uptr), "indirect result type");
$::static_assert($::alignof((*saved)(1u32)) == $::alignof(uptr), "static pointer is not read");
$::static_assert(sizeof(factory()(1u32)) == sizeof(uptr), "factory is not evaluated");
$::static_assert(sizeof(callbacks[1u32](1u32)) == sizeof(uptr), "callback array is not read");
$::static_assert(sizeof(holder.callback(1u32)) == sizeof(uptr), "callback member is not read");
$::static_assert(sizeof(holder_pointer->callback(1u32)) == sizeof(uptr), "callback holder is not dereferenced");
$::static_assert(sizeof((*pointer_to_callback)(1u32)) == sizeof(uptr), "pointer to callback is not dereferenced");
$::static_assert(sizeof((1u32 ? saved : callbacks[0u32])(1u32)) == sizeof(uptr), "conditional callback type");
[[eval_only]] static uptr inspect(in Callback pointer) {
    return sizeof((*pointer)(1u32)) + (0u32 ? pointer(2u32) : 5uptr);
}
$::static_assert(inspect((Callback)0u32) == sizeof(uptr) + 5uptr, "held pointer is not called");
$::static_assert(sizeof(((u32 (*)(in bool))0uptr)((u32 *)0uptr)) == sizeof(u32), "pointer-to-bool argument");
$::static_assert(sizeof(((u32 (*)(out u32))0uptr)(1.0f64)) == sizeof(u32), "discarded output has no input conversion");
]=])
check(indirect_copyout_record "same nominal record type.*copy-out" [=[
struct Record { u32 value; };
static struct Record actual;
$::static_assert(sizeof(((u32 (*)(out u32))0uptr)(actual)) == sizeof(u32), "invalid copy-out");
]=])
check(indirect_copyout_pointer "pointer.*floating.*copy-out" [=[
static f64 actual;
$::static_assert(sizeof(((u32 (*)(out u32 *))0uptr)(actual)) == sizeof(u32), "invalid copy-out");
]=])
foreach(callee "((u32 (*)())0u32)" "(*((u32 (*)())0u32))")
    string(MAKE_C_IDENTIFIER "${callee}" key)
    check(indirect_reached_${key} "indirect calls are not permitted"
        "$::static_assert(${callee}() == 1u32, \"reached call\");")
endforeach()
foreach(context skipped sizeof)
    foreach(pair
            "((u32 (*)(in u32))0u32)()|invalid argument count"
            "((u32 (*)(in u32))0u32)(1u32, 2u32)|invalid argument count"
            "((u32 (*)(in u32 *))0u32)(1.0f64)|incompatible argument type|pointer.*floating"
            "((u32 (*)(in u32 *))0u32)(3u32)|incompatible argument type"
            "((u32 (*)(in u32))0u32)(missing)|unresolved name"
            "((u32 *)0u32)()|does not have a function type")
        string(FIND "${pair}" "|" split)
        string(SUBSTRING "${pair}" 0 ${split} expression)
        math(EXPR split "${split} + 1")
        string(SUBSTRING "${pair}" ${split} -1 error)
        string(SHA256 key "${expression}")
        string(SUBSTRING "${key}" 0 12 key)
        if(context STREQUAL skipped)
            set(use "(0u32 ? ${expression} : 1u32)")
        else()
            set(use "sizeof(${expression})")
        endif()
        check(indirect_${context}_${key} "${error}"
            "$::static_assert(${use} != 0u32, \"invalid call must remain diagnosed\");")
    endforeach()
endforeach()
check(object_callee "call.*function|callable|not a function" [=[
static u32 selected() { u32 value = 3u32; if (0u32) ((value))(4u32); return value; }
]=])
check(generic_address_count "generic argument count" [=[
static T add<T, u32 N>(in T value) { return value + (T)N; }
static u32 (*pointer)(in u32) = ((add))<u32>;
]=])
check(generic_address_kind "requires a type argument" [=[
static T add<T, u32 N>(in T value) { return value + (T)N; }
static u32 (*pointer)(in u32) = ((add))::<3u32, 2u32>;
]=])
check(generic_address_non_generic "non.generic function" [=[
static u32 add(in u32 value) { return value + 2u32; }
static u32 (*pointer)(in u32) = ((add))::<u32>;
]=])
check(generic_local_value "non.generic local value" [=[
static u32 add(in u32 value) { return value + 2u32; }
static u32 selected() {
    u32 (*pointer)(in u32) = add;
    u32 (*other)(in u32) = ((pointer))::<u32>;
    return other(3u32);
}
]=])
check(generic_computed_value "named function designator" [=[
typedef u32 (*Callback)(in u32);
static u32 add(in u32 value) { return value + 2u32; }
static Callback factory() { return add; }
static u32 selected() { return factory()::<u32>(3u32); }
]=])
check(generic_meta_address "address|direct.*call|translation.only" [=[
[[eval_only]] static T identity<T>(in T value) { return value; }
static u32 (*pointer)(in u32) = identity<u32>;
]=])
