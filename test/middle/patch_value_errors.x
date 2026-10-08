#if CASE == 0
global u64 patch_runtime_initial(in u64 value) {
    return $::patch(value);
}
#else
global u128 patch_unavailable_width() {
    return $::patch(1u128);
}
#endif
