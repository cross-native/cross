// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "backend/llvm/llvm_ir.hpp"

#include "backend/llvm/mir_ir.hpp"

#include "common/uint128.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <bit>
#include <iomanip>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace cross::debug {
namespace {

std::string llvm_string(std::string_view text) {
    std::ostringstream out;
    out << '"';
    for (const char byte : text) {
        const auto ch = static_cast<unsigned char>(byte);
        if (ch >= 0x20 && ch <= 0x7e && ch != '"' && ch != '\\') {
            out << static_cast<char>(ch);
        } else {
            constexpr char digits[] = "0123456789ABCDEF";
            out << '\\' << digits[ch >> 4] << digits[ch & 15];
        }
    }
    out << '"';
    return out.str();
}

std::string symbol_name(std::string_view text) {
    return "@" + llvm_string(text);
}

std::string llvm_visibility(hir::SymbolVisibility visibility) {
    switch (visibility) {
    case hir::SymbolVisibility::Default: return {};
    case hir::SymbolVisibility::Hidden: return "hidden ";
    case hir::SymbolVisibility::Protected: return "protected ";
    case hir::SymbolVisibility::Internal: return "hidden ";
    }
    return {};
}

std::string hexadecimal(std::uint64_t value, unsigned width) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0')
        << std::setw(static_cast<int>(width)) << value;
    return out.str();
}

class ModuleEmitter {
public:
    ModuleEmitter(const CompilerOptions& options, Diagnostics& diagnostics,
                  const codegen::ModuleView& module)
        : options_(options), diagnostics_(diagnostics), hir_(module.hir()),
          data_(module.data()), managed_(module.managed()),
          raw_assembly_(module.raw_assembly()) {}

    std::string run() {
        for (const auto& label : hir_.labels) {
            if (!label.is_global || !label.definition ||
                raw_assembly_.owns(label.owner)) {
                continue;
            }
            diagnostics_.error(
                label.location,
                "LLVM debug serialization cannot define an externally named global label");
            return {};
        }
        std::ostringstream module;
        module << "; Cross language 0.9\n"
                  "source_filename = \"cross compilation group\"\n"
               << "target triple = " << llvm_string(options_.target)
               << "\n\n";
        if (!raw_assembly_.module_assembly.empty()) {
            module << "module asm "
                   << llvm_string(raw_assembly_.module_assembly) << "\n\n";
        }

        emit_objects(module);
        emit_declarations(module);
        emit_definitions(module);
        emit_aliases(module);
        emit_retention(module);
        emit_intrinsic_declarations(module);
        return module.str();
    }

private:
    std::string ir_type(hir::TypeId id) const {
        const auto& type = hir_.type(id);
        if (type.kind == hir::Type::Kind::Pointer) return "ptr";
        if (type.kind == hir::Type::Kind::Vector && type.element) {
            return type.scalable
                       ? "<vscale x " + std::to_string(type.lanes) + " x " +
                             ir_type(*type.element) + ">"
                       : "<" + std::to_string(type.lanes) + " x " +
                             ir_type(*type.element) + ">";
        }
        if (type.kind == hir::Type::Kind::Array && type.element) {
            return "[" + std::to_string(type.lanes) + " x " +
                   ir_type(*type.element) + "]";
        }
        if (type.kind == hir::Type::Kind::Record && type.record) {
            return "[" + std::to_string(hir_.record(*type.record).size) +
                   " x i8]";
        }
        switch (type.builtin) {
        case BuiltinType::Void: return "void";
        case BuiltinType::Bool:
        case BuiltinType::I8:
        case BuiltinType::U8: return "i8";
        case BuiltinType::I16:
        case BuiltinType::U16: return "i16";
        case BuiltinType::I32:
        case BuiltinType::U32: return "i32";
        case BuiltinType::I64:
        case BuiltinType::U64: return "i64";
        case BuiltinType::Iptr:
        case BuiltinType::Uptr:
            return "i" + std::to_string(data_.address_bits);
        case BuiltinType::I128:
        case BuiltinType::U128: return "i128";
        case BuiltinType::F32: return "float";
        case BuiltinType::F64: return "double";
        case BuiltinType::Fptr:
            return data_.address_bits == 32 ? "float" : "double";
        case BuiltinType::F80: return "x86_fp80";
        case BuiltinType::F128: return "fp128";
        case BuiltinType::Label: return "ptr";
        }
        return "void";
    }

