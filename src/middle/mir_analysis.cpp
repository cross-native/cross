// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "middle/mir_analysis.hpp"

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace cross::mir {
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
    : reachable_(function.blocks.size()),
      dominators_(function.blocks.size(),
                  std::vector<std::uint64_t>(
                      (function.blocks.size() + 63U) / 64U)),
      immediate_dominators_(function.blocks.size()),
      children_(function.blocks.size()) {
    const auto count = function.blocks.size();
    if (function.entry.value >= count) return;

    std::vector<BlockId> pending{function.entry};
    while (!pending.empty()) {
        const auto block = pending.back();
        pending.pop_back();
        if (block.value >= count || reachable_[block.value]) continue;
        reachable_[block.value] = true;
        for (const auto successor :
             function.blocks[block.value].terminator.successors) {
            if (successor.value < count && !reachable_[successor.value]) {
                pending.push_back(successor);
            }
        }
    }

    std::vector<std::uint64_t> all((count + 63U) / 64U);
    for (std::size_t block = 0; block < count; ++block) {
        if (reachable_[block]) {
            all[block / 64U] |= std::uint64_t{1} << (block % 64U);
        }
    }
    for (std::size_t block = 0; block < count; ++block) {
        if (reachable_[block]) dominators_[block] = all;
    }
    std::fill(dominators_[function.entry.value].begin(),
              dominators_[function.entry.value].end(), 0);
    dominators_[function.entry.value][function.entry.value / 64U] |=
        std::uint64_t{1} << (function.entry.value % 64U);

    bool changed = true;
    while (changed) {
        changed = false;
        for (const auto& block : function.blocks) {
            if (block.id.value >= count || !reachable_[block.id.value] ||
                block.id == function.entry) {
                continue;
            }
            std::vector<std::uint64_t> next;
            bool saw_predecessor = false;
            for (const auto predecessor : block.predecessors) {
                if (predecessor.value >= count ||
                    !reachable_[predecessor.value]) {
                    continue;
                }
                if (!saw_predecessor) {
                    next = dominators_[predecessor.value];
                    saw_predecessor = true;
                } else {
                    for (std::size_t word = 0; word < next.size(); ++word) {
                        next[word] &= dominators_[predecessor.value][word];
                    }
                }
            }
            if (!saw_predecessor) next.assign(all.size(), 0);
            next[block.id.value / 64U] |=
                std::uint64_t{1} << (block.id.value % 64U);
            if (next != dominators_[block.id.value]) {
                dominators_[block.id.value] = std::move(next);
                changed = true;
            }
        }
    }

    for (const auto& block : function.blocks) {
        if (!reachable(block.id) || block.id == function.entry) continue;
        std::optional<BlockId> best;
        unsigned best_depth{};
        for (std::size_t candidate = 0; candidate < count; ++candidate) {
            const BlockId id{static_cast<std::uint32_t>(candidate)};
            if (id == block.id || !dominates(id, block.id)) continue;
            unsigned depth{};
            for (const auto word : dominators_[candidate]) {
                depth += static_cast<unsigned>(std::popcount(word));
            }
            if (!best || depth > best_depth) {
                best = id;
                best_depth = depth;
            }
        }
        immediate_dominators_[block.id.value] = best;
        if (best) children_[best->value].push_back(block.id);
    }
}

bool DominatorTree::bit(BlockId block, BlockId candidate) const {
    if (block.value >= dominators_.size() ||
        candidate.value >= reachable_.size()) {
        return false;
    }
    return (dominators_[block.value][candidate.value / 64U] &
            (std::uint64_t{1} << (candidate.value % 64U))) != 0;
}

bool DominatorTree::reachable(BlockId block) const {
    return block.value < reachable_.size() && reachable_[block.value];
}

bool DominatorTree::dominates(BlockId dominator, BlockId block) const {
    return reachable(dominator) && reachable(block) && bit(block, dominator);
}

std::optional<BlockId> DominatorTree::immediate_dominator(
    BlockId block) const {
    return block.value < immediate_dominators_.size()
        ? immediate_dominators_[block.value]
        : std::nullopt;
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
