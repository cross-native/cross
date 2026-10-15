// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace cross::mips {

// The instructions the MIPS backend emits and costs, in the order of
// `TargetInfo::instruction_costs`.
enum class Instruction : std::uint16_t {
#define X(name, spelling, latency) name,
#include "target/mips/instructions.def"
#undef X
};

} // namespace cross::mips
