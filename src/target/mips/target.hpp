// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace cross {

struct TargetInfo;

// MIPS byte order is part of the target layout, while the canonical
// architecture identity remains "mips" so both variants share one backend,
// ABI model family, feature graph, and instruction selector.
[[nodiscard]] const TargetInfo& mips_target();
[[nodiscard]] const TargetInfo& mipsel_target();

} // namespace cross
