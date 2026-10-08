// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/mir_transform.hpp"

#include "middle/mir_analysis.hpp"

#include <algorithm>
#include <cassert>
#include <limits>
#include <numeric>
#include <optional>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cross::mir {
namespace {

unsigned scalar_integer_bits(const hir::Module& module, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind != hir::Type::Kind::Builtin)
        return 0;
    switch (type.builtin) {
    case BuiltinType::Bool:
    case BuiltinType::I8:
    case BuiltinType::U8:
        return 8;
    case BuiltinType::I16:
    case BuiltinType::U16:
        return 16;
    case BuiltinType::I32:
    case BuiltinType::U32:
        return 32;
    case BuiltinType::I64:
    case BuiltinType::U64:
        return 64;
    case BuiltinType::Iptr:
    case BuiltinType::Uptr:
        return module.address_bits;
    case BuiltinType::I128:
    case BuiltinType::U128:
        return 128;
    case BuiltinType::F32:
    case BuiltinType::F64:
    case BuiltinType::F80:
    case BuiltinType::F128:
    case BuiltinType::Fptr:
    case BuiltinType::Label:
    case BuiltinType::Void:
        return 0;
    }
    return 0;
}

void replace_value_uses(ManagedFunction& function, ValueId from,
                        ValueId to) {
    for (auto& value : function.values) {
        for (auto& operand : value.operands) {
            if (operand == from) operand = to;
        }
        for (auto& argument : value.call_arguments) {
            if (argument.value == from) argument.value = to;
        }
        for (auto& incoming : value.incoming) {
            if (incoming.value == from) incoming.value = to;
        }
    }
    for (auto& block : function.blocks) {
        if (block.terminator.value == from) block.terminator.value = to;
    }
}

void remove_values(ManagedFunction& function,
                   const std::unordered_set<std::uint32_t>& removed) {
    if (removed.empty()) return;
    for (auto& block : function.blocks) {
        std::erase_if(block.values, [&](ValueId value) {
            return removed.contains(value.value);
        });
    }
    compact_managed_values(function);
}

bool commute_masked_truncations(ManagedFunction& function,
                                const hir::Module& hir_module) {
    const UseLists uses(function);
    std::unordered_set<std::uint32_t> removed;
    bool changed = false;
    for (auto& block : function.blocks) {
        for (std::size_t position = 0; position < block.values.size();
             ++position) {
            const auto truncate_id = block.values[position];
            const auto truncate = function.values[truncate_id.value];
            if (truncate.kind != ValueKind::Cast ||
                truncate.cast != CastOperation::Truncate ||
                truncate.operands.size() != 1 ||
                scalar_integer_bits(hir_module, truncate.type) == 0) {
                continue;
            }
            const auto and_id = truncate.operands.front();
            if (and_id.value >= function.values.size()) continue;
            const auto bit_and = function.values[and_id.value];
            const auto wide_bits =
                scalar_integer_bits(hir_module, bit_and.type);
            const auto narrow_bits =
                scalar_integer_bits(hir_module, truncate.type);
            if (bit_and.kind != ValueKind::Binary ||
                bit_and.binary != BinaryOperation::BitAnd ||
                bit_and.operands.size() != 2 || wide_bits <= narrow_bits) {
                continue;
            }

            std::optional<ValueId> narrow_mask;
            ValueId wide_value{};
            for (unsigned mask_index = 0; mask_index < 2; ++mask_index) {
                const auto extend_id = bit_and.operands[mask_index];
                if (extend_id.value >= function.values.size()) continue;
                const auto& extend = function.values[extend_id.value];
                if (extend.kind != ValueKind::Cast ||
                    extend.cast != CastOperation::ZeroExtend ||
                    extend.type != bit_and.type ||
                    extend.operands.size() != 1 ||
                    function.values[extend.operands.front().value].type !=
                        truncate.type) {
                    continue;
                }
                narrow_mask = extend.operands.front();
                wide_value = bit_and.operands[1U - mask_index];
                break;
            }
            if (!narrow_mask || wide_value.value >= function.values.size() ||
                function.values[wide_value.value].type != bit_and.type) {
                continue;
            }

            ManagedValue narrow;
            narrow.id = {
                static_cast<std::uint32_t>(function.values.size())};
            narrow.location = truncate.location;
            narrow.type = truncate.type;
            narrow.kind = ValueKind::Cast;
            narrow.cast = CastOperation::Truncate;
            narrow.operands = {wide_value};
            const auto narrow_id = narrow.id;
            function.values.push_back(std::move(narrow));

            auto& replacement = function.values[truncate_id.value];
            replacement.kind = ValueKind::Binary;
            replacement.binary = BinaryOperation::BitAnd;
            replacement.operands = {narrow_id, *narrow_mask};
            block.values.insert(
                block.values.begin() +
                    static_cast<std::ptrdiff_t>(position),
                narrow_id);
            ++position;
            if (uses.uses(and_id).size() == 1) {
                removed.insert(and_id.value);
            }
            changed = true;
        }
    }
    remove_values(function, removed);
    return changed;
}

bool narrow_single_use_integer_loads(ManagedFunction& function,
                                     hir::Module& hir_module,
                                     const TargetInfo& target) {
    const UseLists uses(function);
    std::unordered_set<std::uint32_t> removed;
    bool changed = false;
    for (const auto& block : function.blocks) {
        // New address values are inserted in the load's defining block, which
        // can differ from the truncation's block. Iterate a stable copy here.
        const auto values = block.values;
        for (const auto truncate_id : values) {
            const auto truncate = function.values[truncate_id.value];
            if (truncate.kind != ValueKind::Cast ||
                truncate.cast != CastOperation::Truncate ||
                truncate.operands.size() != 1) {
                continue;
            }
            const auto target_bits =
                scalar_integer_bits(hir_module, truncate.type);
            const auto load_id = truncate.operands.front();
            if (target_bits == 0 || load_id.value >= function.values.size()) {
                continue;
            }
            const auto load = function.values[load_id.value];
            const auto source_bits =
                scalar_integer_bits(hir_module, load.type);
            const auto& load_uses = uses.uses(load_id);
            if ((load.kind != ValueKind::PointerLoad &&
                 load.kind != ValueKind::IndexedLoad) ||
                load.is_volatile_access || source_bits <= target_bits ||
                source_bits % 8U != 0 || target_bits % 8U != 0 ||
                load_uses.size() != 1 || !load_uses.front().user ||
                *load_uses.front().user != truncate_id) {
                continue;
            }
            const auto source_bytes = source_bits / 8U;
            const auto target_bytes = target_bits / 8U;
            const auto byte_offset =
                target.data_layout.byte_order == ByteOrder::Big
                    ? source_bytes - target_bytes
                    : 0U;
            if (target_bytes == 0 || byte_offset % target_bytes != 0) {
                continue;
            }

            const auto load_block_id = uses.definition_block(load_id);
            if (!load_block_id) continue;
            auto& load_block = function.blocks[load_block_id->value];
            const auto load_position = std::find(
                load_block.values.begin(), load_block.values.end(), load_id);
            if (load_position == load_block.values.end()) continue;

            std::vector<ValueId> address_values;
            ValueId wide_address{};
            if (load.kind == ValueKind::IndexedLoad) {
                if (load.operands.size() != 2) continue;
                ManagedValue address;
                address.id = {
                    static_cast<std::uint32_t>(function.values.size())};
                address.location = load.location;
                address.type =
                    function.values[load.operands.front().value].type;
                address.kind = ValueKind::IndexedAddress;
                address.operands = load.operands;
                wide_address = address.id;
                function.values.push_back(std::move(address));
                address_values.push_back(wide_address);
            } else {
                if (load.operands.size() != 1) continue;
                wide_address = load.operands.front();
            }

            const auto narrow_pointer = hir_module.pointer_to(truncate.type);
            ManagedValue reinterpret;
            reinterpret.id = {
                static_cast<std::uint32_t>(function.values.size())};
            reinterpret.location = load.location;
            reinterpret.type = narrow_pointer;
            reinterpret.kind = ValueKind::Cast;
            reinterpret.cast = CastOperation::Reinterpret;
            reinterpret.operands = {wide_address};
            auto narrow_address = reinterpret.id;
            function.values.push_back(std::move(reinterpret));
            address_values.push_back(narrow_address);

            if (byte_offset != 0) {
                ManagedValue offset;
                offset.id = {
                    static_cast<std::uint32_t>(function.values.size())};
                offset.location = load.location;
                offset.type = *hir_module.builtin(BuiltinType::Uptr);
                offset.kind = ValueKind::ConstantInteger;
                offset.integer = byte_offset / target_bytes;
                const auto offset_id = offset.id;
                function.values.push_back(std::move(offset));
                address_values.push_back(offset_id);

                ManagedValue address;
                address.id = {
                    static_cast<std::uint32_t>(function.values.size())};
                address.location = load.location;
                address.type = narrow_pointer;
                address.kind = ValueKind::IndexedAddress;
                address.operands = {narrow_address, offset_id};
                narrow_address = address.id;
                function.values.push_back(std::move(address));
                address_values.push_back(narrow_address);
            }

            const auto insertion = static_cast<std::size_t>(
                load_position - load_block.values.begin());
            load_block.values.insert(
                load_block.values.begin() +
                    static_cast<std::ptrdiff_t>(insertion),
                address_values.begin(), address_values.end());

            auto& replacement = function.values[load_id.value];
            replacement.kind = ValueKind::PointerLoad;
            replacement.type = truncate.type;
            replacement.operands = {narrow_address};
            if (replacement.memory_alignment != 0 && byte_offset != 0) {
                replacement.memory_alignment = std::gcd(
                    replacement.memory_alignment, byte_offset);
            }
            replace_value_uses(function, truncate_id, load_id);
            removed.insert(truncate_id.value);
            changed = true;
        }
    }
    remove_values(function, removed);
    return changed;
}

