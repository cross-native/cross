// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/mir_analysis.hpp"
#include "common/integer_semantics.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace cross::mir {

std::optional<LocalAddress> LocalPointerTargets::definite() const {
    if (!unknown && addresses.size() == 1 && addresses.front().bytes)
        return addresses.front();
    return std::nullopt;
}

namespace {

void join_targets(LocalPointerTargets& into, const LocalPointerTargets& from) {
    into.unknown |= from.unknown;
    for (const auto& address : from.addresses) {
        if (std::find(into.addresses.begin(), into.addresses.end(), address) == into.addresses.end())
            into.addresses.push_back(address);
    }
    std::sort(into.addresses.begin(), into.addresses.end(), [](const auto& a, const auto& b) {
        return a.slot.value != b.slot.value ? a.slot.value < b.slot.value : a.bytes < b.bytes;
    });
    // A loop may keep advancing a pointer. Bound exact displacements per cell
    // while preserving the cell as a possible source of uninitialized reads.
    std::vector<LocalAddress> widened;
    for (std::size_t begin = 0; begin < into.addresses.size();) {
        auto end = begin + 1;
        while (end < into.addresses.size() && into.addresses[end].slot == into.addresses[begin].slot) ++end;
        if (!into.addresses[begin].bytes || end - begin > 8)
            widened.push_back({into.addresses[begin].slot, std::nullopt});
        else for (auto index = begin; index < end; ++index) widened.push_back(into.addresses[index]);
        begin = end;
    }
    into.addresses = std::move(widened);
}

std::optional<IntegerType> local_integer_type(const hir::Module& module,
                                             const TargetInfo& target, hir::TypeId id) {
    const auto& type = module.type(id);
    if (type.kind != hir::Type::Kind::Builtin || type.builtin < BuiltinType::Bool ||
        type.builtin > BuiltinType::Uptr) return std::nullopt;
    const auto size = hir::layout_size(module, id, target);
    if (!size || *size > 16) return std::nullopt;
    const bool signed_type = type.builtin == BuiltinType::I8 || type.builtin == BuiltinType::I16 ||
        type.builtin == BuiltinType::I32 || type.builtin == BuiltinType::I64 ||
        type.builtin == BuiltinType::I128 || type.builtin == BuiltinType::Iptr;
    return IntegerType{static_cast<unsigned>(*size * 8), signed_type, type.builtin == BuiltinType::Bool};
}

std::optional<UInt128> local_integer_constant(const ManagedFunction& function,
    const hir::Module& module, const TargetInfo& target, ValueId id, unsigned depth = 0) {
    if (depth >= 32 || id.value >= function.values.size()) return std::nullopt;
    const auto& value = function.values[id.value];
    const auto type = local_integer_type(module, target, value.type);
    if (!type) return std::nullopt;
    if (value.kind == ValueKind::ConstantInteger)
        return mask_to(UInt128{value.integer, value.integer_high}, type->bits);
    if (value.operands.empty()) return std::nullopt;
    const auto left = local_integer_constant(function, module, target, value.operands[0], depth + 1);
    if (!left) return std::nullopt;
    if (value.kind == ValueKind::Cast) {
        const auto from = local_integer_type(module, target, function.values[value.operands[0].value].type);
        if (from) return convert_integer(*left, *from, *type);
    }
    IntegerOperation operation;
    UInt128 a = *left;
    UInt128 b;
    if (value.kind == ValueKind::Unary && value.unary == UnaryOperation::Negate) {
        operation = IntegerOperation::Subtract;
        a = {};
        b = *left;
    } else if (value.kind == ValueKind::Binary && value.operands.size() == 2 &&
               (value.binary == BinaryOperation::Add || value.binary == BinaryOperation::Subtract ||
                value.binary == BinaryOperation::Multiply)) {
        const auto right = local_integer_constant(function, module, target, value.operands[1], depth + 1);
        if (!right) return std::nullopt;
        b = *right;
        operation = value.binary == BinaryOperation::Add ? IntegerOperation::Add
            : value.binary == BinaryOperation::Subtract ? IntegerOperation::Subtract : IntegerOperation::Multiply;
    } else return std::nullopt;
    const auto result = checked_integer_operation(operation, a, b, *type);
    return result.error == IntegerError::None ? std::optional<UInt128>(result.value) : std::nullopt;
}

} // namespace

