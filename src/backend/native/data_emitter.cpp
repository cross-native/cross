// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend/native/data_emitter.hpp"

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "middle/codegen_module.hpp"
#include "target/assembly_format.hpp"
#include "target/subtarget.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <sstream>
#include <string>
#include <string_view>

namespace cross::native {
namespace {

bool safe_assembly_text(std::string_view text) {
    return !text.empty() &&
           std::all_of(text.begin(), text.end(), [](unsigned char ch) {
               return ch >= 0x20 && ch != 0x7f && ch != '"' && ch != '\\';
           });
}

AssemblySymbolVisibility assembly_visibility(
    hir::SymbolVisibility visibility) {
    switch (visibility) {
    case hir::SymbolVisibility::Default:
        return AssemblySymbolVisibility::Default;
    case hir::SymbolVisibility::Hidden:
        return AssemblySymbolVisibility::Hidden;
    case hir::SymbolVisibility::Protected:
        return AssemblySymbolVisibility::Protected;
    case hir::SymbolVisibility::Internal:
        return AssemblySymbolVisibility::Internal;
    }
    return AssemblySymbolVisibility::Default;
}

void emit_bits(std::ostringstream& out, UInt128 value, unsigned bytes,
               ByteOrder byte_order) {
    value = mask_to(value, bytes * 8);
    if (bytes == 1) out << "\t.byte " << value.low << '\n';
    else if (bytes == 2) out << "\t.short " << value.low << '\n';
    else if (bytes == 4) out << "\t.long " << value.low << '\n';
    else if (bytes == 8) out << "\t.quad " << value.low << '\n';
    else if (bytes == 16 && byte_order == ByteOrder::Little) {
        out << "\t.quad " << value.low << "\n\t.quad " << value.high << '\n';
    } else if (bytes == 16) {
        out << "\t.quad " << value.high << "\n\t.quad " << value.low << '\n';
    } else {
        for (unsigned index = 0; index < bytes; ++index) {
            const auto offset = byte_order == ByteOrder::Little
                                    ? index
                                    : bytes - index - 1;
            const auto byte = offset < 8
                                  ? (value.low >> (offset * 8)) & 0xffU
                                  : (value.high >> ((offset - 8) * 8)) & 0xffU;
            out << "\t.byte " << byte << '\n';
        }
    }
}

class Emitter {
public:
    Emitter(const codegen::ModuleView& module,
            const Subtarget& subtarget, const CompilerOptions& options,
            Diagnostics& diagnostics, bool patch_owned = false)
        : module_(module), options_(options), diagnostics_(diagnostics),
          format_(subtarget.object_format()), patch_owned_(patch_owned) {}

    std::string run() {
        if (format_ == ObjectFormat::Unsupported) {
            for (const auto& object : module_.data().objects) {
                if (patch_owned_ ==
                    module_.raw_assembly().owns(object.source)) {
                    diagnostics_.error(
                        object.location,
                        "native data emission is not implemented for this "
                        "object format");
                }
            }
            return {};
        }
        bool emitted_object = false;
        for (const auto& object : module_.data().objects) {
            if (patch_owned_ ==
                module_.raw_assembly().owns(object.source)) {
                emit_object(object);
                emitted_object = true;
            }
        }
        if (!patch_owned_) emit_symbol_indirections();
        // GCC's top-level assembly is emitted before its generated functions
        // and inherits the final section. Leave embedded patch data in the
        // ordinary text section so subsequent compiler output remains code.
        if (patch_owned_ && emitted_object) out_ << ".text\n";
        if (!patch_owned_ && format_ == ObjectFormat::Coff &&
            std::any_of(module_.data().objects.begin(),
                        module_.data().objects.end(),
                        [&](const data::Object& object) {
                            return object.is_thread_local &&
                                   !module_.raw_assembly().owns(object.source);
                        })) {
            emit_coff_tls_support();
        }
        return out_.str();
    }

private:
    void emit_alias(std::string_view alias_name,
                    std::string_view target_name, bool weak,
                    hir::SymbolVisibility visibility, bool function,
                    unsigned object_size, bool retain,
                    SourceLocation location) {
        if (weak && format_ == ObjectFormat::MachO) {
            diagnostics_.error(
                location,
                "weak aliases are not representable in Mach-O");
            return;
        }
        if (!safe_assembly_text(alias_name) ||
            !safe_assembly_text(target_name)) {
            diagnostics_.error(
                location,
                "alias link name cannot be represented by the selected assembler");
            return;
        }
        const auto alias = assembly_symbol(format_, alias_name);
        const auto target = assembly_symbol(format_, target_name);
        std::string error;
        const auto directives = assembly_symbol_directives(
            format_, {alias, true, true, weak,
                      assembly_visibility(visibility)},
            error);
        if (!directives) {
            diagnostics_.error(location, error);
            return;
        }
        if (!directives->empty()) out_ << *directives << '\n';
        out_ << ".set " << alias << ',' << target << '\n';
        if (format_ == ObjectFormat::Elf) {
            out_ << ".type " << alias
                 << (function ? ",@function\n" : ",@object\n");
            if (!function) {
                out_ << ".size " << alias << ", " << object_size << '\n';
            }
        } else if (format_ == ObjectFormat::Coff) {
            out_ << ".def " << alias << "; .scl 2; .type "
                 << (function ? "32" : "0") << "; .endef\n";
        }
        if (retain && format_ == ObjectFormat::Coff) {
            out_ << ".section .drectve\n.ascii \" -include:"
                 << alias_name << "\"\n";
        } else if (retain && format_ == ObjectFormat::MachO) {
            out_ << ".no_dead_strip " << alias << '\n';
        }
    }