bool factor_zero_extended_bitwise_chains_impl(
    ManagedFunction& function, const hir::Module& hir_module) {
    struct Plan {
        BlockId block;
        ValueId root;
        ValueId inner;
        ValueId first_extend;
        ValueId second_extend;
        ValueId wide_base;
        ValueId first_narrow;
        ValueId second_narrow;
        hir::TypeId narrow_type;
        BinaryOperation operation{BinaryOperation::BitXor};
        SourceLocation location;
        bool direct{};
    };

    const UseLists uses(function);
    const auto zero_extension = [&](ValueId id, hir::TypeId wide_type)
        -> const ManagedValue* {
        if (id.value >= function.values.size()) return nullptr;
        const auto& value = function.values[id.value];
        return value.kind == ValueKind::Cast &&
                value.cast == CastOperation::ZeroExtend &&
                value.type == wide_type && value.operands.size() == 1 &&
                !value.effect_input && !value.effect_output
            ? &value
            : nullptr;
    };
    const auto bitwise = [](BinaryOperation operation) {
        return operation == BinaryOperation::BitAnd ||
            operation == BinaryOperation::BitOr ||
            operation == BinaryOperation::BitXor;
    };

    std::vector<Plan> plans;
    std::unordered_set<std::uint32_t> reserved;
    for (const auto& block : function.blocks) {
        for (const auto root_id : block.values) {
            const auto& root = function.values[root_id.value];
            if (root.kind != ValueKind::Binary || !bitwise(root.binary) ||
                root.operands.size() != 2 || root.effect_input ||
                root.effect_output || reserved.contains(root_id.value)) {
                continue;
            }
            const auto wide_bits =
                scalar_integer_bits(hir_module, root.type);
            if (wide_bits == 0) continue;

            const auto* left_extend =
                zero_extension(root.operands[0], root.type);
            const auto* right_extend =
                zero_extension(root.operands[1], root.type);
            if (left_extend && right_extend) {
                const auto first = left_extend->operands.front();
                const auto second = right_extend->operands.front();
                if (function.values[first.value].type !=
                        function.values[second.value].type ||
                    scalar_integer_bits(
                        hir_module, function.values[first.value].type) >=
                        wide_bits) {
                    continue;
                }
                plans.push_back(
                    {block.id, root_id, {}, root.operands[0],
                     root.operands[1], {}, first, second,
                     function.values[first.value].type, root.binary,
                     root.location, true});
                reserved.insert(root_id.value);
                reserved.insert(root.operands[0].value);
                reserved.insert(root.operands[1].value);
                continue;
            }

            for (unsigned outer_extend_index = 0;
                 outer_extend_index < 2; ++outer_extend_index) {
                const auto outer_extend_id =
                    root.operands[outer_extend_index];
                const auto inner_id =
                    root.operands[1U - outer_extend_index];
                const auto* outer_extend =
                    zero_extension(outer_extend_id, root.type);
                if (!outer_extend || inner_id.value >= function.values.size() ||
                    reserved.contains(inner_id.value)) {
                    continue;
                }
                const auto& inner = function.values[inner_id.value];
                if (inner.kind != ValueKind::Binary ||
                    inner.binary != root.binary || inner.type != root.type ||
                    inner.operands.size() != 2 || inner.effect_input ||
                    inner.effect_output || uses.uses(inner_id).size() != 1) {
                    continue;
                }
                for (unsigned inner_extend_index = 0;
                     inner_extend_index < 2; ++inner_extend_index) {
                    const auto inner_extend_id =
                        inner.operands[inner_extend_index];
                    const auto wide_base =
                        inner.operands[1U - inner_extend_index];
                    const auto* inner_extend =
                        zero_extension(inner_extend_id, root.type);
                    if (!inner_extend ||
                        zero_extension(wide_base, root.type) ||
                        reserved.contains(inner_extend_id.value)) {
                        continue;
                    }
                    const auto first = inner_extend->operands.front();
                    const auto second = outer_extend->operands.front();
                    const auto narrow_type =
                        function.values[first.value].type;
                    const auto narrow_bits =
                        scalar_integer_bits(hir_module, narrow_type);
                    if (function.values[second.value].type != narrow_type ||
                        function.values[wide_base.value].type != root.type ||
                        narrow_bits == 0 || narrow_bits >= wide_bits) {
                        continue;
                    }
                    plans.push_back(
                        {block.id, root_id, inner_id, inner_extend_id,
                         outer_extend_id, wide_base, first, second,
                         narrow_type, root.binary, root.location, false});
                    reserved.insert(root_id.value);
                    reserved.insert(inner_id.value);
                    reserved.insert(inner_extend_id.value);
                    reserved.insert(outer_extend_id.value);
                    inner_extend_index = 2;
                }
                if (!plans.empty() && plans.back().root == root_id) break;
            }
        }
    }
    if (plans.empty()) return false;

    std::unordered_set<std::uint32_t> removed;
    for (const auto& plan : plans) {
        auto& block = function.blocks[plan.block.value];
        const auto position =
            std::find(block.values.begin(), block.values.end(), plan.root);
        if (position == block.values.end()) continue;

        ManagedValue narrow;
        narrow.id = {
            static_cast<std::uint32_t>(function.values.size())};
        narrow.location = plan.location;
        narrow.type = plan.narrow_type;
        narrow.kind = ValueKind::Binary;
        narrow.binary = plan.operation;
        narrow.operands = {plan.first_narrow, plan.second_narrow};
        const auto narrow_id = narrow.id;
        function.values.push_back(std::move(narrow));

        ManagedValue extend;
        extend.id = {
            static_cast<std::uint32_t>(function.values.size())};
        extend.location = plan.location;
        extend.type = function.values[plan.root.value].type;
        extend.kind = ValueKind::Cast;
        extend.cast = CastOperation::ZeroExtend;
        extend.operands = {narrow_id};
        const auto extend_id = extend.id;
        function.values.push_back(std::move(extend));

        const auto insertion = static_cast<std::size_t>(
            position - block.values.begin());
        block.values.insert(
            block.values.begin() + static_cast<std::ptrdiff_t>(insertion),
            {narrow_id, extend_id});
        auto& root = function.values[plan.root.value];
        if (plan.direct) {
            root.kind = ValueKind::Cast;
            root.cast = CastOperation::ZeroExtend;
            root.operands = {narrow_id};
            removed.insert(extend_id.value);
        } else {
            root.operands = {plan.wide_base, extend_id};
            removed.insert(plan.inner.value);
        }
        if (uses.uses(plan.first_extend).size() == 1) {
            removed.insert(plan.first_extend.value);
        }
        if (uses.uses(plan.second_extend).size() == 1) {
            removed.insert(plan.second_extend.value);
        }
    }
    remove_values(function, removed);
    return true;
}

