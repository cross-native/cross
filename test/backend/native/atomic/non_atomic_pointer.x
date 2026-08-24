global u32 cell;

global u32 non_atomic_pointer() {
    return $::atomic_load(&cell, $::memory::relaxed);
}
