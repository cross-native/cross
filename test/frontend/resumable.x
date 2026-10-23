// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Scripts written with the resumable extension, run a step per call.
#include "resumable.xh"

syntax script::resumable;

struct walker {
    i32 x;
    label resume;
    u32 steps;
};

resumable(self->resume) void walk(struct walker *self) {
    for (self->steps = 0; self->steps < 3; ++self->steps) {
        self->x += 1;
        yield;
    }
    self->x = 0;
}

// A nested script keeps its state in the state of the script that waits for it.
struct blink { label resume; u32 count; };
struct patrol { label resume; struct blink lamp; u32 trips; i32 log; };

resumable(self->resume) void flash(struct blink *self) {
    self->count = 0;
    while (self->count < 2) {
        ++self->count;
        yield;
    }
}

resumable(self->resume) void route(struct patrol *self) {
    for (self->trips = 0; self->trips < 2; ++self->trips) {
        self->log = self->log * 10 + 1;
        do {
            flash(&self->lamp);
            yield;
        } while (self->lamp.resume);
        self->log = self->log * 10 + 2;
    }
}

// A computed goto reaches only the labels whose address is taken; `skip` is
// a direct goto's target, and `kept` lives across both kinds of edge.
[[noinline]] static i32 dispatch(in u32 selector, in i32 seed) {
    i32 kept = seed * 3;
    label to = selector == 0 ? dispatch::first : dispatch::second;
    goto to;
skip:
    kept += 100;
    return kept;
first:
    kept += 1;
    goto skip;
second:
    return kept + 2;
}

global u32 resumable_check() {
    struct walker w = {};
    i32 trace = 0;
    for (u32 call = 0; call < 9; ++call) {
        walk(&w);
        trace = trace * 10 + w.x;
    }
    if (trace != 123012301 || !w.resume) return 2;
    struct patrol p = {};
    u32 calls = 0;
    do {
        route(&p);
        ++calls;
    } while (p.resume);
    if (calls != 7 || p.log != 1212 || p.trips != 2 || p.lamp.resume) return 3;
    if (dispatch(0, 5) != 116 || dispatch(1, 5) != 17) return 4;
    return 1;
}
