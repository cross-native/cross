// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "linker/link.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>

namespace cross::linker {
namespace {

constexpr std::uint32_t shn_abs = 0xfff1;
constexpr std::uint32_t shn_common = 0xfff2;
constexpr std::uint32_t shn_loreserve = 0xff00;

std::string hex(std::uint64_t value) {
    std::ostringstream out;
    out << "0x" << std::hex << value;
    return out.str();
}

std::optional<std::vector<std::uint8_t>> read_file(const std::string& path, Diagnostics& diagnostics) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        diagnostics.command_error("cannot open '" + path + "'");
        return std::nullopt;
    }
    return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)),
                                     std::istreambuf_iterator<char>());
}

void collect_references(const script::Expression& expression, std::set<std::string>& names) {
    if (expression.kind == script::Expression::Kind::Symbol) names.insert(expression.name);
    if (expression.kind == script::Expression::Kind::Function &&
        (expression.name == "DEFINED" || expression.name == "ADDR" ||
         expression.name == "LOADADDR" || expression.name == "SIZEOF" ||
         expression.name == "ALIGNOF" || expression.name == "ORIGIN" ||
         expression.name == "LENGTH" || expression.name == "CONSTANT" ||
         expression.name == "SEGMENT_START")) {
        for (std::size_t index = 1; index < expression.operands.size(); ++index) {
            collect_references(*expression.operands[index], names);
        }
        return;
    }
    for (const auto& operand : expression.operands) collect_references(*operand, names);
}

template <typename Visit>
void each_assignment(const script::Script& script, Visit visit) {
    for (const auto& command : script.commands) {
        if (const auto* assignment = std::get_if<script::Assignment>(&command)) {
            visit(*assignment);
        } else if (const auto* section = std::get_if<script::OutputSection>(&command)) {
            for (const auto& item : section->items) {
                if (const auto* inner = std::get_if<script::Assignment>(&item)) visit(*inner);
            }
        }
    }
}

template <typename Visit>
void each_expression(const script::Script& script, Visit visit) {
    for (const auto& command : script.commands) {
        if (const auto* assignment = std::get_if<script::Assignment>(&command)) {
            visit(*assignment->value);
        } else if (const auto* assertion = std::get_if<script::Assert>(&command)) {
            visit(*assertion->condition);
        } else if (const auto* section = std::get_if<script::OutputSection>(&command)) {
            for (const auto* expression :
                 {section->address.get(), section->load_address.get(), section->alignment.get(),
                  section->subalign.get(), section->update_dot.get(),
                  section->fill ? section->fill->value.get() : nullptr}) {
                if (expression) visit(*expression);
            }
            for (const auto& item : section->items) {
                if (const auto* inner = std::get_if<script::Assignment>(&item)) {
                    visit(*inner->value);
                } else if (const auto* data = std::get_if<script::Data>(&item)) {
                    visit(*data->value);
                } else if (const auto* check = std::get_if<script::Assert>(&item)) {
                    visit(*check->condition);
                } else if (const auto* fill = std::get_if<script::Fill>(&item)) {
                    visit(*fill->value);
                }
            }
        }
    }
}

// Sections GNU ld treats as debugging information by name.
bool debugging_section(std::string_view name) {
    return name.starts_with(".debug") || name.starts_with(".gnu.debuglto_.debug_") ||
           name.starts_with(".gnu.linkonce.wi.") || name.starts_with(".zdebug") ||
           name.starts_with(".line") || name.starts_with(".stab") || name == ".gdb_index" ||
           name.starts_with(".mdebug");
}

bool reserved_section(const InputSection& section) {
    if ((section.flags & elf::shf_gnu_retain) != 0) return true;
    switch (section.type) {
    case elf::sht_note:
    case elf::sht_init_array:
    case elf::sht_fini_array:
    case elf::sht_preinit_array:
        return true;
    default:
        break;
    }
    if (section.type >= 0x60000000U) return true;
    const auto& name = section.name;
    return name.starts_with(".ctors") || name.starts_with(".dtors") || name == ".init" ||
           name == ".fini" || name.starts_with(".jcr") || name == ".eh_frame";
}

} // namespace

std::string InputSection::location(std::uint64_t at) const {
    return file->name() + ":(" + name + "+" + hex(at) + ")";
}

Symbol& Link::symbol(const std::string& name) {
    const auto found = symbol_index_.find(name);
    if (found != symbol_index_.end()) return *found->second;
    auto& result = symbols_.emplace_back();
    result.name = name;
    symbol_index_.emplace(name, &result);
    return result;
}

