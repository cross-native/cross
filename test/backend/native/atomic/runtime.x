global u32 [[atomic]] atomic_cell;
global f32 [[atomic]] atomic_float;

[[noinline]]
static u32 atomic_parameter(in volatile u32 [[atomic]] input) {
    volatile u32 [[atomic]] value = input;
    value += 2u32;
    return value;
}

[[noinline]]
static void atomic_inout(inout u32 [[atomic]] value) {
    ++value;
}

global i32 atomic_entry() {
    atomic_cell = 3u32;
    u32 implicit_value = atomic_cell;
    u32 loaded = $::atomic_load(&atomic_cell, $::memory::relaxed);
    $::atomic_store(&atomic_cell, 5u32, $::memory::release);
    u32 exchanged = $::atomic_exchange(
        &atomic_cell, 7u32, $::memory::acq_rel);
    u32 added = $::atomic_fetch_add(
        &atomic_cell, 2u32, $::memory::relaxed);
    u32 subtracted = $::atomic_fetch_sub(
        &atomic_cell, 3u32, $::memory::seq_cst);
    u32 anded = $::atomic_fetch_and(
        &atomic_cell, 3u32, $::memory::acquire);
    u32 xored = $::atomic_fetch_xor(
        &atomic_cell, 7u32, $::memory::release);
    u32 ored = $::atomic_fetch_or(
        &atomic_cell, 8u32, $::memory::acq_rel);

    u32 expected = 13u32;
    bool changed = $::atomic_compare_exchange(
        &atomic_cell, &expected, 21u32,
        $::memory::seq_cst, $::memory::acquire);
    u32 failed_expected = 20u32;
    bool unchanged = $::atomic_compare_exchange(
        &atomic_cell, &failed_expected, 30u32,
        $::memory::acquire, $::memory::relaxed);

    volatile u16 [[atomic]] local = 4u16;
    u16 local_value = local;

    $::atomic_store(&atomic_float, 1.5f32, $::memory::relaxed);
    f32 old_float = $::atomic_exchange(
        &atomic_float, 2.5f32, $::memory::seq_cst);
    f32 new_float = $::atomic_load(
        &atomic_float, $::memory::acquire);

    $::atomic_signal_fence($::memory::acq_rel);
    $::atomic_thread_fence($::memory::seq_cst);

    i32 result = (implicit_value == 3u32) + (loaded == 3u32) +
                 (exchanged == 5u32) + (added == 7u32) +
                 (subtracted == 9u32) + (anded == 6u32) +
                 (xored == 2u32) + (ored == 5u32) + changed +
                 (expected == 13u32) + (!unchanged) +
                 (failed_expected == 21u32) + (atomic_cell == 21u32) +
                 (local_value == 4u16) + (old_float == 1.5f32) +
                 (new_float == 2.5f32) + $::atomic_is_lock_free(i32) +
                 (!$::atomic_is_lock_free(i128));

    u32 compound_add = (atomic_cell += 3u32);
    u32 post = atomic_cell++;
    bool after_post = atomic_cell == 25u32;
    u32 pre = ++atomic_cell;
    u32 compound_multiply = (atomic_cell *= 2u32);
    u32 compound_divide = (atomic_cell /= 4u32);
    u32 compound_remainder = (atomic_cell %= 5u32);
    u32 compound_shift_left = (atomic_cell <<= 2u32);
    u32 compound_shift_right = (atomic_cell >>= 1u32);
    u32 [[atomic]] *pointer = &atomic_cell;
    u32 through_pointer = (*pointer += 2u32);
    u32 through_index = (pointer[0] ^= 1u32);
    u32 parameter_value = atomic_parameter(atomic_cell);
    bool input_unchanged = atomic_cell == 9u32;
    atomic_inout(atomic_cell);

    f32 compound_float = (atomic_float *= 2.0f32);
    f32 post_float = atomic_float--;

    return result + (compound_add == 24u32) + (post == 24u32) +
           after_post + (pre == 26u32) +
           (compound_multiply == 52u32) +
           (compound_divide == 13u32) +
           (compound_remainder == 3u32) +
           (compound_shift_left == 12u32) +
           (compound_shift_right == 6u32) +
           (through_pointer == 8u32) + (through_index == 9u32) +
           (parameter_value == 11u32) + input_unchanged +
           (atomic_cell == 10u32) + (compound_float == 5.0f32) +
           (post_float == 5.0f32) + (atomic_float == 4.0f32);
}
