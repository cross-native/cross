typedef u32 row3[3];

global u32 global_values[4];
global row3 global_rows[2];
global f80 global_extended[2];

static u32 sum_row(in u32 values[3]) {
    return values[0] + values[1] + values[2];
}

global i32 array_entry() {
    u32 local[4];
    row3 matrix[2];
    f80 extended[2];
    stack u8 scratch[32];
    stack volatile u32 observed;

    local[0] = 3u32;
    local[1] = 5u32;
    local[2] = 7u32;
    local[3] = 11u32;

    matrix[1][0] = 2u32;
    matrix[1][1] = 4u32;
    matrix[1][2] = 6u32;
    u32 *cell = &matrix[1][2];
    *cell += 1u32;

    global_values[2] = local[1] + local[3];
    global_rows[1][1] = matrix[1][2];

    extended[0] = 1.5f80;
    extended[1] = 2.5f80;
    global_extended[1] = extended[0] + extended[1];
    scratch[31] = 9u8;
    observed = 8u32;

    return local[0] + local[2] + global_values[2] +
           sum_row(matrix[1]) + (global_values[0] == 0u32) +
           (global_rows[1][1] == 7u32) +
           (global_extended[1] == 4.0f80) +
           (scratch[31] == 9u8) + (observed == 8u32);
}
