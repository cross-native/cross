// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace cross {

struct SourceFile;

struct SourceLocation {
    const SourceFile* file{};
    std::size_t offset{};
    unsigned line{1};
    unsigned column{1};

    [[nodiscard]] bool valid() const { return file != nullptr; }
};

struct SourceExpansion {
    std::size_t begin{};
    std::size_t end{};
    std::string macro_name;
    SourceLocation invocation;
    SourceLocation definition;
};

struct SourceFile {
    std::filesystem::path path;
    std::string text;
    std::vector<std::size_t> line_starts{0};
    std::vector<SourceExpansion> expansions;

    SourceFile(std::filesystem::path path, std::string text,
               std::vector<SourceExpansion> expansions = {});
    [[nodiscard]] std::string_view line(unsigned line) const;
    [[nodiscard]] const SourceExpansion* expansion_at(
        std::size_t offset) const;
};

class SourceManager {
public:
    const SourceFile* load(const std::filesystem::path& path, std::string& error);
    const SourceFile* add(std::filesystem::path path, std::string text);
    const SourceFile* add(std::filesystem::path path, std::string text,
                          std::vector<SourceExpansion> expansions);

private:
    std::vector<std::unique_ptr<SourceFile>> files_;
};

bool write_file(const std::filesystem::path& path, std::string_view text,
                std::string& error);
std::string quote_command_arg(std::string_view value);

} // namespace cross