    void emit_weak_reference(std::string_view target,
                             SourceLocation location) {
        if (!safe_assembly_text(target)) {
            diagnostics_.error(
                location,
                "weakref link name cannot be represented by the selected assembler");
            return;
        }
        const auto name = assembly_symbol(format_, target);
        std::string error;
        const auto directives = assembly_symbol_directives(
            format_, {name, true, false, true,
                      AssemblySymbolVisibility::Default},
            error);
        if (!directives) {
            diagnostics_.error(location, error);
        } else if (!directives->empty()) {
            out_ << *directives << '\n';
        }
    }

    void emit_symbol_indirections() {
        for (const auto& function : module_.hir().functions) {
            if (function.alias_target) {
                emit_alias(
                    function.link_symbol, *function.alias_target,
                    function.weak, function.visibility, true, 0,
                    function.retain, function.location);
            } else if (function.weakref_target) {
                emit_weak_reference(*function.weakref_target,
                                    function.location);
            }
        }
        for (const auto& object : module_.data().objects) {
            const auto& entity = module_.hir().object(object.source);
            if (entity.alias_target) {
                emit_alias(
                    entity.link_symbol, *entity.alias_target, entity.weak,
                    entity.visibility, false, object.size, object.retain,
                    entity.location);
            }
        }
    }

    void emit_coff_tls_support() {
        // PE static TLS needs an image-local index and IMAGE_TLS_DIRECTORY.
        // Emit them as discardable COMDATs so separately compiled Cross
        // objects coalesce and a user-supplied platform definition can win.
        // This is data-only loader metadata: accessing TLS never inserts a
        // compiler-owned function call or runtime-library dependency.
        out_ << ".section .tls,\"dw\",discard,_tls_start\n"
                ".globl _tls_start\n"
                "_tls_start:\n"
                ".section .tls$ZZZ,\"dw\",discard,_tls_end\n"
                ".globl _tls_end\n"
                "_tls_end:\n"
                ".section .bss$_tls_index,\"bw\",discard,_tls_index\n"
                ".globl _tls_index\n"
                ".p2align 2\n"
                "_tls_index:\n"
                "\t.long 0\n"
                ".section .rdata$_tls_used,\"dr\",discard,_tls_used\n"
                ".globl _tls_used\n"
                ".p2align 3\n"
                "_tls_used:\n"
                "\t.quad _tls_start\n"
                "\t.quad _tls_end\n"
                "\t.quad _tls_index\n"
                "\t.quad 0\n"
                "\t.long 0\n"
                "\t.long 0\n";
    }

