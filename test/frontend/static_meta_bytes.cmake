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
elseif(NOT MODE STREQUAL native)
    message(FATAL_ERROR "unknown mode '${MODE}'")
endif()
set(prefix [=[
static $::meta::bytes bytes(in uptr count) {
    $::meta::buffer buffer = $::meta::alloc(count);
    u8 *data = $::meta::data(buffer);
    for (uptr i = 0uptr; i < count; ++i) data[i] = (u8)i;
    return $::meta::freeze(buffer, count);
}
]=])
function(check name expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${prefix}\n${source}")
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

check(ordinary pass "global uptr entry() { static const u8 a[] = bytes(3uptr); return sizeof(a); }")
check(ordinary_type_dependency pass "global uptr entry() { u32 local[2]; static const u8 a[] = bytes(sizeof(local)); typedef u8 Width[sizeof(a)]; return sizeof(Width); }")
check(parameter_type_dependency pass "global uptr entry(in uptr count) { static const u8 a[] = bytes(sizeof(count)); typedef u8 Width[sizeof(a)]; return sizeof(Width); }")
check(fixed_type_dependency pass "global uptr entry() { u8 local[3]; static const u8 a[3] = bytes(sizeof(local)); return sizeof(a); }")
check(generic_producer pass "static $::meta::bytes produce<T>() { return bytes(sizeof(T)); } global uptr entry() { static const u8 a[] = produce<uptr>(); typedef u8 Width[sizeof(a)]; return sizeof(Width); }")
check(tokens_not_bytes "byte|meta|initializer" "static $::meta::tokens inspect(in $::meta::tokens input) { if (0u32) { static const u8 a[] = $::quote {a b}; } return input; } global u32 entry() { return 1u32; }")
check(ordinary_static_type_binding pass "global uptr entry() { static u32 a; typedef u8 Width[sizeof(a)]; return sizeof(Width); }")
check(empty "nonempty initializer" "global uptr entry() { static const u8 a[] = bytes(0uptr); return sizeof(a); }")
check(mismatch "bound must equal" "global uptr entry() { static const u8 a[2] = bytes(3uptr); return sizeof(a); }")
check(missing "omitted array bound requires" "global uptr entry() { static const u8 a[]; return sizeof(a); }")
check(nonbyte "meta|byte" "global uptr entry() { static const u16 a[] = bytes(3uptr); return sizeof(a); }")
check(automatic "meta|initializer" "global uptr entry() { const u8 a[3] = bytes(3uptr); return sizeof(a); }")
check(runtime_parameter "translation-time|constant|runtime" "global uptr entry(in uptr count) { static const u8 a[] = bytes(count); return sizeof(a); }")
check(writable_const "const" "global uptr entry() { static const u8 a[] = bytes(3uptr); a[0uptr] = 1u8; return sizeof(a); }")

foreach(role helper macro expander)
    set(attribute "")
    set(parameter "in $::meta::tokens input")
    set(invocation "[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return inspect(input); } global u32 entry() { return apply!(1u32); }")
    if(role STREQUAL macro)
        set(attribute "[[macro]]")
        set(invocation "global u32 entry() { return inspect!(1u32); }")
    elseif(role STREQUAL expander)
        set(attribute "[[syntax_expander]]")
        set(parameter "in $::meta::syntax_match input")
        set(invocation "syntax Inspect : expression { prefix \"inspect\"; match body:paren; expand inspect; } syntax Inspect; global u32 entry() { return inspect(); }")
    endif()
    foreach(use unused invoked)
        set(tail "global u32 entry() { return 1u32; }")
        if(use STREQUAL invoked)
            set(tail "${invocation}")
        endif()
        check(${role}_${use}_untaken pass "${attribute} static $::meta::tokens inspect(${parameter}) { if (0u32) { static const u8 a[] = bytes(3uptr); $::static_assert(sizeof(a) == 3uptr, \"inferred\"); } return $::quote {1u32}; } ${tail}")
        check(${role}_${use}_context pass "${attribute} static $::meta::tokens inspect(${parameter}) { if (0u32) { static const u8 a[] = bytes($::meta::len($::quote {a b})); typedef u8 Width[sizeof(a)]; struct Record { u8 data[sizeof(a)]; }; } return $::quote {1u32}; } ${tail}")
        check(${role}_${use}_dependent pass "${attribute} static $::meta::tokens inspect(${parameter}) { if (0u32) { static const u8 a[] = bytes(3uptr); typedef u8 Width[sizeof(a)]; struct Record { u8 data[sizeof(a)]; }; } return $::quote {1u32}; } ${tail}")
        check(${role}_${use}_invalid "static byte array|invalid target-sized bound" "${attribute} static $::meta::tokens inspect(${parameter}) { if (0u32) { static const u8 a[2] = bytes(3uptr); } return $::quote {1u32}; } ${tail}")
    endforeach()
    check(${role}_executed "static.*storage|static storage" "${attribute} static $::meta::tokens inspect(${parameter}) { static const u8 a[] = bytes(3uptr); return $::quote {1u32}; } ${invocation}")
endforeach()

set(syntax [=[
[[syntax_expander]] static $::meta::tokens discard(in $::meta::syntax_match input) { return $::quote { {} }; }
[[syntax_expander]] static $::meta::tokens preserve(in $::meta::syntax_match input) { return $::quote { $::unquote($::syntax::node(input, "body")) }; }
syntax Discard : statement { prefix "discard"; match body:stmt; expand discard; }
syntax Preserve : statement { prefix "preserve"; match body:stmt; expand preserve; }
]=])
check(discard pass "${syntax} global u32 entry() { syntax Discard; discard { static u8 a[] = bytes(0uptr); } return 1u32; }")
check(survive "nonempty initializer" "${syntax} global u32 entry() { syntax Preserve; preserve { static u8 a[] = bytes(0uptr); } return 1u32; }")
check(runtime_local "translation-time|constant|runtime" "global uptr entry() { uptr count = 3uptr; static const u8 a[] = bytes(count); return sizeof(a); }")
