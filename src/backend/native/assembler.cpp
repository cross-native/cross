// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend/native/assembler.hpp"

#include "common/diagnostic.hpp"
#include "common/source.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <random>
#include <sstream>
#include <system_error>

namespace cross::native {
namespace {

bool usable_object_triple(std::string_view triple) {
    // Architecture support belongs to the registered target backend and to
    // llvm-mc, not to this architecture-neutral hand-off.  Keeping that
    // decision here would make every future backend edit common driver code.
    return !triple.empty();
}

std::filesystem::path create_temporary_directory(std::error_code& error) {
    const auto root = std::filesystem::temp_directory_path(error);
    if (error) return {};

    std::random_device entropy;
    const auto stamp = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    for (unsigned attempt = 0; attempt != 32; ++attempt) {
        const auto name = "cross-asm-" + std::to_string(stamp) + "-" +
                          std::to_string(entropy()) + "-" + std::to_string(attempt);
        const auto candidate = root / name;
        if (std::filesystem::create_directory(candidate, error)) return candidate;
        if (error && error != std::errc::file_exists) return {};
        error.clear();
    }
    error = std::make_error_code(std::errc::file_exists);
    return {};
}

class TemporaryDirectory {
public:
    explicit TemporaryDirectory(std::filesystem::path path) : path_(std::move(path)) {}
    ~TemporaryDirectory() {
        if (!preserve_) cleanup();
    }

    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    void preserve() { preserve_ = true; }

private:
    void cleanup() noexcept {
        if (path_.empty()) return;
        // This directory was created by us and contains only these two named
        // hand-off files.  Deliberately do not use remove_all(): an unexpected
        // entry makes the final non-recursive directory removal fail and is
        // left for inspection instead of being traversed or deleted.
        std::error_code ignored;
        std::filesystem::remove(path_ / "input.s", ignored);
        ignored.clear();
        std::filesystem::remove(path_ / "output.o", ignored);
        ignored.clear();
        std::filesystem::remove(path_, ignored);
    }

    std::filesystem::path path_;
    bool preserve_{};
};

int run(const std::vector<std::string>& arguments, bool verbose) {
    std::ostringstream command;
    for (const auto& argument : arguments) {
        if (command.tellp() != std::streampos(0)) command << ' ';
        command << quote_command_arg(argument);
    }
    if (verbose) std::cerr << command.str() << '\n';
    return std::system(command.str().c_str());
}

} // namespace

bool assemble_object(const AssemblyRequest& request, Diagnostics& diagnostics) {
    if (!usable_object_triple(request.target) ||
        request.object_format == ObjectFormat::Unsupported) {
        diagnostics.command_error("native assembler has no object writer for target '" +
                                  std::string(request.target) + "'");
        return false;
    }
    if (request.output.empty()) {
        diagnostics.command_error("native assembler requires an output object path");
        return false;
    }

    std::error_code error;
    auto directory = create_temporary_directory(error);
    if (error) {
        diagnostics.command_error("cannot create assembler temporary directory: " + error.message());
        return false;
    }
    TemporaryDirectory temporary(std::move(directory));
    const auto input = temporary.path() / "input.s";
    const auto object = temporary.path() / "output.o";
    std::string write_error;
    if (!write_file(input, request.assembly, write_error)) {
        diagnostics.command_error(write_error);
        return false;
    }

    std::vector<std::string> command{"llvm-mc", "--filetype=obj",
                                     "--triple=" + std::string(request.target)};
    // Feature and CPU names belong to the Cross target registry. They need
    // not use LLVM spellings, and selection has already produced an exact
    // assembly instruction stream. Forwarding them to the object writer
    // would incorrectly couple custom compiler definitions to LLVM's option
    // vocabulary without changing the chosen encoding.
    command.push_back(input.string());
    command.emplace_back("-o");
    command.push_back(object.string());

    const auto status = run(command, request.verbose);
    if (status != 0) {
        if (request.save_temps) {
            temporary.preserve();
            std::cerr << "cc: saved assembler temporary input '" << input.string() << "'\n";
        }
        diagnostics.command_error("assembler failed for target '" + std::string(request.target) +
                                  "' (temporary input: " + input.string() + ")");
        return false;
    }

    std::filesystem::copy_file(object, request.output,
                               std::filesystem::copy_options::overwrite_existing, error);
    if (error) {
        if (request.save_temps) {
            temporary.preserve();
            std::cerr << "cc: saved assembler temporary input '" << input.string() << "'\n";
        }
        diagnostics.command_error("cannot write assembled object '" + request.output.string() +
                                  "': " + error.message());
        return false;
    }
    if (request.save_temps) {
        temporary.preserve();
        std::cerr << "cc: saved assembler temporary input '" << input.string() << "'\n";
    }
    return true;
}

} // namespace cross::native
