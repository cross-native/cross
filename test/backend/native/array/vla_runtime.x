[[runtime_only, noinline]]
static u32 runtime_sum(in u32 values[5], in u32 count) {
    u32 result = 0u32;
    for (u32 index = 0u32; index < count; index += 1u32) {
        result += values[index];
    }
    return result;
}

[[runtime_only, noinline]]
static u32 add_eight(in u32 a, in u32 b, in u32 c, in u32 d,
                     in u32 e, in u32 f, in u32 g, in u32 h) {
    return a + b + c + d + e + f + g + h;
}

[[runtime_only, noinline]]
static void restrict_copy(in u32 * restrict source,
                          in u32 * restrict destination) {
    *destination = *source;
}

[[runtime_only, noinline]]
static u32 goto_vla_cleanup(in uptr count) {
    u32 result = 0u32;
    uptr previous = 0uptr;
    for (u32 index = 0u32; index < 4u32; index += 1u32) {
        {
            stack u8 temporary[count];
            uptr address = temporary;
            if (index != 0u32 && address != previous) return 100u32;
            previous = address;
            temporary[0] = (u8)(index + 1u32);
            result += temporary[0];
            goto completed;
        }
        return 100u32;
completed:
        result += 0u32;
    }
    {
        stack u8 still_active[count];
        still_active[0] = 9u8;
        goto same_state;
        return 100u32;
same_state:
        result += still_active[0];
    }
    return result;
}

global i32 vla_entry() {
    u32 count = 5u32;
    stack u32 values[count];
    [[aligned(64)]] stack u8 aligned_values[count];
    [[aligned(64)]] stack u8 fixed_values[3];
    f80 extended[count];
    u32 matrix[count][3];
    u32 sum = 0u32;

    if (sizeof(values) != (uptr)count * 4uptr) return -1;
    if (sizeof(matrix) != (uptr)count * 12uptr) return -2;
    if (((uptr)aligned_values & 63uptr) != 0uptr) return -4;
    if (((uptr)fixed_values & 63uptr) != 0uptr) return -5;

    for (u32 index = 0u32; index < count; index += 1u32) {
        values[index] = index + 1u32;
        sum += values[index];
    }
    sum += runtime_sum(values, count);
    extended[4] = 3.5f80;
    matrix[3][2] = 7u32;

    {
        u8 temporary[count];
        if (sizeof(temporary) != (uptr)count) return -3;
        temporary[4] = 2u8;
        sum += temporary[4];
    }

    u32 tail = 4u32;
    u32 copied = 0u32;
    restrict_copy(values, &copied);
    sum += add_eight(1u32, 2u32, 3u32, 4u32,
                     5u32, 6u32, 7u32, 8u32);
    return sum + (extended[4] == 3.5f80) + matrix[3][2] + tail + copied +
           goto_vla_cleanup(count);
}