LocalPointerAnalysis::LocalPointerAnalysis(const ManagedFunction& function,
    const hir::Module& module, const TargetInfo& target) : values_(function.values.size()) {
    const LocalPointerTargets unknown{true, {}};
    using State = std::vector<LocalPointerTargets>;
    std::vector<State> outgoing(function.blocks.size(), State(function.slots.size()));
    std::vector<bool> reached(function.blocks.size());
    const auto pointer_type = [&](hir::TypeId type) {
        return module.type(type).kind == hir::Type::Kind::Pointer;
    };
    const auto shifted = [&](LocalPointerTargets result, ValueId base, ValueId index) {
        const auto& pointer = module.type(function.values[base.value].type);
        const auto scale = pointer.pointee ? hir::layout_size(module, *pointer.pointee, target) : std::nullopt;
        const auto integer = local_integer_constant(function, module, target, index);
        const auto type = local_integer_type(module, target, function.values[index.value].type);
        const bool negative = integer && type && integer_negative(*integer, *type);
        const auto magnitude = integer && type ? (negative ? mask_to(negate(*integer), type->bits) : *integer) : UInt128{};
        for (auto& address : result.addresses) {
            if (!address.bytes || !integer || !type || !scale || *scale == 0 || magnitude.high ||
                magnitude.low > std::numeric_limits<std::uint64_t>::max() / *scale) {
                address.bytes.reset();
                continue;
            }
            const auto delta = magnitude.low * *scale;
            if (negative ? delta > *address.bytes : delta > std::numeric_limits<std::uint64_t>::max() - *address.bytes)
                address.bytes.reset();
            else if (negative) *address.bytes -= delta;
            else *address.bytes += delta;
        }
        LocalPointerTargets normalized;
        join_targets(normalized, result);
        return normalized;
    };
    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& block : function.blocks) {
            State state;
            bool live = block.id == function.entry;
            if (live) state.assign(function.slots.size(), unknown);
            for (const auto predecessor : block.predecessors) {
                if (!reached[predecessor.value]) continue;
                if (!live) state = outgoing[predecessor.value];
                else for (std::size_t i = 0; i < state.size(); ++i)
                    join_targets(state[i], outgoing[predecessor.value][i]);
                live = true;
            }
            if (!live) continue;
            const auto memory_load = [&](const LocalPointerTargets& address) {
                LocalPointerTargets result;
                result.unknown = address.unknown;
                for (const auto& cell : address.addresses) {
                    if (cell.bytes == 0 && pointer_type(function.slots[cell.slot.value].type))
                        join_targets(result, state[cell.slot.value]);
                    else result.unknown = true;
                }
                return result;
            };
            for (const auto id : block.values) {
                const auto& value = function.values[id.value];
                LocalPointerTargets call_targets;
                if (value.kind == ValueKind::Call) {
                    for (const auto& argument : value.call_arguments) {
                        if (argument.value) join_targets(call_targets, targets(*argument.value));
                        if (argument.cell) join_targets(call_targets, state[argument.cell->value]);
                    }
                    // Unknown callees can copy any reachable address into a
                    // pointer cell or their pointer result. Retain those may
                    // targets even though no definite result can be proved.
                    bool expanded = true;
                    while (expanded) {
                        auto next = call_targets;
                        for (const auto& address : call_targets.addresses)
                            if (pointer_type(function.slots[address.slot.value].type))
                                join_targets(next, state[address.slot.value]);
                        expanded = next != call_targets;
                        call_targets = std::move(next);
                    }
                    call_targets.unknown = true;
                }
                if (pointer_type(value.type)) {
                    LocalPointerTargets result = unknown;
                    if (value.kind == ValueKind::SlotAddress && value.slot)
                        result = {false, {{*value.slot, 0}}};
                    else if (value.kind == ValueKind::Load && value.slot)
                        result = state[value.slot->value];
                    else if (value.kind == ValueKind::PointerLoad && !value.operands.empty())
                        result = memory_load(targets(value.operands[0]));
                    else if (value.kind == ValueKind::Cast && value.cast == CastOperation::Reinterpret &&
                             value.operands.size() == 1 && pointer_type(function.values[value.operands[0].value].type))
                        result = targets(value.operands[0]);
                    else if (value.kind == ValueKind::IndexedAddress && value.operands.size() == 2)
                        result = shifted(targets(value.operands[0]), value.operands[0], value.operands[1]);
                    else if (value.kind == ValueKind::Select && value.operands.size() == 3) {
                        result = targets(value.operands[1]);
                        join_targets(result, targets(value.operands[2]));
                    } else if (value.kind == ValueKind::Call) result = call_targets;
                    else if (value.kind == ValueKind::Phi) {
                        result = {};
                        for (const auto& incoming : value.incoming)
                            if (reached[incoming.predecessor.value]) join_targets(result, targets(incoming.value));
                    }
                    if ((value.kind == ValueKind::Load || value.kind == ValueKind::PointerLoad) &&
                        (value.is_volatile_access || module.type(value.type).is_volatile ||
                         module.type(value.type).is_atomic)) result.unknown = true;
                    auto merged = targets(id);
                    join_targets(merged, result);
                    if (merged != values_[id.value]) {
                        values_[id.value] = std::move(merged);
                        changed = true;
                    }
                }
                if ((value.kind == ValueKind::LifetimeStart || value.kind == ValueKind::LifetimeEnd) && value.slot)
                    state[value.slot->value] = unknown;
                else if (value.kind == ValueKind::Store && value.slot && !value.operands.empty())
                    state[value.slot->value] = targets(value.operands[0]);
                else if ((value.kind == ValueKind::PointerStore ||
                          (value.kind == ValueKind::Atomic && value.atomic != AtomicOperation::Load &&
                           value.atomic != AtomicOperation::ThreadFence && value.atomic != AtomicOperation::SignalFence)) &&
                         !value.operands.empty()) {
                    const auto& address = targets(value.operands[0]);
                    const auto exact = address.definite();
                    for (const auto& slot : function.slots) {
                        if (!pointer_type(slot.type)) continue;
                        const bool possible = std::any_of(address.addresses.begin(), address.addresses.end(),
                            [&](const auto& part) { return part.slot == slot.id; });
                        if (!possible && !(address.unknown && slot.address_taken)) continue;
                        const bool full_pointer = value.kind == ValueKind::PointerStore && !value.bit_field_region &&
                            value.operands.size() == 2 && pointer_type(function.values[value.operands[1].value].type);
                        if (full_pointer && exact && exact->slot == slot.id && exact->bytes == 0)
                            state[slot.id.value] = targets(value.operands[1]);
                        else {
                            // A partial or ambiguous overwrite may destroy the old
                            // value; never retain it as a must-alias fact.
                            state[slot.id.value].unknown = true;
                            if (full_pointer) join_targets(state[slot.id.value], targets(value.operands[1]));
                        }
                    }
                } else if (value.kind == ValueKind::Call) {
                    for (const auto& slot : function.slots)
                        if (slot.address_taken && pointer_type(slot.type)) join_targets(state[slot.id.value], call_targets);
                    for (const auto& argument : value.call_arguments)
                        if (argument.cell && pointer_type(function.slots[argument.cell->value].type))
                            join_targets(state[argument.cell->value], call_targets);
                }
            }
            if (!reached[block.id.value] || outgoing[block.id.value] != state) {
                reached[block.id.value] = true;
                outgoing[block.id.value] = std::move(state);
                changed = true;
            }
        }
    }
}