Symbol* Link::find(const std::string& name) const {
    const auto found = symbol_index_.find(name);
    return found == symbol_index_.end() ? nullptr : found->second;
}

void Link::reference(Symbol& target, bool weak, InputFile* file) {
    if (!target.defined()) {
        target.weak = target.referenced ? target.weak && weak : weak;
        // The first object that refers to the symbol, for the symbol table.
        if (!target.file) target.file = file;
    }
    target.referenced = true;
    if (target.kind == Symbol::Kind::Lazy && !weak) {
        pending_.emplace_back(target.archive, target.member);
        fetch_pending();
    }
}

void Link::fetch_pending() {
    if (fetching_) return;
    fetching_ = true;
    while (!pending_.empty()) {
        const auto [archive_index, member_index] = pending_.front();
        pending_.erase(pending_.begin());
        auto& archive = archives_[archive_index];
        auto& member = archive.members[member_index];
        if (member.loaded) continue;
        member.loaded = true;
        const std::span<const std::uint8_t> bytes(archive.bytes.data() + member.offset, member.size);
        if (!is_elf(bytes)) {
            diagnostics_.command_error(archive.path + "(" + member.name +
                                       "): archive member is not an ELF object");
            continue;
        }
        auto file = read_object(bytes, archive.path, member.name, false, diagnostics_);
        if (file) add_file(std::move(file));
    }
    fetching_ = false;
}

void Link::report_duplicate(const Symbol& existing, const InputFile& file, const FileSymbol& symbol) {
    const auto where = [](const InputFile* owner, const InputSection* section, std::uint64_t value) {
        if (section) return section->location(value);
        return owner ? owner->name() : std::string("the linker script");
    };
    const InputSection* section = symbol.shndx < file.sections.size()
                                      ? file.sections[symbol.shndx].get()
                                      : nullptr;
    diagnostics_.command_error(where(&file, section, symbol.value) + ": multiple definition of '" +
                               symbol.name + "'; first defined in " +
                               where(existing.file, existing.section, existing.value));
}

void Link::add_symbols(InputFile& file) {
    for (std::size_t index = 1; index < file.symbols.size(); ++index) {
        auto& entry = file.symbols[index];
        if (entry.binding == elf::stb_local || entry.type == elf::stt_section ||
            entry.type == elf::stt_file || entry.name.empty()) {
            continue;
        }
        auto& target = symbol(entry.name);
        entry.global = &target;
        const bool weak = entry.binding == elf::stb_weak;
        const auto visibility = static_cast<std::uint8_t>(entry.other & 3U);
        if (visibility != elf::stv_default) {
            const auto current = static_cast<std::uint8_t>(target.other & 3U);
            if (current == elf::stv_default || visibility < current) {
                target.other = static_cast<std::uint8_t>((target.other & ~3U) | visibility);
            }
        }
        const bool undefined = entry.shndx == 0 || target_->undefined_index(entry.shndx);
        std::string_view common;
        if (entry.shndx == shn_common) {
            common = "COMMON";
        } else if (!undefined) {
            common = target_->common_section(entry.shndx);
        }
        InputSection* section = nullptr;
        if (!undefined && common.empty() && entry.shndx != shn_abs) {
            if (entry.shndx >= shn_loreserve || entry.shndx >= file.sections.size() ||
                !file.sections[entry.shndx] || file.sections[entry.shndx]->discarded) {
                // A definition in a duplicate COMDAT group or in a section the
                // link drops refers to the definition that is kept.
                reference(target, true, &file);
                continue;
            }
            section = file.sections[entry.shndx].get();
        }
        if (undefined) {
            reference(target, weak, &file);
            continue;
        }
        if (!common.empty()) {
            const auto alignment = std::max<std::uint64_t>(entry.value, 1);
            switch (target.kind) {
            case Symbol::Kind::Undefined:
            case Symbol::Kind::Lazy:
                target.kind = Symbol::Kind::Common;
                target.size = entry.size;
                target.common_alignment = alignment;
                target.file = &file;
                target.weak = false;
                target.type = elf::stt_object;
                target.common_section = common;
                target.common_index = entry.shndx;
                break;
            case Symbol::Kind::Common:
                if (entry.size > target.size) {
                    target.size = entry.size;
                    target.file = &file;
                }
                target.common_alignment = std::max(target.common_alignment, alignment);
                break;
            case Symbol::Kind::Defined:
                if (target.weak && !target.script) {
                    target.kind = Symbol::Kind::Common;
                    target.section = nullptr;
                    target.size = entry.size;
                    target.common_alignment = alignment;
                    target.file = &file;
                    target.weak = false;
                    target.common_section = common;
                    target.common_index = entry.shndx;
                }
                break;
            }
            continue;
        }
        const auto take = [&] {
            target.kind = Symbol::Kind::Defined;
            target.weak = weak;
            target.section = section;
            target.value = entry.value;
            target.size = entry.size;
            target.type = entry.type;
            target.file = &file;
        };
        switch (target.kind) {
        case Symbol::Kind::Undefined:
        case Symbol::Kind::Lazy:
            take();
            break;
        case Symbol::Kind::Common:
            if (!weak) take();
            break;
        case Symbol::Kind::Defined:
            if (target.script) break;
            if (target.weak && !weak) {
                take();
            } else if (!target.weak && !weak) {
                report_duplicate(target, file, entry);
            }
            break;
        }
    }
}

