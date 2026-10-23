// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "middle/hir.hpp"
#include "target/aarch64/selection.hpp"
#include "target/target.hpp"

namespace cross::aarch64 {

// The scratch register of frame accesses whose offset no load, store, or
// add immediate reaches; an instruction that needs it lists it as a clobber.
inline constexpr std::uint32_t frame_scratch = 16;

// Lays out the frame of an allocated function and builds its frame
// program: a frame record (x29, x30) at the top, the registers of its ABI
// that the body writes, then the spill homes, locals, and the outgoing
// argument area at sp, 16-byte aligned. Returns false after a diagnostic.
[[nodiscard]] bool finalize_frame(SelectedFunction& selected, const hir::Function& entity,
                                  const AbiEntry& abi, const CompilerOptions& options,
                                  Diagnostics& diagnostics);

} // namespace cross::aarch64