const LocalPointerTargets& LocalPointerAnalysis::targets(ValueId value) const {
    static const LocalPointerTargets unknown{true, {}};
    return value.value < values_.size() ? values_[value.value] : unknown;
}

bool has_reachable_return(const ManagedFunction& function) {
    if (function.entry.value >= function.blocks.size()) return true;
    std::vector<bool> seen(function.blocks.size());
    std::vector<BlockId> pending{function.entry};
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        if (id.value >= function.blocks.size()) return true;
        if (seen[id.value]) continue;
        seen[id.value] = true;
        const auto& terminator = function.blocks[id.value].terminator;
        if (terminator.kind == TerminatorKind::Return || terminator.kind == TerminatorKind::None ||
            terminator.kind == TerminatorKind::IndirectBranch) return true;
        pending.insert(pending.end(), terminator.successors.begin(), terminator.successors.end());
    }
    return false;
}

bool may_trap_floating(const ManagedValue& value, const ManagedFunction& function,
                       const hir::Module& hir_module,
                       const floating::Environment& environment) {
    if (environment.traps == 0) return false;
    switch (value.kind) {
    case ValueKind::Unary:
        if (value.unary == UnaryOperation::Negate) return false;
        break;
    case ValueKind::Cast:
        if (value.cast == CastOperation::Reinterpret) return false;
        break;
    case ValueKind::Binary:
    case ValueKind::Intrinsic:
    case ValueKind::MachineInstruction:
        break;
    default:
        return false;
    }
    const auto floating = [&](hir::TypeId id) {
        const auto* type = &hir_module.type(id);
        if (type->kind == hir::Type::Kind::Vector && type->element)
            type = &hir_module.type(*type->element);
        if (type->kind != hir::Type::Kind::Builtin) return false;
        switch (type->builtin) {
        case BuiltinType::F32:
        case BuiltinType::F64:
        case BuiltinType::F80:
        case BuiltinType::F128:
        case BuiltinType::Fptr: return true;
        default: return false;
        }
    };
    return floating(value.type) ||
           std::any_of(value.operands.begin(), value.operands.end(), [&](ValueId operand) {
               return floating(function.values[operand.value].type);
           });
}

