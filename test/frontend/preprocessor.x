#define ANSWER 42
#define DOUBLE(value) ((value) + (value))
#define ADD3(first, ...) ((first) + $::macro::args)

#if $::has_instruction($::_movabs)
global i32 answer() {
    return ADD3(DOUBLE(ANSWER), 1 + 2);
}
#endif

#if $::has_builtin($::_ret) && $::has_attribute("naked")
global i32 raw_queries() {
    return 1;
}
#endif

global i32 patch_value_u64 = $::has_patch_value(u64);
global i32 patch_value_u128 = $::has_patch_value(u128);
global i32 patch_operand_movabs = $::has_patch_operand($::_movabs, 1, u64);
global i32 patch_operand_add = $::has_patch_operand($::_add, 1, u64);
global i32 patch_operand_signed = $::has_patch_operand($::_movabs, 1, i64);
global i32 patch_operand_float = $::has_patch_operand($::_movabs, 1, f64);
global i32 patch_operand_narrow = $::has_patch_operand($::_movabs, 1, u32);
global i32 operator_attribute = $::has_attribute("operator");
global i32 patch_repeatable = $::has_feature($::feature::repeatable_patch);
global i32 patch_concurrent = $::has_feature($::feature::concurrent_patch);
global i64 language_version = $::language::version;
global i32 automatic_evaluation =
    $::has_feature($::feature::automatic_evaluation);
global i32 atomic_feature = $::has_feature($::feature::atomics);
global i32 variadic_feature = $::has_feature($::feature::variadics);
global i32 target_avx = $::has_feature($::feature::avx);
global i32 target_avx2 = $::has_feature($::feature::avx2);
global i32 target_avx512f = $::has_feature($::feature::avx512f);
global i32 target_avx512bw = $::has_feature($::feature::avx512bw);
global i32 atomic_intrinsic =
    $::has_intrinsic($::atomic_compare_exchange);
global i32 atomic_order_builtin =
    $::has_builtin($::memory::seq_cst);