    std::string abi_prefix(const hir::Function& function) const {
        const auto* target = target_for_triple(options_.target);
        const auto* abi = target ? find_abi(*target, function.abi) : nullptr;
        if (!target || !abi) {
            diagnostics_.error(function.location,
                               "unsupported target ABI id " +
                                   std::to_string(function.abi.value));
            return {};
        }
        if (abi->canonical_name == target->default_abi(options_.target)) {
            return {};
        }
        return std::string(abi->llvm_calling_convention) + ' ';
    }

    void emit_declarations(std::ostringstream& module) const {
        for (const auto& function : hir_.functions) {
            const bool raw = raw_assembly_.owns(function.id);
            if ((function.definition && !raw) || function.alias_target) {
                continue;
            }
            module << "declare ";
            if (function.weakref_target) module << "extern_weak ";
            if (raw) module << "dso_local ";
            module << llvm_visibility(function.visibility)
                   << abi_prefix(function) << ir_type(function.result_type)
                   << ' ' << symbol_name(function.link_symbol) << '(';
            for (std::size_t index = 0;
                 index < function.parameters.size(); ++index) {
                if (index != 0) module << ", ";
                const auto& parameter = function.parameters[index];
                const bool manual_cell =
                    parameter.physical_location &&
                    *parameter.physical_location != "auto";
                module << (parameter.mode == ParameterMode::In &&
                                   !manual_cell
                               ? ir_type(parameter.type)
                               : std::string("ptr"));
            }
            if (function.variadic) {
                if (!function.parameters.empty()) module << ", ";
                module << "...";
            }
            module << ")\n\n";
        }
    }

    void emit_definitions(std::ostringstream& module) {
        for (const auto& function : hir_.functions) {
            if (!function.definition || raw_assembly_.owns(function.id)) {
                continue;
            }
            const auto* managed = managed_.find(function.id);
            if (!managed) {
                diagnostics_.error(
                    function.location,
                    "LLVM debug serializer received a function without "
                    "managed or raw MIR ownership");
                continue;
            }
            module << emit_managed_mir_function(
                hir_, *managed, options_, diagnostics_);
        }
    }

    std::string function_alias_type(const hir::Function& function) const {
        std::string result = ir_type(function.result_type) + " (";
        for (std::size_t index = 0; index < function.parameters.size(); ++index) {
            if (index != 0) result += ", ";
            const auto& parameter = function.parameters[index];
            const bool manual_cell =
                parameter.physical_location &&
                *parameter.physical_location != "auto";
            result += parameter.mode == ParameterMode::In && !manual_cell
                ? ir_type(parameter.type)
                : std::string("ptr");
        }
        if (function.variadic) {
            if (!function.parameters.empty()) result += ", ";
            result += "...";
        }
        return result + ')';
    }

    void emit_aliases(std::ostringstream& module) const {
        bool emitted{};
        for (const auto& function : hir_.functions) {
            if (!function.alias_target) continue;
            module << symbol_name(function.link_symbol) << " = "
                   << (function.weak ? "weak " : "")
                   << llvm_visibility(function.visibility) << "alias "
                   << function_alias_type(function) << ", ptr "
                   << symbol_name(*function.alias_target) << '\n';
            emitted = true;
        }
        for (const auto& object : data_.objects) {
            const auto& entity = hir_.object(object.source);
            if (!entity.alias_target) continue;
            module << symbol_name(entity.link_symbol) << " = "
                   << (entity.weak ? "weak " : "")
                   << llvm_visibility(entity.visibility) << "alias "
                   << ir_type(object.type) << ", ptr "
                   << symbol_name(*entity.alias_target) << '\n';
            emitted = true;
        }
        if (emitted) module << '\n';
    }

    std::string floating_initializer(const data::Object& object) const {
        const auto& type = hir_.type(object.type);
        if (type.builtin == BuiltinType::F32 ||
            (type.builtin == BuiltinType::Fptr && object.size == 4)) {
            const auto value = std::bit_cast<float>(
                static_cast<std::uint32_t>(object.bits.low));
            return "0x" + hexadecimal(
                              std::bit_cast<std::uint64_t>(
                                  static_cast<double>(value)),
                              16);
        }
        if (type.builtin == BuiltinType::F64 ||
            (type.builtin == BuiltinType::Fptr && object.size == 8)) {
            return "0x" + hexadecimal(object.bits.low, 16);
        }
        if (type.builtin == BuiltinType::F80) {
            return "0xK" + hexadecimal(object.bits.high & 0xffffU, 4) +
                   hexadecimal(object.bits.low, 16);
        }
        if (type.builtin == BuiltinType::F128) {
            return "0xL" + hexadecimal(object.bits.high, 16) +
                   hexadecimal(object.bits.low, 16);
        }
        return "zeroinitializer";
    }

