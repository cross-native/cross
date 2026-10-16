// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Generic records and unions: instances, identity, layout, self-reference,
// forward declarations, deduction from instances, value parameters, local
// generic records, and anonymous members. Each check runs at translation
// time through $::static_assert and at runtime through the entry function.

#if !$::has_feature($::feature::generic_types)
#error generic records and unions are not reported as available
#endif

struct list<T> { T *items; uptr count; };
struct pair<A, B> { A first; B second; };
struct buf<T, uptr N> { T data[N]; u8 used; };
union either<T> { T value; u32 bits; };
struct node<T> { T value; struct node<T> *next; };
struct tree<T>;
struct holder { struct list<u32> words; struct tree<u8> *root; };
struct tree<T> { T key; struct tree<T> *left; struct tree<T> *right; };
typedef struct list<u8> bytes;
enum mode [[underlying(u8)]] { fast, slow };
struct tuned<enum mode M, bool B> { u32 value; u8 flags[M == slow && B ? 2 : 1]; };
struct packed_pair<A, B> [[packed]] { A a; B b; };
struct aligned_box<T> [[aligned(16)]] { T v; };
union bits<T> { T value; u8 raw[sizeof(T)]; };
struct variant<T> { u32 tag; union { T value; u32 error; }; };
struct field<uptr W> { u32 low : W; u32 high : 32 - W; };
struct over<uptr A> { u8 head; u32 body [[aligned(A)]]; };

// Equal normalized arguments select one instance; others are distinct types.
$::static_assert(sizeof(struct list<u8>) == 2 * sizeof(uptr), "list layout");
$::static_assert(sizeof(struct buf<u16, 3>) == 8, "value parameter extent");
$::static_assert(sizeof(struct buf<u16, 2 + 1>) == sizeof(struct buf<u16, 3uptr>), "same instance");
$::static_assert(sizeof(struct buf<u16, 4>) == 10, "another instance");
$::static_assert(sizeof(union either<u64>) == 8, "union instance");
$::static_assert(sizeof(struct list<struct list<u8>>) == sizeof(struct list<u8>), "nested instance");
$::static_assert(sizeof(struct packed_pair<u8, u32>) == 5, "packed applies to every instance");
$::static_assert(sizeof(struct packed_pair<u8, u64>) == 9, "packed applies to every instance");
$::static_assert($::alignof(struct aligned_box<u8>) == 16, "aligned applies to every instance");
$::static_assert(sizeof(struct tuned<slow, 1 == 1>) == 8, "enumeration and bool arguments");
$::static_assert(sizeof(struct tuned<fast, 1 == 1>) == 8, "enumeration and bool arguments");
$::static_assert($::offsetof(struct tuned<slow, 1 == 1>, flags) == 4, "enumeration argument");
$::static_assert(sizeof(union bits<u32>) == 4, "sizeof of a parameter");
$::static_assert(sizeof(struct variant<u64>) == 16, "anonymous member per instance");
$::static_assert(sizeof(struct variant<u8>) == 8, "anonymous member per instance");
$::static_assert($::offsetof(struct buf<u8, 4>, used) == 4, "offsetof an instance member");
$::static_assert(sizeof(struct field<5>) == 4, "bit-field width parameter");
$::static_assert($::offsetof(struct over<16>, body) == 16, "member alignment parameter");
$::static_assert($::offsetof(struct over<4>, body) == 4, "member alignment parameter");

// A redeclaration's value arguments are compared for each used instance.
static T last<T, uptr N>(in struct buf<T, N> *b);
static T first<T>(in struct list<T> values) { return values.items[0]; }
static uptr length<T>(in struct list<T> *values) { return values->count; }
static T sum_buf<T, uptr N>(in struct buf<T, N> *b) {
    T sum = 0;
    for (uptr i = 0; i < N; ++i) sum = sum + b->data[i];
    return sum;
}
static struct pair<B, A> swap<A, B>(in struct pair<A, B> p) {
    struct pair<B, A> result = { p.second, p.first };
    return result;
}
static struct list<T> make<T>(in T *items, in uptr count) {
    struct list<T> result = { items, count };
    return result;
}
static T local_sum<T>(in T a, in T b) {
    struct duo<U> { T first; U second; };
    struct duo<T> p = { a, b };
    struct duo<u8> q = { a, 1 };
    return p.first + p.second + (T)q.second;
}
static T local_union<T>(in T a) {
    struct box<U> { T head; union { U small; T wide; }; };
    struct box<u16> b = {};
    b.head = a;
    b.small = 7;
    return b.head + (T)b.small;
}
static u32 apply(in u32 (*f)(in struct pair<u32, u32> p), in u32 x) {
    struct pair<u32, u32> p = { x, 1 };
    return f(p);
}
static u32 add(in struct pair<u32, u32> p) { return p.first + p.second; }

