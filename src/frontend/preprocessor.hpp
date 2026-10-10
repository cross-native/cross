// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "common/source.hpp"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cross {

class Preprocessor {
public:
    Preprocessor(SourceManager& sources, Diagnostics& diagnostics,
                 const CompilerOptions& options);
    std::string process(const std::filesystem::path& input);
    const std::vector<std::filesystem::path>& dependencies() const { return dependencies_; }
    const std::vector<SourceLocation>& output_line_locations() const {
        return output_line_locations_;
    }

private:
    struct Macro {
        std::string replacement;
        std::vector<std::string> parameters;
        bool function_like{};
        bool variadic{};
    };
    // A function-like macro invocation left open at the end of a line: its
    // name still awaits '(' or its arguments await ')'.
    struct OpenInvocation {
        std::string name;
        bool arguments{};
    };

    std::string expand_includes(const std::filesystem::path& path,
                                std::vector<std::filesystem::path>& stack);
    std::string expand_macros(std::string_view source, const SourceFile* file);
    std::filesystem::path find_include(const std::filesystem::path& including,
                                       std::string_view name, bool quoted) const;
    void install_predefined_macros();
    void define_command_line_macros();
    std::string expand_text(std::string_view text,
                            std::unordered_set<std::string>& disabled,
                            unsigned depth,
                            std::optional<SourceLocation> condition = {},
                            std::optional<SourceLocation> origin = {},
                            std::optional<OpenInvocation>* open = nullptr) const;
    std::string substitute(const Macro& macro, const std::vector<std::string>& arguments,
                           std::unordered_set<std::string>& disabled,
                           unsigned depth,
                           std::optional<SourceLocation> condition = {},
                           std::optional<SourceLocation> origin = {}) const;
    std::string evaluate_query(std::string_view name,
                               const std::vector<std::string>& arguments,
                               std::optional<SourceLocation> origin) const;

    SourceManager& sources_;
    Diagnostics& diagnostics_;
    const CompilerOptions& options_;
    std::unordered_map<std::string, Macro> macros_;
    std::unordered_set<std::string> pragma_once_files_;
    std::unordered_set<std::string> already_included_;
    std::vector<SourceLocation> line_locations_;
    std::vector<SourceLocation> output_line_locations_;
    std::vector<std::filesystem::path> dependencies_;
    std::unordered_set<std::string> dependency_identities_;
};

} // namespace cross
