# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# Translation-time evaluation reads constant objects in every evaluation
# context: static initializers, enumerators, array bounds, case labels,
# assertions, generic value arguments, $::eval, eval-only and ordinary calls,
# and macro expanders. Other static objects stay unreadable, and a cycle
# through initializers is an error.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

function(compile name source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}\n")
    if(NOT ARGN)
        set(ARGN -target x86_64-unknown-linux-gnu)
    endif()
    execute_process(COMMAND "${CC}" -S -O2 ${ARGN} "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    set(status "${status}" PARENT_SCOPE)
    set(diagnostics "${out}${err}" PARENT_SCOPE)
endfunction()

function(accept name source)
    compile(${name} "${source}" ${ARGN})
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name}: unexpected rejection\n${diagnostics}")
    endif()
endfunction()

# Whether the body of `function` in the assembly of `name` returns `constant`.
function(returns name function constant)
    file(READ "${OUTPUT}/${name}.s" assembly)
    string(FIND "${assembly}" "\n${function}:" start)
    string(FIND "${assembly}" ".size ${function}," stop)
    if(start EQUAL -1 OR stop EQUAL -1)
        message(FATAL_ERROR "${name}: no function ${function}\n${assembly}")
    endif()
    math(EXPR length "${stop} - ${start}")
    string(SUBSTRING "${assembly}" ${start} ${length} body)
    if(NOT body MATCHES "\\$${constant},")
        message(FATAL_ERROR "${name}: ${function} does not return the folded ${constant}\n${body}")
    endif()
endfunction()

# `location` is the line and column the diagnostic names.
function(reject name location expected source)
    compile(${name} "${source}" ${ARGN})
    string(FIND "${diagnostics}" "${name}.x:${location}: error: ${expected}" position)
    if(status EQUAL 0 OR position EQUAL -1)
        message(FATAL_ERROR "${name}: missing '${location}: error: ${expected}'\n${diagnostics}")
    endif()
endfunction()

# Scalars in each mandatory context.
set(contexts [=[
static const i32 k = 5;
static const i32 k2 = k + 1;
enum sizes { small = k, large = k2 * 2 };
static i32 bounded[k2];
$::static_assert(sizeof(bounded) == 6 * sizeof(i32) && large == 12, "bound and enumerator");
static i32 twice<i32 N>() { return N * 2; }
global i32 generic_value() { return twice<k2>(); }
global i32 cases(in i32 x) {
    switch (x) {
    case k: return 1;
    case k2: return 2;
    default: return 0;
    }
}
global i32 forced() { return $::eval(k * k2); }
]=])
accept(contexts "${contexts}")
accept(contexts_mips "${contexts}" -target mips-unknown-elf)
returns(contexts generic_value 12)
returns(contexts forced 30)

# Tables, subobjects, strings through constant pointers, pointers to constant
# objects, and translation-time functions over tables.
accept(tables [=[
struct point { i32 x; i32 y; };
struct named { const u8 *const name; i32 id; };
static const u32 table[4] = { 10, 20, 30, 40 };
static const struct point points[] = { { 1, 2 }, { 3, 4 } };
static const struct named names[2] = { { "alpha", 1 }, { "beta", 2 } };
static const i32 k = 5;
static const i32 *const pk = &k;
static const struct point *const second = &points[1];
$::static_assert(table[2] == 30 && points[1].y == 4 && second->x == 3, "subobjects");
$::static_assert(names[1].name[0] == 'b' && names[0].name[4] == 'a' && names[1].id == 2, "strings");
$::static_assert(*pk == 5 && pk[0] == 5, "through a pointer");
[[eval_only]] static u32 sum_table() {
    u32 total = 0;
    for (uptr i = 0; i < 4; ++i) total += table[i];
    return total;
}
$::static_assert(sum_table() == 100, "eval-only");
static const u32 crc_seed[4] = { 0u32, 1u32, 2u32, 3u32 };
[[eval_only]] static u32 crc_entry(in uptr index) {
    u32 c = crc_seed[index];
    for (u32 bit = 0; bit < 8; ++bit) c = (c & 1u32) != 0 ? 0xedb88320u32 ^ (c >> 1) : c >> 1;
    return c;
}
global u32 crc[4] = { crc_entry(0), crc_entry(1), crc_entry(2), crc_entry(3) };
$::static_assert(crc_entry(1) == 0x77073096u32, "generated table");
static u32 lookup(in uptr i) { return table[i]; }
global u32 folded() { return lookup(3); }
]=])
returns(tables folded 40)