    std::string address_initializer(const data::Object& object) {
        if (!object.address) return "null";
        const auto& address = *object.address;
        std::string target;
        if (address.kind == data::AddressKind::Object && address.object) {
            target = symbol_name(hir_.object(*address.object).link_symbol);
        } else if (address.kind == data::AddressKind::Function &&
                   address.function) {
            target = symbol_name(hir_.function(*address.function).link_symbol);
        } else if (address.kind == data::AddressKind::Label &&
                   address.function && address.label) {
            if (raw_assembly_.owns(*address.function)) {
                diagnostics_.error(
                    object.location,
                    "LLVM debug serialization cannot represent a raw-assembly "
                    "local-label address");
                return "null";
            }
            const auto& function = hir_.function(*address.function);
            const auto& label = hir_.labels.at(address.label->value);
            target = "blockaddress(" + symbol_name(function.link_symbol) +
                     ", %" + llvm_label_name(label.id) + ')';
        } else {
            diagnostics_.error(object.location,
                               "data IR address constant has no target");
            return "null";
        }
        if (address.addend == 0) return target;
        return "getelementptr (i8, ptr " + target + ", i" +
               std::to_string(data_.address_bits) + ' ' +
               std::to_string(address.addend) + ')';
    }

    std::string initializer(const data::Object& object) {
        switch (object.initializer) {
        case data::InitializerKind::Declaration: return {};
        case data::InitializerKind::Zero: return "zeroinitializer";
        case data::InitializerKind::Uninitialized: return "undef";
        case data::InitializerKind::Integer: {
            const auto& type = hir_.type(object.type);
            if (type.kind == hir::Type::Kind::Pointer ||
                (type.kind == hir::Type::Kind::Builtin && type.builtin == BuiltinType::Label))
                return "inttoptr (i" + std::to_string(object.size * 8) + ' ' +
                       to_decimal(object.bits) + " to " + ir_type(object.type) + ')';
            return to_decimal(object.bits);
        }
        case data::InitializerKind::Floating:
            return floating_initializer(object);
        case data::InitializerKind::Address: {
            const auto address = address_initializer(object);
            const auto& type = hir_.type(object.type);
            if (type.kind == hir::Type::Kind::Builtin &&
                type.builtin != BuiltinType::Label) {
                return "ptrtoint (ptr " + address + " to " +
                       ir_type(object.type) + ')';
            }
            return address;
        }
        case data::InitializerKind::Bytes: {
            std::string result = "[";
            for (std::size_t index = 0; index < object.bytes.size(); ++index) {
                if (index != 0) result += ", ";
                result += "i8 " +
                          std::to_string(
                              static_cast<unsigned>(object.bytes[index]));
            }
            return result + ']';
        }
        case data::InitializerKind::Aggregate: {
            const auto bytes = [&](std::size_t begin, std::size_t end) {
                std::string result = "[";
                for (auto index = begin; index < end; ++index) {
                    if (index != begin) result += ", ";
                    result += "i8 " + std::to_string(
                        static_cast<unsigned>(object.bytes[index]));
                }
                return result + ']';
            };
            if (object.relocations.empty()) {
                return bytes(0, object.bytes.size());
            }
            std::string result = "<{ ";
            std::size_t offset{};
            bool first = true;
            const auto separate = [&] {
                if (!first) result += ", ";
                first = false;
            };
            for (const auto& relocation : object.relocations) {
                if (offset != relocation.offset) {
                    separate();
                    const auto count = relocation.offset - offset;
                    result += "[" + std::to_string(count) + " x i8] " +
                              bytes(offset, relocation.offset);
                }
                separate();
                data::Object scalar;
                scalar.location = object.location;
                scalar.address = relocation.address;
                result += "ptr " + address_initializer(scalar);
                offset = relocation.offset + relocation.size;
            }
            if (offset != object.bytes.size()) {
                separate();
                const auto count = object.bytes.size() - offset;
                result += "[" + std::to_string(count) + " x i8] " +
                          bytes(offset, object.bytes.size());
            }
            return result + " }>";
        }
        }
        return "zeroinitializer";
    }

