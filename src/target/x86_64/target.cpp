// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/x86_64/target.hpp"

#include "target/subtarget.hpp"
#include "target/x86_64/manual_endpoint.hpp"

namespace cross {
namespace {

std::string_view feature_name(x86_64::RegisterFeature feature) {
    switch (feature) {
    case x86_64::RegisterFeature::base: return "base";
    case x86_64::RegisterFeature::avx: return "avx";
    case x86_64::RegisterFeature::avx512f: return "avx512f";
    }
    return "unknown";
}

std::vector<RegisterEntry> target_registers() {
    std::vector<RegisterEntry> result;
    result.reserve(x86_64::register_views().size());
    for (const auto& view : x86_64::register_views()) {
        std::vector<RegisterEntry::ScalarMode> hard_modes;
        if (view.register_class == x86_64::RegisterClass::integer) {
            hard_modes.push_back({view.bits, false});
        } else if (view.register_class == x86_64::RegisterClass::simd) {
            if (view.bits >= 32) {
                hard_modes.push_back({32, false});
                hard_modes.push_back({32, true});
            }
            if (view.bits >= 64) {
                hard_modes.push_back({64, false});
                hard_modes.push_back({64, true});
            }
        }
        const auto register_class =
            view.register_class == x86_64::RegisterClass::integer
                ? "integer"
                : view.register_class == x86_64::RegisterClass::simd
                      ? "simd"
                      : view.register_class == x86_64::RegisterClass::mask
                            ? "mask" : "x87";
        result.push_back({view.name, view.storage_name, view.bits,
                          register_class,
                          feature_name(view.required_feature), view.address_capable,
                          std::move(hard_modes),
                          view.storage_name == "rsp" ||
                              view.storage_name == "rbp"});
    }
    return result;
}

unsigned integer_constant_materialization_cost(
    const Subtarget&, const IntegerConstantCostQuery& query) {
    if (query.bits > 64 || query.high != 0) return 2;
    if (query.bits < 64) return 1;
    const auto value = static_cast<std::int64_t>(query.low);
    return value < std::numeric_limits<std::int32_t>::min() ||
                   value > std::numeric_limits<std::int32_t>::max()
               ? 2U
               : 1U;
}

InstructionOperandEntry integer_register_operand(
    InstructionOperandRole role, unsigned bits) {
    return {role, true, false, bits, 0, false,
            false, false, "integer"};
}

InstructionOperandEntry class_register_operand(
    InstructionOperandRole role, unsigned bits, std::string_view register_class) {
    return {role, true, false, bits, 0, false,
            false, false, register_class};
}

InstructionOperandEntry integer_source_operand(
    unsigned bits, unsigned immediate_bits, bool immediate_signed) {
    return {InstructionOperandRole::Input, true, true, bits,
            immediate_bits, immediate_signed,
            false, false, "integer"};
}

InstructionOperandEntry integer_memory_operand(
    InstructionOperandRole role, unsigned bits, bool allow_atomic = false) {
    InstructionOperandEntry result;
    result.role = role;
    result.allow_memory = true;
    result.memory_bits = bits;
    result.allow_atomic_memory = allow_atomic;
    return result;
}

InstructionOperandEntry address_memory_operand(
    InstructionOperandRole role, bool allow_atomic = false) {
    return integer_memory_operand(role, 0, allow_atomic);
}

InstructionEntry integer_move_form(std::string_view name,
                                   std::string_view mnemonic,
                                   unsigned bits) {
    return {name, "x86-64", mnemonic,
            {integer_register_operand(InstructionOperandRole::Output, bits),
             integer_register_operand(InstructionOperandRole::Input, bits)},
            {}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_binary_form(std::string_view name,
                                     std::string_view mnemonic,
                                     unsigned bits,
                                     unsigned immediate_bits,
                                     bool immediate_signed,
                                     bool reads_flags = false) {
    return {name, "x86-64", mnemonic,
            {integer_register_operand(InstructionOperandRole::InOut, bits),
             integer_source_operand(bits, immediate_bits, immediate_signed)},
            reads_flags ? std::vector<std::string_view>{"flags"}
                        : std::vector<std::string_view>{},
            {"flags"}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_shift_form(std::string_view name,
                                    std::string_view mnemonic,
                                    unsigned bits) {
    return {name, "x86-64", mnemonic,
            {integer_register_operand(InstructionOperandRole::InOut, bits),
             {InstructionOperandRole::Input, false, true, 0, 8, false}},
            {}, {"flags"}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_compare_form(std::string_view name,
                                      std::string_view mnemonic,
                                      unsigned bits,
                                      unsigned immediate_bits,
                                      bool immediate_signed) {
    return {name, "x86-64", mnemonic,
            {integer_register_operand(InstructionOperandRole::Input, bits),
             integer_source_operand(bits, immediate_bits, immediate_signed)},
            {}, {"flags"}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_unary_form(std::string_view name,
                                    std::string_view mnemonic,
                                    unsigned bits,
                                    bool writes_flags,
                                    bool preserves_some_flags = false) {
    return {name, "x86-64", mnemonic,
            {integer_register_operand(InstructionOperandRole::InOut, bits)},
            preserves_some_flags ? std::vector<std::string_view>{"flags"}
                                 : std::vector<std::string_view>{},
            writes_flags ? std::vector<std::string_view>{"flags"}
                         : std::vector<std::string_view>{},
            0, InstructionControlEffect::None};
}

InstructionEntry integer_exchange_form(std::string_view name,
                                       std::string_view mnemonic,
                                       unsigned bits,
                                       bool writes_flags) {
    return {name, "x86-64", mnemonic,
            {integer_register_operand(InstructionOperandRole::InOut, bits),
             integer_register_operand(InstructionOperandRole::InOut, bits)},
            {}, writes_flags ? std::vector<std::string_view>{"flags"}
                             : std::vector<std::string_view>{},
            0, InstructionControlEffect::None};
}

InstructionEntry integer_bit_form(std::string_view name,
                                  std::string_view mnemonic,
                                  unsigned bits,
                                  bool modifies_base) {
    return {name, "x86-64", mnemonic,
            {integer_register_operand(modifies_base
                                          ? InstructionOperandRole::InOut
                                          : InstructionOperandRole::Input,
                                      bits),
             integer_source_operand(bits, 8, false)},
            {}, {"flags"}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_cmov_form(std::string_view name,
                                   std::string_view mnemonic,
                                   unsigned bits) {
    return {name, "x86-64", mnemonic,
            {integer_register_operand(InstructionOperandRole::InOut, bits),
             integer_register_operand(InstructionOperandRole::Input, bits)},
            {"flags"}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_setcc_form(std::string_view name,
                                    std::string_view mnemonic) {
    return {name, "x86-64", mnemonic,
            {integer_register_operand(InstructionOperandRole::Output, 8)},
            {"flags"}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_load_form(std::string_view name,
                                   std::string_view mnemonic,
                                   unsigned bits) {
    return {name, "x86-64", mnemonic,
            {integer_register_operand(InstructionOperandRole::Output, bits),
             integer_memory_operand(InstructionOperandRole::Input, bits)},
            {"memory"}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_store_form(std::string_view name,
                                    std::string_view mnemonic,
                                    unsigned bits) {
    return {name, "x86-64", mnemonic,
            {integer_memory_operand(InstructionOperandRole::Output, bits),
             integer_register_operand(InstructionOperandRole::Input, bits)},
            {}, {"memory"}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_register_memory_form(
    std::string_view name, std::string_view mnemonic, unsigned bits,
    bool reads_flags = false) {
    return {name, "x86-64", mnemonic,
            {integer_register_operand(InstructionOperandRole::InOut, bits),
             integer_memory_operand(InstructionOperandRole::Input, bits)},
            reads_flags ? std::vector<std::string_view>{"flags", "memory"}
                        : std::vector<std::string_view>{"memory"},
            {"flags"}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_memory_source_form(
    std::string_view name, std::string_view mnemonic, unsigned bits,
    unsigned immediate_bits, bool immediate_signed,
    bool reads_flags = false) {
    auto reads = std::vector<std::string_view>{"memory"};
    if (reads_flags) reads.push_back("flags");
    return {name, "x86-64", mnemonic,
            {integer_memory_operand(InstructionOperandRole::InOut, bits),
             integer_source_operand(bits, immediate_bits, immediate_signed)},
            std::move(reads), {"flags", "memory"}, 0,
            InstructionControlEffect::None};
}

InstructionEntry integer_compare_register_memory_form(
    std::string_view name, std::string_view mnemonic, unsigned bits) {
    return {name, "x86-64", mnemonic,
            {integer_register_operand(InstructionOperandRole::Input, bits),
             integer_memory_operand(InstructionOperandRole::Input, bits)},
            {"memory"}, {"flags"}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_compare_memory_source_form(
    std::string_view name, std::string_view mnemonic, unsigned bits,
    unsigned immediate_bits, bool immediate_signed) {
    return {name, "x86-64", mnemonic,
            {integer_memory_operand(InstructionOperandRole::Input, bits),
             integer_source_operand(bits, immediate_bits, immediate_signed)},
            {"memory"}, {"flags"}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_unary_memory_form(
    std::string_view name, std::string_view mnemonic, unsigned bits,
    bool writes_flags, bool preserves_some_flags = false) {
    auto reads = std::vector<std::string_view>{"memory"};
    if (preserves_some_flags) reads.push_back("flags");
    auto writes = std::vector<std::string_view>{"memory"};
    if (writes_flags) writes.push_back("flags");
    return {name, "x86-64", mnemonic,
            {integer_memory_operand(InstructionOperandRole::InOut, bits)},
            std::move(reads), std::move(writes), 0,
            InstructionControlEffect::None};
}

InstructionEntry integer_shift_memory_form(
    std::string_view name, std::string_view mnemonic, unsigned bits) {
    return {name, "x86-64", mnemonic,
            {integer_memory_operand(InstructionOperandRole::InOut, bits),
             {InstructionOperandRole::Input, false, true, 0, 8, false}},
            {"memory"}, {"flags", "memory"}, 0,
            InstructionControlEffect::None};
}

InstructionEntry integer_cmov_memory_form(
    std::string_view name, std::string_view mnemonic, unsigned bits) {
    return {name, "x86-64", mnemonic,
            {integer_register_operand(InstructionOperandRole::InOut, bits),
             integer_memory_operand(InstructionOperandRole::Input, bits)},
            {"flags", "memory"}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_exchange_memory_form(
    std::string_view name, std::string_view mnemonic, unsigned bits,
    bool writes_flags) {
    auto writes = std::vector<std::string_view>{"memory"};
    if (writes_flags) writes.push_back("flags");
    return {name, "x86-64", mnemonic,
            {integer_memory_operand(InstructionOperandRole::InOut, bits,
                                    name == "$::_xchg"),
             integer_register_operand(InstructionOperandRole::InOut, bits)},
            {"memory"}, std::move(writes), 0,
            InstructionControlEffect::None};
}

InstructionEntry integer_cmpxchg_form(std::string_view name,
                                      std::string_view feature,
                                      std::string_view mnemonic,
                                      unsigned bits,
                                      bool memory,
                                      bool atomic_memory = false) {
    return {name, feature, mnemonic,
            {memory
                 ? integer_memory_operand(InstructionOperandRole::InOut, bits,
                                          atomic_memory)
                 : integer_register_operand(InstructionOperandRole::InOut, bits),
             integer_register_operand(InstructionOperandRole::Input, bits)},
            memory ? std::vector<std::string_view>{"rax", "memory"}
                   : std::vector<std::string_view>{"rax"},
            memory ? std::vector<std::string_view>{"rax", "flags", "memory"}
                   : std::vector<std::string_view>{"rax", "flags"},
            0, InstructionControlEffect::None};
}

InstructionEntry integer_locked_exchange_form(
    std::string_view name, std::string_view mnemonic, unsigned bits) {
    return {name, "x86-64", mnemonic,
            {integer_memory_operand(InstructionOperandRole::InOut, bits, true),
             integer_register_operand(InstructionOperandRole::InOut, bits)},
            {"memory"}, {"flags", "memory"}, 0,
            InstructionControlEffect::None};
}

InstructionEntry integer_movbe_load_form(unsigned bits) {
    return {"$::_movbe", "movbe", bits == 16 ? "movbew" :
                                     bits == 32 ? "movbel" : "movbeq",
            {integer_register_operand(InstructionOperandRole::Output, bits),
             integer_memory_operand(InstructionOperandRole::Input, bits)},
            {"memory"}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_movbe_store_form(unsigned bits) {
    return {"$::_movbe", "movbe", bits == 16 ? "movbew" :
                                     bits == 32 ? "movbel" : "movbeq",
            {integer_memory_operand(InstructionOperandRole::Output, bits),
             integer_register_operand(InstructionOperandRole::Input, bits)},
            {}, {"memory"}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_extend_form(
    std::string_view name, std::string_view mnemonic,
    unsigned destination_bits, unsigned source_bits, bool memory) {
    return {name, "x86-64", mnemonic,
            {integer_register_operand(InstructionOperandRole::Output,
                                      destination_bits),
             memory
                 ? integer_memory_operand(InstructionOperandRole::Input,
                                          source_bits)
                 : integer_register_operand(InstructionOperandRole::Input,
                                            source_bits)},
            memory ? std::vector<std::string_view>{"memory"}
                   : std::vector<std::string_view>{},
            {}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_scan_form(
    std::string_view name, std::string_view mnemonic,
    unsigned bits, bool memory) {
    return {name, "x86-64", mnemonic,
            {integer_register_operand(InstructionOperandRole::Output, bits),
             memory
                 ? integer_memory_operand(InstructionOperandRole::Input, bits)
                 : integer_register_operand(InstructionOperandRole::Input, bits)},
            memory ? std::vector<std::string_view>{"memory"}
                   : std::vector<std::string_view>{},
            {"flags"}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_lea_form(unsigned memory_bits) {
    return {"$::_lea", "x86-64", "leaq",
            {integer_register_operand(InstructionOperandRole::Output, 64),
             integer_memory_operand(InstructionOperandRole::Input,
                                    memory_bits)},
            {}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry integer_accumulator_form(
    std::string_view name, std::string_view mnemonic, unsigned bits,
    bool signed_divide = false, bool divide = false) {
    std::vector<std::string_view> reads{"rax"};
    std::vector<std::string_view> writes{"rax", "flags"};
    if (bits != 8) {
        reads.push_back("rdx");
        writes.push_back("rdx");
    }
    if (!divide && !signed_divide) reads.resize(1);
    return {name, "x86-64", mnemonic,
            {integer_register_operand(InstructionOperandRole::Input, bits)},
            std::move(reads), std::move(writes), 0,
            InstructionControlEffect::None};
}

InstructionOperandEntry x87_register_operand(
    InstructionOperandRole role, std::string_view storage) {
    auto result = class_register_operand(role, 80, "x87");
    result.register_storage = storage;
    return result;
}

InstructionEntry fixed_integer_register_form(
    std::string_view name, std::string_view mnemonic,
    InstructionOperandRole role, unsigned bits, std::string_view storage,
    std::vector<std::string_view> reads = {},
    std::vector<std::string_view> writes = {}) {
    auto operand = integer_register_operand(role, bits);
    operand.register_storage = storage;
    return {name, "x86-64", mnemonic, {operand}, std::move(reads),
            std::move(writes), 0, InstructionControlEffect::None};
}

InstructionEntry x87_memory_form(
    std::string_view name, std::string_view mnemonic, unsigned bits,
    InstructionOperandRole role, int stack_delta = 0,
    std::vector<std::string_view> reads = {},
    std::vector<std::string_view> writes = {}) {
    InstructionEntry result{
        name, "x86-64", mnemonic, {integer_memory_operand(role, bits)},
        std::move(reads), std::move(writes), 0,
        InstructionControlEffect::None};
    result.ordered_stack_delta = stack_delta;
    return result;
}

InstructionEntry x87_pair_form(
    std::string_view name, std::string_view mnemonic,
    std::string_view destination, std::string_view source,
    int stack_delta = 0, bool writes_flags = false) {
    InstructionEntry result{
        name, "x86-64", mnemonic,
        {x87_register_operand(InstructionOperandRole::InOut, destination),
         x87_register_operand(InstructionOperandRole::Input, source)},
        {}, writes_flags ? std::vector<std::string_view>{"flags"}
                         : std::vector<std::string_view>{"x87-status"},
        0, InstructionControlEffect::None};
    result.ordered_stack_delta = stack_delta;
    return result;
}

InstructionEntry x87_index_form(
    std::string_view name, std::string_view mnemonic,
    std::string_view storage, InstructionOperandRole role,
    int stack_delta = 0,
    std::vector<std::string_view> reads = {"st0"},
    std::vector<std::string_view> writes = {"x87-status"}) {
    InstructionEntry result{
        name, "x86-64", mnemonic,
        {x87_register_operand(role, storage)}, std::move(reads),
        std::move(writes), 0, InstructionControlEffect::None};
    result.ordered_stack_delta = stack_delta;
    return result;
}

InstructionEntry x87_nullary_form(
    std::string_view name, std::string_view mnemonic,
    std::vector<std::string_view> reads,
    std::vector<std::string_view> writes,
    int stack_delta = 0) {
    InstructionEntry result{name, "x86-64", mnemonic, {}, std::move(reads),
                            std::move(writes), 0,
                            InstructionControlEffect::None};
    result.ordered_stack_delta = stack_delta;
    return result;
}

InstructionEntry integer_bit_memory_form(
    std::string_view name, std::string_view mnemonic, unsigned bits,
    bool modifies_base) {
    auto writes = std::vector<std::string_view>{"flags"};
    if (modifies_base) writes.push_back("memory");
    return {name, "x86-64", mnemonic,
            {integer_memory_operand(modifies_base
                                        ? InstructionOperandRole::InOut
                                        : InstructionOperandRole::Input,
                                    bits),
             integer_source_operand(bits, 8, false)},
            {"memory"}, std::move(writes), 0,
            InstructionControlEffect::None};
}

InstructionEntry packed_legacy_binary_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned bits,
    bool writes_flags = false) {
    return {name, feature, mnemonic,
            {class_register_operand(InstructionOperandRole::InOut, bits, "simd"),
             class_register_operand(InstructionOperandRole::Input, bits, "simd")},
            {}, writes_flags ? std::vector<std::string_view>{"flags"}
                             : std::vector<std::string_view>{},
            0, InstructionControlEffect::None};
}

InstructionEntry packed_legacy_unary_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned bits) {
    return {name, feature, mnemonic,
            {class_register_operand(InstructionOperandRole::Output, bits, "simd"),
             class_register_operand(InstructionOperandRole::Input, bits, "simd")},
            {}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry packed_legacy_immediate_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned bits,
    bool destructive = true) {
    return {name, feature, mnemonic,
            {class_register_operand(destructive
                                        ? InstructionOperandRole::InOut
                                        : InstructionOperandRole::Output,
                                    bits, "simd"),
             class_register_operand(InstructionOperandRole::Input, bits, "simd"),
             {InstructionOperandRole::Input, false, true, 0, 8, false}},
            {}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry packed_legacy_shift_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned bits) {
    return {name, feature, mnemonic,
            {class_register_operand(InstructionOperandRole::InOut, bits, "simd"),
             {InstructionOperandRole::Input, false, true, 0, 8, false}},
            {}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry packed_vex_binary_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned bits) {
    return {name, feature, mnemonic,
            {class_register_operand(InstructionOperandRole::Output, bits, "simd"),
             class_register_operand(InstructionOperandRole::Input, bits, "simd"),
             class_register_operand(InstructionOperandRole::Input, bits, "simd")},
            {}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry packed_vex_unary_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned bits) {
    return {name, feature, mnemonic,
            {class_register_operand(InstructionOperandRole::Output, bits, "simd"),
             class_register_operand(InstructionOperandRole::Input, bits, "simd")},
            {}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry packed_vex_immediate_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned bits) {
    return {name, feature, mnemonic,
            {class_register_operand(InstructionOperandRole::Output, bits, "simd"),
             class_register_operand(InstructionOperandRole::Input, bits, "simd"),
             {InstructionOperandRole::Input, false, true, 0, 8, false}},
            {}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry packed_load_form(std::string_view name,
                                  std::string_view feature,
                                  std::string_view mnemonic,
                                  unsigned bits) {
    return {name, feature, mnemonic,
            {class_register_operand(InstructionOperandRole::Output, bits, "simd"),
             integer_memory_operand(InstructionOperandRole::Input, bits)},
            {"memory"}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry packed_store_form(std::string_view name,
                                   std::string_view feature,
                                   std::string_view mnemonic,
                                   unsigned bits) {
    return {name, feature, mnemonic,
            {integer_memory_operand(InstructionOperandRole::Output, bits),
             class_register_operand(InstructionOperandRole::Input, bits, "simd")},
            {}, {"memory"}, 0, InstructionControlEffect::None};
}

InstructionEntry packed_legacy_binary_memory_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned bits) {
    return {name, feature, mnemonic,
            {class_register_operand(InstructionOperandRole::InOut, bits, "simd"),
             integer_memory_operand(InstructionOperandRole::Input, bits)},
            {"memory"}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry packed_vex_binary_memory_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned bits) {
    return {name, feature, mnemonic,
            {class_register_operand(InstructionOperandRole::Output, bits, "simd"),
             class_register_operand(InstructionOperandRole::Input, bits, "simd"),
             integer_memory_operand(InstructionOperandRole::Input, bits)},
            {"memory"}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry packed_legacy_immediate_memory_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned register_bits,
    unsigned memory_bits, bool destructive = true) {
    return {name, feature, mnemonic,
            {class_register_operand(destructive
                                        ? InstructionOperandRole::InOut
                                        : InstructionOperandRole::Output,
                                    register_bits, "simd"),
             integer_memory_operand(InstructionOperandRole::Input,
                                    memory_bits),
             {InstructionOperandRole::Input, false, true, 0, 8, false}},
            {"memory"}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry packed_vex_unary_memory_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned register_bits,
    unsigned memory_bits) {
    return {name, feature, mnemonic,
            {class_register_operand(InstructionOperandRole::Output,
                                    register_bits, "simd"),
             integer_memory_operand(InstructionOperandRole::Input,
                                    memory_bits)},
            {"memory"}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry packed_vex_binary_immediate_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned bits) {
    return {name, feature, mnemonic,
            {class_register_operand(InstructionOperandRole::Output, bits,
                                    "simd"),
             class_register_operand(InstructionOperandRole::Input, bits,
                                    "simd"),
             class_register_operand(InstructionOperandRole::Input, bits,
                                    "simd"),
             {InstructionOperandRole::Input, false, true, 0, 8, false}},
            {}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry packed_vex_binary_immediate_memory_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned bits) {
    return {name, feature, mnemonic,
            {class_register_operand(InstructionOperandRole::Output, bits,
                                    "simd"),
             class_register_operand(InstructionOperandRole::Input, bits,
                                    "simd"),
             integer_memory_operand(InstructionOperandRole::Input, bits),
             {InstructionOperandRole::Input, false, true, 0, 8, false}},
            {"memory"}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry packed_to_integer_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned integer_bits,
    unsigned vector_bits) {
    return {name, feature, mnemonic,
            {integer_register_operand(InstructionOperandRole::Output,
                                      integer_bits),
             class_register_operand(InstructionOperandRole::Input,
                                    vector_bits, "simd")},
            {}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry system_memory_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, InstructionOperandRole role,
    bool writes_memory) {
    return {name, feature, mnemonic,
            {address_memory_operand(role)},
            {"memory"},
            writes_memory ? std::vector<std::string_view>{"memory"}
                          : std::vector<std::string_view>{},
            0, InstructionControlEffect::None};
}

InstructionEntry xstate_memory_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, InstructionOperandRole role,
    bool writes_memory) {
    auto result = system_memory_form(
        name, feature, mnemonic, role, writes_memory);
    result.implicit_reads = {"rax", "rdx", "memory"};
    return result;
}

InstructionEntry packed_evex_masked_binary_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned bits, bool zeroing,
    std::string_view operand_prefix = {}) {
    InstructionEntry result{
        name, feature, mnemonic,
        {class_register_operand(zeroing ? InstructionOperandRole::Output
                                        : InstructionOperandRole::InOut,
                                bits, "simd"),
         class_register_operand(InstructionOperandRole::Input, bits, "simd"),
         class_register_operand(InstructionOperandRole::Input, bits, "simd"),
         class_register_operand(InstructionOperandRole::Input, 64, "mask")},
        {}, {}, 0, InstructionControlEffect::None};
    result.assembly_mask_operand = 3;
    result.assembly_zeroing = zeroing;
    result.assembly_operand_prefix = operand_prefix;
    return result;
}

InstructionEntry packed_evex_masked_binary_memory_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned bits, unsigned memory_bits,
    bool zeroing, unsigned broadcast_count = 0) {
    InstructionEntry result{
        name, feature, mnemonic,
        {class_register_operand(zeroing ? InstructionOperandRole::Output
                                        : InstructionOperandRole::InOut,
                                bits, "simd"),
         class_register_operand(InstructionOperandRole::Input, bits, "simd"),
         integer_memory_operand(InstructionOperandRole::Input, memory_bits),
         class_register_operand(InstructionOperandRole::Input, 64, "mask")},
        {"memory"}, {}, 0, InstructionControlEffect::None};
    result.assembly_mask_operand = 3;
    result.assembly_zeroing = zeroing;
    if (broadcast_count != 0) {
        result.assembly_broadcast_operand = 2;
        result.assembly_broadcast_count = broadcast_count;
    }
    return result;
}

InstructionEntry packed_evex_compare_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic, unsigned bits) {
    return {name, feature, mnemonic,
            {class_register_operand(InstructionOperandRole::Output, 64,
                                    "mask"),
             class_register_operand(InstructionOperandRole::Input, bits,
                                    "simd"),
             class_register_operand(InstructionOperandRole::Input, bits,
                                    "simd"),
             {InstructionOperandRole::Input, false, true, 0, 8, false}},
            {}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry mask_binary_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic) {
    return {name, feature, mnemonic,
            {class_register_operand(InstructionOperandRole::Output, 64,
                                    "mask"),
             class_register_operand(InstructionOperandRole::Input, 64,
                                    "mask"),
             class_register_operand(InstructionOperandRole::Input, 64,
                                    "mask")},
            {}, {}, 0, InstructionControlEffect::None};
}

InstructionEntry mask_immediate_form(
    std::string_view name, std::string_view feature,
    std::string_view mnemonic) {
    return {name, feature, mnemonic,
            {class_register_operand(InstructionOperandRole::Output, 64,
                                    "mask"),
             class_register_operand(InstructionOperandRole::Input, 64,
                                    "mask"),
             {InstructionOperandRole::Input, false, true, 0, 8, false}},
            {}, {}, 0, InstructionControlEffect::None};
}

const SubtargetTable subtargets{
    "x86-64",
    {
        {"base", true, false, {}, {}},
        {"sse3", false, true, {"base"}, {}},
        {"ssse3", false, true, {"sse3"}, {}},
        {"sse4.1", false, true, {"ssse3"}, {}},
        {"sse4.2", false, true, {"sse4.1"}, {}},
        {"popcnt", false, true, {"base"}, {}},
        {"aes", false, true, {"base"}, {}},
        {"pclmul", false, true, {"base"}, {}},
        {"avx", false, true, {"sse4.2"}, {}},
        {"f16c", false, true, {"avx"}, {}},
        {"fma", false, true, {"avx"}, {}},
        {"avx2", false, true, {"avx"}, {}},
        {"bmi", false, true, {"base"}, {}},
        {"bmi2", false, true, {"base"}, {}},
        {"lzcnt", false, true, {"base"}, {}},
        {"movbe", false, true, {"base"}, {}},
        {"adx", false, true, {"base"}, {}},
        {"rdrnd", false, true, {"base"}, {}},
        {"rdseed", false, true, {"base"}, {}},
        {"sha", false, true, {"base"}, {}},
        {"gfni", false, true, {"base"}, {}},
        {"vpclmulqdq", false, true, {"avx", "pclmul"}, {}},
        {"vaes", false, true, {"avx2", "aes"}, {}},
        {"serialize", false, true, {"base"}, {}},
        {"cx16", false, true, {"base"}, {}},
        {"rdtscp", false, true, {"base"}, {}},
        {"xsave", false, true, {"base"}, {}},
        {"xsaveopt", false, true, {"xsave"}, {}},
        {"xsavec", false, true, {"xsave"}, {}},
        {"xsaves", false, true, {"xsave"}, {}},
        {"fsgsbase", false, true, {"base"}, {}},
        {"rdpid", false, true, {"base"}, {}},
        {"pku", false, true, {"base"}, {}},
        {"clflushopt", false, true, {"base"}, {}},
        {"clwb", false, true, {"base"}, {}},
        {"prefetchw", false, true, {"base"}, {}},
        {"monitor", false, true, {"base"}, {}},
        {"waitpkg", false, true, {"base"}, {}},
        {"invpcid", false, true, {"base"}, {}},
        {"cldemote", false, true, {"base"}, {}},
        {"movdiri", false, true, {"base"}, {}},
        {"movdir64b", false, true, {"base"}, {}},
        {"enqcmd", false, true, {"base"}, {}},
        {"wbnoinvd", false, true, {"base"}, {}},
        {"avx512f", false, true, {"avx2", "fma", "f16c"}, {}},
        {"avx512dq", false, true, {"avx512f"}, {}},
        {"avx512cd", false, true, {"avx512f"}, {}},
        {"avx512bw", false, true, {"avx512f"}, {}},
        {"avx512vl", false, true, {"avx512f"}, {}},
        {"avx512vnni", false, true, {"avx512f"}, {}},
        {"avx512vbmi", false, true, {"avx512bw"}, {}},
        {"avx512vbmi2", false, true, {"avx512bw"}, {}},
        {"avx512bitalg", false, true, {"avx512bw"}, {}},
        {"avx512vpopcntdq", false, true, {"avx512f"}, {}},
    },
    {
        {"generic", {"base"}},
        {"x86-64", {"base"}},
        {"x86-64-v2", {"base", "sse3", "ssse3", "sse4.1", "sse4.2",
                        "popcnt", "cx16"}},
        {"x86-64-v3", {"base", "sse3", "ssse3", "sse4.1", "sse4.2",
                        "popcnt", "avx", "f16c", "fma", "avx2", "bmi",
                        "bmi2", "lzcnt", "movbe", "cx16", "xsave"}},
        {"x86-64-v4", {"base", "sse3", "ssse3", "sse4.1", "sse4.2",
                        "popcnt", "avx", "f16c", "fma", "avx2", "bmi",
                        "bmi2", "lzcnt", "movbe", "avx512f", "avx512dq",
                        "avx512cd", "avx512bw", "avx512vl", "cx16",
                        "xsave"}},
        {"haswell", {"base", "sse3", "ssse3", "sse4.1", "sse4.2",
                     "popcnt", "aes", "pclmul", "avx", "f16c", "fma",
                     "avx2", "bmi", "bmi2", "lzcnt", "movbe", "rdrnd",
                     "cx16", "xsave", "xsaveopt", "fsgsbase", "rdtscp",
                     "invpcid"}},
        {"skylake", {"base", "sse3", "ssse3", "sse4.1", "sse4.2",
                     "popcnt", "aes", "pclmul", "avx", "f16c", "fma",
                     "avx2", "bmi", "bmi2", "lzcnt", "movbe", "adx",
                     "rdrnd", "rdseed", "cx16", "xsave", "xsaveopt",
                     "xsavec", "xsaves", "fsgsbase", "rdtscp", "pku",
                     "clflushopt", "invpcid"}},
        {"skylake-avx512",
         {"base", "sse3", "ssse3", "sse4.1", "sse4.2", "popcnt",
          "aes", "pclmul", "avx", "f16c", "fma", "avx2", "bmi",
          "bmi2", "lzcnt", "movbe", "adx", "rdrnd", "rdseed",
          "avx512f", "avx512dq",
          "avx512cd", "avx512bw", "avx512vl", "cx16", "xsave",
          "xsaveopt", "xsavec", "xsaves", "fsgsbase", "rdtscp", "pku",
          "clflushopt", "invpcid"}},
        {"znver1", {"base", "sse3", "ssse3", "sse4.1", "sse4.2",
                    "popcnt", "aes", "pclmul", "avx", "f16c", "fma",
                    "avx2", "bmi", "bmi2", "lzcnt", "movbe", "adx",
                    "rdrnd", "rdseed", "sha", "cx16", "xsave",
                    "xsaveopt", "xsavec", "xsaves", "fsgsbase", "rdtscp",
                    "prefetchw", "monitor"}},
        {"znver2", {"base", "sse3", "ssse3", "sse4.1", "sse4.2",
                    "popcnt", "aes", "pclmul", "avx", "f16c", "fma",
                    "avx2", "bmi", "bmi2", "lzcnt", "movbe", "adx",
                    "rdrnd", "rdseed", "sha", "cx16", "xsave",
                    "xsaveopt", "xsavec", "xsaves", "fsgsbase", "rdtscp",
                    "rdpid", "prefetchw", "monitor", "clwb"}},
        {"znver3", {"base", "sse3", "ssse3", "sse4.1", "sse4.2",
                    "popcnt", "aes", "pclmul", "avx", "f16c", "fma",
                    "avx2", "bmi", "bmi2", "lzcnt", "movbe", "adx",
                    "rdrnd", "rdseed", "sha", "gfni", "vaes",
                    "vpclmulqdq", "cx16", "xsave", "xsaveopt", "xsavec",
                    "xsaves", "fsgsbase", "rdtscp", "rdpid", "prefetchw",
                    "monitor", "clwb"}},
    },
};

const TargetInfo target{
    "x86-64",
    {"x86_64", "amd64"},
    {ByteOrder::Little, 16, 16, 16,
     BitFieldOrder::LeastSignificantFirst},
    target_registers(),
    {
        {"i8", 8, "x86-64"}, {"u8", 8, "x86-64"},
        {"i16", 16, "x86-64"}, {"u16", 16, "x86-64"},
        {"i32", 32, "x86-64"}, {"u32", 32, "x86-64"},
        {"i64", 64, "x86-64"}, {"u64", 64, "x86-64"},
        {"iptr", 64, "x86-64"}, {"uptr", 64, "x86-64"},
    },
    {
        {"$::_movabs", "x86-64", "movabsq",
         {integer_register_operand(InstructionOperandRole::Output, 64),
          {InstructionOperandRole::Input, false, true, 0, 64, false, false, true}},
         {}, {}, 0, InstructionControlEffect::None},
        integer_binary_form("$::_add", "addq", 64, 32, true),
        integer_binary_form("$::_sub", "subq", 64, 32, true),
        integer_binary_form("$::_xor", "xorq", 64, 32, true),
        integer_binary_form("$::_and", "andq", 64, 32, true),
        integer_binary_form("$::_or", "orq", 64, 32, true),
        integer_binary_form("$::_imul", "imulq", 64, 32, true),
        integer_shift_form("$::_shl", "shlq", 64),
        integer_shift_form("$::_shr", "shrq", 64),
        integer_compare_form("$::_cmp", "cmpq", 64, 32, true),

        // Baseline integer mnemonics deliberately share source names across
        // widths.  Raw lowering resolves the typed form from the hard-bound
        // register views and immediate class.
        integer_move_form("$::_mov", "movb", 8),
        integer_move_form("$::_mov", "movw", 16),
        integer_move_form("$::_mov", "movl", 32),
        integer_move_form("$::_mov", "movq", 64),
        integer_binary_form("$::_add", "addb", 8, 8, true),
        integer_binary_form("$::_add", "addw", 16, 16, true),
        integer_binary_form("$::_add", "addl", 32, 32, true),
        integer_binary_form("$::_sub", "subb", 8, 8, true),
        integer_binary_form("$::_sub", "subw", 16, 16, true),
        integer_binary_form("$::_sub", "subl", 32, 32, true),
        integer_binary_form("$::_xor", "xorb", 8, 8, true),
        integer_binary_form("$::_xor", "xorw", 16, 16, true),
        integer_binary_form("$::_xor", "xorl", 32, 32, true),
        integer_binary_form("$::_and", "andb", 8, 8, true),
        integer_binary_form("$::_and", "andw", 16, 16, true),
        integer_binary_form("$::_and", "andl", 32, 32, true),
        integer_binary_form("$::_or", "orb", 8, 8, true),
        integer_binary_form("$::_or", "orw", 16, 16, true),
        integer_binary_form("$::_or", "orl", 32, 32, true),
        integer_binary_form("$::_imul", "imulw", 16, 16, true),
        integer_binary_form("$::_imul", "imull", 32, 32, true),
        integer_shift_form("$::_shl", "shlb", 8),
        integer_shift_form("$::_shl", "shlw", 16),
        integer_shift_form("$::_shl", "shll", 32),
        integer_shift_form("$::_shr", "shrb", 8),
        integer_shift_form("$::_shr", "shrw", 16),
        integer_shift_form("$::_shr", "shrl", 32),
        integer_shift_form("$::_sar", "sarb", 8),
        integer_shift_form("$::_sar", "sarw", 16),
        integer_shift_form("$::_sar", "sarl", 32),
        integer_shift_form("$::_sar", "sarq", 64),
        integer_shift_form("$::_rol", "rolb", 8),
        integer_shift_form("$::_rol", "rolw", 16),
        integer_shift_form("$::_rol", "roll", 32),
        integer_shift_form("$::_rol", "rolq", 64),
        integer_shift_form("$::_ror", "rorb", 8),
        integer_shift_form("$::_ror", "rorw", 16),
        integer_shift_form("$::_ror", "rorl", 32),
        integer_shift_form("$::_ror", "rorq", 64),
        integer_compare_form("$::_cmp", "cmpb", 8, 8, true),
        integer_compare_form("$::_cmp", "cmpw", 16, 16, true),
        integer_compare_form("$::_cmp", "cmpl", 32, 32, true),
        integer_compare_form("$::_test", "testb", 8, 8, true),
        integer_compare_form("$::_test", "testw", 16, 16, true),
        integer_compare_form("$::_test", "testl", 32, 32, true),
        integer_compare_form("$::_test", "testq", 64, 32, true),
        integer_binary_form("$::_adc", "adcb", 8, 8, true, true),
        integer_binary_form("$::_adc", "adcw", 16, 16, true, true),
        integer_binary_form("$::_adc", "adcl", 32, 32, true, true),
        integer_binary_form("$::_adc", "adcq", 64, 32, true, true),
        integer_binary_form("$::_sbb", "sbbb", 8, 8, true, true),
        integer_binary_form("$::_sbb", "sbbw", 16, 16, true, true),
        integer_binary_form("$::_sbb", "sbbl", 32, 32, true, true),
        integer_binary_form("$::_sbb", "sbbq", 64, 32, true, true),
        integer_unary_form("$::_inc", "incb", 8, true, true),
        integer_unary_form("$::_inc", "incw", 16, true, true),
        integer_unary_form("$::_inc", "incl", 32, true, true),
        integer_unary_form("$::_inc", "incq", 64, true, true),
        integer_unary_form("$::_dec", "decb", 8, true, true),
        integer_unary_form("$::_dec", "decw", 16, true, true),
        integer_unary_form("$::_dec", "decl", 32, true, true),
        integer_unary_form("$::_dec", "decq", 64, true, true),
        integer_unary_form("$::_neg", "negb", 8, true),
        integer_unary_form("$::_neg", "negw", 16, true),
        integer_unary_form("$::_neg", "negl", 32, true),
        integer_unary_form("$::_neg", "negq", 64, true),
        integer_unary_form("$::_not", "notb", 8, false),
        integer_unary_form("$::_not", "notw", 16, false),
        integer_unary_form("$::_not", "notl", 32, false),
        integer_unary_form("$::_not", "notq", 64, false),
        integer_unary_form("$::_bswap", "bswapl", 32, false),
        integer_unary_form("$::_bswap", "bswapq", 64, false),
        integer_exchange_form("$::_xchg", "xchgb", 8, false),
        integer_exchange_form("$::_xchg", "xchgw", 16, false),
        integer_exchange_form("$::_xchg", "xchgl", 32, false),
        integer_exchange_form("$::_xchg", "xchgq", 64, false),
        integer_exchange_form("$::_xadd", "xaddb", 8, true),
        integer_exchange_form("$::_xadd", "xaddw", 16, true),
        integer_exchange_form("$::_xadd", "xaddl", 32, true),
        integer_exchange_form("$::_xadd", "xaddq", 64, true),
        integer_bit_form("$::_bt", "btw", 16, false),
        integer_bit_form("$::_bt", "btl", 32, false),
        integer_bit_form("$::_bt", "btq", 64, false),
        integer_bit_form("$::_bts", "btsw", 16, true),
        integer_bit_form("$::_bts", "btsl", 32, true),
        integer_bit_form("$::_bts", "btsq", 64, true),
        integer_bit_form("$::_btr", "btrw", 16, true),
        integer_bit_form("$::_btr", "btrl", 32, true),
        integer_bit_form("$::_btr", "btrq", 64, true),
        integer_bit_form("$::_btc", "btcw", 16, true),
        integer_bit_form("$::_btc", "btcl", 32, true),
        integer_bit_form("$::_btc", "btcq", 64, true),
        integer_cmov_form("$::_cmove", "cmovew", 16),
        integer_cmov_form("$::_cmove", "cmovel", 32),
        integer_cmov_form("$::_cmove", "cmoveq", 64),
        integer_cmov_form("$::_cmovne", "cmovnew", 16),
        integer_cmov_form("$::_cmovne", "cmovnel", 32),
        integer_cmov_form("$::_cmovne", "cmovneq", 64),
        integer_cmov_form("$::_cmova", "cmovaw", 16),
        integer_cmov_form("$::_cmova", "cmoval", 32),
        integer_cmov_form("$::_cmova", "cmovaq", 64),
        integer_cmov_form("$::_cmovae", "cmovaew", 16),
        integer_cmov_form("$::_cmovae", "cmovael", 32),
        integer_cmov_form("$::_cmovae", "cmovaeq", 64),
        integer_cmov_form("$::_cmovb", "cmovbw", 16),
        integer_cmov_form("$::_cmovb", "cmovbl", 32),
        integer_cmov_form("$::_cmovb", "cmovbq", 64),
        integer_cmov_form("$::_cmovbe", "cmovbew", 16),
        integer_cmov_form("$::_cmovbe", "cmovbel", 32),
        integer_cmov_form("$::_cmovbe", "cmovbeq", 64),
        integer_cmov_form("$::_cmovg", "cmovgw", 16),
        integer_cmov_form("$::_cmovg", "cmovgl", 32),
        integer_cmov_form("$::_cmovg", "cmovgq", 64),
        integer_cmov_form("$::_cmovge", "cmovgew", 16),
        integer_cmov_form("$::_cmovge", "cmovgel", 32),
        integer_cmov_form("$::_cmovge", "cmovgeq", 64),
        integer_cmov_form("$::_cmovl", "cmovlw", 16),
        integer_cmov_form("$::_cmovl", "cmovll", 32),
        integer_cmov_form("$::_cmovl", "cmovlq", 64),
        integer_cmov_form("$::_cmovle", "cmovlew", 16),
        integer_cmov_form("$::_cmovle", "cmovlel", 32),
        integer_cmov_form("$::_cmovle", "cmovleq", 64),
        integer_setcc_form("$::_sete", "sete"),
        integer_setcc_form("$::_setne", "setne"),
        integer_setcc_form("$::_seta", "seta"),
        integer_setcc_form("$::_setae", "setae"),
        integer_setcc_form("$::_setb", "setb"),
        integer_setcc_form("$::_setbe", "setbe"),
        integer_setcc_form("$::_setg", "setg"),
        integer_setcc_form("$::_setge", "setge"),
        integer_setcc_form("$::_setl", "setl"),
        integer_setcc_form("$::_setle", "setle"),
        {"$::_clc", "x86-64", "clc", {}, {}, {"flags"}, 0,
         InstructionControlEffect::None},
        {"$::_stc", "x86-64", "stc", {}, {}, {"flags"}, 0,
         InstructionControlEffect::None},
        {"$::_cmc", "x86-64", "cmc", {}, {"flags"}, {"flags"}, 0,
         InstructionControlEffect::None},
        {"$::_nop", "x86-64", "nop", {}, {}, {}, 0,
         InstructionControlEffect::None},
        {"$::_cpuid", "x86-64", "cpuid", {}, {"rax", "rcx"},
         {"rax", "rbx", "rcx", "rdx"}, 0,
         InstructionControlEffect::None},
        {"$::_rdtsc", "x86-64", "rdtsc", {}, {}, {"rax", "rdx"}, 0,
         InstructionControlEffect::None},
        {"$::_syscall", "x86-64", "syscall", {}, {"memory"},
         {"rcx", "r11", "flags", "memory"}, 0,
         InstructionControlEffect::None},
        {"$::_ud2", "x86-64", "ud2", {}, {}, {}, 0,
         InstructionControlEffect::Trap},
        {"$::_int3", "x86-64", "int3", {}, {}, {}, 0,
         InstructionControlEffect::Trap},

        // Typed base+disp32 memory forms.  The source lvalue supplies volatile
        // and atomic qualification; these ordinary forms reject atomic
        // lvalues and describe their memory dependencies explicitly.
        integer_load_form("$::_mov", "movb", 8),
        integer_load_form("$::_mov", "movw", 16),
        integer_load_form("$::_mov", "movl", 32),
        integer_load_form("$::_mov", "movq", 64),
        integer_store_form("$::_mov", "movb", 8),
        integer_store_form("$::_mov", "movw", 16),
        integer_store_form("$::_mov", "movl", 32),
        integer_store_form("$::_mov", "movq", 64),
#define CROSS_X86_MEMORY_BINARY(name, stem, reads_flags)                       \
        integer_register_memory_form(name, stem "b", 8, reads_flags),         \
        integer_register_memory_form(name, stem "w", 16, reads_flags),        \
        integer_register_memory_form(name, stem "l", 32, reads_flags),        \
        integer_register_memory_form(name, stem "q", 64, reads_flags),        \
        integer_memory_source_form(name, stem "b", 8, 8, true, reads_flags),  \
        integer_memory_source_form(name, stem "w", 16, 16, true, reads_flags),\
        integer_memory_source_form(name, stem "l", 32, 32, true, reads_flags),\
        integer_memory_source_form(name, stem "q", 64, 32, true, reads_flags)
        CROSS_X86_MEMORY_BINARY("$::_add", "add", false),
        CROSS_X86_MEMORY_BINARY("$::_sub", "sub", false),
        CROSS_X86_MEMORY_BINARY("$::_xor", "xor", false),
        CROSS_X86_MEMORY_BINARY("$::_and", "and", false),
        CROSS_X86_MEMORY_BINARY("$::_or", "or", false),
        CROSS_X86_MEMORY_BINARY("$::_adc", "adc", true),
        CROSS_X86_MEMORY_BINARY("$::_sbb", "sbb", true),
#undef CROSS_X86_MEMORY_BINARY
#define CROSS_X86_MEMORY_COMPARE(name, stem)                                   \
        integer_compare_register_memory_form(name, stem "b", 8),              \
        integer_compare_register_memory_form(name, stem "w", 16),             \
        integer_compare_register_memory_form(name, stem "l", 32),             \
        integer_compare_register_memory_form(name, stem "q", 64),             \
        integer_compare_memory_source_form(name, stem "b", 8, 8, true),       \
        integer_compare_memory_source_form(name, stem "w", 16, 16, true),     \
        integer_compare_memory_source_form(name, stem "l", 32, 32, true),     \
        integer_compare_memory_source_form(name, stem "q", 64, 32, true)
        CROSS_X86_MEMORY_COMPARE("$::_cmp", "cmp"),
        CROSS_X86_MEMORY_COMPARE("$::_test", "test"),
#undef CROSS_X86_MEMORY_COMPARE
#define CROSS_X86_MEMORY_SHIFT(name, stem)                                     \
        integer_shift_memory_form(name, stem "b", 8),                         \
        integer_shift_memory_form(name, stem "w", 16),                        \
        integer_shift_memory_form(name, stem "l", 32),                        \
        integer_shift_memory_form(name, stem "q", 64)
        CROSS_X86_MEMORY_SHIFT("$::_shl", "shl"),
        CROSS_X86_MEMORY_SHIFT("$::_shr", "shr"),
        CROSS_X86_MEMORY_SHIFT("$::_sar", "sar"),
        CROSS_X86_MEMORY_SHIFT("$::_rol", "rol"),
        CROSS_X86_MEMORY_SHIFT("$::_ror", "ror"),
#undef CROSS_X86_MEMORY_SHIFT
#define CROSS_X86_MEMORY_UNARY(name, stem, flags, preserve)                    \
        integer_unary_memory_form(name, stem "b", 8, flags, preserve),        \
        integer_unary_memory_form(name, stem "w", 16, flags, preserve),       \
        integer_unary_memory_form(name, stem "l", 32, flags, preserve),       \
        integer_unary_memory_form(name, stem "q", 64, flags, preserve)
        CROSS_X86_MEMORY_UNARY("$::_inc", "inc", true, true),
        CROSS_X86_MEMORY_UNARY("$::_dec", "dec", true, true),
        CROSS_X86_MEMORY_UNARY("$::_neg", "neg", true, false),
        CROSS_X86_MEMORY_UNARY("$::_not", "not", false, false),
#undef CROSS_X86_MEMORY_UNARY
#define CROSS_X86_MEMORY_EXCHANGE(name, stem, flags)                           \
        integer_exchange_memory_form(name, stem "b", 8, flags),              \
        integer_exchange_memory_form(name, stem "w", 16, flags),             \
        integer_exchange_memory_form(name, stem "l", 32, flags),             \
        integer_exchange_memory_form(name, stem "q", 64, flags)
        CROSS_X86_MEMORY_EXCHANGE("$::_xchg", "xchg", false),
        CROSS_X86_MEMORY_EXCHANGE("$::_xadd", "xadd", true),
#undef CROSS_X86_MEMORY_EXCHANGE
#define CROSS_X86_CMPXCHG(width, suffix)                                      \
        integer_cmpxchg_form("$::_cmpxchg", "x86-64", "cmpxchg" suffix,    \
                             width, false),                                   \
        integer_cmpxchg_form("$::_cmpxchg", "x86-64", "cmpxchg" suffix,    \
                             width, true),                                    \
        integer_cmpxchg_form("$::_lock_cmpxchg", "x86-64",                 \
                             "lock cmpxchg" suffix, width, true, true),       \
        integer_locked_exchange_form("$::_lock_xadd",                        \
                                     "lock xadd" suffix, width)
        CROSS_X86_CMPXCHG(8, "b"),
        CROSS_X86_CMPXCHG(16, "w"),
        CROSS_X86_CMPXCHG(32, "l"),
        CROSS_X86_CMPXCHG(64, "q"),
#undef CROSS_X86_CMPXCHG
        integer_movbe_load_form(16),
        integer_movbe_load_form(32),
        integer_movbe_load_form(64),
        integer_movbe_store_form(16),
        integer_movbe_store_form(32),
        integer_movbe_store_form(64),
#define CROSS_X86_EXTEND(name, stem, destination, source, suffix)              \
        integer_extend_form(name, stem suffix, destination, source, false),   \
        integer_extend_form(name, stem suffix, destination, source, true)
        CROSS_X86_EXTEND("$::_movzx", "movz", 16, 8, "bw"),
        CROSS_X86_EXTEND("$::_movzx", "movz", 32, 8, "bl"),
        CROSS_X86_EXTEND("$::_movzx", "movz", 64, 8, "bq"),
        CROSS_X86_EXTEND("$::_movzx", "movz", 32, 16, "wl"),
        CROSS_X86_EXTEND("$::_movzx", "movz", 64, 16, "wq"),
        CROSS_X86_EXTEND("$::_movsx", "movs", 16, 8, "bw"),
        CROSS_X86_EXTEND("$::_movsx", "movs", 32, 8, "bl"),
        CROSS_X86_EXTEND("$::_movsx", "movs", 64, 8, "bq"),
        CROSS_X86_EXTEND("$::_movsx", "movs", 32, 16, "wl"),
        CROSS_X86_EXTEND("$::_movsx", "movs", 64, 16, "wq"),
        CROSS_X86_EXTEND("$::_movsx", "movs", 64, 32, "lq"),
#undef CROSS_X86_EXTEND
        integer_lea_form(8),
        integer_lea_form(16),
        integer_lea_form(32),
        integer_lea_form(64),
        integer_lea_form(128),
        integer_lea_form(256),
        integer_lea_form(512),
#define CROSS_X86_SCAN(name, stem, bits, suffix)                              \
        integer_scan_form(name, stem suffix, bits, false),                    \
        integer_scan_form(name, stem suffix, bits, true)
        CROSS_X86_SCAN("$::_bsf", "bsf", 16, "w"),
        CROSS_X86_SCAN("$::_bsf", "bsf", 32, "l"),
        CROSS_X86_SCAN("$::_bsf", "bsf", 64, "q"),
        CROSS_X86_SCAN("$::_bsr", "bsr", 16, "w"),
        CROSS_X86_SCAN("$::_bsr", "bsr", 32, "l"),
        CROSS_X86_SCAN("$::_bsr", "bsr", 64, "q"),
#undef CROSS_X86_SCAN
        integer_accumulator_form("$::_mul", "mulb", 8),
        integer_accumulator_form("$::_mul", "mulw", 16),
        integer_accumulator_form("$::_mul", "mull", 32),
        integer_accumulator_form("$::_mul", "mulq", 64),
        integer_accumulator_form("$::_imul_full", "imulb", 8),
        integer_accumulator_form("$::_imul_full", "imulw", 16),
        integer_accumulator_form("$::_imul_full", "imull", 32),
        integer_accumulator_form("$::_imul_full", "imulq", 64),
        integer_accumulator_form("$::_div", "divb", 8, false, true),
        integer_accumulator_form("$::_div", "divw", 16, false, true),
        integer_accumulator_form("$::_div", "divl", 32, false, true),
        integer_accumulator_form("$::_div", "divq", 64, false, true),
        integer_accumulator_form("$::_idiv", "idivb", 8, true, true),
        integer_accumulator_form("$::_idiv", "idivw", 16, true, true),
        integer_accumulator_form("$::_idiv", "idivl", 32, true, true),
        integer_accumulator_form("$::_idiv", "idivq", 64, true, true),
        {"$::_cwd", "x86-64", "cwd", {}, {"rax"}, {"rdx"}, 0,
         InstructionControlEffect::None},
        {"$::_cdq", "x86-64", "cdq", {}, {"rax"}, {"rdx"}, 0,
         InstructionControlEffect::None},
        {"$::_cqo", "x86-64", "cqo", {}, {"rax"}, {"rdx"}, 0,
         InstructionControlEffect::None},

        // Dense-stack-aware x87 forms. Register forms are constrained to the
        // encodable st0/st(i) pairs; memory pushes and pops carry a distinct
        // ordered-stack effect verified across raw control-flow joins.
        x87_memory_form("$::_fld", "flds", 32,
                        InstructionOperandRole::Input, 1, {"memory"},
                        {"st0", "x87-status"}),
        x87_memory_form("$::_fld", "fldl", 64,
                        InstructionOperandRole::Input, 1, {"memory"},
                        {"st0", "x87-status"}),
        x87_memory_form("$::_fld", "fldt", 80,
                        InstructionOperandRole::Input, 1, {"memory"},
                        {"st0", "x87-status"}),
        x87_memory_form("$::_fild", "filds", 16,
                        InstructionOperandRole::Input, 1, {"memory"},
                        {"st0", "x87-status"}),
        x87_memory_form("$::_fild", "fildl", 32,
                        InstructionOperandRole::Input, 1, {"memory"},
                        {"st0", "x87-status"}),
        x87_memory_form("$::_fild", "fildll", 64,
                        InstructionOperandRole::Input, 1, {"memory"},
                        {"st0", "x87-status"}),
        x87_memory_form("$::_fst", "fsts", 32,
                        InstructionOperandRole::Output, 0, {"st0"},
                        {"memory", "x87-status"}),
        x87_memory_form("$::_fst", "fstl", 64,
                        InstructionOperandRole::Output, 0, {"st0"},
                        {"memory", "x87-status"}),
        x87_memory_form("$::_fstp", "fstps", 32,
                        InstructionOperandRole::Output, -1, {"st0"},
                        {"memory", "x87-status"}),
        x87_memory_form("$::_fstp", "fstpl", 64,
                        InstructionOperandRole::Output, -1, {"st0"},
                        {"memory", "x87-status"}),
        x87_memory_form("$::_fstp", "fstpt", 80,
                        InstructionOperandRole::Output, -1, {"st0"},
                        {"memory", "x87-status"}),
        x87_memory_form("$::_fistp", "fistps", 16,
                        InstructionOperandRole::Output, -1, {"st0"},
                        {"memory", "x87-status"}),
        x87_memory_form("$::_fistp", "fistpl", 32,
                        InstructionOperandRole::Output, -1, {"st0"},
                        {"memory", "x87-status"}),
        x87_memory_form("$::_fistp", "fistpll", 64,
                        InstructionOperandRole::Output, -1, {"st0"},
                        {"memory", "x87-status"}),
#define CROSS_X86_X87_MEMORY_BINARY(name, stem)                               \
        x87_memory_form(name, stem "s", 32, InstructionOperandRole::Input, \
                        0, {"st0", "memory"},                              \
                        {"st0", "x87-status"}),                            \
        x87_memory_form(name, stem "l", 64, InstructionOperandRole::Input, \
                        0, {"st0", "memory"},                              \
                        {"st0", "x87-status"})
        CROSS_X86_X87_MEMORY_BINARY("$::_fadd", "fadd"),
        CROSS_X86_X87_MEMORY_BINARY("$::_fsub", "fsub"),
        CROSS_X86_X87_MEMORY_BINARY("$::_fsubr", "fsubr"),
        CROSS_X86_X87_MEMORY_BINARY("$::_fmul", "fmul"),
        CROSS_X86_X87_MEMORY_BINARY("$::_fdiv", "fdiv"),
        CROSS_X86_X87_MEMORY_BINARY("$::_fdivr", "fdivr"),
#undef CROSS_X86_X87_MEMORY_BINARY
#define CROSS_X86_X87_REGISTER(index)                                        \
        x87_pair_form("$::_fadd", "fadd", "st0", "st" #index),          \
        x87_pair_form("$::_fadd", "fadd", "st" #index, "st0"),          \
        x87_pair_form("$::_fsub", "fsub", "st0", "st" #index),          \
        x87_pair_form("$::_fsub", "fsub", "st" #index, "st0"),          \
        x87_pair_form("$::_fsubr", "fsubr", "st0", "st" #index),        \
        x87_pair_form("$::_fsubr", "fsubr", "st" #index, "st0"),        \
        x87_pair_form("$::_fmul", "fmul", "st0", "st" #index),          \
        x87_pair_form("$::_fmul", "fmul", "st" #index, "st0"),          \
        x87_pair_form("$::_fdiv", "fdiv", "st0", "st" #index),          \
        x87_pair_form("$::_fdiv", "fdiv", "st" #index, "st0"),          \
        x87_pair_form("$::_fdivr", "fdivr", "st0", "st" #index),        \
        x87_pair_form("$::_fdivr", "fdivr", "st" #index, "st0"),        \
        x87_pair_form("$::_faddp", "faddp", "st" #index, "st0", -1),    \
        x87_pair_form("$::_fsubp", "fsubp", "st" #index, "st0", -1),    \
        x87_pair_form("$::_fsubrp", "fsubrp", "st" #index, "st0", -1),  \
        x87_pair_form("$::_fmulp", "fmulp", "st" #index, "st0", -1),    \
        x87_pair_form("$::_fdivp", "fdivp", "st" #index, "st0", -1),    \
        x87_pair_form("$::_fdivrp", "fdivrp", "st" #index, "st0", -1),  \
        x87_pair_form("$::_fcomi", "fcomi", "st0", "st" #index, 0, true), \
        x87_pair_form("$::_fucomi", "fucomi", "st0", "st" #index, 0, true), \
        x87_pair_form("$::_fcomip", "fcomip", "st0", "st" #index, -1, true), \
        x87_pair_form("$::_fucomip", "fucomip", "st0", "st" #index, -1, true), \
        x87_index_form("$::_fxch", "fxch", "st" #index,                   \
                       InstructionOperandRole::InOut, 0, {"st0"}, {"st0"}), \
        x87_index_form("$::_fld", "fld", "st" #index,                    \
                       InstructionOperandRole::Input, 1, {}, {"st0"}),      \
        x87_index_form("$::_fstp", "fstp", "st" #index,                  \
                       InstructionOperandRole::InOut, -1, {"st0"},         \
                       {"x87-status"}),                                     \
        x87_index_form("$::_fcom", "fcom", "st" #index,                  \
                       InstructionOperandRole::Input, 0, {"st0"},          \
                       {"x87-status"}),                                     \
        x87_index_form("$::_fucom", "fucom", "st" #index,                \
                       InstructionOperandRole::Input, 0, {"st0"},          \
                       {"x87-status"})
        CROSS_X86_X87_REGISTER(1),
        CROSS_X86_X87_REGISTER(2),
        CROSS_X86_X87_REGISTER(3),
        CROSS_X86_X87_REGISTER(4),
        CROSS_X86_X87_REGISTER(5),
        CROSS_X86_X87_REGISTER(6),
        CROSS_X86_X87_REGISTER(7),
#undef CROSS_X86_X87_REGISTER
#define CROSS_X86_X87_UNARY(name, mnemonic)                                  \
        x87_nullary_form(name, mnemonic, {"st0"},                           \
                         {"st0", "x87-status"})
        CROSS_X86_X87_UNARY("$::_fabs", "fabs"),
        CROSS_X86_X87_UNARY("$::_fchs", "fchs"),
        CROSS_X86_X87_UNARY("$::_fsqrt", "fsqrt"),
        CROSS_X86_X87_UNARY("$::_frndint", "frndint"),
        CROSS_X86_X87_UNARY("$::_fsin", "fsin"),
        CROSS_X86_X87_UNARY("$::_fcos", "fcos"),
        CROSS_X86_X87_UNARY("$::_f2xm1", "f2xm1"),
#undef CROSS_X86_X87_UNARY
        x87_nullary_form("$::_ftst", "ftst", {"st0"}, {"x87-status"}),
        x87_nullary_form("$::_fxam", "fxam", {"st0"}, {"x87-status"}),
        x87_nullary_form("$::_fyl2x", "fyl2x", {"st0", "st1"},
                         {"st0", "x87-status"}, -1),
        x87_nullary_form("$::_fpatan", "fpatan", {"st0", "st1"},
                         {"st0", "x87-status"}, -1),
        x87_nullary_form("$::_fprem", "fprem", {"st0", "st1"},
                         {"st0", "x87-status"}),
        x87_nullary_form("$::_fprem1", "fprem1", {"st0", "st1"},
                         {"st0", "x87-status"}),
        x87_nullary_form("$::_fscale", "fscale", {"st0", "st1"},
                         {"st0", "x87-status"}),
        x87_nullary_form("$::_fptan", "fptan", {"st0"},
                         {"st0", "st1", "x87-status"}, 1),
        x87_nullary_form("$::_fsincos", "fsincos", {"st0"},
                         {"st0", "st1", "x87-status"}, 1),
        x87_nullary_form("$::_fxtract", "fxtract", {"st0"},
                         {"st0", "st1", "x87-status"}, 1),
        x87_nullary_form("$::_fcompp", "fcompp", {"st0", "st1"},
                         {"x87-status"}, -2),
        x87_nullary_form("$::_fucompp", "fucompp", {"st0", "st1"},
                         {"x87-status"}, -2),
#define CROSS_X86_X87_CONSTANT(name, mnemonic)                               \
        x87_nullary_form(name, mnemonic, {}, {"st0", "x87-status"}, 1)
        CROSS_X86_X87_CONSTANT("$::_fld1", "fld1"),
        CROSS_X86_X87_CONSTANT("$::_fldz", "fldz"),
        CROSS_X86_X87_CONSTANT("$::_fldpi", "fldpi"),
        CROSS_X86_X87_CONSTANT("$::_fldl2e", "fldl2e"),
        CROSS_X86_X87_CONSTANT("$::_fldl2t", "fldl2t"),
        CROSS_X86_X87_CONSTANT("$::_fldlg2", "fldlg2"),
        CROSS_X86_X87_CONSTANT("$::_fldln2", "fldln2"),
#undef CROSS_X86_X87_CONSTANT
        x87_memory_form("$::_fldcw", "fldcw", 16,
                        InstructionOperandRole::Input, 0, {"memory"},
                        {"x87-control"}),
        x87_memory_form("$::_fnstcw", "fnstcw", 16,
                        InstructionOperandRole::Output, 0, {"x87-control"},
                        {"memory"}),
        x87_memory_form("$::_fnstsw", "fnstsw", 16,
                        InstructionOperandRole::Output, 0, {"x87-status"},
                        {"memory"}),
        fixed_integer_register_form("$::_fnstsw", "fnstsw",
                                    InstructionOperandRole::Output, 16, "rax",
                                    {"x87-status"}),
        x87_nullary_form("$::_fnclex", "fnclex", {}, {"x87-status"}),
        x87_nullary_form("$::_fclex", "fclex", {}, {"x87-status"}),
        x87_nullary_form("$::_fwait", "fwait", {"x87-status"}, {}),
        [] {
            auto form = x87_nullary_form("$::_fninit", "fninit", {},
                                         {"x87-control", "x87-status"});
            form.ordered_stack_reset = true;
            return form;
        }(),
        [] {
            auto form = x87_nullary_form("$::_finit", "finit", {},
                                         {"x87-control", "x87-status"});
            form.ordered_stack_reset = true;
            return form;
        }(),
        [] {
            auto form = x87_nullary_form("$::_emms", "emms", {},
                                         {"x87-status"});
            form.ordered_stack_reset = true;
            return form;
        }(),
        {"$::_sahf", "x86-64", "sahf", {}, {"rax"}, {"flags"}, 0,
         InstructionControlEffect::None},
        {"$::_lahf", "x86-64", "lahf", {}, {"flags"}, {"rax"}, 0,
         InstructionControlEffect::None},
#define CROSS_X86_MEMORY_BIT(name, stem, modifies)                             \
        integer_bit_memory_form(name, stem "w", 16, modifies),               \
        integer_bit_memory_form(name, stem "l", 32, modifies),               \
        integer_bit_memory_form(name, stem "q", 64, modifies)
        CROSS_X86_MEMORY_BIT("$::_bt", "bt", false),
        CROSS_X86_MEMORY_BIT("$::_bts", "bts", true),
        CROSS_X86_MEMORY_BIT("$::_btr", "btr", true),
        CROSS_X86_MEMORY_BIT("$::_btc", "btc", true),
#undef CROSS_X86_MEMORY_BIT
#define CROSS_X86_MEMORY_CMOV(name, stem)                                      \
        integer_cmov_memory_form(name, stem "w", 16),                        \
        integer_cmov_memory_form(name, stem "l", 32),                        \
        integer_cmov_memory_form(name, stem "q", 64)
        CROSS_X86_MEMORY_CMOV("$::_cmove", "cmove"),
        CROSS_X86_MEMORY_CMOV("$::_cmovne", "cmovne"),
        CROSS_X86_MEMORY_CMOV("$::_cmova", "cmova"),
        CROSS_X86_MEMORY_CMOV("$::_cmovae", "cmovae"),
        CROSS_X86_MEMORY_CMOV("$::_cmovb", "cmovb"),
        CROSS_X86_MEMORY_CMOV("$::_cmovbe", "cmovbe"),
        CROSS_X86_MEMORY_CMOV("$::_cmovg", "cmovg"),
        CROSS_X86_MEMORY_CMOV("$::_cmovge", "cmovge"),
        CROSS_X86_MEMORY_CMOV("$::_cmovl", "cmovl"),
        CROSS_X86_MEMORY_CMOV("$::_cmovle", "cmovle"),
#undef CROSS_X86_MEMORY_CMOV
        {"$::_push", "x86-64", "pushq",
         {{InstructionOperandRole::Input, true, true, 64, 32, true}},
         {"rsp"}, {"rsp", "memory"}, -8, InstructionControlEffect::None},
        {"$::_pop", "x86-64", "popq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false}},
         {"rsp", "memory"}, {"rsp"}, 8, InstructionControlEffect::None},
        {"$::_vzeroupper", "avx", "vzeroupper", {},
         {}, {"simd-upper"}, 0, InstructionControlEffect::None},

        // Common packed forms.  Unsuffixed source names are width-overloaded;
        // the register views select XMM or YMM encodings and the subtarget
        // feature gate selects the legal family.
        packed_legacy_unary_form("$::_movdqa", "x86-64", "movdqa", 128),
        packed_legacy_unary_form("$::_movdqu", "x86-64", "movdqu", 128),
        packed_load_form("$::_movdqa", "x86-64", "movdqa", 128),
        packed_store_form("$::_movdqa", "x86-64", "movdqa", 128),
        packed_load_form("$::_movdqu", "x86-64", "movdqu", 128),
        packed_store_form("$::_movdqu", "x86-64", "movdqu", 128),
#define CROSS_X86_SSE_FLOAT_BINARY(name, mnemonic)                             \
        packed_legacy_binary_form(name "ps", "x86-64", mnemonic "ps", 128),\
        packed_legacy_binary_form(name "pd", "x86-64", mnemonic "pd", 128)
        CROSS_X86_SSE_FLOAT_BINARY("$::_add", "add"),
        CROSS_X86_SSE_FLOAT_BINARY("$::_sub", "sub"),
        CROSS_X86_SSE_FLOAT_BINARY("$::_mul", "mul"),
        CROSS_X86_SSE_FLOAT_BINARY("$::_div", "div"),
        CROSS_X86_SSE_FLOAT_BINARY("$::_min", "min"),
        CROSS_X86_SSE_FLOAT_BINARY("$::_max", "max"),
#undef CROSS_X86_SSE_FLOAT_BINARY
        packed_legacy_unary_form("$::_sqrtps", "x86-64", "sqrtps", 128),
        packed_legacy_unary_form("$::_sqrtpd", "x86-64", "sqrtpd", 128),
        packed_legacy_binary_form("$::_andps", "x86-64", "andps", 128),
        packed_legacy_binary_form("$::_andpd", "x86-64", "andpd", 128),
        packed_legacy_binary_form("$::_orps", "x86-64", "orps", 128),
        packed_legacy_binary_form("$::_orpd", "x86-64", "orpd", 128),
        packed_legacy_binary_form("$::_xorps", "x86-64", "xorps", 128),
        packed_legacy_binary_form("$::_xorpd", "x86-64", "xorpd", 128),
#define CROSS_X86_SSE2_INTEGER_BINARY(name, mnemonic)                         \
        packed_legacy_binary_form(name, "x86-64", mnemonic, 128)
        CROSS_X86_SSE2_INTEGER_BINARY("$::_paddb", "paddb"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_paddw", "paddw"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_paddd", "paddd"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_paddq", "paddq"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_psubb", "psubb"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_psubw", "psubw"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_psubd", "psubd"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_psubq", "psubq"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_pand", "pand"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_pandn", "pandn"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_por", "por"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_pxor", "pxor"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_pcmpeqb", "pcmpeqb"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_pcmpeqw", "pcmpeqw"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_pcmpeqd", "pcmpeqd"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_pcmpgtb", "pcmpgtb"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_pcmpgtw", "pcmpgtw"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_pcmpgtd", "pcmpgtd"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_pmullw", "pmullw"),
        CROSS_X86_SSE2_INTEGER_BINARY("$::_pmuludq", "pmuludq"),
#undef CROSS_X86_SSE2_INTEGER_BINARY
#define CROSS_X86_SSE2_SHIFT(name, mnemonic)                                  \
        packed_legacy_shift_form(name, "x86-64", mnemonic, 128)
        CROSS_X86_SSE2_SHIFT("$::_psllw", "psllw"),
        CROSS_X86_SSE2_SHIFT("$::_pslld", "pslld"),
        CROSS_X86_SSE2_SHIFT("$::_psllq", "psllq"),
        CROSS_X86_SSE2_SHIFT("$::_psrlw", "psrlw"),
        CROSS_X86_SSE2_SHIFT("$::_psrld", "psrld"),
        CROSS_X86_SSE2_SHIFT("$::_psrlq", "psrlq"),
        CROSS_X86_SSE2_SHIFT("$::_psraw", "psraw"),
        CROSS_X86_SSE2_SHIFT("$::_psrad", "psrad"),
#undef CROSS_X86_SSE2_SHIFT
        packed_legacy_immediate_form("$::_pshufd", "x86-64", "pshufd", 128,
                                     false),
        packed_legacy_immediate_form("$::_pshuflw", "x86-64", "pshuflw", 128,
                                     false),
        packed_legacy_immediate_form("$::_pshufhw", "x86-64", "pshufhw", 128,
                                     false),
        packed_legacy_binary_form("$::_haddps", "sse3", "haddps", 128),
        packed_legacy_binary_form("$::_haddpd", "sse3", "haddpd", 128),
        packed_legacy_binary_form("$::_hsubps", "sse3", "hsubps", 128),
        packed_legacy_binary_form("$::_hsubpd", "sse3", "hsubpd", 128),
        packed_legacy_binary_form("$::_addsubps", "sse3", "addsubps", 128),
        packed_legacy_binary_form("$::_addsubpd", "sse3", "addsubpd", 128),
        packed_legacy_binary_form("$::_pshufb", "ssse3", "pshufb", 128),
        packed_legacy_binary_form("$::_phaddw", "ssse3", "phaddw", 128),
        packed_legacy_binary_form("$::_phaddd", "ssse3", "phaddd", 128),
        packed_legacy_binary_form("$::_phsubw", "ssse3", "phsubw", 128),
        packed_legacy_binary_form("$::_phsubd", "ssse3", "phsubd", 128),
        packed_legacy_binary_form("$::_pmaddubsw", "ssse3", "pmaddubsw", 128),
        packed_legacy_binary_form("$::_pmulhrsw", "ssse3", "pmulhrsw", 128),
        packed_legacy_unary_form("$::_pabsb", "ssse3", "pabsb", 128),
        packed_legacy_unary_form("$::_pabsw", "ssse3", "pabsw", 128),
        packed_legacy_unary_form("$::_pabsd", "ssse3", "pabsd", 128),
        packed_legacy_immediate_form("$::_pblendw", "sse4.1", "pblendw", 128),
        packed_legacy_immediate_form("$::_blendps", "sse4.1", "blendps", 128),
        packed_legacy_immediate_form("$::_blendpd", "sse4.1", "blendpd", 128),
        packed_legacy_binary_form("$::_pmulld", "sse4.1", "pmulld", 128),
        packed_legacy_binary_form("$::_pcmpeqq", "sse4.1", "pcmpeqq", 128),
        packed_legacy_binary_form("$::_pminsb", "sse4.1", "pminsb", 128),
        packed_legacy_binary_form("$::_pminsd", "sse4.1", "pminsd", 128),
        packed_legacy_binary_form("$::_pminuw", "sse4.1", "pminuw", 128),
        packed_legacy_binary_form("$::_pminud", "sse4.1", "pminud", 128),
        packed_legacy_binary_form("$::_pmaxsb", "sse4.1", "pmaxsb", 128),
        packed_legacy_binary_form("$::_pmaxsd", "sse4.1", "pmaxsd", 128),
        packed_legacy_binary_form("$::_pmaxuw", "sse4.1", "pmaxuw", 128),
        packed_legacy_binary_form("$::_pmaxud", "sse4.1", "pmaxud", 128),
        {"$::_ptest", "sse4.1", "ptest",
         {class_register_operand(InstructionOperandRole::Input, 128, "simd"),
          class_register_operand(InstructionOperandRole::Input, 128, "simd")},
         {}, {"flags"}, 0, InstructionControlEffect::None},
        packed_legacy_immediate_form("$::_roundps", "sse4.1", "roundps", 128,
                                     false),
        packed_legacy_immediate_form("$::_roundpd", "sse4.1", "roundpd", 128,
                                     false),
        packed_legacy_binary_form("$::_pcmpgtq", "sse4.2", "pcmpgtq", 128),
        packed_legacy_binary_memory_form("$::_addps", "x86-64", "addps", 128),
        packed_legacy_binary_memory_form("$::_addpd", "x86-64", "addpd", 128),
        packed_legacy_binary_memory_form("$::_paddb", "x86-64", "paddb", 128),
        packed_legacy_binary_memory_form("$::_pxor", "x86-64", "pxor", 128),
#define CROSS_X86_AVX_FLOAT_BINARY(name, mnemonic)                             \
        packed_vex_binary_form(name, "avx", mnemonic, 128),                  \
        packed_vex_binary_form(name, "avx", mnemonic, 256)
        CROSS_X86_AVX_FLOAT_BINARY("$::_vaddps", "vaddps"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vaddpd", "vaddpd"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vsubps", "vsubps"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vsubpd", "vsubpd"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vmulps", "vmulps"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vmulpd", "vmulpd"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vdivps", "vdivps"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vdivpd", "vdivpd"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vminps", "vminps"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vminpd", "vminpd"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vmaxps", "vmaxps"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vmaxpd", "vmaxpd"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vandps", "vandps"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vandpd", "vandpd"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vorps", "vorps"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vorpd", "vorpd"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vxorps", "vxorps"),
        CROSS_X86_AVX_FLOAT_BINARY("$::_vxorpd", "vxorpd"),
#undef CROSS_X86_AVX_FLOAT_BINARY
        packed_vex_unary_form("$::_vsqrtps", "avx", "vsqrtps", 128),
        packed_vex_unary_form("$::_vsqrtps", "avx", "vsqrtps", 256),
        packed_vex_unary_form("$::_vsqrtpd", "avx", "vsqrtpd", 128),
        packed_vex_unary_form("$::_vsqrtpd", "avx", "vsqrtpd", 256),
#define CROSS_X86_AVX2_INTEGER_BINARY(name, mnemonic)                          \
        packed_vex_binary_form(name, "avx2", mnemonic, 128),                 \
        packed_vex_binary_form(name, "avx2", mnemonic, 256)
        CROSS_X86_AVX2_INTEGER_BINARY("$::_vpaddb", "vpaddb"),
        CROSS_X86_AVX2_INTEGER_BINARY("$::_vpaddw", "vpaddw"),
        CROSS_X86_AVX2_INTEGER_BINARY("$::_vpaddd", "vpaddd"),
        CROSS_X86_AVX2_INTEGER_BINARY("$::_vpaddq", "vpaddq"),
        CROSS_X86_AVX2_INTEGER_BINARY("$::_vpsubb", "vpsubb"),
        CROSS_X86_AVX2_INTEGER_BINARY("$::_vpsubw", "vpsubw"),
        CROSS_X86_AVX2_INTEGER_BINARY("$::_vpsubd", "vpsubd"),
        CROSS_X86_AVX2_INTEGER_BINARY("$::_vpsubq", "vpsubq"),
        CROSS_X86_AVX2_INTEGER_BINARY("$::_vpand", "vpand"),
        CROSS_X86_AVX2_INTEGER_BINARY("$::_vpandn", "vpandn"),
        CROSS_X86_AVX2_INTEGER_BINARY("$::_vpor", "vpor"),
        CROSS_X86_AVX2_INTEGER_BINARY("$::_vpxor", "vpxor"),
        CROSS_X86_AVX2_INTEGER_BINARY("$::_vpmulld", "vpmulld"),
        CROSS_X86_AVX2_INTEGER_BINARY("$::_vpshufb", "vpshufb"),
#undef CROSS_X86_AVX2_INTEGER_BINARY
        packed_vex_immediate_form("$::_vpermq", "avx2", "vpermq", 256),
        packed_vex_binary_form("$::_vpermd", "avx2", "vpermd", 256),
        packed_vex_binary_form("$::_vpsllvd", "avx2", "vpsllvd", 128),
        packed_vex_binary_form("$::_vpsllvd", "avx2", "vpsllvd", 256),
        packed_vex_binary_form("$::_vpsrlvd", "avx2", "vpsrlvd", 128),
        packed_vex_binary_form("$::_vpsrlvd", "avx2", "vpsrlvd", 256),
        packed_vex_binary_memory_form("$::_vaddps", "avx", "vaddps", 128),
        packed_vex_binary_memory_form("$::_vaddps", "avx", "vaddps", 256),
        packed_vex_binary_memory_form("$::_vaddpd", "avx", "vaddpd", 128),
        packed_vex_binary_memory_form("$::_vaddpd", "avx", "vaddpd", 256),
        packed_vex_binary_memory_form("$::_vpaddb", "avx2", "vpaddb", 128),
        packed_vex_binary_memory_form("$::_vpaddb", "avx2", "vpaddb", 256),
        packed_vex_binary_memory_form("$::_vpxor", "avx2", "vpxor", 128),
        packed_vex_binary_memory_form("$::_vpxor", "avx2", "vpxor", 256),

        // Complete the common SSE2/SSSE3/SSE4 and AVX/AVX2 packed families.
        // Every ordinary binary family has both a register and typed-memory
        // final source so raw code does not need an artificial load merely to
        // select an addressing mode already present in the ISA.
#define CROSS_X86_LEGACY_NEW_BINARY(name, feature, mnemonic)                  \
        packed_legacy_binary_form(name, feature, mnemonic, 128),             \
        packed_legacy_binary_memory_form(name, feature, mnemonic, 128)
        CROSS_X86_LEGACY_NEW_BINARY("$::_paddsb", "x86-64", "paddsb"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_paddsw", "x86-64", "paddsw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_paddusb", "x86-64", "paddusb"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_paddusw", "x86-64", "paddusw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_psubsb", "x86-64", "psubsb"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_psubsw", "x86-64", "psubsw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_psubusb", "x86-64", "psubusb"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_psubusw", "x86-64", "psubusw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_pavgb", "x86-64", "pavgb"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_pavgw", "x86-64", "pavgw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_pmaxsw", "x86-64", "pmaxsw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_pmaxub", "x86-64", "pmaxub"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_pminsw", "x86-64", "pminsw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_pminub", "x86-64", "pminub"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_pmaddwd", "x86-64", "pmaddwd"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_pmulhw", "x86-64", "pmulhw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_pmulhuw", "x86-64", "pmulhuw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_psadbw", "x86-64", "psadbw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_packsswb", "x86-64", "packsswb"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_packssdw", "x86-64", "packssdw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_packuswb", "x86-64", "packuswb"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_punpckhbw", "x86-64", "punpckhbw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_punpckhwd", "x86-64", "punpckhwd"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_punpckhdq", "x86-64", "punpckhdq"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_punpckhqdq", "x86-64", "punpckhqdq"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_punpcklbw", "x86-64", "punpcklbw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_punpcklwd", "x86-64", "punpcklwd"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_punpckldq", "x86-64", "punpckldq"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_punpcklqdq", "x86-64", "punpcklqdq"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_phaddsw", "ssse3", "phaddsw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_phsubsw", "ssse3", "phsubsw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_psignb", "ssse3", "psignb"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_psignw", "ssse3", "psignw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_psignd", "ssse3", "psignd"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_packusdw", "sse4.1", "packusdw"),
        CROSS_X86_LEGACY_NEW_BINARY("$::_pmuldq", "sse4.1", "pmuldq"),
#undef CROSS_X86_LEGACY_NEW_BINARY

#define CROSS_X86_LEGACY_MEMORY(name, feature, mnemonic)                      \
        packed_legacy_binary_memory_form(name, feature, mnemonic, 128)
        CROSS_X86_LEGACY_MEMORY("$::_subps", "x86-64", "subps"),
        CROSS_X86_LEGACY_MEMORY("$::_subpd", "x86-64", "subpd"),
        CROSS_X86_LEGACY_MEMORY("$::_mulps", "x86-64", "mulps"),
        CROSS_X86_LEGACY_MEMORY("$::_mulpd", "x86-64", "mulpd"),
        CROSS_X86_LEGACY_MEMORY("$::_divps", "x86-64", "divps"),
        CROSS_X86_LEGACY_MEMORY("$::_divpd", "x86-64", "divpd"),
        CROSS_X86_LEGACY_MEMORY("$::_minps", "x86-64", "minps"),
        CROSS_X86_LEGACY_MEMORY("$::_minpd", "x86-64", "minpd"),
        CROSS_X86_LEGACY_MEMORY("$::_maxps", "x86-64", "maxps"),
        CROSS_X86_LEGACY_MEMORY("$::_maxpd", "x86-64", "maxpd"),
        CROSS_X86_LEGACY_MEMORY("$::_andps", "x86-64", "andps"),
        CROSS_X86_LEGACY_MEMORY("$::_andpd", "x86-64", "andpd"),
        CROSS_X86_LEGACY_MEMORY("$::_orps", "x86-64", "orps"),
        CROSS_X86_LEGACY_MEMORY("$::_orpd", "x86-64", "orpd"),
        CROSS_X86_LEGACY_MEMORY("$::_xorps", "x86-64", "xorps"),
        CROSS_X86_LEGACY_MEMORY("$::_xorpd", "x86-64", "xorpd"),
        CROSS_X86_LEGACY_MEMORY("$::_paddw", "x86-64", "paddw"),
        CROSS_X86_LEGACY_MEMORY("$::_paddd", "x86-64", "paddd"),
        CROSS_X86_LEGACY_MEMORY("$::_paddq", "x86-64", "paddq"),
        CROSS_X86_LEGACY_MEMORY("$::_psubb", "x86-64", "psubb"),
        CROSS_X86_LEGACY_MEMORY("$::_psubw", "x86-64", "psubw"),
        CROSS_X86_LEGACY_MEMORY("$::_psubd", "x86-64", "psubd"),
        CROSS_X86_LEGACY_MEMORY("$::_psubq", "x86-64", "psubq"),
        CROSS_X86_LEGACY_MEMORY("$::_pand", "x86-64", "pand"),
        CROSS_X86_LEGACY_MEMORY("$::_pandn", "x86-64", "pandn"),
        CROSS_X86_LEGACY_MEMORY("$::_por", "x86-64", "por"),
        CROSS_X86_LEGACY_MEMORY("$::_pcmpeqb", "x86-64", "pcmpeqb"),
        CROSS_X86_LEGACY_MEMORY("$::_pcmpeqw", "x86-64", "pcmpeqw"),
        CROSS_X86_LEGACY_MEMORY("$::_pcmpeqd", "x86-64", "pcmpeqd"),
        CROSS_X86_LEGACY_MEMORY("$::_pcmpgtb", "x86-64", "pcmpgtb"),
        CROSS_X86_LEGACY_MEMORY("$::_pcmpgtw", "x86-64", "pcmpgtw"),
        CROSS_X86_LEGACY_MEMORY("$::_pcmpgtd", "x86-64", "pcmpgtd"),
        CROSS_X86_LEGACY_MEMORY("$::_pmullw", "x86-64", "pmullw"),
        CROSS_X86_LEGACY_MEMORY("$::_pmuludq", "x86-64", "pmuludq"),
        CROSS_X86_LEGACY_MEMORY("$::_haddps", "sse3", "haddps"),
        CROSS_X86_LEGACY_MEMORY("$::_haddpd", "sse3", "haddpd"),
        CROSS_X86_LEGACY_MEMORY("$::_hsubps", "sse3", "hsubps"),
        CROSS_X86_LEGACY_MEMORY("$::_hsubpd", "sse3", "hsubpd"),
        CROSS_X86_LEGACY_MEMORY("$::_addsubps", "sse3", "addsubps"),
        CROSS_X86_LEGACY_MEMORY("$::_addsubpd", "sse3", "addsubpd"),
        CROSS_X86_LEGACY_MEMORY("$::_pshufb", "ssse3", "pshufb"),
        CROSS_X86_LEGACY_MEMORY("$::_phaddw", "ssse3", "phaddw"),
        CROSS_X86_LEGACY_MEMORY("$::_phaddd", "ssse3", "phaddd"),
        CROSS_X86_LEGACY_MEMORY("$::_phsubw", "ssse3", "phsubw"),
        CROSS_X86_LEGACY_MEMORY("$::_phsubd", "ssse3", "phsubd"),
        CROSS_X86_LEGACY_MEMORY("$::_pmaddubsw", "ssse3", "pmaddubsw"),
        CROSS_X86_LEGACY_MEMORY("$::_pmulhrsw", "ssse3", "pmulhrsw"),
        CROSS_X86_LEGACY_MEMORY("$::_pmulld", "sse4.1", "pmulld"),
        CROSS_X86_LEGACY_MEMORY("$::_pcmpeqq", "sse4.1", "pcmpeqq"),
        CROSS_X86_LEGACY_MEMORY("$::_pminsb", "sse4.1", "pminsb"),
        CROSS_X86_LEGACY_MEMORY("$::_pminsd", "sse4.1", "pminsd"),
        CROSS_X86_LEGACY_MEMORY("$::_pminuw", "sse4.1", "pminuw"),
        CROSS_X86_LEGACY_MEMORY("$::_pminud", "sse4.1", "pminud"),
        CROSS_X86_LEGACY_MEMORY("$::_pmaxsb", "sse4.1", "pmaxsb"),
        CROSS_X86_LEGACY_MEMORY("$::_pmaxsd", "sse4.1", "pmaxsd"),
        CROSS_X86_LEGACY_MEMORY("$::_pmaxuw", "sse4.1", "pmaxuw"),
        CROSS_X86_LEGACY_MEMORY("$::_pmaxud", "sse4.1", "pmaxud"),
        CROSS_X86_LEGACY_MEMORY("$::_pcmpgtq", "sse4.2", "pcmpgtq"),
#undef CROSS_X86_LEGACY_MEMORY
        packed_legacy_shift_form("$::_pslldq", "x86-64", "pslldq", 128),
        packed_legacy_shift_form("$::_psrldq", "x86-64", "psrldq", 128),
        packed_legacy_immediate_form("$::_palignr", "ssse3", "palignr", 128),
        packed_legacy_immediate_memory_form(
            "$::_palignr", "ssse3", "palignr", 128, 128),
        packed_legacy_immediate_form("$::_dpps", "sse4.1", "dpps", 128),
        packed_legacy_immediate_form("$::_dppd", "sse4.1", "dppd", 128),
        packed_legacy_immediate_form("$::_mpsadbw", "sse4.1", "mpsadbw", 128),
        packed_legacy_unary_form("$::_phminposuw", "sse4.1", "phminposuw", 128),
        packed_load_form("$::_phminposuw", "sse4.1", "phminposuw", 128),
        packed_load_form("$::_pabsb", "ssse3", "pabsb", 128),
        packed_load_form("$::_pabsw", "ssse3", "pabsw", 128),
        packed_load_form("$::_pabsd", "ssse3", "pabsd", 128),
        packed_legacy_immediate_memory_form(
            "$::_roundps", "sse4.1", "roundps", 128, 128, false),
        packed_legacy_immediate_memory_form(
            "$::_roundpd", "sse4.1", "roundpd", 128, 128, false),
        packed_load_form("$::_sqrtps", "x86-64", "sqrtps", 128),
        packed_load_form("$::_sqrtpd", "x86-64", "sqrtpd", 128),
        packed_vex_unary_memory_form(
            "$::_vsqrtps", "avx", "vsqrtps", 128, 128),
        packed_vex_unary_memory_form(
            "$::_vsqrtps", "avx", "vsqrtps", 256, 256),
        packed_vex_unary_memory_form(
            "$::_vsqrtpd", "avx", "vsqrtpd", 128, 128),
        packed_vex_unary_memory_form(
            "$::_vsqrtpd", "avx", "vsqrtpd", 256, 256),
        packed_legacy_immediate_memory_form(
            "$::_vpermq", "avx2", "vpermq", 256, 256, false),

        packed_legacy_unary_form("$::_movaps", "x86-64", "movaps", 128),
        packed_load_form("$::_movaps", "x86-64", "movaps", 128),
        packed_store_form("$::_movaps", "x86-64", "movaps", 128),
        packed_legacy_unary_form("$::_movups", "x86-64", "movups", 128),
        packed_load_form("$::_movups", "x86-64", "movups", 128),
        packed_store_form("$::_movups", "x86-64", "movups", 128),
        packed_legacy_unary_form("$::_movapd", "x86-64", "movapd", 128),
        packed_load_form("$::_movapd", "x86-64", "movapd", 128),
        packed_store_form("$::_movapd", "x86-64", "movapd", 128),
        packed_legacy_unary_form("$::_movupd", "x86-64", "movupd", 128),
        packed_load_form("$::_movupd", "x86-64", "movupd", 128),
        packed_store_form("$::_movupd", "x86-64", "movupd", 128),
        packed_to_integer_form("$::_movmskps", "x86-64", "movmskps", 32, 128),
        packed_to_integer_form("$::_movmskpd", "x86-64", "movmskpd", 32, 128),
        packed_to_integer_form("$::_pmovmskb", "x86-64", "pmovmskb", 32, 128),

#define CROSS_X86_AVX_NEW_BINARY(name, feature, mnemonic)                     \
        packed_vex_binary_form(name, feature, mnemonic, 128),                \
        packed_vex_binary_form(name, feature, mnemonic, 256),                \
        packed_vex_binary_memory_form(name, feature, mnemonic, 128),         \
        packed_vex_binary_memory_form(name, feature, mnemonic, 256)
        CROSS_X86_AVX_NEW_BINARY("$::_vhaddps", "avx", "vhaddps"),
        CROSS_X86_AVX_NEW_BINARY("$::_vhaddpd", "avx", "vhaddpd"),
        CROSS_X86_AVX_NEW_BINARY("$::_vhsubps", "avx", "vhsubps"),
        CROSS_X86_AVX_NEW_BINARY("$::_vhsubpd", "avx", "vhsubpd"),
        CROSS_X86_AVX_NEW_BINARY("$::_vaddsubps", "avx", "vaddsubps"),
        CROSS_X86_AVX_NEW_BINARY("$::_vaddsubpd", "avx", "vaddsubpd"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpaddsb", "avx2", "vpaddsb"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpaddsw", "avx2", "vpaddsw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpaddusb", "avx2", "vpaddusb"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpaddusw", "avx2", "vpaddusw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpsubsb", "avx2", "vpsubsb"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpsubsw", "avx2", "vpsubsw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpsubusb", "avx2", "vpsubusb"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpsubusw", "avx2", "vpsubusw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpavgb", "avx2", "vpavgb"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpavgw", "avx2", "vpavgw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpmaxsw", "avx2", "vpmaxsw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpmaxub", "avx2", "vpmaxub"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpminsw", "avx2", "vpminsw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpminub", "avx2", "vpminub"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpmaddwd", "avx2", "vpmaddwd"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpmulhw", "avx2", "vpmulhw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpmulhuw", "avx2", "vpmulhuw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpsadbw", "avx2", "vpsadbw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpacksswb", "avx2", "vpacksswb"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpackssdw", "avx2", "vpackssdw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpackuswb", "avx2", "vpackuswb"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpackusdw", "avx2", "vpackusdw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpunpckhbw", "avx2", "vpunpckhbw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpunpckhwd", "avx2", "vpunpckhwd"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpunpckhdq", "avx2", "vpunpckhdq"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpunpckhqdq", "avx2", "vpunpckhqdq"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpunpcklbw", "avx2", "vpunpcklbw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpunpcklwd", "avx2", "vpunpcklwd"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpunpckldq", "avx2", "vpunpckldq"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpunpcklqdq", "avx2", "vpunpcklqdq"),
        CROSS_X86_AVX_NEW_BINARY("$::_vphaddw", "avx2", "vphaddw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vphaddd", "avx2", "vphaddd"),
        CROSS_X86_AVX_NEW_BINARY("$::_vphaddsw", "avx2", "vphaddsw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vphsubw", "avx2", "vphsubw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vphsubd", "avx2", "vphsubd"),
        CROSS_X86_AVX_NEW_BINARY("$::_vphsubsw", "avx2", "vphsubsw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpsignb", "avx2", "vpsignb"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpsignw", "avx2", "vpsignw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpsignd", "avx2", "vpsignd"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpmaddubsw", "avx2", "vpmaddubsw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpmulhrsw", "avx2", "vpmulhrsw"),
        CROSS_X86_AVX_NEW_BINARY("$::_vpmuldq", "avx2", "vpmuldq"),
#undef CROSS_X86_AVX_NEW_BINARY

#define CROSS_X86_AVX_MEMORY(name, feature, mnemonic)                         \
        packed_vex_binary_memory_form(name, feature, mnemonic, 128),         \
        packed_vex_binary_memory_form(name, feature, mnemonic, 256)
        CROSS_X86_AVX_MEMORY("$::_vsubps", "avx", "vsubps"),
        CROSS_X86_AVX_MEMORY("$::_vsubpd", "avx", "vsubpd"),
        CROSS_X86_AVX_MEMORY("$::_vmulps", "avx", "vmulps"),
        CROSS_X86_AVX_MEMORY("$::_vmulpd", "avx", "vmulpd"),
        CROSS_X86_AVX_MEMORY("$::_vdivps", "avx", "vdivps"),
        CROSS_X86_AVX_MEMORY("$::_vdivpd", "avx", "vdivpd"),
        CROSS_X86_AVX_MEMORY("$::_vminps", "avx", "vminps"),
        CROSS_X86_AVX_MEMORY("$::_vminpd", "avx", "vminpd"),
        CROSS_X86_AVX_MEMORY("$::_vmaxps", "avx", "vmaxps"),
        CROSS_X86_AVX_MEMORY("$::_vmaxpd", "avx", "vmaxpd"),
        CROSS_X86_AVX_MEMORY("$::_vandps", "avx", "vandps"),
        CROSS_X86_AVX_MEMORY("$::_vandpd", "avx", "vandpd"),
        CROSS_X86_AVX_MEMORY("$::_vorps", "avx", "vorps"),
        CROSS_X86_AVX_MEMORY("$::_vorpd", "avx", "vorpd"),
        CROSS_X86_AVX_MEMORY("$::_vxorps", "avx", "vxorps"),
        CROSS_X86_AVX_MEMORY("$::_vxorpd", "avx", "vxorpd"),
        CROSS_X86_AVX_MEMORY("$::_vpaddw", "avx2", "vpaddw"),
        CROSS_X86_AVX_MEMORY("$::_vpaddd", "avx2", "vpaddd"),
        CROSS_X86_AVX_MEMORY("$::_vpaddq", "avx2", "vpaddq"),
        CROSS_X86_AVX_MEMORY("$::_vpsubb", "avx2", "vpsubb"),
        CROSS_X86_AVX_MEMORY("$::_vpsubw", "avx2", "vpsubw"),
        CROSS_X86_AVX_MEMORY("$::_vpsubd", "avx2", "vpsubd"),
        CROSS_X86_AVX_MEMORY("$::_vpsubq", "avx2", "vpsubq"),
        CROSS_X86_AVX_MEMORY("$::_vpand", "avx2", "vpand"),
        CROSS_X86_AVX_MEMORY("$::_vpandn", "avx2", "vpandn"),
        CROSS_X86_AVX_MEMORY("$::_vpor", "avx2", "vpor"),
        CROSS_X86_AVX_MEMORY("$::_vpmulld", "avx2", "vpmulld"),
        CROSS_X86_AVX_MEMORY("$::_vpshufb", "avx2", "vpshufb"),
#undef CROSS_X86_AVX_MEMORY

#define CROSS_X86_VEX_IMMEDIATE_PAIR(name, feature, mnemonic)                 \
        packed_vex_immediate_form(name, feature, mnemonic, 128),             \
        packed_vex_immediate_form(name, feature, mnemonic, 256)
        CROSS_X86_VEX_IMMEDIATE_PAIR("$::_vpsllw", "avx2", "vpsllw"),
        CROSS_X86_VEX_IMMEDIATE_PAIR("$::_vpslld", "avx2", "vpslld"),
        CROSS_X86_VEX_IMMEDIATE_PAIR("$::_vpsllq", "avx2", "vpsllq"),
        CROSS_X86_VEX_IMMEDIATE_PAIR("$::_vpsrlw", "avx2", "vpsrlw"),
        CROSS_X86_VEX_IMMEDIATE_PAIR("$::_vpsrld", "avx2", "vpsrld"),
        CROSS_X86_VEX_IMMEDIATE_PAIR("$::_vpsrlq", "avx2", "vpsrlq"),
        CROSS_X86_VEX_IMMEDIATE_PAIR("$::_vpsraw", "avx2", "vpsraw"),
        CROSS_X86_VEX_IMMEDIATE_PAIR("$::_vpsrad", "avx2", "vpsrad"),
        CROSS_X86_VEX_IMMEDIATE_PAIR("$::_vpslldq", "avx2", "vpslldq"),
        CROSS_X86_VEX_IMMEDIATE_PAIR("$::_vpsrldq", "avx2", "vpsrldq"),
#undef CROSS_X86_VEX_IMMEDIATE_PAIR
        packed_vex_binary_immediate_form("$::_vshufps", "avx", "vshufps", 128),
        packed_vex_binary_immediate_form("$::_vshufps", "avx", "vshufps", 256),
        packed_vex_binary_immediate_form("$::_vshufpd", "avx", "vshufpd", 128),
        packed_vex_binary_immediate_form("$::_vshufpd", "avx", "vshufpd", 256),
        packed_vex_binary_immediate_form("$::_vblendps", "avx", "vblendps", 128),
        packed_vex_binary_immediate_form("$::_vblendps", "avx", "vblendps", 256),
        packed_vex_binary_immediate_form("$::_vblendpd", "avx", "vblendpd", 128),
        packed_vex_binary_immediate_form("$::_vblendpd", "avx", "vblendpd", 256),
        packed_vex_binary_immediate_form("$::_vpalignr", "avx2", "vpalignr", 128),
        packed_vex_binary_immediate_form("$::_vpalignr", "avx2", "vpalignr", 256),
        packed_vex_binary_immediate_form("$::_vpblendw", "avx2", "vpblendw", 128),
        packed_vex_binary_immediate_form("$::_vpblendw", "avx2", "vpblendw", 256),
        packed_vex_binary_immediate_form(
            "$::_vperm2f128", "avx", "vperm2f128", 256),
        packed_vex_binary_immediate_form(
            "$::_vperm2i128", "avx2", "vperm2i128", 256),
        packed_vex_binary_immediate_memory_form(
            "$::_vshufps", "avx", "vshufps", 128),
        packed_vex_binary_immediate_memory_form(
            "$::_vshufps", "avx", "vshufps", 256),
        packed_vex_binary_immediate_memory_form(
            "$::_vshufpd", "avx", "vshufpd", 128),
        packed_vex_binary_immediate_memory_form(
            "$::_vshufpd", "avx", "vshufpd", 256),
        packed_vex_binary_immediate_memory_form(
            "$::_vpalignr", "avx2", "vpalignr", 128),
        packed_vex_binary_immediate_memory_form(
            "$::_vpalignr", "avx2", "vpalignr", 256),

#define CROSS_X86_VMOV(name, mnemonic, bits)                                  \
        packed_vex_unary_form(name, "avx", mnemonic, bits),                 \
        packed_load_form(name, "avx", mnemonic, bits),                      \
        packed_store_form(name, "avx", mnemonic, bits)
        CROSS_X86_VMOV("$::_vmovdqa", "vmovdqa", 128),
        CROSS_X86_VMOV("$::_vmovdqa", "vmovdqa", 256),
        CROSS_X86_VMOV("$::_vmovdqu", "vmovdqu", 128),
        CROSS_X86_VMOV("$::_vmovdqu", "vmovdqu", 256),
        CROSS_X86_VMOV("$::_vmovaps", "vmovaps", 128),
        CROSS_X86_VMOV("$::_vmovaps", "vmovaps", 256),
        CROSS_X86_VMOV("$::_vmovups", "vmovups", 128),
        CROSS_X86_VMOV("$::_vmovups", "vmovups", 256),
        CROSS_X86_VMOV("$::_vmovapd", "vmovapd", 128),
        CROSS_X86_VMOV("$::_vmovapd", "vmovapd", 256),
        CROSS_X86_VMOV("$::_vmovupd", "vmovupd", 128),
        CROSS_X86_VMOV("$::_vmovupd", "vmovupd", 256),
#undef CROSS_X86_VMOV
        packed_to_integer_form("$::_vmovmskps", "avx", "vmovmskps", 32, 128),
        packed_to_integer_form("$::_vmovmskps", "avx", "vmovmskps", 32, 256),
        packed_to_integer_form("$::_vmovmskpd", "avx", "vmovmskpd", 32, 128),
        packed_to_integer_form("$::_vmovmskpd", "avx", "vmovmskpd", 32, 256),
        packed_to_integer_form("$::_vpmovmskb", "avx2", "vpmovmskb", 32, 128),
        packed_to_integer_form("$::_vpmovmskb", "avx2", "vpmovmskb", 32, 256),

        {"$::_popcnt", "popcnt", "popcntq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false}},
         {}, {"flags"}, 0, InstructionControlEffect::None},
        {"$::_lzcnt", "lzcnt", "lzcntq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false}},
         {}, {"flags"}, 0, InstructionControlEffect::None},
        {"$::_tzcnt", "bmi", "tzcntq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false}},
         {}, {"flags"}, 0, InstructionControlEffect::None},
        {"$::_blsi", "bmi", "blsiq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false}},
         {}, {"flags"}, 0, InstructionControlEffect::None},
        {"$::_blsr", "bmi", "blsrq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false}},
         {}, {"flags"}, 0, InstructionControlEffect::None},
        {"$::_blsmsk", "bmi", "blsmskq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false}},
         {}, {"flags"}, 0, InstructionControlEffect::None},
        {"$::_andn", "bmi", "andnq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false}},
         {}, {"flags"}, 0, InstructionControlEffect::None},
        {"$::_shlx", "bmi2", "shlxq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_shrx", "bmi2", "shrxq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_sarx", "bmi2", "sarxq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_pdep", "bmi2", "pdepq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_pext", "bmi2", "pextq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_rorx", "bmi2", "rorxq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false},
          {InstructionOperandRole::Input, false, true, 0, 8, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_crc32", "sse4.2", "crc32q",
         {{InstructionOperandRole::InOut, true, false, 64, 0, false},
          {InstructionOperandRole::Input, true, false, 64, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_aesenc", "aes", "aesenc",
         {{InstructionOperandRole::InOut, true, false, 128, 0, false},
          {InstructionOperandRole::Input, true, false, 128, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_aesenclast", "aes", "aesenclast",
         {{InstructionOperandRole::InOut, true, false, 128, 0, false},
          {InstructionOperandRole::Input, true, false, 128, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_aesdec", "aes", "aesdec",
         {{InstructionOperandRole::InOut, true, false, 128, 0, false},
          {InstructionOperandRole::Input, true, false, 128, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_aesdeclast", "aes", "aesdeclast",
         {{InstructionOperandRole::InOut, true, false, 128, 0, false},
          {InstructionOperandRole::Input, true, false, 128, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_aesimc", "aes", "aesimc",
         {{InstructionOperandRole::Output, true, false, 128, 0, false},
          {InstructionOperandRole::Input, true, false, 128, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_aeskeygenassist", "aes", "aeskeygenassist",
         {{InstructionOperandRole::Output, true, false, 128, 0, false},
          {InstructionOperandRole::Input, true, false, 128, 0, false},
          {InstructionOperandRole::Input, false, true, 0, 8, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_pclmulqdq", "pclmul", "pclmulqdq",
         {{InstructionOperandRole::InOut, true, false, 128, 0, false},
          {InstructionOperandRole::Input, true, false, 128, 0, false},
          {InstructionOperandRole::Input, false, true, 0, 8, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vcvtps2ph128", "f16c", "vcvtps2ph",
         {{InstructionOperandRole::Output, true, false, 128, 0, false},
          {InstructionOperandRole::Input, true, false, 128, 0, false},
          {InstructionOperandRole::Input, false, true, 0, 8, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vcvtps2ph256", "f16c", "vcvtps2ph",
         {{InstructionOperandRole::Output, true, false, 128, 0, false},
          {InstructionOperandRole::Input, true, false, 256, 0, false},
          {InstructionOperandRole::Input, false, true, 0, 8, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vcvtph2ps128", "f16c", "vcvtph2ps",
         {{InstructionOperandRole::Output, true, false, 128, 0, false},
          {InstructionOperandRole::Input, true, false, 128, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vcvtph2ps256", "f16c", "vcvtph2ps",
         {{InstructionOperandRole::Output, true, false, 256, 0, false},
          {InstructionOperandRole::Input, true, false, 128, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vfmadd132ps128", "fma", "vfmadd132ps",
         {{InstructionOperandRole::InOut, true, false, 128, 0, false},
          {InstructionOperandRole::Input, true, false, 128, 0, false},
          {InstructionOperandRole::Input, true, false, 128, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vfmadd132ps256", "fma", "vfmadd132ps",
         {{InstructionOperandRole::InOut, true, false, 256, 0, false},
          {InstructionOperandRole::Input, true, false, 256, 0, false},
          {InstructionOperandRole::Input, true, false, 256, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},

#define CROSS_X86_AVX512_BINARY(name, mnemonic)                               \
        packed_vex_binary_form(name, "avx512f", mnemonic, 512),             \
        packed_vex_binary_memory_form(name, "avx512f", mnemonic, 512)
        CROSS_X86_AVX512_BINARY("$::_vaddps", "vaddps"),
        CROSS_X86_AVX512_BINARY("$::_vaddpd", "vaddpd"),
        CROSS_X86_AVX512_BINARY("$::_vsubps", "vsubps"),
        CROSS_X86_AVX512_BINARY("$::_vsubpd", "vsubpd"),
        CROSS_X86_AVX512_BINARY("$::_vmulps", "vmulps"),
        CROSS_X86_AVX512_BINARY("$::_vmulpd", "vmulpd"),
        CROSS_X86_AVX512_BINARY("$::_vdivps", "vdivps"),
        CROSS_X86_AVX512_BINARY("$::_vdivpd", "vdivpd"),
        CROSS_X86_AVX512_BINARY("$::_vminps", "vminps"),
        CROSS_X86_AVX512_BINARY("$::_vminpd", "vminpd"),
        CROSS_X86_AVX512_BINARY("$::_vmaxps", "vmaxps"),
        CROSS_X86_AVX512_BINARY("$::_vmaxpd", "vmaxpd"),
        CROSS_X86_AVX512_BINARY("$::_vpaddd", "vpaddd"),
        CROSS_X86_AVX512_BINARY("$::_vpaddq", "vpaddq"),
        CROSS_X86_AVX512_BINARY("$::_vpsubd", "vpsubd"),
        CROSS_X86_AVX512_BINARY("$::_vpsubq", "vpsubq"),
        CROSS_X86_AVX512_BINARY("$::_vpmulld", "vpmulld"),
        CROSS_X86_AVX512_BINARY("$::_vpandd", "vpandd"),
        CROSS_X86_AVX512_BINARY("$::_vpandq", "vpandq"),
        CROSS_X86_AVX512_BINARY("$::_vpord", "vpord"),
        CROSS_X86_AVX512_BINARY("$::_vporq", "vporq"),
        CROSS_X86_AVX512_BINARY("$::_vpxord", "vpxord"),
        CROSS_X86_AVX512_BINARY("$::_vpxorq", "vpxorq"),
#undef CROSS_X86_AVX512_BINARY
        packed_vex_unary_form("$::_vsqrtps", "avx512f", "vsqrtps", 512),
        packed_vex_unary_form("$::_vsqrtpd", "avx512f", "vsqrtpd", 512),
        packed_vex_unary_memory_form(
            "$::_vsqrtps", "avx512f", "vsqrtps", 512, 512),
        packed_vex_unary_memory_form(
            "$::_vsqrtpd", "avx512f", "vsqrtpd", 512, 512),
        packed_vex_unary_form(
            "$::_vmovdqa32", "avx512f", "vmovdqa32", 512),
        packed_load_form("$::_vmovdqa32", "avx512f", "vmovdqa32", 512),
        packed_store_form("$::_vmovdqa32", "avx512f", "vmovdqa32", 512),
        packed_vex_unary_form(
            "$::_vmovdqu32", "avx512f", "vmovdqu32", 512),
        packed_load_form("$::_vmovdqu32", "avx512f", "vmovdqu32", 512),
        packed_store_form("$::_vmovdqu32", "avx512f", "vmovdqu32", 512),

        packed_evex_masked_binary_form(
            "$::_vaddps_mask", "avx512f", "vaddps", 512, false),
        packed_evex_masked_binary_memory_form(
            "$::_vaddps_mask", "avx512f", "vaddps", 512, 512, false),
        packed_evex_masked_binary_form(
            "$::_vaddps_maskz", "avx512f", "vaddps", 512, true),
        packed_evex_masked_binary_memory_form(
            "$::_vaddps_maskz", "avx512f", "vaddps", 512, 512, true),
        packed_evex_masked_binary_memory_form(
            "$::_vaddps_broadcast_maskz", "avx512f", "vaddps", 512,
            32, true, 16),
        packed_evex_masked_binary_form(
            "$::_vaddps_rn_sae_maskz", "avx512f", "vaddps", 512, true,
            "{rn-sae}"),
        packed_evex_masked_binary_form(
            "$::_vpaddd_mask", "avx512f", "vpaddd", 512, false),
        packed_evex_masked_binary_memory_form(
            "$::_vpaddd_mask", "avx512f", "vpaddd", 512, 512, false),
        packed_evex_masked_binary_form(
            "$::_vpaddd_maskz", "avx512f", "vpaddd", 512, true),
        packed_evex_masked_binary_memory_form(
            "$::_vpaddd_maskz", "avx512f", "vpaddd", 512, 512, true),
        packed_evex_compare_form(
            "$::_vpcmpd512", "avx512f", "vpcmpd", 512),
        packed_evex_compare_form(
            "$::_vpcmpud512", "avx512f", "vpcmpud", 512),
        packed_evex_compare_form(
            "$::_vcmpps512", "avx512f", "vcmpps", 512),
        packed_evex_compare_form(
            "$::_vcmppd512", "avx512f", "vcmppd", 512),

        {"$::_vpmullq512", "avx512dq", "vpmullq",
         {{InstructionOperandRole::Output, true, false, 512, 0, false},
          {InstructionOperandRole::Input, true, false, 512, 0, false},
          {InstructionOperandRole::Input, true, false, 512, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vpmullq256", "avx512dq", "vpmullq",
         {{InstructionOperandRole::Output, true, false, 256, 0, false},
          {InstructionOperandRole::Input, true, false, 256, 0, false},
          {InstructionOperandRole::Input, true, false, 256, 0, false}},
         {}, {}, 0, InstructionControlEffect::None, {"avx512vl"}},
        {"$::_vpconflictd512", "avx512cd", "vpconflictd",
         {{InstructionOperandRole::Output, true, false, 512, 0, false},
          {InstructionOperandRole::Input, true, false, 512, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vpconflictd256", "avx512cd", "vpconflictd",
         {{InstructionOperandRole::Output, true, false, 256, 0, false},
          {InstructionOperandRole::Input, true, false, 256, 0, false}},
         {}, {}, 0, InstructionControlEffect::None, {"avx512vl"}},
        {"$::_vpaddb512", "avx512bw", "vpaddb",
         {{InstructionOperandRole::Output, true, false, 512, 0, false},
          {InstructionOperandRole::Input, true, false, 512, 0, false},
          {InstructionOperandRole::Input, true, false, 512, 0, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vpaddb256_evex", "avx512bw", "vpaddb",
         {{InstructionOperandRole::Output, true, false, 256, 0, false},
          {InstructionOperandRole::Input, true, false, 256, 0, false},
          {InstructionOperandRole::Input, true, false, 256, 0, false}},
         {}, {}, 0, InstructionControlEffect::None, {"avx512vl"}},
        {"$::_kandw", "avx512f", "kandw",
         {{InstructionOperandRole::Output, true, false, 64, 0, false,
           false, false, "mask"},
          {InstructionOperandRole::Input, true, false, 64, 0, false,
           false, false, "mask"},
          {InstructionOperandRole::Input, true, false, 64, 0, false,
           false, false, "mask"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_korw", "avx512f", "korw",
         {{InstructionOperandRole::Output, true, false, 64, 0, false,
           false, false, "mask"},
          {InstructionOperandRole::Input, true, false, 64, 0, false,
           false, false, "mask"},
          {InstructionOperandRole::Input, true, false, 64, 0, false,
           false, false, "mask"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_kxorw", "avx512f", "kxorw",
         {{InstructionOperandRole::Output, true, false, 64, 0, false,
           false, false, "mask"},
          {InstructionOperandRole::Input, true, false, 64, 0, false,
           false, false, "mask"},
          {InstructionOperandRole::Input, true, false, 64, 0, false,
           false, false, "mask"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_knotw", "avx512f", "knotw",
         {{InstructionOperandRole::Output, true, false, 64, 0, false,
           false, false, "mask"},
          {InstructionOperandRole::Input, true, false, 64, 0, false,
           false, false, "mask"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_kortestw", "avx512f", "kortestw",
         {{InstructionOperandRole::Input, true, false, 64, 0, false,
           false, false, "mask"},
          {InstructionOperandRole::Input, true, false, 64, 0, false,
           false, false, "mask"}},
         {}, {"flags"}, 0, InstructionControlEffect::None},
        {"$::_kmovw_to_mask", "avx512f", "kmovw",
         {{InstructionOperandRole::Output, true, false, 64, 0, false,
           false, false, "mask"},
          {InstructionOperandRole::Input, true, false, 32, 0, false,
           false, false, "integer"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_kmovw_from_mask", "avx512f", "kmovw",
         {{InstructionOperandRole::Output, true, false, 32, 0, false,
           false, false, "integer"},
          {InstructionOperandRole::Input, true, false, 64, 0, false,
           false, false, "mask"}},
         {}, {}, 0, InstructionControlEffect::None},
        mask_binary_form("$::_kandnw", "avx512f", "kandnw"),
        mask_binary_form("$::_kxnorw", "avx512f", "kxnorw"),
        mask_binary_form("$::_kaddw", "avx512dq", "kaddw"),
        mask_binary_form("$::_kunpckbw", "avx512f", "kunpckbw"),
        mask_immediate_form("$::_kshiftlw", "avx512f", "kshiftlw"),
        mask_immediate_form("$::_kshiftrw", "avx512f", "kshiftrw"),
        {"$::_ktestw", "avx512f", "ktestw",
         {class_register_operand(InstructionOperandRole::Input, 64, "mask"),
          class_register_operand(InstructionOperandRole::Input, 64, "mask")},
         {}, {"flags"}, 0, InstructionControlEffect::None},
        {"$::_adcx", "adx", "adcxq",
         {{InstructionOperandRole::InOut, true, false, 64, 0, false,
           false, false, "integer"},
          {InstructionOperandRole::Input, true, false, 64, 0, false,
           false, false, "integer"}},
         {"flags"}, {"flags"}, 0, InstructionControlEffect::None},
        {"$::_adox", "adx", "adoxq",
         {{InstructionOperandRole::InOut, true, false, 64, 0, false,
           false, false, "integer"},
          {InstructionOperandRole::Input, true, false, 64, 0, false,
           false, false, "integer"}},
         {"flags"}, {"flags"}, 0, InstructionControlEffect::None},
        {"$::_rdrand", "rdrnd", "rdrandq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false,
           false, false, "integer"}},
         {}, {"flags"}, 0, InstructionControlEffect::None},
        {"$::_rdseed", "rdseed", "rdseedq",
         {{InstructionOperandRole::Output, true, false, 64, 0, false,
           false, false, "integer"}},
         {}, {"flags"}, 0, InstructionControlEffect::None},
        {"$::_sha1rnds4", "sha", "sha1rnds4",
         {{InstructionOperandRole::InOut, true, false, 128, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 128, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, false, true, 0, 8, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_sha1msg1", "sha", "sha1msg1",
         {{InstructionOperandRole::InOut, true, false, 128, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 128, 0, false,
           false, false, "simd"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_sha1msg2", "sha", "sha1msg2",
         {{InstructionOperandRole::InOut, true, false, 128, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 128, 0, false,
           false, false, "simd"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_sha256msg1", "sha", "sha256msg1",
         {{InstructionOperandRole::InOut, true, false, 128, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 128, 0, false,
           false, false, "simd"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_sha256msg2", "sha", "sha256msg2",
         {{InstructionOperandRole::InOut, true, false, 128, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 128, 0, false,
           false, false, "simd"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_gf2p8mulb128", "gfni", "gf2p8mulb",
         {{InstructionOperandRole::InOut, true, false, 128, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 128, 0, false,
           false, false, "simd"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vpclmulqdq128", "vpclmulqdq", "vpclmulqdq",
         {{InstructionOperandRole::Output, true, false, 128, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 128, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 128, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, false, true, 0, 8, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vpclmulqdq256", "vpclmulqdq", "vpclmulqdq",
         {{InstructionOperandRole::Output, true, false, 256, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 256, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 256, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, false, true, 0, 8, false}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vaesenc128", "vaes", "vaesenc",
         {{InstructionOperandRole::Output, true, false, 128, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 128, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 128, 0, false,
           false, false, "simd"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vaesenc256", "vaes", "vaesenc",
         {{InstructionOperandRole::Output, true, false, 256, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 256, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 256, 0, false,
           false, false, "simd"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vpdpbusd512", "avx512vnni", "vpdpbusd",
         {{InstructionOperandRole::InOut, true, false, 512, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 512, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 512, 0, false,
           false, false, "simd"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vpermb512", "avx512vbmi", "vpermb",
         {{InstructionOperandRole::Output, true, false, 512, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 512, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 512, 0, false,
           false, false, "simd"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vpshldvd512", "avx512vbmi2", "vpshldvd",
         {{InstructionOperandRole::Output, true, false, 512, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 512, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 512, 0, false,
           false, false, "simd"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vpopcntb512", "avx512bitalg", "vpopcntb",
         {{InstructionOperandRole::Output, true, false, 512, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 512, 0, false,
           false, false, "simd"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_vpopcntq512", "avx512vpopcntdq", "vpopcntq",
         {{InstructionOperandRole::Output, true, false, 512, 0, false,
           false, false, "simd"},
          {InstructionOperandRole::Input, true, false, 512, 0, false,
           false, false, "simd"}},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_serialize", "serialize", "serialize", {},
         {"memory"}, {"memory"}, 0, InstructionControlEffect::None},
        {"$::_rdtscp", "rdtscp", "rdtscp", {},
         {"memory"}, {"rax", "rdx", "rcx"}, 0,
         InstructionControlEffect::None},
        {"$::_xgetbv", "xsave", "xgetbv", {},
         {"rcx"}, {"rax", "rdx"}, 0, InstructionControlEffect::None},
        {"$::_xsetbv", "xsave", "xsetbv", {},
         {"rax", "rcx", "rdx", "memory"}, {"memory"}, 0,
         InstructionControlEffect::None},
        {"$::_rdfsbase", "fsgsbase", "rdfsbaseq",
         {integer_register_operand(InstructionOperandRole::Output, 64)},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_rdgsbase", "fsgsbase", "rdgsbaseq",
         {integer_register_operand(InstructionOperandRole::Output, 64)},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_wrfsbase", "fsgsbase", "wrfsbaseq",
         {integer_register_operand(InstructionOperandRole::Input, 64)},
         {}, {"memory"}, 0, InstructionControlEffect::None},
        {"$::_wrgsbase", "fsgsbase", "wrgsbaseq",
         {integer_register_operand(InstructionOperandRole::Input, 64)},
         {}, {"memory"}, 0, InstructionControlEffect::None},
        {"$::_rdpid", "rdpid", "rdpid",
         {integer_register_operand(InstructionOperandRole::Output, 64)},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_rdpkru", "pku", "rdpkru", {},
         {"rcx"}, {"rax", "rdx"}, 0, InstructionControlEffect::None},
        {"$::_wrpkru", "pku", "wrpkru", {},
         {"rax", "rcx", "rdx", "memory"}, {"memory"}, 0,
         InstructionControlEffect::None},

        system_memory_form("$::_prefetchnta", "x86-64", "prefetchnta",
                           InstructionOperandRole::Input, false),
        system_memory_form("$::_prefetcht0", "x86-64", "prefetcht0",
                           InstructionOperandRole::Input, false),
        system_memory_form("$::_prefetcht1", "x86-64", "prefetcht1",
                           InstructionOperandRole::Input, false),
        system_memory_form("$::_prefetcht2", "x86-64", "prefetcht2",
                           InstructionOperandRole::Input, false),
        system_memory_form("$::_prefetchw", "prefetchw", "prefetchw",
                           InstructionOperandRole::Input, false),
        system_memory_form("$::_clflush", "x86-64", "clflush",
                           InstructionOperandRole::Input, true),
        system_memory_form("$::_clflushopt", "clflushopt", "clflushopt",
                           InstructionOperandRole::Input, true),
        system_memory_form("$::_clwb", "clwb", "clwb",
                           InstructionOperandRole::Input, true),
        system_memory_form("$::_cldemote", "cldemote", "cldemote",
                           InstructionOperandRole::Input, true),
        system_memory_form("$::_fxsave", "x86-64", "fxsave64",
                           InstructionOperandRole::Output, true),
        system_memory_form("$::_fxrstor", "x86-64", "fxrstor64",
                           InstructionOperandRole::Input, false),
        xstate_memory_form("$::_xsave", "xsave", "xsave64",
                           InstructionOperandRole::Output, true),
        xstate_memory_form("$::_xrstor", "xsave", "xrstor64",
                           InstructionOperandRole::Input, false),
        xstate_memory_form("$::_xsaveopt", "xsaveopt", "xsaveopt64",
                           InstructionOperandRole::Output, true),
        xstate_memory_form("$::_xsavec", "xsavec", "xsavec64",
                           InstructionOperandRole::Output, true),
        xstate_memory_form("$::_xsaves", "xsaves", "xsaves64",
                           InstructionOperandRole::Output, true),
        xstate_memory_form("$::_xrstors", "xsaves", "xrstors64",
                           InstructionOperandRole::Input, false),
        {"$::_stmxcsr", "x86-64", "stmxcsr",
         {integer_memory_operand(InstructionOperandRole::Output, 32)},
         {}, {"memory"}, 0, InstructionControlEffect::None},
        {"$::_ldmxcsr", "x86-64", "ldmxcsr",
         {integer_memory_operand(InstructionOperandRole::Input, 32)},
         {"memory"}, {}, 0, InstructionControlEffect::None},

        {"$::_cmpxchg8b", "x86-64", "cmpxchg8b",
         {integer_memory_operand(InstructionOperandRole::InOut, 64)},
         {"rax", "rdx", "rbx", "rcx", "memory"},
         {"rax", "rdx", "flags", "memory"}, 0,
         InstructionControlEffect::None},
        {"$::_lock_cmpxchg8b", "x86-64", "lock cmpxchg8b",
         {integer_memory_operand(InstructionOperandRole::InOut, 64, true)},
         {"rax", "rdx", "rbx", "rcx", "memory"},
         {"rax", "rdx", "flags", "memory"}, 0,
         InstructionControlEffect::None},
        {"$::_cmpxchg16b", "cx16", "cmpxchg16b",
         {integer_memory_operand(InstructionOperandRole::InOut, 128)},
         {"rax", "rdx", "rbx", "rcx", "memory"},
         {"rax", "rdx", "flags", "memory"}, 0,
         InstructionControlEffect::None},
        {"$::_lock_cmpxchg16b", "cx16", "lock cmpxchg16b",
         {integer_memory_operand(InstructionOperandRole::InOut, 128, true)},
         {"rax", "rdx", "rbx", "rcx", "memory"},
         {"rax", "rdx", "flags", "memory"}, 0,
         InstructionControlEffect::None},

        {"$::_monitor", "monitor", "monitor", {},
         {"rax", "rcx", "rdx", "memory"}, {"memory"}, 0,
         InstructionControlEffect::None},
        {"$::_mwait", "monitor", "mwait", {},
         {"rax", "rcx", "memory"}, {"memory"}, 0,
         InstructionControlEffect::None},
        {"$::_umonitor", "waitpkg", "umonitor",
         {integer_register_operand(InstructionOperandRole::Input, 64)},
         {"memory"}, {"memory"}, 0, InstructionControlEffect::None},
        {"$::_umwait", "waitpkg", "umwait",
         {integer_register_operand(InstructionOperandRole::Input, 32)},
         {"rax", "rdx", "memory"}, {"flags", "memory"}, 0,
         InstructionControlEffect::None},
        {"$::_tpause", "waitpkg", "tpause",
         {integer_register_operand(InstructionOperandRole::Input, 32)},
         {"rax", "rdx", "memory"}, {"flags", "memory"}, 0,
         InstructionControlEffect::None},
        {"$::_invpcid", "invpcid", "invpcid",
         {integer_register_operand(InstructionOperandRole::Input, 64),
          integer_memory_operand(InstructionOperandRole::Input, 128)},
         {"memory"}, {"memory"}, 0, InstructionControlEffect::None},
        {"$::_movdiri", "movdiri", "movdiri",
         {integer_memory_operand(InstructionOperandRole::Output, 32),
          integer_register_operand(InstructionOperandRole::Input, 32)},
         {}, {"memory"}, 0, InstructionControlEffect::None},
        {"$::_movdiri", "movdiri", "movdiri",
         {integer_memory_operand(InstructionOperandRole::Output, 64),
          integer_register_operand(InstructionOperandRole::Input, 64)},
         {}, {"memory"}, 0, InstructionControlEffect::None},
        {"$::_movdir64b", "movdir64b", "movdir64b",
         {integer_register_operand(InstructionOperandRole::Input, 64),
          address_memory_operand(InstructionOperandRole::Input)},
         {"memory"}, {"memory"}, 0, InstructionControlEffect::None},
        {"$::_enqcmd", "enqcmd", "enqcmd",
         {integer_register_operand(InstructionOperandRole::Input, 64),
          address_memory_operand(InstructionOperandRole::Input)},
         {"memory"}, {"flags", "memory"}, 0,
         InstructionControlEffect::None},
        {"$::_enqcmds", "enqcmd", "enqcmds",
         {integer_register_operand(InstructionOperandRole::Input, 64),
          address_memory_operand(InstructionOperandRole::Input)},
         {"memory"}, {"flags", "memory"}, 0,
         InstructionControlEffect::None},
        {"$::_wbnoinvd", "wbnoinvd", "wbnoinvd", {},
         {"memory"}, {"memory"}, 0, InstructionControlEffect::None},
        {"$::_pause", "base", "pause", {},
         {}, {}, 0, InstructionControlEffect::None},
        {"$::_lfence", "base", "lfence", {},
         {"memory"}, {"memory"}, 0, InstructionControlEffect::None},
        {"$::_sfence", "base", "sfence", {},
         {"memory"}, {"memory"}, 0, InstructionControlEffect::None},
        {"$::_mfence", "base", "mfence", {},
         {"memory"}, {"memory"}, 0, InstructionControlEffect::None},
        {"$::_ret", "x86-64", "retq", {},
         {"rsp", "memory"}, {"rsp"}, 0, InstructionControlEffect::RawReturn},
        {"$::_jmp", "x86-64", "jmp",
         {{InstructionOperandRole::Input, false, false, 0, 0, false, true}},
         {}, {}, 0, InstructionControlEffect::UnconditionalBranch},
        {"$::_jmp_indirect", "x86-64", "jmp",
         {{InstructionOperandRole::Input, true, false, 64, 0, false,
           false, false, "integer", true}},
         {}, {}, 0, InstructionControlEffect::UnconditionalBranch},
        {"$::_je", "x86-64", "je",
         {{InstructionOperandRole::Input, false, false, 0, 0, false, true}},
         {"flags"}, {}, 0, InstructionControlEffect::ConditionalBranch},
        {"$::_jne", "x86-64", "jne",
         {{InstructionOperandRole::Input, false, false, 0, 0, false, true}},
         {"flags"}, {}, 0, InstructionControlEffect::ConditionalBranch},
        {"$::_ja", "x86-64", "ja",
         {{InstructionOperandRole::Input, false, false, 0, 0, false, true}},
         {"flags"}, {}, 0, InstructionControlEffect::ConditionalBranch},
        {"$::_jae", "x86-64", "jae",
         {{InstructionOperandRole::Input, false, false, 0, 0, false, true}},
         {"flags"}, {}, 0, InstructionControlEffect::ConditionalBranch},
        {"$::_jb", "x86-64", "jb",
         {{InstructionOperandRole::Input, false, false, 0, 0, false, true}},
         {"flags"}, {}, 0, InstructionControlEffect::ConditionalBranch},
        {"$::_jbe", "x86-64", "jbe",
         {{InstructionOperandRole::Input, false, false, 0, 0, false, true}},
         {"flags"}, {}, 0, InstructionControlEffect::ConditionalBranch},
        {"$::_jg", "x86-64", "jg",
         {{InstructionOperandRole::Input, false, false, 0, 0, false, true}},
         {"flags"}, {}, 0, InstructionControlEffect::ConditionalBranch},
        {"$::_jge", "x86-64", "jge",
         {{InstructionOperandRole::Input, false, false, 0, 0, false, true}},
         {"flags"}, {}, 0, InstructionControlEffect::ConditionalBranch},
        {"$::_jl", "x86-64", "jl",
         {{InstructionOperandRole::Input, false, false, 0, 0, false, true}},
         {"flags"}, {}, 0, InstructionControlEffect::ConditionalBranch},
        {"$::_jle", "x86-64", "jle",
         {{InstructionOperandRole::Input, false, false, 0, 0, false, true}},
         {"flags"}, {}, 0, InstructionControlEffect::ConditionalBranch},
    },
    {{8, "base"}, {16, "base"}, {32, "base"}, {64, "base"}},
    {{128, {}, {}}, {256, "avx2", "avx"},
     {512, "avx512f", "avx512f"}},
    {
        {"m.arch", {}, OptionValueKind::Text, std::string("generic"), {},
         0, 0, OptionCategory::Target, false,
         OptionImplementation::Implemented,
         "instruction-compatible x86-64 CPU"},
        {"m.tune", {}, OptionValueKind::Text, std::string("generic"), {},
         0, 0, OptionCategory::Target, true,
         OptionImplementation::Implemented,
         "x86-64 scheduling and cost CPU"},
        {"m.risc-cisc-balance", {}, OptionValueKind::Unsigned,
         std::uint64_t{50}, {}, 0, 100, OptionCategory::Target, true,
         OptionImplementation::Implemented,
         "continuous MIR cost position: 0 is RISC-like, 100 is CISC-like"},
        {"m.cmodel", {}, OptionValueKind::Enumeration,
         std::string("small"), {"small", "kernel", "medium", "large"},
         0, 0, OptionCategory::Target, false,
         OptionImplementation::Implemented,
         "x86-64 code and data addressability model"},
        {"m.sse3", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable SSE3 instruction forms"},
        {"m.ssse3", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable Supplemental SSE3 instruction forms"},
        {"m.sse4.1", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable SSE4.1 instruction forms"},
        {"m.sse4.2", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable SSE4.2 instruction forms"},
        {"m.popcnt", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable population-count instructions"},
        {"m.aes", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable AES instructions"},
        {"m.pclmul", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable carry-less multiplication instructions"},
        {"m.avx", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable AVX register and instruction forms"},
        {"m.f16c", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable half-precision conversion instructions"},
        {"m.fma", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable fused multiply-add selection under contraction policy"},
        {"m.avx2", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable AVX2 integer-vector instruction forms"},
        {"m.bmi", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable BMI bit-manipulation instructions"},
        {"m.bmi2", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable BMI2 flag-preserving shifts and bit manipulation"},
        {"m.lzcnt", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable leading-zero count instructions"},
        {"m.movbe", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable byte-swapping move instructions"},
        {"m.adx", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable dual-carry-chain integer instructions"},
        {"m.rdrnd", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable hardware random-number instructions"},
        {"m.rdseed", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable hardware seed-generation instructions"},
        {"m.sha", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable SHA-1 and SHA-256 instructions"},
        {"m.gfni", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable Galois-field arithmetic instructions"},
        {"m.vpclmulqdq", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable vector carry-less multiplication instructions"},
        {"m.vaes", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable vector AES instructions"},
        {"m.serialize", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable the SERIALIZE instruction"},
        {"m.cx16", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable 16-byte compare-and-exchange"},
        {"m.rdtscp", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable ordered time-stamp counter reads"},
        {"m.xsave", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable extended-state save/restore and XGETBV/XSETBV"},
        {"m.xsaveopt", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable optimized extended-state save"},
        {"m.xsavec", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable compacted extended-state save"},
        {"m.xsaves", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable supervisor extended-state save/restore"},
        {"m.fsgsbase", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable direct FS/GS base access"},
        {"m.rdpid", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable processor-ID reads"},
        {"m.pku", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable protection-key register access"},
        {"m.clflushopt", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable optimized cache-line flush"},
        {"m.clwb", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable cache-line write-back"},
        {"m.prefetchw", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable write-intent prefetch"},
        {"m.monitor", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable MONITOR/MWAIT"},
        {"m.waitpkg", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable user-mode monitor/wait instructions"},
        {"m.invpcid", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable process-context invalidation"},
        {"m.cldemote", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable cache-line demotion"},
        {"m.movdiri", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable direct integer stores"},
        {"m.movdir64b", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable direct 64-byte stores"},
        {"m.enqcmd", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable enqueue-command instructions"},
        {"m.wbnoinvd", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable cache write-back without invalidation"},
        {"m.avx512f", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable AVX-512 Foundation register and instruction forms"},
        {"m.avx512dq", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable AVX-512 doubleword/quadword instruction forms"},
        {"m.avx512cd", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable AVX-512 conflict-detection instruction forms"},
        {"m.avx512bw", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable AVX-512 byte/word instruction forms"},
        {"m.avx512vl", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable AVX-512 128-/256-bit vector-length forms"},
        {"m.avx512vnni", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable AVX-512 neural-network instructions"},
        {"m.avx512vbmi", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable AVX-512 byte permutation instructions"},
        {"m.avx512vbmi2", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable second-generation AVX-512 byte manipulation instructions"},
        {"m.avx512bitalg", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable AVX-512 bit-algorithm instructions"},
        {"m.avx512vpopcntdq", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "enable AVX-512 dword/qword population counts"},
        {"m.red-zone", {}, OptionValueKind::Boolean, false, {}, 0, 0,
         OptionCategory::Target, false, OptionImplementation::Implemented,
         "allow eligible leaf frames to use the 128-byte x86-64 red zone"},
        {"m.prefer-vector-width", {}, OptionValueKind::Enumeration,
         std::string("none"), {"none", "128", "256", "512"}, 0, 0,
         OptionCategory::Target, true, OptionImplementation::Implemented,
         "preferred maximum width for internal vector operations"},
    },
    &subtargets,
    {integer_constant_materialization_cost},
};

} // namespace

const TargetInfo& x86_64_target() {
    return target;
}

} // namespace cross
