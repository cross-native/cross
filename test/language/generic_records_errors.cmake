# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

# Each case must fail with the message at the given line and column, and
# with every further expected diagnostic line.
function(reject name location expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    execute_process(COMMAND "${CC}" -target x86_64-unknown-linux-gnu -S -O0 ${FLAGS}
        "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
    if(status EQUAL 0 OR NOT err MATCHES "${name}.x:${location}: error: [^\n]*${expected}")
        message(FATAL_ERROR "${name}: missing '${expected}' at ${location}\n${out}\n${err}")
    endif()
    foreach(line IN LISTS ALSO)
        if(NOT err MATCHES "${name}.x:${line}")
            message(FATAL_ERROR "${name}: missing '${line}'\n${err}")
        endif()
    endforeach()
endfunction()

# Each case must compile.
function(accept name source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    execute_process(COMMAND "${CC}" -target x86_64-unknown-linux-gnu -S -O0
        "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name}: expected success\n${out}\n${err}")
    endif()
endfunction()

set(records [=[
struct list<T> { T *items; uptr count; };
struct pair<A, B> { A first; B second; };
struct buf<T, uptr N> { T data[N]; };
]=])

# Different argument lists denote unrelated types, even with equal layouts.
reject(distinct_values 5:62 "a record value requires the same nominal record type in assignment" "${records}
global void f(struct list<u32> *a, struct list<i32> *b) { *a = *b; }
")
reject(distinct_pointers 5:58 "incompatible pointee types in return" "${records}
global struct list<i32> *f(struct list<u32> *a) { return a; }
")
reject(distinct_extents 5:68 "a record value requires the same nominal record type in assignment" "${records}
global void f(struct buf<u8, 4> *a, struct buf<u8, 2 + 3> *b) { *a = *b; }
")
accept(equal_extents "${records}
global void f(struct buf<u8, 4> *a, struct buf<u8, 2 + 2> *b) { *a = *b; }
")

# Every use names all arguments, of the kinds the parameters declare.
reject(missing_argument 5:15 "'struct pair' takes 2 generic arguments, but 1 was given" "${records}
global struct pair<u32> *p;
")
reject(extra_argument 5:15 "'struct list' takes 1 generic argument, but 2 were given" "${records}
global struct list<u32, u8> *p;
")
reject(no_arguments 5:15 "generic record 'list' requires an argument list" "${records}
global struct list *p;
")
reject(empty_arguments 5:22 "a generic argument list cannot be empty" "${records}
global struct list<> *p;
")
reject(type_for_value 5:15 "generic value parameter 'N' of 'struct buf' requires a value argument" "${records}
global struct buf<u8, u16> *p;
")
reject(value_for_type 5:15 "generic type parameter 'T' of 'struct buf' requires a type argument" "${records}
global struct buf<4, 4> *p;
")
reject(unrepresentable 2:23 "generic value argument is not representable in parameter type 'u8'" [=[
struct buf<T, u8 N> { T data[N]; };
global struct buf<u8, 300> *p;
]=])
reject(not_generic 2:15 "record 'plain' is not generic" [=[
struct plain { u32 x; };
global struct plain<u32> *p;
]=])
reject(undeclared 1:15 "generic record 'nothing' is not declared" [=[
global struct nothing<u32> *p;
]=])

# Definitions and declarations.
reject(empty_parameters 1:15 "a generic parameter list cannot be empty" [=[
struct list<> { u32 x; };
]=])
reject(anonymous 1:8 "an anonymous record cannot be generic" [=[
struct <T> { T x; } value;
]=])
reject(enumeration 1:11 "an enumeration cannot be generic" [=[
enum color<T> { red };
]=])
reject(with_object 1:8 "a generic record definition must be a declaration of its own" [=[
struct list<T> { T *items; } value;
]=])
reject(different_kinds 2:1 "record tag 'list' was previously declared with a different generic parameter list" [=[
struct list<T>;
struct list<T, uptr N> { T data[N]; };
]=])
set(ALSO "1:1: note: earlier declaration is here")
reject(different_value_type 2:1 "record tag 'buf' was previously declared with a different generic parameter list" [=[
struct buf<T, uptr N>;
struct buf<T, u8 N> { T data[N]; };
]=])
unset(ALSO)
reject(not_generic_before 2:1 "record tag 'list' was previously declared without generic parameters" [=[
struct list;
struct list<T> { T *items; };
]=])
reject(duplicate_definition 2:1 "duplicate definition of record 'list'" [=[
struct list<T> { T *items; };
struct list<U> { U *items; };
]=])
reject(named_nested 1:26 "a generic record cannot define a named record or union in its member list" [=[
struct outer<T> { struct inner { T v; } i; };
]=])
reject(nested_enumeration 1:19 "a generic record cannot define an enumeration in its member list" [=[
struct outer<T> { enum { first, second } e; T v; };
]=])

reject(different_redeclaration 5:8 "generic declarations of 'last' have incompatible interfaces after substitution" "${records}
static T last<T, uptr N>(in struct buf<T, N> *b);
global u8 f(struct buf<u8, 4> *b) { return last<u8, 4>(b); }
static T last<T, uptr N>(in struct buf<T, N + 1> *b) { return b->data[N - 1]; }
")

# Instances are checked like written records.
set(ALSO "2:15: note: in instance 'struct box<void>'")
reject(incomplete_member 1:17 "record member has an incomplete or non-object type" [=[
struct box<T> { T v; };
global struct box<void> value;
]=])
unset(ALSO)
reject(contains_itself 1:17 "record contains itself by value through a member cycle" [=[
struct box<T> { struct box<T> v; };
global struct box<u8> value;
]=])
reject(depth_budget 1:30 "nesting depth limit 128" [=[
struct grow<T> { T v; struct grow<T *> *next; };
global struct grow<u8> root;
]=])
set(FLAGS -fgeneric-instance-limit=2)
reject(instance_budget 4:15 "instance limit 2" [=[
struct box<T> { T v; };
global struct box<u8> a;
global struct box<u16> b;
global struct box<u32> c;
]=])
unset(FLAGS)

# Deduction binds type arguments structurally; value parameters are explicit.
reject(conflicting_deduction 6:64 "conflicting deductions for generic type parameter 'T'" "${records}
static T pick<T>(in struct list<T> a, in T b) { return b; }
global u16 f(in struct list<u32> l, in u16 v) { return pick(l, v); }
")
reject(value_not_deduced 6:44 "generic value parameter 'N' requires an explicit argument" "${records}
static u8 at<T, uptr N>(in struct buf<T, N> *b) { return b->data[0]; }
global u8 f(struct buf<u8, 4> *b) { return at(b); }
")
reject(not_an_instance 6:50 "generic type deduction requires an exact structural match" "${records}
static T first<T>(in struct list<T> l) { return l.items[0]; }
global u32 f(struct list<u32> *l) { return first(l); }
")