bool canonicalize_bitwise_operations_impl(ManagedFunction& function,
                                          const hir::Module& hir_module) {
    const UseLists uses(function);
    bool changed = false;
    for (const auto& block : function.blocks) {
        for (const auto id : block.values) {
            auto& combine = function.values[id.value];
            if (combine.kind != ValueKind::Binary ||
                combine.binary != BinaryOperation::BitOr ||
                combine.operands.size() != 2) {
                continue;
            }

            const auto first_id = combine.operands[0];
            const auto second_id = combine.operands[1];
            if (first_id.value >= function.values.size() ||
                second_id.value >= function.values.size()) {
                continue;
            }
            const auto first_block = uses.definition_block(first_id);
            const auto second_block = uses.definition_block(second_id);
            if (first_block != block.id || second_block != block.id ||
                uses.uses(first_id).size() != 1 ||
                uses.uses(second_id).size() != 1) {
                continue;
            }
            const auto& first = function.values[first_id.value];
            const auto& second = function.values[second_id.value];
            if (first.kind != ValueKind::Binary ||
                second.kind != ValueKind::Binary ||
                first.operands.size() != 2 || second.operands.size() != 2 ||
                first.operands[0] != second.operands[0] ||
                first.type != second.type || first.type != combine.type ||
                scalar_integer_bits(hir_module, first.type) == 0) {
                continue;
            }

            const ManagedValue* direct_shift{};
            const ManagedValue* complement_shift{};
            BinaryOperation rotate{};
            if (first.binary == BinaryOperation::ShiftLeft &&
                second.binary == BinaryOperation::ShiftRightLogical) {
                direct_shift = &first;
                complement_shift = &second;
                rotate = BinaryOperation::RotateLeft;
            } else if (first.binary == BinaryOperation::ShiftRightLogical &&
                       second.binary == BinaryOperation::ShiftLeft) {
                direct_shift = &first;
                complement_shift = &second;
                rotate = BinaryOperation::RotateRight;
            } else {
                continue;
            }

            // Keep the initial canonical form within the scalar modes every
            // current target can either select directly or cheaply legalize.
            // Wider and vector funnel shifts can be admitted once Machine IR
            // supports multi-instruction legalization before selection.
            const auto bits =
                scalar_integer_bits(hir_module, direct_shift->type);
            if (bits < 32 || bits > 64)
                continue;

            const auto direct_amount = direct_shift->operands[1];
            const auto complement_amount = complement_shift->operands[1];
            const auto constant_amount =
                [&](ValueId amount) -> std::optional<std::uint64_t> {
                if (amount.value >= function.values.size()) {
                    return std::nullopt;
                }
                const auto& value = function.values[amount.value];
                if (value.kind != ValueKind::ConstantInteger ||
                    value.integer_high != 0) {
                    return std::nullopt;
                }
                return value.integer;
            };
            const auto direct_constant = constant_amount(direct_amount);
            const auto complement_constant = constant_amount(complement_amount);
            const bool constant_complement =
                direct_constant && complement_constant &&
                *direct_constant < bits && *complement_constant <= bits &&
                *direct_constant + *complement_constant == bits;

            bool variable_complement = false;
            if (!constant_complement &&
                complement_amount.value < function.values.size()) {
                const auto& subtract = function.values[complement_amount.value];
                const auto subtract_block =
                    uses.definition_block(complement_amount);
                variable_complement =
                    subtract_block == block.id &&
                    subtract.kind == ValueKind::Binary &&
                    subtract.binary == BinaryOperation::Subtract &&
                    subtract.operands.size() == 2 &&
                    subtract.operands[1] == direct_amount &&
                    uses.uses(complement_amount).size() == 1;
                if (variable_complement) {
                    const auto width = constant_amount(subtract.operands[0]);
                    variable_complement = width && *width == bits;
                }
            }
            if (!constant_complement && !variable_complement)
                continue;

            combine.binary = rotate;
            combine.operands = {direct_shift->operands[0],
                                direct_shift->operands[1]};
            changed = true;
        }
    }
    return changed;
}

bool commutative_tail_operation(BinaryOperation operation) {
    return operation == BinaryOperation::Add ||
        operation == BinaryOperation::Multiply ||
        operation == BinaryOperation::BitAnd ||
        operation == BinaryOperation::BitOr ||
        operation == BinaryOperation::BitXor;
}

bool factorable_integer_tail(const ManagedValue& value,
                             const hir::Module& hir_module) {
    if (value.kind != ValueKind::Binary || value.operands.size() != 2 ||
        scalar_integer_bits(hir_module, value.type) == 0 ||
        value.effect_input || value.effect_output) {
        return false;
    }
    const bool operation_supported =
        value.binary == BinaryOperation::Add ||
        value.binary == BinaryOperation::Subtract ||
        value.binary == BinaryOperation::Multiply ||
        value.binary == BinaryOperation::BitAnd ||
        value.binary == BinaryOperation::BitOr ||
        value.binary == BinaryOperation::BitXor;
    return operation_supported;
}

struct PhiTailCandidate {
    BlockId predecessor;
    ValueId result;
    ValueId operands[2];
    BinaryOperation operation{BinaryOperation::Add};
    hir::TypeId type;
    SourceLocation location;
};

struct PhiTailMember {
    BlockId predecessor;
    ValueId result;
    ValueId varying;
};

struct PhiTailPlan {
    BlockId join;
    ValueId join_phi;
    BinaryOperation operation{BinaryOperation::Add};
    hir::TypeId type;
    ValueId common;
    unsigned common_index{};
    SourceLocation location;
    std::vector<PhiTailMember> members;
};

std::optional<ValueId> varying_tail_operand(
    const PhiTailCandidate& candidate, BinaryOperation operation,
    hir::TypeId type, ValueId common, unsigned common_index) {
    if (candidate.operation != operation || candidate.type != type) {
        return std::nullopt;
    }
    if (candidate.operands[common_index] == common) {
        return candidate.operands[1U - common_index];
    }
    if (commutative_tail_operation(operation) &&
        candidate.operands[1U - common_index] == common) {
        return candidate.operands[common_index];
    }
    return std::nullopt;
}