# Block statics, objects defined later in the group, and other source units.
accept(block_statics [=[
global i32 local_static() {
    static const i32 base = 3;
    static const i32 derived[2] = { base, base * 4 };
    $::static_assert(derived[1] == 12, "block statics");
    return derived[1];
}
]=])
accept(defined_later [=[
const i32 later;
const i32 doubled = later * 2;
$::static_assert(doubled == 14, "defined later");
const i32 later = 7;
]=])
file(WRITE "${OUTPUT}/unit_a.x" "struct limits { i32 low; i32 high; };
const struct limits shared_limits = { 3, 9 };
const u8 *const greeting = \"hello\";
")
file(WRITE "${OUTPUT}/unit_b.x" "struct limits { i32 low; i32 high; };
const struct limits shared_limits;
const u8 *const greeting;
static const struct limits wider = { shared_limits, .high = shared_limits.high * 2 };
\$::static_assert(wider.low == 3 && wider.high == 18 && greeting[1] == 'e', \"another unit\");
global i32 span() { return wider.high - wider.low; }
")
foreach(order "unit_a;unit_b" "unit_b;unit_a")
    set(inputs)
    foreach(unit IN LISTS order)
        list(APPEND inputs "${OUTPUT}/${unit}.x")
    endforeach()
    execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu ${inputs} -o "${OUTPUT}/units.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "units (${order}): constant objects of another unit\n${out}${err}")
    endif()
endforeach()

# Macros: one emits member reads of a constant object, the initializer that a
# derived record needs, and one reads a constant table while it expands.
accept(macros [=[
struct entity { i32 hp; i32 x; };
struct receiver {
    i32 (*harm)(struct entity *self, i32 amount);
    void (*push)(struct entity *self, i32 dx);
    i32 weight;
};
typedef struct receiver receiver_t;
static i32 hero_harm(struct entity *self, i32 amount) { return self->hp -= amount; }
static i32 partner_harm(struct entity *self, i32 amount) { return self->hp -= amount / 2; }
static void hero_push(struct entity *self, i32 dx) { self->x += dx; }
static const receiver_t hero = { .harm = hero_harm, .push = hero_push, .weight = 5 };

static u32 same_spelling(in $::meta::bytes a, in $::meta::bytes b) {
    if ($::meta::len(a) != $::meta::len(b)) return 0u32;
    for (uptr i = 0; i < $::meta::len(a); ++i)
        if ($::meta::at(a, i) != $::meta::at(b, i)) return 0u32;
    return 1u32;
}

// derive!(T base replaced...): an entry for each member of T that the list
// does not replace, read from the constant object `base`.
[[macro]]
static $::meta::tokens derive($::meta::tokens input) {
    $::meta::type t = $::meta::type($::meta::at(input, 0));
    $::meta::tokens base = $::meta::at(input, 1);
    $::meta::tokens body = $::meta::parse("");
    for (uptr i = 0; i < $::meta::member_count(t); ++i) {
        $::meta::tokens name = $::meta::member_name(t, i);
        u32 replaced = 0u32;
        for (uptr j = 2; j < $::meta::len(input); ++j)
            replaced |= same_spelling($::meta::spelling(name), $::meta::spelling($::meta::at(input, j)));
        if (replaced == 0u32)
            body = $::meta::concat(body, $::quote { .$::unquote(name) = $::unquote(base).$::unquote(name), });
    }
    return body;
}

static const receiver_t partner = { derive!(receiver_t hero harm) .harm = partner_harm };
$::static_assert(partner.weight == 5, "copied by the macro");

static const u32 widths[3] = { 8, 16, 32 };

[[macro]]
static $::meta::tokens widest($::meta::tokens input) {
    u32 best = 0;
    for (uptr i = 0; i < 3; ++i)
        if (widths[i] > best) best = widths[i];
    if (best == 32) return $::quote { 32u32 };
    return $::quote { 0u32 };
}
$::static_assert(widest!() == 32u32, "read while expanding");

global i32 use(struct entity *e) {
    partner.push(e, 1);
    return partner.harm(e, 4);
}
]=])
file(READ "${OUTPUT}/macros.s" assembly)
if(NOT assembly MATCHES "partner:[^:]*\\.quad[^\n]*\n[^\n]*\\.quad")
    message(FATAL_ERROR "macros: the derived record lacks its function relocations\n${assembly}")
endif()

# A constant pointer to a constant compound literal, and its copy.
accept(literal_pointer [=[
static const i32 *const p = (const i32[]){ 1, 2 };
$::static_assert(p[1] == 2, "through a literal");
static const i32 *const q = p;
global i32 g() { return q[1]; }
]=])
file(READ "${OUTPUT}/literal_pointer.s" assembly)
if(NOT assembly MATCHES "_q:\n[ \t]*\\.quad[ \t]+[^\n]*compound")
    message(FATAL_ERROR "literal_pointer: the copy does not name the literal\n${assembly}")
endif()

# A macro reads a constant object whose initializer calls a generic function.
accept(macro_generic [=[
static T larger<T>(in T a, in T b) { return a > b ? a : b; }
static const u32 limit = larger<u32>(3, 7);
[[macro]]
static $::meta::tokens limit_value($::meta::tokens input) {
    if (limit == 7) return $::quote { 7u32 };
    return $::quote { 0u32 };
}
$::static_assert(limit_value!() == 7u32, "a generic initializer read while expanding");
]=])

# Objects that are not constant objects.
reject(not_const 2:22 "runtime/static storage cannot be read during translation-time evaluation: 'm' is not const"
    "static i32 m = 1;\nstatic const i32 x = m;")
reject(weak 2:22 "runtime/static storage cannot be read during translation-time evaluation: 'w' is weak"
    "[[weak]] global const i32 w = 1;\nstatic const i32 x = w + 1;")
reject(noinit 2:22 "runtime/static storage cannot be read during translation-time evaluation: 'n' is noinit"
    "[[noinit]] static const i32 n;\nstatic const i32 x = n + 1;")
reject(volatile 2:22 "runtime/static storage cannot be read during translation-time evaluation: 'v' has a volatile or atomic type"
    "static const volatile i32 v = 1;\nstatic const i32 x = v;")
reject(fixed_address 2:22 "runtime/static storage cannot be read during translation-time evaluation: 'a' has a fixed address"
    "[[address(0x1000)]] global const i32 a = 1;\nstatic const i32 x = a;")
reject(through_pointer 3:22 "runtime/static storage cannot be read during translation-time evaluation: 'm' is not const"
    "static i32 m = 1;\nstatic const i32 *const p = &m;\nstatic const i32 x = *p;")
reject(enumerator 2:14 "runtime/static storage cannot be read during translation-time evaluation: 'm' is not const"
    "static i32 m = 1;\nenum e { a = m };")
# A cycle through initializers.
reject(cycle 2:15 "the initializer of constant object 'a' depends on its own value"
    "const i32 a;\nconst i32 b = a + 1;\nconst i32 a = b + 1;")
reject(self_cycle 2:35 "the initializer of constant object 's' depends on its own value"
    "struct pair { i32 k; i32 l; };\nstatic const struct pair s = { 1, s.k };")
