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
// An integer conversion of a `source_bits`-wide constant.
[[nodiscard]] std::optional<UInt128> fold_integer_cast(CastOperation cast,
                                                       UInt128 value,
                                                       unsigned source_bits,
                                                       unsigned target_bits);
// The exact result of an integer operation on `bits`-wide constants (the
// operand width for a comparison), or nothing when the operation is
// undefined for them or not folded. Shift counts are reduced modulo the
// width.
[[nodiscard]] std::optional<UInt128> fold_integer_binary(
    BinaryOperation operation, UInt128 left, UInt128 right, unsigned bits);

// Conservative high-zero-bit proof through integer operations, casts and phis.
[[nodiscard]] unsigned known_leading_zeros(const ManagedFunction& function,
                                          const hir::Module& hir_module,
                                          ValueId value);

// Convert a legal top-tested natural loop into an initial zero-trip guard
// and a bottom-tested loop. The transform repairs value SSA and effect SSA
// and deliberately rejects secondary exits, multiple latches, and
// addressable blocks rather than weakening those invariants. An inner loop,
// or one whose body is its latch, rotates only when its first test, given
// the constant values its header phis enter with, folds an operation:
// duplicating a test that does not specialize costs code without saving
// work. Under a size goal a loop rotates only when that first test folds to
// a constant, so that no guard remains.
[[nodiscard]] bool rotate_guarded_loops(ManagedFunction& function,
                                        const hir::Module& hir_module,
                                        bool size_goal = false);

// Folds a branch that the branch edges dominating it decide, and threads an
// edge into a join past the branches that the path from it decides, through
// copies of the path's blocks that keep at most `limit` operations. Integer
// bounds and relations come from constants, arithmetic, masks, shifts, and
// conversions, from the comparisons of the edges, and from the values phis
// take on the path. A thread keeps the loops it starts in and enters no loop
// body but leaves a loop it enters at its header.
[[nodiscard]] bool thread_decided_branches(ManagedFunction& function,
                                           const hir::Module& hir_module,
                                           unsigned limit);

// Lowers each bit intrinsic and rotation to the target's instructions or to
// an inline sequence, whichever the subtarget's costs favor; afterwards the
// target lowers every remaining one itself.
[[nodiscard]] bool lower_bit_operations(ManagedFunction& function,
                                        const hir::Module& hir_module,
                                        const Subtarget& subtarget);

// Splits each integer twice as wide as the target's `selected_integer_bits`
// into operations on its two halves, as inline sequences: division and
// remainder become a loop. Parameters and call results stay wide and are
// split after their definitions, and call arguments and returned values are
// joined before their uses, so only those boundary forms stay wide.
bool expand_wide_integers(ManagedFunction& function, hir::Module& hir_module,
                          const Subtarget& subtarget, Diagnostics& diagnostics);

// The widest native vector that the subtarget and -mprefer-vector-width
// allow for at least two lanes of `element_bits`, or zero.
[[nodiscard]] unsigned preferred_vector_bits(const Subtarget& subtarget,
                                             const CompilerOptions& options,
                                             bool floating, unsigned element_bits);

// A part of an aggregate that one register-wide access moves: an unsigned
// integer, or a vector of u64 lanes.
struct MemoryUnit {
    std::uint64_t offset{};
    unsigned bytes{};
    bool vector{};
};

// The units that cover `size` bytes with the fewest instructions per byte
// that the target's memory accesses allow, preferring naturally aligned
// accesses on a tie: stores at `store_alignment` and, unless absent, loads
// at `load_alignment`, both the alignment of offset zero.
[[nodiscard]] std::vector<MemoryUnit> plan_memory_units(
    std::uint64_t size, std::optional<unsigned> load_alignment,
    unsigned store_alignment, const Subtarget& subtarget,
    const CompilerOptions& options);
// The type through which a unit is accessed, which may alias any object.
[[nodiscard]] hir::TypeId memory_unit_type(hir::Module& module, const MemoryUnit& unit);
// How many equal units a copy or fill repeats in straight-line code before
// a loop is shorter (size goals) or the code grows too large (speed goals).
[[nodiscard]] unsigned straight_unit_limit(const CompilerOptions& options);

// Copies the records that only stores consume as units, without the
// temporary that holds a record value (-ftree-sra).
bool copy_records_by_units(ManagedFunction& function, hir::Module& module,
                           const Subtarget& subtarget, const CompilerOptions& options);

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

// Removes the non-volatile stores whose bytes later stores of their block
// write before any operation may read them.
bool eliminate_overwritten_stores(ManagedFunction& function,
                                  const hir::Module& hir_module,
                                  const TargetInfo& target);

// Moves a non-volatile store to a slot or object at a constant offset out of
// a natural loop when it runs before every exit and no other memory operation
// of the loop may access its bytes; a copy runs on each exit edge instead.
// Calls, atomics, volatile accesses, machine instructions, patch values, and
// floating operations that may raise a trap `environment` enables keep the
// stores of their loop.
[[nodiscard]] bool sink_loop_stores(ManagedFunction& function,
                                    const hir::Module& hir_module,
                                    const TargetInfo& target,
                                    const floating::Environment& environment);

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

// Removes the effect-free values that nothing uses, except floating
// operations that may raise an exception `environment` traps.
void remove_dead_values(ManagedFunction& function,
                        const hir::Module& hir_module,
                        const floating::Environment& environment);

} // namespace cross::mir
