// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// A quoted tag or typedef bound to a block-scope record of the executing
// helper denotes that record wherever the generated code is placed.

[[macro]]
static $::meta::tokens pair_size(in $::meta::tokens input) {
    struct Pair { u32 first; u64 second; };
    return $::quote { sizeof(struct Pair) };
}

// Declares an array of the helper's records and fills its last element.
[[macro]]
static $::meta::tokens make_pairs(in $::meta::tokens input) {
    struct Pair { u32 first; u64 second; };
    typedef struct Pair Pairs[2];
    return $::quote {
        Pairs $::unquote(input);
        struct Pair *last = &$::unquote(input)[1];
        last->first = 3u32;
        last->second = 4u64;
    };
}

[[macro]]
static $::meta::tokens bits_type(in $::meta::tokens input) {
    union Bits { u32 word; u8 bytes[4]; };
    return $::quote { union Bits };
}

// Nested records with a pointer member and an array member.
[[macro]]
static $::meta::tokens outer_type(in $::meta::tokens input) {
    struct Inner { u16 a; u16 b; };
    struct Outer { struct Inner inner; struct Inner *link; u32 tail[3]; };
    return $::quote { struct Outer };
}

// A typedef of an anonymous record.
[[macro]]
static $::meta::tokens item_type(in $::meta::tokens input) {
    typedef struct { u8 tag; u32 value; } Item;
    return $::quote { Item };
}

// An ordinary translation-time helper quotes its own records too.
static $::meta::tokens node_tokens(in bool link) {
    struct Node { struct Node *next; u32 value; };
    typedef struct Node *Link;
    if (link) return $::quote { Link };
    return $::quote { struct Node };
}

[[macro]]
static $::meta::tokens node_type(in $::meta::tokens input) { return node_tokens($::meta::len(input) != 0uptr); }

// A syntax expander quotes its own record.
namespace gen {
    [[syntax_expander]]
    static $::meta::tokens expand_boxed($::meta::syntax_match input) {
        struct Boxed { u32 value; u32 extra; };
        $::meta::tokens name = $::syntax::capture(input, "name");
        return $::quote { struct Boxed $::unquote(name); };
    }
    syntax boxed : statement {
        prefix "boxed";
        match name:ident ";";
        expand expand_boxed;
    }
}

// An outer-block record named from an inner block. Its member extent is a
// block-scope enumerator.
[[macro]]
static $::meta::tokens tagged_type(in $::meta::tokens input) {
    enum Kind { First = 1, Count = 5 };
    struct Tagged { enum Kind kind; u8 data[Count]; };
    {
        u32 unrelated = 0u32;
        return $::quote { struct Tagged };
    }
}

// A same-named tag at the expansion site is a different type.
struct Pair { u8 only; };

$::static_assert(pair_size!() == 16uptr && sizeof(bits_type!()) == 4uptr &&
    sizeof(outer_type!()) == 32uptr && sizeof(item_type!()) == 8uptr &&
    sizeof(node_type!()) == 16uptr && sizeof(node_type!(link)) == 8uptr &&
    sizeof(tagged_type!()) == 12uptr && sizeof(struct Pair) == 1uptr, "quoted record layouts");

static u64 compute() {
    make_pairs!(pairs);
    bits_type!() bits;
    bits.word = 0x01020304u32;
    outer_type!() outer;
    outer.inner.a = 5u16;
    outer.inner.b = 6u16;
    outer.link = &outer.inner;
    outer.tail[2] = 7u32;
    item_type!() items[2];
    item_type!() *item = &items[1];
    item->tag = 8u8;
    item->value = 9u32;
    node_type!() first;
    node_type!() second;
    node_type!(link) head = &first;
    first.next = &second;
    first.value = 10u32;
    second.next = (void *)0uptr;
    second.value = 11u32;
    struct Pair site;
    site.only = 12u8;
    syntax gen::boxed;
    boxed box;
    box.value = 13u32;
    box.extra = 14u32;
    tagged_type!() tagged;
    tagged.data[4] = 15u8;
    // 3 + 4 + 4 + (5 + 6 + 7) + (8 + 9) + (10 + 11) + 12 + 32 + (13 + 14) + (15 + 12)
    return (u64)pairs[1].first + pairs[1].second + (u64)bits.bytes[0] +
        (u64)outer.link->a + (u64)outer.link->b + (u64)outer.tail[2] +
        (u64)items[1].tag + (u64)items[1].value + (u64)head->value + (u64)head->next->value +
        (u64)site.only + (u64)sizeof(pairs) + (u64)box.value + (u64)box.extra +
        (u64)tagged.data[4] + (u64)sizeof(tagged);
}

// Computed during compilation.
global u64 folded = compute();

global u32 quote_local_records_entry() {
    return folded == 165u64 && $::runtime(compute()) == folded;
}