bool Link::add_file(std::unique_ptr<InputFile> file) {
    if (file->object.elf64 != object_.elf64 || file->object.big_endian != object_.big_endian ||
        file->object.machine != object_.machine) {
        const auto describe = [](const assembler::ObjectInfo& info) {
            return std::string(info.elf64 ? "ELF64" : "ELF32") +
                   (info.big_endian ? " big-endian" : " little-endian") + " machine " +
                   std::to_string(info.machine);
        };
        diagnostics_.command_error(file->name() + ": " + describe(file->object) +
                                   " is incompatible with the " + describe(object_) + " output");
        return false;
    }
    if (!target_->add_object(*file, diagnostics_)) return false;
    for (const auto& group : file->groups) {
        if (!group.comdat || comdat_signatures_.insert(group.signature).second) continue;
        for (const auto member : group.members) {
            if (member < file->sections.size() && file->sections[member]) {
                file->sections[member]->discarded = true;
            }
        }
    }
    auto& added = *file;
    files_.push_back(std::move(file));
    add_symbols(added);
    return true;
}

std::optional<std::string> Link::find_library(const std::string& name) {
    std::vector<std::filesystem::path> directories = options_.library_paths;
    directories.insert(directories.end(), script_.search_directories.begin(),
                       script_.search_directories.end());
    const std::string file = name.starts_with(":") ? name.substr(1) : "lib" + name + ".a";
    for (const auto& directory : directories) {
        const auto candidate = directory / file;
        std::error_code code;
        if (std::filesystem::is_regular_file(candidate, code)) return candidate.string();
    }
    return std::nullopt;
}

bool Link::open_input(const Input& input, std::vector<std::uint8_t> bytes, const std::string& path) {
    if (input.kind == Input::Kind::Binary) {
        return add_file(binary_object(std::move(bytes), path, object_));
    }
    if (input.kind == Input::Kind::JustSymbols) {
        auto file = read_object(bytes, path, {}, true, diagnostics_);
        if (!file) return false;
        if (file->object.elf64 != object_.elf64 || file->object.machine != object_.machine) {
            diagnostics_.command_error(path + ": symbol file is incompatible with the output");
            return false;
        }
        file->sections.clear();
        file->just_symbols = true;
        auto& owner = *file;
        files_.push_back(std::move(file));
        for (auto& entry : owner.symbols) {
            if ((entry.binding != elf::stb_global && entry.binding != elf::stb_weak) ||
                entry.shndx == 0 || entry.shndx == shn_common || entry.name.empty() ||
                entry.type == elf::stt_section || entry.type == elf::stt_file) {
                continue;
            }
            auto& target = symbol(entry.name);
            if (target.defined()) {
                if (!target.weak && !target.script && entry.binding != elf::stb_weak) {
                    report_duplicate(target, owner, entry);
                }
                continue;
            }
            target.kind = Symbol::Kind::Defined;
            target.weak = entry.binding == elf::stb_weak;
            target.section = nullptr;
            target.value = mask(entry.value);
            target.size = entry.size;
            target.type = entry.type;
            target.file = &owner;
        }
        return true;
    }
    if (is_archive(bytes)) {
        auto archive = read_archive(std::move(bytes), path, diagnostics_);
        if (!archive) return false;
        const auto archive_index = archives_.size();
        archives_.push_back(std::move(*archive));
        // Copy the index: fetching members may add archives.
        const auto entries = archives_[archive_index].symbols;
        for (const auto& [name, member] : entries) {
            auto& target = symbol(name);
            if (target.kind != Symbol::Kind::Undefined) continue;
            target.kind = Symbol::Kind::Lazy;
            target.archive = archive_index;
            target.member = member;
            if (target.referenced && !target.weak) pending_.emplace_back(archive_index, member);
        }
        fetch_pending();
        return true;
    }
    if (!is_elf(bytes)) {
        diagnostics_.command_error(path + ": file format not recognized");
        return false;
    }
    auto file = read_object(bytes, path, {}, false, diagnostics_);
    return file && add_file(std::move(file));
}

