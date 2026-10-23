// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace cross {

struct TargetInfo;

// A64 with little-endian ELF64 LP64 objects.
[[nodiscard]] const TargetInfo& aarch64_target();

} // namespace cross