    std::string aggregate_ir_type(const data::Object& object) const {
        if (object.relocations.empty()) {
            return "[" + std::to_string(object.bytes.size()) + " x i8]";
        }
        std::string result = "<{ ";
        std::size_t offset{};
        bool first = true;
        const auto append = [&](std::string value) {
            if (!first) result += ", ";
            first = false;
            result += std::move(value);
        };
        for (const auto& relocation : object.relocations) {
            if (offset != relocation.offset) {
                append("[" + std::to_string(relocation.offset - offset) +
                       " x i8]");
            }
            append("ptr");
            offset = relocation.offset + relocation.size;
        }
        if (offset != object.bytes.size()) {
            append("[" + std::to_string(object.bytes.size() - offset) +
                   " x i8]");
        }
        return result + " }>";
    }

    void emit_objects(std::ostringstream& module) {
        bool emitted = false;
        for (const auto& object : data_.objects) {
            const auto& entity = hir_.object(object.source);
            if (entity.alias_target) continue;
            const auto symbol = symbol_name(entity.link_symbol);
            const auto storage = object.read_only ? "constant " : "global ";
            const auto tls = [&]() -> std::string {
                if (!object.is_thread_local) return {};
                const auto model = object.tls_model.empty()
                    ? (entity.definition && entity.linkage != Linkage::Global
                           ? std::string("local-exec")
                           : std::string("initial-exec"))
                    : object.tls_model;
                return "thread_local(" +
                       std::string(model == "local-exec" ? "localexec"
                                                         : "initialexec") +
                       ") ";
            }();
            if (raw_assembly_.owns(object.source)) {
                module << symbol << " = external "
                       << llvm_visibility(entity.visibility) << tls << storage
                       << ir_type(object.type) << ", align "
                       << object.alignment << '\n';
                emitted = true;
                continue;
            }
            if (object.initializer == data::InitializerKind::Declaration) {
                module << symbol << " = "
                       << (entity.weakref_target ? "extern_weak "
                                                 : "external ")
                       << llvm_visibility(entity.visibility) << tls << storage
                       << ir_type(object.type) << ", align "
                       << object.alignment << '\n';
                emitted = true;
                continue;
            }
            const auto storage_type =
                object.initializer == data::InitializerKind::Aggregate
                    ? aggregate_ir_type(object)
                    : ir_type(object.type);
            module << symbol << " = "
                   << (entity.weak
                           ? "weak "
                           : entity.linkage == Linkage::Global ? ""
                                                               : "internal ")
                   << llvm_visibility(entity.visibility)
                   << tls << storage << storage_type << ' '
                   << initializer(object);
            if (entity.section) {
                module << ", section " << llvm_string(*entity.section);
            } else if (object.initializer ==
                       data::InitializerKind::Uninitialized) {
                module << ", section \".noinit\"";
            }
            module << ", align " << object.alignment << '\n';
            emitted = true;
        }
        if (emitted) module << '\n';
    }

    void emit_retention(std::ostringstream& module) const {
        std::vector<std::string> compiler_symbols;
        std::vector<std::string> linker_symbols;
        std::unordered_set<std::string> compiler_seen;
        std::unordered_set<std::string> linker_seen;
        const auto append_unique = [](std::vector<std::string>& symbols,
                                      std::unordered_set<std::string>& seen,
                                      std::string symbol) {
            if (seen.insert(symbol).second) {
                symbols.push_back(std::move(symbol));
            }
        };
        for (const auto& object : data_.objects) {
            const auto& entity = hir_.object(object.source);
            if ((object.initializer == data::InitializerKind::Declaration &&
                 !entity.alias_target) ||
                raw_assembly_.owns(object.source)) {
                continue;
            }
            const auto symbol =
                symbol_name(entity.link_symbol);
            if (object.retain) {
                append_unique(linker_symbols, linker_seen, symbol);
            } else if (object.used) {
                append_unique(compiler_symbols, compiler_seen, symbol);
            }
        }
        for (const auto& function : hir_.functions) {
            if (!function.definition && !function.alias_target) continue;
            const auto symbol = symbol_name(function.link_symbol);
            if (function.retain) {
                append_unique(linker_symbols, linker_seen, symbol);
            } else if (function.used) {
                append_unique(compiler_symbols, compiler_seen, symbol);
            }
        }
        const auto emit = [&](std::string_view name,
                              const std::vector<std::string>& symbols) {
            if (symbols.empty()) return;
            module << '@' << name << " = appending global [" << symbols.size()
                   << " x ptr] [";
            for (std::size_t index = 0; index < symbols.size(); ++index) {
                if (index != 0) module << ", ";
                module << "ptr " << symbols[index];
            }
            module << "], section \"llvm.metadata\"\n";
        };
        emit("llvm.compiler.used", compiler_symbols);
        emit("llvm.used", linker_symbols);
        if (!compiler_symbols.empty() || !linker_symbols.empty()) {
            module << '\n';
        }
    }

