global u32 [[atomic]] cell;

global bool invalid_failure_order() {
    u32 expected = 0u32;
    return $::atomic_compare_exchange(
        &cell, &expected, 1u32,
        $::memory::acquire, $::memory::seq_cst);
}
