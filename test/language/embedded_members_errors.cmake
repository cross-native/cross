# Copyright (C) 2026 Cross contributors
# SPDX-License-Identifier: GPL-3.0-or-later

foreach(required CC OUTPUT)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "${required} must name a value")
    endif()
endforeach()
file(MAKE_DIRECTORY "${OUTPUT}")

# Each case must fail with the message at the given line and column.
function(reject name location expected source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    execute_process(COMMAND "${CC}" -target x86_64-unknown-linux-gnu -S -O0
        "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
    if(status EQUAL 0 OR NOT err MATCHES "${name}.x:${location}: error: [^\n]*${expected}")
        message(FATAL_ERROR "${name}: missing '${expected}' at ${location}\n${out}\n${err}")
    endif()
endfunction()

# Each case must compile.
function(accept name source)
    file(WRITE "${OUTPUT}/${name}.x" "${source}")
    execute_process(COMMAND "${CC}" -target x86_64-unknown-linux-gnu -S -O0
        "${OUTPUT}/${name}.x" -o "${OUTPUT}/${name}.s"
        RESULT_VARIABLE status OUTPUT_VARIABLE out ERROR_VARIABLE err TIMEOUT 20)
    if(NOT status EQUAL 0)
        message(FATAL_ERROR "${name}: expected success\n${out}\n${err}")
    endif()
endfunction()

set(records [=[
struct entity { i32 x; i32 y; };
struct other { i32 x; u32 z; };
struct pair { struct entity; struct other; };
struct twice { struct { struct entity; }; struct { struct entity; }; };
]=])

reject(ambiguous_access 6:45 "member 'x' is ambiguous" "${records}
global i32 read(struct pair *p) { return p->x; }
")
reject(ambiguous_designator 6:30 "member 'x' is ambiguous" "${records}
global struct pair value = { .x = 1 };
")
reject(ambiguous_offsetof 6:43 "member 'x' is ambiguous" "${records}
\$::static_assert(\$::offsetof(struct pair, x) == 0, \"offset\");
")
reject(ambiguous_conversion 6:57 "pointer conversion to an embedded member's type is ambiguous" "${records}
global struct entity *convert(struct twice *t) { return t; }
")
reject(name_collision 3:5 "embedded member name 'entity' collides with another member" [=[
struct entity { i32 x; };
struct outer { struct entity;
    u32 entity; };
]=])
reject(typedef_collision 3:5 "embedded member name 'base_t' collides with another member" [=[
typedef struct base { i32 x; } base_t;
struct outer { u32 base_t;
    base_t; };
]=])
reject(incomplete 2:16 "incomplete record type 'later'" [=[
struct later;
struct outer { struct later; u32 tail; };
]=])
reject(missing_member 3:46 "record has no member named 'w'" [=[
struct entity { i32 x; };
struct outer { struct entity; };
global i32 read(struct outer *o) { return o->w; }
]=])
reject(implicit_downward 3:57 "implicit pointer conversion discards qualifiers or uses incompatible pointee types" [=[
struct entity { i32 x; };
struct outer { u8 tag; struct entity; };
global struct outer *convert(struct entity *e) { return e; }
]=])
reject(discarded_const 3:63 "pointer conversion to or from an embedded member's type discards qualifiers in return" [=[
struct entity { i32 x; };
struct outer { u8 tag; struct entity; };
global struct entity *convert(const struct outer *o) { return o; }
]=])
reject(const_member 3:57 "pointer conversion to or from an embedded member's type discards qualifiers in return" [=[
struct entity { i32 x; };
struct outer { u8 tag; const struct entity; };
global struct entity *convert(struct outer *o) { return o; }
]=])
reject(not_embedded 3:57 "implicit pointer conversion discards qualifiers or uses incompatible pointee types" [=[
struct entity { i32 x; };
struct outer { u8 tag; struct entity e; };
global struct entity *convert(struct outer *o) { return o; }
]=])
reject(exhaustive 4:31 "does not name member 'entity'" [=[
struct entity { i32 x; i32 y; };
struct outer [[exhaustive]] { struct entity; u32 tail; };
global struct outer complete = { .x = 1, .tail = 2 };
global struct outer partial = { .tail = 2 };
]=])
reject(exhaustive_anonymous 3:31 "does not initialize its anonymous embedded member" [=[
struct outer [[exhaustive]] { struct { i32 x; i32 y; }; u32 tail; };
global struct outer complete = { { 1, 2 }, 3 };
global struct outer partial = { .tail = 3 };
]=])
reject(const_promoted 3:43 "cannot write a const subobject" [=[
struct entity { i32 x; };
struct outer { const struct entity; };
global void write(struct outer *o) { o->x = 1; }
]=])

# Ambiguity is an error only at an access; a direct member hides promoted
# ones, and an exhaustive record accepts its embedded member by position, by
# name, or through a promoted designator.
accept(valid [=[
struct entity { i32 x; i32 y; };
struct other { i32 x; u32 z; };
struct pair { struct entity; struct other; };
struct shadow { struct entity; struct other; i32 x; };
struct outer [[exhaustive]] { struct entity; struct { u32 a; u32 b; }; u32 tail; };
global i32 read(struct pair *p) { return p->y + (i32)p->z + p->entity.x + p->other.x; }
global i32 hidden(struct shadow *s) { return s->x + s->entity.x; }
global struct outer by_position = { { 1, 2 }, { 3, 4 }, 5 };
global struct outer by_name = { .entity = { 1, 2 }, .a = 3, .tail = 5 };
global struct outer promoted = { .y = 2, .b = 4, .tail = 5 };
]=])
