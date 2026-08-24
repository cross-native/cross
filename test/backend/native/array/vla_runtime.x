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

global i32 vla_entry() {
    u32 count = 5u32;
    stack u32 values[count];
    f80 extended[count];
    u32 matrix[count][3];
    u32 sum = 0u32;

    for (u32 index = 0u32; index < count; index += 1u32) {
        values[index] = index + 1u32;
        sum += values[index];
    }
    sum += runtime_sum(values, count);
    extended[4] = 3.5f80;
    matrix[3][2] = 7u32;

    {
        u8 temporary[count];
        temporary[4] = 2u8;
        sum += temporary[4];
    }

    u32 tail = 4u32;
    sum += add_eight(1u32, 2u32, 3u32, 4u32,
                     5u32, 6u32, 7u32, 8u32);
    return sum + (extended[4] == 3.5f80) + matrix[3][2] + tail;
}
