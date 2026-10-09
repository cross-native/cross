// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Identifiers that $::meta::parse and $::meta::token construct in a helper
// bind the helper's block-scope typedefs and tags, as a quote there does.
static $::meta::tokens choice_tokens() {
    typedef u16 Choice;
    return $::meta::parse("sizeof(Choice)");
}

static $::meta::tokens pair_tokens(in bool link) {
    struct Pair { u32 first; u64 second; };
    typedef struct Pair *Link;
    if (link) return $::meta::parse("Link");
    return $::meta::parse("struct Pair");
}

// A value declared in an inner block shadows the helper's typedef.
static $::meta::tokens shadow_tokens() {
    typedef u16 Wide;
    {
        u32 Wide = 1u32;
        return $::meta::parse("Wide");
    }
}

static $::meta::tokens word_token() {
    typedef u64 Word;
    return $::meta::token("identifier", "Word");
}

// A generic helper's names denote the instance's types.
static $::meta::tokens generic_tokens<T>(in T value) {
    typedef T Value;
    struct Twin { T first; T second; };
    return $::meta::parse("sizeof(Value) + sizeof(struct Twin) * 100uptr");
}

typedef u64 Choice;
global u32 Wide = 77u32;

[[macro]] static $::meta::tokens choice_size(in $::meta::tokens input) { return choice_tokens(); }
[[macro]] static $::meta::tokens pair_type(in $::meta::tokens input) { return pair_tokens(1u32 == 0u32); }
[[macro]] static $::meta::tokens link_type(in $::meta::tokens input) { return pair_tokens(1u32 == 1u32); }
[[macro]] static $::meta::tokens shadowed(in $::meta::tokens input) { return shadow_tokens(); }
[[macro]] static $::meta::tokens word_type(in $::meta::tokens input) { return word_token(); }
[[macro]] static $::meta::tokens generic_sizes(in $::meta::tokens input) { return generic_tokens(0u32); }

// A macro's own block-scope typedef.
[[macro]]
static $::meta::tokens byte_size(in $::meta::tokens input) {
    typedef u8 Byte;
    return $::meta::parse("sizeof(Byte)");
}

global u32 parse_local_types_entry() {
    pair_type!() pair;
    pair.first = 3u32;
    pair.second = 4u64;
    link_type!() link = &pair;
    word_type!() word = 0xffffffffffu64;
    return choice_size!() == 2uptr && sizeof(Choice) == 8uptr && sizeof(pair_type!()) == 16uptr &&
           link->second == 4u64 && shadowed!() == 77u32 && sizeof(word_type!()) == 8uptr &&
           word == 0xffffffffffu64 && generic_sizes!() == 804uptr && byte_size!() == 1uptr;
}
