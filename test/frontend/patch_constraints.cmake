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
set(declarations [=[
struct Pair { u32 value; };
struct Sink { uptr address; uptr bits : 3; uptr array[2]; };
static struct Pair pair;
static struct Sink record;
static const struct Sink fixed_record;
static uptr sink;
static const uptr fixed;
static volatile uptr changing;
static [[atomic]] uptr atomic_sink;
static uptr *pointer;
static void empty() {}
]=])
set(cases zero excess record void meta dereference conditional const volatile atomic bitfield pointer_index member_const array_sink)
set(zero "$::patch()")
set(excess "$::patch(1u32, sink, sink)")
set(record "$::patch(pair)")
set(void "$::patch(empty())")
set(meta "$::patch(input)")
set(dereference "$::patch(1u32, *&sink)")
set(conditional "$::patch(1u32, 1u32 ? sink : sink)")
set(const "$::patch(1u32, fixed)")
set(volatile "$::patch(1u32, changing)")
set(atomic "$::patch(1u32, atomic_sink)")
set(bitfield "$::patch(1u32, record.bits)")
set(pointer_index "$::patch(1u32, pointer[0])")
set(member_const "$::patch(1u32, fixed_record.address)")
set(array_sink "$::patch(1u32, record.array)")
foreach(case IN LISTS cases)
    if(case MATCHES "^(zero|excess)$")
        set(expected "patch requires an initial value and optional address sink")
    elseif(case MATCHES "^(record|void|meta)$")
        set(expected "patch initial value must have scalar type")
    elseif(case MATCHES "^(dereference|conditional)$")
        set(expected "patch address sink must be a static object followed only")
    elseif(case MATCHES "^(const|volatile|atomic|member_const)$")
        set(expected "patch address sink must be unqualified and non-atomic")
    elseif(case STREQUAL "bitfield")
        set(expected "patch address sink cannot designate a bit-field")
    elseif(case STREQUAL "pointer_index")
        set(expected "patch array sink requires a direct array object")
    else()
        set(expected "patch address sink must designate a scalar subobject")
    endif()
    set(helper "${declarations}
        static $::meta::tokens helper(in $::meta::tokens input) { if (0u32) ${${case}}; return input; }")
    check(${case}_unused "${expected}" "${helper} global u32 entry() { return 1u32; }")
    check(${case}_invoked "${expected}" "${helper}
        [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
        global u32 entry() { return apply!(1u32); }")
    check(${case}_layout "${expected}" "${declarations}
        static $::meta::tokens helper(in $::meta::tokens input) {
            uptr size = sizeof((u32)${${case}}); return input;
        }")
    if(NOT case STREQUAL "meta")
        check(${case}_required "${expected}" "${declarations}
            $::static_assert(${${case}}, \"required source\");")
    endif()
endforeach()
check(expander "patch requires an initial value" [=[
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    if (0u32) $::patch(); return $::quote { 1u32 };
}
syntax Unused : expression { prefix "unused"; match "(" ")"; expand expand; }
]=])
check(generic "patch initial value must have scalar type" "${declarations}
    static u32 helper<T>(in T value) { if (0u32) $::patch(value); return 1u32; }
    static u32 invoke() { struct Pair value = {1u32}; return helper(value); }
    $::static_assert(invoke(), \"instantiated source\");")
check(reached "patch is not permitted during translation-time evaluation"
    "$::static_assert($::patch(1u32), \"reached patch\");")
check(reached_helper "patch is not permitted during translation-time evaluation" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    $::patch(1u32); return input;
}
global u32 entry() { return apply!(1u32); }
]=])
string(CONCAT valid_source "${declarations}" [=[
$::static_assert(sizeof($::patch(7u16, record.address)) == sizeof(u16), "exact type");
$::static_assert($::alignof($::patch(7u16)) == $::alignof(u16), "exact alignment");
$::static_assert(1u32 || $::patch(7u32), "unselected patch");
static uptr size<T>() { return sizeof($::patch((T)3u32)); }
$::static_assert(size<u16>() == sizeof(u16), "generic source type");
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::patch(1u32, record.array[1]);
    if (sizeof($::patch(3u16)) != sizeof(u16)) return $::quote { 0u32 };
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(61u32); }
]=])
check(valid_types pass "${valid_source}")
set(capture [=[
[[syntax_expander]] static $::meta::tokens drop(in $::meta::syntax_match input) { return $::quote { 1u32 }; }
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "value")) };
}
syntax Drop : expression { prefix "drop"; match "(" value:expr ")"; expand drop; }
syntax Keep : expression { prefix "keep"; match "(" value:expr ")"; expand keep; }
syntax Drop, Keep;
]=])
check(discard pass "${capture} $::static_assert(drop($::patch()) == 1u32, \"discarded source\");")
check(survival "patch requires an initial value" "${capture}
    $::static_assert(keep(sizeof((u32)$::patch())) == sizeof(u32), \"surviving source\");")
check(capture_type pass "${capture}
    $::static_assert(sizeof(keep($::patch(3u16))) == sizeof(u16), \"retained patch type\");")

# Sink type is an exact target requirement, not integer conversion or equal
# storage size. Test source that disappears as well as retained/runtime code.
foreach(type u32 u64 iptr f64 "uptr *" "enum Address")
    string(MAKE_C_IDENTIFIER "${type}" key)
    set(sink_declarations "enum Address [[underlying(uptr)]] { Zero = 0uptr };
        static ${type} wrong;")
    set(expected "complete unqualified non-atomic uptr subobject")
    check(sink_type_${key}_unused "${expected}" "${sink_declarations}
        static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) $::patch(1u32, wrong); return input;
        }")
    check(sink_type_${key}_layout "${expected}" "${sink_declarations}
        $::static_assert(sizeof($::patch(1u32, wrong)) == sizeof(u32), \"sink type\");")
    check(sink_type_${key}_runtime "${expected}" "${sink_declarations}
        global u32 entry() { return $::patch(1u32, wrong); }")
    if(NOT MODE MATCHES "^mips")
        check(sink_type_${key}_raw "${expected}" "${sink_declarations}
            static $::meta::tokens helper(in $::meta::tokens input) {
                if (0u32) $::_movabs($::reg::r10, $::patch(1u64, wrong)); return input;
            }")
    endif()
endforeach()
check(sink_type_context "complete unqualified non-atomic uptr subobject" [=[
static u64 wrong;
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::patch($::eval(count($::quote {})), wrong);
    return input;
}
]=])
check(sink_type_index_context "complete unqualified non-atomic uptr subobject" [=[
static u64 wrong[1];
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::patch(1u32, wrong[$::eval(count($::quote {}))]);
    return input;
}
]=])
check(sink_type_alias pass [=[
typedef uptr Address;
struct Sinks { Address cells[2]; };
static struct Sinks record;
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::patch(1u32, record.cells[1]); return input;
}
$::static_assert(sizeof($::patch(1u32, record.cells[0])) == sizeof(u32), "alias sink");
]=])
check(sink_type_discard pass "${capture} static u64 wrong;
    global u32 entry() { return drop($::patch(1u32, wrong)); }")
check(sink_type_retained "complete unqualified non-atomic uptr subobject" "${capture} static u64 wrong;
    global uptr entry() { return sizeof(keep($::patch(1u32, wrong))); }")

foreach(index "-1i32" "2uptr" "0x10000000000000000u128" "runtime_index")
    if(index STREQUAL "-1i32")
        set(expected "nonnegative integer constant index")
    elseif(index STREQUAL "runtime_index")
        set(expected "runtime/static storage cannot be read during translation-time evaluation")
    else()
        set(expected "array sink index is out of range")
    endif()
    check(index_${index} "${expected}" "${declarations}
        static uptr runtime_index;
        static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) $::patch(1u32, record.array[${index}]); return input;
        }")
    check(index_layout_${index} "${expected}" "${declarations}
        static uptr runtime_index;
        $::static_assert(sizeof($::patch(1u32, record.array[${index}])) == sizeof(u32), \"source index\");")
endforeach()
check(index_helper pass "${declarations}
    static uptr index() { return sizeof(u16) - 1uptr; }
    $::static_assert(sizeof($::patch(1u32, record.array[index()])) == sizeof(u32), \"required index call\");")
set(context_index [=[
static uptr sinks[2];
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::patch(1u32, sinks[$::eval(count($::quote { TOKENS }))]);
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
]=])
string(REPLACE TOKENS "one" context_valid "${context_index}")
check(context_dependent pass "${context_valid} global u32 entry() { return apply!(1u32) + apply!(2u32); }")
string(REPLACE TOKENS "one two" context_invalid "${context_index}")
check(context_unused pass "${context_invalid} global u32 entry() { return 1u32; }")
check(context_invoked "array sink index is out of range"
    "${context_invalid} global u32 entry() { return apply!(1u32); }")
check(index_discard pass "${capture} ${declarations}
    $::static_assert(drop($::patch(1u32, record.array[99])) == 1u32, \"discarded index\");")
check(index_survival "array sink index is out of range" "${capture} ${declarations}
    $::static_assert(sizeof(keep($::patch(1u32, record.array[99]))) == sizeof(u32), \"surviving index\");")

foreach(sink_name "local" "local_record.address" "input_sink")
    set(storage_source "${declarations}
        static $::meta::tokens helper(in $::meta::tokens input, in uptr input_sink) {
            uptr local; struct Sink local_record;
            if (0u32) $::patch(1u32, ${sink_name}); return input;
        }")
    check(storage_${sink_name} "patch address sink must designate a static-duration object" "${storage_source}")
    check(storage_invoked_${sink_name} "patch address sink must designate a static-duration object" "${storage_source}
        [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input, 0uptr); }
        global u32 entry() { return apply!(1u32); }")
endforeach()
check(storage_layout "patch address sink must designate a static-duration object" [=[
static uptr sink;
static $::meta::tokens helper(in $::meta::tokens input) {
    uptr sink;
    uptr size = sizeof($::patch(1u32, sink));
    return input;
}
]=])
check(storage_generic "patch address sink must designate a static-duration object" [=[
static u32 helper<T>(in T cell) { if (0u32) $::patch(1u32, cell); return 1u32; }
$::static_assert(helper<uptr>(0uptr), "generic automatic sink");
]=])
check(storage_thread "patch address sink cannot designate thread-local storage" [=[
static [[thread_local]] uptr sink;
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::patch(1u32, sink); return input;
}
]=])
check(storage_thread_layout "patch address sink cannot designate thread-local storage" [=[
struct Slots { uptr cells[2]; };
static [[thread_local]] struct Slots record;
$::static_assert(sizeof($::patch(1u32, record.cells[1])) == sizeof(u32), "thread-local sink");
]=])
check(storage_static_local pass [=[
static uptr sink;
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) { static uptr sink; $::patch(1u32, sink); }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { static uptr sink; return $::patch(apply!(1u32), sink); }
]=])
check(storage_quoted_global pass [=[
static uptr sink;
[[macro]] static $::meta::tokens make(in $::meta::tokens input) { return $::quote { $::patch(1u32, sink) }; }
global u32 entry() { uptr sink; return make!(); }
]=])
check(storage_gensym_static pass [=[
[[macro]] static $::meta::tokens make(in $::meta::tokens input) {
    $::meta::tokens name = $::meta::gensym("sink");
    return $::quote { { static uptr $::unquote(name); return $::patch(1u32, $::unquote(name)); } };
}
global u32 entry() { make!() }
]=])
check(storage_discard pass "${capture}
    global u32 entry() { uptr local; return drop($::patch(1u32, local)); }")
check(storage_survival "patch address sink must designate a static-duration object" "${capture}
    global uptr entry() { uptr local; return sizeof(keep($::patch(1u32, local))); }")
check(storage_gensym_global pass [=[
static uptr sink = 0uptr;
[[macro]] static $::meta::tokens make(in $::meta::tokens input) {
    $::meta::tokens first = $::meta::gensym("sink");
    $::meta::tokens second = $::meta::gensym("sink");
    return $::quote {
        static uptr $::unquote(first);
        static uptr $::unquote(second) = 0uptr;
        global u32 entry() { return $::patch(1u32, $::unquote(first)); }
    };
}
make!()
]=])

set(initialized_declarations [=[
struct Tagged { u32 tag; uptr sink; };
union Overlap { uptr sink; u32 tag; };
static uptr scalar = 0uptr;
static struct Tagged empty = {};
static struct Tagged partial = { .tag = 3u32 };
static struct Tagged filled = { .sink = 0uptr };
static union Overlap overlap = { .tag = 3u32 };
static struct Tagged array[3] = { [sizeof(u8)] = { .sink = 0uptr }, [2] = { .tag = 7u32 } };
]=])
foreach(sink_name "scalar" "filled.sink" "overlap.sink" "array[1].sink")
    string(MAKE_C_IDENTIFIER "${sink_name}" identifier)
    check(initialized_${identifier} "uninitialized static-duration subobject" "${initialized_declarations}
        static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) $::patch(1u32, ${sink_name}); return input;
        }")
    check(initialized_layout_${identifier} "uninitialized static-duration subobject" "${initialized_declarations}
        $::static_assert(sizeof($::patch(1u32, ${sink_name})) == sizeof(u32), \"initialized sink\");")
endforeach()
foreach(sink_name "empty.sink" "partial.sink" "array[0].sink" "array[2].sink")
    string(MAKE_C_IDENTIFIER "${sink_name}" identifier)
    check(omitted_${identifier} pass "${initialized_declarations}
        static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) $::patch(1u32, ${sink_name}); return input;
        }
        [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
        global u32 entry() { return $::patch(apply!(1u32), ${sink_name}); }")
endforeach()
check(initialized_static_local "uninitialized static-duration subobject" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) { static uptr sink = 0uptr; $::patch(1u32, sink); } return input;
}
]=])
check(initialized_forward "uninitialized static-duration subobject" [=[
uptr sink;
global uptr sink = 0uptr;
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::patch(1u32, sink); return input;
}
]=])
check(initialized_later "uninitialized static-duration subobject" [=[
uptr sink;
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::patch(1u32, sink); return input;
}
global uptr sink = 0uptr;
]=])
set(context_initializer [=[
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) {
        static uptr sinks[2] = { [$::eval(count($::quote { TOKENS }))] = 0uptr };
        $::patch(1u32, sinks[1]);
    }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
]=])
string(REPLACE TOKENS "" initializer_valid "${context_initializer}")
check(initializer_context_valid pass "${initializer_valid} global u32 entry() { return apply!(1u32); }")
string(REPLACE TOKENS "one" initializer_invalid "${context_initializer}")
check(initializer_context_unused pass "${initializer_invalid} global u32 entry() { return 1u32; }")
check(initializer_context_invoked "uninitialized static-duration subobject"
    "${initializer_invalid} global u32 entry() { return apply!(1u32); }")
check(initializer_discard pass "${capture} ${initialized_declarations}
    global u32 entry() { return drop($::patch(1u32, filled.sink)); }")
check(initializer_survival "uninitialized static-duration subobject" "${capture} ${initialized_declarations}
    global uptr entry() { return sizeof(keep($::patch(1u32, filled.sink))); }")

# Translation-only static declarations remain source-checked, but are never
# lifted into emitted runtime storage. Only invocation-dependent proofs defer.
set(addend_declarations [=[
static u64 storage[4];
struct OffsetRecord { u64 first; u64 last; };
static struct OffsetRecord records[2];
]=])
foreach(initial
        "(uptr)&storage + 0x7fffffffffffffffu64 + 1u64"
        "(uptr)&storage - 0x8000000000000000u64 - 1u64"
        "(uptr)((uptr)&storage + 0x10000000000000000u128)"
        "(uptr)(storage + 0x1000000000000000u64)"
        "(uptr)&storage[0x1000000000000000u64]"
        "(uptr)&records[0x07ffffffffffffffu64].last + 9u64"
        "(uptr)&storage + -1u64")
    string(MAKE_C_IDENTIFIER "${initial}" key)
    check(addend_${key}_unused "static address addend exceeds the supported relocation range" "${addend_declarations}
        static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) { static uptr value = ${initial}; } return input;
        }")
    if(NOT MODE MATCHES "^mips")
        check(addend_${key}_patch "static address addend exceeds the supported relocation range" "${addend_declarations}
            static $::meta::tokens helper(in $::meta::tokens input) {
                if (0u32) $::patch(${initial}); return input;
            }")
        check(addend_${key}_layout "static address addend exceeds the supported relocation range" "${addend_declarations}
            $::static_assert(sizeof($::patch(${initial})) == sizeof(uptr), \"addend\");")
    endif()
endforeach()
check(addend_valid pass "${addend_declarations}
    static $::meta::tokens helper(in $::meta::tokens input) {
        if (0u32) {
            static uptr high = (uptr)&storage + 0x7fffffffffffffffu64;
            static uptr low = (uptr)&storage - 0x8000000000000000u64;
            static uptr negative = (uptr)&storage + (i8)-1i32;
            static uptr pointer = (uptr)(storage + 2i32 - 1i32);
            static uptr member = (uptr)&records[1].last;
        }
        return input;
    }")
set(addend_context [=[
static u32 object;
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) {
        static uptr value = (uptr)&object + 0x7fffffffffffffffu64 + $::eval(count($::quote { TOKENS }));
    }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
]=])
string(REPLACE TOKENS "one" addend_bad "${addend_context}")
string(REPLACE TOKENS "" addend_good "${addend_context}")
check(addend_context_unused pass "${addend_bad}")
check(addend_context_valid pass "${addend_good} global u32 entry() { return apply!(1u32) + apply!(2u32); }")
check(addend_context_invalid "static address addend exceeds the supported relocation range"
    "${addend_bad} global u32 entry() { return apply!(1u32); }")

set(static_declarations [=[
static u32 state;
[[runtime_only]] static u32 runtime() { return 7u32; }
struct Values { u32 value; u32 *pointer; };
]=])
set(static_runtime "static u32 value = runtime();")
set(static_read "static u32 value = state;")
set(static_local "static u32 value = local;")
set(static_address "static u32 *value = &local;")
set(static_aggregate "static struct Values value = { .value = runtime() };")
set(static_aggregate_address "static struct Values value = { .pointer = &local };")
set(static_patch "static u32 value = $::patch(1u32);")
foreach(case runtime read local address aggregate aggregate_address patch)
    if(case MATCHES "^(runtime|aggregate)$")
        set(expected "call to runtime-only function")
    elseif(case STREQUAL read)
        set(expected "runtime/static storage cannot be read")
    elseif(case STREQUAL local)
        set(expected "(not a translation-time value|uninitialized|runtime local)")
    elseif(case MATCHES "address$")
        set(expected "cannot depend on an automatic local or parameter")
    else()
        set(expected "patch is not permitted during translation-time evaluation")
    endif()
    set(helper "${static_declarations}
        static $::meta::tokens helper(in $::meta::tokens input) {
            u32 local = 1u32; if (0u32) { ${static_${case}} } return input;
        }")
    check(static_value_${case}_unused "${expected}" "${helper}")
    check(static_value_${case}_invoked "${expected}" "${helper}
        [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
        global u32 entry() { return apply!(1u32); }")
endforeach()
string(CONCAT static_valid_source "${static_declarations}" [=[
static u32 constant() { return 7u32; }
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) {
        static u32 local = constant() + 3u32;
        static f64 number = 1.5f64 + 2.5f64;
        static u32 *pointer = &state;
        static u32 *local_pointer = &local;
        static uptr address = (uptr)&local + 1uptr;
        static u32 (*function)() = constant;
        static struct Values record = { .value = 1u32, .pointer = &state };
        static u32 *member = &record.value;
        static u32 array[2] = { 1u32, 2u32 };
        static u32 *element = &array[constant() - 6u32];
        static uptr selection = 1u32 ? (uptr)&state : (uptr)&local;
    }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(1u32); }
]=])
check(static_value_valid pass "${static_valid_source}")
foreach(level O0 O2)
    file(READ "${OUTPUT}/static_value_valid-${level}.s" assembly)
    if(assembly MATCHES "helper")
        message(FATAL_ERROR "translation-only static storage escaped into ${level} output")
    endif()
endforeach()
check(static_value_reached "runtime/static storage cannot be used" [=[
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
    static u32 value = 1u32; return input;
}
global u32 entry() { return apply!(1u32); }
]=])
set(static_context [=[
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
[[runtime_only]] static uptr runtime() { return 1uptr; }
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) {
        static uptr value = $::eval(count($::quote { TOKENS })) ? runtime() : 1uptr;
    }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
]=])
string(REPLACE TOKENS "" static_context_valid "${static_context}")
check(static_value_context_valid pass "${static_context_valid} global u32 entry() { return apply!(1u32); }")
string(REPLACE TOKENS "one" static_context_invalid "${static_context}")
check(static_value_context_unused pass "${static_context_invalid}")
check(static_value_context_invoked "call to runtime-only function"
    "${static_context_invalid} global u32 entry() { return apply!(1u32); }")
check(static_value_bytes pass [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) {
        static const u8 text[3] = $::meta::spelling($::quote { abc });
        static uptr length = sizeof(text);
    }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(1u32); }
]=])
check(static_value_context_independent "call to runtime-only function" [=[
[[runtime_only]] static uptr runtime() { return 7uptr; }
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) { static uptr value = runtime() + $::eval(count($::quote {})); }
    return input;
}
]=])

