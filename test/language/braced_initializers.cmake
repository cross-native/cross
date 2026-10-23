# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Braces around a scalar's value or a u8 array's string: malformed braces in
# each context that checks an initializer, and the code of a braced register
# initializer and a braced function-pointer initializer.
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

# `location` is the line and column the diagnostic names.
function(reject name location expected source)
    compile(${name} "${source}" ${ARGN})
    string(FIND "${diagnostics}" "${name}.x:${location}: error: ${expected}" position)
    if(status EQUAL 0 OR position EQUAL -1)
        message(FATAL_ERROR "${name}: missing '${location}: error: ${expected}'\n${diagnostics}")
    endif()
endfunction()

set(empty "empty scalar initializer")
set(excess "excess entry in scalar initializer")
set(nested "too many braces around scalar initializer")
set(string_excess "excess entry in u8 array initializer")
set(fit "string initializer does not fit in the u8 array")
set(pair "struct pair { u32 a; u32 b; };\n")
set(text "struct text { u8 bytes[3]; };\n")

# Automatic, block-static, and file-scope scalars.
reject(automatic_empty 1:26 "${empty}" "global u32 f() { u32 x = { }; return x; }")
reject(automatic_excess 1:34 "${excess}" "global u32 f() { u32 x = { 1u32, 2u32 }; return x; }")
reject(automatic_nested 1:28 "${nested}" "global u32 f() { u32 x = { { 1u32 } }; return x; }")
reject(automatic_member_designator 1:28 "member designator requires a record destination"
    "global u32 f() { u32 x = { .a = 1u32 }; return x; }")
reject(automatic_index_designator 1:28 "array designator requires an array destination"
    "global u32 f() { u32 x = { [0] = 1u32 }; return x; }")
reject(block_static_excess 1:41 "${excess}" "global u32 f() { static u32 x = { 1u32, 2u32 }; return x; }")
reject(file_empty 1:16 "${empty}" "global u32 g = { };")
reject(file_nested 1:18 "${nested}" "global u32 g = { { 1u32 } };")
reject(file_nested_mips 1:18 "${nested}" "global u32 g = { { 1u32 } };" -target mips-unknown-elf)
reject(file_designator 1:18 "array designator requires an array destination" "global u32 g = { [0] = 1u32 };")

# Scalar members, compound literals, translation-time and generic code.
reject(member_excess 2:44 "${excess}" "${pair}global u32 f() { struct pair p = { { 1u32, 2u32 }, 3u32 }; return p.a; }")
reject(member_empty 2:36 "${empty}" "${pair}global u32 f() { struct pair p = { { }, 3u32 }; return p.a; }")
reject(file_member_nested 2:28 "${nested}" "${pair}global struct pair g = { { { 1u32 } }, 3u32 };")
reject(literal_empty 1:30 "${empty}" "global u32 f() { return (u32){ }; }")
reject(literal_excess 1:38 "${excess}" "global u32 f() { return (u32){ 1u32, 2u32 }; }")
reject(file_literal_excess 1:29 "${excess}" "global u32 g = (u32){ 1u32, 2u32 };")
reject(evaluated_excess 1:34 "${excess}"
    "static u32 h() { u32 x = { 1u32, 2u32 }; return x; } global u32 g = $::eval(h());")
reject(generic_nested 1:36 "${nested}"
    "static T copy<T>(in T v) { T x = { { v } }; return x; } global u32 f() { return copy(1u32); }")

# A string in braces initializes a whole u8 array: nothing may follow it, and
# the array must hold its bytes and terminating zero.
reject(string_excess 1:36 "${string_excess}" "global u32 f() { u8 s[] = { \"abc\", \"d\" }; return s[0]; }")
reject(string_excess_byte 1:37 "${string_excess}" "global u32 f() { u8 s[8] = { \"abc\", 1u8 }; return s[0]; }")
reject(file_string_excess 1:26 "${string_excess}" "global u8 s[] = { \"abc\", \"d\" };")
reject(file_member_string_excess 2:33 "${string_excess}" "${text}global struct text t = { { \"a\", \"b\" } };")
reject(literal_string_excess 1:40 "${string_excess}" "global u32 f() { return ((u8[]){ \"ab\", \"c\" })[0]; }")
reject(string_fit 1:30 "${fit}" "global u32 f() { u8 s[3] = { \"abc\" }; return s[0]; }")
reject(file_string_fit 1:20 "${fit}" "global u8 s[3] = { \"abc\" };")
reject(file_string_fit_mips 1:20 "${fit}" "global u8 s[3] = { \"abc\" };" -target mips-unknown-elf)
reject(member_string_fit 2:38 "${fit}" "${text}global u32 f() { struct text t = { { \"abc\" } }; return t.bytes[0]; }")
reject(literal_string_fit 1:35 "${fit}" "global u32 f() { return ((u8[3]){ \"abc\" })[0]; }")

# A vector is neither an aggregate nor a scalar.
reject(vector 2:25 "brace initializer requires an aggregate or scalar destination"
    "typedef u32 v4 [[vector_size(16)]];\nglobal u32 f() { v4 v = { 1u32 }; return v[0]; }")

# A braced value of a raw register object in a naked x86-64 function.
compile(raw_register [=[
[[naked]] static void seed(out u64 value "r9") {
    register u64 initial "r10" = { 37u64 };
    $::_mov(value, initial);
    $::_ret();
}
global u64 f() { u64 v; seed(v); return v; }
]=])
file(READ "${OUTPUT}/raw_register.s" raw_register)
if(NOT status EQUAL 0 OR NOT raw_register MATCHES "movabsq[ \t]+\\$37, %r10")
    message(FATAL_ERROR "raw_register: no braced register initializer\n${diagnostics}\n${raw_register}")
endif()

# A braced function designator converts to the pointer's interface through
# an adapter, as an unbraced one does.
compile(adapter [=[
typedef i32 (*ms_callback)(in i32) [[abi("ms_abi")]];
static i32 add_one(in i32 value) { return value + 1i32; }
global ms_callback braced = { add_one };
global i32 call(in i32 v) { ms_callback local = { add_one }; return local(v); }
]=])
file(READ "${OUTPUT}/adapter.s" adapter)
string(REGEX MATCHALL "leaq[ \t]+[^ \t\n]*adapter[^ \t\n]*" adapter_uses "${adapter}")
list(LENGTH adapter_uses adapter_count)
if(NOT status EQUAL 0 OR NOT adapter MATCHES "\\.quad[ \t]+[^ \t\n]*adapter" OR adapter_count LESS 1)
    message(FATAL_ERROR "adapter: braced function pointers bypass the adapter\n${diagnostics}\n${adapter}")
endif()
