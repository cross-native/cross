global u32 [[atomic]] cell;

global void invalid_store_order() {
    $::atomic_store(&cell, 1u32, $::memory::acquire);
}