std::optional<std::string> Link::find_script_input(const script::ScriptInput& input) {
    // GNU ld's search for a file INPUT or GROUP names: the naming script's
    // directory, the current directory, then the library directories.
    if (input.library) return find_library(input.name);
    std::vector<std::filesystem::path> candidates;
    const std::filesystem::path name(input.name);
    if (name.is_relative() && !input.directory.empty()) candidates.push_back(input.directory / name);
    candidates.push_back(name);
    if (name.is_relative()) {
        for (const auto& directory : options_.library_paths) candidates.push_back(directory / name);
        for (const auto& directory : script_.search_directories) candidates.push_back(directory / name);
    }
    for (const auto& candidate : candidates) {
        std::error_code code;
        if (std::filesystem::is_regular_file(candidate, code)) return candidate.string();
    }
    return std::nullopt;
}

void Link::require(const std::string& name) {
    auto& target = symbol(name);
    if (!target.defined()) reference(target, false, nullptr);
}

bool Link::read_inputs() {
    struct Request {
        Input input;
        // A file a script names, searched for as GNU ld does.
        std::optional<script::ScriptInput> named;
    };
    struct Loaded {
        Input input;
        std::string path;
        std::vector<std::uint8_t> bytes;
    };
    // The command line, with the main script's INPUT and GROUP files at the
    // script's position and each implicit script's files after it.
    std::vector<Request> requests;
    for (const auto& input : options_.inputs) requests.push_back({input, std::nullopt});
    // Only an implicit script's own directory is searched first (GNU ld).
    const auto script_inputs = [&](std::size_t first, std::size_t position, bool implicit) {
        std::vector<Request> named;
        for (std::size_t index = first; index < script_.inputs.size(); ++index) {
            auto input = script_.inputs[index];
            if (!implicit) input.directory.clear();
            named.push_back({{input.library ? Input::Kind::Library : Input::Kind::File, input.name}, input});
        }
        requests.insert(requests.begin() + static_cast<std::ptrdiff_t>(std::min(position, requests.size())),
                        named.begin(), named.end());
    };
    script_inputs(0, options_.script_position, false);
    std::vector<Loaded> loaded;
    bool ok = true;
    for (std::size_t index = 0; index < requests.size(); ++index) {
        const auto request = requests[index];
        std::string path = request.input.name;
        if (request.named) {
            const auto found = find_script_input(*request.named);
            if (!found) {
                diagnostics_.error(request.named->location,
                                   "cannot find " + std::string(request.named->library ? "-l" : "") +
                                       request.named->name);
                ok = false;
                continue;
            }
            path = *found;
        } else if (request.input.kind == Input::Kind::Library) {
            const auto found = find_library(request.input.name);
            if (!found) {
                diagnostics_.command_error("cannot find -l" + request.input.name);
                ok = false;
                continue;
            }
            path = *found;
        }
        auto bytes = read_file(path, diagnostics_);
        if (!bytes) {
            ok = false;
            continue;
        }
        if ((request.input.kind == Input::Kind::File || request.input.kind == Input::Kind::Library) &&
            !is_elf(*bytes) && !is_archive(*bytes)) {
            // A file that is neither an object nor an archive is an
            // implicit linker script, which augments the main one.
            std::string text(bytes->begin(), bytes->end());
            const auto* source = sources_.add(path, std::move(text));
            const auto first_input = script_.inputs.size();
            const auto first_extern = script_.externs.size();
            if (!script::parse(*source, script_, options_.library_paths, sources_, diagnostics_)) {
                ok = false;
                continue;
            }
            for (std::size_t other = first_extern; other < script_.externs.size(); ++other) {
                require(script_.externs[other]);
            }
            script_inputs(first_input, index + 1, true);
            continue;
        }
        loaded.push_back({request.input, std::move(path), std::move(*bytes)});
    }
    if (!ok) return false;

    // The first ELF object, possibly an archive member, decides what the
    // command line leaves open.
    std::optional<assembler::ObjectInfo> first;
    for (const auto& item : loaded) {
        if (item.input.kind == Input::Kind::Binary) continue;
        std::span<const std::uint8_t> bytes = item.bytes;
        if (is_archive(bytes)) {
            auto archive = read_archive(item.bytes, item.path, diagnostics_);
            if (!archive) return false;
            for (const auto& member : archive->members) {
                const std::span<const std::uint8_t> body(archive->bytes.data() + member.offset,
                                                         member.size);
                if (is_elf(body)) {
                    bytes = body;
                    break;
                }
            }
        }
        if (!is_elf(bytes) || bytes.size() < 20) continue;
        assembler::ObjectInfo info;
        info.elf64 = bytes[4] == 2;
        info.big_endian = bytes[5] == 2;
        info.machine = static_cast<std::uint16_t>(info.big_endian ? (bytes[18] << 8U) | bytes[19]
                                                                  : (bytes[19] << 8U) | bytes[18]);
        first = info;
        break;
    }
    if (!first) {
        diagnostics_.command_error(loaded.empty() ? "no input files"
                                                  : "no input object determines the target");
        return false;
    }
    object_ = *first;
    if (options_.elf64) object_.elf64 = *options_.elf64;
    if (options_.big_endian) object_.big_endian = *options_.big_endian;
    const auto factory = lookup_(object_.machine);
    if (!factory) {
        diagnostics_.command_error("ELF machine " + std::to_string(object_.machine) +
                                   " is not supported");
        return false;
    }
    target_ = factory(object_);
    object_.rela = object_.elf64;
    page_size_ = options_.page_size.value_or(target_->page_size());

    for (auto& item : loaded) {
        if (!open_input(item.input, std::move(item.bytes), item.path)) ok = false;
    }
    return ok && diagnostics_.errors() == 0;
}

