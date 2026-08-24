// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "middle/codegen_module.hpp"

#include <string>

namespace cross::debug {

class LlvmTextSerializer {
public:
    LlvmTextSerializer(const CompilerOptions& options, Diagnostics& diagnostics);
    std::string serialize(const codegen::ModuleView& module);

private:
    const CompilerOptions& options_;
    Diagnostics& diagnostics_;
};

} // namespace cross::debug
