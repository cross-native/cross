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
                  section->fill.get()}) {
                if (expression) visit(*expression);
            }
            for (const auto& item : section->items) {
                if (const auto* inner = std::get_if<script::Assignment>(&item)) {
                    visit(*inner->value);
                } else if (const auto* data = std::get_if<script::Data>(&item)) {
                    visit(*data->value);
                } else if (const auto* check = std::get_if<script::Assert>(&item)) {
                    visit(*check->condition);
                }
            }
        }
    }
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

bool Link::read_inputs() {
    struct Loaded {
        const Input* input;
        std::string path;
        std::vector<std::uint8_t> bytes;
    };
    std::vector<Loaded> loaded;
    bool ok = true;
    for (const auto& input : options_.inputs) {
        std::string path = input.name;
        if (input.kind == Input::Kind::Library) {
            const auto found = find_library(input.name);
            if (!found) {
                diagnostics_.command_error("cannot find -l" + input.name);
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
        loaded.push_back({&input, std::move(path), std::move(*bytes)});
    }
    if (!ok) return false;

    // The first ELF object, possibly an archive member, decides what the
    // command line leaves open.
    std::optional<assembler::ObjectInfo> first;
    for (const auto& item : loaded) {
        if (item.input->kind == Input::Kind::Binary) continue;
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
        if (!open_input(*item.input, std::move(item.bytes), item.path)) ok = false;
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
            if (section && section->alloc()) section->live = false;
        }
    }
    for (const auto& file : files_) {
        for (const auto& section : file->sections) {
            if (!section || section->discarded) continue;
            if (!section->alloc()) {
                section->live = true;
            } else if (section->keep || reserved_section(*section)) {
                mark(section.get());
            }
        }
    }
    const auto mark_symbol = [&](const std::string& name) {
        if (const auto* target = find(name); target && target->defined()) mark(target->section);
    };
    mark_symbol(options_.entry.value_or(script_.entry.value_or("_start")));
    for (const auto& name : options_.undefined) mark_symbol(name);
    for (const auto& name : script_.externs) mark_symbol(name);
    for (const auto& name : script_references_) mark_symbol(name);
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
        // Unwind tables do not keep the code they describe.
        if (!section->alloc() || section->name == ".eh_frame") continue;
        for (const auto& relocation : section->relocations) {
            mark(defining_section(*section->file, relocation.symbol.value));
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
                if (!target || target->defined() || target->weak) continue;
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
    const auto require = [&](const std::string& name) {
        auto& target = symbol(name);
        if (!target.defined()) reference(target, false, nullptr);
    };
    for (const auto& name : options_.undefined) require(name);
    for (const auto& name : script_.externs) require(name);
    if (options_.entry || script_.entry) require(options_.entry.value_or(script_.entry.value_or("")));
    for (const auto& name : script_references_) require(name);
    if (!read_inputs()) return false;
    if (!options_.script && !options_.relocatable) {
        const auto* source = sources_.add("<default script>", target_->default_script());
        if (!script::parse(*source, script_, {}, sources_, diagnostics_)) return false;
        each_expression(script_, [&](const script::Expression& expression) {
            collect_references(expression, script_references_);
        });
    }
    define_script_symbols();
    target_->merge_sections(included_sections(), diagnostics_);
    if (diagnostics_.errors() != 0) return false;
    // A relocatable link keeps common symbols common.
    if (options_.relocatable) return write_relocatable();
    allocate_commons();
    if (!place_sections()) return false;
    if (options_.gc_sections) collect_garbage();
    for (auto& placement : placements_) {
        for (auto& item : placement->items) {
            std::erase_if(item.sections, [](const InputSection* section) { return !section->included(); });
        }
    }
    if (!check_undefined()) return false;
    prune_records();
    merge_sections();
    // SIZEOF_HEADERS counts the program headers, which the layout decides.
    for (int attempt = 0;; ++attempt) {
        if (!layout() || !build_segments()) return false;
        if (!uses_headers_size_ || segments_.size() == segment_estimate_ || attempt == 4) break;
        segment_estimate_ = segments_.size();
    }
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