    bool select_section(const hir::Object& entity,
                        const data::Object& object) {
        const bool patched = std::ranges::any_of(
            module_.raw_assembly().patch_relocations,
            [&](const mir::AssemblyBundle::PatchRelocation& relocation) {
                return relocation.sink.object == object.source;
            });
        const bool zero = object.initializer == data::InitializerKind::Zero &&
                          !object.read_only && !patched;
        const bool uninitialized =
            object.initializer == data::InitializerKind::Uninitialized;
        const bool split_section = options_.data_sections || object.retain;
        const auto section = entity.section
            ? *entity.section
            : object.is_thread_local
                  ? format_ == ObjectFormat::Coff
                        ? std::string(".tls$") +
                              (split_section ? entity.link_symbol : "")
                        : std::string(zero || uninitialized ? ".tbss" :
                                                                  ".tdata") +
                              (split_section ? "." + entity.link_symbol : "")
            : uninitialized
                  ? std::string(".noinit") +
                        (split_section ? "." + entity.link_symbol : "")
            : split_section
                  ? std::string(
                        object.read_only
                            ? (format_ == ObjectFormat::Coff ? ".rdata$"
                                                            : ".rodata.")
                            : zero
                                  ? (format_ == ObjectFormat::Coff ? ".bss$"
                                                                  : ".bss.")
                            : (format_ == ObjectFormat::Coff ? ".data$"
                                                            : ".data.")) +
                        entity.link_symbol
                  : object.read_only
                        ? std::string(format_ == ObjectFormat::Coff ? ".rdata"
                                                                  : ".rodata")
                        : zero ? std::string(".bss")
                               : std::string(".data");
        if (!safe_assembly_text(section)) {
            diagnostics_.error(
                object.location,
                "object section name cannot be represented by the selected "
                "assembler");
            return false;
        }
        const auto kind = object.is_thread_local
            ? (zero || uninitialized
                   ? AssemblySectionKind::ThreadZeroFill
                   : AssemblySectionKind::ThreadData)
            : uninitialized
            ? AssemblySectionKind::NoInit
            : zero ? AssemblySectionKind::ZeroFill
            : object.read_only ? AssemblySectionKind::ReadOnlyData
                               : AssemblySectionKind::WritableData;
        std::string error;
        const auto directive = assembly_section_directive(
            format_, {section, kind, entity.section.has_value(), object.retain},
            error);
        if (!directive) {
            diagnostics_.error(object.location, error);
            return false;
        }
        out_ << *directive << '\n';
        return true;
    }

    std::string address(const data::AddressConstant& address) const {
        std::string result;
        if (address.kind == data::AddressKind::Object) {
            result = assembly_symbol(
                format_, module_.hir().object(*address.object).link_symbol);
        } else if (address.kind == data::AddressKind::Function) {
            result = assembly_symbol(
                format_,
                module_.hir().function(*address.function).link_symbol);
        } else {
            const auto& label =
                module_.hir().labels.at(address.label->value);
            result = label.is_global
                         ? assembly_symbol(format_, label.link_symbol)
                         : ".Lcross.label." +
                               std::to_string(address.function->value) + '.' +
                               std::to_string(address.label->value);
        }
        if (address.addend > 0) {
            result += '+' + std::to_string(address.addend);
        } else if (address.addend < 0) {
            result += std::to_string(address.addend);
        }
        return result;
    }

