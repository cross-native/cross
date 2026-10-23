# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Assignment conversion between a pointer and an integer requires a cast,
# except that a source-constant integer zero becomes a null pointer and a
# pointer becomes a bool; a label converts implicitly only to a label or a
# bool. Every conversion context, static initializers, generic instances, and
# translation-time helpers apply the same rule.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile name source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}\n")
    if(NOT ARGN)
        set(ARGN -target x86_64-unknown-linux-gnu)
    endif()
    execute_process(COMMAND "${CC}" -S -O2 ${ARGN} "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(status "${status}" PARENT_SCOPE)
    set(diagnostics "${out}${err}" PARENT_SCOPE)
endfunction()

# `expected` is literal text; the diagnostic must be located in the source.
function(reject name expected source)
    compile(${name} "${source}" ${ARGN})
    string(FIND "${diagnostics}" "error: ${expected}" position)
    if(status EQUAL 0 OR position EQUAL -1 OR
       NOT diagnostics MATCHES "${name}\\.x:[0-9]+:[0-9]+: error:")
        message(FATAL_ERROR "${name}: missing '${expected}'\n${diagnostics}")
    endif()
endfunction()

function(accept name source)
    compile(${name} "${source}" ${ARGN})
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name}: unexpected rejection\n${diagnostics}")
    endif()
endfunction()

set(to_integer "implicit conversion from pointer 'u8 *' to integer 'u32' requires an explicit cast")
set(to_pointer "implicit conversion from integer 'u32' to pointer 'u8 *' requires an explicit cast")
set(prelude [=[
struct pair { u32 number; u8 *bytes; };
enum color { red, green };
static u32 take_integer(in u32 value) { return value; }
static u8 *take_pointer(in u8 *value) { return value; }
static void give_integer(out u32 value) { value = 1u32; }
static void give_pointer(out u8 *value) { value = 0; }
static void update_integer(inout u32 value) { value += 1u32; }
]=])

# Each context, from a pointer to an integer and back.
foreach(case
        "local_initializer|${to_integer} in local initializer|global u32 f(in u8 *p) { u32 x = p; return x; }"
        "assignment|${to_integer} in assignment|global u32 f(in u8 *p) { u32 x = 0u32; x = p; return x; }"
        "return|${to_integer} in return|global u32 f(in u8 *p) { return p; }"
        "argument|${to_integer} in call argument|global u32 f(in u8 *p) { return take_integer(p); }"
        "inout_argument|${to_integer} in call argument|global void f(in u8 *p) { update_integer(p); }"
        "copy_out|${to_integer} in parameter copy-out|global u32 f() { u32 x = 0u32; give_pointer(x); return x; }"
        "brace_entry|${to_integer} in local initializer|global u32 f(in u8 *p) { struct pair v = { p, 0 }; return v.number; }"
        "designated_entry|${to_integer} in local initializer|global u32 f(in u8 *p) { struct pair v = { .number = p }; return v.number; }"
        "braced_scalar|${to_integer} in local initializer|global u32 f(in u8 *p) { u32 x = { p }; return x; }"
        "compound_literal|${to_integer} in local initializer|global u32 f(in u8 *p) { return (u32){ p }; }"
        "conditional_result|${to_integer} in local initializer|global u32 f(in u8 *p, in u32 c) { u32 x = c != 0u32 ? p : 0; return x; }"
        "enumeration|implicit conversion from pointer 'u8 *' to integer 'enum color' requires an explicit cast in local initializer|global u32 f(in u8 *p) { enum color c = p; return (u32)c; }"
        "string|implicit conversion from pointer 'const u8 *' to integer 'u8' requires an explicit cast in local initializer|global u8 f() { u8 c = \"abc\"; return c; }"
        "string_entry|implicit conversion from pointer 'const u8 *' to integer 'u8' requires an explicit cast in local initializer|global u8 f() { u8 s[4] = { [0] = \"abc\" }; return s[0]; }"
        "local_pointer|${to_pointer} in local initializer|global u8 *f(in u32 x) { u8 *p = x; return p; }"
        "constant_pointer|implicit conversion from integer 'i32' to pointer 'u8 *' requires an explicit cast in local initializer|global u8 *f() { u8 *p = 4096; return p; }"
        "assignment_pointer|${to_pointer} in assignment|global u8 *f(in u32 x) { u8 *p = 0; p = x; return p; }"
        "return_pointer|${to_pointer} in return|global u8 *f(in u32 x) { return x; }"
        "argument_pointer|${to_pointer} in call argument|global u8 *f(in u32 x) { return take_pointer(x); }"
        "copy_out_pointer|${to_pointer} in parameter copy-out|global u8 *f() { u8 *p = 0; give_integer(p); return p; }"
        "brace_entry_pointer|${to_pointer} in local initializer|global u8 *f(in u32 x) { struct pair v = { 0, x }; return v.bytes; }"
        "compound_literal_pointer|${to_pointer} in local initializer|global u8 *f(in u32 x) { return (u8 *){ x }; }"
        "bool_pointer|implicit conversion from integer 'bool' to pointer 'u8 *' requires an explicit cast in local initializer|global u8 *f(in bool b) { u8 *p = b; return p; }"
        "function_pointer|implicit conversion from integer 'i32' to pointer 'void ()|typedef void (*callback)();\nglobal void f() { callback c = 1; c(); }"
        "local_variable_zero|${to_pointer} in local initializer|global u8 *f() { u32 zero = 0u32; u8 *p = zero; return p; }"
        "untaken_branch|${to_integer} in local initializer|global u32 f(in u8 *p) { if (0u32) { u32 x = p; return x; } return 0u32; }"
        "unevaluated_operand|${to_integer} in call argument|global uptr f(in u8 *p) { return sizeof(take_integer(p)); }")
    string(FIND "${case}" "|" first)
    string(FIND "${case}" "|" last REVERSE)
    string(SUBSTRING "${case}" 0 ${first} name)
    math(EXPR first "${first} + 1")
    math(EXPR length "${last} - ${first}")
    string(SUBSTRING "${case}" ${first} ${length} expected)
    math(EXPR last "${last} + 1")
    string(SUBSTRING "${case}" ${last} -1 body)
    reject(${name} "${expected}" "${prelude}${body}")
