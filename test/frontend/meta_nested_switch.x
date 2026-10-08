// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

namespace MetaNestedSwitch {
[[eval_only]] static u32 branches(in u32 selector) {
    u32 value = 0u32, tests = 0u32;
    switch (selector) {
        if (++tests) { case 1u32: value += 11u32; }
        else { case 2u32: value += 23u32; }
        value += 5u32;
        break;
        default: value = 47u32;
    }
    return value + tests * 100u32;
}
[[eval_only]] static u32 while_entry() {
    u32 index = 0u32, tests = 0u32, total = 0u32, prefix = 0u32;
    switch (1u32) {
        while (++tests < 3u32) {
            ++prefix;
            case 1u32:
            total += ++index;
            if (index == 1u32) continue;
            if (index == 3u32) break;
        }
        total += 100u32;
        break;
        default: return 0u32;
    }
    return total * 1000u32 + tests * 100u32 + prefix * 10u32 + index;
}
[[eval_only]] static u32 do_entry() {
    u32 index = 0u32, tests = 0u32, total = 0u32;
    switch (1u32) {
        do {
            total += 100u32;
            case 1u32:
            total += ++index;
            if (index < 2u32) continue;
        } while (++tests < 3u32);
        total += 7u32;
        break;
        default: return 0u32;
    }
    return total * 100u32 + tests * 10u32 + index;
}
[[eval_only]] static u32 for_entry() {
    u32 index = 0u32, tests = 0u32, total = 0u32, prefix = 0u32, initial = 0u32;
    switch (1u32) {
        for (++initial; ++tests && index < 3u32; ++index) {
            ++prefix;
            case 1u32:
            total += index + 1u32;
            if (index == 0u32) continue;
        }
        total += 7u32;
        break;
        default: return 0u32;
    }
    return total * 10000u32 + initial * 1000u32 + tests * 100u32 + prefix * 10u32 + index;
}
[[eval_only]] static u32 ownership() {
    u32 total = 0u32;
    for (u32 outer = 0u32; outer < 3u32; ++outer) {
        switch (outer) {
            if (0u32) { case 0u32: total += 1u32; continue; }
            do { case 1u32: total += 3u32; break; } while (1u32);
            total += 5u32;
            break;
            default:
                switch (0u32) { case 0u32: total += 7u32; break; }
                total += 11u32;
                break;
        }
        total += 13u32;
    }
    return total;
}
[[eval_only]] static u32 returns(in u32 selector) {
    switch (selector) {
        for (;;) { if (0u32) { case 1u32: { u32 value = 17u32; return value; } } }
        default: return 19u32;
    }
}
[[eval_only]] static u32 default_entry() {
    u32 value = 0u32;
    switch (9u32) {
        if (0u32) { default: value = 17u32; }
        value += 2u32;
    }
    return value;
}
[[eval_only]] static uptr for_type() {
    switch (1u32) {
        for (u32 skipped = 99u32; 0u32;) { case 1u32: return sizeof(skipped); }
    }
    return 0uptr;
}
[[eval_only]] static bool check() {
    return branches(1u32) == 16u32 && branches(2u32) == 28u32 && branches(3u32) == 47u32 &&
        while_entry() == 106223u32 && do_entry() == 21333u32 && for_entry() == 130323u32 &&
        ownership() == 53u32 && returns(1u32) == 17u32 && returns(2u32) == 19u32 &&
        default_entry() == 19u32 && for_type() == 4uptr;
}
}