std::vector<PhiTailPlan> collect_phi_tail_plans(
    const ManagedFunction& function, const hir::Module& hir_module) {
    const UseLists uses(function);
    std::vector<PhiTailPlan> plans;
    for (const auto& join : function.blocks) {
        std::vector<ValueId> phis;
        for (const auto id : join.values) {
            if (function.values[id.value].kind == ValueKind::Phi) {
                phis.push_back(id);
            }
        }
        // Factoring every other join phi would require one corresponding phi
        // in each shared tail. Keep the initial profitability proof narrow.
        if (phis.size() != 1 || join.predecessors.size() < 3) continue;
        const auto phi_id = phis.front();
        const auto& phi = function.values[phi_id.value];
        std::vector<PhiTailCandidate> candidates;
        for (const auto& incoming : phi.incoming) {
            if (incoming.predecessor.value >= function.blocks.size() ||
                incoming.value.value >= function.values.size()) {
                continue;
            }
            const auto& predecessor =
                function.blocks[incoming.predecessor.value];
            const auto& value = function.values[incoming.value.value];
            const auto& reverse_uses = uses.uses(incoming.value);
            if (predecessor.terminator.kind != TerminatorKind::Branch ||
                predecessor.terminator.successors.size() != 1 ||
                predecessor.terminator.successors.front() != join.id ||
                uses.definition_block(incoming.value) !=
                    incoming.predecessor ||
                reverse_uses.size() != 1 ||
                reverse_uses.front().kind != UseKind::PhiIncoming ||
                reverse_uses.front().block != incoming.predecessor ||
                reverse_uses.front().user != phi_id ||
                value.operands.size() != 2 ||
                value.operands[0].value >= function.values.size() ||
                value.operands[1].value >= function.values.size() ||
                !factorable_integer_tail(value, hir_module) ||
                function.values[value.operands[0].value].type != value.type ||
                function.values[value.operands[1].value].type != value.type) {
                continue;
            }
            candidates.push_back(
                {incoming.predecessor, incoming.value,
                 {value.operands[0], value.operands[1]}, value.binary,
                 value.type, value.location});
        }

        std::unordered_set<std::uint32_t> selected;
        while (true) {
            std::optional<PhiTailPlan> best;
            for (const auto& seed : candidates) {
                if (selected.contains(seed.result.value)) continue;
                for (unsigned common_index = 0; common_index < 2;
                     ++common_index) {
                    PhiTailPlan candidate;
                    candidate.join = join.id;
                    candidate.join_phi = phi_id;
                    candidate.operation = seed.operation;
                    candidate.type = seed.type;
                    candidate.common = seed.operands[common_index];
                    candidate.common_index =
                        commutative_tail_operation(seed.operation)
                        ? 0U : common_index;
                    candidate.location = seed.location;
                    for (const auto& member : candidates) {
                        if (selected.contains(member.result.value)) continue;
                        const auto varying = varying_tail_operand(
                            member, seed.operation, seed.type,
                            candidate.common, common_index);
                        if (!varying) continue;
                        candidate.members.push_back(
                            {member.predecessor, member.result, *varying});
                    }
                    if (candidate.members.size() >= 3 &&
                        (!best || candidate.members.size() >
                                      best->members.size())) {
                        best = std::move(candidate);
                    }
                }
            }
            if (!best) break;
            for (const auto& member : best->members) {
                selected.insert(member.result.value);
            }
            plans.push_back(std::move(*best));
        }
    }
    return plans;
}

template <typename Item, typename Predecessor>
std::vector<Item> collapse_tail_predecessors(
    const std::vector<Item>& source,
    const std::unordered_set<std::uint32_t>& collapsed,
    Item replacement, Predecessor predecessor) {
    std::vector<Item> result;
    result.reserve(source.size());
    bool inserted = false;
    for (const auto& item : source) {
        if (!collapsed.contains(predecessor(item).value)) {
            result.push_back(item);
        } else if (!inserted) {
            result.push_back(replacement);
            inserted = true;
        }
    }
    return result;
}

bool apply_phi_tail_plan(ManagedFunction& function, const PhiTailPlan& plan,
                         std::unordered_set<std::uint32_t>& removed) {
    if (plan.join.value >= function.blocks.size() ||
        plan.join_phi.value >= function.values.size() ||
        plan.common.value >= function.values.size() ||
        plan.common_index > 1 || plan.members.size() < 3) {
        return false;
    }

    // Plans are collected before any rewrite. Validate the complete plan
    // against the current function first so several disjoint groups may be
    // applied to one join without a failed late check leaving partial SSA.
    const auto& join = function.blocks[plan.join.value];
    const auto& join_phi = function.values[plan.join_phi.value];
    if (join.effect.value >= function.effects.size() ||
        join_phi.kind != ValueKind::Phi || join_phi.type != plan.type ||
        std::count(join.values.begin(), join.values.end(), plan.join_phi) !=
            1) {
        return false;
    }
    const auto& join_effect = function.effects[join.effect.value];
    if (join_effect.kind != EffectKind::Phi) return false;

    std::unordered_set<std::uint32_t> collapsed;
    std::unordered_set<std::uint32_t> results;
    for (const auto& member : plan.members) {
        if (member.predecessor.value >= function.blocks.size() ||
            member.result.value >= function.values.size() ||
            member.varying.value >= function.values.size() ||
            removed.contains(member.result.value) ||
            !collapsed.insert(member.predecessor.value).second ||
            !results.insert(member.result.value).second) {
            return false;
        }
        const auto& predecessor =
            function.blocks[member.predecessor.value];
        if (predecessor.terminator.kind != TerminatorKind::Branch ||
            predecessor.terminator.successors.size() != 1 ||
            predecessor.terminator.successors.front() != plan.join ||
            std::count(predecessor.values.begin(), predecessor.values.end(),
                       member.result) != 1 ||
            function.values[member.varying.value].type != plan.type ||
            std::count(join.predecessors.begin(), join.predecessors.end(),
                       member.predecessor) != 1 ||
            std::count_if(
                join_phi.incoming.begin(), join_phi.incoming.end(),
                [&](const PhiIncoming& incoming) {
                    return incoming.predecessor == member.predecessor &&
                        incoming.value == member.result;
                }) != 1 ||
            std::count_if(
                join_effect.incoming.begin(), join_effect.incoming.end(),
                [&](const EffectIncoming& incoming) {
                    return incoming.predecessor == member.predecessor &&
                        incoming.effect == predecessor.terminator.effect;
                }) != 1) {
            return false;
        }
    }

    const BlockId shared_id{
        static_cast<std::uint32_t>(function.blocks.size())};
    const EffectId shared_effect_id{
        static_cast<std::uint32_t>(function.effects.size())};
    const ValueId varying_id{
        static_cast<std::uint32_t>(function.values.size())};

    ManagedValue varying;
    varying.id = varying_id;
    varying.location = plan.location;
    varying.type = plan.type;
    varying.kind = ValueKind::Phi;
    for (const auto& member : plan.members) {
        varying.incoming.push_back({member.predecessor, member.varying});
    }
    function.values.push_back(std::move(varying));

    const ValueId result_id{
        static_cast<std::uint32_t>(function.values.size())};
    ManagedValue result;
    result.id = result_id;
    result.location = plan.location;
    result.type = plan.type;
    result.kind = ValueKind::Binary;
    result.binary = plan.operation;
    result.operands = plan.common_index == 0
        ? std::vector<ValueId>{plan.common, varying_id}
        : std::vector<ValueId>{varying_id, plan.common};
    function.values.push_back(std::move(result));

    ManagedEffect shared_effect;
    shared_effect.id = shared_effect_id;
    shared_effect.location = plan.location;
    shared_effect.kind = EffectKind::Phi;
    for (const auto& member : plan.members) {
        auto& predecessor = function.blocks[member.predecessor.value];
        predecessor.terminator.successors.front() = shared_id;
        shared_effect.incoming.push_back(
            {member.predecessor, predecessor.terminator.effect});
        removed.insert(member.result.value);
    }

    auto& mutable_join = function.blocks[plan.join.value];
    mutable_join.predecessors = collapse_tail_predecessors(
        mutable_join.predecessors, collapsed, shared_id,
        [](BlockId predecessor) { return predecessor; });
    auto& mutable_join_effect =
        function.effects[mutable_join.effect.value];
    mutable_join_effect.incoming = collapse_tail_predecessors(
        mutable_join_effect.incoming, collapsed,
        EffectIncoming{shared_id, shared_effect_id},
        [](const EffectIncoming& incoming) {
            return incoming.predecessor;
        });
    auto& mutable_join_phi = function.values[plan.join_phi.value];
    mutable_join_phi.incoming = collapse_tail_predecessors(
        mutable_join_phi.incoming, collapsed,
        PhiIncoming{shared_id, result_id},
        [](const PhiIncoming& incoming) { return incoming.predecessor; });

    ManagedBlock shared;
    shared.id = shared_id;
    shared.location = plan.location;
    shared.values = {varying_id, result_id};
    for (const auto& member : plan.members) {
        shared.predecessors.push_back(member.predecessor);
    }
    shared.effect = shared_effect_id;
    shared.terminator = {
        TerminatorKind::Branch, plan.location, std::nullopt,
        {plan.join}, shared_effect_id};
    function.effects.push_back(std::move(shared_effect));
    function.blocks.push_back(std::move(shared));
    return true;
}

