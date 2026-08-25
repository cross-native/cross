// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "middle/codegen_module.hpp"

#include <string>

namespace cross::debug {

// GCC's __GIMPLE input is experimental and version-sensitive.  This adapter
// is an optional validation/oracle path; native Machine IR remains the
// production backend boundary.
enum class GimpleStart { Gimple, Rtl };

class GimpleTextSerializer {
public:
    GimpleTextSerializer(const CompilerOptions& options,
                         Diagnostics& diagnostics, GimpleStart start);
    std::string serialize(const codegen::ModuleView& module);

private:
    const CompilerOptions& options_;
    Diagnostics& diagnostics_;
    GimpleStart start_;
};

} // namespace cross::debug
