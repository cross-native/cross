$::static_assert($::has_abi("ms_abi"), "x86-64 must expose ms_abi");
$::static_assert(1 + 2 * 3 == 7, "constant expression precedence");

global uptr align_i64() {
    $::static_assert(8 > 4, "block assertion");
    return $::alignof(i64);
}

