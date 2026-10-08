// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/mir_transform.hpp"

#include <iostream>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace cross::mir;

bool expect(bool condition, std::string_view message) {
    if (condition) return true;
    std::cerr << "forwarding blocks test: " << message << '\n';
    return false;
}

cross::hir::Module test_hir() {
    using namespace cross;
    hir::Module module;
    hir::Type boolean;
    boolean.kind = hir::Type::Kind::Builtin;
    boolean.builtin = BuiltinType::Bool;
    module.types.push_back(boolean);
    hir::Type integer;
    integer.kind = hir::Type::Kind::Builtin;
    integer.builtin = BuiltinType::U64;
    module.types.push_back(integer);
    hir::Function source;
    source.id = {0};
    source.result_type = {1};
    source.ownership = hir::BodyOwnership::ManagedMir;
    hir::Parameter condition;
    condition.type = {0};
    condition.mode = ParameterMode::In;
    source.parameters.push_back(condition);
    module.functions.push_back(std::move(source));
    return module;
}

// Builds a function with a boolean parameter in the entry block. finish()
// derives predecessor lists and effect phis from the terminators.
struct Builder {
    ManagedFunction function;

    Builder() {
        function.result_type = {1};
        function.entry = {0};
        block();
        const auto parameter = value(BlockId{0}, ValueKind::Parameter);
        function.values[parameter.value].type = {0};
        function.parameters = {parameter};
    }

    BlockId block() {
        ManagedBlock result;
        result.id = {static_cast<std::uint32_t>(function.blocks.size())};
        function.blocks.push_back(std::move(result));
        return function.blocks.back().id;
    }

    ValueId value(BlockId owner, ValueKind kind) {
        ManagedValue result;
        result.id = {static_cast<std::uint32_t>(function.values.size())};
        result.kind = kind;
        result.type = {1};
        function.values.push_back(std::move(result));
        function.blocks[owner.value].values.push_back(function.values.back().id);
        return function.values.back().id;
    }

    ValueId constant(BlockId owner, std::uint64_t integer) {
        const auto id = value(owner, ValueKind::ConstantInteger);
        function.values[id.value].integer = integer;
        return id;
    }

    ValueId phi(BlockId owner, std::vector<PhiIncoming> incoming) {
        const auto id = value(owner, ValueKind::Phi);
        function.values[id.value].incoming = std::move(incoming);
        return id;
    }

    void branch(BlockId owner, BlockId successor) {
        function.blocks[owner.value].terminator = {TerminatorKind::Branch, {}, std::nullopt, {successor}, {}};
    }

    void split(BlockId owner, BlockId taken, BlockId other) {
        function.blocks[owner.value].terminator = {
            TerminatorKind::ConditionalBranch, {}, function.parameters.front(), {taken, other}, {}};
    }

    void ret(BlockId owner, ValueId result) {
        function.blocks[owner.value].terminator = {TerminatorKind::Return, {}, result, {}, {}};
    }

    ManagedFunction finish() {
        for (auto& block : function.blocks) {
            ManagedEffect effect;
            effect.id = {static_cast<std::uint32_t>(function.effects.size())};
            effect.kind = block.id == function.entry ? EffectKind::Entry : EffectKind::Phi;
            function.effects.push_back(effect);
            block.effect = effect.id;
            if (block.terminator.effect.value == 0) block.terminator.effect = effect.id;
        }
        for (const auto& block : function.blocks) {
            for (const auto successor : block.terminator.successors) {
                function.blocks[successor.value].predecessors.push_back(block.id);
                function.effects[function.blocks[successor.value].effect.value].incoming.push_back(
                    {block.id, block.terminator.effect});
            }
        }
        return function;
    }
};

bool verified(const ManagedFunction& function, std::string_view name) {
    ManagedModule module;
    module.functions.push_back(function);
    module.definitions.insert(0);
    std::ostringstream text;
    cross::Diagnostics diagnostics(text);
    const auto hir = test_hir();
    const bool valid = verify(module, hir, diagnostics);
    if (!valid) std::cerr << name << ":\n" << text.str();
    return expect(valid, name);
}

