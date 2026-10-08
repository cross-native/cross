// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/control_flow.hpp"
#include "middle/mir.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_set>
#include <vector>

namespace cross::mir {

std::optional<UInt128> unsigned_upper_bound_at_exit(const ManagedFunction& function,
                                                  ValueId value, BlockId at);
bool has_reachable_return(const ManagedFunction& function);

struct LocalAddress {
    SlotId slot;
    // Missing displacement means some part of this cell, not a proven write.
    std::optional<std::uint64_t> bytes;
    friend bool operator==(const LocalAddress&, const LocalAddress&) = default;
};

struct LocalPointerTargets {
    bool unknown{};
    std::vector<LocalAddress> addresses;
    [[nodiscard]] std::optional<LocalAddress> definite() const;
    friend bool operator==(const LocalPointerTargets&,
                           const LocalPointerTargets&) = default;
};

// Flow-sensitive local pointer cells, SSA copies, and CFG joins. This is a
// source-MIR analysis: no optimizer, ABI carrier, or physical register facts
// are required. Calls/unknown writes invalidate addressable pointer cells.
// Widening retains may-alias cell identity without guessing a displacement.
class LocalPointerAnalysis {
public:
    LocalPointerAnalysis(const ManagedFunction& function,
                         const hir::Module& module, const TargetInfo& target);
    [[nodiscard]] const LocalPointerTargets& targets(ValueId value) const;
private:
    std::vector<LocalPointerTargets> values_;
};

enum class UseKind : std::uint8_t {
    Operand,
    CallArgument,
    PhiIncoming,
    Terminator,
};

struct ValueUse {
    BlockId block;
    std::optional<ValueId> user;
    UseKind kind{UseKind::Operand};
    std::uint32_t index{};
};

// Reverse SSA edges and definition ownership. This is intentionally an
// analysis rather than fields embedded in ManagedValue: transformations can
// invalidate and rebuild it in one place after compacting value IDs.
class UseLists {
public:
    explicit UseLists(const ManagedFunction& function);

    [[nodiscard]] const std::vector<ValueUse>& uses(ValueId value) const;
    [[nodiscard]] std::optional<BlockId> definition_block(
        ValueId value) const;
    [[nodiscard]] std::size_t value_count() const { return uses_.size(); }

private:
    std::vector<std::vector<ValueUse>> uses_;
    std::vector<std::optional<BlockId>> definition_blocks_;
};

class DominatorTree {
public:
    explicit DominatorTree(const ManagedFunction& function);

    [[nodiscard]] bool reachable(BlockId block) const;
    [[nodiscard]] bool dominates(BlockId dominator, BlockId block) const;
    [[nodiscard]] std::optional<BlockId> immediate_dominator(
        BlockId block) const;
    [[nodiscard]] const std::vector<BlockId>& children(BlockId block) const;
    [[nodiscard]] std::size_t block_count() const { return children_.size(); }

private:
    Dominance dominance_;
    std::vector<std::vector<BlockId>> children_;
};

// A natural loop is formed by one or more backedges to the same dominating
// header. `preheader` is present only when the outside edge is a dedicated
// unconditional block, which is the form consumed by LICM/vectorization.
struct NaturalLoop {
    BlockId header;
    std::vector<BlockId> latches;
    std::unordered_set<std::uint32_t> blocks;
    std::optional<BlockId> preheader;
    std::optional<std::size_t> parent;
    std::vector<std::size_t> children;
};

struct CanonicalLoop {
    BlockId header;
    BlockId preheader;
    std::unordered_set<std::uint32_t> blocks;
};

class LoopForest {
public:
    LoopForest(const ManagedFunction& function,
               const DominatorTree& dominators);

    [[nodiscard]] const std::vector<NaturalLoop>& loops() const {
        return loops_;
    }
    [[nodiscard]] const std::vector<CanonicalLoop>& canonical_loops() const {
        return canonical_loops_;
    }
    [[nodiscard]] std::optional<std::size_t> innermost_loop(
        BlockId block) const;

private:
    std::vector<NaturalLoop> loops_;
    std::vector<CanonicalLoop> canonical_loops_;
};

enum class AnalysisKind : std::uint8_t {
    Uses = 1U << 0U,
    Dominators = 1U << 1U,
    Loops = 1U << 2U,
};

using AnalysisMask = std::uint8_t;

[[nodiscard]] constexpr AnalysisMask analysis_mask(AnalysisKind kind) {
    return static_cast<AnalysisMask>(kind);
}

class PreservedAnalyses {
public:
    [[nodiscard]] static constexpr PreservedAnalyses none() {
        return PreservedAnalyses{0};
    }
    [[nodiscard]] static constexpr PreservedAnalyses all() {
        return PreservedAnalyses{
            analysis_mask(AnalysisKind::Uses) |
            analysis_mask(AnalysisKind::Dominators) |
            analysis_mask(AnalysisKind::Loops)};
    }
    // Instruction/value changes leave analyses derived solely from the CFG
    // valid, but invalidate reverse SSA use lists.
    [[nodiscard]] static constexpr PreservedAnalyses cfg() {
        return PreservedAnalyses{
            analysis_mask(AnalysisKind::Dominators) |
            analysis_mask(AnalysisKind::Loops)};
    }

    [[nodiscard]] constexpr bool preserves(AnalysisKind kind) const {
        return (mask_ & analysis_mask(kind)) != 0;
    }

private:
    constexpr explicit PreservedAnalyses(AnalysisMask mask) : mask_(mask) {}
    AnalysisMask mask_{};
};

class FunctionAnalysisManager {
public:
    explicit FunctionAnalysisManager(const ManagedFunction& function)
        : function_(&function) {}

    [[nodiscard]] const UseLists& uses();
    [[nodiscard]] const DominatorTree& dominators();
    [[nodiscard]] const LoopForest& loops();
    [[nodiscard]] bool cached(AnalysisKind kind) const;
    void invalidate(PreservedAnalyses preserved);

private:
    const ManagedFunction* function_{};
    std::optional<UseLists> uses_;
    std::optional<DominatorTree> dominators_;
    std::optional<LoopForest> loops_;
};

} // namespace cross::mir
