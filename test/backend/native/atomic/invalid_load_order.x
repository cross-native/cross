global u32 [[atomic]] cell;

global u32 invalid_load_order() {
    return $::atomic_load(&cell, $::memory::release);
}
