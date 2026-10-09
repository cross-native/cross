# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Diagnostics and compile-time checks for member offsets, incomplete arrays,
# for-clause lists, exhaustive records, fixed addresses, attribute regions,
# and function-pointer casts.
foreach(required CC SOURCE_DIR OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

# compile(name source [cc arguments...]) writes the source and compiles it.
function(compile name source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}\n")
    if(NOT ARGN)
        set(ARGN -target x86_64-unknown-linux-gnu)
    endif()
    execute_process(COMMAND "${CC}" -S ${ARGN} "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(status "${status}" PARENT_SCOPE)
    set(diagnostics "${out}${err}" PARENT_SCOPE)
endfunction()

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

# $::offsetof
set(record "struct inner { u16 x; };\nstruct s { u8 a; u32 b : 3; u32 list[2]; struct inner nested; };\nstruct opaque;")
reject(offsetof_bit_field "$::offsetof cannot be applied to a bit-field"
    "${record}\nglobal uptr f() { return $::offsetof(struct s, b); }")
reject(offsetof_member "record has no member named 'missing'"
    "${record}\nglobal uptr f() { return $::offsetof(struct s, nested.missing); }")
reject(offsetof_incomplete "$::offsetof requires a complete record type"
    "${record}\nglobal uptr f() { return $::offsetof(struct opaque, a); }")
reject(offsetof_scalar "$::offsetof requires a complete record type"
    "${record}\nglobal uptr f() { return $::offsetof(u32, a); }")
reject(offsetof_range "$::offsetof index is out of range"
    "${record}\nglobal uptr f() { if (0u32) { return $::offsetof(struct s, list[2]); } return 0; }")
reject(offsetof_index "$::offsetof index requires an array member"
    "${record}\n$::static_assert($::offsetof(struct s, a[0]) == 0uptr, \"index\");")
reject(offsetof_runtime "runtime local or parameter is not a translation-time value"
    "${record}\nglobal uptr f(in u32 i) { return $::offsetof(struct s, list[i]); }")
reject(offsetof_designator "$::offsetof member designator requires a record"
    "${record}\nglobal uptr f() { return $::offsetof(struct s, list.x); }")
accept(offsetof_constants
    "${record}\n$::static_assert($::offsetof(struct s, nested.x) == 12uptr, \"nested\");\nu8 bytes[$::offsetof(struct s, list[1])];\nenum e { at = $::offsetof(struct s, list) };\nuptr twice<uptr N>() { return N * 2uptr; }\nglobal uptr f() { return twice<$::offsetof(struct s, nested)>() + sizeof(bytes) + at; }")

# Incomplete array declarations
reject(array_size "sizeof requires a complete object type with fixed size"
    "u32 table[];\nglobal uptr f() { return sizeof(table); }")
reject(array_static "an object definition with an omitted array bound requires an initializer"
    "static u32 table[];")
reject(array_global "an object definition with an omitted array bound requires an initializer"
    "global u32 table[];")
reject(array_element "incompatible redeclaration of object 'table'"
    "u32 table[];\nglobal u16 table[] = {1, 2};")
file(WRITE "${OUTPUT}/array_use.x" "u32 table[];\nglobal uptr count() { return sizeof(table) / sizeof(table[0]); }\nglobal u32 *second() { return &table[1]; }\n")
file(WRITE "${OUTPUT}/array_def.x" "global u32 table[] = {1, 2, 3};\n")
execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu
    "${OUTPUT}/array_use.x" "${OUTPUT}/array_def.x" -o "${OUTPUT}/array_group.s"
    RESULT_VARIABLE status ERROR_VARIABLE err)
if(NOT status EQUAL 0)
    message(FATAL_ERROR "a definition in the group did not complete the array\n${err}")
endif()
execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu
    "${OUTPUT}/array_use.x" -o "${OUTPUT}/array_alone.s"
    RESULT_VARIABLE status ERROR_VARIABLE err)