bool recorded_use_matches(const ManagedFunction& function,
                          const ValueUse& use, ValueId from) {
    if (use.kind == UseKind::Terminator) {
        return use.block.value < function.blocks.size() &&
               function.blocks[use.block.value].terminator.value == from;
    }
    if (!use.user || use.user->value >= function.values.size()) return false;
    const auto& user = function.values[use.user->value];
    switch (use.kind) {
    case UseKind::Operand:
        return use.index < user.operands.size() &&
               user.operands[use.index] == from;
    case UseKind::CallArgument:
        return use.index < user.call_arguments.size() &&
               user.call_arguments[use.index].value == from;
    case UseKind::PhiIncoming:
        return use.index < user.incoming.size() &&
               user.incoming[use.index].value == from;
    case UseKind::Terminator: break;
    }
    return false;
}

void rewrite_recorded_use(ManagedFunction& function, const ValueUse& use,
                          ValueId from, ValueId to) {
    assert(recorded_use_matches(function, use, from));
    (void)from;
    if (use.kind == UseKind::Terminator) {
        function.blocks[use.block.value].terminator.value = to;
        return;
    }
    auto& user = function.values[use.user->value];
    switch (use.kind) {
    case UseKind::Operand: user.operands[use.index] = to; return;
    case UseKind::CallArgument:
        user.call_arguments[use.index].value = to;
        return;
    case UseKind::PhiIncoming: user.incoming[use.index].value = to; return;
    case UseKind::Terminator: return;
    }
}

bool cloneable_rotated_guard_value(const ManagedValue& value) {
    if (value.effect_input || value.effect_output || value.slot ||
        value.callee || value.label || value.object || value.patch_sink ||
        value.is_volatile_access || !value.call_arguments.empty() ||
        !value.incoming.empty()) {
        return false;
    }
    switch (value.kind) {
    case ValueKind::Unary:
    case ValueKind::Binary:
    case ValueKind::Cast:
    case ValueKind::Select:
    case ValueKind::Splat:
    case ValueKind::ExtractElement:
    case ValueKind::InsertElement:
        return true;
    case ValueKind::Intrinsic:
        return value.intrinsic == IntrinsicOperation::Expect;
    default: return false;
    }
}

