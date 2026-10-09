// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/diagnostic.hpp"
#include "model/model.hpp"
#include "target/abi_lowering.hpp"

#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

using namespace cross;

bool register_is(const ArgumentAssignment& value, std::size_t piece,
                 std::string_view name) {
    return piece < value.pieces.size() &&
           value.pieces[piece].location.kind ==
               LocationKind::Register &&
           value.pieces[piece].location.reg == name;
}

bool register_is(const ReturnAssignment& value, std::size_t piece,
                 std::string_view name) {
    return piece < value.pieces.size() &&
           value.pieces[piece].location.kind ==
               LocationKind::Register &&
           value.pieces[piece].location.reg == name;
}

bool shadow_register_is(const ArgumentAssignment& value, std::size_t piece,
                        std::string_view name) {
    return piece < value.shadows.size() &&
           value.shadows[piece].location.kind == LocationKind::Register &&
           value.shadows[piece].location.reg == name;
}

bool stack_is(const ArgumentAssignment& value, std::size_t piece,
              std::size_t offset) {
    return piece < value.pieces.size() &&
           value.pieces[piece].location.kind == LocationKind::Stack &&
           value.pieces[piece].location.stack_offset == offset;
}

bool extension_is(const ArgumentAssignment& value, std::size_t piece,
                  AbiExtensionKind extension) {
    return piece < value.pieces.size() &&
           value.pieces[piece].extension == extension;
}

bool extension_is(const ReturnAssignment& value, std::size_t piece,
                  AbiExtensionKind extension) {
    return piece < value.pieces.size() &&
           value.pieces[piece].extension == extension;
}

AbiValue scalar(ScalarMode mode) {
    AbiValue result;
    result.mode = mode;
    result.alignment_bits =
        mode.bits >= 64 ? 64 : mode.bits;
    return result;
}

AbiValue aggregate(std::uint16_t bits,
                   std::initializer_list<AbiValue> fields) {
    AbiValue result;
    result.mode = ScalarMode::aggregate(bits);
    result.alignment_bits = 64;
    result.elements = fields;
    return result;
}

AbiValue pair(std::initializer_list<AbiValue> fields) {
    AbiValue result;
    result.mode = ScalarMode::pair(128);
    result.alignment_bits = 64;
    result.elements = fields;
    return result;
}

int fail(std::string_view message) {
    std::cerr << "ABI rule test failed: " << message << '\n';
    return 1;
}

