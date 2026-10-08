# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later
foreach(required CC HOST_CXX MODEL OUTPUT MODE)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
set(root "${OUTPUT}")
file(MAKE_DIRECTORY "${root}")
function(check name expected source)
    file(WRITE "${root}/${name}.x" "${source}")
    foreach(level O0 O2)
        execute_process(COMMAND "${CC}" -S -${level} -fno-eval-calls ${ARGN}
            "${root}/${name}.x" -o "${root}/${name}-${level}.s"
            RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
        if(expected STREQUAL "accept")
            if(NOT result EQUAL 0)
                message(FATAL_ERROR "${name}/${level}: unexpected rejection\n${out}\n${err}")
            endif()
        elseif(NOT result EQUAL 1 OR NOT err MATCHES "${expected}" OR
               NOT err MATCHES "${name}.x:[0-9]+:[0-9]+: error:")
            message(FATAL_ERROR "${name}/${level}: missing '${expected}'\n${out}\n${err}")
        endif()
    endforeach()
endfunction()

set(positive [=[
namespace Types { typedef u64 Word; typedef Word *Words; }
static T identity<T>(in T value) { return value; }
[[syntax_expander]] static $::meta::tokens hold(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "definition")) };
}
syntax Hold : item { prefix "hold_variadic"; match definition:function_def; expand hold; }
syntax Hold;

hold_variadic [[abi("@ABI@"), noinline, variadic(Types::Words values "@STATE@")]]
static u64 alias_state(in u64 tag, ...) {
    typedef u8 Extent[sizeof(values)];
    $::static_assert(sizeof(Extent) == sizeof(Types::Words), "variadic cell type was lost");
    $::static_assert($::alignof(values) == $::alignof(Types::Words), "variadic cell alignment was lost");
    values += 1u32;
    --values;
    return identity(values)[0];
}
hold_variadic [[abi("@ABI@"), noinline, variadic(T *values "@STATE@")]]
static u64 generic_state<T>(in T tag, ...) {
    typedef u8 Extent[sizeof(values)];
    $::static_assert(sizeof(Extent) == sizeof(T *), "generic state type was not substituted");
    return identity(values)[0];
}
[[macro]] static $::meta::tokens define(in $::meta::tokens input) {
    $::meta::tokens name = $::meta::gensym("values");
    return $::quote {
        hold_variadic [[abi("@ABI@"), noinline, variadic(Types::Words $::unquote(name) "@STATE@")]]
        static u64 fresh_state(in u64 tag, ...) {
            typedef u8 Extent[sizeof($::unquote(name))];
            $::static_assert(sizeof(Extent) == sizeof(Types::Words), "fresh state type was lost");
            return identity($::unquote(name))[0];
        }
    };
}
define!()

[[macro]] static $::meta::tokens forward(in $::meta::tokens input) { return input; }
[[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
    $::meta::syntax function = $::syntax::node(input, "function");
    $::meta::tokens raw = $::meta::tokens(function);
    $::meta::syntax body = $::meta::child(function, $::meta::child_count(function) - 1uptr);
    $::meta::syntax header = $::meta::parse("function_header",
        $::meta::slice(raw, 0uptr, $::meta::len(raw) - 1uptr), $::syntax::context(function));
    return $::quote {
        namespace Structured { $::unquote(function) }
        namespace Projected { $::unquote(raw) }
        namespace Header { $::unquote(header) $::unquote(body) }
        namespace RawBody { $::unquote(header) $::unquote($::meta::tokens(body)) }
    };
}
syntax Copy : item { prefix "copy_variadic"; match function:function_def; expand copy; }
syntax Copy;
copy_variadic [[abi("@ABI@"), noinline, variadic(T *values "@STATE@")]]
static u64 copied_state<T>(in T tag, ...) { return forward!(values)[0]; }

[[syntax_expander]] static $::meta::tokens move(in $::meta::syntax_match input) {
    $::meta::syntax function = $::syntax::node(input, "function");
    $::meta::tokens parameters = $::meta::children($::meta::slice($::meta::tokens(function), 3uptr, 1uptr));
    $::meta::tokens name = $::meta::slice(parameters, $::meta::len(parameters) - 1uptr, 1uptr);
    $::meta::syntax body = $::meta::child(function, $::meta::child_count(function) - 1uptr);
    return $::quote {
        [[abi("@ABI@"), noinline, variadic(Types::Words $::unquote(name) "@STATE@")]]
        static u64 moved_state(in u64 tag, ...) $::unquote(body)
    };
}
syntax Move : item { prefix "move_state"; match function:function_def; expand move; }
syntax Move;
move_state static u64 captured(in Types::Words values) { return forward!(values)[0]; }

[[syntax_expander]] static $::meta::tokens decorate(in $::meta::syntax_match input) {
    $::meta::syntax definition = $::meta::parse("function_def", $::quote {
        [[abi("@ABI@"), noinline, variadic(Types::Words $::unquote($::syntax::capture(input, "name")) "@STATE@")]]
        $::unquote($::syntax::node(input, "header"))
        $::unquote($::syntax::capture(input, "body"))
    }, $::syntax::context(input));
    return $::quote { $::unquote(definition) };
}
syntax Decorate : item { prefix "decorate_state"; match name:ident header:function_header body:block; expand decorate; }
syntax Decorate;
decorate_state values static u64 decorated_state(in u64 tag, ...) { return forward!(values)[0]; }
[[macro]] static $::meta::tokens parameters(in $::meta::tokens ignored) {
    return $::quote { in u64 tag, ... };
}
hold_variadic [[abi("@ABI@"), noinline, variadic(Types::Words values "@STATE@")]]
static u64 deferred_state(parameters!()) { return forward!(values)[0]; }
[[macro]] static $::meta::tokens state_definition(in $::meta::tokens name) {
    return $::quote { Types::Words $::unquote(name) "@STATE@" };
}
hold_variadic [[abi("@ABI@"), noinline, variadic(state_definition!(values))]]
static u64 generated_state(in u64 tag, ...) { return forward!(values)[0]; }

[[abi("@HOST_ABI@")]] global i32 entry() {
    if (alias_state(0u64, @ARGUMENTS@) != 21u64) return 0;
    if (generic_state(0u64, @ARGUMENTS@) != 21u64) return 1;
    if (fresh_state(0u64, @ARGUMENTS@) != 21u64) return 2;
    if (Structured::copied_state(0u64, @ARGUMENTS@) != 21u64) return 3;
    if (Projected::copied_state(0u64, @ARGUMENTS@) != 21u64) return 4;
    if (Header::copied_state(0u64, @ARGUMENTS@) != 21u64) return 5;
    if (RawBody::copied_state(0u64, @ARGUMENTS@) != 21u64) return 6;
    if (moved_state(0u64, @ARGUMENTS@) != 21u64) return 7;
    if (decorated_state(0u64, @ARGUMENTS@) != 21u64) return 8;
    if (deferred_state(0u64, @ARGUMENTS@) != 21u64) return 9;
    if (generated_state(0u64, @ARGUMENTS@) != 21u64) return 10;
    return 42;
}
]=])
if(WIN32)
    set(HOST_ABI ms_abi)
