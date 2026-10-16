// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/mir_analysis.hpp"

#include <span>
#include <unordered_map>

namespace cross::mir {

// Target-independent canonical forms consumed by every native target and by
// the optional debug serializers. These entry points are intentionally small:
// pass ordering and policy remain owned by optimize().
[[nodiscard]] bool
canonicalize_bitwise_operations(ManagedFunction& function,
                                const hir::Module& hir_module);
[[nodiscard]] bool narrow_bitwise_values(
    ManagedFunction& function, hir::Module& hir_module,
    const TargetInfo& target);
// Combine equal-width narrow operands before one shared zero extension. This
// preserves exact bitwise semantics while shortening wide XOR/OR/AND chains.
[[nodiscard]] bool factor_zero_extended_bitwise_chains(
    ManagedFunction& function, const hir::Module& hir_module);
[[nodiscard]] bool eliminate_forwarding_blocks(ManagedFunction& function);
// Merges each block into its predecessor when that is the block's only
// predecessor and the block its only successor.
[[nodiscard]] bool merge_block_chains(ManagedFunction& function);
[[nodiscard]] bool factor_common_phi_tails(
    ManagedFunction& function, const hir::Module& hir_module);
// Reuse an existing unsigned `index + 1` latch value in expressions shaped
// as `(base + index) + 1`. This is modular integer algebra, but deliberately
// requires a canonical unit recurrence so the rewrite removes an add instead
// of merely changing evaluation order or increasing register pressure.
[[nodiscard]] bool reassociate_unit_recurrence_adds(
    ManagedFunction& function, const hir::Module& hir_module,
    std::span<const CanonicalLoop> loops, const UseLists& use_lists);
// Convert a legal outermost top-tested natural loop into an initial zero-trip
// guard and a bottom-tested loop. The transform repairs value SSA and effect
// SSA and deliberately rejects secondary exits, multiple latches, compact
// self-latches, and addressable blocks rather than weakening those invariants.
[[nodiscard]] bool rotate_guarded_loops(ManagedFunction& function);

// Lowers each bit intrinsic and rotation to the target's instructions or to
// an inline sequence, whichever the subtarget's costs favor; afterwards the
// target lowers every remaining one itself.
[[nodiscard]] bool lower_bit_operations(ManagedFunction& function,
                                        const hir::Module& hir_module,
                                        const Subtarget& subtarget);

// Value replacements collected by one sweep and applied to every use at once,
// instead of rescanning the function per replaced value. Chains resolve to
// their final value. A sweep that inspects operands rewrites each value with
// `rewrite` before inspecting it, which sees every replacement made so far.
class ValueReplacements {
public:
    void add(ValueId from, ValueId to);
    [[nodiscard]] bool empty() const { return targets_.empty(); }
    [[nodiscard]] ValueId resolve(ValueId id);
    void rewrite(ManagedValue& value);
    void apply(ManagedFunction& function);

private:
    std::unordered_map<std::uint32_t, ValueId> targets_;
};

// Keep canonical dense value IDs after a transformation removes definitions.
// The helper rewrites every value-bearing MIR edge, including effect owners,
// parameter lists, phi inputs, and terminators.
void compact_managed_values(ManagedFunction& function);

// Removes effectful values whose uses were already replaced and reconnects
// the effect chain around them.
void remove_effectful_values(ManagedFunction& function,
                             const std::unordered_set<std::uint32_t>& removed);

// Replaces a non-volatile load of a location whose contents are known on
// every path, from a dominating store or load and no intervening effect
// that may change it, with that value.
[[nodiscard]] bool forward_memory_values(ManagedFunction& function,
                                         const hir::Module& hir_module,
                                         const TargetInfo& target);

// The initial bytes of the const objects defined in the compilation group
// whose contents no store, loader, or patcher may change.
class ConstantObjects {
public:
    ConstantObjects(const hir::Module& hir_module,
                    const data::Module& data_module,
                    const CompilerOptions& options);
    // The `bytes` bytes at `offset` as an integer in the target byte order,
    // unless they are unknown or hold a relocation.
    [[nodiscard]] std::optional<UInt128> read(hir::ObjectId object,
                                              std::int64_t offset,
                                              unsigned bytes) const;

private:
    struct Image {
        std::vector<unsigned char> bytes;
        std::vector<data::Relocation> relocations;
    };
    ByteOrder byte_order_;
    std::unordered_map<std::uint32_t, Image> images_;
};

// Replaces non-volatile scalar loads from constant offsets of such objects
// with constants.
[[nodiscard]] bool fold_constant_object_reads(ManagedFunction& function,
                                              const hir::Module& hir_module,
                                              const ConstantObjects& objects,
                                              const TargetInfo& target);

// Shared identity-preserving cleanup used by CFG transforms after they make
// one or more non-addressable blocks unreachable.
void prune_unreachable_blocks(ManagedFunction& function);

} // namespace cross::mir
