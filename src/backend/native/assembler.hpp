// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "target/subtarget.hpp"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace cross {

class Diagnostics;

namespace native {

// Deliberately small boundary between Cross's native code generator and an
// external object writer.  The assembly is already target assembly; the
// invoked program only parses it and writes the requested object format.
struct AssemblyRequest {
    std::string_view assembly;
    std::string_view target;
    std::string_view cpu;
    // Exact object-writer spellings supplied by the active target backend;
    // never the user/model-facing Cross feature names.
    const std::vector<std::string>* target_features{};
    ObjectFormat object_format{ObjectFormat::Unsupported};
    std::filesystem::path output;
    bool verbose{};
    bool save_temps{};
};

// Assemble target assembly using llvm-mc.  The resolved Subtarget format and
// target triple must agree; on failure a command diagnostic is emitted and
// output is not reported as successful.
[[nodiscard]] bool assemble_object(const AssemblyRequest& request,
                                   Diagnostics& diagnostics);

} // namespace native
} // namespace cross