void Link::define_script_symbols() {
    // Assignments define their symbols; PROVIDE only defines a symbol that
    // is referenced and not otherwise defined.
    each_assignment(script_, [&](const script::Assignment& assignment) {
        if (assignment.symbol == ".") return;
        auto& target = symbol(assignment.symbol);
        if (assignment.provide) {
            if (target.defined() && !target.script) return;
            if (!target.referenced && !script_references_.contains(assignment.symbol)) return;
        }
        if (target.kind == Symbol::Kind::Common) target.size = 0;
        target.kind = Symbol::Kind::Defined;
        target.script = true;
        target.weak = false;
        target.section = nullptr;
        target.file = nullptr;
        target.type = elf::stt_notype;
        if (assignment.hidden) target.hidden = true;
    });
}

void Link::allocate_commons() {
    // Each file's common symbols share one zero-filled section per kind,
    // in symbol order.
    std::map<std::pair<InputFile*, std::string_view>, InputSection*> sections;
    for (auto& entry : symbols_) {
        if (entry.kind != Symbol::Kind::Common) continue;
        auto& section = sections[{entry.file, entry.common_section}];
        if (!section) {
            auto created = std::make_unique<InputSection>();
            created->file = entry.file;
            created->name = std::string(entry.common_section);
            created->type = elf::sht_nobits;
            created->flags = elf::shf_alloc | elf::shf_write;
            section = created.get();
            entry.file->sections.push_back(std::move(created));
        }
        const auto alignment = std::max<std::uint64_t>(entry.common_alignment, 1);
        section->alignment = std::max(section->alignment, alignment);
        section->size = (section->size + alignment - 1) / alignment * alignment;
        entry.kind = Symbol::Kind::Defined;
        entry.section = section;
        entry.value = section->size;
        section->size += entry.size;
    }
}

InputSection* Link::defining_section(const InputFile& file, std::uint32_t index) const {
    if (index == 0 || index >= file.symbols.size()) return nullptr;
    const auto& entry = file.symbols[index];
    if (entry.global) return entry.global->defined() ? entry.global->section : nullptr;
    if (entry.shndx == 0 || entry.shndx >= file.sections.size()) return nullptr;
    return file.sections[entry.shndx].get();
}

std::vector<InputSection*> Link::included_sections() const {
    std::vector<InputSection*> result;
    for (const auto& file : files_) {
        for (const auto& section : file->sections) {
            if (section && section->included()) result.push_back(section.get());
        }
    }
    return result;
}