bool rotate_one_guarded_loop(ManagedFunction& function,
                             const NaturalLoop& loop,
                             const UseLists& uses) {
    if (!loop.preheader || loop.latches.size() != 1 ||
        loop.header.value >= function.blocks.size()) {
        return false;
    }
    const auto header_id = loop.header;
    const auto preheader_id = *loop.preheader;
    const auto latch_id = loop.latches.front();
    if (preheader_id.value >= function.blocks.size() ||
        latch_id.value >= function.blocks.size() || latch_id == header_id) {
        return false;
    }

    const auto header_snapshot = function.blocks[header_id.value];
    const auto latch_snapshot = function.blocks[latch_id.value];
    if (header_snapshot.predecessors.size() != 2 ||
        std::count(header_snapshot.predecessors.begin(),
                   header_snapshot.predecessors.end(), preheader_id) != 1 ||
        std::count(header_snapshot.predecessors.begin(),
                   header_snapshot.predecessors.end(), latch_id) != 1 ||
        header_snapshot.terminator.kind !=
            TerminatorKind::ConditionalBranch ||
        !header_snapshot.terminator.value ||
        header_snapshot.terminator.successors.size() != 2 ||
        header_snapshot.terminator.successors[0] ==
            header_snapshot.terminator.successors[1] ||
        header_snapshot.terminator.effect != header_snapshot.effect ||
        latch_snapshot.terminator.kind != TerminatorKind::Branch ||
        latch_snapshot.terminator.successors.size() != 1 ||
        latch_snapshot.terminator.successors.front() != header_id) {
        return false;
    }

    std::optional<BlockId> body_id;
    std::optional<BlockId> exit_id;
    for (const auto successor : header_snapshot.terminator.successors) {
        if (loop.blocks.contains(successor.value)) body_id = successor;
        else exit_id = successor;
    }
    if (!body_id || !exit_id || *body_id == header_id ||
        *body_id == latch_id ||
        *exit_id == preheader_id ||
        body_id->value >= function.blocks.size() ||
        exit_id->value >= function.blocks.size()) {
        return false;
    }
    const auto body_snapshot = function.blocks[body_id->value];
    const auto exit_snapshot = function.blocks[exit_id->value];
    const auto valid_values = [&](const ManagedBlock& block) {
        return std::all_of(block.values.begin(), block.values.end(),
                           [&](ValueId id) {
                               return id.value < function.values.size();
                           });
    };
    if (!valid_values(header_snapshot) || !valid_values(body_snapshot) ||
        !valid_values(exit_snapshot) ||
        header_snapshot.terminator.value->value >= function.values.size() ||
        body_snapshot.predecessors != std::vector<BlockId>{header_id} ||
        exit_snapshot.predecessors != std::vector<BlockId>{header_id} ||
        std::any_of(body_snapshot.values.begin(), body_snapshot.values.end(),
                    [&](ValueId id) {
                        return function.values[id.value].kind ==
                               ValueKind::Phi;
                    }) ||
        std::any_of(exit_snapshot.values.begin(), exit_snapshot.values.end(),
                    [&](ValueId id) {
                        return function.values[id.value].kind ==
                               ValueKind::Phi;
                    })) {
        return false;
    }

    // A secondary exit would need a distinct live-out/effect repair. Keep the
    // first implementation deliberately single-exit so every use outside the
    // loop is dominated by the one repaired exit block.
    for (const auto member : loop.blocks) {
        if (member >= function.blocks.size()) return false;
        for (const auto successor :
             function.blocks[member].terminator.successors) {
            if (loop.blocks.contains(successor.value)) continue;
            if (member != header_id.value || successor != *exit_id) {
                return false;
            }
        }
    }
    if (std::any_of(function.labels.begin(), function.labels.end(),
                    [&](const ManagedLabel& label) {
                        return loop.blocks.contains(label.block.value) ||
                               label.block == *exit_id;
                    })) {
        return false;
    }

    if (header_snapshot.effect.value >= function.effects.size() ||
        body_snapshot.effect.value >= function.effects.size() ||
        exit_snapshot.effect.value >= function.effects.size()) {
        return false;
    }
    const auto& header_effect =
        function.effects[header_snapshot.effect.value];
    const auto& body_effect = function.effects[body_snapshot.effect.value];
    const auto& exit_effect = function.effects[exit_snapshot.effect.value];
    if (header_effect.kind != EffectKind::Phi ||
        body_effect.kind != EffectKind::Phi ||
        exit_effect.kind != EffectKind::Phi ||
        std::any_of(header_snapshot.values.begin(),
                    header_snapshot.values.end(), [&](ValueId id) {
                        const auto& value = function.values[id.value];
                        return value.effect_input || value.effect_output;
                    })) {
        return false;
    }

    struct StatePhi {
        ValueId header;
        ValueId initial;
        ValueId carried;
        ValueId body;
        ValueId exit;
        hir::TypeId type;
        SourceLocation location;
    };
    std::vector<StatePhi> states;
    std::unordered_map<std::uint32_t, std::size_t> state_index;
    for (const auto id : header_snapshot.values) {
        const auto& value = function.values[id.value];
        if (value.kind != ValueKind::Phi) continue;
        if (value.incoming.size() != 2) return false;
        std::optional<ValueId> initial;
        std::optional<ValueId> carried;
        for (const auto& incoming : value.incoming) {
            if (incoming.value.value >= function.values.size()) return false;
            if (incoming.predecessor == preheader_id) initial = incoming.value;
            else if (incoming.predecessor == latch_id) carried = incoming.value;
        }
        if (!initial || !carried) return false;
        state_index.emplace(id.value, states.size());
        states.push_back({id, *initial, *carried, {}, {}, value.type,
                          value.location});
    }
    if (states.empty()) return false;

    std::vector<std::int8_t> dependency(function.values.size(), -1);
    const auto depends_on_state = [&](auto&& self, ValueId id) -> bool {
        if (id.value >= function.values.size()) return true;
        if (state_index.contains(id.value)) return true;
        const auto definition = uses.definition_block(id);
        if (!definition || *definition != header_id) return false;
        auto& cached = dependency[id.value];
        if (cached >= 0) return cached != 0;
        cached = 0;
        const auto& value = function.values[id.value];
        for (const auto operand : value.operands) {
            if (self(self, operand)) {
                cached = 1;
                break;
            }
        }
        return cached != 0;
    };
    const auto condition_id = *header_snapshot.terminator.value;
    if (!depends_on_state(depends_on_state, condition_id)) return false;

    // Header-derived values used in the old body were recomputed on every
    // visit to the header. Leaving such a use dominated by the one-time guard
    // would be valid SSA but stale semantics, so reject it. Header PHIs are
    // handled separately below.
    for (const auto id : header_snapshot.values) {
        if (state_index.contains(id.value) ||
            !depends_on_state(depends_on_state, id)) {
            continue;
        }
        if (std::any_of(uses.uses(id).begin(), uses.uses(id).end(),
                        [&](const ValueUse& use) {
                            return use.block != header_id;
                        })) {
            return false;
        }
    }
    for (const auto& state : states) {
        const auto definition = uses.definition_block(state.carried);
        if (definition && *definition == header_id &&
            !state_index.contains(state.carried.value) &&
            depends_on_state(depends_on_state, state.carried)) {
            return false;
        }
    }

    std::vector<ValueId> guard_clone_order;
    std::unordered_set<std::uint32_t> guard_seen;
    const auto collect_guard = [&](auto&& self, ValueId id) -> bool {
        if (id.value >= function.values.size()) return false;
        if (!depends_on_state(depends_on_state, id) ||
            state_index.contains(id.value)) {
            return true;
        }
        const auto definition = uses.definition_block(id);
        if (!definition || *definition != header_id ||
            !cloneable_rotated_guard_value(function.values[id.value])) {
            return false;
        }
        if (!guard_seen.insert(id.value).second) return true;
        for (const auto operand : function.values[id.value].operands) {
            if (!self(self, operand)) return false;
        }
        guard_clone_order.push_back(id);
        return true;
    };
    if (!collect_guard(collect_guard, condition_id)) return false;

    // Validate the immutable use-list coordinates before allocating any new
    // values. Appending PHIs cannot invalidate these coordinates, so all
    // rewrites below are then non-failing and the transform is transactional
    // for every rejected input.
    for (const auto& state : states) {
        for (const auto& use : uses.uses(state.header)) {
            if (use.block != header_id &&
                !recorded_use_matches(function, use, state.header)) {
                return false;
            }
        }
    }

    const auto first_body_phi = function.values.size();
    for (std::size_t index = 0; index < states.size(); ++index) {
        states[index].body = ValueId{static_cast<std::uint32_t>(
            first_body_phi + index)};
    }
    const auto first_exit_phi = first_body_phi + states.size();
    for (std::size_t index = 0; index < states.size(); ++index) {
        states[index].exit = ValueId{static_cast<std::uint32_t>(
            first_exit_phi + index)};
    }
    const auto map_carried = [&](ValueId value) {
        const auto found = state_index.find(value.value);
        return found == state_index.end() ? value
                                          : states[found->second].body;
    };

    for (const auto& state : states) {
        ManagedValue phi;
        phi.id = state.body;
        phi.location = state.location;
        phi.type = state.type;
        phi.kind = ValueKind::Phi;
        phi.incoming = {{header_id, state.header},
                        {latch_id, map_carried(state.carried)}};
        function.values.push_back(std::move(phi));
    }
    for (const auto& state : states) {
        ManagedValue phi;
        phi.id = state.exit;
        phi.location = state.location;
        phi.type = state.type;
        phi.kind = ValueKind::Phi;
        phi.incoming = {{header_id, state.header},
                        {latch_id, map_carried(state.carried)}};
        function.values.push_back(std::move(phi));
    }

    // The use list predates the new PHIs, which is exactly what scoped
    // replacement needs: initial guard uses remain on the old state, body
    // uses receive the rotated recurrence, and live-outs receive exit PHIs.
    for (const auto& state : states) {
        for (const auto& use : uses.uses(state.header)) {
            if (use.block == header_id) continue;
            const auto replacement = loop.blocks.contains(use.block.value)
                ? state.body
                : state.exit;
            rewrite_recorded_use(function, use, state.header, replacement);
        }
    }

    std::vector<ValueId> rotated_values;
    std::unordered_map<std::uint32_t, ValueId> cloned_guard;
    const auto remap_guard_operand = [&](ValueId operand) {
        const auto state = state_index.find(operand.value);
        if (state != state_index.end()) {
            return map_carried(states[state->second].carried);
        }
        const auto clone = cloned_guard.find(operand.value);
        return clone == cloned_guard.end() ? operand : clone->second;
    };
    for (const auto source_id : guard_clone_order) {
        auto clone = function.values[source_id.value];
        clone.id = ValueId{
            static_cast<std::uint32_t>(function.values.size())};
        for (auto& operand : clone.operands) {
            operand = remap_guard_operand(operand);
        }
        const auto clone_id = clone.id;
        function.values.push_back(std::move(clone));
        rotated_values.push_back(clone_id);
        cloned_guard.emplace(source_id.value, clone_id);
    }
    const auto rotated_condition = remap_guard_operand(condition_id);

    auto& mutable_header = function.blocks[header_id.value];
    std::erase(mutable_header.predecessors, latch_id);
    for (const auto& state : states) {
        auto& phi = function.values[state.header.value];
        std::erase_if(phi.incoming, [&](const PhiIncoming& incoming) {
            return incoming.predecessor == latch_id;
        });
    }
    auto& mutable_header_effect =
        function.effects[mutable_header.effect.value];
    std::erase_if(
        mutable_header_effect.incoming,
        [&](const EffectIncoming& incoming) {
            return incoming.predecessor == latch_id;
        });

    auto& mutable_latch = function.blocks[latch_id.value];
    const auto latch_final_effect = mutable_latch.terminator.effect;
    mutable_latch.values.insert(mutable_latch.values.end(),
                                rotated_values.begin(),
                                rotated_values.end());
    mutable_latch.terminator = {
        TerminatorKind::ConditionalBranch,
        header_snapshot.terminator.location,
        rotated_condition,
        header_snapshot.terminator.successors,
        latch_final_effect};

    auto& mutable_body = function.blocks[body_id->value];
    mutable_body.predecessors.push_back(latch_id);
    std::vector<ValueId> body_values;
    body_values.reserve(states.size() + mutable_body.values.size());
    for (const auto& state : states) body_values.push_back(state.body);
    body_values.insert(body_values.end(), mutable_body.values.begin(),
                       mutable_body.values.end());
    mutable_body.values = std::move(body_values);

    auto& mutable_exit = function.blocks[exit_id->value];
    mutable_exit.predecessors.push_back(latch_id);
    std::vector<ValueId> exit_values;
    exit_values.reserve(states.size() + mutable_exit.values.size());
    for (const auto& state : states) exit_values.push_back(state.exit);
    exit_values.insert(exit_values.end(), mutable_exit.values.begin(),
                       mutable_exit.values.end());
    mutable_exit.values = std::move(exit_values);

    function.effects[mutable_body.effect.value].incoming.push_back(
        {latch_id, latch_final_effect});
    function.effects[mutable_exit.effect.value].incoming.push_back(
        {latch_id, latch_final_effect});
    return true;
}

