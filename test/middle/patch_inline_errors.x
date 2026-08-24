global uptr inline_patch_sink;

[[raw_inline]]
static u64 raw_inline_patch() {
    return $::patch(7u64, inline_patch_sink);
}

[[always_inline]]
static u64 always_inline_patch() {
    return $::patch(9u64, inline_patch_sink);
}