if(status EQUAL 0 OR NOT err MATCHES "array_use\\.x:2:[0-9]+: error: sizeof requires a complete object type")
    message(FATAL_ERROR "an imported incomplete array allowed sizeof\n${err}")
endif()

# Expression lists in for clauses
reject(for_condition "expected ';'"
    "global u32 f() { u32 i = 0; u32 j = 0; for (; i < 3, j < 2; ++i) {} return i; }")
reject(for_trailing "expected expression"
    "global u32 f() { u32 i; for (i = 0,; i < 3; ++i) {} return i; }")

# exhaustive
set(points "struct point [[exhaustive]] { i32 x; i32 y; };\nstruct plain { struct point at; u32 extra; };")
reject(exhaustive_missing "initializer of exhaustive record 'point' does not name member 'y'"
    "${points}\nstruct point origin = { .x = 0 };")
reject(exhaustive_positional "initializer of exhaustive record 'point' does not name member 'y'"
    "${points}\nglobal i32 f() { struct point p = { 1 }; return p.x; }")
reject(exhaustive_nested "initializer of exhaustive record 'point' does not name member 'x'"
    "${points}\nstruct plain value = { .at = { .y = 1 } };")
reject(exhaustive_evaluated "initializer of exhaustive record 'point' does not name member 'y'"
    "${points}\nstruct point make() { struct point p = { .x = 1 }; return p; }\nglobal i32 f() { return $::eval(make()).x; }")
reject(exhaustive_union "attribute 'exhaustive' is not valid on a union"
    "union u [[exhaustive]] { u8 a; u32 b; };")
reject(exhaustive_object "attribute 'exhaustive' is not valid on an object"
    "global u32 value [[exhaustive]];")
reject(exhaustive_argument "exhaustive does not take arguments"
    "struct s [[exhaustive(1)]] { u32 a; };")
reject(exhaustive_declaration "attribute 'exhaustive' requires a complete record definition"
    "struct s [[exhaustive]];")
accept(exhaustive_complete
    "${points}\nstruct point origin = { .y = 0, .x = 0 };\nstruct plain loose = { .extra = 1 };\nstruct point both[2] = { { 1, 2 }, { .x = 3, .y = 4 } };")

# address(N)
reject(address_initializer "address requires an object declaration that is not a definition"
    "u32 cell [[address(0x1000)]] = 5;")
reject(address_definition "address requires an object declaration that is not a definition"
    "static u32 cell [[address(0x1000)]];")
reject(address_body "address requires a function declaration without a body"
    "u32 f() [[address(0x1000)]] { return 1; }")
reject(address_defined "object 'cell' has a fixed address and cannot also be defined"
    "u32 cell [[address(0x1000)]];\nglobal u32 cell = 3;")
reject(address_function_defined "function 'f' has a fixed address and cannot also be defined"
    "u32 f() [[address(0x1000)]];\nu32 f() { return 1; }")
reject(address_zero "address requires a positive integer constant that fits the target address width"
    "u32 cell [[address(0)]];")
reject(address_negative "address requires a positive integer constant that fits the target address width"
    "u32 cell [[address(-4)]];")
reject(address_width "address requires a positive integer constant that fits the target address width"
    "u32 cell [[address(0x100000000)]];\nglobal u32 f() { return cell; }"
    -target mips-unknown-elf -mabi=o32)
reject(address_arguments "address requires one integer argument"
    "u32 cell [[address(1, 2)]];")
reject(address_conflict "conflicting address attributes"
    "u32 cell [[address(0x1000)]];\nu32 cell [[address(0x2000)]];")
reject(address_weakref "address cannot be combined with alias, weakref, or thread_local"
    "u32 cell [[address(0x1000), weakref(\"other\")]];")
reject(address_typedef "attribute 'address' is not valid on a typedef"
    "typedef u32 word [[address(4)]];")
# References use the address itself: no symbol names the entity.
accept(address_code
    "u32 device_cell [[address(0x10000000)]];\nu32 device_entry(in u32 code) [[address(0x10000100)]];\nglobal u32 poll() { device_cell = 1; return device_entry(device_cell); }\nglobal u32 *where = &device_cell;"
    -O2 -target x86_64-unknown-linux-gnu)