std::optional<UInt128> unsigned_upper_bound_at_exit(const ManagedFunction& function,
                                                  ValueId value, BlockId at) {
    const DominatorTree dominance(function);
    std::optional<UInt128> bound;
    for (const auto& block : function.blocks) {
        if (!dominance.dominates(block.id, at)) continue;
        for (const auto id : block.values) {
            const auto& fact = function.values[id.value];
            if (fact.kind != ValueKind::Intrinsic || fact.intrinsic != IntrinsicOperation::Assume ||
                fact.binary != BinaryOperation::UnsignedLess || fact.operands.size() != 1 ||
                fact.operands.front() != value) continue;
            const UInt128 candidate{fact.integer, fact.integer_high};
            if (!bound || candidate < *bound) bound = candidate;
        }
    }
    return bound;
}
namespace {

const std::vector<ValueUse> empty_uses;
const std::vector<BlockId> empty_children;

bool strict_subset(const std::unordered_set<std::uint32_t>& left,
                   const std::unordered_set<std::uint32_t>& right) {
    if (left.size() >= right.size()) return false;
    return std::all_of(left.begin(), left.end(), [&](std::uint32_t block) {
        return right.contains(block);
    });
}

} // namespace

UseLists::UseLists(const ManagedFunction& function)
    : uses_(function.values.size()),
      definition_blocks_(function.values.size()) {
    const auto record = [&](ValueId used, BlockId block,
                            std::optional<ValueId> user, UseKind kind,
                            std::size_t index) {
        if (used.value >= uses_.size()) return;
        uses_[used.value].push_back(
            {block, user, kind, static_cast<std::uint32_t>(index)});
    };

    for (const auto& block : function.blocks) {
        for (const auto id : block.values) {
            if (id.value >= function.values.size()) continue;
            definition_blocks_[id.value] = block.id;
            const auto& value = function.values[id.value];
            for (std::size_t index = 0; index < value.operands.size(); ++index) {
                record(value.operands[index], block.id, id,
                       UseKind::Operand, index);
            }
            for (std::size_t index = 0;
                 index < value.call_arguments.size(); ++index) {
                if (value.call_arguments[index].value) {
                    record(*value.call_arguments[index].value, block.id, id,
                           UseKind::CallArgument, index);
                }
            }
            for (std::size_t index = 0; index < value.incoming.size(); ++index) {
                record(value.incoming[index].value,
                       value.incoming[index].predecessor, id,
                       UseKind::PhiIncoming, index);
            }
        }
        if (block.terminator.value) {
            record(*block.terminator.value, block.id, std::nullopt,
                   UseKind::Terminator, 0);
        }
    }
}

const std::vector<ValueUse>& UseLists::uses(ValueId value) const {
    return value.value < uses_.size() ? uses_[value.value] : empty_uses;
}

std::optional<BlockId> UseLists::definition_block(ValueId value) const {
    return value.value < definition_blocks_.size()
        ? definition_blocks_[value.value]
        : std::nullopt;
}

DominatorTree::DominatorTree(const ManagedFunction& function)
    : dominance_(function.blocks.size(), function.entry.value,
                 [&](std::uint32_t block) -> const std::vector<BlockId>& {
                     return function.blocks[block].terminator.successors;
                 },
                 [&](std::uint32_t block) -> const std::vector<BlockId>& {
                     return function.blocks[block].predecessors;
                 }),
      children_(function.blocks.size()) {
    for (std::uint32_t block = 0; block < children_.size(); ++block)
        for (const auto child : dominance_.children(block))
            children_[block].push_back(BlockId{child});
}

bool DominatorTree::reachable(BlockId block) const {
    return dominance_.reachable(block.value);
}

bool DominatorTree::dominates(BlockId dominator, BlockId block) const {
    return dominance_.dominates(dominator.value, block.value);
}

std::optional<BlockId> DominatorTree::immediate_dominator(
    BlockId block) const {
    const auto dominator = dominance_.immediate_dominator(block.value);
    return dominator ? std::optional<BlockId>(BlockId{*dominator}) : std::nullopt;
}

