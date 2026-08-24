// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

namespace cross {
class Diagnostics;
namespace hir {
class Module;
}
namespace machine {
struct Module;
}

namespace native {

// Enforces the strictly-standalone backend boundary after target selection.
// Every symbolic machine operand must resolve to an entity explicitly present
// in the compilation-group HIR; target selection may not synthesize helper
// functions or runtime data.
bool audit_standalone(const machine::Module& machine_module,
                      const hir::Module& hir_module,
                      Diagnostics& diagnostics);

} // namespace native
} // namespace cross
