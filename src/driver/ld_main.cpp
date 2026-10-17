// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "linker/linker.hpp"
#include "target/target.hpp"

#include <array>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

namespace {

using namespace cross;

struct Format {
    std::string_view name;
    bool elf64;
    bool big_endian;
    std::uint64_t page_size;
    bool traditional;
};

// GNU ld's MIPS emulations and output formats.
constexpr std::array emulations{
    Format{"elf32btsmip", false, true, 0x10000, true}, Format{"elf32ltsmip", false, false, 0x10000, true},
    Format{"elf32ebmip", false, true, 0x1000, false},  Format{"elf32elmip", false, false, 0x1000, false},
    Format{"elf64btsmip", true, true, 0x10000, true},  Format{"elf64ltsmip", true, false, 0x10000, true},
};
constexpr std::array output_formats{
    Format{"elf32-tradbigmips", false, true, 0x10000, true},
    Format{"elf32-tradlittlemips", false, false, 0x10000, true},
    Format{"elf32-bigmips", false, true, 0x1000, false},
    Format{"elf32-littlemips", false, false, 0x1000, false},
    Format{"elf64-tradbigmips", true, true, 0x10000, true},
    Format{"elf64-tradlittlemips", true, false, 0x10000, true},
    Format{"elf64-bigmips", true, true, 0x10000, false},
    Format{"elf64-littlemips", true, false, 0x10000, false},
};

void print_help() {
    std::cout << R"(Usage: ld [options] file ...
  -o FILE                 write the output to FILE (default a.out)
  -T FILE, --script=FILE  lay out the output with linker script FILE
  -e SYMBOL, --entry=SYMBOL
                          start execution at SYMBOL (or an address)
  -L DIR                  search DIR for -l libraries and INCLUDE scripts
  -l NAME                 link libNAME.a (or NAME with -l:NAME)
  -R FILE, --just-symbols=FILE
                          use the symbol addresses of FILE, not its contents
  -b binary, --format=binary
                          read the following inputs as raw data (-b elf ends)
  -u SYMBOL               require SYMBOL, pulling archive members
  --defsym SYMBOL=EXPR    define SYMBOL as the script expression EXPR
  -Ttext=ADDR, -Tdata=ADDR, -Tbss=ADDR, --section-start=NAME=ADDR
                          place an output section at ADDR
  --gc-sections           remove unreferenced sections
  --print-gc-sections     list the sections --gc-sections removes
  --gc-keep-exported      keep sections with global symbols of default or
                          protected visibility (with -r, the roots)
  -d, -dc, -dp            allocate common symbols even with -r
  --eh-frame-hdr          write .eh_frame_hdr and PT_GNU_EH_FRAME
  --no-eh-frame-hdr       do not (the default)
  -nostdlib               accepted; there are no default library directories
  -Map FILE, -M           write a link map to FILE, or to standard output
  -m EMULATION            select the output: elf32btsmip, elf32ltsmip,
                          elf32ebmip, elf32elmip, elf64btsmip, elf64ltsmip
  --oformat=FORMAT        select the output by BFD name, such as elf32-bigmips
  -EB, -EL                select big- or little-endian output
  -z max-page-size=SIZE   align loadable segments in the file to SIZE
  -s, --strip-all         omit the symbol table
  --fatal-warnings        treat warnings as errors
  --start-group, --end-group
                          accepted; archives are searched in any order
)";
}

std::optional<std::uint64_t> parse_number(std::string_view text) {
    if (text.empty()) return std::nullopt;
    const std::string copy(text);
    char* end = nullptr;
    const auto value = std::strtoull(copy.c_str(), &end, 0);
    if (!end || *end != '\0') return std::nullopt;
    return value;
}

linker::TargetFactory lookup(std::uint16_t machine) {
    for (const auto* target : all_targets()) {
        if (target->elf_machine == machine && target->linker) return target->linker;
    }
    return nullptr;
}

class CommandLine {
public:
    CommandLine(int argc, char** argv, Diagnostics& diagnostics)
        : argc_(argc), argv_(argv), diagnostics_(diagnostics) {}

