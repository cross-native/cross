global u128 [[atomic]] cell;

global u128 unsupported_width() {
    return $::atomic_load(&cell, $::memory::seq_cst);
}