endforeach()
reject(variadic_c_abi "implicit conversion from integer 'i32' to pointer 'const u8 *' requires an explicit cast in call argument"
    "[[abi(\"sysv_abi\")]] i32 printf(in const u8 *format, ...);\nglobal i32 f() { return printf(7); }")
reject(c_abi_argument "implicit conversion from pointer 'u32 *' to integer 'u64' requires an explicit cast in call argument"
    "[[abi(\"sysv_abi\")]] void consume(in u64 value);\nglobal void f(in u32 *p) { consume(p); }")
reject(mips_local "${to_integer} in local initializer"
    "global u32 f(in u8 *p) { u32 x = p; return x; }" -target mips-unknown-elf)

# Static storage, at file and block scope, scalars and brace entries.
set(object "global u32 object;\n")
reject(static_to_integer "implicit conversion from pointer 'u32 *' to integer 'uptr' requires an explicit cast in static initializer"
    "${object}global uptr address = &object;")
reject(static_to_pointer "implicit conversion from integer 'i32' to pointer 'u32 *' requires an explicit cast in static initializer"
    "global u32 *device = 4096;")
reject(static_string "implicit conversion from pointer 'const u8 *' to integer 'u8' requires an explicit cast in static initializer"
    "global u8 letter = \"abc\";")
reject(static_string_entry "implicit conversion from pointer 'const u8 *' to integer 'u8' requires an explicit cast in static initializer"
    "global u8 text[4] = { [0] = \"abc\" };")
reject(static_entry "implicit conversion from pointer 'u32 *' to integer 'uptr' requires an explicit cast in static initializer"
    "${object}struct cell { uptr address; };\nglobal struct cell cells[2] = { [1] = { .address = &object } };")
reject(static_entry_pointer "implicit conversion from integer 'i32' to pointer 'u8 *' requires an explicit cast in static initializer"
    "struct cell { u32 number; u8 *bytes; };\nglobal struct cell value = { 1, 2 };")
reject(block_static "implicit conversion from pointer 'u32 *' to integer 'uptr' requires an explicit cast in static initializer"
    "${object}global uptr f() { static uptr address = &object; return address; }")
reject(block_static_pointer "implicit conversion from integer 'i32' to pointer 'u32 *' requires an explicit cast in static initializer"
    "global u32 *f() { static u32 *device = 4096; return device; }")
# An operand error is reported before the conversion of its result.
reject(static_conditional "conditional pointer/integer operands require an integer constant zero"
    "global i32 value = 1 ? 7 : \"bad\";")

# Labels: only label and bool, implicitly; explicit uptr conversion stays.
set(labels "global void f(in uptr x) { here: ; ")
reject(label_to_integer "implicit conversion from label to integer 'uptr' requires an explicit cast in local initializer"
    "${labels}uptr u = f::here; }")