// Removes legal forwarding blocks in the order of a cleanup that compacts and
// restarts its scan after each removal: always the lowest-numbered legal block
// next. Removals are applied in place and compacted once by the caller. A
// removal can change the legality only of the removed block's predecessors, its
// destination, and the definitions of values its own values used, so only those
// blocks are examined again; every other rejected block stays rejected.
class ForwardingSweep {
public:
    explicit ForwardingSweep(ManagedFunction& function)
        : function_(function), labeled_(function.blocks.size()),
          removed_(function.blocks.size()),
          definitions_(function.values.size()),
          other_uses_(function.values.size()),
          phi_users_(function.values.size()) {
        for (const auto& label : function.labels)
            if (label.block.value < labeled_.size())
                labeled_[label.block.value] = true;
        // The same uses as UseLists: values listed in blocks and terminators.
        for (const auto& block : function.blocks) {
            pending_.insert(block.id.value);
            for (const auto id : block.values) {
                if (id.value >= function.values.size())
                    continue;
                definitions_[id.value] = block.id;
                const auto& value = function.values[id.value];
                for (const auto operand : value.operands)
                    count_use(operand, true);
                for (const auto& argument : value.call_arguments)
                    if (argument.value)
                        count_use(*argument.value, true);
                for (const auto& incoming : value.incoming)
                    if (incoming.value.value < phi_users_.size())
                        phi_users_[incoming.value.value].push_back(id);
            }
            if (block.terminator.value)
                count_use(*block.terminator.value, true);
        }
    }

    // Removes up to `limit` blocks and returns how many were removed.
    std::size_t run(std::size_t limit) {
        std::size_t count = 0;
        while (count < limit && !pending_.empty()) {
            const BlockId id{*pending_.begin()};
            pending_.erase(pending_.begin());
            if (removed_[id.value])
                continue;
            Translation translated;
            if (!legal(function_.blocks[id.value], translated))
                continue;
            remove(id, translated);
            ++count;
        }
        return count;
    }

private:
    using Translation =
        std::unordered_map<std::uint32_t,
                           std::unordered_map<std::uint32_t, ValueId>>;

    void count_use(ValueId id, bool add) {
        if (id.value >= other_uses_.size())
            return;
        if (add)
            ++other_uses_[id.value];
        else
            --other_uses_[id.value];
    }

    void revisit(ValueId id) {
        if (id.value < definitions_.size() && definitions_[id.value])
            pending_.insert(definitions_[id.value]->value);
    }

    // Every use of `id` is an incoming value of a `target` phi on the edge
    // from `forwarding`. Users in removed blocks no longer exist; a listed
    // user that no longer names `id` is stale.
    bool only_forwarded(ValueId id, BlockId forwarding,
                        const ManagedBlock& target) const {
        if (other_uses_[id.value] != 0)
            return false;
        for (const auto user : phi_users_[id.value]) {
            const auto owner = definitions_[user.value];
            if (owner && removed_[owner->value])
                continue;
            const auto& phi = function_.values[user.value];
            bool member = false;
            for (const auto& edge : phi.incoming) {
                if (edge.value != id)
                    continue;
                if (!member) {
                    if (phi.kind != ValueKind::Phi ||
                        std::find(target.values.begin(), target.values.end(),
                                  user) == target.values.end())
                        return false;
                    member = true;
                }
                if (edge.predecessor != forwarding)
                    return false;
            }
        }
        return true;
    }

    bool legal(const ManagedBlock& forwarding, Translation& translated) const {
        const auto& function = function_;
        if (forwarding.id == function.entry ||
            forwarding.terminator.kind != TerminatorKind::Branch ||
            forwarding.terminator.successors.size() != 1 ||
            forwarding.predecessors.empty() || labeled_[forwarding.id.value] ||
            !std::all_of(forwarding.values.begin(), forwarding.values.end(),
                         [&](ValueId id) {
                             const auto kind = function.values[id.value].kind;
                             return kind == ValueKind::Phi ||
                                    kind == ValueKind::LifetimeStart ||
                                    kind == ValueKind::LifetimeEnd;
                         })) {
            return false;
        }

        const auto destination = forwarding.terminator.successors.front();
        if (destination == forwarding.id ||
            destination.value >= function.blocks.size()) {
            return false;
        }
        const auto& target = function.blocks[destination.value];
        if (std::find(target.predecessors.begin(), target.predecessors.end(),
                      forwarding.id) == target.predecessors.end() ||
            std::any_of(
                forwarding.predecessors.begin(), forwarding.predecessors.end(),
                [&](BlockId predecessor) {
                    return std::find(target.predecessors.begin(),
                                     target.predecessors.end(),
                                     predecessor) != target.predecessors.end();
                })) {
            return false;
        }

        // Translate values crossing the forwarding block before changing the
        // CFG. A phi may be discarded only when every use is a destination
        // phi on the edge being replaced.
        for (const auto id : forwarding.values) {
            const auto& value = function.values[id.value];
            if (value.kind != ValueKind::Phi)
                continue;
            auto& incoming = translated[id.value];
            for (const auto& edge : value.incoming) {
                if (!incoming.emplace(edge.predecessor.value, edge.value)
                         .second)
                    return false;
            }
            if (incoming.size() != forwarding.predecessors.size() ||
                std::any_of(forwarding.predecessors.begin(),
                            forwarding.predecessors.end(),
                            [&](BlockId predecessor) {
                                return !incoming.contains(predecessor.value);
                            }) ||
                !only_forwarded(id, forwarding.id, target))
                return false;
        }

        const auto target_effect_id = target.effect;
        if (target_effect_id.value >= function.effects.size())
            return false;
        const auto& target_effect = function.effects[target_effect_id.value];
        if (std::count_if(target_effect.incoming.begin(),
                          target_effect.incoming.end(),
                          [&](const EffectIncoming& incoming) {
                              return incoming.predecessor == forwarding.id;
                          }) != 1) {
            return false;
        }

        for (const auto predecessor : forwarding.predecessors) {
            if (predecessor.value >= function.blocks.size())
                return false;
            const auto& owner = function.blocks[predecessor.value];
            if (std::count(owner.terminator.successors.begin(),
                           owner.terminator.successors.end(),
                           forwarding.id) != 1)
                return false;
        }
        return true;
    }