    void emit_initializer(const data::Object& object) {
        std::vector<const mir::AssemblyBundle::PatchRelocation*> patches;
        for (const auto& patch :
             module_.raw_assembly().patch_relocations) {
            if (patch.sink.object == object.source) patches.push_back(&patch);
        }
        if (!patches.empty()) {
            emit_patched_initializer(object, std::move(patches));
            return;
        }
        switch (object.initializer) {
        case data::InitializerKind::Declaration: return;
        case data::InitializerKind::Zero:
            out_ << "\t.zero " << object.size << '\n';
            return;
        case data::InitializerKind::Uninitialized:
            out_ << "\t.space " << object.size << '\n';
            return;
        case data::InitializerKind::Integer:
            emit_bits(out_, object.bits, object.size,
                      module_.data().byte_order);
            return;
        case data::InitializerKind::Floating: {
            const auto& type = module_.hir().type(object.type);
            if (type.kind == hir::Type::Kind::Builtin &&
                type.builtin == BuiltinType::F80 && object.size >= 10 &&
                module_.data().byte_order == ByteOrder::Little) {
                out_ << "\t.quad " << object.bits.low << "\n\t.short "
                     << (object.bits.high & 0xffffU) << '\n';
                if (object.size > 10) {
                    out_ << "\t.zero " << object.size - 10 << '\n';
                }
            } else {
                emit_bits(out_, object.bits, object.size,
                          module_.data().byte_order);
            }
            return;
        }
        case data::InitializerKind::Address:
            if (object.size == 4) {
                out_ << "\t.long " << address(*object.address) << '\n';
            } else if (object.size == 8) {
                out_ << "\t.quad " << address(*object.address) << '\n';
            } else {
                diagnostics_.error(
                    object.location,
                    "target has no scalar relocation directive for this "
                    "address width");
            }
            return;
        case data::InitializerKind::Bytes:
        case data::InitializerKind::Aggregate: {
            const auto emit_bytes = [&](std::size_t begin,
                                        std::size_t limit) {
                for (auto offset = begin; offset < limit;) {
                    out_ << "\t.byte ";
                    const auto end = std::min(offset + 16, limit);
                    for (auto index = offset; index < end; ++index) {
                        if (index != offset) out_ << ',';
                        out_ << static_cast<unsigned>(object.bytes[index]);
                    }
                    out_ << '\n';
                    offset = end;
                }
            };
            std::size_t offset{};
            for (const auto& relocation : object.relocations) {
                emit_bytes(offset, relocation.offset);
                if (relocation.size == 4) {
                    out_ << "\t.long " << address(relocation.address) << '\n';
                } else if (relocation.size == 8) {
                    out_ << "\t.quad " << address(relocation.address) << '\n';
                } else {
                    diagnostics_.error(
                        object.location,
                        "target has no aggregate relocation directive for this address width");
                }
                offset = relocation.offset + relocation.size;
            }
            emit_bytes(offset, object.bytes.size());
            return;
        }
        }
    }

    void emit_patched_initializer(
        const data::Object& object,
        std::vector<const mir::AssemblyBundle::PatchRelocation*> patches) {
        if (object.initializer != data::InitializerKind::Zero &&
            object.initializer != data::InitializerKind::Bytes &&
            object.initializer != data::InitializerKind::Aggregate) {
            diagnostics_.error(
                object.location,
                "$::patch sink container has an incompatible static initializer");
            return;
        }
        std::ranges::sort(
            patches, {},
            [](const mir::AssemblyBundle::PatchRelocation* patch) {
                return patch->sink.offset;
            });
        const auto emit_bytes = [&](std::size_t begin, std::size_t limit) {
            if (begin == limit) return;
            if (object.initializer == data::InitializerKind::Zero) {
                out_ << "\t.zero " << limit - begin << '\n';
                return;
            }
            for (auto offset = begin; offset < limit;) {
                out_ << "\t.byte ";
                const auto end = std::min(offset + 16, limit);
                for (auto index = offset; index < end; ++index) {
                    if (index != offset) out_ << ',';
                    out_ << static_cast<unsigned>(object.bytes[index]);
                }
                out_ << '\n';
                offset = end;
            }
        };
        struct Event {
            std::uint64_t offset{};
            unsigned size{};
            const data::Relocation* data{};
            const mir::AssemblyBundle::PatchRelocation* patch{};
        };
        std::vector<Event> events;
        events.reserve(object.relocations.size() + patches.size());
        for (const auto& relocation : object.relocations) {
            events.push_back(
                {relocation.offset, relocation.size, &relocation, nullptr});
        }
        for (const auto* patch : patches) {
            const auto storage_bytes = patch_address_storage_bytes(
                patch->sink.representation, module_.data().address_bits);
            if (!storage_bytes || patch->sink.storage_bytes != storage_bytes) {
                diagnostics_.error(object.location,
                    "target has no $::patch sink relocation for this address representation");
                return;
            }
            events.push_back(
                {patch->sink.offset, patch->sink.storage_bytes, nullptr, patch});
        }
        std::ranges::sort(events, {}, &Event::offset);
        std::uint64_t offset{};
        for (const auto& event : events) {
            if (event.offset < offset || event.offset > object.size ||
                event.size > object.size - event.offset) {
                diagnostics_.error(
                    object.location,
                    "$::patch sink relocation overlaps initialized data or exceeds its object");
                return;
            }
            emit_bytes(static_cast<std::size_t>(offset),
                       static_cast<std::size_t>(event.offset));
            if (event.patch) {
                if (event.size == 4) {
                    out_ << "\t.long ";
                } else if (event.size == 8) {
                    out_ << "\t.quad ";
                } else {
                    diagnostics_.error(
                        object.location,
                        "target has no $::patch sink relocation directive for this address width");
                    return;
                }
                out_ << event.patch->end_label << '-'
                     << event.patch->field_bytes << '\n';
            } else if (event.size == 4) {
                out_ << "\t.long " << address(event.data->address) << '\n';
            } else if (event.size == 8) {
                out_ << "\t.quad " << address(event.data->address) << '\n';
            } else {
                diagnostics_.error(
                    object.location,
                    "target has no aggregate relocation directive for this address width");
                return;
            }
            offset = event.offset + event.size;
        }
        emit_bytes(static_cast<std::size_t>(offset), object.size);
    }

