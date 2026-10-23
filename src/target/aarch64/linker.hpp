// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <memory>

namespace cross {

namespace assembler {
struct ObjectInfo;
}
namespace linker {
class Target;
}

namespace aarch64 {

// The AArch64 part of the linker: relocations, the GOT, static TLS,
// range-extension veneers, and relative relocations for position-
// independent executables.
[[nodiscard]] std::unique_ptr<linker::Target> make_linker_target(
    const assembler::ObjectInfo& output);

} // namespace aarch64
} // namespace cross