reject(integer_to_label "implicit conversion from integer 'uptr' to label requires an explicit cast in local initializer"
    "${labels}label l = x; goto l; }")
reject(zero_to_label "implicit conversion from integer 'i32' to label requires an explicit cast in local initializer"
    "${labels}label l = 0; }")
reject(label_to_pointer "implicit conversion from label to pointer 'u8 *' is not allowed in local initializer"
    "${labels}u8 *p = f::here; }")
reject(label_argument "implicit conversion from label to integer 'uptr' requires an explicit cast in call argument"
    "static void keep(in uptr value) {}\n${labels}keep(f::here); }")
accept(label_casts "global bool f(in uptr x) { here: ; uptr u = (uptr)f::here; label l = (label)x; bool b = f::here; return b && u != 0uptr && l == l; }")

# Generic instances and translation-time helpers apply the rule too, even
# when they are not called.
reject(generic_instance "${to_integer} in local initializer"
    "static u32 narrow<T>(in T value) { u32 x = value; return x; }\nglobal u32 f(in u8 *p) { return narrow(p); }")
accept(generic_integer_instance
    "static u32 narrow<T>(in T value) { u32 x = value; return x; }\nglobal u32 f(in u64 v) { return narrow(v); }")
reject(eval_only_unused "${to_integer} in local initializer"
    "[[eval_only]] static u32 helper(in u8 *p) { u32 x = p; return x; }\nglobal u32 f() { return 1u32; }")
reject(required_argument "implicit conversion from pointer 'const u8 *' to integer 'u32' requires an explicit cast in call argument"
    "static u32 length(in u32 value) { return value; }\n$::static_assert(length(\"abc\") == 3u32, \"no cast\");")

# Null pointer constants, a pointer as a truth value, and explicit casts.
set(valid [=[
typedef void (*callback)();
struct pair { u32 number; u8 *bytes; };
global u32 object;
global u8 *null_object = 0;
global u8 *braced_null = { 0 };
global struct pair null_entry = { 1u32, 0 };
global callback null_callback = 0;
global uptr address = (uptr)&object;
global u32 *device = (u32 *)4096;
global u8 *folded = 1 - 1;
static u32 zero() { return 0u32; }
static u8 *pass(in u8 *p) { return p; }
static void reset(out u8 *p) { p = 0; }
static u32 *pick(in u32 select, in u32 *p) { return select != 0u32 ? p : 0; }
global uptr runtime(in u8 *p, in uptr x, in u32 select) {
    u8 *q = 0;
    q = 0;
    q = zero();
    q = pass(0);
    reset(q);
    callback c = 0;
    bool truth = p;
    bool truth_too = p ? 1 : 0;
    u32 *selected = pick(select, &object);
    selected = select != 0u32 ? selected : 0;
    struct pair entry = { .bytes = 0 };
    u8 *view = (u8 *)x;
    u32 narrow = (u32)(uptr)p;
    if (q == 0 && c == 0 && truth && truth_too && entry.bytes == 0)
        return (uptr)view + narrow + (uptr)selected;
    return 0;
}
static u8 *null_result() { return 0; }
$::static_assert(null_result() == 0, "a null constant at translation time");
]=])
accept(valid_x86_64 "${valid}")
accept(valid_mips "${valid}" -target mips-unknown-elf)
accept(valid_noeval "${valid}" -target x86_64-unknown-linux-gnu -fno-eval-calls)

# A null pointer constant is an integer constant zero or such an expression
# cast to `void *`; it converts to every pointer type, function pointers
# included, compares with any pointer, and gives a `?:` the other arm's type.
set(incompatible "implicit pointer conversion discards qualifiers or uses incompatible pointee types")
set(callbacks "typedef u32 (*callback)(in u32 value);\nstatic u32 twice(in u32 value) { return value * 2u32; }\n")
reject(void_cast_variable "${incompatible} in local initializer"
    "${callbacks}global u32 f(in uptr x) { callback c = (void *)x; return c(1u32); }")
reject(void_object_null "${incompatible} in local initializer"
    "${callbacks}global u32 f() { void *none = 0; callback c = none; return c(1u32); }")
reject(void_cast_qualified "${incompatible} in local initializer"
    "${callbacks}global u32 f() { callback c = (const void *)0; return c(1u32); }")
reject(void_cast_twice "${incompatible} in local initializer"
    "${callbacks}global u32 f() { callback c = (void *)(void *)0; return c(1u32); }")