void Link::collect_garbage() {
    std::vector<InputSection*> work;
    const auto mark = [&](InputSection* section) {
        if (!section || section->live || section->discarded) return;
        section->live = true;
        work.push_back(section);
    };
    for (const auto& file : files_) {
        for (const auto& section : file->sections) {
            if (section) section->live = false;
        }
    }
    for (const auto& file : files_) {
        for (const auto& section : file->sections) {
            if (!section || section->discarded) continue;
            if (section->keep || reserved_section(*section)) mark(section.get());
        }
    }
    const auto mark_symbol = [&](const std::string& name) {
        if (const auto* target = find(name); target && target->defined()) mark(target->section);
    };
    mark_symbol(options_.entry.value_or(script_.entry.value_or("_start")));
    for (const auto& name : options_.undefined) mark_symbol(name);
    for (const auto& name : script_.externs) mark_symbol(name);
    for (const auto& name : script_references_) mark_symbol(name);
    // --gc-keep-exported: global symbols that other modules could use.
    if (options_.keep_exported) {
        for (const auto& entry : symbols_) {
            const auto visibility = entry.other & 3U;
            if (entry.defined() && !entry.script && entry.section &&
                visibility != elf::stv_hidden && visibility != elf::stv_internal) {
                mark(entry.section);
            }
        }
    }
    while (!work.empty()) {
        auto* section = work.back();
        work.pop_back();
        if (section->group) {
            for (const auto member : section->file->groups[*section->group].members) {
                if (member < section->file->sections.size()) {
                    mark(section->file->sections[member].get());
                }
            }
        }
        // Unwind tables do not keep the code they describe; the FDEs of
        // live code keep their LSDAs and personality routines.
        if (!section->alloc() || section->name == ".eh_frame") continue;
        for (const auto& relocation : section->relocations) {
            mark(defining_section(*section->file, relocation.symbol.value));
        }
        mark_frames(*section, mark);
    }
    // Non-allocated sections stay with a file that keeps an allocated
    // section: its debugging sections and those without relocations (so
    // MIPS .pdr goes), as in GNU ld.
    for (const auto& file : files_) {
        const bool kept = std::any_of(file->sections.begin(), file->sections.end(), [](const auto& section) {
            return section && section->live && section->alloc() && section->type != elf::sht_note;
        });
        if (!kept) continue;
        for (const auto& section : file->sections) {
            if (section && !section->alloc() && !section->group &&
                (debugging_section(section->name) || section->relocations.empty())) {
                mark(section.get());
            }
        }
    }
    // Undefined symbols that no live section refers to leave the symbol
    // table, as GNU ld hides them.
    for (const auto& file : files_) {
        for (const auto& section : file->sections) {
            if (!section || !section->included() || (!section->alloc() && !options_.relocatable)) continue;
            for (const auto& relocation : section->relocations) {
                const auto index = relocation.symbol.value;
                if (index < file->symbols.size() && file->symbols[index].global) {
                    live_references_.insert(file->symbols[index].global);
                }
            }
        }
    }
    for (const auto& file : files_) {
        for (const auto& section : file->sections) {
            if (section && !section->discarded && !section->live) {
                if (options_.print_gc_sections) {
                    diagnostics_.note({}, "removing unused section '" + section->name +
                                              "' in file '" + file->name() + "'");
                }
            }
        }
    }
}

bool Link::gc_root_defined() const {
    std::vector<std::string> roots = options_.undefined;
    roots.push_back(options_.entry.value_or("_start"));
    return std::any_of(roots.begin(), roots.end(), [&](const std::string& name) {
        const auto* target = find(name);
        return target && target->defined() && target->section;
    });
}

bool Link::check_undefined() {
    std::map<const Symbol*, std::vector<std::string>> references;
    std::vector<const Symbol*> order;
    for (const auto& file : files_) {
        for (const auto& section : file->sections) {
            if (!section || !section->included()) continue;
            for (const auto& relocation : section->relocations) {
                const auto index = relocation.symbol.value;
                if (index == 0 || index >= file->symbols.size()) continue;
                const auto* target = file->symbols[index].global;
                if (!target || target->defined() || target->weak ||
                    target_->implicit_symbol(target->name)) {
                    continue;
                }
                auto& list = references[target];
                if (list.empty()) order.push_back(target);
                list.push_back(section->location(relocation.offset));
            }
        }
    }
    for (const auto* target : order) {
        const auto& list = references[target];
        for (std::size_t index = 0; index < list.size() && index < 3; ++index) {
            diagnostics_.command_error(list[index] + ": undefined reference to '" + target->name + "'");
        }
        if (list.size() > 3) {
            diagnostics_.note({}, std::to_string(list.size() - 3) + " more references to '" +
                                      target->name + "'");
        }
    }
    return order.empty();
}