    void emit_intrinsic_declarations(std::ostringstream& module) const {
        std::vector<std::string> expect_types;
        std::vector<std::string> rotate_left_types;
        std::vector<std::string> rotate_right_types;
        bool trap = false;
        bool va_start = false;
        bool dynamic_stack = false;
        for (const auto& function : managed_.functions) {
            for (const auto& value : function.values) {
                if (value.kind == mir::ValueKind::VariadicState) {
                    va_start = true;
                }
                if (value.kind == mir::ValueKind::DynamicStackSave ||
                    value.kind == mir::ValueKind::DynamicAlloca ||
                    value.kind == mir::ValueKind::DynamicStackRestore) {
                    dynamic_stack = true;
                }
                if (value.kind == mir::ValueKind::Binary &&
                    (value.binary == mir::BinaryOperation::RotateLeft ||
                     value.binary == mir::BinaryOperation::RotateRight)) {
                    auto& types =
                        value.binary == mir::BinaryOperation::RotateLeft
                            ? rotate_left_types : rotate_right_types;
                    const auto type = ir_type(value.type);
                    if (std::find(types.begin(), types.end(), type) ==
                        types.end()) {
                        types.push_back(type);
                    }
                }
                if (value.kind != mir::ValueKind::Intrinsic ||
                    value.intrinsic != mir::IntrinsicOperation::Expect) {
                    continue;
                }
                const auto type = ir_type(value.type);
                if (std::find(expect_types.begin(), expect_types.end(), type) ==
                    expect_types.end()) {
                    expect_types.push_back(type);
                }
            }
            trap = trap || std::any_of(
                               function.blocks.begin(), function.blocks.end(),
                               [](const mir::ManagedBlock& block) {
                                   return block.terminator.kind ==
                                          mir::TerminatorKind::Trap;
                               });
        }
        std::sort(expect_types.begin(), expect_types.end());
        std::sort(rotate_left_types.begin(), rotate_left_types.end());
        std::sort(rotate_right_types.begin(), rotate_right_types.end());
        for (const auto& type : expect_types) {
            module << "declare " << type << " @llvm.expect." << type << '('
                   << type << ", " << type << ")\n";
        }
        for (const auto& type : rotate_left_types) {
            module << "declare " << type << " @llvm.fshl." << type << '('
                   << type << ", " << type << ", " << type << ")\n";
        }
        for (const auto& type : rotate_right_types) {
            module << "declare " << type << " @llvm.fshr." << type << '('
                   << type << ", " << type << ", " << type << ")\n";
        }
        if (trap) module << "declare void @llvm.trap()\n";
        if (va_start) module << "declare void @llvm.va_start(ptr)\n";
        if (dynamic_stack) {
            module << "declare ptr @llvm.stacksave.p0()\n"
                      "declare void @llvm.stackrestore.p0(ptr)\n";
        }
        if (!expect_types.empty() || !rotate_left_types.empty() ||
            !rotate_right_types.empty() || trap || va_start ||
            dynamic_stack) {
            module << '\n';
        }
    }

    const CompilerOptions& options_;
    Diagnostics& diagnostics_;
    const hir::Module& hir_;
    const data::Module& data_;
    const mir::ManagedModule& managed_;
    const mir::AssemblyBundle& raw_assembly_;
};

} // namespace

LlvmTextSerializer::LlvmTextSerializer(const CompilerOptions& options,
                                       Diagnostics& diagnostics)
    : options_(options), diagnostics_(diagnostics) {}

std::string LlvmTextSerializer::serialize(const codegen::ModuleView& module) {
    if (const auto location = codegen::requested_alignment_location(module)) {
        diagnostics_.error(*location, "LLVM debug serialization does not encode typedef alignment");
        return {};
    }
    return ModuleEmitter(options_, diagnostics_, module).run();
}

} // namespace cross::debug
