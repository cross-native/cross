global uptr generic_patch_sink;

static u64 generic_patch_value<T>() {
    return $::patch(0x1020304050607080u64, generic_patch_sink);
}

global u64 instantiate_patch_twice() {
    return generic_patch_value::<u32>() +
           generic_patch_value::<u64>();
}
