global uptr shared_patch_sink;

global u64 managed_patch_conflict() {
    return $::patch(0x1122334455667788u64, shared_patch_sink);
}

[[naked]]
global void raw_patch_conflict(out u64 value "rax") {
    $::_movabs(value, $::patch(0x8877665544332211u64,
                               shared_patch_sink));
    $::_ret();
}
