// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

namespace cross::x86_64 {

// The instructions whose costs or availability x86-64 selection consults,
// in the order of `TargetInfo::instruction_costs`; the registry's mnemonics
// follow them there.
enum class Instruction : std::uint16_t {
#define X(name, spelling, latency) name,
#include "target/x86_64/instructions.def"
#undef X
};

} // namespace cross::x86_64