set(initial_declarations [=[
static uptr sink;
static u32 state;
static volatile u32 changing;
static [[atomic]] u32 atomic_value;
[[runtime_only]] static u32 runtime() { return 7u32; }
static u32 constant() { return 7u32; }
]=])
set(initial_local "local")
set(initial_static "state")
set(initial_volatile "changing")
set(initial_atomic "atomic_value")
set(initial_runtime "runtime()")
set(initial_barrier "$::runtime(7u32)")
set(initial_nested "$::patch(7u32)")
set(initial_divzero "7u32 / 0u32")
set(initial_address "(uptr)&local")
foreach(case local static volatile atomic runtime barrier nested divzero address)
    if(case STREQUAL local)
        set(expected "(not a translation-time value|uninitialized|runtime local)")
    elseif(case MATCHES "^(static|volatile|atomic)$")
        set(expected "(runtime/static storage cannot be read|volatile|atomic)")
    elseif(case STREQUAL runtime)
        set(expected "call to runtime-only function")
    elseif(case STREQUAL barrier)
        set(expected "runtime is invalid where a translation-time value is required")
    elseif(case STREQUAL nested)
        set(expected "patch is not permitted during translation-time evaluation")
    elseif(case STREQUAL divzero)
        set(expected "division by zero")
    else()
        set(expected "cannot depend on an automatic local or parameter")
    endif()
    set(helper "${initial_declarations}
        static $::meta::tokens helper(in $::meta::tokens input) {
            u32 local = 1u32; if (0u32) $::patch(${initial_${case}}, sink); return input;
        }")
    check(initial_${case}_unused "${expected}" "${helper}")
    check(initial_${case}_invoked "${expected}" "${helper}
        [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
        global u32 entry() { return apply!(1u32); }")
    check(initial_${case}_layout "${expected}" "${initial_declarations}
        global uptr entry() { u32 local = 1u32; return sizeof($::patch(${initial_${case}})); }")
endforeach()
check(initial_generic "(not a translation-time value|uninitialized|runtime local)" [=[
static $::meta::tokens helper<T>(in T value, in $::meta::tokens input) {
    if (0u32) $::patch(value); return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(1u32, input); }
global u32 entry() { return apply!(1u32); }
]=])
check(initial_constants pass "${initial_declarations}
    static $::meta::tokens helper(in $::meta::tokens input) {
        if (0u32) $::patch(constant(), sink);
        uptr size = sizeof($::patch(1u32 ? constant() : runtime()));
        uptr nested = sizeof($::patch((u32)sizeof($::patch(1u16))));
        uptr short_circuit = sizeof($::patch((u32)(0u32 && $::patch(1u32))));
        return input;
    }
    [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
    global u32 entry() { return apply!(1u32); }")
set(initial_context [=[
static uptr sink;
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
[[runtime_only]] static uptr runtime() { return 1uptr; }
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::patch($::eval(count($::quote { TOKENS })) ? runtime() : 7uptr, sink);
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
]=])
string(REPLACE TOKENS "" initial_context_valid "${initial_context}")
check(initial_context_valid pass "${initial_context_valid} global u32 entry() { return apply!(1u32) + apply!(2u32); }")
string(REPLACE TOKENS "one" initial_context_invalid "${initial_context}")
check(initial_context_unused pass "${initial_context_invalid}")
check(initial_context_invoked "call to runtime-only function"
    "${initial_context_invalid} global u32 entry() { return apply!(1u32); }")
check(initial_discard pass "${capture} ${initial_declarations}
    global u32 entry() { u32 local = 1u32; return drop($::patch(local)); }")
check(initial_survival "(not a translation-time value|uninitialized|runtime local)" "${capture} ${initial_declarations}
    global uptr entry() { u32 local = 1u32; return sizeof(keep($::patch(local))); }")
if(NOT MODE MATCHES "^mips")
    check(initial_symbolic pass "${initial_declarations}
        static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
        static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) {
                static u32 local;
                $::patch((uptr)&local + $::eval(count($::quote { one })), sink);
                $::patch((uptr)&state - 1uptr);
                $::patch((uptr)&changing);
                $::patch((uptr)(1u32 ? &state : &local));
                $::patch((uptr)&constant);
            }
            return input;
        }
        [[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
        global u32 entry() { return apply!(1u32); }")
endif()

# Generic materializer capability is a source constraint, even when no code
# or patch cell is emitted. Direct instruction fields have a separate contract.
foreach(initial "1u128" "1i128" "1.0f32" "1.0f64" "(bool)1u32" "(u32*)0uptr")
    string(MAKE_C_IDENTIFIER "${initial}" case)
    set(expected "no contiguous .*patch materializer")
    check(materializer_${case}_unused "${expected}" "
        static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) $::patch(${initial}); return input;
        }")
    check(materializer_${case}_invoked "${expected}" "
        [[macro]] static $::meta::tokens apply(in $::meta::tokens input) {
            if (0u32) $::patch(${initial}); return input;
        }
        global u32 entry() { return apply!(1u32); }")
    check(materializer_${case}_layout "${expected}" "
        $::static_assert(sizeof($::patch(${initial})) > 0uptr, \"source capability\");")
    check(materializer_${case}_discard pass "${capture}
        global u32 entry() { return drop($::patch(${initial})); }")
    check(materializer_${case}_survival "${expected}" "${capture}
        global uptr entry() { return sizeof(keep($::patch(${initial}))); }")
endforeach()
check(materializer_context_independent "no contiguous .*patch materializer" [=[
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::patch((u128)$::eval(count($::quote {}))); return input;
}
]=])
check(materializer_generic "no contiguous .*patch materializer" [=[
static uptr size<T>() { return sizeof($::patch((T)1u32)); }
$::static_assert(size<u128>() > 0uptr, "instantiated target capability");
]=])
check(materializer_integer_types pass [=[
enum Code [[underlying(u16)]] { first = 1u16 };
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) {
        $::patch(1i8); $::patch(1u8); $::patch(1i16); $::patch(1u16);
        $::patch(1i32); $::patch(1u32); $::patch(1i64); $::patch(1u64);
        $::patch(1iptr); $::patch(1uptr); $::patch(first);
    }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(1u32); }
]=])
if(MODE MATCHES "^mips")
    set(symbol_expected "patch materializer cannot encode a symbol relocation")
else()
    set(symbol_expected pass)
endif()
set(subobject_declarations [=[
struct AddressRecord { u8 tag; u32 values[3]; };
static struct AddressRecord address_record;
static struct AddressRecord address_records[2];
static u32 address_matrix[2][3];
]=])
foreach(initial
        "(uptr)(address_record.values + 1u32)"
        "(uptr)((&address_record)->values + 1u32)"
        "(uptr)((*&address_record).values + 1u32)"
        "(uptr)(address_records->values + 1u32)"
        "(uptr)(address_matrix[1] + 1u32)"
        "(uptr)(*(address_matrix + 1u32) + 1u32)"
        "(uptr)(*((u32 (*)[3])address_matrix) + 1u32)")
    string(MAKE_C_IDENTIFIER "${initial}" case)
    check(subobject_${case}_static pass "${subobject_declarations}
        static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) { static uptr value = ${initial}; } return input;
        }")
    check(subobject_${case}_patch "${symbol_expected}" "${subobject_declarations}
        static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) $::patch(${initial}); return input;
        }")
endforeach()
# Forming an absolute address is not a storage read or a symbolic relocation.
# The same expressions must stay numeric when their patch cells survive.
foreach(initial
        "(uptr)&*((u32*)0x1000uptr)"
        "(uptr)((u32*)0x1000uptr + 1u32)"
        "(uptr)(1u32 + (u32*)0x1000uptr)"
        "(uptr)&((u32*)0x1000uptr)[2u32]"
        "(uptr)((u32*)0x1008uptr - 1u32)"
        "(uptr)&((struct AddressRecord*)0x1000uptr)->values[1u32]"
        "(uptr)(((struct AddressRecord*)0x1000uptr)->values + 1u32)"
        "(uptr)(*((u32 (*)[3])0x1000uptr) + 1u32)"
        "(uptr)((u32 (*)[3])0x1000uptr)[1u32]")
    string(MAKE_C_IDENTIFIER "${initial}" case)
    check(absolute_${case}_unused pass "${subobject_declarations}
        static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) { static uptr value = ${initial}; $::patch(${initial}); } return input;
        }")
    check(absolute_${case}_runtime pass "${subobject_declarations}
        global uptr entry() { return $::patch(${initial}); }")
endforeach()
foreach(initial "(uptr)((u32*)0uptr - 1u32)" "(uptr)((u32*)-1uptr + 1u32)")
    string(MAKE_C_IDENTIFIER "${initial}" case)
    check(absolute_${case}_overflow "overflows the selected target width" "
        static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) $::patch(${initial}); return input;
        }")
endforeach()
check(array_storage_read "runtime/static storage cannot be read" "${subobject_declarations}
    static $::meta::tokens helper(in $::meta::tokens input) {
        if (0u32) $::patch(address_record.values[1]); return input;
    }")
check(array_automatic_escape "cannot depend on an automatic local or parameter" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    u32 automatic[2][3];
    if (0u32) { static uptr value = (uptr)automatic[1]; } return input;
}
]=])
foreach(initial "(uptr)&state" "(uptr)&constant" "(uptr)&state + 1uptr")
    string(MAKE_C_IDENTIFIER "${initial}" case)
    check(materializer_symbol_${case}_unused "${symbol_expected}" "${initial_declarations}
        static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) $::patch(${initial}); return input;
        }")
    check(materializer_symbol_${case}_layout "${symbol_expected}" "${initial_declarations}
        $::static_assert(sizeof($::patch(${initial})) == sizeof(uptr), \"source relocation\");")
endforeach()
set(symbol_context [=[
static u32 state;
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::patch($::eval(count($::quote { TOKENS })) ? (uptr)&state : 1uptr);
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
]=])
string(REPLACE TOKENS "" symbol_numeric "${symbol_context}")
string(REPLACE TOKENS "one" symbol_relocation "${symbol_context}")
check(materializer_symbol_context_unused pass "${symbol_relocation}")
check(materializer_symbol_context_numeric pass "${symbol_numeric}
    global u32 entry() { return apply!(1u32) + apply!(2u32); }")
check(materializer_symbol_context_invoked "${symbol_expected}" "${symbol_relocation}
    global u32 entry() { return apply!(1u32); }")
foreach(initial
        "(uptr)&state + $::eval(count($::quote {}))"
        "(uptr)(&state + $::eval(count($::quote {})))"
        "(uptr)&array[$::eval(count($::quote {}))]")
    string(MAKE_C_IDENTIFIER "${initial}" case)
    check(symbolic_category_${case} "${symbol_expected}" "
        static u32 state;
        static u32 array[2];
        static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
        static $::meta::tokens helper(in $::meta::tokens input) {
            if (0u32) $::patch(${initial}); return input;
        }")
endforeach()
if(NOT MODE MATCHES "^mips")
    # A direct raw field checks its own exact type set, not value-fit conversion
    # or the existence of a generic materializer for a different source type.
    check(materializer_raw_direct pass [=[
[[naked]] global void entry() {
    register u64 result "r10";
    $::_movabs(result, $::patch((u64)1u128));
    $::_ret();
}
]=])
    check(materializer_raw_erased pass [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::_movabs($::reg::r10, $::patch((u64)1u128)); return input;
}
]=])
    check(materializer_raw_nested "no contiguous .*patch materializer" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::_movabs($::reg::r10, (u64)$::patch(1u128)); return input;
}
]=])
    foreach(initial value "constant()")
        string(MAKE_C_IDENTIFIER "${initial}" case)
        set(enum_declarations "enum Code [[underlying(u64)]] { value = 1u64 };
            static enum Code constant() { return value; }")
        check(raw_enum_${case}_unused "no patch field accepting exactly" "${enum_declarations}
            static $::meta::tokens helper(in $::meta::tokens input) {
                if (0u32) $::_movabs($::reg::r10, $::patch(${initial})); return input;
            }")
        check(raw_enum_${case}_runtime "no patch field accepting exactly" "${enum_declarations}
            [[naked]] global void entry() {
                register u64 result \"r10\";
                $::_movabs(result, $::patch(${initial})); $::_ret();
            }")
        check(raw_enum_${case}_cast pass "${enum_declarations}
            [[naked]] global void entry() {
                register u64 result \"r10\";
                $::_movabs(result, $::patch((u64)${initial})); $::_ret();
            }")
    endforeach()
    foreach(initial "1u32" "1i32" "1u128" "1.0f64" "(bool)1u32" "(u32*)0uptr")
        string(MAKE_C_IDENTIFIER "${initial}" case)
        check(raw_field_${case}_unused "no patch field accepting exactly" "
            static $::meta::tokens helper(in $::meta::tokens input) {
                if (0u32) $::_movabs($::reg::r10, $::patch(${initial})); return input;
            }")
        check(raw_field_${case}_runtime "no patch field accepting exactly" "
            [[naked]] global void entry() {
                register u64 result \"r10\";
                $::_movabs(result, $::patch(${initial})); $::_ret();
            }")
    endforeach()
    foreach(initial "1u64" "-1i64" "1uptr" "1iptr" "(u64)1u128" "constant() + 1u64")
        string(MAKE_C_IDENTIFIER "${initial}" case)
        check(raw_field_${case}_constant pass "
            static u64 constant() { return 6u64; }
            [[naked]] global void entry() {
                register u64 result \"r10\";
                $::_movabs(result, $::patch(${initial})); $::_ret();
            }")
    endforeach()
    check(raw_field_nonpatch "no patch field accepting exactly" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::_add($::reg::r10, $::patch(1u64)); return input;
}
]=])
    check(raw_field_wrong_index "no patch field accepting exactly" [=[
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::_movabs($::patch(1u64), 1u64); return input;
}
]=])
    check(raw_field_context_independent "no patch field accepting exactly" [=[
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::_movabs($::reg::r10, $::patch((u32)$::eval(count($::quote {})))); return input;
}
]=])
    foreach(initial "(uptr)&state" "(uptr)&constant" "(uptr)&state + 1uptr")
        string(MAKE_C_IDENTIFIER "${initial}" case)
        foreach(sink "" ", sink")
            string(MAKE_C_IDENTIFIER "${sink}" suffix)
            check(raw_symbol_${case}_${suffix}_unused "instruction patch field cannot encode a symbol relocation"
                "${initial_declarations}
                static $::meta::tokens helper(in $::meta::tokens input) {
                    if (0u32) $::_movabs($::reg::r10, $::patch(${initial}${sink})); return input;
                }")
            check(raw_symbol_${case}_${suffix}_runtime "instruction patch field cannot encode a symbol relocation"
                "${initial_declarations}
                [[naked]] global void entry() {
                    register u64 result \"r10\";
                    $::_movabs(result, $::patch(${initial}${sink})); $::_ret();
                }")
        endforeach()
    endforeach()
    set(raw_symbol_context [=[
static u32 state;
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
static $::meta::tokens helper(in $::meta::tokens input) {
    if (0u32) $::_movabs($::reg::r10,
        $::patch($::eval(count($::quote { TOKENS })) ? (uptr)&state : 1uptr));
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
]=])
    string(REPLACE TOKENS "one" raw_symbol_invalid "${raw_symbol_context}")
    string(REPLACE TOKENS "" raw_symbol_valid "${raw_symbol_context}")
    check(raw_symbol_context_unused pass "${raw_symbol_invalid}")
    check(raw_symbol_context_numeric pass "${raw_symbol_valid}
        global u32 entry() { return apply!(1u32) + apply!(2u32); }")
    check(raw_symbol_context_invoked "instruction patch field cannot encode a symbol relocation"
        "${raw_symbol_invalid} global u32 entry() { return apply!(1u32); }")
    foreach(initial
            "(uptr)&state + $::eval(count($::quote {}))"
            "(uptr)(&state + $::eval(count($::quote {})))"
            "(uptr)&array[$::eval(count($::quote {}))]")
        string(MAKE_C_IDENTIFIER "${initial}" case)
        check(raw_symbolic_category_${case} "instruction patch field cannot encode a symbol relocation" "
            static u32 state;
            static u32 array[2];
            static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
            static $::meta::tokens helper(in $::meta::tokens input) {
                if (0u32) $::_movabs($::reg::r10, $::patch(${initial})); return input;
            }")
    endforeach()
endif()