static u32 check_instances() {
    u32 words[3] = { 7, 8, 9 };
    struct list<u32> l = { words, 3 };
    if (first(l) != 7 || length(&l) != 3) return 2;
    struct buf<u32, 4> b = { { 1, 2, 3, 4 }, 4 };
    if (sum_buf<u32, 4>(&b) != 10 || last<u32, 4>(&b) != 4) return 3;
    struct buf<u32, 2 + 2> same = b;
    if (same.data[3] != 4) return 4;
    struct pair<u8, u32> p = { 1, 2 };
    struct pair<u32, u8> q = swap(p);
    if (q.first != 2 || q.second != 1) return 5;
    struct pair<u8, u32> designated = { .second = 6 };
    if (designated.first != 0 || designated.second != 6) return 6;
    struct list<struct list<u32>> nested = { &l, 1 };
    if (nested.items[0].items[2] != 9) return 7;
    struct holder h = {};
    h.words = l;
    if (h.words.count != 3 || h.root != 0) return 8;
    return 1;
}

static T last<T, uptr N>(in struct buf<T, N + 0> *b) { return b->data[N - 1]; }

static u32 check_self_reference() {
    struct node<u32> second = { 2, 0 };
    struct node<u32> head = { 1, &second };
    if (head.next->value != 2 || head.next->next != 0) return 2;
    struct tree<u8> leaf = { 3, 0, 0 };
    struct tree<u8> root = { 5, &leaf, 0 };
    if (root.left->key != 3) return 3;
    return 1;
}

static u32 check_values() {
    u8 raw[4] = { 1, 2, 3, 4 };
    bytes b = make(raw, 4);
    if (b.count != 4 || b.items[3] != 4) return 2;
    struct tuned<slow, 1 == 1> t = {};
    t.flags[1] = 5;
    if (t.flags[1] != 5) return 3;
    struct field<5> f = {};
    f.low = 31;
    f.high = 1;
    if (f.low + f.high != 32) return 4;
    return 1;
}

static u32 check_unions() {
    union either<f32> e;
    e.value = 1.0f32;
    if (e.bits != 0x3f800000u32) return 2;
    union bits<u16> w;
    w.value = 0x0102u16;
    if (w.raw[0] + w.raw[1] != 3) return 3;
    struct variant<u64> v = {};
    v.tag = 1;
    v.error = 2;
    if (v.error != 2 || v.tag != 1) return 4;
    return 1;
}

static u32 check_local() {
    if (local_sum(3u32, 4u32) != 8 || local_sum(3u16, 4u16) != 8) return 2;
    if (local_union(1u32) != 8 || local_union(2u64) != 9) return 3;
    struct local<T> { T a; T b; };
    struct local<u8> x = { 1, 2 };
    struct local<u32> y = { 3, 4 };
    if (x.a + x.b + y.a + y.b != 10) return 4;
    return 1;
}

$::static_assert(check_instances() == 1, "instances at translation time");
$::static_assert(check_self_reference() == 1, "self-reference at translation time");
$::static_assert(check_values() == 1, "value arguments at translation time");
$::static_assert(check_unions() == 1, "unions at translation time");
$::static_assert(check_local() == 1, "local generic records at translation time");

global u32 generic_records_entry() {
    u32 result = $::runtime(check_instances());
    if (result != 1) return 10 + result;
    result = $::runtime(check_self_reference());
    if (result != 1) return 20 + result;
    result = $::runtime(check_values());
    if (result != 1) return 30 + result;
    result = $::runtime(check_unions());
    if (result != 1) return 40 + result;
    result = $::runtime(check_local());
    if (result != 1) return 50 + result;
    // Translation-time evaluation makes no indirect calls.
    if (apply(add, 4) != 5) return 60;
    return 1;
}
