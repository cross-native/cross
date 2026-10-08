// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace ArrayBounds {
    static uptr width() { return 4uptr; }
    typedef u8 Bytes[width()];
    typedef u8 Bytes[4];
    typedef u8 PointerBytes[sizeof(uptr)];
    static u8 storage[sizeof(uptr)] = { 1u8, 2u8 };
    static u8 text[width()] = "abc";
    static u8 matrix[width()][sizeof(uptr)];
    enum { Four = sizeof(u8[width()]) };
    typedef u8 Wrapped[0xffffffffu32 + 2u32];
    typedef u8 UnsignedCompare[0xffffffffffffffffu64 > 0u64 ? 4u32 : 3u32];
    typedef u8 MixedCompare[-1i32 < 1u32 ? 8u32 : 4u32];
    typedef u8 Wide[(1u128 << 96u32) >> 94u32];
    typedef u8 Octal[010];
    typedef u8 PointerArithmetic[(0xffffffffuptr + 2uptr) == 1uptr ? 4u32 : 8u32];
    typedef u8 ShortCircuit[1u32 || (1u32 / 0u32)];

    [[noinline]] static u32 row(in u8 (*value)[sizeof(uptr)]) {
        return (*value)[1uptr];
    }
    [[noinline]] static u32 first(in u8 value[width()]) { return value[0uptr]; }
    [[noinline]] static u32 generic<uptr N>(in u8 (*value)[N]) {
        typedef u8 Row[N];
        typedef u8 Row[N + 0uptr];
        return (u32)sizeof(Row) + (*value)[0uptr];
    }
    [[noinline]] static u32 adjusted<uptr N>(in u8 value[N]) {
        return (u32)N + value[0uptr];
    }
    typedef u32 (*Callback)(in u8 value[width()]);
    [[noinline]] static u32 runtime_rows(in uptr rows) {
        u8 values[rows][width()] = { "abc", "def" };
        return sizeof(values) == rows * 4uptr && values[1uptr][1uptr] == 101u8 ? 1u32 : 0u32;
    }
    enum { shadowed = 64uptr };
    [[noinline]] static u32 local_layout<T>(in T shadowed, in uptr rows) {
        typedef u8 ParameterBytes[sizeof(shadowed)];
        typedef u8 PointerBytesLocal[sizeof(&shadowed)];
        static u8 saved[sizeof(shadowed)] = { 7u8 };
        ParameterBytes bytes = { 9u8 };
        u8 local[3] = { 1u8 };
        typedef u8 LocalBytes[sizeof(local)];
        LocalBytes from_local = { 11u8 };
        typedef u32 LocalVector [[ext_vector_type(sizeof(local) + 1uptr)]];
        struct LocalCell {
            LocalVector lanes;
            u8 parameter[sizeof(shadowed)];
            u8 local_bytes[sizeof(local)];
        } cell;
        cell.lanes[3uptr] = 17u32;
        cell.parameter[sizeof(shadowed) - 1uptr] = 29u8;
        u8 nested[rows][sizeof(local)] = { { 13u8 } };
        uptr counter = 0uptr;
        typedef u8 Unevaluated[sizeof(++counter)];
        if (sizeof(bytes) != sizeof(T) || sizeof(saved) != sizeof(T) ||
            bytes[0uptr] != 9u8 || saved[0uptr] != 7u8 ||
            sizeof(PointerBytesLocal) != sizeof(uptr) || sizeof(from_local) != 3uptr ||
            from_local[0uptr] != 11u8 || sizeof(nested) != rows * 3uptr ||
            sizeof(LocalVector) != 16uptr || cell.lanes[3uptr] != 17u32 ||
            sizeof(cell.parameter) != sizeof(T) || sizeof(cell.local_bytes) != 3uptr ||
            cell.parameter[sizeof(T) - 1uptr] != 29u8 ||
            nested[0uptr][0uptr] != 13u8 || sizeof(Unevaluated) != sizeof(uptr) ||
            counter != 0uptr) return 0u32;
        {
            u64 shadowed = 0u64;
            typedef u8 Inner[sizeof(shadowed)];
            if (sizeof(Inner) != sizeof(u64)) return 0u32;
        }
        return 1u32;
    }
    static u32 meta_local_layout<T>(in T value) {
        typedef u8 BytesOfValue[sizeof(value)];
        BytesOfValue bytes = { 19u8 };
        u8 local[3] = { 1u8 };
        typedef u8 BytesOfLocal[sizeof(local)];
        struct Cell { u8 parameter[sizeof(value)]; u8 bytes[sizeof(local)]; } cell;
        cell.parameter[sizeof(T) - 1uptr] = 31u8;
        return sizeof(bytes) == sizeof(T) && bytes[0uptr] == 19u8 &&
            sizeof(BytesOfLocal) == 3uptr && sizeof(cell.parameter) == sizeof(T) &&
            sizeof(cell.bytes) == 3uptr && cell.parameter[sizeof(T) - 1uptr] == 31u8 ? 1u32 : 0u32;
    }
    [[noinline]] static u32 ordinary_local_layout(in u16 value) {
        u8 local[3];
        struct Cell { u8 parameter[sizeof(value)]; u8 bytes[sizeof(local)]; } cell;
        cell.parameter[1uptr] = 37u8;
        return sizeof(cell.parameter) == 2uptr && sizeof(cell.bytes) == 3uptr &&
            cell.parameter[1uptr] == 37u8 ? 1u32 : 0u32;
    }
    [[macro]] static $::meta::tokens check(in $::meta::tokens input) {
        typedef u8 Local[width()];
        Local bytes = "abc";
        if (sizeof(bytes) != 4uptr || generic<4uptr>(&bytes) != 101u32 ||
            adjusted<4uptr>(bytes) != 101u32 || meta_local_layout(5u16) != 1u32 ||
            meta_local_layout(7uptr) != 1u32) return $::quote { 0u32 };
        return input;
    }
    [[syntax_expander]] static $::meta::tokens copy(in $::meta::syntax_match input) {
        return $::quote { $::unquote($::syntax::node(input, "body")) };
    }
    syntax Function : item { prefix "array_function"; match body:function_def; expand copy; }
    syntax Declaration : item { prefix "array_declaration"; match body:declaration; expand copy; }
    syntax Function, Declaration;
    array_declaration typedef u8 CopiedBytes[width()];
    array_function [[noinline]] static u32 copied_header<uptr N>(in u8 (*value)[N]) {
        return (u32)sizeof(*value);
    }
