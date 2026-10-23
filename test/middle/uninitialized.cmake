# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

# -Wuninitialized and -Wmaybe-uninitialized: reads that the assignment proof
# finds unassigned on every path or on some path, what keeps it quiet,
# protothreads through switch and computed goto with their notes, the
# computed-goto targets, the groups and severities, and the same reports at
# every optimization level.
foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} is required")
    endif()
endforeach()
file(REMOVE_RECURSE "${OUTPUT}")
file(MAKE_DIRECTORY "${OUTPUT}")

# compile(CASE SOURCE FLAGS...) sets `status` and `err`.
function(compile case source)
    file(WRITE "${OUTPUT}/${case}.x" "${source}")
    execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu ${ARGN}
        "${OUTPUT}/${case}.x" -o "${OUTPUT}/${case}.s"
        RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE error)
    set(status "${result}" PARENT_SCOPE)
    set(err "${out}${error}" PARENT_SCOPE)
endfunction()

# expect(CASE SOURCE PATTERNS FLAGS...): compiles and finds each pattern of
# the list PATTERNS in the diagnostics, which hold exactly that many
# warnings or errors.
function(expect case source patterns)
    compile(${case} "${source}" ${ARGN})
    string(REGEX MATCHALL "(warning|error): " reports "${err}")
    list(LENGTH reports total)
    list(LENGTH patterns wanted)
    if(NOT total EQUAL wanted)
        message(FATAL_ERROR "${case}: expected ${wanted} reports, got ${total}\n${err}")
    endif()
    foreach(pattern IN LISTS patterns)
        if(NOT err MATCHES "${pattern}")
            message(FATAL_ERROR "${case}: expected '${pattern}'\n${err}")
        endif()
    endforeach()
    set(err "${err}" PARENT_SCOPE)
    set(status "${status}" PARENT_SCOPE)
endfunction()

function(expect_clean case source)
    compile(${case} "${source}" ${ARGN})
    if(NOT status EQUAL 0 OR NOT err STREQUAL "")
        message(FATAL_ERROR "${case}: expected no diagnostic\n${err}")
    endif()
endfunction()

function(expect_note case pattern)
    if(NOT err MATCHES "${pattern}")
        message(FATAL_ERROR "${case}: expected note '${pattern}'\n${err}")
    endif()
endfunction()

set(definite "is used uninitialized \\[-Wuninitialized\\]")
set(maybe "may be used uninitialized \\[-Wmaybe-uninitialized\\]")

# --- Paths ---

set(paths [=[
bool get(out i32 v);
void take(in i32 v);
global i32 straight() {
    i32 x;
    return x;
}
global i32 branch(in bool c) {
    i32 x;
    if (c) x = 1;
    return x;
}
global i32 both(in bool c) {
    i32 x;
    if (c) x = 1; else x = 2;
    return x;
}
global i32 loop(in i32 n) {
    i32 x;
    for (i32 i = 0; i < n; ++i) x = i;
    return x;
}
global i32 fresh_each_time(in i32 n) {
    i32 s = 0;
    for (i32 i = 0; i < n; ++i) {
        i32 t;
        if (i == 0) t = 5;
        s += t;
    }
    return s;
}
global i32 initialized_each_time(in i32 n) {
    i32 s = 0;
    for (i32 i = 0; i < n; ++i) {
        i32 t = i;
        s += t;
    }
    return s;
}
global i32 forever(in bool c) {
    i32 x;
    while (1) { if (c) { x = 1; break; } }
    return x;
}
global i32 into_scope(in bool c) {
    if (c) goto inside;
    {
        i32 x = 1;
inside:
        return x;
    }
}
global i32 when_true(in bool c) {
    i32 v;
    if (c && get(v)) return v;
    if (!c || !get(v) || v < 0) return 0;
    return v;
}
global i32 when_false(in bool c) {
    i32 v;
    if (c || get(v)) return v;
    return 0;
}
global i32 conditional(in bool c) {
    i32 v;
    i32 r = c ? get(v) : 0;
    return r + v;
}
global i32 argument() {
    i32 v;
    take(v);
    get(v);
    return v;
}
global i32 compound() {
    i32 v;
    v += 1;
    return v;
}
global i32 volatile_local() {
    volatile i32 v;
    return v;
}
global i32 atomic_local() {
    i32 [[atomic]] a;
    return a;
}
global i32 through_pointer(in bool c) {
    i32 x;
    i32 y;
    i32 *p = &x;
    if (c) *p = 3;
    i32 *q = &y;
    *q = 4;
    return *p + y;
}
]=])
expect(paths "${paths}" "paths.x:5:12: warning: 'x' ${definite};paths.x:10:12: warning: 'x' ${maybe};paths.x:20:12: warning: 'x' ${maybe};paths.x:27:14: warning: 't' ${maybe};paths.x:49:16: warning: 'x' ${maybe};paths.x:60:29: warning: 'v' ${maybe};paths.x:66:16: warning: 'v' ${maybe};paths.x:70:10: warning: 'v' ${definite};paths.x:76:7: warning: 'v' ${definite};paths.x:81:12: warning: 'v' ${definite};paths.x:85:12: warning: 'a' ${definite};paths.x:94:12: warning: 'x' ${maybe}" -Wall)
expect_note(paths "paths.x:4:9: note: 'x' was declared here")