bool validate_shipped_abis() {
    const auto* cross_abi =
        model_registry().find_abi("x86-64", "cross", {});
    const auto* sysv =
        model_registry().find_abi("x86-64", "sysv_abi", {});
    const auto* microsoft =
        model_registry().find_abi("x86-64", "ms_abi", {});
    const auto* o32 =
        model_registry().find_abi("mips", "o32", {});
    const auto* eabi32 =
        model_registry().find_abi("mips", "eabi32", {});
    const auto* mips_cross32 =
        model_registry().find_abi("mips", "cross32", {});
    const auto* mips_cross64 =
        model_registry().find_abi("mips", "cross64", {});
    const auto* mips_cross_n64 =
        model_registry().find_abi("mips", "cross-n64", {});
    // Both MIPS address models name their Cross ABI `cross`; the triple's
    // default ABI selects the address model, and no triple is ambiguous.
    const auto* mips_cross_alias =
        model_registry().find_abi("mips", "cross", "mips-unknown-elf");
    const auto* mips64_cross_alias =
        model_registry().find_abi("mips", "cross", "mips64el-unknown-elf");
    if (!cross_abi || !sysv || !microsoft || !o32 || !eabi32 ||
        !mips_cross32 || mips_cross_alias != mips_cross32 ||
        !mips_cross64 || !mips_cross_n64 ||
        mips64_cross_alias != mips_cross_n64 ||
        model_registry().find_abi("mips", "cross", {})) {
        std::cerr << "shipped ABI name resolution mismatch\n";
        return false;
    }
    if (!cross_abi->gcc_calling_attribute.empty() ||
        sysv->gcc_calling_attribute != "sysv_abi" ||
        microsoft->gcc_calling_attribute != "ms_abi") {
        return false;
    }
    if (!cross_abi->id.valid() || !sysv->id.valid() ||
        !microsoft->id.valid() ||
        model_registry().find_abi(cross_abi->id) != cross_abi ||
        model_registry().find_abi(sysv->id) != sysv ||
        model_registry().find_abi(microsoft->id) != microsoft) {
        return false;
    }
    for (std::size_t index = 0; index < sysv->variadic_states.size();
         ++index) {
        if (!sysv->variadic_states[index].id.valid() ||
            sysv->variadic_states[index].id.value != index) {
            return false;
        }
    }

    const auto i64 = scalar(ScalarMode::integer(64));
    const auto f64 = scalar(ScalarMode::floating(64));
    const auto f80 = scalar(ScalarMode::floating(80));
    const auto i128 = AbiValue{ScalarMode::integer(128)};
    const auto ref_i128 =
        AbiValue{ScalarMode::integer(128),
                 ValueTransport::ByReference};

    const auto o32_i32 = scalar(ScalarMode::integer(32));
    const auto o32_pointer = scalar(ScalarMode::pointer(32));
    const auto o32_f32 = scalar(ScalarMode::floating(32));
    const std::array<std::string, 4> o32_hard{
        "mips3", "hard-float", "fp32", "odd-spreg"};
    const std::array<std::string, 2> o32_soft{"mips3", "soft-float"};

    const auto o32_integer_pair = classify_call_arguments(
        *o32, std::array{o32_i32, i64, o32_i32}, o32_hard);
    const auto o32_pointer_argument = classify_call_arguments(
        *o32, std::array{o32_pointer}, o32_hard);
    if (!o32_integer_pair ||
        !register_is(o32_integer_pair.layout.arguments[0], 0, "a0") ||
        !extension_is(o32_integer_pair.layout.arguments[0], 0,
                      AbiExtensionKind::Sign) ||
        !register_is(o32_integer_pair.layout.arguments[1], 0, "a2") ||
        !extension_is(o32_integer_pair.layout.arguments[1], 0,
                      AbiExtensionKind::Sign) ||
        !register_is(o32_integer_pair.layout.arguments[1], 1, "a3") ||
        !extension_is(o32_integer_pair.layout.arguments[1], 1,
                      AbiExtensionKind::Sign) ||
        !stack_is(o32_integer_pair.layout.arguments[2], 0, 16) ||
        !extension_is(o32_integer_pair.layout.arguments[2], 0,
                      AbiExtensionKind::None) ||
        !o32_pointer_argument ||
        !extension_is(o32_pointer_argument.layout.arguments[0], 0,
                      AbiExtensionKind::Sign)) {
        return false;
    }

    const auto o32_leading_float = classify_call_arguments(
        *o32, std::array{o32_f32, o32_f32, o32_i32}, o32_hard);
    const auto o32_after_integer = classify_call_arguments(
        *o32, std::array{o32_i32, o32_f32}, o32_hard);
    const auto o32_aligned_float = classify_call_arguments(
        *o32, std::array{o32_f32, f64, o32_i32}, o32_hard);
    if (!o32_leading_float ||
        !register_is(o32_leading_float.layout.arguments[0], 0, "f12") ||
        !register_is(o32_leading_float.layout.arguments[1], 0, "f14") ||
        !register_is(o32_leading_float.layout.arguments[2], 0, "a2") ||
        !o32_after_integer ||
        !register_is(o32_after_integer.layout.arguments[0], 0, "a0") ||
        !register_is(o32_after_integer.layout.arguments[1], 0, "a1") ||
        !o32_aligned_float ||
        !register_is(o32_aligned_float.layout.arguments[0], 0, "f12") ||
        !register_is(o32_aligned_float.layout.arguments[1], 0, "f14") ||
        !stack_is(o32_aligned_float.layout.arguments[2], 0, 16)) {
        return false;
    }

    const auto o32_i64_result = classify_return(*o32, i64, o32_hard);
    const auto o32_f64_result = classify_return(*o32, f64, o32_hard);
    const auto o32_soft_f64_result = classify_return(*o32, f64, o32_soft);
    if (!o32_i64_result ||
        !register_is(o32_i64_result, 0, "v0") ||
        !extension_is(o32_i64_result, 0, AbiExtensionKind::Sign) ||
        !register_is(o32_i64_result, 1, "v1") ||
        !extension_is(o32_i64_result, 1, AbiExtensionKind::Sign) ||
        !o32_f64_result || !register_is(o32_f64_result, 0, "f0") ||
        !o32_soft_f64_result ||
        !register_is(o32_soft_f64_result, 0, "v0") ||
        !extension_is(o32_soft_f64_result, 0,
                      AbiExtensionKind::None) ||
        !register_is(o32_soft_f64_result, 1, "v1") ||
        !extension_is(o32_soft_f64_result, 1,
                      AbiExtensionKind::None)) {
        return false;
    }

    const std::array<std::string, 5> eabi_single{
        "mips2", "hard-float", "fp32", "single-float", "odd-spreg"};
    const std::array<std::string, 4> eabi_double{
        "mips2", "hard-float", "fp32", "odd-spreg"};
    const auto eabi_independent = classify_call_arguments(
        *eabi32,
        std::array{o32_f32, o32_i32, o32_f32, o32_i32},
        eabi_single);
    if (!eabi_independent ||
        !register_is(eabi_independent.layout.arguments[0], 0, "f12") ||
        !register_is(eabi_independent.layout.arguments[1], 0, "a0") ||
        !register_is(eabi_independent.layout.arguments[2], 0, "f13") ||
        !register_is(eabi_independent.layout.arguments[3], 0, "a1")) {
        return false;
    }
    const auto eabi_integer_pair = classify_call_arguments(
        *eabi32, std::array{o32_i32, i64, o32_i32}, eabi_single);
    if (!eabi_integer_pair ||
        !register_is(eabi_integer_pair.layout.arguments[0], 0, "a0") ||
        !register_is(eabi_integer_pair.layout.arguments[1], 0, "a2") ||
        !register_is(eabi_integer_pair.layout.arguments[1], 1, "a3") ||
        !register_is(eabi_integer_pair.layout.arguments[2], 0, "t0")) {
        return false;
    }
    const std::array nine_i32{o32_i32, o32_i32, o32_i32, o32_i32,
                              o32_i32, o32_i32, o32_i32, o32_i32,
                              o32_i32};
    const auto eabi_stack =
        classify_call_arguments(*eabi32, nine_i32, eabi_single);
    if (!eabi_stack ||
        !register_is(eabi_stack.layout.arguments[7], 0, "t3") ||
        !stack_is(eabi_stack.layout.arguments[8], 0, 0)) {
        return false;
    }
    const auto eabi_single_f64_argument =
        classify_call_arguments(*eabi32, std::array{f64}, eabi_single);
    const auto eabi_single_f64_result =
        classify_return(*eabi32, f64, eabi_single);
    const auto eabi_double_f64_argument =
        classify_call_arguments(*eabi32, std::array{f64}, eabi_double);
    const auto eabi_double_f64_result =
        classify_return(*eabi32, f64, eabi_double);
    const auto eabi_small_record_result =
        classify_return(*eabi32, aggregate(64, {o32_i32, o32_i32}),
                        eabi_single);
    if (!eabi_single_f64_argument ||
        !register_is(eabi_single_f64_argument.layout.arguments[0], 0, "a0") ||
        !register_is(eabi_single_f64_argument.layout.arguments[0], 1, "a1") ||
        !eabi_single_f64_result ||
        !register_is(eabi_single_f64_result, 0, "v0") ||
        !register_is(eabi_single_f64_result, 1, "v1") ||
        !eabi_double_f64_argument ||
        !register_is(eabi_double_f64_argument.layout.arguments[0], 0, "f12") ||
        !eabi_double_f64_result ||
        !register_is(eabi_double_f64_result, 0, "f0") ||
        !eabi_small_record_result ||
        !register_is(eabi_small_record_result, 0, "v0") ||
        !register_is(eabi_small_record_result, 1, "v1")) {
        return false;
    }

    // The 32-bit MIPS Cross ABI is stable across ISA selection.  MIPS III
    // may select native 64-bit operations internally, but public i64 values
    // retain the same word-pair interface as MIPS I and MIPS32 objects.
    const std::array<std::string, 4> mips1_cross_features{
        "mips1", "hard-float", "fp32", "odd-spreg"};
    const std::array<std::string, 5> mips3_cross_features{
        "mips1", "mips2", "mips3", "hard-float", "fp32"};
    const auto mips_cross_mips1 = classify_call_arguments(
        *mips_cross32, std::array{i64, i64, o32_i32},
        mips1_cross_features);
    const auto mips_cross_mips3 = classify_call_arguments(
        *mips_cross32, std::array{i64, i64, o32_i32},
        mips3_cross_features);
    const auto mips_cross_result =
        classify_return(*mips_cross32, i64, mips1_cross_features);
    if (!mips_cross_mips1 || !mips_cross_mips3 || !mips_cross_result) {
        return false;
    }
    for (const auto* layout : {&mips_cross_mips1.layout,
                               &mips_cross_mips3.layout}) {
        if (!register_is(layout->arguments[0], 0, "a0") ||
            !register_is(layout->arguments[0], 1, "a1") ||
            !register_is(layout->arguments[1], 0, "a2") ||
            !register_is(layout->arguments[1], 1, "a3") ||
            !register_is(layout->arguments[2], 0, "t0")) {
            return false;
        }
    }
    if (!register_is(mips_cross_result, 0, "v0") ||
        !register_is(mips_cross_result, 1, "v1")) {
        return false;
    }
    // The preserved s0-s7 follow every volatile argument register.
    std::array<AbiValue, 15> mips_cross_words;
    mips_cross_words.fill(o32_i32);
    const auto mips_cross_spread = classify_call_arguments(
        *mips_cross32, mips_cross_words, mips1_cross_features);
    if (!mips_cross_spread ||
        !register_is(mips_cross_spread.layout.arguments[13], 0, "t9") ||
        !register_is(mips_cross_spread.layout.arguments[14], 0, "s0")) {
        return false;
    }

    // Cross64 is deliberately a distinct contract: it is unavailable before
    // MIPS III, carries an i64 in one 64-bit GPR, and makes the platform
    // callee-saved bank available as private argument channels.
    const std::array five_i64{i64, i64, i64, i64, i64};
    const auto mips_cross64_unavailable = classify_call_arguments(
        *mips_cross64, std::array{i64}, mips1_cross_features);
    const auto mips_cross64_arguments = classify_call_arguments(
        *mips_cross64, five_i64, mips3_cross_features);
    const auto mips_cross64_result =
        classify_return(*mips_cross64, i64, mips3_cross_features);
    if (mips_cross64_unavailable || !mips_cross64_arguments ||
        !mips_cross64_result ||
        mips_cross64_arguments.layout.arguments[0].pieces.size() != 1 ||
        !register_is(mips_cross64_arguments.layout.arguments[0], 0, "a0") ||
        !register_is(mips_cross64_arguments.layout.arguments[3], 0, "a3") ||
        !register_is(mips_cross64_arguments.layout.arguments[4], 0, "t0") ||
        mips_cross64_result.pieces.size() != 1 ||
        !register_is(mips_cross64_result, 0, "v0")) {
        return false;
    }

    // cross-n64 keeps the cross64 channels with 64-bit pointers: integer and
    // floating cursors are independent, and a record result wider than the
    // four result registers returns through a hidden pointer.
    const auto n64_pointer = scalar(ScalarMode::pointer(64));
    const auto mips_cross_n64_arguments = classify_call_arguments(
        *mips_cross_n64, std::array{n64_pointer, f64, i64},
        mips3_cross_features);
    const auto mips_cross_n64_quad = classify_return(
        *mips_cross_n64, aggregate(256, {i64, i64, i64, i64}),
        mips3_cross_features);
    const auto mips_cross_n64_large = classify_return(
        *mips_cross_n64, aggregate(320, {i64, i64, i64, i64, i64}),
        mips3_cross_features);
    if (!mips_cross_n64_arguments ||
        mips_cross_n64_arguments.layout.arguments[0].pieces.size() != 1 ||
        !register_is(mips_cross_n64_arguments.layout.arguments[0], 0, "a0") ||
        !register_is(mips_cross_n64_arguments.layout.arguments[1], 0, "f12") ||
        !register_is(mips_cross_n64_arguments.layout.arguments[2], 0, "a1") ||
        !mips_cross_n64_quad || mips_cross_n64_quad.indirect ||
        !register_is(mips_cross_n64_quad, 0, "v0") ||
        !register_is(mips_cross_n64_quad, 3, "a1") ||
        !mips_cross_n64_large || !mips_cross_n64_large.indirect ||
        !register_is(mips_cross_n64_large, 0, "a0")) {
        std::cerr << "cross-n64 classification mismatch\n";
        return false;
    }

    const std::array seven_i64{i64, i64, i64, i64, i64, i64, i64};
    const auto cross_registers =
        classify_call_arguments(*cross_abi, seven_i64);
    if (!cross_registers ||
        cross_registers.layout.outgoing_area_size != 0 ||
        !register_is(cross_registers.layout.arguments[0], 0, "r10") ||
        !register_is(cross_registers.layout.arguments[1], 0, "r9") ||
        !register_is(cross_registers.layout.arguments[2], 0, "r8") ||
        !register_is(cross_registers.layout.arguments[3], 0, "rcx") ||
        !register_is(cross_registers.layout.arguments[4], 0, "rdx") ||
        !register_is(cross_registers.layout.arguments[5], 0, "r11") ||
        !register_is(cross_registers.layout.arguments[6], 0, "rax")) {
        return false;
    }

    const std::array cross_mixed{f64, i64, f64};
    const auto cross_independent =
        classify_call_arguments(*cross_abi, cross_mixed);
    if (!cross_independent ||
        !register_is(cross_independent.layout.arguments[0], 0, "xmm5") ||
        !register_is(cross_independent.layout.arguments[1], 0, "r10") ||
        !register_is(cross_independent.layout.arguments[2], 0, "xmm4")) {
        return false;
    }

    const auto cross_four_words =
        aggregate(256, {i64, i64, i64, i64});
    const auto cross_aggregate_argument = classify_call_arguments(
        *cross_abi, std::array{cross_four_words});
    const auto cross_aggregate_result =
        classify_return(*cross_abi, cross_four_words);
    if (!cross_aggregate_argument ||
        !register_is(cross_aggregate_argument.layout.arguments[0], 0, "r10") ||
        !register_is(cross_aggregate_argument.layout.arguments[0], 3, "rcx") ||
        !cross_aggregate_result ||
        !register_is(cross_aggregate_result, 0, "rax") ||
        !register_is(cross_aggregate_result, 3, "r8")) {
        return false;
    }

    const std::array six_i64{i64, i64, i64, i64, i64, i64};
    const auto sysv_registers =
        classify_call_arguments(*sysv, six_i64);
    if (!sysv_registers ||
        sysv_registers.layout.outgoing_area_size != 0 ||
        !register_is(sysv_registers.layout.arguments[0], 0, "rdi") ||
        !register_is(sysv_registers.layout.arguments[5], 0, "r9")) {
        return false;
    }

    const std::array sysv_overflow{
        i64, i64, i64, i64, i64, i64, i64};
    const auto sysv_stack =
        classify_call_arguments(*sysv, sysv_overflow);
    if (!sysv_stack ||
        !stack_is(sysv_stack.layout.arguments[6], 0, 0) ||
        sysv_stack.layout.outgoing_area_size != 16 ||
        callee_stack_offset(
            sysv_stack.layout.arguments[6].pieces[0], *sysv) != 8) {
        return false;
    }

    const std::array sysv_i128_rollback{
        i64, i64, i64, i64, i64, i128, i64};
    const auto sysv_wide =
        classify_call_arguments(*sysv, sysv_i128_rollback);
    if (!sysv_wide ||
        !stack_is(sysv_wide.layout.arguments[5], 0, 0) ||
        !stack_is(sysv_wide.layout.arguments[5], 1, 8) ||
        !register_is(sysv_wide.layout.arguments[6], 0, "r9") ||
        sysv_wide.layout.arguments[5].stack_alignment != 16) {
        return false;
    }

    const std::array microsoft_split{i64, i64, i64, i128};
    const auto microsoft_wide =
        classify_call_arguments(*microsoft, microsoft_split);
    if (!microsoft_wide ||
        !register_is(microsoft_wide.layout.arguments[3], 0, "r9") ||
        !stack_is(microsoft_wide.layout.arguments[3], 1, 32) ||
        microsoft_wide.layout.argument_stack_base_size != 32 ||
        microsoft_wide.layout.used_stack_size != 40 ||
        microsoft_wide.layout.outgoing_area_size != 48) {
        return false;
    }

    const std::array microsoft_reference{ref_i128};
    const auto microsoft_ref =
        classify_parameters(*microsoft, microsoft_reference);
    if (!microsoft_ref ||
        !register_is(microsoft_ref.layout.arguments[0], 0, "rcx") ||
        microsoft_ref.layout.arguments[0].pieces.size() != 1 ||
        microsoft_ref.layout.arguments[0].pieces[0].value_bits != 64 ||
        microsoft_ref.layout.outgoing_area_size != 32) {
        return false;
    }

    const std::array sysv_variadic_values{i64, f64, i64};
    const auto sysv_variadic = classify_variadic_call_arguments(
        *sysv, sysv_variadic_values, 1);
    if (!sysv_variadic ||
        !register_is(sysv_variadic.layout.arguments[0], 0, "rdi") ||
        !register_is(sysv_variadic.layout.arguments[1], 0, "xmm0") ||
        !register_is(sysv_variadic.layout.arguments[2], 0, "rsi") ||
        abi_cursor_count(sysv_variadic.layout.named_cursors, "integer") != 1 ||
        sysv_variadic.layout.implicit_register_values.size() != 1 ||
        sysv_variadic.layout.implicit_register_values[0].reg != "al" ||
        sysv_variadic.layout.implicit_register_values[0].value != 1) {
        return false;
    }

    const std::array sysv_variadic_extended{
        i64, i64, i64, i64, i64, i64, i64, f80};
    const auto sysv_extended = classify_variadic_call_arguments(
        *sysv, sysv_variadic_extended, 7);
    if (!sysv_extended ||
        !stack_is(sysv_extended.layout.arguments[6], 0, 0) ||
        !stack_is(sysv_extended.layout.arguments[7], 0, 16) ||
        sysv_extended.layout.arguments[7].stack_alignment != 16 ||
        sysv_extended.layout.arguments[7].stack_size != 16 ||
        sysv_extended.layout.variadic_stack_offset != 8 ||
        sysv_extended.layout.implicit_register_values.size() != 1 ||
        sysv_extended.layout.implicit_register_values[0].value != 0) {
        return false;
    }

    const auto extended_return = classify_return(*sysv, f80);
    if (!extended_return ||
        !register_is(extended_return, 0, "st0")) {
        return false;
    }

    const std::array microsoft_variadic_values{i64, f64};
    const auto microsoft_variadic = classify_variadic_call_arguments(
        *microsoft, microsoft_variadic_values, 1);
    if (!microsoft_variadic ||
        !register_is(microsoft_variadic.layout.arguments[0], 0, "rcx") ||
        !register_is(microsoft_variadic.layout.arguments[1], 0, "xmm1") ||
        !shadow_register_is(
            microsoft_variadic.layout.arguments[1], 0, "rdx") ||
        abi_cursor_count(
            microsoft_variadic.layout.named_cursors,
            "argument-slot") != 1) {
        return false;
    }

    const auto v256 = scalar(ScalarMode::vector(256));
    const auto v512 = scalar(ScalarMode::vector(512));
    const std::array<std::string, 1> avx{"avx"};
    const std::array<std::string, 2> avx512{"avx", "avx512f"};
    const std::array v256_argument{v256};
    const std::array v512_argument{v512};

    const auto cross_v256_base =
        classify_call_arguments(*cross_abi, v256_argument);
    const auto cross_v256_base_result =
        classify_return(*cross_abi, v256);
    const auto cross_v512_avx_result =
        classify_return(*cross_abi, v512, avx);
    const auto cross_v512_avx512 =
        classify_call_arguments(*cross_abi, v512_argument, avx512);
    if (!cross_v256_base ||
        !stack_is(cross_v256_base.layout.arguments[0], 0, 0) ||
        cross_v256_base.layout.arguments[0].stack_alignment != 32 ||
        !cross_v256_base_result ||
        !register_is(cross_v256_base_result, 0, "xmm0") ||
        !register_is(cross_v256_base_result, 1, "xmm1") ||
        !cross_v512_avx_result ||
        !register_is(cross_v512_avx_result, 0, "ymm0") ||
        !register_is(cross_v512_avx_result, 1, "ymm1") ||
        !cross_v512_avx512 ||
        !register_is(cross_v512_avx512.layout.arguments[0], 0, "zmm5")) {
        std::cerr << "Cross wide-vector classification mismatch\n";
        return false;
    }

    const auto sysv_v256_base =
        classify_call_arguments(*sysv, v256_argument);
    const auto sysv_v256_base_result = classify_return(*sysv, v256);
    if (!sysv_v256_base ||
        !stack_is(sysv_v256_base.layout.arguments[0], 0, 0) ||
        sysv_v256_base.layout.arguments[0].stack_alignment != 32 ||
        sysv_v256_base.layout.outgoing_area_alignment != 32 ||
        !sysv_v256_base_result ||
        !register_is(sysv_v256_base_result, 0, "xmm0") ||
        !register_is(sysv_v256_base_result, 1, "xmm1")) {
        std::cerr << "SysV baseline v256 classification mismatch\n";
        return false;
    }

    const auto sysv_v256_avx =
        classify_call_arguments(*sysv, v256_argument, avx);
    const auto sysv_v256_avx_result = classify_return(*sysv, v256, avx);
    if (!sysv_v256_avx ||
        !register_is(sysv_v256_avx.layout.arguments[0], 0, "ymm0") ||
        !sysv_v256_avx_result ||
        !register_is(sysv_v256_avx_result, 0, "ymm0")) {
        std::cerr << "SysV AVX v256 classification mismatch\n";
        return false;
    }

    const std::array sysv_variadic_v256{i64, v256};
    const auto sysv_v256_unnamed = classify_variadic_call_arguments(
        *sysv, sysv_variadic_v256, 1, avx);
    if (!sysv_v256_unnamed ||
        !stack_is(sysv_v256_unnamed.layout.arguments[1], 0, 0) ||
        sysv_v256_unnamed.layout.arguments[1].stack_alignment != 32 ||
        sysv_v256_unnamed.layout.outgoing_area_alignment != 32 ||
        sysv_v256_unnamed.layout.implicit_register_values.size() != 1 ||
        sysv_v256_unnamed.layout.implicit_register_values[0].value != 0) {
        std::cerr << "SysV variadic v256 classification mismatch\n";
        return false;
    }

    const auto sysv_v512_avx =
        classify_call_arguments(*sysv, v512_argument, avx);
    const auto sysv_v512_avx_result = classify_return(*sysv, v512, avx);
    if (!sysv_v512_avx ||
        !stack_is(sysv_v512_avx.layout.arguments[0], 0, 0) ||
        sysv_v512_avx.layout.arguments[0].stack_alignment != 64 ||
        sysv_v512_avx.layout.outgoing_area_alignment != 64 ||
        !sysv_v512_avx_result ||
        !register_is(sysv_v512_avx_result, 0, "ymm0") ||
        !register_is(sysv_v512_avx_result, 1, "ymm1")) {
        std::cerr << "SysV AVX v512 classification mismatch\n";
        return false;
    }

    const auto sysv_v512_avx512 =
        classify_call_arguments(*sysv, v512_argument, avx512);
    const auto sysv_v512_avx512_result =
        classify_return(*sysv, v512, avx512);
    if (!sysv_v512_avx512 ||
        !register_is(sysv_v512_avx512.layout.arguments[0], 0, "zmm0") ||
        !sysv_v512_avx512_result ||
        !register_is(sysv_v512_avx512_result, 0, "zmm0")) {
        std::cerr << "SysV AVX-512 v512 classification mismatch\n";
        return false;
    }

    const auto microsoft_v256 =
        classify_call_arguments(*microsoft, v256_argument, avx);
    const auto microsoft_v256_base =
        classify_call_arguments(*microsoft, v256_argument);
    const auto microsoft_v256_result =
        classify_return(*microsoft, v256, avx);
    const auto microsoft_v256_base_result =
        classify_return(*microsoft, v256);
    if (!microsoft_v256 || !microsoft_v256.layout.arguments[0].indirect ||
        !register_is(microsoft_v256.layout.arguments[0], 0, "rcx") ||
        !microsoft_v256_base ||
        !microsoft_v256_base.layout.arguments[0].indirect ||
        !register_is(microsoft_v256_base.layout.arguments[0], 0, "rcx") ||
        !register_is(microsoft_v256_base.layout.arguments[0], 1, "rdx") ||
        microsoft_v256_base.layout.arguments[0].pieces[0]
                .indirect_value_bits != 128 ||
        microsoft_v256_base.layout.arguments[0].pieces[1]
                .value_bit_offset != 128 ||
        !microsoft_v256_result ||
        !register_is(microsoft_v256_result, 0, "ymm0") ||
        !microsoft_v256_base_result ||
        !register_is(microsoft_v256_base_result, 0, "xmm0") ||
        !register_is(microsoft_v256_base_result, 1, "xmm1")) {
        std::cerr << "Win64 v256 classification mismatch\n";
        return false;
    }

    const auto i32 = scalar(ScalarMode::integer(32));
    const auto f32 = scalar(ScalarMode::floating(32));
    const auto u8 = scalar(ScalarMode::integer(8));
    const auto two_i32 = aggregate(64, {i32, i32});
    const auto two_f32 = aggregate(64, {f32, f32});
    const auto mixed32 = aggregate(64, {f32, i32});
    const auto two_f64 = aggregate(128, {f64, f64});
    const std::array sysv_integer_aggregate{two_i32};
    const std::array sysv_sse_aggregate{two_f32};
    const std::array sysv_mixed_aggregate{mixed32};
    const std::array sysv_two_sse_eightbytes{two_f64};
    const auto integer_aggregate =
        classify_call_arguments(*sysv, sysv_integer_aggregate);
    const auto sse_aggregate =
        classify_call_arguments(*sysv, sysv_sse_aggregate);
    const auto mixed_aggregate =
        classify_call_arguments(*sysv, sysv_mixed_aggregate);
    const auto two_sse_eightbytes =
        classify_call_arguments(*sysv, sysv_two_sse_eightbytes);
    if (!integer_aggregate ||
        integer_aggregate.layout.arguments[0].pieces.size() != 1 ||
        !register_is(integer_aggregate.layout.arguments[0], 0, "rdi") ||
        integer_aggregate.layout.arguments[0].pieces[0].value_bits != 64 ||
        !sse_aggregate ||
        sse_aggregate.layout.arguments[0].pieces.size() != 1 ||
        !register_is(sse_aggregate.layout.arguments[0], 0, "xmm0") ||
        !mixed_aggregate ||
        mixed_aggregate.layout.arguments[0].pieces.size() != 1 ||
        !register_is(mixed_aggregate.layout.arguments[0], 0, "rdi") ||
        !two_sse_eightbytes ||
        two_sse_eightbytes.layout.arguments[0].pieces.size() != 2 ||
        !register_is(two_sse_eightbytes.layout.arguments[0], 0, "xmm0") ||
        !register_is(two_sse_eightbytes.layout.arguments[0], 1, "xmm1")) {
        std::cerr << "SysV aggregate eightbyte merge mismatch\n";
        return false;
    }

    auto unaligned = aggregate(72, {u8, i64});
    unaligned.alignment_bits = 8;
    unaligned.element_offsets_bits = {0, 8};
    const std::array unaligned_argument{unaligned};
    const auto sysv_unaligned =
        classify_call_arguments(*sysv, unaligned_argument);
    const auto sysv_unaligned_result = classify_return(*sysv, unaligned);
    if (!sysv_unaligned ||
        !stack_is(sysv_unaligned.layout.arguments[0], 0, 0) ||
        !sysv_unaligned_result || !sysv_unaligned_result.indirect ||
        !register_is(sysv_unaligned_result, 0, "rdi")) {
        std::cerr << "SysV unaligned aggregate memory classification mismatch\n";
        return false;
    }

    const auto three_i64 = aggregate(192, {i64, i64, i64});
    const std::array large_aggregate_argument{three_i64};
    const auto sysv_large =
        classify_call_arguments(*sysv, large_aggregate_argument);
    const auto sysv_large_result = classify_return(*sysv, three_i64);
    if (!sysv_large ||
        !stack_is(sysv_large.layout.arguments[0], 0, 0) ||
        !sysv_large_result || !sysv_large_result.indirect ||
        !register_is(sysv_large_result, 0, "rdi")) {
        std::cerr << "SysV large aggregate memory classification mismatch\n";
        return false;
    }

    auto aligned_channel = three_i64;
    aligned_channel.transport = ValueTransport::ByReference;
    aligned_channel.alignment_bits = 128;
    const std::array<AbiValue, 7> pointer_channels{
        aligned_channel, aligned_channel, aligned_channel, aligned_channel,
        aligned_channel, aligned_channel, aligned_channel};
    const auto spilled_channels =
        classify_call_arguments(*sysv, pointer_channels);
    if (!spilled_channels ||
        spilled_channels.layout.arguments[6].pieces.size() != 1 ||
        spilled_channels.layout.arguments[6].pieces[0].location.kind !=
            LocationKind::Stack ||
        spilled_channels.layout.arguments[6].stack_alignment != 8 ||
        spilled_channels.layout.arguments[6].stack_size != 8) {
        std::cerr << "by-reference channel inherited referent alignment\n";
        return false;
    }

    auto aligned_f80 = f80;
    aligned_f80.alignment_bits = 128;
    auto f80_record = aggregate(128, {aligned_f80});
    f80_record.alignment_bits = 128;
    const std::array f80_record_argument{f80_record};
    const auto sysv_f80_record =
        classify_call_arguments(*sysv, f80_record_argument);
    const auto sysv_f80_record_result =
        classify_return(*sysv, f80_record);
    if (!sysv_f80_record ||
        !stack_is(sysv_f80_record.layout.arguments[0], 0, 0) ||
        sysv_f80_record.layout.arguments[0].stack_size != 16 ||
        sysv_f80_record.layout.arguments[0].stack_alignment != 16 ||
        !sysv_f80_record_result || sysv_f80_record_result.indirect ||
        sysv_f80_record_result.pieces.size() != 1 ||
        !register_is(sysv_f80_record_result, 0, "st0")) {
        std::cerr << "SysV x87 aggregate transport mismatch\n";
        return false;
    }

    const auto three_bytes = aggregate(24, {u8, u8, u8});
    const std::array microsoft_small_aggregate{two_i32};
    const std::array microsoft_odd_aggregate{three_bytes};
    const auto microsoft_small =
        classify_call_arguments(*microsoft, microsoft_small_aggregate);
    const auto microsoft_odd =
        classify_call_arguments(*microsoft, microsoft_odd_aggregate);
    if (!microsoft_small || microsoft_small.layout.arguments[0].indirect ||
        !register_is(microsoft_small.layout.arguments[0], 0, "rcx") ||
        !microsoft_odd || !microsoft_odd.layout.arguments[0].indirect ||
        !register_is(microsoft_odd.layout.arguments[0], 0, "rcx")) {
        std::cerr << "Win64 aggregate size classification mismatch\n";
        return false;
    }

    const auto wide_return = classify_return(*sysv, i128);
    return wide_return && wide_return.pieces.size() == 2 &&
           register_is(wide_return, 0, "rax") &&
           register_is(wide_return, 1, "rdx");
}

} // namespace