#ifdef CUSTOM_SYNTAX_ABI
    struct Box { u8 bytes[width()]; };
    [[noinline, abi("stack_result_abi")]] static struct Box stack_result() {
        struct Box result = { .bytes = "abc" };
        return result;
    }
    [[noinline, abi("memory_result_abi")]] static struct Box memory_result() {
        struct Box result = { .bytes = "def" };
        return result;
    }
#endif
    array_function [[noinline]] static u32 run() {
        static u8 local[width()] = "abc";
        Bytes array = "abc";
        CopiedBytes copied = "def";
        struct CopiedCell { u8 bytes[sizeof(copied)]; } copied_cell;
        copied_cell.bytes[3uptr] = 41u8;
        u8 wrapped_local[0xffffffffu32 + 2u32] = { 23u8 };
        if (sizeof(Wrapped) != 1uptr || sizeof(UnsignedCompare) != 4uptr ||
            sizeof(MixedCompare) != 4uptr || sizeof(Wide) != 4uptr || sizeof(Octal) != 8uptr ||
            sizeof(PointerArithmetic) != sizeof(uptr) || sizeof(ShortCircuit) != 1uptr ||
            sizeof(wrapped_local) != 1uptr || wrapped_local[0uptr] != 23u8 ||
            sizeof(copied_cell.bytes) != 4uptr || copied_cell.bytes[3uptr] != 41u8) return 0u32;
        PointerBytes pointer_bytes = { 2u8, 3u8 };
        u8 nested[2][width()] = { "abc", "def" };
        Callback callback = first;
        if (sizeof(storage) != sizeof(uptr) || sizeof(text) != 4uptr ||
            text[3uptr] != 0u8 || local[1uptr] != 98u8 || Four != 4uptr ||
            sizeof(matrix) != 4uptr * sizeof(uptr)) return 0u32;
        if (local_layout<u16>(5u16, 2uptr) != 1u32 ||
            local_layout<uptr>(7uptr, 3uptr) != 1u32 ||
            ordinary_local_layout(9u16) != 1u32) return 0u32;
        if (row(&storage) != 2u32 || row(&pointer_bytes) != 3u32 ||
            first(text) != 97u32 || callback(text) != 97u32 ||
            generic<4uptr>(&array) != 101u32 || adjusted<4uptr>(array) != 101u32 ||
            copied_header<4uptr>(&copied) != 4u32 || runtime_rows(2uptr) != 1u32 ||
            nested[1uptr][1uptr] != 101u8 || sizeof(nested) != 8uptr) return 0u32;
#ifdef CUSTOM_SYNTAX_ABI
        struct Box stack_box = stack_result(), memory_box = memory_result();
        if (stack_box.bytes[1uptr] != 98u8 || memory_box.bytes[1uptr] != 101u8) return 0u32;
#endif
        return check!(61u32);
    }
}
#ifdef CUSTOM_SYNTAX_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() { return ArrayBounds::run(); }
