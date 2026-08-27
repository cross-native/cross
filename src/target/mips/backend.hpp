// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace cross {
class TargetBackend;

namespace mips {
[[nodiscard]] const TargetBackend& backend();
}
} // namespace cross
