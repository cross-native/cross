// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "linker/linker.hpp"

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace cross::linker {

[[nodiscard]] bool is_elf(std::span<const std::uint8_t> bytes);
[[nodiscard]] bool is_archive(std::span<const std::uint8_t> bytes);

// Reads an ELF file's sections, symbols, relocations, and groups. Only a
// relocatable object is accepted unless `any_type` (for symbol files); null
// after a diagnostic.
[[nodiscard]] std::unique_ptr<InputFile> read_object(std::span<const std::uint8_t> bytes,
                                                     std::string path, std::string member,
                                                     bool any_type, Diagnostics& diagnostics);

struct Archive {
    struct Member {
        std::string name;
        std::size_t offset{};
        std::size_t size{};
        bool loaded{};
    };
    std::string path;
    std::vector<std::uint8_t> bytes;
    std::vector<Member> members;
    // Each defined symbol of the index and the member that defines it.
    std::vector<std::pair<std::string, std::size_t>> symbols;
};

// Reads a GNU-format archive and its symbol index; nullopt after a
// diagnostic.
[[nodiscard]] std::optional<Archive> read_archive(std::vector<std::uint8_t> bytes,
                                                  std::string path, Diagnostics& diagnostics);

// A raw file as a writable `.data` section with `_binary_NAME_start`,
// `_end`, and `_size` symbols, as `-b binary` gives.
[[nodiscard]] std::unique_ptr<InputFile> binary_object(std::vector<std::uint8_t> bytes,
                                                       std::string path,
                                                       const assembler::ObjectInfo& object);

} // namespace cross::linker