else()
    set(HOST_ABI sysv_abi)
endif()
if(MODE STREQUAL custom)
    file(READ "${MODEL}" model_source)
    string(REPLACE "abi \"odd_abi\" {" [=[abi "odd_abi" {
    variadic_supported = true;
    variadic_state "user_area" { type = "u64*"; kind = "stack_address"; }
]=] model_source "${model_source}")
    file(WRITE "${root}/variadic-model.xm" "${model_source}")
endif()
if(MODE STREQUAL shipped OR MODE STREQUAL custom)
    set(profile "${MODE}")
    set(flags)
    if(profile STREQUAL custom)
        set(ABI odd_abi)
        set(STATE user_area)
        set(ARGUMENTS "13u64, 21u64")
        list(APPEND flags "--model=${root}/variadic-model.xm" -mabi=odd_abi)
    else()
        set(ABI sysv_abi)
        set(STATE gp_arg_area)
        set(ARGUMENTS "21u64")
    endif()
    string(CONFIGURE "${positive}" source @ONLY)
    check(valid_${profile} accept "${source}" ${flags})
    foreach(level O0 O2)
        set(SOURCE "${root}/valid_${profile}.x")
        set(RUNNER "${CMAKE_CURRENT_LIST_DIR}/../support/native_entry.cpp")
        set(OUTPUT "${root}/runtime-${profile}-${level}")
        set(ENTRY entry)
        set(EXPECTED 42)
        set(CC_FLAGS -${level} -fno-eval-calls ${flags})
        include("${CMAKE_CURRENT_LIST_DIR}/../support/run_native.cmake")
    endforeach()
    return()
elseif(NOT MODE STREQUAL diagnostics)
    message(FATAL_ERROR "unknown variadic binding test mode: ${MODE}")
endif()

foreach(type "u64 **" "const u64 *" "u64 *const")
    string(MD5 key "${type}")
    check(wrong_${key} "requires binding type 'u64\\*'"
        "typedef ${type} View; [[abi(\"sysv_abi\"), variadic(View state \"gp_arg_area\")]] global void inspect(in u64 tag, ...) {}")
endforeach()
check(wrong_array "requires binding type 'u64\\*'"
    "[[abi(\"sysv_abi\"), variadic(u64 state[2] \"gp_arg_area\")]] global void inspect(in u64 tag, ...) {}")
check(wrong_generic "requires binding type 'u64\\*'" [=[
[[abi("sysv_abi"), variadic(T *state "gp_arg_area")]] static void inspect<T>(in T tag, ...) {}
global void use() { inspect(0u32, 1u32); }
]=])
check(runtime_bound "runtime local or parameter is not a translation-time value" [=[
[[abi("sysv_abi"), variadic(u32 count "gp_offset")]] global void inspect(in u64 tag, ...) {
    typedef u8 Invalid[count];
}
]=])
check(state_operator "scalar operator requires numeric operands" [=[
[[abi("sysv_abi"), variadic(u64 *state "gp_arg_area")]] global void inspect(in u64 tag, ...) {
    if (0u32) state * 2u32;
}
]=])