    bool parse(linker::Options& options, bool& help, bool& version) {
        bool binary = false;
        while (++index_ < argc_) {
            const std::string_view argument = argv_[index_];
            if (argument.size() < 2 || argument[0] != '-') {
                options.inputs.push_back({binary ? linker::Input::Kind::Binary : linker::Input::Kind::File,
                                          std::string(argument)});
                continue;
            }
            std::optional<std::string> value;
            if (argument == "--help") {
                help = true;
            } else if (argument == "--version" || argument == "-v" || argument == "-V") {
                version = true;
            } else if (argument == "-EB" || argument == "-EL") {
                options.big_endian = argument == "-EB";
            } else if (argument == "-r" || argument == "-i" || argument == "--relocatable") {
                options.relocatable = true;
            } else if (argument == "-s" || argument == "--strip-all") {
                options.strip_all = true;
            } else if (argument == "--gc-sections") {
                options.gc_sections = true;
            } else if (argument == "--no-gc-sections") {
                options.gc_sections = false;
            } else if (argument == "--print-gc-sections") {
                options.print_gc_sections = true;
            } else if (argument == "--gc-keep-exported") {
                options.keep_exported = true;
            } else if (argument == "-d" || argument == "-dc" || argument == "-dp") {
                options.define_common = true;
            } else if (argument == "--eh-frame-hdr") {
                options.eh_frame_header = true;
            } else if (argument == "--no-eh-frame-hdr") {
                options.eh_frame_header = false;
            } else if (argument == "-nostdlib") {
                // There are no default library directories to drop.
            } else if (argument == "--fatal-warnings") {
                options.fatal_warnings = true;
            } else if (argument == "--start-group" || argument == "--end-group" || argument == "-(" ||
                       argument == "-)") {
            } else if (argument == "-M" || argument == "--print-map") {
                options.map = "-";
            } else if ((value = take(argument, "-Map", "--Map"))) {
                options.map = *value;
            } else if ((value = take(argument, "-Ttext", "--Ttext"))) {
                if (!section_start(options, ".text", *value)) return false;
            } else if ((value = take(argument, "-Tdata", "--Tdata"))) {
                if (!section_start(options, ".data", *value)) return false;
            } else if ((value = take(argument, "-Tbss", "--Tbss"))) {
                if (!section_start(options, ".bss", *value)) return false;
            } else if ((value = take(argument, "--section-start", {}))) {
                const auto equals = value->find('=');
                if (equals == std::string::npos ||
                    !section_start(options, value->substr(0, equals), value->substr(equals + 1))) {
                    if (equals == std::string::npos) {
                        diagnostics_.command_error("--section-start expects NAME=ADDRESS");
                    }
                    return false;
                }
            } else if ((value = take(argument, "--defsym", {}))) {
                options.definitions.push_back(*value);
            } else if ((value = take(argument, "--oformat", {}))) {
                if (!format(options, *value, output_formats, "output format")) return false;
            } else if ((value = take(argument, "--format", "-b", true))) {
                if (*value == "binary") {
                    binary = true;
                } else if (*value == "default" || value->starts_with("elf")) {
                    binary = false;
                } else {
                    diagnostics_.command_error("unsupported input format '" + *value + "'");
                    return false;
                }
            } else if ((value = take(argument, "--output", "-o", true))) {
                options.output = *value;
            } else if ((value = take(argument, "--script", "-T", true))) {
                options.script = *value;
                options.script_position = options.inputs.size();
            } else if ((value = take(argument, "--entry", "-e", true))) {
                options.entry = *value;
            } else if ((value = take(argument, "--library-path", "-L", true))) {
                options.library_paths.emplace_back(*value);
            } else if ((value = take(argument, "--library", "-l", true))) {
                options.inputs.push_back({linker::Input::Kind::Library, *value});
            } else if ((value = take(argument, "--just-symbols", "-R", true))) {
                options.inputs.push_back({linker::Input::Kind::JustSymbols, *value});
            } else if ((value = take(argument, "--undefined", "-u", true))) {
                options.undefined.push_back(*value);
            } else if ((value = take(argument, {}, "-m", true))) {
                if (!format(options, *value, emulations, "emulation")) return false;
            } else if ((value = take(argument, {}, "-z", true))) {
                const auto size = value->starts_with("max-page-size=")
                                      ? parse_number(std::string_view(*value).substr(14))
                                      : std::nullopt;
                if (!size || *size == 0 || (*size & (*size - 1)) != 0) {
                    diagnostics_.command_error("unsupported option '-z " + *value + "'");
                    return false;
                }
                options.page_size = *size;
            } else {
                if (!failed_) diagnostics_.command_error("unknown option '" + std::string(argument) + "'");
                return false;
            }
            if (failed_) return false;
        }
        return true;
    }

private:
    // The value of `name=VALUE` or `name VALUE` (long), or of `-xVALUE` or
    // `-x VALUE` (short, when `joined`); nullopt when the argument is
    // another option.
    std::optional<std::string> take(std::string_view argument, std::string_view name,
                                    std::string_view short_name, bool joined = false) {
        for (const auto option : {name, short_name}) {
            if (option.empty() || !argument.starts_with(option)) continue;
            const auto rest = argument.substr(option.size());
            const bool is_short = option == short_name && joined;
            if (!rest.empty()) {
                if (rest.front() == '=' && !is_short) return std::string(rest.substr(1));
                if (is_short) return std::string(rest);
                continue;
            }
            if (index_ + 1 >= argc_) {
                diagnostics_.command_error("option '" + std::string(option) + "' needs a value");
                failed_ = true;
                return std::string();
            }
            return std::string(argv_[++index_]);
        }
        return std::nullopt;
    }

    bool section_start(linker::Options& options, std::string_view name, std::string_view text) {
        const auto address = parse_number(text.starts_with("0x") || text.starts_with("0X")
                                              ? text
                                              : "0x" + std::string(text));
        if (!address) {
            diagnostics_.command_error("invalid address '" + std::string(text) + "'");
            return false;
        }
        options.section_starts.emplace_back(std::string(name), *address);
        return true;
    }

    template <typename Table>
    bool format(linker::Options& options, std::string_view name, const Table& table,
                std::string_view what) {
        for (const auto& entry : table) {
            if (entry.name != name) continue;
            options.elf64 = entry.elf64;
            if (!options.big_endian) options.big_endian = entry.big_endian;
            if (!options.page_size) options.page_size = entry.page_size;
            options.format.traditional = entry.traditional;
            return true;
        }
        diagnostics_.command_error("unsupported " + std::string(what) + " '" + std::string(name) + "'");
        return false;
    }

    int argc_;
    char** argv_;
    Diagnostics& diagnostics_;
    int index_{};
    bool failed_{};
};

int link_main(int argc, char** argv) {
    Diagnostics diagnostics(std::cerr, "ld");
    linker::Options options;
    bool help = false;
    bool version = false;
    CommandLine command(argc, argv, diagnostics);
    if (!command.parse(options, help, version)) return 1;
    if (help) {
        print_help();
        return 0;
    }
    if (version) {
        print_version();
        return 0;
    }
    if (options.inputs.empty()) {
        diagnostics.command_error("no input files");
        return 1;
    }
    return linker::link(options, &lookup, diagnostics) ? 0 : 1;
}

} // namespace

int main(int argc, char** argv) { return link_main(argc, argv); }
