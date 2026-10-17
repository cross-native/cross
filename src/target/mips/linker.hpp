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

namespace mips {

// The MIPS part of the linker: relocations, header flags, and the merged
// .reginfo, .MIPS.options, and .MIPS.abiflags sections.
[[nodiscard]] std::unique_ptr<linker::Target> make_linker_target(
    const assembler::ObjectInfo& output);

} // namespace mips
} // namespace cross
