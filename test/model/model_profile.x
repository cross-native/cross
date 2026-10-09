$::static_assert($::has_abi("test_sysv"), "custom ABI model is visible");
$::static_assert($::has_mangling("test"), "custom mangling model is visible");
$::static_assert($::has_mangling("descriptor"), "descriptor mangler is visible");
$::static_assert($::has_profile("test-profile"), "custom profile is visible");
$::static_assert($::has_attribute("packed"), "normative attributes are queryable");
$::static_assert(!$::has_attribute("not_a_cross_attribute"), "unknown attributes are absent");

global i32 model_entry() {
    return 17;
}

global i32 model_data = 19;

[[runtime_only]]
global T model_identity<T>(in T value) {
    return value;
}

global i32 model_generic_entry() {
    return model_identity::<i32>(23);
}

[[runtime_only]]
global uptr model_count<uptr N>() {
    return N;
}

global uptr model_count_entry() {
    return model_count::<7uptr>();
}

[[abi("test_abi")]]
global i32 model_function_abi(in i32 value) {
    return value;
}

[[abi("odd_abi"), runtime_only, noinline]]
global i32 model_odd_echo(in i32 value) {
    return value;
}

global i32 model_odd_caller() {
    return model_odd_echo(41);
}
