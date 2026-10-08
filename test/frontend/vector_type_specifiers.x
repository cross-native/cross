// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace VectorTypeSpecifiers {
    static uptr count() { return 4uptr; }
    enum { Count = 4u32 };
    typedef u32 HeaderAlias [[ext_vector_type((uptr)Count)]];
    typedef u32 Repeated [[ext_vector_type(2uptr + 2uptr)]];
    typedef u32 Repeated [[ext_vector_type(4)]];
    [[eval_only]] static HeaderAlias alias_result<uptr Count>(in u32 value) {
        HeaderAlias result = value;
        return result;
    }
    static T copied<T>(in T value) { return value; }
#if !$::has_feature($::feature::fixed_vectors)
    [[eval_only]]
#endif
    static T doubled<T, uptr N>(in T value) {
        typedef T Alias [[ext_vector_type(N)]];
        typedef T Alias [[ext_vector_type(N + 0uptr)]];
        T [[ext_vector_type(N)]] lanes = value;
        lanes[N - 1uptr] += value;
        return lanes[N - 1uptr];
    }
    [[eval_only]] static T [[ext_vector_type(N)]] generic_vector<T, uptr N>(in T value) {
        T [[ext_vector_type(N)]] lanes = value;
        return lanes;
    }
    [[eval_only]] static uptr width_of<T>() { return sizeof(T); }
    [[eval_only]] static u32 [[ext_vector_type(4)]] make(in u32 value) {
        u32 [[vector_size(16)]] lanes = value;
        lanes[2uptr] += 4u32;
        return lanes;
    }
    [[macro]] static $::meta::tokens validate(in $::meta::tokens input) {
        uptr [[vector_size(16)]] widths = 3uptr;
        if (sizeof(widths) != 16uptr || widths[16uptr / sizeof(uptr) - 1uptr] != 3uptr)
            return $::quote { 0u32 };
        const u32 [[ext_vector_type(4)]] values = make(3u32);
        if (values[0uptr] != 3u32 || values[2uptr] != 7u32) return $::quote { 0u32 };
        struct Cell { u32 [[ext_vector_type(4)]] lanes; } cell = { .lanes = make(5u32) };
        if (cell.lanes[2uptr] != 9u32) return $::quote { 0u32 };
        u32 [[vector_size(sizeof(u32) * count())]] computed = 7u32;
        u16 [[ext_vector_type(0x4u32)]] hexadecimal = 9u16;
        if (computed[3uptr] != 7u32 || hexadecimal[3uptr] != 9u16 ||
            sizeof(Repeated) != 16uptr || sizeof(alias_result<8uptr>(1u32)) != 16uptr ||
            alias_result<8uptr>(13u32)[3uptr] != 13u32 ||
            doubled<u32, 4uptr>(3u32) != 6u32 || doubled<u16, 8uptr>(5u16) != 10u16 ||
            copied(generic_vector<u32, 4uptr>(11u32))[3uptr] != 11u32 ||
            width_of<uptr [[vector_size(2uptr * sizeof(uptr))]]>() != 2uptr * sizeof(uptr))
            return $::quote { 0u32 };
        return input;
    }
    [[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
        return $::quote { $::unquote($::syntax::node(input, "body")) };
    }
    [[syntax_expander]] static $::meta::tokens width(in $::meta::syntax_match input) {
        return $::quote { sizeof($::unquote($::syntax::node(input, "body"))) };
    }
    syntax Copy : item { prefix "vector_function"; match body:function_def; expand copy; }
    syntax Width : expression { prefix "vector_width"; match "(" body:type ")"; expand width; }
    syntax Copy, Width;
#ifdef CUSTOM_SYNTAX_ABI
    struct VectorBox { u32 [[ext_vector_type(count())]] lanes; };
    vector_function [[noinline, abi("memory_result_abi")]]
    static struct VectorBox memory_result(in u32 value) {
        struct VectorBox result = { .lanes = value };
        return result;
    }
    vector_function [[noinline, abi("stack_result_abi")]]
    static struct VectorBox stack_result(in u32 value) {
        struct VectorBox result = { .lanes = value };
        return result;
    }
#endif
    vector_function [[noinline]] static u32 run() {
        // These ordinary type names survive public capture, structured splice,
        // target layout and runtime code generation under the selected ABI.
        u32 [[ext_vector_type(4)]] *pointer = (u32 [[vector_size(16)]] *)0uptr;
        if (sizeof(*pointer) != 16uptr || vector_width(uptr [[vector_size(16)]]) != 16uptr ||
            vector_width(u16 [[ext_vector_type(count())]]) != 8uptr)
            return 0u32;
#if $::has_feature($::feature::fixed_vectors)
        if (doubled<u32, 4uptr>(3u32) != 6u32 ||
            doubled<u16, 8uptr>(5u16) != 10u16) return 0u32;
#endif
#ifdef CUSTOM_SYNTAX_ABI
        struct VectorBox memory = memory_result(3u32), stack = stack_result(7u32);
        if (memory.lanes[2uptr] != 3u32 || stack.lanes[1uptr] != 7u32) return 0u32;
        memory.lanes[2uptr] += stack.lanes[1uptr]++;
        if (memory.lanes[2uptr] != 10u32 || stack.lanes[1uptr] != 8u32) return 0u32;
#endif
        return validate!(89u32);
    }
}
