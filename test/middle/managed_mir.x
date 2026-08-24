global i32 mir_choose(in bool choose, in i32 left, in i32 right) {
    return choose ? left + 1 : right + 2;
}

global i32 mir_if(in i32 value) {
    if (value > 10) {
        return value + 3;
    } else {
        return value - 4;
    }
}

global i32 mir_safe_div(in i32 divisor, in i32 value) {
    return divisor != 0 && value / divisor > 2;
}

global u64 mir_widen(in bool choose, in i32 narrow, in u64 wide) {
    return choose ? narrow : wide;
}

global i32 mir_small_rank(in u8 unsigned_value, in i8 signed_value) {
    return signed_value < unsigned_value;
}

global i32 mir_wide_rank(in u32 unsigned_value, in i64 signed_value) {
    return signed_value < unsigned_value;
}

global i32 mir_effect_loop(in i32 limit) {
    i32 total = 0;
    for (i32 index = 0; index < limit; ++index) {
        if (index == 2) {
            continue;
        }
        if (index == 6) {
            break;
        }
        total += mir_choose(index & 1, index, index);
    }
    return total;
}

global void mir_copyout(i32 value) {
    value += 1;
}

global i32 mir_copyout_call() {
    i32 value = 3;
    mir_copyout(value);
    return value;
}

global i32 managed_mir_entry() {
    // This test exercises runtime MIR call lowering, so make that stage
    // explicit even though every argument happens to be known here.
    return $::runtime(
        mir_choose(1, 10, 20) + mir_choose(0, 10, 20) +
        mir_if(12) + mir_if(5) +
        mir_safe_div(0, 9) + mir_safe_div(3, 9) +
        mir_small_rank(0, -1) + mir_wide_rank(0, -1) +
        mir_effect_loop(8)
    );
}
