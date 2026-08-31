// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "middle/mir.hpp"

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
[[nodiscard]] bool eliminate_forwarding_blocks(ManagedFunction& function);
[[nodiscard]] bool factor_common_phi_tails(
    ManagedFunction& function, const hir::Module& hir_module);
// Convert a legal outermost top-tested natural loop into an initial zero-trip
// guard and a bottom-tested loop. The transform repairs value SSA and effect
// SSA and deliberately rejects secondary exits, multiple latches, compact
// self-latches, and addressable blocks rather than weakening those invariants.
[[nodiscard]] bool rotate_guarded_loops(ManagedFunction& function);

// Keep canonical dense value IDs after a transformation removes definitions.
// The helper rewrites every value-bearing MIR edge, including effect owners,
// parameter lists, phi inputs, and terminators.
void compact_managed_values(ManagedFunction& function);

// Shared identity-preserving cleanup used by CFG transforms after they make
// one or more non-addressable blocks unreachable.
void prune_unreachable_blocks(ManagedFunction& function);

} // namespace cross::mir
