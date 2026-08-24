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

std::string quoted(std::string_view value) {
    return '"' + std::string(value) + '"';
}

std::string symbol(std::string_view name) {
    const bool simple =
        !name.empty() &&
        std::all_of(name.begin(), name.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') ||
                   (ch >= 'A' && ch <= 'Z') ||
                   (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' ||
                   ch == '$';
        });
    return simple ? std::string(name) : quoted(name);
}

void emit_bits(std::ostringstream& out, UInt128 value, unsigned bytes,
               ByteOrder byte_order) {
    value = mask_to(value, bytes * 8);
    if (bytes == 1) out << "\t.byte " << value.low << '\n';
    else if (bytes == 2) out << "\t.word " << value.low << '\n';
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
            Diagnostics& diagnostics)
        : module_(module), options_(options), diagnostics_(diagnostics),
          format_(subtarget.object_format()) {}

    std::string run() {
        if (format_ == ObjectFormat::Unsupported) {
            for (const auto& object : module_.data().objects) {
                if (!module_.raw_assembly().owns(object.source)) {
                    diagnostics_.error(
                        object.location,
                        "native data emission is not implemented for this "
                        "object format");
                }
            }
            return {};
        }
        for (const auto& object : module_.data().objects) {
            if (!module_.raw_assembly().owns(object.source)) emit_object(object);
        }
        if (format_ == ObjectFormat::Coff &&
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
        const bool zero = object.initializer == data::InitializerKind::Zero &&
                          !object.read_only;
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
            result = symbol(module_.hir().object(*address.object).link_symbol);
        } else if (address.kind == data::AddressKind::Function) {
            result = symbol(module_.hir().function(*address.function).link_symbol);
        } else {
            result = ".Lcross.label." +
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
                out_ << "\t.quad " << object.bits.low << "\n\t.word "
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
        }
    }

    void emit_object(const data::Object& object) {
        const auto& entity = module_.hir().object(object.source);
        const auto name = symbol(entity.link_symbol);
        if (object.initializer == data::InitializerKind::Declaration) {
            out_ << ".extern " << name << '\n';
            return;
        }
        if (!select_section(entity, object)) return;
        out_ << ".p2align " << std::countr_zero(object.alignment) << '\n';
        if (entity.linkage == Linkage::Global) out_ << ".globl " << name << '\n';
        else if (format_ == ObjectFormat::Elf) out_ << ".local " << name << '\n';
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
    std::ostringstream out_;
};

} // namespace

std::string emit_data_assembly(const codegen::ModuleView& module,
                               const Subtarget& subtarget,
                               const CompilerOptions& options,
                               Diagnostics& diagnostics) {
    return Emitter(module, subtarget, options, diagnostics).run();
}

} // namespace cross::native
