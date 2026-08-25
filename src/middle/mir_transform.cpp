// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/mir_transform.hpp"

#include "middle/mir_analysis.hpp"

#include <algorithm>
#include <optional>
#include <unordered_map>
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

bool eliminate_one_forwarding_block(ManagedFunction& function) {
    const UseLists uses(function);
    for (const auto& forwarding : function.blocks) {
        if (forwarding.id == function.entry ||
            forwarding.terminator.kind != TerminatorKind::Branch ||
            forwarding.terminator.successors.size() != 1 ||
            forwarding.predecessors.empty() ||
            std::any_of(function.labels.begin(), function.labels.end(),
                        [&](const ManagedLabel& label) {
                            return label.block == forwarding.id;
                        }) ||
            !std::all_of(forwarding.values.begin(), forwarding.values.end(),
                         [&](ValueId id) {
                             const auto kind = function.values[id.value].kind;
                             return kind == ValueKind::Phi ||
                                    kind == ValueKind::LifetimeStart ||
                                    kind == ValueKind::LifetimeEnd;
                         })) {
            continue;
        }

        const auto destination = forwarding.terminator.successors.front();
        if (destination == forwarding.id ||
            destination.value >= function.blocks.size()) {
            continue;
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
            continue;
        }

        // Translate values crossing the forwarding block before changing the
        // CFG. A phi may be discarded only when every use is a destination
        // phi on the edge being replaced.
        std::unordered_map<std::uint32_t,
                           std::unordered_map<std::uint32_t, ValueId>>
            translated;
        bool valid = true;
        for (const auto id : forwarding.values) {
            const auto& value = function.values[id.value];
            if (value.kind != ValueKind::Phi)
                continue;
            auto& incoming = translated[id.value];
            for (const auto& edge : value.incoming) {
                if (!incoming.emplace(edge.predecessor.value, edge.value)
                         .second) {
                    valid = false;
                    break;
                }
            }
            if (!valid || incoming.size() != forwarding.predecessors.size() ||
                std::any_of(forwarding.predecessors.begin(),
                            forwarding.predecessors.end(),
                            [&](BlockId predecessor) {
                                return !incoming.contains(predecessor.value);
                            })) {
                valid = false;
                break;
            }
            for (const auto& use : uses.uses(id)) {
                if (use.kind != UseKind::PhiIncoming || !use.user ||
                    std::find(target.values.begin(), target.values.end(),
                              *use.user) == target.values.end()) {
                    valid = false;
                    break;
                }
                const auto& phi = function.values[use.user->value];
                if (phi.kind != ValueKind::Phi ||
                    use.index >= phi.incoming.size() ||
                    phi.incoming[use.index].predecessor != forwarding.id ||
                    phi.incoming[use.index].value != id) {
                    valid = false;
                    break;
                }
            }
            if (!valid)
                break;
        }
        if (!valid)
            continue;

        const auto target_effect_id = target.effect;
        if (target_effect_id.value >= function.effects.size())
            continue;
        const auto& target_effect = function.effects[target_effect_id.value];
        if (std::count_if(target_effect.incoming.begin(),
                          target_effect.incoming.end(),
                          [&](const EffectIncoming& incoming) {
                              return incoming.predecessor == forwarding.id;
                          }) != 1) {
            continue;
        }

        const auto forwarding_id = forwarding.id;
        const auto old_predecessors = forwarding.predecessors;
        for (const auto predecessor : old_predecessors) {
            if (predecessor.value >= function.blocks.size()) {
                valid = false;
                break;
            }
            const auto& owner = function.blocks[predecessor.value];
            if (std::count(owner.terminator.successors.begin(),
                           owner.terminator.successors.end(),
                           forwarding_id) != 1) {
                valid = false;
                break;
            }
        }
        if (!valid)
            continue;

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

        auto& mutable_effect = function.effects[target_effect_id.value];
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
            }
            phi.incoming.insert(
                phi.incoming.begin() +
                    static_cast<std::ptrdiff_t>(incoming_offset),
                replacements.begin(), replacements.end());
        }

        // The forwarding block is now unreachable. Reuse the central
        // compactor so block, value, effect, and label identities remain
        // dense for every serializer and target backend.
        prune_unreachable_blocks(function);
        return true;
    }
    return false;
}

} // namespace

bool canonicalize_bitwise_operations(ManagedFunction& function,
                                     const hir::Module& hir_module) {
    return canonicalize_bitwise_operations_impl(function, hir_module);
}

bool eliminate_forwarding_blocks(ManagedFunction& function) {
    bool changed = false;
    while (eliminate_one_forwarding_block(function))
        changed = true;
    return changed;
}

} // namespace cross::mir
