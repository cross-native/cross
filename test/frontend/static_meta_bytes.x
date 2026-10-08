// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
static $::meta::bytes make_bytes(in uptr count, in u8 first) {
    $::meta::buffer buffer = $::meta::alloc(count);
    u8 *data = $::meta::data(buffer);
    for (uptr i = 0uptr; i < count; ++i) data[i] = first + (u8)i;
    return $::meta::freeze(buffer, count);
}

[[noinline]] static u32 mutable_bytes() {
    static u8 data[] = make_bytes(3uptr, 7u8);
    ++data[0uptr];
    return data[0uptr] + data[1uptr] + data[2uptr] + (u32)sizeof(data);
}

[[noinline]] static u32 generic_bytes<T>() {
    static const u8 data[] = make_bytes(sizeof(T), (u8)sizeof(T));
    return (u32)sizeof(data) + data[sizeof(data) - 1uptr];
}

[[syntax_expander]] static $::meta::tokens preserve(in $::meta::syntax_match input) {
    return $::quote { $::unquote($::syntax::node(input, "body")) };
}
syntax Preserve : statement { prefix "preserve"; match body:stmt; expand preserve; }

#ifdef HOST_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    u32 local[2];
    static const u8 from_local[] = make_bytes(sizeof(local), 0u8);
    static const u8 first[] = make_bytes(3uptr, 2u8);
    static const u8 second[2] = make_bytes(2uptr, 13u8);
    static const u8 dependent[] = make_bytes(sizeof(first), 31u8);
    typedef u8 InferredWidth[sizeof(first)];
    struct InferredRecord { u8 data[sizeof(dependent)]; };
    typedef const u8 Bytes[];
    static Bytes aliased = make_bytes(2uptr, 19u8);
    $::static_assert(sizeof(first) == 3uptr, "inferred static bytes size");
    if (sizeof(from_local) != sizeof(local) || from_local[sizeof(local) - 1uptr] != (u8)(sizeof(local) - 1uptr))
        return 0u32;
    if (sizeof(first) != 3uptr || sizeof(InferredWidth) != 3uptr || first[0uptr] != 2u8 || first[2uptr] != 4u8 ||
        sizeof(second) != 2uptr || second[1uptr] != 14u8 ||
        sizeof(struct InferredRecord) != 3uptr || dependent[2uptr] != 33u8 ||
        sizeof(aliased) != 2uptr || aliased[1uptr] != 20u8) return 0u32;
    if (mutable_bytes() != 28u32 || mutable_bytes() != 29u32) return 0u32;
    {
        static const u8 first[] = make_bytes(5uptr, 37u8);
        typedef u8 InnerWidth[sizeof(first)];
        if (sizeof(InnerWidth) != 5uptr || first[4uptr] != 41u8) return 0u32;
    }
    if (sizeof(InferredWidth) != 3uptr || first[2uptr] != 4u8) return 0u32;
    if (generic_bytes<u16>() != 5u32 || generic_bytes<uptr>() != 3u32 * (u32)sizeof(uptr) - 1u32)
        return 0u32;
    syntax Preserve;
    preserve {
        static const u8 copied[] = make_bytes(4uptr, 23u8);
        typedef u8 CopiedWidth[sizeof(copied)];
        if (sizeof(copied) != 4uptr || sizeof(CopiedWidth) != 4uptr || copied[3uptr] != 26u8) return 0u32;
    }
    return 61u32;
}