const std::vector<BlockId>& DominatorTree::children(BlockId block) const {
    return block.value < children_.size() ? children_[block.value]
                                          : empty_children;
}

LoopForest::LoopForest(const ManagedFunction& function,
                       const DominatorTree& dominators) {
    for (const auto& tail : function.blocks) {
        for (const auto header : tail.terminator.successors) {
            if (!dominators.dominates(header, tail.id)) continue;
            auto found = std::find_if(
                loops_.begin(), loops_.end(), [&](const NaturalLoop& loop) {
                    return loop.header == header;
                });
            if (found == loops_.end()) {
                NaturalLoop loop;
                loop.header = header;
                loops_.push_back(std::move(loop));
                found = std::prev(loops_.end());
            }
            found->latches.push_back(tail.id);
            std::unordered_set<std::uint32_t> members{
                header.value, tail.id.value};
            std::vector<BlockId> work;
            if (tail.id != header) work.push_back(tail.id);
            while (!work.empty()) {
                const auto item = work.back();
                work.pop_back();
                if (item.value >= function.blocks.size()) continue;
                for (const auto predecessor :
                     function.blocks[item.value].predecessors) {
                    if (predecessor.value < function.blocks.size() &&
                        members.insert(predecessor.value).second &&
                        predecessor != header) {
                        work.push_back(predecessor);
                    }
                }
            }
            found->blocks.insert(members.begin(), members.end());
        }
    }

    std::sort(loops_.begin(), loops_.end(),
              [](const NaturalLoop& left, const NaturalLoop& right) {
                  if (left.blocks.size() != right.blocks.size()) {
                      return left.blocks.size() < right.blocks.size();
                  }
                  return left.header.value < right.header.value;
              });

    for (auto& loop : loops_) {
        std::vector<BlockId> outside;
        if (loop.header.value < function.blocks.size()) {
            for (const auto predecessor :
                 function.blocks[loop.header.value].predecessors) {
                if (!loop.blocks.contains(predecessor.value)) {
                    outside.push_back(predecessor);
                }
            }
        }
        if (outside.size() == 1 &&
            outside.front().value < function.blocks.size()) {
            const auto candidate = outside.front();
            const auto& terminator =
                function.blocks[candidate.value].terminator;
            if (terminator.kind == TerminatorKind::Branch &&
                terminator.successors.size() == 1 &&
                terminator.successors.front() == loop.header) {
                loop.preheader = candidate;
                canonical_loops_.push_back(
                    {loop.header, candidate, loop.blocks});
            }
        }
    }

    for (std::size_t child = 0; child < loops_.size(); ++child) {
        std::optional<std::size_t> parent;
        for (std::size_t candidate = 0; candidate < loops_.size();
             ++candidate) {
            if (!strict_subset(loops_[child].blocks,
                               loops_[candidate].blocks)) {
                continue;
            }
            if (!parent || loops_[candidate].blocks.size() <
                               loops_[*parent].blocks.size()) {
                parent = candidate;
            }
        }
        loops_[child].parent = parent;
        if (parent) loops_[*parent].children.push_back(child);
    }
}

std::optional<std::size_t> LoopForest::innermost_loop(BlockId block) const {
    for (std::size_t index = 0; index < loops_.size(); ++index) {
        if (loops_[index].blocks.contains(block.value)) return index;
    }
    return std::nullopt;
}

const UseLists& FunctionAnalysisManager::uses() {
    if (!uses_) uses_.emplace(*function_);
    return *uses_;
}

const DominatorTree& FunctionAnalysisManager::dominators() {
    if (!dominators_) dominators_.emplace(*function_);
    return *dominators_;
}

const LoopForest& FunctionAnalysisManager::loops() {
    if (!loops_) loops_.emplace(*function_, dominators());
    return *loops_;
}

bool FunctionAnalysisManager::cached(AnalysisKind kind) const {
    switch (kind) {
    case AnalysisKind::Uses: return uses_.has_value();
    case AnalysisKind::Dominators: return dominators_.has_value();
    case AnalysisKind::Loops: return loops_.has_value();
    }
    return false;
}

void FunctionAnalysisManager::invalidate(PreservedAnalyses preserved) {
    if (!preserved.preserves(AnalysisKind::Uses)) uses_.reset();
    if (!preserved.preserves(AnalysisKind::Loops)) loops_.reset();
    if (!preserved.preserves(AnalysisKind::Dominators)) {
        // LoopForest stores a dependency on the dominator result used to
        // construct it, so it cannot survive independently.
        loops_.reset();
        dominators_.reset();
    }
}

} // namespace cross::mir