    void emit_object(const data::Object& object) {
        const auto& entity = module_.hir().object(object.source);
        const auto name = assembly_symbol(format_, entity.link_symbol);
        if (object.initializer == data::InitializerKind::Declaration) {
            if (entity.alias_target) return;
            out_ << ".extern " << name << '\n';
            std::string symbol_error;
            const auto directives = assembly_symbol_directives(
                format_, {name, true, false,
                          entity.weakref_target.has_value(),
                          assembly_visibility(entity.visibility)},
                symbol_error);
            if (!directives) {
                diagnostics_.error(object.location, symbol_error);
            } else if (!directives->empty()) {
                out_ << *directives << '\n';
            }
            return;
        }
        if (!select_section(entity, object)) return;
        out_ << ".p2align " << std::countr_zero(object.alignment) << '\n';
        std::string symbol_error;
        const auto directives = assembly_symbol_directives(
            format_, {name, entity.linkage == Linkage::Global, true,
                      entity.weak, assembly_visibility(entity.visibility)},
            symbol_error);
        if (!directives) {
            diagnostics_.error(object.location, symbol_error);
            return;
        }
        if (!directives->empty()) out_ << *directives << '\n';
        if (format_ == ObjectFormat::Elf) {
            out_ << ".type " << name
                 << (object.is_thread_local ? ",@tls_object\n" : ",@object\n");
        } else if (format_ == ObjectFormat::Coff) {
            out_ << ".def " << name << "; .scl "
                 << (entity.linkage == Linkage::Global ? "2" : "3")
                 << "; .type 0; .endef\n";
        }
        out_ << name << ":\n";
        emit_initializer(object);
        if (format_ == ObjectFormat::Elf) {
            out_ << ".size " << name << ", " << object.size << '\n';
        }
        if (object.retain && format_ == ObjectFormat::Coff &&
            entity.linkage == Linkage::Global) {
            out_ << ".section .drectve\n.ascii \" -include:"
                 << entity.link_symbol << "\"\n";
        } else if (object.retain && format_ == ObjectFormat::MachO) {
            out_ << ".no_dead_strip " << name << '\n';
        }
    }

    const codegen::ModuleView& module_;
    const CompilerOptions& options_;
    Diagnostics& diagnostics_;
    ObjectFormat format_;
    bool patch_owned_{};
    std::ostringstream out_;
};

} // namespace

std::string emit_data_assembly(const codegen::ModuleView& module,
                               const Subtarget& subtarget,
                               const CompilerOptions& options,
                               Diagnostics& diagnostics) {
    return Emitter(module, subtarget, options, diagnostics).run();
}

std::string emit_patch_data_assembly(const codegen::ModuleView& module,
                                     const Subtarget& subtarget,
                                     const CompilerOptions& options,
                                     Diagnostics& diagnostics) {
    return Emitter(module, subtarget, options, diagnostics, true).run();
}

} // namespace cross::native
