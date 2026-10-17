// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Reductions updated under a condition: counts selected by conjunctions of
// floating comparisons and comparisons of the index with a 64-bit
// invariant, which a vectorized loop evaluates in 32-bit lanes, also beside
// scalar floating updates. test_entry returns zero when every result
// matches a loop that counts down, which no vectorizer takes.

static f32 box_x[32];
static f32 box_y[32];
static u32 weights[32];
static f32 drift_x[32];

static f32 distance(in f32 a, in f32 b) { return a > b ? a - b : b - a; }

[[noinline]] global u32 contacts(in uptr k, in f32 x, in f32 y) {
    u32 total = 0u32;
    for (uptr j = 0; j < 32; ++j) {
        if (j != k && distance(box_x[j], x) < 2.0f32 &&
            distance(box_y[j], y) < 2.0f32) {
            total = total + 1u32;
        }
    }
    return total;
}

[[noinline]] global u32 contacts_reference(in uptr k, in f32 x, in f32 y) {
    u32 total = 0u32;
    for (uptr j = 32; j > 0; --j) {
        const uptr at = j - 1;
        if (at != k && distance(box_x[at], x) < 2.0f32 &&
            distance(box_y[at], y) < 2.0f32) {
            total = total + 1u32;
        }
    }
    return total;
}

// Each step moves one box with scalar arithmetic, then counts in lanes.
[[noinline]] global u32 drifting_contacts(in uptr steps, in f32 x) {
    u32 total = 0u32;
    for (uptr n = 0; n < steps; ++n) {
        const uptr k = n & 31;
        drift_x[k] = drift_x[k] + 0.75f32;
        for (uptr j = 0; j < 32; ++j) {
            if (j != k && distance(drift_x[j], x) < 2.0f32) {
                total = total + 1u32;
            }
        }
    }
    return total;
}

[[noinline]] global u32 drifting_contacts_reference(in uptr steps, in f32 x) {
    u32 total = 0u32;
    for (uptr n = 0; n < steps; ++n) {
        const uptr k = n & 31;
        drift_x[k] = drift_x[k] + 0.75f32;
        for (uptr j = 32; j > 0; --j) {
            if (j - 1 != k && distance(drift_x[j - 1], x) < 2.0f32) {
                total = total + 1u32;
            }
        }
    }
    return total;
}

[[noinline]] global u32 weighted_below(in uptr k) {
    u32 total = 0u32;
    for (uptr j = 0; j < 32; ++j) {
        if (j < k) total = total + weights[j];
    }
    return total;
}

[[noinline]] global u32 weighted_below_reference(in uptr k) {
    u32 total = 0u32;
    for (uptr j = 32; j > 0; --j) {
        if (j - 1 < k) total = total + weights[j - 1];
    }
    return total;
}

[[noinline]] global u32 skipped(in uptr k, in f32 x) {
    u32 total = 7u32;
    for (uptr j = 0; j < 32; ++j) {
        if (j >= k || box_x[j] < x) {
        } else {
            total = total + 2u32;
        }
    }
    return total;
}

[[noinline]] global u32 skipped_reference(in uptr k, in f32 x) {
    u32 total = 7u32;
    for (uptr j = 32; j > 0; --j) {
        if (j - 1 >= k || box_x[j - 1] < x) {
        } else {
            total = total + 2u32;
        }
    }
    return total;
}

[[noinline]] global u32 marked(in f32 x) {
    u32 bits = 0u32;
    for (uptr j = 0; j < 32; ++j) {
        if (box_x[j] > x) bits = bits | weights[j];
    }
    return bits;
}

[[noinline]] global u32 marked_reference(in f32 x) {
    u32 bits = 0u32;
    for (uptr j = 32; j > 0; --j) {
        if (box_x[j - 1] > x) bits = bits | weights[j - 1];
    }
    return bits;
}

// The call keeps the loop scalar, so its branches stay as they are.
[[noinline]] global u32 weight_of(in uptr j) { return weights[j] & 3u32; }

[[noinline]] global u32 called_contacts(in uptr k, in f32 x, in f32 y) {
    u32 total = 0u32;
    for (uptr j = 0; j < 32; ++j) {
        if (j != k && distance(box_x[j], x) < 2.0f32 &&
            distance(box_y[j], y) < 2.0f32) {
            total = total + weight_of(j);
        }
    }
    return total;
}

[[noinline]] global u32 called_contacts_reference(in uptr k, in f32 x,
                                                  in f32 y) {
    u32 total = 0u32;
    for (uptr j = 32; j > 0; --j) {
        const uptr at = j - 1;
        if (at != k && distance(box_x[at], x) < 2.0f32 &&
            distance(box_y[at], y) < 2.0f32) {
            total = total + weight_of(at);
        }
    }
    return total;
}

global u32 test_entry() {
    u32 seed = 12345u32;
    for (u32 j = 0u32; j < 32u32; ++j) {
        seed = seed * 1664525u32 + 1013904223u32;
        box_x[j] = (f32)(seed >> 27);
        box_y[j] = (f32)((seed >> 22) & 31u32);
        weights[j] = seed >> 7;
    }
    const uptr ks[8] = {0, 5, 17, 31, 32, 100, 4294967301, 18446744073709551615};
    for (u32 index = 0u32; index < 8u32; ++index) {
        const uptr k = ks[index];
        for (u32 step = 0u32; step < 8u32; ++step) {
            const f32 x = (f32)(step * 4u32) + 0.5f32;
            const f32 y = (f32)(31u32 - step * 3u32) - 0.25f32;
            if (contacts(k, x, y) != contacts_reference(k, x, y)) return 1u32 + index;
            if (called_contacts(k, x, y) != called_contacts_reference(k, x, y)) {
                return 90u32 + index;
            }
            if (skipped(k, x) != skipped_reference(k, x)) return 20u32 + index;
            if (marked(x) != marked_reference(x)) return 40u32 + step;
        }
        if (weighted_below(k) != weighted_below_reference(k)) return 60u32 + index;
    }
    for (u32 j = 0u32; j < 32u32; ++j) drift_x[j] = box_x[j];
    const u32 drifted = drifting_contacts(70, 9.5f32);
    for (u32 j = 0u32; j < 32u32; ++j) drift_x[j] = box_x[j];
    if (drifted != drifting_contacts_reference(70, 9.5f32)) return 80u32;
    return 0u32;
}