SymbolValue Link::symbol_value(const InputFile& file, std::uint32_t index) const {
    SymbolValue result;
    if (index == 0 || index >= file.symbols.size()) return result;
    const auto& entry = file.symbols[index];
    result.name = entry.name;
    if (entry.global) {
        const auto& target = *entry.global;
        if (!target.defined()) {
            result.undefined = true;
            return result;
        }
        if (target.section) {
            if (!target.section->included()) {
                result.removed = true;
                return result;
            }
            result.value = section_address(*target.section, target.value);
        } else {
            result.value = target.value;
        }
        return result;
    }
    result.local = true;
    result.section = entry.type == elf::stt_section;
    if (entry.shndx == shn_abs) {
        result.value = entry.value;
        return result;
    }
    if (entry.shndx == 0) {
        result.undefined = true;
        return result;
    }
    const auto* section = entry.shndx < file.sections.size() ? file.sections[entry.shndx].get()
                                                             : nullptr;
    if (!section || !section->included()) {
        result.removed = true;
        return result;
    }
    if (section->merged && result.section) {
        result.merge = section;
        return result;
    }
    result.value = section_address(*section, entry.value);
    return result;
}

void Link::prune_records() {
    for (const auto& file : files_) {
        for (const auto& section : file->sections) {
            if (!section || !section->included() || section->relocations.empty()) continue;
            target_->prune(*section, [&](std::uint32_t index) { return foreign(*file, index); });
        }
    }
}

bool Link::foreign(const InputFile& file, std::uint32_t index) const {
    // GNU ld's test: no symbol, a definition another file supplies, or a
    // section the link dropped.
    if (index == 0 || index >= file.symbols.size()) return true;
    const auto& entry = file.symbols[index];
    if (entry.global) {
        const auto* section = entry.global->defined() ? entry.global->section : nullptr;
        return section && (section->file != &file || !section->included());
    }
    if (entry.shndx == 0 || entry.shndx >= file.sections.size()) return false;
    const auto* section = file.sections[entry.shndx].get();
    return section && !section->included();
}

std::optional<std::uint64_t> Link::gp() const {
    const auto* target = find("_gp");
    if (!target || !target->defined()) return std::nullopt;
    if (target->section) return section_address(*target->section, target->value);
    return target->value;
}

std::optional<std::uint64_t> Link::tls_base() const {
    // The TLS segment starts at its lowest TLS output section.
    std::optional<std::uint64_t> base;
    for (const auto& step : steps_) {
        const auto* output = step.placement && step.placement->emitted ? step.placement->output : nullptr;
        if (!output || !output->alloc() || (output->flags & elf::shf_tls) == 0) continue;
        if (!base || output->address < *base) base = output->address;
    }
    return base;
}

bool Link::check_cross_references() {
    // NOCROSSREFS: no relocation in one of the listed output sections may
    // refer to a symbol in another (with NOCROSSREFS_TO, in the first).
    if (script_.cross_references.empty()) return true;
    const auto listed = [](const script::CrossReferences& list, const OutputSection* section) {
        return section &&
               std::find(list.sections.begin(), list.sections.end(), section->name) != list.sections.end();
    };
    bool ok = true;
    for (const auto& file : files_) {
        for (const auto& section : file->sections) {
            if (!section || !section->included() || !section->output) continue;
            for (const auto& relocation : section->relocations) {
                const auto* target = defining_section(*file, relocation.symbol.value);
                if (!target || !target->included() || target->output == section->output) continue;
                for (const auto& list : script_.cross_references) {
                    const bool prohibited =
                        list.to ? (target->output->name == list.sections.front() &&
                                   std::find(list.sections.begin() + 1, list.sections.end(),
                                             section->output->name) != list.sections.end())
                                : (listed(list, section->output) && listed(list, target->output));
                    if (!prohibited) continue;
                    const auto& name = file->symbols[relocation.symbol.value].name;
                    diagnostics_.command_error(section->location(relocation.offset) +
                                               ": prohibited cross reference from " + section->output->name +
                                               " to '" + name + "' in " + target->output->name);
                    ok = false;
                    break;
                }
            }
        }
    }
    return ok;
}

