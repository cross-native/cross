// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "middle/machine_ir.hpp"

#include <cstdint>
#include <vector>

namespace cross::aarch64 {

// The registers the explicit spill mode stages values through: operands
// in `scratches`, in order, and an indirect call's target in
// `call_target`, which no ABI passes an argument in.
struct SpillPolicy {
    std::vector<std::uint32_t> scratches;
    std::uint32_t call_target{};
};

// The explicit spill mode of the shared allocator design, in place of a
// register allocator: every virtual register lives in a frame home; each
// instruction reloads what it reads into scratch registers just before it
// and stores what it writes just after it, as Machine IR loads and stores
// whose scratch registers are virtual registers with physical assignments;
// PHIs become copies between homes on their incoming edges, which are
// split where they are critical. Returns false after a diagnostic.
[[nodiscard]] bool assign_frame_homes(machine::Function& function, const SpillPolicy& policy,
                                      Diagnostics& diagnostics);

} // namespace cross::aarch64