// `if (c) { if (c) { ... body ... } }` with one variable merged at every join.
// Every join forwards to the next outer one; removing them in order must give
// the exit one predecessor per condition block plus the body, with each phi
// input translated through the whole chain.
bool nested_joins() {
    constexpr std::uint32_t depth = 64;
    Builder builder;
    std::vector<BlockId> conditions{builder.function.entry};
    for (std::uint32_t index = 1; index < depth; ++index) conditions.push_back(builder.block());
    const auto body = builder.block();
    std::vector<BlockId> joins;
    for (std::uint32_t index = 0; index < depth; ++index) joins.push_back(builder.block());
    const auto exit = builder.block();
    const auto initial = builder.constant(builder.function.entry, 1);
    const auto updated = builder.constant(body, 2);
    for (std::uint32_t index = 0; index < depth; ++index)
        builder.split(conditions[index], index + 1 < depth ? conditions[index + 1] : body, joins[index]);
    builder.branch(body, joins.back());
    std::vector<ValueId> merged(depth);
    for (std::uint32_t index = depth; index-- > 0;) {
        const auto inner = index + 1 < depth ? PhiIncoming{joins[index + 1], merged[index + 1]}
                                             : PhiIncoming{body, updated};
        merged[index] = builder.phi(joins[index], {{conditions[index], initial}, inner});
        builder.branch(joins[index], index == 0 ? exit : joins[index - 1]);
    }
    const auto result = builder.phi(exit, {{joins.front(), merged.front()}});
    builder.ret(exit, result);
    auto function = builder.finish();

    bool ok = expect(eliminate_forwarding_blocks(function), "nested joins should be removed");
    ok &= expect(function.blocks.size() == depth + 2, "every join should be removed");
    if (!ok) return false;
    const auto& last = function.blocks.back();
    std::vector<BlockId> predecessors;
    std::vector<PhiIncoming> incoming;
    for (std::uint32_t index = 0; index <= depth; ++index) {
        predecessors.push_back({index});
        incoming.push_back({{index}, index < depth ? initial : updated});
    }
    ok &= expect(last.predecessors == predecessors, "exit predecessors should keep source order");
    ok &= expect(last.values.size() == 1 &&
                     function.values[last.values.front().value].incoming.size() == incoming.size(),
                 "exit phi should take one input per new predecessor");
    if (ok) {
        // Value IDs are compacted, so compare inputs through their constants.
        const auto& phi = function.values[last.values.front().value];
        for (std::size_t index = 0; index < incoming.size(); ++index) {
            const auto& value = function.values[phi.incoming[index].value.value];
            ok &= expect(phi.incoming[index].predecessor == incoming[index].predecessor &&
                             value.kind == ValueKind::ConstantInteger &&
                             value.integer == (index < depth ? 1U : 2U),
                         "exit phi input should be translated through every join");
        }
    }
    for (std::uint32_t index = 0; index < depth; ++index) {
        const auto& successors = function.blocks[index].terminator.successors;
        ok &= expect(successors.size() == 2 && successors[1] == last.id,
                     "each condition should branch straight to the exit");
    }
    return ok && verified(function, "nested joins should satisfy MIR verification");
}

