// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace MetaRecordValues {
struct Pair { uptr first; u32 second; };
union Choice { u32 whole; u16 part; };
typedef struct Pair Alias;
struct Other { uptr first; u32 second; };
global void output(out struct Pair value);
static T copy<T>(in T value) { return value; }
[[operator("+")]] static struct Pair add(in const struct Pair left, in struct Pair right) {
    struct Pair result = {left.first + right.first, left.second + right.second};
    return result;
}
[[operator("!")]] static bool empty(in struct Pair value) {
    return value.first == 0uptr && value.second == 0u32;
}
static struct Pair identity(in struct Pair value) { return value; }
static uptr last() { return 1uptr; }
static $::meta::tokens helper(in $::meta::tokens input) {
    const Alias seed = {3uptr, 5u32};
    struct Pair value = copy(seed);
    value = seed;
    struct Pair selected = $::meta::len(input) != 0uptr ? seed : value;
    struct Pair sum = identity(selected) + (struct Pair)value;
    if (sum.first != 6uptr || sum.second != 10u32 || !sum) return $::quote { 0u32 };
    const union Choice original = {.whole = 19u32};
    union Choice copied = copy(original);
    copied = original;
    union Choice chosen = (bool)1u8 ? copied : original;
    if (chosen.whole != 19u32) return $::quote { 0u32 };
    struct Pair *pointer = &value;
    if (pointer->first != 3uptr || sizeof(*pointer) != sizeof(struct Pair))
        return $::quote { 0u32 };
    u8 tag;
    struct Pair indexed[2] = {[sizeof(tag) - 1uptr] = seed, [last()] = {7uptr, 11u32}};
    struct Nested { struct Pair pair; u8 text[3]; } nested = {indexed[1uptr], "ab"};
    if (indexed[0uptr].second != 5u32 || nested.pair.first != 7uptr ||
        nested.text[2uptr] != 0u8) return $::quote { 0u32 };
    uptr count = $::meta::len(input) + 1uptr;
    struct Pair dynamic[count] = {[last()] = seed};
    if (dynamic[1uptr].first != 3uptr) return $::quote { 0u32 };
    if (0u32) {
        const struct Other discarded = {23uptr, 29u32};
        struct Other array[1] = {{31uptr, 37u32}};
        volatile struct Pair observable = {41uptr, 43u32};
        struct Pair unexecuted_copy = observable;
        output(0u32);
        output(discarded);
        output((struct Other)discarded);
        output(array);
        output(value);
    }
    return input;
}
[[macro]] static $::meta::tokens apply(in $::meta::tokens input) { return helper(input); }
static u32 run() { return apply!(87u32); }
}

#ifdef CUSTOM_META_ABI
[[abi(HOST_ABI)]]
#endif
global u32 syntax_raw_entry() {
    return MetaRecordValues::run() == 87u32 ? 61u32 : 0u32;
}
