// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/mir_transform.hpp"

#include "middle/mir_analysis.hpp"

#include <algorithm>
#include <optional>
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

bool eliminate_forwarding_blocks(ManagedFunction& function) {
    bool changed = false;
    while (eliminate_one_forwarding_block(function))
        changed = true;
    return changed;
}

} // namespace cross::mir