// Removing block 4 makes the lower-numbered block 3 removable: its phi was
// also used on block 4's outgoing edge. A one-at-a-time cleanup restarts its
// scan and removes block 3 next; the sweep must examine it again too.
bool earlier_block_revisited() {
    Builder builder;
    const auto left = builder.block(), right = builder.block();
    const auto upper = builder.block(), lower = builder.block(), exit = builder.block();
    builder.split(builder.function.entry, left, right);
    const auto one = builder.constant(left, 1), two = builder.constant(right, 2);
    builder.branch(left, upper);
    builder.branch(right, upper);
    const auto first = builder.phi(upper, {{left, one}, {right, two}});
    builder.branch(upper, lower);
    const auto second = builder.phi(lower, {{upper, first}});
    builder.branch(lower, exit);
    builder.phi(exit, {{lower, first}});
    const auto result = builder.phi(exit, {{lower, second}});
    builder.ret(exit, result);
    auto function = builder.finish();

    bool ok = expect(eliminate_forwarding_blocks(function), "a forwarding block should be removed");
    ok &= expect(function.blocks.size() == 4 &&
                     function.blocks[1].terminator.successors == std::vector<BlockId>{{3}} &&
                     function.blocks[2].terminator.successors == std::vector<BlockId>{{3}} &&
                     function.blocks[3].predecessors == std::vector<BlockId>{{1}, {2}},
                 "both forwarding blocks should be removed, the earlier one second");
    for (const auto id : function.blocks.back().values) {
        const auto& phi = function.values[id.value];
        ok &= expect(phi.incoming.size() == 2 &&
                         function.values[phi.incoming[0].value.value].integer == 1 &&
                         function.values[phi.incoming[1].value.value].integer == 2,
                     "exit phis should take the arm constants");
    }
    return ok && verified(function, "revisited forwarding should satisfy MIR verification");
}

// An empty diamond: removing one arm makes the entry a predecessor of the
// join, so the other arm must stay to keep the two edges distinct.
bool empty_diamond() {
    Builder builder;
    const auto left = builder.block(), right = builder.block(), join = builder.block();
    builder.split(builder.function.entry, left, right);
    builder.branch(left, join);
    builder.branch(right, join);
    const auto zero = builder.constant(builder.function.entry, 0);
    builder.ret(join, zero);
    auto function = builder.finish();

    bool ok = expect(eliminate_forwarding_blocks(function), "one diamond arm should be removed");
    ok &= expect(function.blocks.size() == 3 &&
                     function.blocks[0].terminator.successors == std::vector<BlockId>{{2}, {1}} &&
                     function.blocks[1].terminator.successors == std::vector<BlockId>{{2}} &&
                     function.blocks[2].predecessors == std::vector<BlockId>{{0}, {1}},
                 "the second arm must stay once the first is gone");
    return ok && verified(function, "diamond forwarding should satisfy MIR verification");
}

// Lifetime markers do not keep a block; the destination's effect phi then
// takes the predecessor's terminator effect.
bool lifetime_block() {
    Builder builder;
    const auto marker_block = builder.block(), exit = builder.block();
    builder.branch(builder.function.entry, marker_block);
    const auto marker = builder.value(marker_block, ValueKind::LifetimeEnd);
    builder.branch(marker_block, exit);
    const auto zero = builder.constant(builder.function.entry, 0);
    builder.ret(exit, zero);
    // The marker block's entry effect phi is effect 1 once finish() runs.
    ManagedEffect output;
    output.id = {3};
    output.kind = EffectKind::Operation;
    output.input = EffectId{1};
    output.operation = marker;
    builder.function.values[marker.value].effect_input = EffectId{1};
    builder.function.values[marker.value].effect_output = output.id;
    builder.function.blocks[marker_block.value].terminator.effect = output.id;
    auto function = builder.finish();
    function.effects.push_back(output);

    bool ok = expect(eliminate_forwarding_blocks(function), "a lifetime-only block should be removed");
    ok &= expect(function.blocks.size() == 2 && function.effects.size() == 2 &&
                     function.effects[function.blocks[1].effect.value].incoming.size() == 1 &&
                     function.effects[function.blocks[1].effect.value].incoming.front().effect ==
                         function.blocks[0].terminator.effect,
                 "the exit effect should come from the entry terminator");
    return ok && verified(function, "lifetime forwarding should satisfy MIR verification");
}

} // namespace

int main() {
    bool ok = nested_joins();
    ok &= earlier_block_revisited();
    ok &= empty_diamond();
    ok &= lifetime_block();
    return ok ? 0 : 1;
}