file(READ "${OUTPUT}/address_code.s" assembly)
if(assembly MATCHES "device_cell|device_entry" OR NOT assembly MATCHES "268435456" OR
   NOT assembly MATCHES "268435712")
    message(FATAL_ERROR "fixed-address references did not use their addresses\n${assembly}")
endif()

# Attribute regions
reject(region_unused "attribute 'packed' of this region is valid for none of its declarations"
    "[[packed]] { u32 f(); }")
reject(region_empty "attribute 'abi' of this region is valid for none of its declarations"
    "[[abi(\"sysv_abi\")]] {}")
reject(region_unknown "unknown attribute 'bogus'"
    "[[bogus]] { u32 f(); }")
reject(region_statement "attribute 'aligned' is not valid on this statement"
    "global u32 f() { [[aligned(8)]] { } return 0; }")
set(ms "typedef u32 (*ms_unary)(in u32 value) [[abi(\"ms_abi\")]];")
reject(region_typedef "a function-pointer value cannot change its callable ABI"
    "[[abi(\"sysv_abi\")]] { typedef u32 (*unary)(in u32 value); }\n${ms}\nglobal ms_unary f(in unary value) { return value; }")
accept(region_inner_attribute
    "[[abi(\"sysv_abi\")]] { typedef u32 (*unary)(in u32 value) [[abi(\"ms_abi\")]]; }\n${ms}\nglobal ms_unary f(in unary value) { return value; }")
accept(region_nested
    "[[abi(\"sysv_abi\")]] { [[abi(\"ms_abi\")]] { typedef u32 (*unary)(in u32 value); } }\n${ms}\nglobal ms_unary f(in unary value) { return value; }")
accept(region_namespace
    "[[section(\".boot\")]] { namespace boot { global u32 value = 1; global u32 read() { return value; } } }")
file(READ "${OUTPUT}/region_namespace.s" assembly)
string(REGEX MATCHALL "\\.section \"?\\.boot" placed "${assembly}")
list(LENGTH placed sections)
if(NOT sections EQUAL 2)
    message(FATAL_ERROR "a region section did not reach both definitions\n${assembly}")
endif()

# Function-pointer casts
set(callbacks "typedef u32 (*narrow)(in u32 value);\ntypedef u64 (*wide)(in u64 value);")
accept(cast_function_pointer
    "${callbacks}\nglobal u32 apply(in u32 value) { return value + 1; }\nglobal wide erased = (wide)apply;\nglobal uptr code = (uptr)apply;\nglobal u32 call(in wide value) { narrow restored = (narrow)value; return restored(1) + ((narrow)code)(2); }")
reject(cast_implicit "implicit pointer conversion discards qualifiers or uses incompatible pointee types"
    "${callbacks}\nglobal wide f(in narrow value) { return value; }")
reject(cast_object "explicit pointer conversion discards qualifiers or uses incompatible pointee types"
    "${callbacks}\nglobal void *f(in narrow value) { return (void *)value; }")

# Parsed syntax captures use the same grammar for offsetof and for-clause lists.
accept(syntax_capture [=[
namespace flow {
    [[syntax_expander]]
    static $::meta::tokens expand_unless($::meta::syntax_match input) {
        $::meta::syntax condition = $::syntax::node(input, "condition");
        $::meta::syntax body = $::syntax::node(input, "body");
        return $::quote { if (!$::unquote(condition)) $::unquote(body) };
    }
    syntax unless : statement {
        prefix "unless";
        match "(" condition:expr ")" body:stmt;
        expand expand_unless;
    }
}
struct s { u32 a; u32 b; };
global u32 f(u32 value) {
    syntax flow::unless;
    u32 i;
    u32 j;
    unless ($::offsetof(struct s, b) == 0) {
        for (i = 0, j = 4; i < j; ++i, --j) value += i;
    }
    return value;
}
]=])
