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
                message(FATAL_ERROR "${name}/${level}: expected success\n${out}\n${err}")
            endif()
            if(name STREQUAL explicit_layout)
                file(READ "${OUTPUT}/${name}-${level}.s" assembly)
                if(NOT assembly MATCHES "\\.p2align[ \t]+5[^\r\n]*[\r\n]+\\.globl ownership_object[\r\n]+\\.(def|type) ownership_object[^\r\n]*[\r\n]+ownership_object:")
                    message(FATAL_ERROR "${name}/${level}: missing independent object alignment in ${OUTPUT}/${name}-${level}.s")
                endif()
            endif()
        elseif(NOT status EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: (error|note):")
            message(FATAL_ERROR "${name}/${level}: missing located '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()

check(ambiguous_object "ambiguous leading 'aligned'" [=[
[[aligned(8)]] struct Record { u8 value; } object;
]=])
check(ambiguous_function "ambiguous leading 'aligned'" [=[
[[aligned(32)]] global struct Result { u8 value; } make() {
    struct Result result = { 1u8 }; return result;
}
]=])
foreach(attribute packed "aligned(8)")
    string(MAKE_C_IDENTIFIER "${attribute}" case)
    check(ambiguous_member_${case} "ambiguous leading" "struct Outer { [[${attribute}]] struct Inner { u8 byte; u32 word; } member; };")
endforeach()
check(ambiguous_typedef "ambiguous leading 'aligned'" [=[
typedef [[aligned(8)]] struct Record { u8 value; } Alias;
]=])
check(ambiguous_local "ambiguous leading 'aligned'" [=[
global u32 entry() { [[aligned(8)]] struct Record { u8 value; } object; return 1u32; }
]=])
check(ambiguous_pointer "ambiguous leading 'aligned'" [=[
[[aligned(8)]] struct Record { u8 value; } *pointer;
]=])
check(ambiguous_anonymous "ambiguous leading 'aligned'" [=[
[[aligned(8)]] union { u8 byte; u32 word; } object;
]=])
check(ambiguous_generated "ambiguous leading 'aligned'" [=[
[[macro]] static $::meta::tokens copy(in $::meta::tokens input) { return input; }
copy!([[aligned(8)]]) struct Record { u8 value; } copy!(object);
]=])
check(ambiguous_mixed_list "ambiguous leading 'aligned'" [=[
[[aligned(8)]] struct Record { u8 value; } function(), object;
]=])
check(packed_helper "attribute 'packed' is not valid on a function" [=[
[[packed]] static $::meta::tokens helper(in $::meta::tokens input) { return input; }
global u32 entry() { return 1u32; }
]=])
check(packed_called_helper "attribute 'packed' is not valid on a function" [=[
static $::meta::tokens helper(in $::meta::tokens input) [[packed]] { return input; }
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
global u32 entry() { return apply!(1u32); }
]=])
check(packed_object "attribute 'packed' is not valid on an object" [=[
global struct Record { u8 value; } object [[packed]];
]=])
check(underlying_object "attribute 'underlying' is not valid on an object" [=[
global enum Enumeration { Value = 1u32 } object [[underlying(u16)]];
]=])
check(underlying_function "attribute 'underlying' is not valid on a function" [=[
static enum Enumeration { Value = 1u32 } helper() [[underlying(u16)]] { return Value; }
global u32 entry() { return (u32)helper(); }
]=])
foreach(attribute packed "aligned(8)" "underlying(u16)")
    string(MAKE_C_IDENTIFIER "${attribute}" case)
    check(parameter_${case} "not valid on a parameter" "global u32 entry(in u32 value [[${attribute}]]) { return value; }")
    check(parameter_prefix_${case} "not valid on a parameter" "global u32 entry([[${attribute}]] in u32 value) { return value; }")
endforeach()

check(explicit_layout pass [=[
struct TypeAligned [[aligned(32)]] { u8 value; } type_object;
[[link_name("ownership_object")]] global struct ObjectAligned { u8 value; } object [[aligned(32)]];
struct MemberOnly { u8 first; struct Inner { u8 byte; u32 word; } member [[packed]]; };
struct TypeOnly { u8 first; struct PackedInner [[packed]] { u8 byte; u32 word; } member; };
struct MemberAligned { struct Small { u8 value; } member [[aligned(16)]]; };
[[packed]] struct PrefixPacked { u8 byte; u32 word; } packed_object;
typedef [[packed]] struct AliasRecord { u8 byte; u32 word; } Alias;
typedef struct [[packed]] { u8 byte; u32 word; } Anonymous;
[[packed]] union PrefixUnion { u8 byte; u32 word; } union_object;
[[aligned(16), packed, aligned(32)]] struct Standalone { u8 byte; u32 word; };
[[underlying(u16)]] enum EnumObject { Enumerator = 1u16 } enum_object;
[[aligned(16)]] struct PrototypeResult { u8 value; } prototype(), other_prototype();
[[packed]] static struct Result { u8 byte; u32 word; } make() [[aligned(32)]] {
    struct Result result = { 1u8, 2u32 }; return result;
}
static u32 parameter(in [[aligned(16)]] struct Parameter { u8 value; } value) { return value.value; }
$::static_assert(sizeof(struct TypeAligned) == 32uptr, "type alignment");
$::static_assert(sizeof(struct ObjectAligned) == 1uptr, "object must not align type");
$::static_assert(sizeof(struct Inner) == 8uptr, "member packing must not pack type");
$::static_assert(sizeof(struct MemberOnly) == 9uptr, "packed outer member");
$::static_assert(sizeof(struct PackedInner) == 5uptr && sizeof(struct TypeOnly) == 6uptr, "type packing");
$::static_assert(sizeof(struct Small) == 1uptr && sizeof(struct MemberAligned) == 16uptr, "member alignment");
$::static_assert(sizeof(struct PrefixPacked) == 5uptr && sizeof(Alias) == 5uptr, "unambiguous packed");
$::static_assert(sizeof(Anonymous) == 5uptr && $::alignof(union PrefixUnion) == 1uptr, "anonymous and union layout");
$::static_assert(sizeof(struct Standalone) == 32uptr, "standalone leading alignment");
$::static_assert(sizeof(enum EnumObject) == 2uptr, "enum-only ownership");
$::static_assert(sizeof(struct PrototypeResult) == 16uptr, "prototype has no entry-alignment target");
$::static_assert(sizeof(struct Result) == 5uptr, "function alignment must not affect result layout");
$::static_assert(sizeof(struct Parameter) == 16uptr, "parameter tag ownership");
global u32 entry() {
    [[packed]] struct Local { u8 byte; u32 word; } local;
    [[aligned(16), packed]] struct LocalTag { u8 byte; u32 word; };
    $::static_assert(sizeof(local) == 5uptr && sizeof(struct LocalTag) == 16uptr, "local ownership");
    return make().word;
}
]=])

set(syntax [=[
[[syntax_expander]] static $::meta::tokens keep(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) { return $::quote {}; }
syntax Keep : item { prefix "keep"; match body:function_def; expand keep; }
syntax Discard : item { prefix "discard"; match body:function_def; expand discard; }
syntax Keep, Discard;
]=])
check(discarded_ambiguous pass "${syntax}
discard [[aligned(32)]] static struct Result { u8 value; } make() { struct Result r = { 1u8 }; return r; }
global u32 entry() { return 1u32; }")
check(retained_ambiguous "ambiguous leading 'aligned'" "${syntax}
keep [[aligned(32)]] static struct Result { u8 value; } make() { struct Result r = { 1u8 }; return r; }
global u32 entry() { return 1u32; }")
check(retained_explicit pass "${syntax}
keep static struct Result [[packed]] { u8 byte; u32 word; } make() [[aligned(32)]] {
    struct Result r = { 1u8, 2u32 }; return r;
}
$::static_assert(sizeof(struct Result) == 5uptr, \"retained type layout\");
global u32 entry() { return make().word; }")