# --- Aggregates, unions, bit-fields, and run-time indices ---

set(parts [=[
struct pair { i32 a; i32 b; };
union choice { u8 narrow; u32 wide; };
struct bits { u32 low : 3; u32 high : 5; };
typedef f32 float4 [[ext_vector_type(4)]];
global i32 member() {
    struct pair p;
    p.a = 1;
    return p.a + p.b;
}
global i32 element() {
    i32 a[4];
    a[0] = 1;
    a[1] = 2;
    return a[0] + a[3];
}
global i32 copied() {
    struct pair p;
    struct pair q = p;
    return q.a;
}
global u32 any_member() {
    union choice u;
    u.narrow = 1u8;
    return u.wide;
}
global u32 field() {
    struct bits b;
    b.low = 1u32;
    return b.high;
}
global u32 fields() {
    struct bits b;
    b.low = 1u32;
    b.high = 2u32;
    return b.low + b.high;
}
global f32 lane() {
    float4 v;
    v[1] = 2.0f32;
    return v[1];
}
global i32 indexed_store(in i32 i) {
    i32 a[4];
    a[i] = 1;
    return a[2];
}
global i32 indexed_loop() {
    i32 a[8];
    for (i32 i = 0; i < 8; ++i) a[i] = i;
    i32 s = 0;
    for (i32 i = 0; i < 8; ++i) s += a[i];
    return s + a[3];
}
global i32 indexed_read(in i32 i) {
    i32 a[4];
    return a[i];
}
global i32 indexed_part(in i32 i) {
    i32 a[4];
    a[0] = 1;
    return a[i];
}
global i32 large_initializer() {
    i32 a[16] = { 1 };
    u8 text[40] = "short";
    return a[15] + (i32)text[39];
}
]=])
expect(parts "${parts}" "parts.x:8:20: warning: 'p.b' ${definite};parts.x:14:19: warning: 'a\\[3\\]' ${definite};parts.x:29:14: warning: 'b.high' ${definite};parts.x:56:12: warning: 'a' ${definite}" -Wall)

# --- Escaping addresses ---

expect_clean(escapes [=[
struct pair { i32 a; i32 b; };
struct holder { i32 *p; };
void fill(i32 *p);
void fill_pair(struct pair *p);
void keep(struct holder *h);
global i32 *last;
global i32 passed() {
    i32 x;
    fill(&x);
    return x;
}
global i32 member_passed() {
    struct pair p;
    fill(&p.a);
    return p.b;
}
global i32 stored() {
    i32 x;
    last = &x;
    fill_pair((struct pair *)0);
    return x;
}
global i32 held() {
    i32 x;
    struct holder h;
    h.p = &x;
    keep(&h);
    return x;
}
global i32 through_pointer_cell() {
    i32 x;
    i32 *p = &x;
    i32 **pp = &p;
    fill(*pp);
    return x;
}
global i32 as_integer() {
    i32 x;
    uptr bits = (uptr)&x;
    *(i32 *)bits = 1;
    return x;
}
global u64 register_view() {
    register u64 r "r12";
    return r;
}
]=] -Wall)

# --- Protothreads ---

set(protothreads [=[
struct pt { u32 line; i32 kept; label pc; };
i32 work();
void emit(in i32 v);
global void switched(struct pt *s) {
    i32 lost;
    switch (s->line) {
    case 0:
        for (s->kept = 0; s->kept < 3; ++s->kept) {
            lost = work();
            s->line = 1;
            return;
    case 1:
            emit(lost);
            emit(s->kept);
        }
    }
    s->line = 0;
}
global void labelled(struct pt *s) {
    struct pt *self = s;
    i32 temp;
    if (self->pc) goto self->pc;
    temp = work();
    self->pc = labelled::resumed;
    return;
resumed:
    emit(self->kept);
    emit(temp);
    self->pc = (label)0;
}
global void again(struct pt *self, in bool more) {
    if (self->pc) goto self->pc;
    i32 count = work();
    while (more) {
        if (count > 3) {
            self->pc = again::wait;
            return;
        }
wait:
        emit(count);
        more = 0;
    }
}
]=])
expect(protothreads "${protothreads}" "protothreads.x:13:18: warning: 'lost' ${definite};protothreads.x:28:10: warning: 'temp' ${definite};protothreads.x:40:14: warning: 'count' ${maybe}" -Wall)
expect_note(protothreads "protothreads.x:21:9: note: 'temp' was declared here")
expect_note(protothreads "protothreads.x:22:19: note: this computed goto reaches label 'resumed' with 'temp' unassigned")
expect_note(protothreads "protothreads.x:26:1: note: label 'resumed' is here")
expect_note(protothreads "protothreads.x:32:19: note: this computed goto reaches label 'wait' with 'count' unassigned")
expect_note(protothreads "protothreads.x:39:1: note: label 'wait' is here")
# Not `self`, which the dispatch finds assigned, nor a goto note for `lost`,
# which a switch resumes.
string(REGEX MATCHALL "computed goto" notes "${err}")
list(LENGTH notes count)
if(NOT count EQUAL 2 OR err MATCHES "'self'")
    message(FATAL_ERROR "protothreads: unexpected reports\n${err}")
