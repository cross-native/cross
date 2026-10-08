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
elseif(MODE STREQUAL mips OR MODE STREQUAL mipsel)
    list(APPEND flags -target "${MODE}-unknown-elf" -mprofile=r3000-o32)
elseif(MODE STREQUAL mips64 OR MODE STREQUAL mips64el)
    list(APPEND flags -target "${MODE}-unknown-elf" -mabi=n64)
elseif(NOT MODE STREQUAL native)
    message(FATAL_ERROR "unknown profile ${MODE}")
endif()
function(check name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    foreach(level O0 O2)
        foreach(folding normal noeval)
            set(optional)
            if(folding STREQUAL noeval)
                set(optional -fno-eval-calls)
            endif()
            execute_process(COMMAND "${CC}" ${flags} -S -${level} ${optional} ${ARGN}
                "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}-${level}-${folding}.s"
                RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
            if(expected STREQUAL pass)
                if(NOT status EQUAL 0)
                    message(FATAL_ERROR "${MODE}/${name}/${level}/${folding}: unexpected rejection\n${out}\n${err}")
                endif()
            elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
                   NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):")
                message(FATAL_ERROR "${MODE}/${name}/${level}/${folding}: missing '${expected}'\n${out}\n${err}")
            endif()
            if(expected MATCHES "budget|recursion")
                string(REGEX MATCHALL ":[0-9]+:[0-9]+: error:" errors "${err}")
                list(LENGTH errors count)
                if(NOT count EQUAL 1)
                    message(FATAL_ERROR "${MODE}/${name}/${level}/${folding}: resource failure cascaded\n${err}")
                endif()
            endif()
        endforeach()
    endforeach()
endfunction()
set(types "typedef u32 Lanes [[ext_vector_type(4)]];\n")
set(identity "namespace Helpers { static T identity<T>(in T value) { return value; } static u32 add(in u32 a, in u32 b) { return a + b; } }\n")
set(entry "global u32 entry() { return 0u32; }\n")
check(zero pass [=[
static u8 zero(in u32 value) { u32 local = value; return (u8)(local - value); }
static u32 *return_null() { return zero(7u32); }
static void take(in u32 *pointer) {}
global u32 entry() {
    u32 *pointer = zero(9u32);
    pointer = zero(11u32);
    take(zero(13u32));
    typedef void (*Callback)();
    Callback callback = zero(17u32);
    if (pointer != zero(19u32) || zero(23u32) != callback) return 1u32;
    if ((1u32 ? pointer : zero(29u32)) != return_null()) return 2u32;
    return 0u32;
}
]=])
foreach(index "Helpers::identity<u32>(4u32)" "Helpers::add(2u32, 2u32)" "Helpers::add(Helpers::identity<u32>(3u32), 1u32)")
    string(SHA256 key "${index}")
    string(SUBSTRING "${key}" 0 12 key)
    check(index_${key} "fixed-vector lane index is out of range"
        "${types}${identity}static u32 unused(in Lanes *value) { return (*value)[${index}]; }\n${entry}")
    check(unevaluated_${key} "fixed-vector lane index is out of range"
        "${types}${identity}static uptr unused(in Lanes *value) { return sizeof((*value)[${index}]); }\n${entry}")
endforeach()
check(string_index "fixed-vector lane index is out of range" "${types}static u32 boundary(in const u8 *value) { return value[0u32] == 'a' ? 4u32 : 0u32; }\nstatic u32 unused(in Lanes *value) { return (*value)[boundary(\"abc\")]; }\n${entry}")
check(aggregate_inputs pass [=[
struct Pair { u32 first; u32 second; };
static struct Pair make() { struct Pair value = {3u32, 4u32}; return value; }
static u32 sum(in struct Pair value) { return value.first + value.second; }
global u32 entry() { u32 *pointer = 0u32; return pointer != (sum(make()) - 7u32); }
]=])
check(projected_inputs pass [=[
struct Pair { u32 first; u32 second; };
static struct Pair make() { struct Pair value = {3u32, 4u32}; return value; }
static u32 zero(in u32 value) { return value - value; }
global u32 entry() {
    u32 *pointer = 0u32;
    return pointer != zero(make().first) || pointer != zero("abc"[1u32]) ||
        pointer != zero(*"abc") || pointer != zero(*((const i8 *)"\xff"));
}
]=])
check(address_inputs pass [=[
struct Pair { u32 first; u32 second; };
static struct Pair object;
static u32 array[4];
static void target() {}
typedef void (*Callback)();
static u32 zero(in const u32 *value) { return 0u32; }
static u32 callback_zero(in Callback value) { return 0u32; }
global u32 entry() {
    u32 *pointer = 0u32;
    return pointer != zero(&object.first) || pointer != zero(&array[2u32]) ||
        pointer != zero((const u32 *)0u32) || pointer != callback_zero(&target);
}
]=])
check(vector_address pass "${types}static Lanes vector;\nstatic u32 zero(in const u32 *value) { return 0u32; }\nglobal u32 entry() { u32 *pointer = 0u32; return pointer != zero(&vector[2u32]); }")
check(vector_address_bounds "fixed-vector lane index is out of range" "${types}static Lanes vector;\nstatic u32 zero(in const u32 *value) { return 0u32; }\nglobal u32 entry() { u32 *pointer = 0u32; return pointer != zero(&vector[4u32]); }")
check(qualified_vector_address pass "${types}static const volatile Lanes vector;\nstatic u32 zero(in const volatile u32 *value) { return 0u32; }\nglobal u32 entry() { u32 *pointer = 0u32; return pointer != zero(&vector[2u32]); }")
foreach(view "const u32 *" "const volatile u8 *")
    string(SHA256 key "${view}")
    string(SUBSTRING "${key}" 0 12 key)
    check(string_view_${key} "pointer/integer equality requires an integer constant zero"
        "static u32 zero(in u32 value) { return 0u32; }\nglobal u32 entry() { u32 *pointer = 0u32; return pointer == zero(((${view})\"abcd\")[0u32]); }")
endforeach()
check(runtime_address "pointer/integer equality requires an integer constant zero" [=[
static u32 zero(in const u32 *value) { return 0u32; }
global u32 entry(in u32 input) { u32 *pointer = 0u32; return pointer == zero(&input); }
]=])
check(static_read "pointer/integer equality requires an integer constant zero" [=[
static u32 array[4];
static u32 zero(in u32 value) { return 0u32; }
global u32 entry() { u32 *pointer = 0u32; return pointer == zero(array[0u32]); }
]=])
check(address_inspection "pointer/integer equality requires an integer constant zero" [=[
static u32 object;
static u32 zero(in const u32 *value) { return (u32)((uptr)value - (uptr)value); }
global u32 entry() { u32 *pointer = 0u32; return pointer == zero(&object); }
]=])
check(vector_input pass "${types}[[eval_only]] static u32 first(in Lanes value) { return value[0u32]; }\nglobal u32 entry() { u32 *pointer = 0u32; return pointer != first((Lanes)0u32); }")
check(runtime_index pass "${types}${identity}static u32 unused(in Lanes *value, in u32 index) { return (*value)[Helpers::identity<u32>(index)]; }\n${entry}")
check(runtime_zero "pointer/integer equality requires an integer constant zero" [=[
static u32 ignored(in u32 value) { return 0u32; }
global u32 entry(in u32 input) { u32 *pointer = 0u32; return pointer == ignored(input); }
]=])
check(runtime_only "pointer/integer equality requires an integer constant zero" [=[
[[runtime_only]] static u32 zero() { return 0u32; }
global u32 entry() { u32 *pointer = 0u32; return pointer == zero(); }
]=])
check(effectful "pointer/integer equality requires an integer constant zero" [=[
static u32 effects;
static u32 zero() { ++effects; return 0u32; }
global u32 entry() { u32 *pointer = 0u32; return pointer == zero(); }
]=])
check(nonzero "pointer/integer equality requires an integer constant zero" "${identity}global u32 entry() { u32 *pointer = 0u32; return pointer == Helpers::identity<u32>(1u32); }")
check(narrow_runtime "pointer casts require an integer at least as wide" [=[
typedef void (*Callback)();
static u8 ignored(in u32 value) { return 0u8; }
global Callback entry(in u32 input) { return (Callback)ignored(input); }
]=])
set(context [=[
typedef u32 Lanes [[ext_vector_type(4)]];
static uptr count(in $::meta::tokens input) { return $::meta::len(input); }
static $::meta::tokens helper(in $::meta::tokens input) {
    Lanes value = 0u32;
    if (0u32) value[count($::quote { a b c d })];
    return input;
}
]=])
check(context_unused pass "${context}${entry}")
check(context_called "fixed-vector lane index is out of range" "${context}[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }\nglobal u32 entry() { return apply!(0u32); }")
set(nested "3u32")
foreach(index RANGE 1 240)
    set(nested "same(${nested})")
endforeach()
set(deep "${types}static u32 same(in u32 value) { return value; }\nstatic u32 unused(in Lanes *value) { return (*value)[${nested}]; }\n${entry}")
check(deep pass "${deep}" -feval-depth-limit=512)
# Argument-tree nesting is not call recursion: children execute before their
# parents enter. Limit work here, and actual recursive calls separately below.
check(deep_limited "instruction budget exceeded 25" "${deep}" -feval-step-limit=25 -fno-eval-calls)
check(recursive "recursion depth exceeded" "static u32 loop(in u32 value) { return loop(value); }\nstatic u32 unused(in u32 *pointer) { return pointer == loop(0u32); }\n${entry}" -feval-depth-limit=16)
