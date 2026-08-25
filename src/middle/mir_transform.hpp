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
[[nodiscard]] bool eliminate_forwarding_blocks(ManagedFunction& function);

// Shared identity-preserving cleanup used by CFG transforms after they make
// one or more non-addressable blocks unreachable.
void prune_unreachable_blocks(ManagedFunction& function);

} // namespace cross::mir
