// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

global u8 duff_source[64];
global u8 duff_target[64];

// Case labels inside a do-while body (Duff's device).
[[noinline]] global void duff_copy(in u8 *to, in const u8 *from, in u32 count) {
    u32 rounds = (count + 7u32) / 8u32;
    switch (count % 8u32) {
    case 0: do { *to++ = *from++;
    case 7:      *to++ = *from++;
    case 6:      *to++ = *from++;
    case 5:      *to++ = *from++;
    case 4:      *to++ = *from++;
    case 3:      *to++ = *from++;
    case 2:      *to++ = *from++;
    case 1:      *to++ = *from++;
            } while (--rounds > 0u32);
    }
}

// Entering an if arm skips its condition.
[[noinline]] global i32 arm_entry(in i32 x) {
    switch (x) {
        if (x) case 1: return 1;
    }
    return 0;
}

// Entering a loop body skips the initializer and test; later iterations
// run the increment and test, and break/continue bind to the loop.
[[noinline]] global u32 loop_entry(in u32 start) {
    u32 total = 0u32;
    u32 i = 0u32;
    switch (start) {
    case 0:
        for (i = 0u32; i < 4u32; i++) {
            total += 1u32;
    case 1:
            total += 10u32;
        }
        break;
    default:
        while (i < 3u32) {
            ++i;
            if (i == 2u32) continue;
            total += 100u32;
    case 2:
            total += 1000u32;
            if (total > 5000u32) break;
        }
    }
    return total * 16u32 + i;
}

// Jumping past an ordinary declaration is allowed.
[[noinline]] global i32 block_entry(in i32 x) {
    i32 result = 0;
    switch (x) {
    case 0: {
        i32 value = 5;
        result += value;
    case 1:
        value = 7;
        result += value;
        break;
    }
    case 2:
        result = 100;
    }
    return result;
}

// Each case label belongs to its nearest enclosing switch.
[[noinline]] global u32 nested_switch_entry(in u32 outer, in u32 inner) {
    u32 result = 0u32;
    switch (outer) {
    case 0:
        while (result < 100u32) {
            switch (inner) {
            case 0: result += 1u32; break;
            case 1: result += 2u32;
            default: result += 3u32;
            }
    case 1:
            result += 10u32;
            if (result > 40u32) break;
        }
        break;
    default:
        result = 7u32;
    }
    return result;
}

// Labels in statements after an unconditional transfer stay reachable.
[[noinline]] global u32 skipped_case_entry(in u32 x) {
    switch (x) {
    case 0:
        return 10u32;
        while (x) {
    case 1:
            return 20u32;
        }
    }
    return 30u32;
}

[[noinline]] global u32 skipped_label_entry(in u32 x) {
    if (x == 3u32) goto inside;
    return 1u32;
    if (x == 5u32) {
inside:
        return 7u32;
    }
    return 2u32;
}

// A variable-length array in a braced case block is released whenever
// control leaves the block, so every round reuses the same storage.
[[noinline]] global u32 vla_block_entry(in u32 selector, in u32 count,
                                        in u32 rounds) {
    u32 total = 0u32;
    uptr previous = 0;
    for (u32 round = 0u32; round < rounds; round++) {
        switch (selector) {
        case 1: {
            u8 bytes[count];
            uptr address = bytes;
            if (round != 0u32 && address != previous) return 0xdeadu32;
            previous = address;
            for (u32 i = 0u32; i < count; i++) bytes[i] = (u8)(i + round);
            total += bytes[count - 1u32];
            break;
        }
        case 2: {
            u32 words[count];
            uptr address = words;
            if (round != 0u32 && address != previous) return 0xbeefu32;
            previous = address;
            words[count - 1u32] = round;
            if ((round & 1u32) != 0u32) continue;
            total += words[count - 1u32];
            break;
        }
        default:
            total += 1u32;
        }
    }
    return total;
}

// A variable-length array may follow the last label of a switch body.
[[noinline]] global u32 vla_tail_entry(in u32 selector, in u32 count) {
    u32 result = 0u32;
    switch (selector) {
    case 1:
        result = 5u32;
    default:
        u32 values[count];
        values[count - 1u32] = result + count;
        result = values[count - 1u32];
    }
    return result;
}

global i32 switch_nested_entry() {
    for (u32 i = 0u32; i < 64u32; i++) duff_source[i] = (u8)(i * 7u32 + 1u32);
    for (u32 count = 1u32; count <= 40u32; count++) {
        for (u32 i = 0u32; i < 64u32; i++) duff_target[i] = 0u8;
        duff_copy(duff_target, duff_source, count);
        for (u32 i = 0u32; i < 64u32; i++) {
            u8 expected = i < count ? duff_source[i] : 0u8;
            if (duff_target[i] != expected) return 1;
        }
    }
    if (arm_entry(1) != 1 || arm_entry(0) != 0 || arm_entry(2) != 0) return 2;
    if (loop_entry(0u32) != 708u32 || loop_entry(1u32) != 692u32 ||
        loop_entry(2u32) != 51203u32 || loop_entry(5u32) != 35203u32) {
        return 3;
    }
    if (block_entry(0) != 12 || block_entry(1) != 7 ||
        block_entry(2) != 100 || block_entry(3) != 0) {
        return 4;
    }
    if (nested_switch_entry(0u32, 0u32) != 44u32 ||
        nested_switch_entry(0u32, 1u32) != 45u32 ||
        nested_switch_entry(0u32, 5u32) != 52u32 ||
        nested_switch_entry(1u32, 0u32) != 43u32 ||
        nested_switch_entry(2u32, 0u32) != 7u32) {
        return 5;
    }
    if (skipped_case_entry(0u32) != 10u32 || skipped_case_entry(1u32) != 20u32 ||
        skipped_case_entry(2u32) != 30u32) {
        return 6;
    }
    if (skipped_label_entry(3u32) != 7u32 || skipped_label_entry(5u32) != 1u32) {
        return 7;
    }
    u32 bytes_expected = 0u32;
    for (u32 round = 0u32; round < 300u32; round++) {
        bytes_expected += (199u32 + round) & 0xffu32;
    }
    if (vla_block_entry(1u32, 200u32, 300u32) != bytes_expected) return 8;
    if (vla_block_entry(2u32, 1000u32, 300u32) != 22350u32) return 9;
    if (vla_block_entry(3u32, 1u32, 300u32) != 300u32) return 10;
    if (vla_tail_entry(1u32, 3u32) != 8u32 || vla_tail_entry(0u32, 4u32) != 4u32) {
        return 11;
    }
    return 0;
}