endif()

# A computed goto reaches the labels whose address the group takes, in code
# or data, wherever it takes them, and the global labels: `quiet` is only a
# direct goto's target.
set(targets [=[
global i32 narrowed(in label to) {
    i32 x;
    goto to;
quiet:
    return x;
taken:
    x = 1;
    goto quiet;
elsewhere:
    return x;
}
global label stored = narrowed::elsewhere;
global void choose(out label to) { to = narrowed::taken; }
global i32 exported(in label to) {
    i32 y;
    goto to;
global label resume_here:
    return y;
}
global i32 untaken(in label to) {
    i32 z;
    goto to;
    return z;
}
]=])
expect(targets "${targets}" "targets.x:10:12: warning: 'x' ${definite};targets.x:18:12: warning: 'y' ${definite}" -Wall)
expect_note(targets "targets.x:3:5: note: this computed goto reaches label 'elsewhere' with 'x' unassigned")
expect_note(targets "targets.x:16:5: note: this computed goto reaches label 'resume_here' with 'y' unassigned")

# --- Generated code ---

set(generated [=[
[[macro]]
static $::meta::tokens copy_fresh($::meta::tokens input) {
    $::meta::tokens temp = $::meta::gensym("temp");
    return $::quote { u32 $::unquote(temp); $::unquote(input) = $::unquote(temp); };
}
static T pick<T>(in bool c) {
    T x;
    if (c) x = (T)1;
    return x;
}
global u32 macro_local() {
    u32 x;
    copy_fresh!(x);
    return x;
}
global i32 instances(in bool c) {
    return pick<i32>(c) + (i32)pick<u8>(c);
}
]=])
expect(generated "${generated}" "warning: 'temp' ${definite};generated.x:9:12: warning: 'x' ${maybe}" -Wall)
expect_note(generated "generated.x:13:5: note: 'temp' was declared here")

# --- Out cells keep their errors ---

expect(out_cell [=[
global void cell(out i32 v) { v = v + 1; }
]=] "out_cell.x:1:35: error: read of 'out' parameter 'v' before assignment" -Wall)

# --- Groups and severities ---

set(both_kinds [=[
global i32 f(in bool c) {
    i32 x;
    i32 y;
    if (c) y = 1;
    return x + y;
}
]=])
set(both_reports "'x' ${definite};'y' ${maybe}")
expect_clean(default "${both_kinds}")
expect(all "${both_kinds}" "${both_reports}" -Wall)
expect(extra "${both_kinds}" "${both_reports}" -Wextra)
expect(alone "${both_kinds}" "${both_reports}" -Wuninitialized)
expect(maybe_alone "${both_kinds}" "'y' ${maybe}" -Wmaybe-uninitialized)
expect(no_maybe "${both_kinds}" "'x' ${definite}" -Wall -Wno-maybe-uninitialized)
# As in GCC, -Wno-uninitialized also turns off what it enables.
expect_clean(no_definite "${both_kinds}" -Wall -Wno-uninitialized)
expect(maybe_only "${both_kinds}" "'y' ${maybe}" -Wall -Wno-uninitialized -Wmaybe-uninitialized)
expect_clean(extra_off "${both_kinds}" -Wextra -Wno-uninitialized)
expect_clean(inhibit "${both_kinds}" -Wall -w)
expect(error_maybe "${both_kinds}" "warning: 'x' ${definite};error: 'y' may be used uninitialized \\[-Werror=maybe-uninitialized\\]"
    -Wall -Werror=maybe-uninitialized)
if(NOT status EQUAL 1)
    message(FATAL_ERROR "error_maybe: -Werror=maybe-uninitialized did not fail\n${err}")
endif()
expect(error_definite "${both_kinds}"
    "error: 'x' is used uninitialized \\[-Werror=uninitialized\\];error: 'y' may be used uninitialized \\[-Werror=maybe-uninitialized\\]"
    -Werror=uninitialized)
expect(error_exempt "${both_kinds}"
    "error: 'x' is used uninitialized \\[-Werror=uninitialized\\];warning: 'y' ${maybe}"
    -Wall -Werror -Wno-error=maybe-uninitialized)

# --- The same reports at every level ---

foreach(level O0 O1 O2 O3 Os Oz Og)
    foreach(case paths parts protothreads targets)
        execute_process(COMMAND "${CC}" -S -target x86_64-unknown-linux-gnu -${level} -Wall
            "${OUTPUT}/${case}.x" -o "${OUTPUT}/${case}-${level}.s"
            RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE error)
        if(level STREQUAL O0)
            set(reference_${case} "${out}${error}")
        elseif(NOT "${out}${error}" STREQUAL "${reference_${case}}")
            message(FATAL_ERROR "${case}: -${level} reports differ from -O0\n${out}${error}")
        endif()
    endforeach()
endforeach()