bool Link::run() {
    if (options_.script && options_.relocatable) {
        diagnostics_.command_error("-r does not take a linker script");
        return false;
    }
    if (options_.script) {
        std::string message;
        const auto* source = sources_.load(*options_.script, message);
        if (!source) {
            diagnostics_.command_error(message);
            return false;
        }
        if (!script::parse(*source, script_, options_.library_paths, sources_, diagnostics_)) {
            return false;
        }
    }
    for (const auto& definition : options_.definitions) {
        const auto* source = sources_.add("--defsym", definition);
        if (!script::parse_definition(*source, script_, sources_, diagnostics_)) return false;
    }
    each_expression(script_, [&](const script::Expression& expression) {
        collect_references(expression, script_references_);
    });
    // Symbols the script defines need no archive member; the entry, -u, and
    // EXTERN symbols and the symbols the script reads may pull one in.
    define_script_symbols();
    for (const auto& name : options_.undefined) require(name);
    for (const auto& name : script_.externs) require(name);
    if (options_.entry || script_.entry) require(options_.entry.value_or(script_.entry.value_or("")));
    for (const auto& name : script_references_) require(name);
    if (!read_inputs()) return false;
    // A script with INSERT augments the default script instead of
    // replacing it.
    if (!options_.relocatable && (!options_.script || script_.has_insert)) {
        const auto* source = sources_.add("<default script>", target_->default_script(false));
        if (!script::parse(*source, script_, {}, sources_, diagnostics_)) return false;
    }
    each_expression(script_, [&](const script::Expression& expression) {
        collect_references(expression, script_references_);
    });
    define_script_symbols();
    target_->merge_sections(included_sections(), diagnostics_);
    if (diagnostics_.errors() != 0) return false;
    if (options_.relocatable) {
        // A relocatable link keeps common symbols common unless -d.
        if (options_.define_common) allocate_commons();
        parse_frames();
        if (options_.gc_sections) {
            if (!options_.keep_exported && !gc_root_defined()) {
                diagnostics_.command_error("--gc-sections requires a defined symbol root specified by -e or -u");
                return false;
            }
            collect_garbage();
        }
        edit_frames(true);
        return write_relocatable();
    }
    allocate_commons();
    parse_frames();
    if (options_.eh_frame_header) create_frame_header();
    for (auto& created : target_->create_sections(included_sections())) {
        created->keep = true;
        created->file->sections.push_back(std::move(created));
    }
    for (const auto& [name, section] : target_->created_symbols()) {
        auto& defined = symbol(name);
        if (defined.defined()) continue;
        defined.kind = Symbol::Kind::Defined;
        defined.section = section;
        defined.value = 0;
        defined.type = elf::stt_object;
        defined.other = elf::stv_hidden;
        defined.file = section->file;
    }
    if (!place_sections()) return false;
    if (options_.gc_sections) collect_garbage();
    for (auto& placement : placements_) {
        for (auto& item : placement->items) {
            std::erase_if(item.sections, [](const InputSection* section) { return !section->included(); });
            // GNU ld leaves out empty output sections before it edits
            // .eh_frame and .pdr, so editing does not remove a section.
            for (const auto* section : item.sections) {
                if (section->size != 0) placement->keep = true;
            }
        }
    }
    if (!check_undefined()) return false;
    target_->size_sections(included_sections());
    prune_records();
    // Editing .eh_frame may exclude trailing input sections, which keep
    // their place without contents (GNU ld's SEC_EXCLUDE).
    edit_frames(false);
    merge_sections();
    // SIZEOF_HEADERS counts the program headers, which the layout decides.
    for (int attempt = 0;; ++attempt) {
        if (!layout() || !build_segments()) return false;
        if (!uses_headers_size_ || segments_.size() == segment_estimate_ || attempt == 4) break;
        segment_estimate_ = segments_.size();
    }
    if (!check_cross_references()) return false;
    target_->finish_sections(gp());
    assign_offsets();
    if (!write_contents()) return false;
    if (!write_executable()) return false;
    if (options_.map) write_map();
    return diagnostics_.errors() == 0;
}

bool link(const Options& options, TargetLookup lookup, Diagnostics& diagnostics) {
    Link session(options, lookup, diagnostics);
    return session.run();
}

} // namespace cross::linker
