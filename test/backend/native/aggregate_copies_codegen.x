// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Record copies and initializers whose code aggregate_copies_codegen.cmake
// checks: no byte loops, no temporaries, and one copy in address order.

struct motion { i32 walk; i32 dash; i32 jump; i32 fall; };
struct row { struct motion mot; i32 harm; i32 heal; u8 name[8]; };
struct pair { i32 k; i32 l; };
struct big { u32 v[64]; };

global void copy_row(in struct row *to, in const struct row *r) {
    *to = *r;
}

global void copy_big(in struct big *to, in const struct big *from) {
    *to = *from;
}

global void literal_row(in struct row *to, in i32 dash) {
    *to = (struct row){ .mot.dash = dash };
}

global void with_dash(in struct row *to, in const struct row *r, in i32 dash) {
    *to = (struct row){ *r, .mot.dash = dash };
}

global void update_harm(in struct row *r, in i32 harm) {
    *r = (struct row){ *r, .harm = harm };
}

global i32 consume(in const struct pair *p);

global i32 pair_entries(in i32 v) {
    struct pair p = { v, 2 };
    return consume(&p);
}

global volatile struct pair device;

global void store_device(in const struct pair *p) {
    device = *p;
}