int main(int argc, char** argv) {
    if (!validate_shipped_abis()) {
        return fail("shipped ABI programs changed behavior");
    }
    if (argc != 2) return fail("expected one ABI model path");

    Diagnostics diagnostics(std::cerr);
    ModelRegistry registry;
    if (!registry.load_file(std::filesystem::path(argv[1]),
                            diagnostics)) {
        return fail("could not load ABI stress models");
    }
    const auto* go =
        registry.find_abi("x86-64", "go_internal", {});
    const auto* rust =
        registry.find_abi("x86-64", "rust_native", {});
    if (!go || !rust) return fail("stress ABI model is absent");

    const auto i64 = scalar(ScalarMode::integer(64));
    const auto f64 = scalar(ScalarMode::floating(64));
    const auto go_struct = aggregate(192, {i64, f64, i64});
    const std::array go_arguments{go_struct};
    const auto go_layout =
        classify_call_arguments(*go, go_arguments);
    if (!go_layout ||
        go_layout.layout.arguments.size() != 1 ||
        !register_is(go_layout.layout.arguments[0], 0, "rax") ||
        !register_is(go_layout.layout.arguments[0], 1, "xmm0") ||
        !register_is(go_layout.layout.arguments[0], 2, "rbx") ||
        go_layout.layout.register_spill_size != 24) {
        return fail("Go recursive register assignment or spill area");
    }

    AbiValue two_element_array;
    two_element_array.mode = ScalarMode::array(128);
    two_element_array.alignment_bits = 64;
    two_element_array.element_count = 2;
    two_element_array.elements = {i64};
    const std::array go_nontrivial_array{two_element_array, i64};
    const auto go_nontrivial_layout =
        classify_call_arguments(*go, go_nontrivial_array);
    if (!go_nontrivial_layout ||
        go_nontrivial_layout.layout.arguments[0].pieces.empty() ||
        go_nontrivial_layout.layout.arguments[0]
                .pieces[0].location.kind != LocationKind::Stack ||
        !register_is(go_nontrivial_layout.layout.arguments[1], 0,
                     "rax")) {
        return fail(
            "Go non-trivial arrays must use memory with free registers");
    }

    AbiValue nontrivial_array;
    nontrivial_array.mode = ScalarMode::array(640);
    nontrivial_array.alignment_bits = 64;
    nontrivial_array.element_count = 10;
    nontrivial_array.elements = {i64};
    const std::array go_rollback{nontrivial_array, i64};
    const auto go_stack =
        classify_call_arguments(*go, go_rollback);
    if (!go_stack ||
        go_stack.layout.arguments[0].pieces.empty() ||
        go_stack.layout.arguments[0].pieces[0].location.kind !=
            LocationKind::Stack ||
        !register_is(go_stack.layout.arguments[1], 0, "rax")) {
        return fail("Go whole-value stack fallback did not roll back");
    }

    const std::array go_signature_results{nontrivial_array};
    const auto go_signature = classify_signature(
        *go, go_rollback, go_signature_results);
    if (!go_signature ||
        go_signature.layout.call.arguments[0].pieces.empty() ||
        go_signature.layout.call.arguments[0]
                .pieces[0].location.stack_offset != 0 ||
        !register_is(go_signature.layout.call.arguments[1], 0, "rax") ||
        go_signature.layout.results.empty() ||
        go_signature.layout.results[0].pieces.empty() ||
        go_signature.layout.results[0]
                .pieces[0].location.kind != LocationKind::Stack ||
        go_signature.layout.results[0]
                .pieces[0].location.stack_offset != 80 ||
        go_signature.layout.call.register_spill_size != 8 ||
        go_signature.layout.call.used_stack_size != 168 ||
        go_signature.layout.call.outgoing_area_size != 168) {
        return fail(
            "Go argument/result/spill stack-region ordering");
    }

    const std::array go_zero{scalar(ScalarMode::zero()), i64};
    const auto go_zero_layout =
        classify_call_arguments(*go, go_zero);
    if (!go_zero_layout ||
        go_zero_layout.layout.arguments[0].pieces.empty() ||
        go_zero_layout.layout.arguments[0].pieces[0].location.kind !=
            LocationKind::Stack ||
        go_zero_layout.layout.arguments[0].stack_size != 0 ||
        !register_is(go_zero_layout.layout.arguments[1], 0, "rax")) {
        return fail("Go zero-size stack location");
    }

    const auto go_result = classify_return(*go, go_struct);
    if (!go_result ||
        !register_is(go_result, 0, "rax") ||
        !register_is(go_result, 1, "xmm0") ||
        !register_is(go_result, 2, "rbx")) {
        return fail("Go result banks do not reset independently");
    }

    const std::array go_small_spill{
        scalar(ScalarMode::integer(8))};
    const auto go_small_layout =
        classify_call_arguments(*go, go_small_spill);
    if (!go_small_layout ||
        go_small_layout.layout.register_spill_size != 8 ||
        go_small_layout.layout.outgoing_area_size != 8) {
        return fail("Go register spill area rounding");
    }

    const auto rust_small = aggregate(64, {i64});
    const std::array rust_small_arguments{rust_small};
    const auto rust_small_layout =
        classify_call_arguments(*rust, rust_small_arguments);
    if (!rust_small_layout ||
        !register_is(rust_small_layout.layout.arguments[0], 0,
                     "rdi")) {
        return fail("Rust small aggregate coercion");
    }

    const auto rust_pair = pair({i64, i64});
    const std::array rust_pair_arguments{rust_pair};
    const auto rust_pair_layout =
        classify_call_arguments(*rust, rust_pair_arguments);
    if (!rust_pair_layout ||
        !register_is(rust_pair_layout.layout.arguments[0], 0, "rdi") ||
        !register_is(rust_pair_layout.layout.arguments[0], 1, "rsi")) {
        return fail("Rust scalar-pair direct lowering");
    }

    const auto rust_large = aggregate(192, {i64, i64, i64});
    const std::array rust_large_arguments{rust_large};
    const auto rust_large_layout =
        classify_call_arguments(*rust, rust_large_arguments);
    if (!rust_large_layout ||
        !rust_large_layout.layout.arguments[0].indirect ||
        !register_is(rust_large_layout.layout.arguments[0], 0,
                     "rdi")) {
        return fail("Rust large aggregate indirect argument");
    }
    const auto rust_return = classify_return(*rust, rust_large);
    if (!rust_return || !rust_return.indirect ||
        !register_is(rust_return, 0, "rdi") ||
        rust_return.indirect_result_reg != "rax") {
        return fail("Rust large return-area channel");
    }

    const std::array rust_hidden_arguments{i64};
    const std::array rust_hidden_results{rust_large};
    const auto rust_hidden = classify_signature(
        *rust, rust_hidden_arguments, rust_hidden_results);
    if (!rust_hidden ||
        rust_hidden.layout.results.empty() ||
        !register_is(rust_hidden.layout.results[0], 0, "rdi") ||
        !register_is(rust_hidden.layout.call.arguments[0], 0, "rsi")) {
        return fail("Rust hidden result channel did not consume argument cursor");
    }

    const auto rust_vector = scalar(ScalarMode::vector(128));
    const std::array rust_vector_arguments{rust_vector};
    const auto rust_vector_layout =
        classify_call_arguments(*rust, rust_vector_arguments);
    if (!rust_vector_layout ||
        !rust_vector_layout.layout.arguments[0].indirect ||
        !register_is(rust_vector_layout.layout.arguments[0], 0,
                     "rdi")) {
        return fail("Rust SIMD indirect lowering");
    }
    return diagnostics.errors() == 0 ? 0 : 1;
}