    void remove(BlockId forwarding_id, const Translation& translated) {
        auto& function = function_;
        const auto destination =
            function.blocks[forwarding_id.value].terminator.successors.front();
        const auto old_predecessors =
            function.blocks[forwarding_id.value].predecessors;
        for (const auto predecessor : old_predecessors) {
            auto& successors =
                function.blocks[predecessor.value].terminator.successors;
            std::replace(successors.begin(), successors.end(), forwarding_id,
                         destination);
        }

        auto& mutable_target = function.blocks[destination.value];
        const auto predecessor_position =
            std::find(mutable_target.predecessors.begin(),
                      mutable_target.predecessors.end(), forwarding_id);
        const auto predecessor_offset = static_cast<std::size_t>(
            predecessor_position - mutable_target.predecessors.begin());
        mutable_target.predecessors.erase(predecessor_position);
        mutable_target.predecessors.insert(
            mutable_target.predecessors.begin() +
                static_cast<std::ptrdiff_t>(predecessor_offset),
            old_predecessors.begin(), old_predecessors.end());

        auto& mutable_effect = function.effects[mutable_target.effect.value];
        const auto effect_position = std::find_if(
            mutable_effect.incoming.begin(), mutable_effect.incoming.end(),
            [&](const EffectIncoming& incoming) {
                return incoming.predecessor == forwarding_id;
            });
        const auto effect_offset = static_cast<std::size_t>(
            effect_position - mutable_effect.incoming.begin());
        mutable_effect.incoming.erase(effect_position);
        std::vector<EffectIncoming> replacement_effects;
        replacement_effects.reserve(old_predecessors.size());
        for (const auto predecessor : old_predecessors) {
            replacement_effects.push_back(
                {predecessor,
                 function.blocks[predecessor.value].terminator.effect});
        }
        mutable_effect.incoming.insert(
            mutable_effect.incoming.begin() +
                static_cast<std::ptrdiff_t>(effect_offset),
            replacement_effects.begin(), replacement_effects.end());

        for (const auto id : mutable_target.values) {
            auto& phi = function.values[id.value];
            if (phi.kind != ValueKind::Phi)
                continue;
            const auto incoming =
                std::find_if(phi.incoming.begin(), phi.incoming.end(),
                             [&](const PhiIncoming& edge) {
                                 return edge.predecessor == forwarding_id;
                             });
            if (incoming == phi.incoming.end())
                continue;
            const auto incoming_offset =
                static_cast<std::size_t>(incoming - phi.incoming.begin());
            const auto incoming_value = incoming->value;
            phi.incoming.erase(incoming);
            std::vector<PhiIncoming> replacements;
            replacements.reserve(old_predecessors.size());
            for (const auto predecessor : old_predecessors) {
                auto value = incoming_value;
                if (const auto found = translated.find(incoming_value.value);
                    found != translated.end()) {
                    value = found->second.at(predecessor.value);
                }
                replacements.push_back({predecessor, value});
                if (value.value < phi_users_.size())
                    phi_users_[value.value].push_back(id);
            }
            phi.incoming.insert(
                phi.incoming.begin() +
                    static_cast<std::ptrdiff_t>(incoming_offset),
                replacements.begin(), replacements.end());
        }

        // The forwarding block is now unreachable and its values are gone.
        removed_[forwarding_id.value] = true;
        const auto& forwarding = function.blocks[forwarding_id.value];
        for (const auto id : forwarding.values) {
            const auto& value = function.values[id.value];
            for (const auto operand : value.operands) {
                count_use(operand, false);
                revisit(operand);
            }
            for (const auto& argument : value.call_arguments) {
                if (!argument.value)
                    continue;
                count_use(*argument.value, false);
                revisit(*argument.value);
            }
            for (const auto& incoming : value.incoming)
                revisit(incoming.value);
        }
        if (forwarding.terminator.value) {
            count_use(*forwarding.terminator.value, false);
            revisit(*forwarding.terminator.value);
        }
        for (const auto predecessor : old_predecessors)
            pending_.insert(predecessor.value);
        pending_.insert(destination.value);
    }

    ManagedFunction& function_;
    std::vector<bool> labeled_;
    std::vector<bool> removed_;
    std::vector<std::optional<BlockId>> definitions_;
    // Operand, call-argument, and terminator uses by values still present.
    std::vector<std::uint32_t> other_uses_;
    // Values naming each value as a phi input, possibly stale or repeated.
    std::vector<std::vector<ValueId>> phi_users_;
    std::set<std::uint32_t> pending_;
};

} // namespace

bool canonicalize_bitwise_operations(ManagedFunction& function,
                                     const hir::Module& hir_module) {
    return canonicalize_bitwise_operations_impl(function, hir_module);
}

bool narrow_bitwise_values(ManagedFunction& function,
                           hir::Module& hir_module,
                           const TargetInfo& target) {
    const bool factored =
        factor_zero_extended_bitwise_chains_impl(function, hir_module);
    const bool commuted =
        commute_masked_truncations(function, hir_module);
    const bool narrowed =
        narrow_single_use_integer_loads(function, hir_module, target);
    return factored || commuted || narrowed;
}

bool factor_zero_extended_bitwise_chains(
    ManagedFunction& function, const hir::Module& hir_module) {
    return factor_zero_extended_bitwise_chains_impl(function, hir_module);
}

void ValueReplacements::add(ValueId from, ValueId to) {
    to = resolve(to);
    if (to != from) targets_[from.value] = to;
}

ValueId ValueReplacements::resolve(ValueId id) {
    auto target = id;
    for (auto found = targets_.find(target.value); found != targets_.end();
         found = targets_.find(target.value))
        target = found->second;
    for (auto found = targets_.find(id.value); found != targets_.end();
         found = targets_.find(id.value)) {
        id = found->second;
        found->second = target;
    }
    return target;
}

void ValueReplacements::rewrite(ManagedValue& value) {
    if (targets_.empty()) return;
    for (auto& operand : value.operands) operand = resolve(operand);
    for (auto& argument : value.call_arguments)
        if (argument.value) argument.value = resolve(*argument.value);
    for (auto& incoming : value.incoming) incoming.value = resolve(incoming.value);
}

void ValueReplacements::apply(ManagedFunction& function) {
    if (targets_.empty()) return;
    for (auto& value : function.values) rewrite(value);
    for (auto& block : function.blocks)
        if (block.terminator.value) block.terminator.value = resolve(*block.terminator.value);
}

void compact_managed_values(ManagedFunction& function) {
    std::vector<bool> used(function.values.size());
    for (const auto& block : function.blocks) {
        for (const auto value : block.values) used[value.value] = true;
    }
    std::vector<std::optional<ValueId>> remap(function.values.size());
    std::vector<ManagedValue> values;
    values.reserve(function.values.size());
    for (std::size_t index = 0; index < function.values.size(); ++index) {
        if (!used[index]) continue;
        const ValueId id{static_cast<std::uint32_t>(values.size())};
        remap[index] = id;
        auto value = function.values[index];
        value.id = id;
        values.push_back(std::move(value));
    }
    const auto map = [&](ValueId id) {
        return *remap[id.value];
    };
    for (auto& value : values) {
        for (auto& operand : value.operands) operand = map(operand);
        for (auto& argument : value.call_arguments) {
            if (argument.value) argument.value = map(*argument.value);
        }
        for (auto& incoming : value.incoming) {
            incoming.value = map(incoming.value);
        }
    }
    for (auto& effect : function.effects) {
        if (effect.operation) effect.operation = map(*effect.operation);
    }
    for (auto& block : function.blocks) {
        for (auto& value : block.values) value = map(value);
        if (block.terminator.value) {
            block.terminator.value = map(*block.terminator.value);
        }
    }
    for (auto& parameter : function.parameters) parameter = map(parameter);
    function.values = std::move(values);
}

bool factor_common_phi_tails(ManagedFunction& function,
                             const hir::Module& hir_module) {
    const auto plans = collect_phi_tail_plans(function, hir_module);
    std::unordered_set<std::uint32_t> removed;
    bool changed = false;
    for (const auto& plan : plans) {
        changed = apply_phi_tail_plan(function, plan, removed) || changed;
    }
    if (!changed) return false;
    for (auto& block : function.blocks) {
        std::erase_if(block.values, [&](ValueId value) {
            return removed.contains(value.value);
        });
    }
    compact_managed_values(function);
    return true;
}

bool rotate_guarded_loops(ManagedFunction& function) {
    bool changed = false;
    // Each rotation rewires the latch and invalidates every CFG-derived
    // analysis. Rebuild before considering another loop; the original block
    // count is also a conservative progress bound for one invocation.
    const auto budget = function.blocks.size();
    for (std::size_t round = 0; round < budget; ++round) {
        const DominatorTree dominators(function);
        const LoopForest loops(function, dominators);
        const UseLists uses(function);
        bool rotated = false;
        for (const auto& loop : loops.loops()) {
            // Keep inner compact loops in their canonical form. Duplicating
            // their guards increases code size and regresses mixed workloads;
            // target block placement can still make their hot bodies and
            // backedges contiguous without changing MIR.
            if (loop.parent) continue;
            if (!rotate_one_guarded_loop(function, loop, uses)) continue;
            changed = true;
            rotated = true;
            break;
        }
        if (!rotated) break;
    }
    return changed;
}

bool eliminate_forwarding_blocks(ManagedFunction& function) {
    // The first removal sees the function as given, unreachable blocks
    // included, and compaction then drops every unreachable block. After
    // that a removal detaches only its own block, so the rest share one
    // compaction.
    if (ForwardingSweep(function).run(1) == 0)
        return false;
    prune_unreachable_blocks(function);
    if (ForwardingSweep(function).run(std::numeric_limits<std::size_t>::max()) != 0)
        prune_unreachable_blocks(function);
    return true;
}

} // namespace cross::mir