reject(void_cast_argument "${incompatible} in call argument"
    "${callbacks}static u32 run(in callback c) { return c(1u32); }\nglobal u32 f(in uptr x) { return run((void *)x); }")
reject(void_cast_comparison "pointer comparison requires compatible pointer types or a null pointer constant"
    "${callbacks}global bool f(in callback c, in uptr x) { return c == (void *)x; }")
reject(incompatible_comparison "pointer comparison requires compatible pointer types or a null pointer constant"
    "global bool f(in u32 *a, in u8 *b) { return a == b; }")
reject(void_cast_conditional "conditional pointer operands have no compatible common type"
    "${callbacks}global callback f(in u32 s, in uptr x) { return s != 0u32 ? twice : (void *)x; }")
reject(static_void_object "${incompatible} in static initializer"
    "${callbacks}global void *none = 0;\nglobal callback c = none;")
reject(void_cast_explicit "explicit cast cannot convert between a function pointer and an object pointer in cast"
    "${callbacks}global u32 f(in uptr x) { callback c = (callback)(void *)x; return c(1u32); }")
reject(void_cast_label "implicit conversion from pointer 'void *' to label is not allowed in local initializer"
    "global void f() { here: ; label l = (void *)0; }")
set(nulls [=[
#define NULL ((void *)0)
typedef u32 (*callback)(in u32 value);
struct ops { callback first; callback second; u8 *bytes; };
static u32 twice(in u32 value) { return value * 2u32; }
static u32 zero() { return 0u32; }
global callback none = NULL;
global callback table[3] = { twice, NULL, (void *)(1 - 1) };
global struct ops operations = { .first = NULL, .second = twice, .bytes = NULL };
global callback cast_none = (callback)NULL;
static u32 run(in callback c) { return c != NULL && NULL != c ? c(3u32) : 0u32; }
static callback choose(in u32 s) { return s != 0u32 ? twice : NULL; }
static callback choose_too(in u32 s) { return s != 0u32 ? NULL : twice; }
global u32 f(in u32 s, in u32 *p) {
    static callback cached = NULL;
    callback c = NULL;
    c = (void *)0;
    c = (void *)zero();
    struct ops local = { NULL, twice, NULL };
    callback literal = (callback){ NULL };
    callback cast = (callback)NULL;
    u32 value = *(s != 0u32 ? p : NULL);
    return run(c) + run(NULL) + run(choose(s)) + run(choose_too(s)) + run(cached) +
        run(local.first) + run(literal) + run(cast) + value + (c == NULL ? 1u32 : 0u32);
}
]=])
accept(null_constants_x86_64 "${nulls}")
accept(null_constants_mips "${nulls}" -target mips-unknown-elf)
accept(null_constants_noeval "${nulls}" -target x86_64-unknown-linux-gnu -fno-eval-calls)

# Static initializers follow the rules of initializers in a block:
# qualifiers, pointee compatibility, and function/object pointer separation.
foreach(case
        "static_const|global const u32 fixed = 1u32;\nglobal u32 *writable = &fixed;"
        "static_string_pointer|global u8 *text = \"abc\";"
        "static_volatile|global volatile u32 port;\nglobal u32 *plain = &port;"
        "static_atomic|global u32 [[atomic]] counter;\nglobal u32 *plain = &counter;"
        "static_pointee|global u32 object;\nglobal u8 *bytes = &object;"
        "static_object_to_function|global u32 object;\ntypedef void (*callback)();\nglobal callback code = &object;"
        "static_function_to_object|global void function() {}\nglobal void *erased = &function;"
        "static_entry_const|global const u32 fixed = 1u32;\nstruct cell { u32 *p; };\nglobal struct cell cells[1] = { { &fixed } };"
        "block_static_const|global u32 f() { static const u32 fixed = 2u32; static u32 *p = &fixed; return *p; }")
    string(FIND "${case}" "|" split)
    string(SUBSTRING "${case}" 0 ${split} name)
    math(EXPR split "${split} + 1")
    string(SUBSTRING "${case}" ${split} -1 body)
    reject(${name} "${incompatible} in static initializer" "${body}")
endforeach()
set(static_valid [=[
global u32 object;
global const u32 *view = &object;
global void *erased = &object;
global const u8 *text = "abc";
global volatile u32 *port = &object;
global u32 f() { static const u32 *cached = &object; return *cached; }
]=])
accept(static_valid_x86_64 "${static_valid}")
accept(static_valid_mips "${static_valid}" -target mips-unknown-elf)
