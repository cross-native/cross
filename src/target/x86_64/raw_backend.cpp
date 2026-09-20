// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/x86_64/raw_backend.hpp"

#include "common/floating_semantics.hpp"
#include "middle/patch_sink.hpp"
#include "target/assembly_format.hpp"

#include "target/x86_64/features.hpp"
#include "target/x86_64/manual_endpoint.hpp"

#include <algorithm>
#include <bit>
#include <charconv>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cross::x86_64 {
namespace {

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

std::optional<std::uint64_t> integer_literal(const Expr& expression) {
    if (expression.kind == Expr::Kind::Parenthesized && expression.left) {
        return integer_literal(*expression.left);
    }
    if (expression.kind == Expr::Kind::Unary && expression.left &&
        (expression.text == "+" || expression.text == "-")) {
        const auto value = integer_literal(*expression.left);
        if (!value) return std::nullopt;
        return expression.text == "-" ? std::uint64_t{0} - *value : *value;
    }
    if (expression.kind != Expr::Kind::Integer) return std::nullopt;
    auto text = expression.text;
    static constexpr std::string_view suffixes[]{
        "iptr", "uptr", "i64", "u64", "i32", "u32", "i16", "u16", "i8", "u8",
    };
    for (const auto suffix : suffixes) {
        if (text.size() > suffix.size() && text.ends_with(suffix)) {
            text.resize(text.size() - suffix.size());
            break;
        }
    }
    text.erase(std::remove(text.begin(), text.end(), '_'), text.end());
    unsigned base = 10;
    std::string_view digits(text);
    if (digits.starts_with("0x") || digits.starts_with("0X")) {
        base = 16;
        digits.remove_prefix(2);
    } else if (digits.starts_with("0b") || digits.starts_with("0B")) {
        base = 2;
        digits.remove_prefix(2);
    } else if (digits.size() > 1 && digits.front() == '0') {
        base = 8;
        digits.remove_prefix(1);
    }
    std::uint64_t value{};
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value,
                                        static_cast<int>(base));
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size()) {
        return std::nullopt;
    }
    return value;
}

bool fits_immediate(std::uint64_t value, unsigned bits, bool is_signed) {
    if (bits == 0 || bits >= 64) return true;
    if (!is_signed) return value < (std::uint64_t{1} << bits);
    const auto positive_max = (std::uint64_t{1} << (bits - 1)) - 1;
    const auto negative_min = std::uint64_t{0} -
                              (std::uint64_t{1} << (bits - 1));
    return value <= positive_max || value >= negative_min;
}

bool fits_instruction_immediate(
    std::uint64_t value, const InstructionOperandEntry& specification) {
    if (fits_immediate(value, specification.immediate_bits,
                       specification.immediate_signed)) {
        return true;
    }
    // An immediate that occupies the complete integer destination width has
    // modulo-width semantics.  Accept both its signed spelling and its
    // unsigned bit-pattern spelling.  Sign-extended short immediates (for
    // example addq imm32) intentionally remain signed-only.
    return specification.immediate_signed && specification.allow_register &&
           specification.register_bits == specification.immediate_bits &&
           fits_immediate(value, specification.immediate_bits, false);
}

bool signed_integer_type(const hir::Type& type) {
    if (type.kind != hir::Type::Kind::Builtin) return false;
    return type.builtin == BuiltinType::I8 ||
           type.builtin == BuiltinType::I16 ||
           type.builtin == BuiltinType::I32 ||
           type.builtin == BuiltinType::I64 ||
           type.builtin == BuiltinType::I128 ||
           type.builtin == BuiltinType::Iptr;
}

bool signed_integer_type(const TypePtr& type) {
    if (!type || type->kind != Type::Kind::Builtin) return false;
    return type->builtin == BuiltinType::I8 ||
           type->builtin == BuiltinType::I16 ||
           type->builtin == BuiltinType::I32 ||
           type->builtin == BuiltinType::I64 ||
           type->builtin == BuiltinType::I128 ||
           type->builtin == BuiltinType::Iptr;
}

std::optional<unsigned> x87_position(std::string_view storage) {
    if (storage.size() != 3 || !storage.starts_with("st") ||
        storage[2] < '0' || storage[2] > '7') {
        return std::nullopt;
    }
    return static_cast<unsigned>(storage[2] - '0');
}

class RawLowerer {
public:
    RawLowerer(const hir::Module& hir_module, const Subtarget& subtarget,
               Diagnostics& diagnostics)
        : hir_(hir_module), subtarget_(subtarget), target_(subtarget.target()),
          diagnostics_(diagnostics) {}

    mir::RawModule run() {
        for (const auto& function : hir_.functions) {
            if (function.ownership == hir::BodyOwnership::RawMir && function.definition) {
                lower_function(function);
            }
        }
        return std::move(module_);
    }

private:
    bool feature_enabled(std::string_view feature) const {
        return feature.empty() || feature == "base" ||
               feature == target_.architecture ||
               subtarget_.has_feature(feature);
    }

    std::optional<std::string_view> missing_feature(
        const InstructionEntry& instruction) const {
        if (!feature_enabled(instruction.feature)) {
            return instruction.feature;
        }
        const auto missing = std::find_if(
            instruction.required_features.begin(),
            instruction.required_features.end(),
            [&](std::string_view feature) {
                return !feature_enabled(feature);
            });
        return missing == instruction.required_features.end()
                   ? std::nullopt
                   : std::optional<std::string_view>(*missing);
    }

    struct RawMemoryAddress {
        const RegisterEntry* base{};
        const RegisterEntry* index{};
        unsigned scale{1};
        TypePtr pointee;
        std::int64_t displacement{};
    };

    std::optional<RawMemoryAddress> memory_address(
        const Expr& expression, bool diagnose) const {
        const auto fail = [&](SourceLocation location,
                              std::string message)
            -> std::optional<RawMemoryAddress> {
            if (diagnose) diagnostics_.error(location, std::move(message));
            return std::nullopt;
        };
        const auto& source = unparenthesized(expression);
        const Expr* pointer = nullptr;
        std::int64_t element_index{};
        const RegisterEntry* index_register = nullptr;
        unsigned index_scale = 1;
        if (source.kind == Expr::Kind::Unary && source.text == "*" &&
            source.left) {
            pointer = &unparenthesized(*source.left);
        } else if (source.kind == Expr::Kind::Binary &&
                   source.text == "index" && source.left && source.right) {
            pointer = &unparenthesized(*source.left);
            const auto index = integer_literal(*source.right);
            if (!index) {
                const auto& index_expression = unparenthesized(*source.right);
                if (index_expression.kind != Expr::Kind::Name) {
                    return fail(source.right->location,
                                "raw memory index must be an integer constant or one hard-bound 64-bit register");
                }
                const auto binding = bindings_.find(index_expression.text);
                const auto type = binding_types_.find(index_expression.text);
                if (binding == bindings_.end() || type == binding_types_.end() ||
                    !type->second || !is_integer(type->second) ||
                    binding->second->register_class != "integer" ||
                    binding->second->bits != 64 ||
                    !binding->second->address_capable) {
                    return fail(index_expression.location,
                                "raw memory index requires a hard-bound 64-bit integer register");
                }
                if (binding->second->storage == "rsp" ||
                    binding->second->storage == "r12") {
                    return fail(index_expression.location,
                                "x86-64 rsp/r12 storage cannot encode an address index");
                }
                index_register = binding->second;
            } else {
                element_index = static_cast<std::int64_t>(*index);
            }
        } else {
            return fail(source.location,
                        "instruction memory operand requires '*pointer' or 'pointer[index]'");
        }
        if (!pointer || pointer->kind != Expr::Kind::Name) {
            return fail(pointer ? pointer->location : source.location,
                        "raw memory base must be one hard-bound pointer register");
        }
        const auto binding = bindings_.find(pointer->text);
        const auto type = binding_types_.find(pointer->text);
        if (binding == bindings_.end() || type == binding_types_.end() ||
            !type->second || type->second->kind != Type::Kind::Pointer ||
            !type->second->pointee) {
            return fail(pointer->location,
                        "raw memory base is not a typed hard-bound pointer");
        }
        if (!binding->second->address_capable ||
            binding->second->register_class != "integer") {
            return fail(pointer->location,
                        "raw memory base requires an address-capable integer register");
        }
        auto bytes = type_bits(type->second->pointee) / 8;
        if (type->second->pointee->kind == Type::Kind::Builtin &&
            type->second->pointee->builtin == BuiltinType::F80) {
            bytes = target_.data_layout.f80_storage_bytes;
        }
        if (bytes == 0) {
            return fail(source.location,
                        "raw memory operand requires a complete byte-sized pointee type");
        }
        if (index_register) {
            if (bytes != 1 && bytes != 2 && bytes != 4 && bytes != 8) {
                return fail(source.right->location,
                            "x86-64 scaled register index requires a 1-, 2-, 4-, or 8-byte pointee");
            }
            index_scale = static_cast<unsigned>(bytes);
        }
        if (element_index > std::numeric_limits<std::int32_t>::max() /
                                static_cast<std::int64_t>(bytes) ||
            element_index < std::numeric_limits<std::int32_t>::min() /
                                static_cast<std::int64_t>(bytes)) {
            return fail(source.location,
                        "raw memory displacement does not fit x86-64 disp32 addressing");
        }
        return RawMemoryAddress{
            binding->second, index_register, index_scale, type->second->pointee,
            element_index * static_cast<std::int64_t>(bytes)};
    }

    bool operand_matches(const Expr& expression,
                         const InstructionOperandEntry& specification) const {
        const Expr* source = &expression;
        if (expression.kind == Expr::Kind::Call && expression.left &&
            expression.left->kind == Expr::Kind::Name &&
            expression.left->text == "$::patch") {
            if (!specification.patchable || expression.arguments.empty() ||
                expression.arguments.size() > 2) return false;
            source = expression.arguments.front().get();
        }
        if (specification.allow_memory) {
            const auto address = memory_address(*source, false);
            if (address) {
                const auto bits = type_bits(address->pointee);
                return (specification.memory_bits == 0 ||
                        bits == specification.memory_bits) &&
                       (!address->pointee->is_atomic ||
                        specification.allow_atomic_memory) &&
                       !((specification.role == InstructionOperandRole::Output ||
                          specification.role == InstructionOperandRole::InOut) &&
                         address->pointee->is_const);
            }
        }
        if (specification.allow_label) {
            return current_function_ && source->kind == Expr::Kind::Name &&
                   hir_.label(current_function_->id, source->text);
        }
        if (source->kind == Expr::Kind::Name && specification.allow_register) {
            const auto found = bindings_.find(source->text);
            if (found != bindings_.end()) {
                const auto* entry = found->second;
                return entry->bits == specification.register_bits &&
                       (specification.register_class.empty() ||
                        entry->register_class == specification.register_class) &&
                       (specification.register_storage.empty() ||
                        entry->storage == specification.register_storage) &&
                       !((specification.role == InstructionOperandRole::Output ||
                          specification.role == InstructionOperandRole::InOut) &&
                         entry->storage == "rsp");
            }
        }
        if (!specification.allow_immediate) return false;
        const auto value = integer_literal(*source);
        return value && fits_instruction_immediate(*value, specification);
    }

    const InstructionEntry* select_instruction(
        std::string_view name,
        const std::vector<std::unique_ptr<Expr>>& arguments,
        SourceLocation location) {
        const auto forms = find_instruction_forms(target_, name);
        if (forms.empty()) return nullptr;

        std::vector<const InstructionEntry*> arity_matches;
        std::vector<const InstructionEntry*> available_matches;
        std::optional<std::string_view> first_missing_feature;
        for (const auto* form : forms) {
            if (form->operands.size() != arguments.size()) continue;
            arity_matches.push_back(form);
            bool matches = true;
            for (std::size_t index = 0; index < arguments.size(); ++index) {
                if (!operand_matches(*arguments[index], form->operands[index])) {
                    matches = false;
                    break;
                }
            }
            if (!matches) continue;
            if (const auto missing = missing_feature(*form)) {
                if (!first_missing_feature) first_missing_feature = missing;
            } else {
                available_matches.push_back(form);
            }
        }
        if (available_matches.size() == 1) return available_matches.front();
        if (available_matches.size() > 1) {
            diagnostics_.error(location,
                               "target instruction '" + std::string(name) +
                                   "' has ambiguous typed forms for these operands");
            return nullptr;
        }
        if (first_missing_feature) {
            diagnostics_.error(location,
                               "target instruction '" + std::string(name) +
                                   "' requires feature '" +
                                   std::string(*first_missing_feature) + "'");
            return nullptr;
        }
        if (arity_matches.empty()) {
            const auto required = forms.front()->operands.size();
            const bool uniform = std::all_of(
                forms.begin(), forms.end(), [&](const auto* form) {
                    return form->operands.size() == required;
                });
            if (uniform) {
                diagnostics_.error(location,
                                   "target instruction '" + std::string(name) +
                                       "' requires " + std::to_string(required) +
                                       " operands");
            } else {
                diagnostics_.error(location,
                                   "no arity of target instruction '" +
                                       std::string(name) + "' accepts " +
                                       std::to_string(arguments.size()) + " operands");
            }
            return nullptr;
        }
        for (std::size_t index = 0; index < arguments.size(); ++index) {
            const auto address = memory_address(*arguments[index], false);
            if (!address) continue;
            bool has_memory_form = false;
            bool has_atomic_form = false;
            bool has_read_form = false;
            for (const auto* form : arity_matches) {
                const auto& operand = form->operands[index];
                if (!operand.allow_memory ||
                    (operand.memory_bits != 0 &&
                     operand.memory_bits != type_bits(address->pointee))) continue;
                has_memory_form = true;
                has_atomic_form = has_atomic_form || operand.allow_atomic_memory;
                has_read_form = has_read_form ||
                                operand.role == InstructionOperandRole::Input;
            }
            if (has_memory_form && address->pointee->is_atomic &&
                !has_atomic_form) {
                diagnostics_.error(
                    arguments[index]->location,
                    "ordinary instruction form cannot access an atomic-qualified lvalue");
                return nullptr;
            }
            if (has_memory_form && address->pointee->is_const &&
                !has_read_form) {
                diagnostics_.error(arguments[index]->location,
                                   "instruction cannot write a const-qualified lvalue");
                return nullptr;
            }
        }
        if (arity_matches.size() == 1) {
            // Preserve the focused operand diagnostic used before typed
            // overloads were introduced (wrong register class, bad label,
            // immediate range, and so on).
            const auto* form = arity_matches.front();
            for (std::size_t index = 0; index < arguments.size(); ++index) {
                if (!operand_matches(*arguments[index], form->operands[index])) {
                    (void)lower_operand(*arguments[index], form->operands[index]);
                    return nullptr;
                }
            }
        }
        diagnostics_.error(location,
                           "no typed form of target instruction '" +
                               std::string(name) + "' matches these operands");
        return nullptr;
    }

    mir::BlockId new_block(SourceLocation location, std::string source_label = {}) {
        const mir::BlockId id{static_cast<std::uint32_t>(current_.blocks.size())};
        mir::RawBlock candidate;
        candidate.id = id;
        candidate.location = location;
        candidate.source_label = std::move(source_label);
        current_.blocks.push_back(std::move(candidate));
        return id;
    }

    mir::RawBlock& block(mir::BlockId id) { return current_.blocks.at(id.value); }
    const mir::RawBlock& block(mir::BlockId id) const { return current_.blocks.at(id.value); }

    void append_layout(mir::BlockId id) {
        if (laid_out_.insert(id.value).second) current_.layout.push_back(id);
    }

    const RegisterEntry* direct_register(std::string_view location,
                                         SourceLocation source_location,
                                         std::string_view entity_name) {
        const auto parsed = parse_manual_endpoint(location);
        if (!parsed || parsed.endpoint.kind != ManualEndpointKind::direct_register ||
            !parsed.endpoint.register_view) {
            diagnostics_.error(source_location,
                               "naked entity '" + std::string(entity_name) +
                                   "' requires one direct physical register endpoint");
            return nullptr;
        }
        const auto* entry = find_register(target_, parsed.endpoint.register_view->name);
        if (!entry || (entry->register_class != "integer" &&
                       entry->register_class != "simd" &&
                       entry->register_class != "mask" &&
                       entry->register_class != "x87")) {
            diagnostics_.error(source_location,
                               "raw x86-64 MIR requires a direct GPR, SIMD, opmask, or x87 register for '" +
                                   std::string(entity_name) + "'");
            return nullptr;
        }
        return entry;
    }

    bool raw_binding_type(const RegisterEntry& entry,
                          const TypePtr& type) const {
        if (!type || type->is_atomic || type->is_const ||
            type->is_volatile) {
            return false;
        }
        if (type->kind == Type::Kind::Vector) {
            return entry.register_class == "simd" && !type->scalable &&
                   type_bits(type) == entry.bits;
        }
        if (type->kind == Type::Kind::Builtin && is_floating(type)) {
            return (entry.register_class == "simd" &&
                    (type->builtin == BuiltinType::F32 ||
                     type->builtin == BuiltinType::F64)) ||
                   (entry.register_class == "x87" &&
                    type->builtin == BuiltinType::F80);
        }
        if (entry.register_class == "mask") {
            return type->kind == Type::Kind::Builtin &&
                   is_integer(type) && type_bits(type) == entry.bits;
        }
        return (type->kind == Type::Kind::Builtin ||
                type->kind == Type::Kind::Pointer) &&
               entry.register_class == "integer" &&
               type_bits(type) == entry.bits;
    }

    const hir::Function* raw_inline_function(std::string_view name) const {
        const hir::Function* result = nullptr;
        for (const auto& candidate : hir_.functions) {
            if (candidate.source_name != name || !candidate.definition ||
                !candidate.definition->attribute("raw_inline")) {
                continue;
            }
            if (result && result->definition != candidate.definition) {
                return nullptr;
            }
            result = &candidate;
        }
        return result;
    }

    const RegisterEntry* storage_view(std::string_view storage,
                                      const TypePtr& type) const {
        const auto found = std::find_if(
            target_.registers.begin(), target_.registers.end(),
            [&](const RegisterEntry& candidate) {
                return candidate.storage == storage &&
                       feature_enabled(candidate.feature) &&
                       raw_binding_type(candidate, type);
            });
        return found == target_.registers.end() ? nullptr : &*found;
    }

    const RegisterEntry* integer_storage_view(std::string_view storage,
                                              unsigned bits) const {
        const auto found = std::find_if(
            target_.registers.begin(), target_.registers.end(),
            [&](const RegisterEntry& candidate) {
                return candidate.storage == storage &&
                       candidate.register_class == "integer" &&
                       candidate.bits == bits &&
                       feature_enabled(candidate.feature);
            });
        return found == target_.registers.end() ? nullptr : &*found;
    }

    void initialize_raw_inline_resources(const hir::Function& function) {
        raw_inline_scratch_.clear();
        scratch_in_use_.clear();
        protected_storages_.clear();
        for (const auto& [name, binding] : bindings_) {
            (void)name;
            protected_storages_.insert(std::string(binding->storage));
        }
        if (function.result_location) {
            const auto endpoint = parse_manual_endpoint(*function.result_location);
            if (endpoint && endpoint.endpoint.register_view) {
                protected_storages_.insert(
                    std::string(endpoint.endpoint.register_view->storage_name));
            }
        }
        for (const auto& clobber : function.clobbers) {
            const auto* entry = find_register(target_, clobber);
            if (!entry || entry->compiler_owned) continue;
            const auto storage = std::string(entry->storage);
            if (std::find(raw_inline_scratch_.begin(), raw_inline_scratch_.end(),
                          storage) == raw_inline_scratch_.end()) {
                raw_inline_scratch_.push_back(storage);
            }
        }
    }

    const RegisterEntry* acquire_scratch(const TypePtr& type,
                                         SourceLocation location,
                                         std::string_view purpose) {
        if (!type || !is_scalar(type) || type->is_atomic || type->is_volatile ||
            type->is_const) {
            diagnostics_.error(
                location,
                "raw-compatible " + std::string(purpose) +
                    " requires an unqualified scalar value");
            return nullptr;
        }
        for (const auto& storage : raw_inline_scratch_) {
            if (scratch_in_use_.contains(storage) ||
                protected_storages_.contains(storage)) {
                continue;
            }
            if (const auto* view = storage_view(storage, type)) {
                scratch_in_use_.insert(storage);
                return view;
            }
        }
        diagnostics_.error(
            location,
            "raw-compatible " + std::string(purpose) + " of type '" +
                type_name(type) +
                "' requires another matching register in the naked caller's "
                "clobber contract");
        return nullptr;
    }

    void release_scratch(const RegisterEntry* entry) {
        if (entry) scratch_in_use_.erase(std::string(entry->storage));
    }

    void bind_parameters(const hir::Function& function) {
        for (const auto& parameter : function.parameters) {
            if (!parameter.physical_location || *parameter.physical_location == "auto") continue;
            if (const auto* entry = direct_register(*parameter.physical_location,
                                                    parameter.location, parameter.name)) {
                if (const auto position = x87_position(entry->storage)) {
                    if (parameter.mode != ParameterMode::Out) {
                        entry_ordered_depth_ =
                            std::max(entry_ordered_depth_, *position + 1);
                    }
                    if (parameter.mode != ParameterMode::In) {
                        return_ordered_depth_ =
                            std::max(return_ordered_depth_, *position + 1);
                    }
                }
                bindings_[parameter.name] = entry;
                if (function.definition) {
                    const auto declaration = std::find_if(
                        function.definition->parameters.begin(),
                        function.definition->parameters.end(),
                        [&](const auto& candidate) {
                            return candidate.name == parameter.name;
                        });
                    if (declaration != function.definition->parameters.end()) {
                        binding_types_[parameter.name] = declaration->type;
                    }
                }
                const auto& type = hir_.type(parameter.type);
                binding_signed_[parameter.name] = signed_integer_type(type);
                if (type.kind == hir::Type::Kind::Builtin &&
                    type.builtin == BuiltinType::Label) {
                    label_bindings_.insert(parameter.name);
                }
            }
        }
    }

    void lower_function(const hir::Function& function) {
        current_function_ = &function;
        bindings_.clear();
        binding_types_.clear();
        binding_signed_.clear();
        label_bindings_.clear();
        inline_frames_.clear();
        inline_call_stack_.clear();
        label_blocks_.clear();
        laid_out_.clear();
        loops_.clear();
        break_targets_.clear();
        entry_ordered_depth_ = 0;
        return_ordered_depth_ = 0;
        current_ = {};
        current_.source = function.id;
        current_.location = function.location;
        current_.symbol = function.link_symbol;
        current_.linkage = function.linkage;
        current_.section = function.section;
        current_.minimum_alignment = function.minimum_alignment;
        current_.entry = new_block(function.location, "entry");
        current_block_ = current_.entry;
        append_layout(current_.entry);
        for (const auto label_id : function.labels) {
            const auto& label = hir_.labels.at(label_id.value);
            label_blocks_.emplace(label_id.value,
                                  new_block(label.location, label.source_name));
        }
        bind_parameters(function);
        initialize_raw_inline_resources(function);
        if (function.result_location) {
            const auto endpoint = parse_manual_endpoint(*function.result_location);
            if (endpoint && endpoint.endpoint.register_view) {
                if (const auto position =
                        x87_position(endpoint.endpoint.register_view->storage_name)) {
                    return_ordered_depth_ =
                        std::max(return_ordered_depth_, *position + 1);
                }
            }
        }
        if (function.definition && function.definition->body) {
            lower_statement(*function.definition->body);
        }
        verify_cfg(function);
        module_.definitions.insert(function.id.value);
        module_.functions.push_back(std::move(current_));
        raw_inline_scratch_.clear();
        scratch_in_use_.clear();
        protected_storages_.clear();
        current_function_ = nullptr;
    }

    bool require_current(SourceLocation location) {
        if (current_block_) return true;
        diagnostics_.error(location, "instruction after raw terminator is unreachable");
        return false;
    }

    const hir::Label* resolve_label(const Expr& expression) {
        if (!current_function_) return nullptr;
        if (expression.kind == Expr::Kind::Parenthesized) {
            diagnostics_.error(expression.location,
                               "raw label instruction requires a literal "
                               "same-function label; use goto with a hard-bound "
                               "label value or $::_jmp_indirect for a register "
                               "target");
            return nullptr;
        }
        if (expression.kind != Expr::Kind::Name) {
            diagnostics_.error(expression.location,
                               "raw branch target must be a same-function label address");
            return nullptr;
        }
        if (const auto* label = hir_.label(current_function_->id, expression.text)) return label;
        diagnostics_.error(expression.location,
                           "unknown same-function raw label '" + expression.text + "'");
        return nullptr;
    }

    mir::Operand label_operand(const hir::Label& label, SourceLocation location) {
        mir::Operand operand;
        operand.kind = mir::Operand::Kind::Label;
        operand.location = location;
        operand.label = {label_blocks_.at(label.id.value), label.source_name};
        return operand;
    }

    static mir::Operand block_operand(mir::BlockId target,
                                      SourceLocation location) {
        mir::Operand operand;
        operand.kind = mir::Operand::Kind::Label;
        operand.location = location;
        operand.label = {target, {}};
        return operand;
    }

    void add_successor(mir::BlockId from, mir::BlockId to) {
        auto& successors = block(from).successors;
        if (std::find(successors.begin(), successors.end(), to) == successors.end()) {
            successors.push_back(to);
        }
    }

    void append_instruction(mir::Instruction instruction) {
        if (!current_block_) return;
        auto id = *current_block_;
        auto& current = block(id);
        current.instructions.push_back(std::move(instruction));
        const auto* form = current.instructions.back().form;
        switch (form->control) {
        case InstructionControlEffect::None:
            return;
        case InstructionControlEffect::RawReturn:
            current.exit = mir::BlockExitKind::Instruction;
            current_block_.reset();
            return;
        case InstructionControlEffect::Trap:
            current.exit = mir::BlockExitKind::Instruction;
            current_block_.reset();
            return;
        case InstructionControlEffect::UnconditionalBranch: {
            current.exit = mir::BlockExitKind::Instruction;
            const auto& target =
                current.instructions.back().operands.front();
            if (target.kind == mir::Operand::Kind::Label) {
                add_successor(id, target.label.target);
            } else {
                if (label_blocks_.empty()) {
                    diagnostics_.error(
                        current.instructions.back().location,
                        "computed raw goto requires at least one same-function label");
                }
                for (const auto& [label, destination] : label_blocks_) {
                    (void)label;
                    add_successor(id, destination);
                }
            }
            current_block_.reset();
            return;
        }
        case InstructionControlEffect::ConditionalBranch: {
            current.exit = mir::BlockExitKind::Instruction;
            const auto& target = current.instructions.back().operands.front().label;
            add_successor(id, target.target);
            const auto fallthrough = new_block(current.instructions.back().location);
            add_successor(id, fallthrough);
            append_layout(fallthrough);
            current_block_ = fallthrough;
            return;
        }
        }
    }

    void enter_block(mir::BlockId id) {
        append_layout(id);
        current_block_ = id;
    }

    bool terminate_jump(mir::BlockId target, SourceLocation location) {
        if (!current_block_) return false;
        const auto* form = find_instruction(target_, "$::_jmp");
        if (!form) {
            diagnostics_.error(location,
                               "selected target has no raw unconditional branch form");
            return false;
        }
        mir::Instruction instruction{location, form, {}};
        instruction.operands.push_back(block_operand(target, location));
        append_instruction(std::move(instruction));
        return true;
    }

    bool terminate_condition(std::string_view instruction_name,
                             mir::BlockId true_target,
                             mir::BlockId false_target,
                             SourceLocation location) {
        if (!current_block_) return false;
        if (true_target == false_target) {
            return terminate_jump(true_target, location);
        }
        const auto* form = find_instruction(target_, instruction_name);
        if (!form ||
            form->control != InstructionControlEffect::ConditionalBranch) {
            diagnostics_.error(location,
                               "selected target has no raw condition branch form '" +
                                   std::string(instruction_name) + "'");
            return false;
        }
        const auto id = *current_block_;
        mir::Instruction instruction{location, form, {}};
        instruction.operands.push_back(block_operand(true_target, location));
        auto& current = block(id);
        current.instructions.push_back(std::move(instruction));
        current.exit = mir::BlockExitKind::Instruction;
        add_successor(id, true_target);
        add_successor(id, false_target);
        current_block_.reset();
        return true;
    }

    void activate_label(const Statement& statement) {
        if (!current_function_) return;
        const auto* label = hir_.label(current_function_->id, statement.label_name);
        if (!label) return;
        const auto target = label_blocks_.at(label->id.value);
        if (current_block_) {
            auto& current = block(*current_block_);
            if (current.exit == mir::BlockExitKind::None) {
                current.exit = mir::BlockExitKind::Fallthrough;
                add_successor(*current_block_, target);
            }
        }
        append_layout(target);
        current_block_ = target;
    }

    void lower_statement(const Statement& statement) {
        if (statement.kind == Statement::Kind::Label) {
            if (!inline_frames_.empty()) {
                diagnostics_.error(statement.location,
                                   "labels are not raw-compatible managed control flow");
            } else {
                activate_label(statement);
            }
            return;
        }
        switch (statement.kind) {
        case Statement::Kind::Compound:
            if (inline_frames_.empty()) {
                for (const auto& child : statement.statements) {
                    lower_statement(*child);
                }
                return;
            }
            {
                auto saved_bindings = bindings_;
                auto saved_types = binding_types_;
                auto saved_signed = binding_signed_;
                auto saved_labels = label_bindings_;
                auto saved_readonly = inline_frames_.back().readonly_names;
                const auto owned_size = inline_frames_.back().owned.size();
                for (const auto& child : statement.statements) {
                    if (!current_block_) break;
                    lower_statement(*child);
                }
                while (inline_frames_.back().owned.size() > owned_size) {
                    release_scratch(inline_frames_.back().owned.back());
                    inline_frames_.back().owned.pop_back();
                }
                bindings_ = std::move(saved_bindings);
                binding_types_ = std::move(saved_types);
                binding_signed_ = std::move(saved_signed);
                label_bindings_ = std::move(saved_labels);
                inline_frames_.back().readonly_names =
                    std::move(saved_readonly);
            }
            return;
        case Statement::Kind::Declaration:
            if (require_current(statement.location)) lower_declaration(*statement.declaration);
            return;
        case Statement::Kind::Expression:
            if (statement.expression && require_current(statement.location)) {
                lower_expression_statement(*statement.expression);
            }
            return;
        case Statement::Kind::Goto:
            if (!inline_frames_.empty()) {
                diagnostics_.error(statement.location,
                                   "goto is not raw-compatible managed control flow");
            } else if (require_current(statement.location)) {
                lower_goto(statement);
            }
            return;
        case Statement::Kind::Empty:
            return;
        case Statement::Kind::Return:
            if (!inline_frames_.empty()) {
                lower_inline_return(statement);
            } else {
                diagnostics_.error(statement.location,
                                   "ordinary return is not permitted in a naked function; use an explicit target control-transfer built-in");
            }
            return;
        case Statement::Kind::If:
            lower_if(statement);
            return;
        case Statement::Kind::Switch:
            lower_switch(statement);
            return;
        case Statement::Kind::Case:
        case Statement::Kind::Default:
            diagnostics_.error(statement.location,
                               "case/default label is not inside a raw-compatible switch");
            return;
        case Statement::Kind::While:
            lower_while(statement);
            return;
        case Statement::Kind::DoWhile:
            lower_do_while(statement);
            return;
        case Statement::Kind::For:
            lower_for(statement);
            return;
        case Statement::Kind::Break:
            if (break_targets_.empty() ||
                (!inline_frames_.empty() &&
                 break_targets_.size() <=
                     inline_frames_.back().break_base)) {
                diagnostics_.error(statement.location,
                                   "raw break is not inside a loop or switch");
            } else if (require_current(statement.location)) {
                (void)terminate_jump(break_targets_.back(),
                                     statement.location);
            }
            return;
        case Statement::Kind::Continue:
            if (loops_.empty() ||
                (!inline_frames_.empty() &&
                 loops_.size() <= inline_frames_.back().loop_base)) {
                diagnostics_.error(statement.location,
                                   "raw continue is not inside a loop");
            } else if (require_current(statement.location)) {
                (void)terminate_jump(loops_.back().continue_target,
                                     statement.location);
            }
            return;
        case Statement::Kind::Label:
            if (!inline_frames_.empty()) {
                diagnostics_.error(statement.location,
                                   "labels are not raw-compatible managed control flow");
            }
            return;
        }
    }

    void lower_goto(const Statement& statement) {
        if (!statement.expression) return;
        const auto& destination = unparenthesized(*statement.expression);
        if (destination.kind != Expr::Kind::Name) {
            diagnostics_.error(
                destination.location,
                "raw goto target must be a same-function label or hard-bound label value");
            return;
        }
        if (const auto* target_label =
                current_function_
                    ? hir_.label(current_function_->id, destination.text)
                    : nullptr) {
            const auto* form = find_instruction(target_, "$::_jmp");
            if (!form) return;
            mir::Instruction instruction{statement.location, form, {}};
            instruction.operands.push_back(
                label_operand(*target_label, destination.location));
            append_instruction(std::move(instruction));
            return;
        }
        const auto binding = bindings_.find(destination.text);
        if (binding == bindings_.end() ||
            !label_bindings_.contains(destination.text)) {
            diagnostics_.error(destination.location,
                               "raw goto target is not a hard-bound label value");
            return;
        }
        const auto* form = find_instruction(target_, "$::_jmp_indirect");
        if (!form) return;
        mir::Instruction instruction{statement.location, form, {}};
        instruction.operands.push_back(
            register_operand(*binding->second, destination.location));
        append_instruction(std::move(instruction));
    }

    void lower_declaration(const VariableDecl& declaration) {
        if (!inline_frames_.empty() && !declaration.storage_register &&
            !declaration.storage_stack && !declaration.storage_static &&
            !declaration.location_name) {
            if (declaration.dynamic_array_bound || !is_scalar(declaration.type)) {
                diagnostics_.error(
                    declaration.location,
                    "raw_inline automatic objects must be fixed-size non-address-taken scalars");
                return;
            }
            const auto* entry = acquire_scratch(
                declaration.type, declaration.location, "local '" +
                    declaration.name + "'");
            if (!entry) return;
            inline_frames_.back().owned.push_back(entry);
            bindings_[declaration.name] = entry;
            binding_types_[declaration.name] = declaration.type;
            binding_signed_[declaration.name] =
                signed_integer_type(declaration.type);
            inline_frames_.back().readonly_names.erase(declaration.name);
            if (declaration.initializer) {
                (void)lower_inline_value(*declaration.initializer, *entry,
                                         declaration.type);
            }
            return;
        }
        if (!inline_frames_.empty()) {
            diagnostics_.error(
                declaration.location,
                "raw_inline local requires automatic scalar storage; explicit register, stack, static, and VLA storage are not compatible");
            return;
        }
        if (!declaration.storage_register || declaration.storage_stack ||
            !declaration.location_name || *declaration.location_name == "auto") {
            diagnostics_.error(declaration.location,
                               "ordinary automatic and stack objects are not permitted in a naked function");
            return;
        }
        const auto* entry = direct_register(*declaration.location_name,
                                            declaration.location, declaration.name);
        if (!entry) return;
        if (!raw_binding_type(*entry, declaration.type)) {
            diagnostics_.error(
                declaration.location,
                "raw register '" + std::string(entry->name) +
                    "' cannot carry object type '" +
                    type_name(declaration.type) + "'");
            return;
        }
        if (entry->storage == "rsp") {
            diagnostics_.error(declaration.location,
                               "raw stack pointer changes must use registry-declared stack instructions");
            return;
        }
        if (bindings_.contains(declaration.name)) {
            diagnostics_.error(declaration.location,
                               "duplicate raw register object '" + declaration.name + "'");
            return;
        }
        bindings_[declaration.name] = entry;
        protected_storages_.insert(std::string(entry->storage));
        binding_types_[declaration.name] = declaration.type;
        binding_signed_[declaration.name] = signed_integer_type(declaration.type);
        if (declaration.type->kind == Type::Kind::Builtin &&
            declaration.type->builtin == BuiltinType::Label) {
            label_bindings_.insert(declaration.name);
        }
        if (!declaration.initializer) return;
        if (!raw_integer_type(declaration.type) &&
            !raw_scalar_float_type(declaration.type)) {
            diagnostics_.error(declaration.initializer->location,
                               "raw register initializer requires a supported scalar register value");
            return;
        }
        (void)lower_inline_value(*declaration.initializer, *entry,
                                 declaration.type);
    }

    static mir::Operand register_operand(const RegisterEntry& entry,
                                         SourceLocation location) {
        mir::Operand operand;
        operand.kind = mir::Operand::Kind::Register;
        operand.location = location;
        operand.reg = {std::string(entry.name), std::string(entry.storage), entry.bits};
        return operand;
    }

    static mir::Operand immediate_operand(std::uint64_t value, unsigned bits,
                                          SourceLocation location) {
        mir::Operand operand;
        operand.kind = mir::Operand::Kind::Immediate;
        operand.location = location;
        operand.immediate.value = value;
        operand.immediate.bits = bits;
        return operand;
    }

    static mir::Operand memory_operand(const RawMemoryAddress& address,
                                       SourceLocation location) {
        mir::Operand operand;
        operand.kind = mir::Operand::Kind::Memory;
        operand.location = location;
        operand.memory.base = {
            std::string(address.base->name),
            std::string(address.base->storage), address.base->bits};
        if (address.index) {
            operand.memory.index = mir::RegisterOperand{
                std::string(address.index->name),
                std::string(address.index->storage), address.index->bits};
            operand.memory.scale = address.scale;
        }
        operand.memory.displacement = address.displacement;
        operand.memory.bits = type_bits(address.pointee);
        operand.memory.is_volatile = address.pointee->is_volatile;
        operand.memory.is_atomic = address.pointee->is_atomic;
        return operand;
    }

    std::optional<mir::PatchSink> resolve_patch_sink(
        const Expr& expression) {
        if (!current_function_) return std::nullopt;
        const hir::Object* result = nullptr;
        std::string_view candidate_name;
        const auto consider = [&](const hir::Object& object) {
            if (object.source_name != candidate_name) return;
            if (object.linkage == Linkage::Static &&
                object.source_unit != current_function_->source_unit) {
                return;
            }
            result = &object;
        };
        auto resolve = [&](std::string_view name) -> const hir::Object* {
            candidate_name = name;
            result = nullptr;
            for (const auto& object : hir_.objects) consider(object);
            if (!result && name.find("::") == std::string::npos) {
                const auto separator =
                    current_function_->source_name.rfind("::");
                if (separator != std::string::npos) {
                    const auto qualified = current_function_->source_name.substr(
                                               0, separator + 2) +
                                           std::string(name);
                    candidate_name = qualified;
                    for (const auto& object : hir_.objects) {
                        consider(object);
                    }
                }
            }
            return result;
        };
        auto sink = mir::resolve_patch_sink_designator(
            expression, hir_, target_, resolve, diagnostics_);
        if (!sink) return std::nullopt;
        const auto key = std::pair{sink->object.value, sink->offset};
        if (!patch_sinks_.insert(key).second) {
            diagnostics_.error(expression.location,
                               "$::patch address sink is used by more than one site");
            return std::nullopt;
        }
        module_.object_definitions.insert(sink->object.value);
        return sink;
    }

    std::optional<mir::Operand> lower_operand(const Expr& expression,
                                             const InstructionOperandEntry& specification) {
        const Expr* source = &expression;
        std::optional<mir::PatchSink> patch_sink;
        bool patch = false;
        if (expression.kind == Expr::Kind::Call && expression.left &&
            expression.left->kind == Expr::Kind::Name &&
            expression.left->text == "$::patch") {
            patch = true;
            if (!specification.patchable) {
                diagnostics_.error(
                    expression.location,
                    "selected instruction operand has no stable patchable field");
                return std::nullopt;
            }
            if (expression.arguments.empty() || expression.arguments.size() > 2) {
                diagnostics_.error(expression.location,
                                   "$::patch requires an initial value and optional address sink");
                return std::nullopt;
            }
            source = expression.arguments.front().get();
            if (expression.arguments.size() == 2) {
                patch_sink = resolve_patch_sink(*expression.arguments[1]);
                if (!patch_sink) return std::nullopt;
            }
        }
        if (specification.allow_label) {
            if (const auto* label = resolve_label(*source)) {
                return label_operand(*label, source->location);
            }
            return std::nullopt;
        }
        if (specification.allow_memory) {
            const auto address = memory_address(*source, true);
            if (!address) return std::nullopt;
            const auto bits = type_bits(address->pointee);
            if (specification.memory_bits != 0 &&
                bits != specification.memory_bits) {
                diagnostics_.error(
                    source->location,
                    "instruction memory operand requires a " +
                        std::to_string(specification.memory_bits) +
                        "-bit lvalue");
                return std::nullopt;
            }
            if (address->pointee->is_atomic &&
                !specification.allow_atomic_memory) {
                diagnostics_.error(
                    source->location,
                    "ordinary instruction form cannot access an atomic-qualified lvalue");
                return std::nullopt;
            }
            if ((specification.role == InstructionOperandRole::Output ||
                 specification.role == InstructionOperandRole::InOut) &&
                address->pointee->is_const) {
                diagnostics_.error(source->location,
                                   "instruction cannot write a const-qualified lvalue");
                return std::nullopt;
            }
            return memory_operand(*address, source->location);
        }
        if (source->kind == Expr::Kind::Name && specification.allow_register) {
            const auto found = bindings_.find(source->text);
            if (found == bindings_.end()) {
                diagnostics_.error(source->location,
                                    "unknown raw register object '" + source->text + "'");
                return std::nullopt;
            }
            const auto* entry = found->second;
            if (entry->bits != specification.register_bits) {
                diagnostics_.error(expression.location,
                                   "instruction operand requires a " +
                                       std::to_string(specification.register_bits) +
                                       "-bit register");
                return std::nullopt;
            }
            if (!specification.register_class.empty() &&
                entry->register_class != specification.register_class) {
                diagnostics_.error(
                    expression.location,
                    "instruction operand requires target register class '" +
                        std::string(specification.register_class) + "'");
                return std::nullopt;
            }
            if (!specification.register_storage.empty() &&
                entry->storage != specification.register_storage) {
                diagnostics_.error(
                    expression.location,
                    "instruction operand requires physical register '" +
                        std::string(specification.register_storage) + "'");
                return std::nullopt;
            }
            if ((specification.role == InstructionOperandRole::Output ||
                 specification.role == InstructionOperandRole::InOut) &&
                entry->storage == "rsp") {
                diagnostics_.error(expression.location,
                                   "raw stack pointer changes must use registry-declared stack instructions");
                return std::nullopt;
            }
            return register_operand(*entry, source->location);
        }
        if (specification.allow_immediate) {
            const auto value = integer_literal(*source);
            if (value) {
                if (!fits_instruction_immediate(*value, specification)) {
                    diagnostics_.error(source->location,
                                       "immediate does not fit the selected x86-64 instruction form");
                    return std::nullopt;
                }
                auto operand = immediate_operand(*value, specification.immediate_bits,
                                                 source->location);
                if (patch) {
                    operand.immediate.patch = true;
                    operand.immediate.patch_id = next_patch_id_++;
                    if (patch_sink) {
                        operand.immediate.patch_sink = *patch_sink;
                    }
                }
                return operand;
            }
        }
        diagnostics_.error(expression.location,
                           "operand does not match the target instruction registry form");
        return std::nullopt;
    }

    static bool register_matches(const RegisterEntry& entry,
                                 const InstructionOperandEntry& operand) {
        return operand.allow_register && entry.bits == operand.register_bits &&
               (operand.register_class.empty() ||
                entry.register_class == operand.register_class) &&
               (operand.register_storage.empty() ||
                entry.storage == operand.register_storage);
    }

    const InstructionEntry* unary_register_form(
        std::string_view name, const RegisterEntry& destination) const {
        for (const auto* form : find_instruction_forms(target_, name)) {
            if (!missing_feature(*form) && form->operands.size() == 1 &&
                register_matches(destination, form->operands[0])) {
                return form;
            }
        }
        return nullptr;
    }

    const InstructionEntry* binary_register_form(
        std::string_view name, const RegisterEntry& destination,
        const RegisterEntry& source) const {
        for (const auto* form : find_instruction_forms(target_, name)) {
            if (!missing_feature(*form) && form->operands.size() == 2 &&
                register_matches(destination, form->operands[0]) &&
                register_matches(source, form->operands[1])) {
                return form;
            }
        }
        return nullptr;
    }

    const InstructionEntry* binary_immediate_form(
        std::string_view name, const RegisterEntry& destination,
        std::uint64_t value) const {
        for (const auto* form : find_instruction_forms(target_, name)) {
            if (!missing_feature(*form) && form->operands.size() == 2 &&
                register_matches(destination, form->operands[0]) &&
                form->operands[1].allow_immediate &&
                fits_instruction_immediate(value, form->operands[1])) {
                return form;
            }
        }
        return nullptr;
    }

    const InstructionEntry* register_memory_form(
        std::string_view name, const RegisterEntry& destination,
        const RawMemoryAddress& source) const {
        const auto bits = type_bits(source.pointee);
        for (const auto* form : find_instruction_forms(target_, name)) {
            if (!missing_feature(*form) && form->operands.size() == 2 &&
                register_matches(destination, form->operands[0]) &&
                form->operands[1].allow_memory &&
                (form->operands[1].memory_bits == 0 ||
                 form->operands[1].memory_bits == bits) &&
                (!source.pointee->is_atomic ||
                 form->operands[1].allow_atomic_memory)) {
                return form;
            }
        }
        return nullptr;
    }

    const InstructionEntry* memory_register_form(
        std::string_view name, const RawMemoryAddress& destination,
        const RegisterEntry& source) const {
        const auto bits = type_bits(destination.pointee);
        for (const auto* form : find_instruction_forms(target_, name)) {
            if (!missing_feature(*form) && form->operands.size() == 2 &&
                form->operands[0].allow_memory &&
                (form->operands[0].memory_bits == 0 ||
                 form->operands[0].memory_bits == bits) &&
                (!destination.pointee->is_atomic ||
                 form->operands[0].allow_atomic_memory) &&
                register_matches(source, form->operands[1])) {
                return form;
            }
        }
        return nullptr;
    }

    bool emit_register_move(const RegisterEntry& destination,
                            const RegisterEntry& source,
                            SourceLocation location) {
        if (destination.storage == source.storage &&
            destination.bits == source.bits) {
            return true;
        }
        const auto* form = binary_register_form("$::_mov", destination, source);
        if (!form) {
            diagnostics_.error(
                location,
                "raw-compatible value move has no legal target register form");
            return false;
        }
        mir::Instruction instruction{location, form, {}};
        instruction.operands.push_back(register_operand(destination, location));
        instruction.operands.push_back(register_operand(source, location));
        append_instruction(std::move(instruction));
        return true;
    }

    static bool raw_scalar_float_type(const TypePtr& type) {
        return type && type->kind == Type::Kind::Builtin &&
               (type->builtin == BuiltinType::F32 ||
                type->builtin == BuiltinType::F64);
    }

    static std::string_view scalar_float_name(const TypePtr& type,
                                              std::string_view f32,
                                              std::string_view f64) {
        return type && type->kind == Type::Kind::Builtin &&
                       type->builtin == BuiltinType::F32
                   ? f32
                   : f64;
    }

    bool emit_float_move(const RegisterEntry& destination,
                         const RegisterEntry& source,
                         const TypePtr& type, SourceLocation location) {
        if (destination.storage == source.storage) return true;
        const auto* form = binary_register_form(
            scalar_float_name(type, "$::_movss", "$::_movsd"),
            destination, source);
        if (!form) {
            diagnostics_.error(location,
                               "raw-compatible floating move has no legal target form");
            return false;
        }
        mir::Instruction instruction{location, form, {}};
        instruction.operands.push_back(register_operand(destination, location));
        instruction.operands.push_back(register_operand(source, location));
        append_instruction(std::move(instruction));
        return true;
    }

    bool emit_value_move(const RegisterEntry& destination,
                         const RegisterEntry& source, const TypePtr& type,
                         SourceLocation location) {
        return raw_scalar_float_type(type)
                   ? emit_float_move(destination, source, type, location)
                   : emit_register_move(destination, source, location);
    }

    bool emit_cross_class_move(std::string_view name,
                               const RegisterEntry& destination,
                               const RegisterEntry& source,
                               SourceLocation location) {
        const auto* form = binary_register_form(name, destination, source);
        if (!form) {
            diagnostics_.error(location,
                               "raw-compatible bit move has no legal target form");
            return false;
        }
        mir::Instruction instruction{location, form, {}};
        instruction.operands.push_back(register_operand(destination, location));
        instruction.operands.push_back(register_operand(source, location));
        append_instruction(std::move(instruction));
        return true;
    }

    bool emit_integer_constant(const RegisterEntry& destination,
                               std::uint64_t value,
                               SourceLocation location) {
        if (destination.register_class != "integer") {
            diagnostics_.error(location,
                               "raw-compatible integer constant requires an integer register");
            return false;
        }
        const auto* full = integer_storage_view(destination.storage, 64);
        const auto* form = find_instruction(target_, "$::_movabs");
        if (!full || !form || form->operands.size() != 2 ||
            !register_matches(*full, form->operands[0])) {
            diagnostics_.error(location,
                               "selected target cannot materialize a raw integer constant without a spill");
            return false;
        }
        if (destination.bits < 64) {
            value &= (std::uint64_t{1} << destination.bits) - 1;
        }
        mir::Instruction instruction{location, form, {}};
        instruction.operands.push_back(register_operand(*full, location));
        instruction.operands.push_back(immediate_operand(value, 64, location));
        append_instruction(std::move(instruction));
        return true;
    }

    bool emit_unary(std::string_view name, const RegisterEntry& destination,
                    SourceLocation location) {
        const auto* form = unary_register_form(name, destination);
        if (!form) {
            diagnostics_.error(location,
                               "raw-compatible unary operation has no legal target form");
            return false;
        }
        mir::Instruction instruction{location, form, {}};
        instruction.operands.push_back(register_operand(destination, location));
        append_instruction(std::move(instruction));
        return true;
    }

    bool emit_binary_register(std::string_view name,
                              const RegisterEntry& destination,
                              const RegisterEntry& source,
                              SourceLocation location) {
        const auto* form = binary_register_form(name, destination, source);
        if (!form) {
            diagnostics_.error(location,
                               "raw-compatible binary operation has no legal target register form");
            return false;
        }
        mir::Instruction instruction{location, form, {}};
        instruction.operands.push_back(register_operand(destination, location));
        instruction.operands.push_back(register_operand(source, location));
        append_instruction(std::move(instruction));
        return true;
    }

    bool emit_binary_immediate(std::string_view name,
                               const RegisterEntry& destination,
                               std::uint64_t value,
                               SourceLocation location) {
        const auto* form = binary_immediate_form(name, destination, value);
        if (!form) return false;
        mir::Instruction instruction{location, form, {}};
        instruction.operands.push_back(register_operand(destination, location));
        instruction.operands.push_back(
            immediate_operand(value, form->operands[1].immediate_bits, location));
        append_instruction(std::move(instruction));
        return true;
    }

    bool emit_load(const RegisterEntry& destination,
                   const RawMemoryAddress& source,
                   SourceLocation location) {
        const auto* form = register_memory_form("$::_mov", destination, source);
        if (!form) {
            diagnostics_.error(location,
                               "raw-compatible load has no legal target form");
            return false;
        }
        mir::Instruction instruction{location, form, {}};
        instruction.operands.push_back(register_operand(destination, location));
        instruction.operands.push_back(memory_operand(source, location));
        append_instruction(std::move(instruction));
        return true;
    }

    bool emit_store(const RawMemoryAddress& destination,
                    const RegisterEntry& source,
                    SourceLocation location) {
        if (destination.pointee->is_const) {
            diagnostics_.error(location,
                               "raw-compatible assignment cannot write a const-qualified lvalue");
            return false;
        }
        const auto* form = memory_register_form("$::_mov", destination, source);
        if (!form) {
            diagnostics_.error(location,
                               "raw-compatible store has no legal target form");
            return false;
        }
        mir::Instruction instruction{location, form, {}};
        instruction.operands.push_back(memory_operand(destination, location));
        instruction.operands.push_back(register_operand(source, location));
        append_instruction(std::move(instruction));
        return true;
    }

    bool emit_float_load(const RegisterEntry& destination,
                         const RawMemoryAddress& source,
                         const TypePtr& type, SourceLocation location) {
        const auto* form = register_memory_form(
            scalar_float_name(type, "$::_movss", "$::_movsd"),
            destination, source);
        if (!form) {
            diagnostics_.error(location,
                               "raw-compatible floating load has no legal target form");
            return false;
        }
        mir::Instruction instruction{location, form, {}};
        instruction.operands.push_back(register_operand(destination, location));
        instruction.operands.push_back(memory_operand(source, location));
        append_instruction(std::move(instruction));
        return true;
    }

    bool emit_float_store(const RawMemoryAddress& destination,
                          const RegisterEntry& source,
                          const TypePtr& type, SourceLocation location) {
        if (destination.pointee->is_const) {
            diagnostics_.error(location,
                               "raw-compatible assignment cannot write a const-qualified lvalue");
            return false;
        }
        const auto* form = memory_register_form(
            scalar_float_name(type, "$::_movss", "$::_movsd"),
            destination, source);
        if (!form) {
            diagnostics_.error(location,
                               "raw-compatible floating store has no legal target form");
            return false;
        }
        mir::Instruction instruction{location, form, {}};
        instruction.operands.push_back(memory_operand(destination, location));
        instruction.operands.push_back(register_operand(source, location));
        append_instruction(std::move(instruction));
        return true;
    }

    bool emit_integer_extension(const RegisterEntry& destination,
                                const RegisterEntry& source,
                                bool signed_source,
                                SourceLocation location) {
        if (destination.bits <= source.bits) {
            diagnostics_.error(location,
                               "internal raw extension requires a wider destination");
            return false;
        }
        if (!signed_source && source.bits == 32 && destination.bits == 64) {
            const auto* narrow_destination =
                integer_storage_view(destination.storage, 32);
            return narrow_destination &&
                   emit_register_move(*narrow_destination, source, location);
        }
        const auto* form = binary_register_form(
            signed_source ? "$::_movsx" : "$::_movzx", destination, source);
        if (!form) {
            diagnostics_.error(
                location,
                "selected target has no spill-free integer extension form");
            return false;
        }
        mir::Instruction instruction{location, form, {}};
        instruction.operands.push_back(register_operand(destination, location));
        instruction.operands.push_back(register_operand(source, location));
        append_instruction(std::move(instruction));
        return true;
    }

    bool lower_integer_cast(const Expr& expression,
                            const RegisterEntry& destination,
                            const TypePtr& destination_type,
                            const TypePtr& source_type) {
        if (!source_type || !raw_integer_type(source_type)) {
            diagnostics_.error(expression.location,
                               "raw-compatible cast requires an integer or pointer source");
            return false;
        }
        if (const auto immediate = integer_literal(expression)) {
            return emit_integer_constant(destination, *immediate,
                                         expression.location);
        }
        const RegisterEntry* source = nullptr;
        const RegisterEntry* scratch = nullptr;
        const auto& value = unparenthesized(expression);
        if (value.kind == Expr::Kind::Name) {
            const auto found = bindings_.find(value.text);
            if (found != bindings_.end()) {
                source = integer_storage_view(found->second->storage,
                                              type_bits(source_type));
            }
        }
        if (!source) {
            scratch = acquire_scratch(source_type, expression.location,
                                      "cast temporary");
            source = scratch;
            if (!source ||
                !lower_inline_value(expression, *source, source_type)) {
                release_scratch(scratch);
                return false;
            }
        }
        bool result{};
        if (destination.bits == source->bits) {
            result = emit_register_move(destination, *source,
                                        expression.location);
        } else if (destination.bits < source->bits) {
            const auto* narrowed = integer_storage_view(source->storage,
                                                        destination.bits);
            result = narrowed && emit_register_move(destination, *narrowed,
                                                    expression.location);
        } else {
            result = emit_integer_extension(destination, *source,
                                            signed_integer_type(source_type),
                                            expression.location);
        }
        release_scratch(scratch);
        (void)destination_type;
        return result;
    }

    static bool raw_integer_type(const TypePtr& type) {
        return type &&
               ((type->kind == Type::Kind::Builtin && is_integer(type)) ||
                type->kind == Type::Kind::Pointer) &&
               type_bits(type) <= 64;
    }

    TypePtr expression_type(const Expr& expression,
                            const TypePtr& fallback = {}) const {
        const auto& source = unparenthesized(expression);
        if (source.kind == Expr::Kind::Floating) {
            if (source.evaluated_floating) {
                return builtin_type(source.evaluated_floating->type);
            }
            return builtin_type(source.text.ends_with("f32")
                                    ? BuiltinType::F32 : BuiltinType::F64);
        }
        if (source.kind == Expr::Kind::Name) {
            const auto found = binding_types_.find(source.text);
            if (found != binding_types_.end()) return found->second;
        }
        if (source.kind == Expr::Kind::Cast && source.type) return source.type;
        if (source.kind == Expr::Kind::Unary && source.left &&
            (source.text == "+" || source.text == "-" ||
             source.text == "~")) {
            return expression_type(*source.left, fallback);
        }
        if (source.kind == Expr::Kind::Call && source.left &&
            source.left->kind == Expr::Kind::Name) {
            if (const auto* callee = raw_inline_function(source.left->text);
                callee && callee->definition) {
                return callee->definition->return_type;
            }
        }
        if (source.kind == Expr::Kind::Unary && source.text == "*" &&
            source.left) {
            const auto pointer = expression_type(*source.left);
            if (pointer && pointer->kind == Type::Kind::Pointer) {
                return pointer->pointee;
            }
        }
        if (source.kind == Expr::Kind::Binary && source.text == "index" &&
            source.left) {
            const auto pointer = expression_type(*source.left);
            if (pointer && pointer->kind == Type::Kind::Pointer) {
                return pointer->pointee;
            }
        }
        if (source.kind == Expr::Kind::Binary && source.left &&
            !comparison_operator(source.text) && source.text != "&&" &&
            source.text != "||") {
            return expression_type(*source.left, fallback);
        }
        return fallback;
    }

    static std::optional<std::string_view> binary_instruction(
        std::string_view operation, bool signed_value) {
        if (operation == "+" || operation == "+=") return "$::_add";
        if (operation == "-" || operation == "-=") return "$::_sub";
        if (operation == "^" || operation == "^=") return "$::_xor";
        if (operation == "&" || operation == "&=") return "$::_and";
        if (operation == "|" || operation == "|=") return "$::_or";
        if (operation == "*" || operation == "*=") return "$::_imul";
        if (operation == "<<" || operation == "<<=") return "$::_shl";
        if (operation == ">>" || operation == ">>=") {
            return signed_value ? "$::_sar" : "$::_shr";
        }
        return std::nullopt;
    }

    const RegisterEntry* acquire_fixed_scratch(
        std::string_view storage, const TypePtr& type,
        const RegisterEntry& destination, SourceLocation location,
        bool& owned) {
        owned = false;
        const auto* view = storage_view(storage, type);
        if (!view) {
            diagnostics_.error(location,
                               "selected target has no required fixed-register view");
            return nullptr;
        }
        if (destination.storage == storage) return view;
        if (std::find(raw_inline_scratch_.begin(), raw_inline_scratch_.end(),
                      storage) == raw_inline_scratch_.end() ||
            protected_storages_.contains(std::string(storage)) ||
            scratch_in_use_.contains(std::string(storage))) {
            diagnostics_.error(
                location,
                "raw-compatible division requires '" + std::string(storage) +
                    "' in the naked caller's available clobber contract");
            return nullptr;
        }
        scratch_in_use_.insert(std::string(storage));
        owned = true;
        return view;
    }

    bool apply_division_rhs(std::string_view operation,
                            const RegisterEntry& destination,
                            const TypePtr& type, const Expr& right,
                            SourceLocation location,
                            const RegisterEntry* prepared_divisor = nullptr) {
        if (destination.bits < 16 || destination.bits > 64) {
            diagnostics_.error(
                location,
                "raw-compatible division currently requires a 16-, 32-, or 64-bit integer value");
            return false;
        }
        bool accumulator_owned{};
        bool high_owned{};
        const auto* accumulator = acquire_fixed_scratch(
            "rax", type, destination, location, accumulator_owned);
        if (!accumulator) return false;
        const auto* high = acquire_fixed_scratch(
            "rdx", type, destination, location, high_owned);
        if (!high) {
            if (accumulator_owned) release_scratch(accumulator);
            return false;
        }

        const auto& divisor_expression = unparenthesized(right);
        const RegisterEntry* divisor = prepared_divisor;
        const RegisterEntry* divisor_scratch = nullptr;
        if (divisor && (divisor->storage == "rax" ||
                        divisor->storage == "rdx")) {
            diagnostics_.error(right.location,
                               "raw-compatible preserved division operand requires a non-fixed clobber register");
            if (high_owned) release_scratch(high);
            if (accumulator_owned) release_scratch(accumulator);
            return false;
        }
        if (!divisor && divisor_expression.kind == Expr::Kind::Name) {
            const auto found = bindings_.find(divisor_expression.text);
            if (found != bindings_.end()) {
                divisor = integer_storage_view(found->second->storage,
                                               destination.bits);
                if (divisor && (divisor->storage == "rax" ||
                                divisor->storage == "rdx")) {
                    divisor = nullptr;
                }
            }
        }
        if (!divisor) {
            divisor_scratch = acquire_scratch(type, right.location,
                                              "division operand");
            divisor = divisor_scratch;
            if (!divisor ||
                !lower_inline_value(right, *divisor, type)) {
                release_scratch(divisor_scratch);
                if (high_owned) release_scratch(high);
                if (accumulator_owned) release_scratch(accumulator);
                return false;
            }
        }

        bool result = emit_register_move(*accumulator, destination, location);
        const bool signed_division = signed_integer_type(type);
        if (result && signed_division) {
            const auto name = destination.bits == 16 ? "$::_cwd" :
                              destination.bits == 32 ? "$::_cdq" : "$::_cqo";
            const auto* extend = find_instruction(target_, name);
            if (!extend || missing_feature(*extend)) {
                diagnostics_.error(
                    location,
                    "selected target has no fixed-register signed-division setup");
                result = false;
            } else {
                append_instruction({location, extend, {}});
            }
        } else if (result) {
            result = emit_integer_constant(*high, 0, location);
        }
        if (result) {
            const auto* divide = unary_register_form(
                signed_division ? "$::_idiv" : "$::_div", *divisor);
            if (!divide) {
                diagnostics_.error(location,
                                   "selected target has no fixed-register division form");
                result = false;
            } else {
                mir::Instruction instruction{location, divide, {}};
                instruction.operands.push_back(
                    register_operand(*divisor, right.location));
                append_instruction(std::move(instruction));
            }
        }
        if (result) {
            const bool remainder = operation == "%" || operation == "%=";
            result = emit_register_move(destination,
                                        remainder ? *high : *accumulator,
                                        location);
        }
        release_scratch(divisor_scratch);
        if (high_owned) release_scratch(high);
        if (accumulator_owned) release_scratch(accumulator);
        return result;
    }

    bool apply_binary_rhs(std::string_view operation,
                          const RegisterEntry& destination,
                          const TypePtr& type, const Expr& right,
                          SourceLocation location,
                          const RegisterEntry* prepared_source = nullptr) {
        if (operation == "/" || operation == "/=" || operation == "%" ||
            operation == "%=") {
            return apply_division_rhs(operation, destination, type, right,
                                      location, prepared_source);
        }
        const auto instruction_name =
            binary_instruction(operation, signed_integer_type(type));
        if (!instruction_name) {
            diagnostics_.error(location,
                               "raw-compatible operator '" +
                                   std::string(operation) +
                                   "' has no spill-free target legalization");
            return false;
        }
        if (prepared_source) {
            return emit_binary_register(*instruction_name, destination,
                                        *prepared_source, location);
        }
        const auto& source = unparenthesized(right);
        if (const auto immediate = integer_literal(source)) {
            if (emit_binary_immediate(*instruction_name, destination,
                                      *immediate, location)) {
                return true;
            }
            if (operation == "<<" || operation == "<<=" ||
                operation == ">>" || operation == ">>=") {
                diagnostics_.error(
                    source.location,
                    "raw-compatible shift count does not fit the target's immediate form");
                return false;
            }
        }
        if (source.kind == Expr::Kind::Name) {
            const auto found = bindings_.find(source.text);
            if (found != bindings_.end() &&
                found->second->bits == destination.bits &&
                found->second->register_class == destination.register_class) {
                return emit_binary_register(*instruction_name, destination,
                                            *found->second, location);
            }
        }
        if (operation == "<<" || operation == "<<=" ||
            operation == ">>" || operation == ">>=") {
            diagnostics_.error(
                source.location,
                "raw-compatible variable shifts require an explicitly modeled count-register form");
            return false;
        }
        const auto* scratch = acquire_scratch(type, source.location,
                                              "expression temporary");
        if (!scratch) return false;
        const bool lowered = lower_inline_value(source, *scratch, type);
        const bool emitted = lowered &&
            emit_binary_register(*instruction_name, destination, *scratch,
                                 location);
        release_scratch(scratch);
        return emitted;
    }

    bool lower_inline_boolean(const Expr& expression,
                              const RegisterEntry& destination,
                              const TypePtr& type) {
        const auto yes = new_block(expression.location);
        const auto no = new_block(expression.location);
        const auto merge = new_block(expression.location);
        if (!lower_raw_condition(expression, yes, no)) return false;
        enter_block(yes);
        const bool yes_ok = emit_integer_constant(destination, 1,
                                                  expression.location);
        if (current_block_) (void)terminate_jump(merge, expression.location);
        enter_block(no);
        const bool no_ok = emit_integer_constant(destination, 0,
                                                 expression.location);
        if (current_block_) (void)terminate_jump(merge, expression.location);
        enter_block(merge);
        (void)type;
        return yes_ok && no_ok;
    }

    bool emit_float_constant(const Expr& expression,
                             const RegisterEntry& destination,
                             const TypePtr& type) {
        const auto format = type->builtin == BuiltinType::F32
                                ? floating::Format::Binary32
                                : floating::Format::Binary64;
        floating::Value value;
        if (expression.evaluated_floating) {
            const auto& evaluated = *expression.evaluated_floating;
            const auto source_format = evaluated.type == BuiltinType::F32
                ? floating::Format::Binary32
                : evaluated.type == BuiltinType::F80
                    ? floating::Format::Extended80
                    : evaluated.type == BuiltinType::F128
                        ? floating::Format::Binary128
                        : floating::Format::Binary64;
            value = {evaluated.bits, source_format};
        } else {
            auto literal = expression.text;
            auto source_format = floating::Format::Binary64;
            if (literal.ends_with("f32")) {
                source_format = floating::Format::Binary32;
                literal.resize(literal.size() - 3);
            } else if (literal.ends_with("f80")) {
                source_format = floating::Format::Extended80;
                literal.resize(literal.size() - 3);
            } else if (literal.ends_with("f128")) {
                source_format = floating::Format::Binary128;
                literal.resize(literal.size() - 4);
            } else if (literal.ends_with("f64")) {
                literal.resize(literal.size() - 3);
            } else if (literal.ends_with("fptr")) {
                literal.resize(literal.size() - 4);
            }
            const auto parsed = floating::parse(std::move(literal),
                                                source_format);
            if (!parsed) {
                diagnostics_.error(expression.location,
                                   "invalid raw-compatible floating literal");
                return false;
            }
            value = *parsed;
        }
        value = floating::convert(value, format);
        const auto* bits = acquire_scratch(
            builtin_type(type->builtin == BuiltinType::F32
                             ? BuiltinType::U32 : BuiltinType::U64),
            expression.location, "floating constant bits");
        if (!bits) return false;
        const bool emitted =
            emit_integer_constant(*bits, value.bits.low, expression.location) &&
            emit_cross_class_move(type->builtin == BuiltinType::F32
                                      ? "$::_movd" : "$::_movq",
                                  destination, *bits, expression.location);
        release_scratch(bits);
        return emitted;
    }

    bool apply_float_binary_rhs(std::string_view operation,
                                const RegisterEntry& destination,
                                const TypePtr& type, const Expr& right,
                                SourceLocation location,
                                const RegisterEntry* prepared_source = nullptr) {
        std::string_view instruction;
        if (operation == "+" || operation == "+=") {
            instruction = scalar_float_name(type, "$::_addss", "$::_addsd");
        } else if (operation == "-" || operation == "-=") {
            instruction = scalar_float_name(type, "$::_subss", "$::_subsd");
        } else if (operation == "*" || operation == "*=") {
            instruction = scalar_float_name(type, "$::_mulss", "$::_mulsd");
        } else if (operation == "/" || operation == "/=") {
            instruction = scalar_float_name(type, "$::_divss", "$::_divsd");
        } else {
            diagnostics_.error(location,
                               "raw-compatible floating operator '" +
                                   std::string(operation) +
                                   "' has no spill-free target legalization");
            return false;
        }
        if (prepared_source) {
            return emit_binary_register(instruction, destination,
                                        *prepared_source, location);
        }
        const auto& source = unparenthesized(right);
        const auto source_type = expression_type(source, type);
        if (is_floating(source_type) &&
            (!raw_scalar_float_type(source_type) ||
             source_type->builtin != type->builtin)) {
            diagnostics_.error(source.location,
                               "raw-compatible mixed floating arithmetic requires a legalized conversion");
            return false;
        }
        if (source.kind == Expr::Kind::Name) {
            const auto found = bindings_.find(source.text);
            const auto found_type = binding_types_.find(source.text);
            if (found != bindings_.end() &&
                found_type != binding_types_.end() &&
                raw_scalar_float_type(found_type->second) &&
                found_type->second->builtin == type->builtin &&
                found->second->register_class == "simd") {
                return emit_binary_register(instruction, destination,
                                            *found->second, location);
            }
        }
        const auto* scratch = acquire_scratch(type, source.location,
                                              "floating expression temporary");
        if (!scratch) return false;
        const bool emitted = lower_inline_value(source, *scratch, type) &&
            emit_binary_register(instruction, destination, *scratch, location);
        release_scratch(scratch);
        return emitted;
    }

    bool expression_reads_storage(const Expr& expression,
                                  std::string_view storage) const {
        const auto& source = unparenthesized(expression);
        if (source.kind == Expr::Kind::Name) {
            const auto found = bindings_.find(source.text);
            return found != bindings_.end() &&
                   found->second->storage == storage;
        }
        for (const auto* child : {source.left.get(), source.right.get(),
                                  source.third.get()}) {
            if (child && expression_reads_storage(*child, storage)) return true;
        }
        for (const auto& argument : source.arguments) {
            if (argument && expression_reads_storage(*argument, storage)) {
                return true;
            }
        }
        return false;
    }

    bool direct_name_uses_storage(const Expr& expression,
                                  std::string_view storage) const {
        const auto& source = unparenthesized(expression);
        if (source.kind != Expr::Kind::Name) return false;
        const auto found = bindings_.find(source.text);
        return found != bindings_.end() &&
               found->second->storage == storage;
    }

    bool lower_inline_float_value(const Expr& expression,
                                  const RegisterEntry& destination,
                                  const TypePtr& type) {
        const auto& source = unparenthesized(expression);
        if (destination.register_class != "simd" || destination.bits != 128) {
            diagnostics_.error(source.location,
                               "raw-compatible f32/f64 value requires a SIMD register");
            return false;
        }
        if (source.kind == Expr::Kind::Floating) {
            return emit_float_constant(source, destination, type);
        }
        if (source.kind == Expr::Kind::Name) {
            const auto found = bindings_.find(source.text);
            const auto found_type = binding_types_.find(source.text);
            if (found == bindings_.end() ||
                found_type == binding_types_.end() ||
                !raw_scalar_float_type(found_type->second) ||
                found_type->second->builtin != type->builtin ||
                found->second->register_class != "simd") {
                diagnostics_.error(source.location,
                                   "raw-compatible floating value requires a matching f32/f64 SIMD register; conversion is not yet legalized");
                return false;
            }
            return emit_float_move(destination, *found->second, type,
                                   source.location);
        }
        if ((source.kind == Expr::Kind::Unary && source.text == "*") ||
            (source.kind == Expr::Kind::Binary && source.text == "index")) {
            const auto address = memory_address(source, true);
            return address && emit_float_load(destination, *address, type,
                                              source.location);
        }
        if (source.kind == Expr::Kind::Unary && source.left &&
            source.text == "+") {
            return lower_inline_value(*source.left, destination, type);
        }
        if (source.kind == Expr::Kind::Unary && source.left &&
            source.text == "-") {
            if (!lower_inline_value(*source.left, destination, type)) {
                return false;
            }
            const auto* sign = acquire_scratch(type, source.location,
                                               "floating sign mask");
            if (!sign) return false;
            const auto* bits = acquire_scratch(
                builtin_type(type->builtin == BuiltinType::F32
                                 ? BuiltinType::U32 : BuiltinType::U64),
                source.location, "floating sign bits");
            if (!bits) {
                release_scratch(sign);
                return false;
            }
            const bool emitted =
                emit_integer_constant(*bits,
                    type->builtin == BuiltinType::F32
                        ? UINT64_C(0x80000000)
                        : UINT64_C(0x8000000000000000),
                    source.location) &&
                emit_cross_class_move(type->builtin == BuiltinType::F32
                                          ? "$::_movd" : "$::_movq",
                                      *sign, *bits, source.location) &&
                emit_binary_register(type->builtin == BuiltinType::F32
                                         ? "$::_xorps" : "$::_xorpd",
                                     destination, *sign, source.location);
            release_scratch(bits);
            release_scratch(sign);
            return emitted;
        }
        if (source.kind == Expr::Kind::Binary && source.left && source.right) {
            const auto left_type = expression_type(*source.left, type);
            if (is_floating(left_type) &&
                (!raw_scalar_float_type(left_type) ||
                 left_type->builtin != type->builtin)) {
                diagnostics_.error(source.left->location,
                                   "raw-compatible mixed floating arithmetic requires a legalized conversion");
                return false;
            }
            const RegisterEntry* preserved = nullptr;
            if (expression_reads_storage(*source.right, destination.storage) &&
                !direct_name_uses_storage(*source.left,
                                          destination.storage)) {
                preserved = acquire_scratch(type, source.right->location,
                                            "preserved floating operand");
                if (!preserved) return false;
                if (!lower_inline_value(*source.right, *preserved, type)) {
                    release_scratch(preserved);
                    return false;
                }
            }
            const bool emitted =
                lower_inline_value(*source.left, destination, type) &&
                apply_float_binary_rhs(source.text, destination, type,
                                       *source.right, source.location,
                                       preserved);
            release_scratch(preserved);
            return emitted;
        }
        if (source.kind == Expr::Kind::Conditional && source.left &&
            source.right && source.third) {
            if (const auto condition = raw_constant_condition(*source.left)) {
                return lower_inline_value(
                    *(*condition ? source.right : source.third), destination,
                    type);
            }
            const auto yes = new_block(source.right->location);
            const auto no = new_block(source.third->location);
            const auto merge = new_block(source.location);
            if (!lower_raw_condition(*source.left, yes, no)) return false;
            enter_block(yes);
            const bool yes_ok = lower_inline_value(*source.right, destination,
                                                   type);
            if (current_block_) (void)terminate_jump(merge, source.location);
            enter_block(no);
            const bool no_ok = lower_inline_value(*source.third, destination,
                                                  type);
            if (current_block_) (void)terminate_jump(merge, source.location);
            enter_block(merge);
            return yes_ok && no_ok;
        }
        if (source.kind == Expr::Kind::Assign) {
            const auto assigned_type = source.left
                ? expression_type(*source.left) : TypePtr{};
            if (!raw_scalar_float_type(assigned_type) ||
                assigned_type->builtin != type->builtin) {
                diagnostics_.error(source.location,
                                   "raw-compatible floating assignment value requires a matching f32/f64 type");
                return false;
            }
            return lower_inline_assignment(source, &destination);
        }
        if (source.kind == Expr::Kind::Call) {
            const auto call_type = expression_type(source);
            if (!raw_scalar_float_type(call_type) ||
                call_type->builtin != type->builtin) {
                diagnostics_.error(source.location,
                                   "raw-compatible floating call result requires a matching f32/f64 type");
                return false;
            }
            return lower_raw_inline_call(source, &destination);
        }
        diagnostics_.error(source.location,
                           "raw-compatible floating expression cannot be legalized without a call, stack object, or spill");
        return false;
    }

    bool lower_inline_value(const Expr& expression,
                            const RegisterEntry& destination,
                            const TypePtr& type) {
        const auto& source = unparenthesized(expression);
        if (raw_scalar_float_type(type)) {
            return lower_inline_float_value(source, destination, type);
        }
        if (!raw_integer_type(type) || destination.register_class != "integer" ||
            destination.bits != type_bits(type)) {
            diagnostics_.error(
                source.location,
                "raw-compatible scalar lowering currently requires an integer or pointer value of at most 64 bits in a matching GPR");
            return false;
        }
        if (const auto immediate = integer_literal(source)) {
            return emit_integer_constant(destination, *immediate,
                                         source.location);
        }
        if (source.kind == Expr::Kind::Name) {
            const auto found = bindings_.find(source.text);
            if (found == bindings_.end()) {
                diagnostics_.error(source.location,
                                   "raw-compatible value refers to unavailable object '" +
                                       source.text + "'");
                return false;
            }
            const auto* view = integer_storage_view(found->second->storage,
                                                    destination.bits);
            if (!view) {
                diagnostics_.error(source.location,
                                   "raw-compatible value has no matching physical register view");
                return false;
            }
            return emit_register_move(destination, *view, source.location);
        }
        if ((source.kind == Expr::Kind::Unary && source.text == "*") ||
            (source.kind == Expr::Kind::Binary && source.text == "index")) {
            const auto address = memory_address(source, true);
            return address && emit_load(destination, *address, source.location);
        }
        if (source.kind == Expr::Kind::Cast && source.left) {
            const auto source_type = expression_type(*source.left, type);
            return lower_integer_cast(*source.left, destination, type,
                                      source_type);
        }
        if (source.kind == Expr::Kind::Unary && source.left) {
            if (source.text == "&") {
                diagnostics_.error(
                    source.location,
                    "raw_inline cannot take the address of an automatic scalar local");
                return false;
            }
            if (source.text == "+") {
                return lower_inline_value(*source.left, destination, type);
            }
            if (source.text == "-" || source.text == "~") {
                if (!lower_inline_value(*source.left, destination, type)) {
                    return false;
                }
                return emit_unary(source.text == "-" ? "$::_neg" : "$::_not",
                                  destination, source.location);
            }
            if (source.text == "!") {
                return lower_inline_boolean(source, destination, type);
            }
        }
        if (source.kind == Expr::Kind::Binary && source.left && source.right) {
            if (comparison_operator(source.text) || source.text == "&&" ||
                source.text == "||") {
                return lower_inline_boolean(source, destination, type);
            }
            const RegisterEntry* preserved = nullptr;
            if (expression_reads_storage(*source.right, destination.storage) &&
                !direct_name_uses_storage(*source.left,
                                          destination.storage)) {
                preserved = acquire_scratch(type, source.right->location,
                                            "preserved expression operand");
                if (!preserved) return false;
                if (!lower_inline_value(*source.right, *preserved, type)) {
                    release_scratch(preserved);
                    return false;
                }
            }
            const bool emitted =
                lower_inline_value(*source.left, destination, type) &&
                apply_binary_rhs(source.text, destination, type,
                                 *source.right, source.location, preserved);
            release_scratch(preserved);
            return emitted;
        }
        if (source.kind == Expr::Kind::Conditional && source.left &&
            source.right && source.third) {
            if (const auto condition = raw_constant_condition(*source.left)) {
                return lower_inline_value(
                    *(*condition ? source.right : source.third), destination,
                    type);
            }
            const auto yes = new_block(source.right->location);
            const auto no = new_block(source.third->location);
            const auto merge = new_block(source.location);
            if (!lower_raw_condition(*source.left, yes, no)) return false;
            enter_block(yes);
            const bool yes_ok =
                lower_inline_value(*source.right, destination, type);
            if (current_block_) (void)terminate_jump(merge, source.location);
            enter_block(no);
            const bool no_ok =
                lower_inline_value(*source.third, destination, type);
            if (current_block_) (void)terminate_jump(merge, source.location);
            enter_block(merge);
            return yes_ok && no_ok;
        }
        if (source.kind == Expr::Kind::Assign) {
            return lower_inline_assignment(source, &destination);
        }
        if (source.kind == Expr::Kind::Call) {
            return lower_raw_inline_call(source, &destination);
        }
        diagnostics_.error(
            source.location,
            "raw-compatible expression cannot be legalized without a call, stack object, or spill");
        return false;
    }

    bool lower_inline_assignment(const Expr& expression,
                                 const RegisterEntry* value_destination) {
        if (!expression.left || !expression.right) {
            diagnostics_.error(expression.location,
                               "raw-compatible assignment is incomplete");
            return false;
        }
        const auto& destination_expression =
            unparenthesized(*expression.left);
        if (destination_expression.kind == Expr::Kind::Name) {
            const auto found = bindings_.find(destination_expression.text);
            const auto type_found =
                binding_types_.find(destination_expression.text);
            if (found == bindings_.end() || type_found == binding_types_.end()) {
                diagnostics_.error(
                    destination_expression.location,
                    "raw assignment destination is not an available scalar register");
                return false;
            }
            if (!inline_frames_.empty() &&
                inline_frames_.back().readonly_names.contains(
                    destination_expression.text)) {
                diagnostics_.error(
                    destination_expression.location,
                    "raw_inline cannot modify an 'in' parameter; copy it to a local first");
                return false;
            }
            bool lowered{};
            if (expression.text == "=") {
                lowered = lower_inline_value(*expression.right, *found->second,
                                             type_found->second);
            } else {
                lowered = raw_scalar_float_type(type_found->second)
                    ? apply_float_binary_rhs(expression.text, *found->second,
                                             type_found->second,
                                             *expression.right,
                                             expression.location)
                    : apply_binary_rhs(expression.text, *found->second,
                                       type_found->second, *expression.right,
                                       expression.location);
            }
            if (!lowered || !value_destination) return lowered;
            const auto* view = raw_scalar_float_type(type_found->second)
                ? storage_view(found->second->storage, type_found->second)
                : integer_storage_view(found->second->storage,
                                       value_destination->bits);
            if (!view) {
                diagnostics_.error(
                    expression.location,
                    "raw-compatible assignment result has no matching register view");
                return false;
            }
            return emit_value_move(*value_destination, *view,
                                   type_found->second, expression.location);
        }
        if (expression.text != "=" ||
            !((destination_expression.kind == Expr::Kind::Unary &&
               destination_expression.text == "*") ||
              (destination_expression.kind == Expr::Kind::Binary &&
               destination_expression.text == "index"))) {
            diagnostics_.error(
                destination_expression.location,
                "raw-compatible assignment requires a scalar local/register or pointer lvalue");
            return false;
        }
        const auto address = memory_address(destination_expression, true);
        if (!address) return false;
        const auto* scratch = acquire_scratch(address->pointee,
                                              expression.location,
                                              "store value");
        if (!scratch) return false;
        bool lowered = lower_inline_value(*expression.right, *scratch,
                                          address->pointee);
        if (lowered && value_destination) {
            const auto* view = raw_scalar_float_type(address->pointee)
                ? storage_view(scratch->storage, address->pointee)
                : integer_storage_view(scratch->storage,
                                       value_destination->bits);
            lowered = view && emit_value_move(*value_destination, *view,
                                              address->pointee,
                                              expression.location);
        }
        if (lowered) {
            lowered = raw_scalar_float_type(address->pointee)
                ? emit_float_store(*address, *scratch, address->pointee,
                                   expression.location)
                : emit_store(*address, *scratch, expression.location);
        }
        release_scratch(scratch);
        return lowered;
    }

    bool lower_raw_inline_call(const Expr& expression,
                               const RegisterEntry* destination) {
        if (!expression.left || expression.left->kind != Expr::Kind::Name) {
            diagnostics_.error(expression.location,
                               "raw-compatible call must name a direct raw_inline function");
            return false;
        }
        const auto* callee = raw_inline_function(expression.left->text);
        if (!callee || !callee->definition || !callee->definition->body) {
            diagnostics_.error(
                expression.location,
                "ordinary or indirect calls are not permitted in a naked function");
            return false;
        }
        if (expression.arguments.size() != callee->definition->parameters.size()) {
            diagnostics_.error(expression.location,
                               "raw_inline argument count does not match '" +
                                   callee->source_name + "'");
            return false;
        }
        if (inline_call_stack_.size() >= 128 ||
            std::find(inline_call_stack_.begin(), inline_call_stack_.end(),
                      callee->id.value) != inline_call_stack_.end()) {
            diagnostics_.error(
                expression.location,
                "recursive raw_inline call cannot be completely eliminated");
            return false;
        }
        const auto& result_type = callee->definition->return_type;
        const bool returns_void =
            result_type && result_type->kind == Type::Kind::Builtin &&
            result_type->builtin == BuiltinType::Void;
        if (returns_void == (destination != nullptr)) {
            diagnostics_.error(
                expression.location,
                returns_void
                    ? "void raw_inline call cannot produce a scalar value"
                    : "non-void raw_inline call requires a destination");
            return false;
        }
        if (destination && !raw_binding_type(*destination, result_type)) {
            diagnostics_.error(
                expression.location,
                "raw_inline result type does not match the physical destination register");
            return false;
        }

        auto saved_bindings = bindings_;
        auto saved_types = binding_types_;
        auto saved_signed = binding_signed_;
        auto saved_labels = label_bindings_;
        std::vector<const RegisterEntry*> owned;
        std::unordered_set<std::string> readonly_names;
        const auto cleanup = [&] {
            for (const auto* entry : owned) release_scratch(entry);
            bindings_ = std::move(saved_bindings);
            binding_types_ = std::move(saved_types);
            binding_signed_ = std::move(saved_signed);
            label_bindings_ = std::move(saved_labels);
        };

        std::vector<const RegisterEntry*> parameter_bindings;
        parameter_bindings.reserve(callee->definition->parameters.size());
        for (std::size_t index = 0;
             index < callee->definition->parameters.size(); ++index) {
            const auto& parameter = callee->definition->parameters[index];
            const auto& argument =
                unparenthesized(*expression.arguments[index]);
            const RegisterEntry* binding = nullptr;
            if (argument.kind == Expr::Kind::Name) {
                const auto found = saved_bindings.find(argument.text);
                const auto found_type = saved_types.find(argument.text);
                const bool floating_mismatch =
                    raw_scalar_float_type(parameter.type) &&
                    (found_type == saved_types.end() ||
                     !raw_scalar_float_type(found_type->second) ||
                     found_type->second->builtin != parameter.type->builtin);
                if (found != saved_bindings.end() && !floating_mismatch) {
                    binding = storage_view(found->second->storage,
                                           parameter.type);
                }
            }
            if (!binding) {
                binding = acquire_scratch(parameter.type, argument.location,
                                          "raw_inline argument");
                if (!binding) {
                    cleanup();
                    return false;
                }
                owned.push_back(binding);
                if (!lower_inline_value(argument, *binding, parameter.type)) {
                    cleanup();
                    return false;
                }
            }
            parameter_bindings.push_back(binding);
        }
        for (std::size_t index = 0;
             index < callee->definition->parameters.size(); ++index) {
            const auto& parameter = callee->definition->parameters[index];
            const auto* binding = parameter_bindings[index];
            bindings_[parameter.name] = binding;
            binding_types_[parameter.name] = parameter.type;
            binding_signed_[parameter.name] =
                signed_integer_type(parameter.type);
            readonly_names.insert(parameter.name);
        }

        const auto continuation = new_block(expression.location);
        inline_call_stack_.push_back(callee->id.value);
        inline_frames_.push_back(
            {callee, destination, result_type, continuation,
             std::move(owned), std::move(readonly_names), loops_.size(),
             break_targets_.size(), false});
        lower_statement(*callee->definition->body);
        auto frame = std::move(inline_frames_.back());
        inline_frames_.pop_back();
        inline_call_stack_.pop_back();
        owned = std::move(frame.owned);
        if (current_block_) {
            if (!returns_void) {
                diagnostics_.error(
                    callee->location,
                    "raw_inline function can reach the end without returning a value");
            }
            (void)terminate_jump(continuation, expression.location);
        }
        cleanup();
        enter_block(continuation);
        return frame.saw_return || returns_void;
    }

    void lower_inline_return(const Statement& statement) {
        const auto* result = inline_frames_.back().result;
        const auto result_type = inline_frames_.back().result_type;
        const auto return_target = inline_frames_.back().return_target;
        const bool returns_void =
            result_type && result_type->kind == Type::Kind::Builtin &&
            result_type->builtin == BuiltinType::Void;
        if (returns_void) {
            if (statement.expression) {
                diagnostics_.error(statement.location,
                                   "void raw_inline function cannot return a value");
            }
        } else if (!statement.expression || !result) {
            diagnostics_.error(statement.location,
                               "non-void raw_inline function must return a value");
        } else {
            (void)lower_inline_value(*statement.expression, *result,
                                     result_type);
        }
        inline_frames_.back().saw_return = true;
        if (current_block_) {
            (void)terminate_jump(return_target, statement.location);
        }
    }

    void lower_expression_statement(const Expr& expression) {
        if (expression.kind == Expr::Kind::Assign) {
            lower_raw_assignment(expression);
            return;
        }
        if (!inline_frames_.empty() &&
            expression.kind == Expr::Kind::Unary && expression.left &&
            (expression.text == "++" || expression.text == "--" ||
             expression.text == "post++" || expression.text == "post--")) {
            const auto& subject = unparenthesized(*expression.left);
            if (subject.kind != Expr::Kind::Name) {
                diagnostics_.error(expression.location,
                                   "raw-compatible increment requires a scalar local");
                return;
            }
            const auto found = bindings_.find(subject.text);
            if (found == bindings_.end()) {
                diagnostics_.error(subject.location,
                                   "raw-compatible increment refers to an unavailable local");
                return;
            }
            (void)emit_unary((expression.text == "++" ||
                              expression.text == "post++")
                                 ? "$::_inc"
                                 : "$::_dec",
                             *found->second, expression.location);
            return;
        }
        if (expression.kind == Expr::Kind::Call && expression.left &&
            expression.left->kind == Expr::Kind::Name) {
            if (const auto* callee =
                    raw_inline_function(expression.left->text)) {
                const auto& result_type = callee->definition->return_type;
                const bool returns_void =
                    result_type && result_type->kind == Type::Kind::Builtin &&
                    result_type->builtin == BuiltinType::Void;
                const RegisterEntry* discarded = nullptr;
                if (!returns_void) {
                    discarded = acquire_scratch(result_type,
                                                expression.location,
                                                "discarded raw_inline result");
                    if (!discarded) return;
                }
                (void)lower_raw_inline_call(expression, discarded);
                release_scratch(discarded);
                return;
            }
        }
        if (expression.kind != Expr::Kind::Call || !expression.left ||
            expression.left->kind != Expr::Kind::Name) {
            diagnostics_.error(expression.location,
                               "naked expression statements must be target instruction calls");
            return;
        }
        const auto& name = expression.left->text;
        if (!target_has_instruction(target_, name)) {
            diagnostics_.error(expression.location,
                               "ordinary calls are not permitted in a naked function");
            return;
        }
        const auto* form = select_instruction(name, expression.arguments,
                                              expression.location);
        if (!form) return;
        if (!inline_frames_.empty() &&
            (form->control != InstructionControlEffect::None ||
             form->stack_delta != 0 || form->ordered_stack_delta != 0 ||
             form->ordered_stack_reset)) {
            diagnostics_.error(
                expression.location,
                "raw_inline body cannot perform a raw control transfer or alter machine stack state");
            return;
        }
        mir::Instruction instruction{expression.location, form, {}};
        for (std::size_t index = 0; index < form->operands.size(); ++index) {
            auto operand = lower_operand(*expression.arguments[index], form->operands[index]);
            if (!operand) return;
            instruction.operands.push_back(std::move(*operand));
        }
        if (form->assembly_mask_operand) {
            const auto index = *form->assembly_mask_operand;
            if (index >= instruction.operands.size() ||
                instruction.operands[index].kind != mir::Operand::Kind::Register ||
                instruction.operands[index].reg.storage == "k0") {
                diagnostics_.error(
                    index < expression.arguments.size()
                        ? expression.arguments[index]->location
                        : expression.location,
                    "explicit EVEX writemask must use k1-k7; k0 means no mask");
                return;
            }
        }
        append_instruction(std::move(instruction));
    }

    static const Expr& unparenthesized(const Expr& expression) {
        if (expression.kind == Expr::Kind::Parenthesized && expression.left) {
            return unparenthesized(*expression.left);
        }
        return expression;
    }

    static bool comparison_operator(std::string_view operation) {
        return operation == "==" || operation == "!=" ||
               operation == "<" || operation == "<=" ||
               operation == ">" || operation == ">=";
    }

    static std::string_view swapped_comparison(std::string_view operation) {
        if (operation == "<") return ">";
        if (operation == "<=") return ">=";
        if (operation == ">") return "<";
        if (operation == ">=") return "<=";
        return operation;
    }

    static bool explicitly_unsigned_literal(const Expr& expression) {
        const auto& source = unparenthesized(expression);
        if (source.kind == Expr::Kind::Unary && source.left &&
            (source.text == "+" || source.text == "-")) {
            return explicitly_unsigned_literal(*source.left);
        }
        if (source.kind != Expr::Kind::Integer) return false;
        const std::string_view text(source.text);
        return text.ends_with("u8") || text.ends_with("u16") ||
               text.ends_with("u32") || text.ends_with("u64") ||
               text.ends_with("u128") || text.ends_with("uptr");
    }

    static bool evaluate_constant_comparison(std::string_view operation,
                                             std::uint64_t left,
                                             std::uint64_t right,
                                             bool signed_comparison) {
        if (operation == "==") return left == right;
        if (operation == "!=") return left != right;
        if (signed_comparison) {
            const auto signed_left = static_cast<std::int64_t>(left);
            const auto signed_right = static_cast<std::int64_t>(right);
            if (operation == "<") return signed_left < signed_right;
            if (operation == "<=") return signed_left <= signed_right;
            if (operation == ">") return signed_left > signed_right;
            return signed_left >= signed_right;
        }
        if (operation == "<") return left < right;
        if (operation == "<=") return left <= right;
        if (operation == ">") return left > right;
        return left >= right;
    }

    static std::optional<bool> raw_constant_condition(
        const Expr& expression) {
        const auto& source = unparenthesized(expression);
        if (const auto value = integer_literal(source)) return *value != 0;
        if (source.kind == Expr::Kind::Unary && source.left &&
            source.text == "!") {
            const auto value = raw_constant_condition(*source.left);
            return value ? std::optional<bool>{!*value} : std::nullopt;
        }
        if (source.kind == Expr::Kind::Binary && source.left && source.right) {
            if (source.text == "&&") {
                const auto left = raw_constant_condition(*source.left);
                if (left && !*left) return false;
                const auto right = raw_constant_condition(*source.right);
                if (left && right) return *left && *right;
                return std::nullopt;
            }
            if (source.text == "||") {
                const auto left = raw_constant_condition(*source.left);
                if (left && *left) return true;
                const auto right = raw_constant_condition(*source.right);
                if (left && right) return *left || *right;
                return std::nullopt;
            }
            if (comparison_operator(source.text)) {
                const auto left = integer_literal(*source.left);
                const auto right = integer_literal(*source.right);
                if (left && right) {
                    return evaluate_constant_comparison(
                        source.text, *left, *right,
                        !explicitly_unsigned_literal(*source.left) &&
                            !explicitly_unsigned_literal(*source.right));
                }
            }
        }
        if (source.kind == Expr::Kind::Conditional && source.left &&
            source.right && source.third) {
            const auto condition = raw_constant_condition(*source.left);
            if (condition) {
                return raw_constant_condition(
                    *(*condition ? source.right : source.third));
            }
        }
        return std::nullopt;
    }

    bool lower_raw_comparison(const Expr& expression,
                              mir::BlockId true_target,
                              mir::BlockId false_target) {
        const Expr* left = &unparenthesized(*expression.left);
        const Expr* right = &unparenthesized(*expression.right);
        auto operation = std::string_view(expression.text);
        auto comparison_type = expression_type(*left, expression_type(*right));
        if (raw_scalar_float_type(comparison_type)) {
            const auto right_type = expression_type(*right);
            if (is_floating(right_type) &&
                right_type->builtin != comparison_type->builtin) {
                diagnostics_.error(expression.location,
                                   "raw-compatible mixed f32/f64 comparison requires an explicit legalized conversion");
                return false;
            }
            const RegisterEntry* left_register = nullptr;
            const RegisterEntry* left_scratch = nullptr;
            if (left->kind == Expr::Kind::Name) {
                const auto found = bindings_.find(left->text);
                if (found != bindings_.end()) left_register = found->second;
            }
            if (!left_register) {
                left_scratch = acquire_scratch(comparison_type, left->location,
                                               "floating comparison value");
                left_register = left_scratch;
                if (!left_register ||
                    !lower_inline_value(*left, *left_register,
                                        comparison_type)) {
                    release_scratch(left_scratch);
                    return false;
                }
            }
            const RegisterEntry* right_register = nullptr;
            const RegisterEntry* right_scratch = nullptr;
            if (right->kind == Expr::Kind::Name) {
                const auto found = bindings_.find(right->text);
                if (found != bindings_.end()) right_register = found->second;
            }
            if (!right_register) {
                right_scratch = acquire_scratch(comparison_type,
                                                right->location,
                                                "floating comparison temporary");
                right_register = right_scratch;
                if (!right_register ||
                    !lower_inline_value(*right, *right_register,
                                        comparison_type)) {
                    release_scratch(right_scratch);
                    release_scratch(left_scratch);
                    return false;
                }
            }
            const auto* compare = binary_register_form(
                scalar_float_name(comparison_type, "$::_ucomiss",
                                  "$::_ucomisd"),
                *left_register, *right_register);
            if (!compare) {
                diagnostics_.error(expression.location,
                                   "selected target has no spill-free floating comparison form");
                release_scratch(right_scratch);
                release_scratch(left_scratch);
                return false;
            }
            mir::Instruction instruction{expression.location, compare, {}};
            instruction.operands.push_back(
                register_operand(*left_register, left->location));
            instruction.operands.push_back(
                register_operand(*right_register, right->location));
            append_instruction(std::move(instruction));
            release_scratch(right_scratch);
            release_scratch(left_scratch);

            std::string_view branch;
            if (operation == "==") branch = "$::_je";
            else if (operation == "!=") branch = "$::_jne";
            else if (operation == "<") branch = "$::_jb";
            else if (operation == "<=") branch = "$::_jbe";
            else if (operation == ">") branch = "$::_ja";
            else branch = "$::_jae";
            if (operation == "==" || operation == "!=" ||
                operation == "<" || operation == "<=") {
                const auto ordered = new_block(expression.location);
                if (!terminate_condition("$::_jp",
                                         operation == "!=" ? true_target
                                                           : false_target,
                                         ordered, expression.location)) {
                    return false;
                }
                enter_block(ordered);
            }
            return terminate_condition(branch, true_target, false_target,
                                       expression.location);
        }
        const auto left_constant = integer_literal(*left);
        const auto right_constant = integer_literal(*right);
        if (left_constant && right_constant) {
            const bool signed_comparison =
                !explicitly_unsigned_literal(*left) &&
                !explicitly_unsigned_literal(*right);
            return terminate_jump(
                evaluate_constant_comparison(operation, *left_constant,
                                             *right_constant,
                                             signed_comparison)
                    ? true_target
                    : false_target,
                expression.location);
        }
        if (left_constant && !right_constant) {
            std::swap(left, right);
            operation = swapped_comparison(operation);
        }
        const RegisterEntry* left_register = nullptr;
        const RegisterEntry* left_scratch = nullptr;
        TypePtr left_type = expression_type(*left);
        if (left->kind == Expr::Kind::Name) {
            const auto binding = bindings_.find(left->text);
            if (binding != bindings_.end()) left_register = binding->second;
        }
        if (!left_register && !inline_frames_.empty()) {
            left_type = expression_type(*left, expression_type(*right));
            left_scratch = acquire_scratch(left_type, left->location,
                                           "comparison value");
            left_register = left_scratch;
            if (left_register &&
                !lower_inline_value(*left, *left_register, left_type)) {
                release_scratch(left_scratch);
                return false;
            }
        }
        if (!left_register || left_register->register_class != "integer" ||
            left_register->bits > 64) {
            diagnostics_.error(
                left->location,
                "raw condition comparison requires an integer or pointer GPR value");
            release_scratch(left_scratch);
            return false;
        }

        const InstructionEntry* compare = nullptr;
        mir::Operand right_operand;
        const RegisterEntry* right_scratch = nullptr;
        if (const auto immediate = integer_literal(*right)) {
            compare = binary_immediate_form("$::_cmp", *left_register,
                                            *immediate);
            if (compare) {
                right_operand = immediate_operand(
                    *immediate, compare->operands[1].immediate_bits,
                    right->location);
            }
        }
        if (!compare && right->kind == Expr::Kind::Name) {
            const auto binding = bindings_.find(right->text);
            if (binding != bindings_.end()) {
                const auto* view = integer_storage_view(
                    binding->second->storage, left_register->bits);
                if (view) {
                    compare = binary_register_form("$::_cmp", *left_register,
                                                   *view);
                    if (compare) {
                        right_operand = register_operand(*view,
                                                         right->location);
                    }
                }
            }
        }
        if (!compare && !inline_frames_.empty()) {
            if (!left_type) left_type = expression_type(*right);
            right_scratch = acquire_scratch(left_type, right->location,
                                            "comparison temporary");
            if (right_scratch &&
                lower_inline_value(*right, *right_scratch, left_type)) {
                compare = binary_register_form("$::_cmp", *left_register,
                                               *right_scratch);
                if (compare) {
                    right_operand = register_operand(*right_scratch,
                                                     right->location);
                }
            }
        }
        if (!compare) {
            diagnostics_.error(expression.location,
                               "selected target has no spill-free integer comparison form");
            release_scratch(right_scratch);
            release_scratch(left_scratch);
            return false;
        }
        mir::Instruction instruction{expression.location, compare, {}};
        instruction.operands.push_back(
            register_operand(*left_register, left->location));
        instruction.operands.push_back(std::move(right_operand));
        append_instruction(std::move(instruction));

        bool signed_comparison = signed_integer_type(left_type);
        if (right->kind == Expr::Kind::Name) {
            const auto found = binding_signed_.find(right->text);
            signed_comparison = signed_comparison &&
                                found != binding_signed_.end() && found->second;
        } else if (explicitly_unsigned_literal(*right)) {
            signed_comparison = false;
        }

        release_scratch(right_scratch);
        release_scratch(left_scratch);

        std::string_view branch;
        if (operation == "==") branch = "$::_je";
        else if (operation == "!=") branch = "$::_jne";
        else if (operation == "<") branch = signed_comparison ? "$::_jl" : "$::_jb";
        else if (operation == "<=") branch = signed_comparison ? "$::_jle" : "$::_jbe";
        else if (operation == ">") branch = signed_comparison ? "$::_jg" : "$::_ja";
        else branch = signed_comparison ? "$::_jge" : "$::_jae";
        return terminate_condition(branch, true_target, false_target,
                                   expression.location);
    }

    bool lower_raw_condition(const Expr& expression,
                             mir::BlockId true_target,
                             mir::BlockId false_target) {
        if (!current_block_) return false;
        const auto& source = unparenthesized(expression);
        if (const auto constant = raw_constant_condition(source)) {
            return terminate_jump(*constant ? true_target : false_target,
                                  source.location);
        }
        if (source.kind == Expr::Kind::Unary && source.left &&
            source.text == "!") {
            return lower_raw_condition(*source.left, false_target, true_target);
        }
        if (source.kind == Expr::Kind::Assign && source.left) {
            lower_raw_assignment(source);
            return current_block_ &&
                   lower_raw_condition(*source.left, true_target, false_target);
        }
        if (source.kind == Expr::Kind::Binary && source.left && source.right) {
            if (source.text == "&&") {
                const auto right = new_block(source.right->location);
                if (!lower_raw_condition(*source.left, right, false_target)) {
                    return false;
                }
                enter_block(right);
                return lower_raw_condition(*source.right, true_target,
                                           false_target);
            }
            if (source.text == "||") {
                const auto right = new_block(source.right->location);
                if (!lower_raw_condition(*source.left, true_target, right)) {
                    return false;
                }
                enter_block(right);
                return lower_raw_condition(*source.right, true_target,
                                           false_target);
            }
            if (comparison_operator(source.text)) {
                return lower_raw_comparison(source, true_target, false_target);
            }
        }
        if (source.kind == Expr::Kind::Conditional && source.left &&
            source.right && source.third) {
            if (const auto condition = raw_constant_condition(*source.left)) {
                return lower_raw_condition(
                    *(*condition ? source.right : source.third), true_target,
                    false_target);
            }
            const auto yes = new_block(source.right->location);
            const auto no = new_block(source.third->location);
            if (!lower_raw_condition(*source.left, yes, no)) return false;
            enter_block(yes);
            const bool yes_ok = lower_raw_condition(*source.right, true_target,
                                                    false_target);
            enter_block(no);
            const bool no_ok = lower_raw_condition(*source.third, true_target,
                                                   false_target);
            return yes_ok && no_ok;
        }
        const RegisterEntry* condition_register = nullptr;
        const RegisterEntry* condition_scratch = nullptr;
        const auto condition_type = expression_type(source);
        if (source.kind == Expr::Kind::Name) {
            const auto found = bindings_.find(source.text);
            if (found != bindings_.end()) condition_register = found->second;
        } else {
            condition_scratch = acquire_scratch(condition_type, source.location,
                                                "condition value");
            condition_register = condition_scratch;
            if (condition_register &&
                !lower_inline_value(source, *condition_register,
                                    condition_type)) {
                release_scratch(condition_scratch);
                return false;
            }
        }
        if (condition_register && raw_scalar_float_type(condition_type)) {
            const auto* zero = acquire_scratch(condition_type,
                                               source.location,
                                               "floating condition zero");
            if (!zero) {
                release_scratch(condition_scratch);
                return false;
            }
            Expr zero_literal;
            zero_literal.kind = Expr::Kind::Floating;
            zero_literal.location = source.location;
            zero_literal.text = condition_type->builtin == BuiltinType::F32
                ? "0.0f32" : "0.0f64";
            const bool compared =
                emit_float_constant(zero_literal, *zero, condition_type) &&
                emit_binary_register(scalar_float_name(condition_type,
                    "$::_ucomiss", "$::_ucomisd"),
                    *condition_register, *zero, source.location);
            release_scratch(zero);
            release_scratch(condition_scratch);
            if (!compared) return false;
            const auto ordered = new_block(source.location);
            if (!terminate_condition("$::_jp", true_target, ordered,
                                     source.location)) {
                return false;
            }
            enter_block(ordered);
            return terminate_condition("$::_jne", true_target, false_target,
                                       source.location);
        }
        if (!condition_register ||
            condition_register->register_class != "integer" ||
            condition_register->bits > 64) {
            diagnostics_.error(
                source.location,
                "raw structured condition requires an integer or pointer GPR value");
            release_scratch(condition_scratch);
            return false;
        }
        const auto* compare = binary_immediate_form("$::_cmp", *condition_register,
                                                    0);
        if (!compare) {
            release_scratch(condition_scratch);
            return false;
        }
        mir::Instruction instruction{source.location, compare, {}};
        instruction.operands.push_back(
            register_operand(*condition_register, source.location));
        instruction.operands.push_back(immediate_operand(
            0, compare->operands[1].immediate_bits, source.location));
        append_instruction(std::move(instruction));
        release_scratch(condition_scratch);
        return terminate_condition("$::_jne", true_target, false_target,
                                   source.location);
    }

    void lower_if(const Statement& statement) {
        if (!statement.condition || !statement.first ||
            !require_current(statement.location)) return;
        if (const auto condition = raw_constant_condition(*statement.condition)) {
            if (*condition) {
                lower_statement(*statement.first);
            } else if (statement.second) {
                lower_statement(*statement.second);
            }
            return;
        }
        const auto then_block = new_block(statement.first->location);
        const auto else_block = new_block(
            statement.second ? statement.second->location : statement.location);
        const auto merge_block = new_block(statement.location);
        if (!lower_raw_condition(*statement.condition, then_block, else_block)) return;

        enter_block(then_block);
        lower_statement(*statement.first);
        const bool then_reaches = current_block_.has_value();
        if (then_reaches) (void)terminate_jump(merge_block, statement.location);

        enter_block(else_block);
        if (statement.second) lower_statement(*statement.second);
        const bool else_reaches = current_block_.has_value();
        if (else_reaches) (void)terminate_jump(merge_block, statement.location);

        if (then_reaches || else_reaches) enter_block(merge_block);
    }

    static void flatten_switch_body(const Statement& statement,
                                    std::vector<const Statement*>& sequence) {
        if (statement.kind == Statement::Kind::Compound) {
            for (const auto& child : statement.statements) {
                flatten_switch_body(*child, sequence);
            }
            return;
        }
        sequence.push_back(&statement);
        if ((statement.kind == Statement::Kind::Case ||
             statement.kind == Statement::Kind::Default) &&
            statement.first) {
            flatten_switch_body(*statement.first, sequence);
        }
    }

    bool emit_switch_compare(const RegisterEntry& selector,
                             const TypePtr& selector_type,
                             std::uint64_t value, mir::BlockId match,
                             mir::BlockId next, SourceLocation location) {
        const InstructionEntry* compare =
            binary_immediate_form("$::_cmp", selector, value);
        const RegisterEntry* temporary = nullptr;
        mir::Operand right;
        if (compare) {
            right = immediate_operand(value,
                                      compare->operands[1].immediate_bits,
                                      location);
        } else {
            temporary = acquire_scratch(selector_type, location,
                                        "switch case value");
            if (!temporary ||
                !emit_integer_constant(*temporary, value, location)) {
                release_scratch(temporary);
                return false;
            }
            compare = binary_register_form("$::_cmp", selector, *temporary);
            if (!compare) {
                diagnostics_.error(
                    location,
                    "selected target has no spill-free switch comparison form");
                release_scratch(temporary);
                return false;
            }
            right = register_operand(*temporary, location);
        }
        mir::Instruction instruction{location, compare, {}};
        instruction.operands.push_back(register_operand(selector, location));
        instruction.operands.push_back(std::move(right));
        append_instruction(std::move(instruction));
        release_scratch(temporary);
        return terminate_condition("$::_je", match, next, location);
    }

    void lower_switch(const Statement& statement) {
        if (!statement.condition || !statement.first ||
            !require_current(statement.location)) {
            return;
        }
        std::vector<const Statement*> sequence;
        flatten_switch_body(*statement.first, sequence);
        std::vector<const Statement*> labels;
        const Statement* fallback = nullptr;
        std::unordered_set<std::uint64_t> values;
        for (const auto* item : sequence) {
            if (item->kind == Statement::Kind::Default) {
                if (fallback) {
                    diagnostics_.error(item->location,
                                       "raw-compatible switch has more than one default label");
                }
                fallback = item;
            } else if (item->kind == Statement::Kind::Case) {
                const auto value = item->expression
                    ? integer_literal(*item->expression)
                    : std::nullopt;
                if (!value) {
                    diagnostics_.error(item->location,
                                       "raw-compatible case requires an integer constant");
                    continue;
                }
                if (!values.insert(*value).second) {
                    diagnostics_.error(item->location,
                                       "duplicate raw-compatible case value");
                }
                labels.push_back(item);
            }
        }
        if (fallback) labels.push_back(fallback);

        const auto end = new_block(statement.location);
        auto saved_bindings = bindings_;
        auto saved_types = binding_types_;
        auto saved_signed = binding_signed_;
        auto saved_labels = label_bindings_;
        const auto owned_size = inline_frames_.empty()
            ? std::size_t{0}
            : inline_frames_.back().owned.size();
        const auto restore_scope = [&] {
            if (!inline_frames_.empty()) {
                while (inline_frames_.back().owned.size() > owned_size) {
                    release_scratch(inline_frames_.back().owned.back());
                    inline_frames_.back().owned.pop_back();
                }
            }
            bindings_ = std::move(saved_bindings);
            binding_types_ = std::move(saved_types);
            binding_signed_ = std::move(saved_signed);
            label_bindings_ = std::move(saved_labels);
        };

        if (const auto selector = integer_literal(*statement.condition)) {
            const Statement* selected = fallback;
            for (const auto* label : labels) {
                if (label->kind == Statement::Kind::Case && label->expression &&
                    integer_literal(*label->expression) == selector) {
                    selected = label;
                    break;
                }
            }
            if (!selected) {
                restore_scope();
                return;
            }
            bool active = false;
            break_targets_.push_back(end);
            for (const auto* item : sequence) {
                if (item == selected) active = true;
                if (!active || item->kind == Statement::Kind::Case ||
                    item->kind == Statement::Kind::Default ||
                    !current_block_) {
                    continue;
                }
                lower_statement(*item);
            }
            break_targets_.pop_back();
            if (current_block_) (void)terminate_jump(end, statement.location);
            restore_scope();
            enter_block(end);
            return;
        }

        TypePtr selector_type = expression_type(*statement.condition);
        const RegisterEntry* selector_register = nullptr;
        const RegisterEntry* selector_scratch = nullptr;
        const auto& selector_expression =
            unparenthesized(*statement.condition);
        if (selector_expression.kind == Expr::Kind::Name) {
            const auto found = bindings_.find(selector_expression.text);
            if (found != bindings_.end()) selector_register = found->second;
        }
        if (!selector_register) {
            selector_scratch = acquire_scratch(
                selector_type, statement.condition->location,
                "switch selector");
            selector_register = selector_scratch;
            if (!selector_register ||
                !lower_inline_value(*statement.condition, *selector_register,
                                    selector_type)) {
                release_scratch(selector_scratch);
                restore_scope();
                return;
            }
        }
        if (!selector_type) {
            selector_type = expression_type(selector_expression);
        }

        std::unordered_map<const Statement*, mir::BlockId> blocks;
        for (const auto* label : labels) {
            blocks.emplace(label, new_block(label->location));
        }
        std::vector<const Statement*> cases;
        for (const auto* label : labels) {
            if (label->kind == Statement::Kind::Case) cases.push_back(label);
        }
        for (std::size_t index = 0; index < cases.size(); ++index) {
            const auto next = new_block(cases[index]->location);
            const auto value = integer_literal(*cases[index]->expression);
            if (!value || !emit_switch_compare(
                              *selector_register, selector_type, *value,
                              blocks.at(cases[index]), next,
                              cases[index]->location)) {
                release_scratch(selector_scratch);
                restore_scope();
                return;
            }
            enter_block(next);
        }
        (void)terminate_jump(fallback ? blocks.at(fallback) : end,
                             statement.location);
        release_scratch(selector_scratch);

        bool saw_label = false;
        break_targets_.push_back(end);
        for (const auto* item : sequence) {
            if (item->kind == Statement::Kind::Case ||
                item->kind == Statement::Kind::Default) {
                saw_label = true;
                if (current_block_) {
                    (void)terminate_jump(blocks.at(item), item->location);
                }
                enter_block(blocks.at(item));
                continue;
            }
            if (saw_label && current_block_) lower_statement(*item);
        }
        break_targets_.pop_back();
        if (current_block_) (void)terminate_jump(end, statement.location);
        restore_scope();
        enter_block(end);
    }

    void lower_while(const Statement& statement) {
        if (!statement.condition || !statement.first ||
            !require_current(statement.location)) return;
        if (const auto condition = raw_constant_condition(*statement.condition);
            condition && !*condition) {
            return;
        }
        const auto test = new_block(statement.condition->location);
        const auto body = new_block(statement.first->location);
        const auto end = new_block(statement.location);
        (void)terminate_jump(test, statement.location);
        enter_block(test);
        if (!lower_raw_condition(*statement.condition, body, end)) return;
        enter_block(body);
        loops_.push_back({end, test});
        break_targets_.push_back(end);
        lower_statement(*statement.first);
        break_targets_.pop_back();
        loops_.pop_back();
        if (current_block_) (void)terminate_jump(test, statement.location);
        enter_block(end);
    }

    void lower_do_while(const Statement& statement) {
        if (!statement.condition || !statement.first ||
            !require_current(statement.location)) return;
        const auto body = new_block(statement.first->location);
        const auto test = new_block(statement.condition->location);
        const auto end = new_block(statement.location);
        (void)terminate_jump(body, statement.location);
        enter_block(body);
        loops_.push_back({end, test});
        break_targets_.push_back(end);
        lower_statement(*statement.first);
        break_targets_.pop_back();
        loops_.pop_back();
        if (current_block_) (void)terminate_jump(test, statement.location);
        enter_block(test);
        if (!lower_raw_condition(*statement.condition, body, end)) return;
        enter_block(end);
    }

    void lower_for(const Statement& statement) {
        if (!statement.first || !statement.second ||
            !require_current(statement.location)) return;
        lower_statement(*statement.first);
        if (!current_block_) return;
        if (statement.condition) {
            if (const auto condition =
                    raw_constant_condition(*statement.condition);
                condition && !*condition) {
                return;
            }
        }
        const auto test = new_block(statement.location);
        const auto body = new_block(statement.second->location);
        const auto increment = new_block(statement.location);
        const auto end = new_block(statement.location);
        (void)terminate_jump(test, statement.location);
        enter_block(test);
        if (statement.condition) {
            if (!lower_raw_condition(*statement.condition, body, end)) return;
        } else {
            (void)terminate_jump(body, statement.location);
        }
        enter_block(body);
        loops_.push_back({end, increment});
        break_targets_.push_back(end);
        lower_statement(*statement.second);
        break_targets_.pop_back();
        loops_.pop_back();
        if (current_block_) (void)terminate_jump(increment, statement.location);
        enter_block(increment);
        if (statement.increment) lower_expression_statement(*statement.increment);
        if (current_block_) (void)terminate_jump(test, statement.location);
        enter_block(end);
    }

    void lower_raw_assignment(const Expr& expression) {
        (void)lower_inline_assignment(expression, nullptr);
    }

    struct LoopContext {
        mir::BlockId break_target;
        mir::BlockId continue_target;
    };

    struct InlineFrame {
        const hir::Function* function{};
        const RegisterEntry* result{};
        TypePtr result_type;
        mir::BlockId return_target;
        std::vector<const RegisterEntry*> owned;
        std::unordered_set<std::string> readonly_names;
        std::size_t loop_base{};
        std::size_t break_base{};
        bool saw_return{};
    };

    struct RawState {
        int stack_depth{};
        unsigned ordered_depth{};
        std::unordered_set<std::string> defined_resources;
    };

    static bool tracked_resource(std::string_view resource) {
        return resource != "rsp" && resource != "memory";
    }

    void verify_cfg(const hir::Function& function) {
        for (auto& candidate : current_.blocks) {
            candidate.predecessors.clear();
            candidate.reachable = false;
        }
        for (const auto& candidate : current_.blocks) {
            for (const auto successor : candidate.successors) {
                auto& predecessors = block(successor).predecessors;
                if (std::find(predecessors.begin(), predecessors.end(), candidate.id) ==
                    predecessors.end()) predecessors.push_back(candidate.id);
            }
        }

        std::vector<std::optional<RawState>> entries(current_.blocks.size());
        std::vector<bool> queued(current_.blocks.size());
        std::vector<bool> depth_mismatch(current_.blocks.size());
        std::vector<bool> ordered_depth_mismatch(current_.blocks.size());
        std::deque<mir::BlockId> worklist;
        RawState entry_state;
        // A hard-bound raw register object denotes the physical state visible
        // at function entry even when it has no initializer.  This is what
        // permits explicit-state instructions such as CPUID to name their
        // architectural inputs without inventing an assignment.  Flags and
        // other non-register resources remain undefined until produced.
        for (const auto& [name, binding] : bindings_) {
            (void)name;
            if (binding->register_class == "x87") continue;
            entry_state.defined_resources.insert(
                std::string(binding->storage));
        }
        entry_state.ordered_depth = entry_ordered_depth_;
        entries[current_.entry.value] = std::move(entry_state);
        worklist.push_back(current_.entry);
        queued[current_.entry.value] = true;
        std::unordered_set<std::string> undefined_resource_diagnostics;

        while (!worklist.empty()) {
            const auto id = worklist.front();
            worklist.pop_front();
            queued[id.value] = false;
            auto state = *entries[id.value];
            auto& candidate = block(id);
            candidate.reachable = true;
            for (const auto& instruction : candidate.instructions) {
                const auto diagnose_undefined = [&](std::string_view resource,
                                                    SourceLocation location) {
                    if (!tracked_resource(resource) ||
                        state.defined_resources.contains(
                            std::string(resource))) {
                        return;
                    }
                    const auto key = std::to_string(location.offset) + ':' +
                                     std::string(resource);
                    if (undefined_resource_diagnostics.insert(key).second) {
                        diagnostics_.error(
                            location,
                            "raw instruction reads undefined machine resource '" +
                                std::string(resource) + "'");
                    }
                };
                for (std::size_t operand_index = 0;
                     operand_index < instruction.operands.size();
                     ++operand_index) {
                    const auto& operand = instruction.operands[operand_index];
                    if (operand.kind != mir::Operand::Kind::Register) continue;
                    const auto position = x87_position(operand.reg.storage);
                    if (position && *position >= state.ordered_depth) {
                        diagnostics_.error(
                            operand.location,
                            "raw x87 operand st" + std::to_string(*position) +
                                " is above the current dense stack depth " +
                                std::to_string(state.ordered_depth));
                    }
                    if (!position &&
                        operand_index < instruction.form->operands.size()) {
                        const auto role =
                            instruction.form->operands[operand_index].role;
                        if (role == InstructionOperandRole::Input ||
                            role == InstructionOperandRole::InOut) {
                            diagnose_undefined(operand.reg.storage,
                                               operand.location);
                        }
                    }
                }
                for (const auto& operand : instruction.operands) {
                    if (operand.kind != mir::Operand::Kind::Memory) continue;
                    diagnose_undefined(operand.memory.base.storage,
                                       operand.location);
                    if (operand.memory.index) {
                        diagnose_undefined(operand.memory.index->storage,
                                           operand.location);
                    }
                }
                for (const auto resource : instruction.form->implicit_reads) {
                    if (const auto position = x87_position(resource)) {
                        if (*position >= state.ordered_depth) {
                            diagnostics_.error(
                                instruction.location,
                                "raw x87 instruction reads st" +
                                    std::to_string(*position) +
                                    " above the current dense stack depth " +
                                    std::to_string(state.ordered_depth));
                        }
                        continue;
                    }
                    diagnose_undefined(resource, instruction.location);
                }
                state.stack_depth -= instruction.form->stack_delta;
                if (state.stack_depth < 0) {
                    diagnostics_.error(instruction.location,
                                       "raw stack instruction reads above the entry stack baseline");
                    state.stack_depth = 0;
                }
                const auto next_ordered_depth = instruction.form->ordered_stack_reset
                    ? 0
                    : static_cast<int>(state.ordered_depth) +
                          instruction.form->ordered_stack_delta;
                if (next_ordered_depth < 0) {
                    diagnostics_.error(
                        instruction.location,
                        "raw x87 instruction pops above the empty stack baseline");
                    state.ordered_depth = 0;
                } else if (next_ordered_depth > 8) {
                    diagnostics_.error(instruction.location,
                                       "raw x87 instruction overflows the eight-entry stack");
                    state.ordered_depth = 8;
                } else {
                    state.ordered_depth =
                        static_cast<unsigned>(next_ordered_depth);
                }
                for (const auto resource : instruction.form->implicit_writes) {
                    if (!x87_position(resource) && tracked_resource(resource)) {
                        state.defined_resources.insert(std::string(resource));
                    }
                }
                for (std::size_t operand_index = 0;
                     operand_index < instruction.operands.size() &&
                     operand_index < instruction.form->operands.size();
                     ++operand_index) {
                    const auto& operand = instruction.operands[operand_index];
                    const auto role =
                        instruction.form->operands[operand_index].role;
                    if (operand.kind == mir::Operand::Kind::Register &&
                        !x87_position(operand.reg.storage) &&
                        (role == InstructionOperandRole::Output ||
                         role == InstructionOperandRole::InOut)) {
                        state.defined_resources.insert(operand.reg.storage);
                    }
                }
                if (instruction.form->control == InstructionControlEffect::RawReturn &&
                    state.stack_depth != 0) {
                    diagnostics_.error(instruction.location,
                                       "raw stack depth is " +
                                           std::to_string(state.stack_depth) +
                                           " bytes at $::_ret; expected 0");
                }
                if (instruction.form->control == InstructionControlEffect::RawReturn &&
                    state.ordered_depth != return_ordered_depth_) {
                    diagnostics_.error(
                        instruction.location,
                        "raw x87 stack depth is " +
                            std::to_string(state.ordered_depth) +
                            " at $::_ret; interface requires " +
                            std::to_string(return_ordered_depth_));
                }
            }
            if (candidate.exit == mir::BlockExitKind::None) {
                diagnostics_.error(candidate.location.valid() ? candidate.location
                                                              : function.location,
                                   "reachable end of naked function requires an explicit target control transfer");
            }
            for (const auto successor : candidate.successors) {
                auto& incoming = entries[successor.value];
                bool changed = false;
                if (!incoming) {
                    incoming = state;
                    changed = true;
                } else {
                    if (incoming->stack_depth != state.stack_depth &&
                        !depth_mismatch[successor.value]) {
                        const auto& target_block = block(successor);
                        diagnostics_.error(
                            target_block.location.valid() ? target_block.location
                                                          : function.location,
                            "inconsistent raw stack depth at " +
                                (target_block.source_label.empty()
                                     ? std::string("control-flow join")
                                     : "label '" + target_block.source_label + "'") +
                                ": " + std::to_string(incoming->stack_depth) +
                                " bytes versus " + std::to_string(state.stack_depth) +
                                " bytes");
                        depth_mismatch[successor.value] = true;
                    }
                    if (incoming->ordered_depth != state.ordered_depth &&
                        !ordered_depth_mismatch[successor.value]) {
                        const auto& target_block = block(successor);
                        diagnostics_.error(
                            target_block.location.valid() ? target_block.location
                                                          : function.location,
                            "inconsistent raw x87 stack depth at " +
                                (target_block.source_label.empty()
                                     ? std::string("control-flow join")
                                     : "label '" + target_block.source_label + "'") +
                                ": " + std::to_string(incoming->ordered_depth) +
                                " entries versus " +
                                std::to_string(state.ordered_depth) + " entries");
                        ordered_depth_mismatch[successor.value] = true;
                    }
                    for (auto resource = incoming->defined_resources.begin();
                         resource != incoming->defined_resources.end();) {
                        if (!state.defined_resources.contains(*resource)) {
                            resource = incoming->defined_resources.erase(resource);
                            changed = true;
                        } else {
                            ++resource;
                        }
                    }
                }
                if (changed && !queued[successor.value]) {
                    worklist.push_back(successor);
                    queued[successor.value] = true;
                }
            }
        }
    }

    const hir::Module& hir_;
    const Subtarget& subtarget_;
    const TargetInfo& target_;
    Diagnostics& diagnostics_;
    mir::RawModule module_;
    mir::RawFunction current_;
    const hir::Function* current_function_{};
    std::optional<mir::BlockId> current_block_;
    std::unordered_map<std::string, const RegisterEntry*> bindings_;
    std::unordered_map<std::string, TypePtr> binding_types_;
    std::unordered_map<std::string, bool> binding_signed_;
    std::unordered_set<std::string> label_bindings_;
    std::vector<std::string> raw_inline_scratch_;
    std::unordered_set<std::string> scratch_in_use_;
    std::unordered_set<std::string> protected_storages_;
    std::vector<InlineFrame> inline_frames_;
    std::vector<std::uint32_t> inline_call_stack_;
    std::unordered_map<std::uint32_t, mir::BlockId> label_blocks_;
    std::unordered_set<std::uint32_t> laid_out_;
    std::vector<LoopContext> loops_;
    std::vector<mir::BlockId> break_targets_;
    std::set<std::pair<std::uint32_t, std::uint64_t>> patch_sinks_;
    std::uint32_t next_patch_id_{};
    unsigned entry_ordered_depth_{};
    unsigned return_ordered_depth_{};
};

bool safe_assembly_text(std::string_view text) {
    return std::all_of(text.begin(), text.end(), [](unsigned char ch) {
        return ch >= 0x20 && ch != 0x7f && ch != '"' && ch != '\\';
    });
}

std::string quoted(std::string_view text) { return '"' + std::string(text) + '"'; }

std::string assembly_symbol(std::string_view symbol) {
    const bool simple = !symbol.empty() &&
        std::all_of(symbol.begin(), symbol.end(), [](unsigned char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                   (ch >= '0' && ch <= '9') || ch == '_' || ch == '.' || ch == '$';
        });
    return simple ? std::string(symbol) : quoted(symbol);
}

std::string block_symbol(const mir::RawFunction& function, mir::BlockId block) {
    return ".Lcross." + std::to_string(function.source.value) + '.' +
           std::to_string(block.value);
}

class RawAssemblyEmitter {
public:
    RawAssemblyEmitter(const mir::RawModule& module,
                       const mir::ManagedModule& managed_module,
                       const hir::Module& hir_module,
                       const Subtarget& subtarget,
                       const CompilerOptions& options,
                       Diagnostics& diagnostics)
        : module_(module), managed_(managed_module), hir_(hir_module),
          options_(options), diagnostics_(diagnostics),
          format_(subtarget.object_format()) {}

    mir::AssemblyBundle run() {
        const auto managed_patch = first_managed_patch();
        if (format_ == ObjectFormat::Unsupported &&
            (!module_.functions.empty() || managed_patch)) {
            diagnostics_.error(!module_.functions.empty()
                                   ? module_.functions.front().location
                                   : managed_patch->location,
                               "raw x86-64 emission is not implemented for this object format");
            return bundle_;
        }
        bundle_.object_definitions = module_.object_definitions;
        for (const auto& function : module_.functions) emit_function(function);
        collect_managed_patch_sinks();
        for (const auto& patch : patches_) {
            bundle_.patch_relocations.push_back(
                {patch.sink, patch.end_label, patch.field_bytes});
        }
        bundle_.definitions = module_.definitions;
        bundle_.module_assembly = output_.str();
        return std::move(bundle_);
    }

private:
    static bool function_has_patch(const mir::RawFunction& function) {
        for (const auto& block : function.blocks) {
            for (const auto& instruction : block.instructions) {
                for (const auto& operand : instruction.operands) {
                    if (operand.kind == mir::Operand::Kind::Immediate &&
                        operand.immediate.patch) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    const mir::ManagedValue* first_managed_patch() const {
        for (const auto& function : managed_.functions) {
            for (const auto& value : function.values) {
                if (value.kind == mir::ValueKind::PatchValue) return &value;
            }
        }
        return nullptr;
    }

    static unsigned managed_patch_bits(const hir::Type& type) {
        if (type.kind != hir::Type::Kind::Builtin) return 0;
        switch (type.builtin) {
        case BuiltinType::Bool:
        case BuiltinType::I8:
        case BuiltinType::U8: return 8;
        case BuiltinType::I16:
        case BuiltinType::U16: return 16;
        case BuiltinType::I32:
        case BuiltinType::U32: return 32;
        case BuiltinType::I64:
        case BuiltinType::U64:
        case BuiltinType::Iptr:
        case BuiltinType::Uptr: return 64;
        default: return 0;
        }
    }

    void collect_managed_patch_sinks() {
        for (const auto& function : managed_.functions) {
            for (const auto& value : function.values) {
                if (value.kind != mir::ValueKind::PatchValue ||
                    !value.patch_sink) {
                    continue;
                }
                const auto& object = hir_.object(value.patch_sink->object);
                bundle_.object_definitions.insert(object.id.value);
                if (std::ranges::any_of(
                        patches_, [&](const PatchEmission& patch) {
                            return patch.sink == *value.patch_sink;
                        })) {
                    diagnostics_.error(
                        value.location,
                        "$::patch address sink is used by more than one site");
                    continue;
                }
                const auto bits = managed_patch_bits(hir_.type(value.type));
                if (bits == 0) {
                    diagnostics_.error(
                        value.location,
                        "managed $::patch value has no contiguous x86-64 field");
                    continue;
                }
                patches_.push_back(
                    {".Lcross.patch.value." +
                         std::to_string(value.patch_id) + ".end",
                     bits / 8, *value.patch_sink});
            }
        }
    }

    void emit_function(const mir::RawFunction& function) {
        const auto& entity = hir_.function(function.source);
        if (!safe_assembly_text(function.symbol) ||
            (function.section && !safe_assembly_text(*function.section))) {
            diagnostics_.error(function.location,
                               "raw symbol or section name cannot be represented by the selected assembler");
            return;
        }
        const auto symbol = assembly_symbol(function.symbol);
        const auto patch_function = function_has_patch(function);
        const auto split_function = options_.function_sections ||
                                    entity.retain ||
                                    entity.temperature !=
                                        hir::FunctionTemperature::Normal;
        const auto section_prefix = [&]() -> std::string {
            if (format_ == ObjectFormat::Coff) {
                if (entity.temperature == hir::FunctionTemperature::Hot)
                    return ".text$hot.";
                if (entity.temperature == hir::FunctionTemperature::Cold)
                    return ".text$cold.";
                return ".text$";
            }
            if (entity.temperature == hir::FunctionTemperature::Hot)
                return ".text.hot.";
            if (entity.temperature == hir::FunctionTemperature::Cold)
                return ".text.unlikely.";
            return ".text.";
        };
        const auto section = function.section
            ? *function.section
            : patch_function
                  ? std::string(format_ == ObjectFormat::Coff
                                    ? ".text$cross.patch."
                                    : ".text.cross.patch.") +
                        std::to_string(function.source.value)
            : split_function
                  ? section_prefix() + function.symbol
                  : std::string(".text");
        std::string section_error;
        const auto directive = assembly_section_directive(
            format_, {section, AssemblySectionKind::Code,
                      function.section.has_value(), entity.retain},
            section_error);
        if (!directive) {
            diagnostics_.error(function.location, section_error);
            return;
        }
        output_ << *directive << '\n';
        unsigned alignment_power = 4;
        if (options_.function_alignment != 0) {
            alignment_power = 0;
            for (auto alignment = options_.function_alignment;
                 alignment > 1; alignment >>= 1U) {
                ++alignment_power;
            }
        } else if (options_.optimize_for == OptimizationGoal::MinimumSize) {
            alignment_power = 2;
        } else if (options_.optimize_for == OptimizationGoal::Size) {
            alignment_power = 3;
        } else if (options_.tune == "skylake" ||
                   options_.tune == "skylake-avx512" ||
                   options_.tune == "znver3") {
            alignment_power = 5;
        }
        alignment_power = std::max(
            alignment_power,
            static_cast<unsigned>(std::countr_zero(function.minimum_alignment)));
        output_ << ".p2align " << alignment_power << "\n";
        std::string symbol_error;
        const auto symbol_directives = assembly_symbol_directives(
            format_, {symbol, function.linkage == Linkage::Global, true,
                      entity.weak, assembly_visibility(entity.visibility)},
            symbol_error);
        if (!symbol_directives) {
            diagnostics_.error(function.location, symbol_error);
            return;
        }
        if (!symbol_directives->empty()) {
            output_ << *symbol_directives << '\n';
        }
        if (format_ == ObjectFormat::Elf) {
            output_ << ".type " << symbol << ",@function\n";
        } else if (format_ == ObjectFormat::Coff) {
            output_ << ".def " << symbol << "; .scl "
                    << (function.linkage == Linkage::Global ? "2" : "3")
                    << "; .type 32; .endef\n";
        }
        output_ << symbol << ":\n";
        for (std::size_t layout_index = 0;
             layout_index < function.layout.size(); ++layout_index) {
            const auto id = function.layout[layout_index];
            if (id != function.entry) output_ << block_symbol(function, id) << ":\n";
            const auto& candidate = function.blocks[id.value];
            if (!candidate.source_label.empty()) {
                const auto* label =
                    hir_.label(function.source, candidate.source_label);
                if (label) {
                    output_ << ".Lcross.label." << function.source.value
                            << '.' << label->id.value << ":\n";
                    if (label->is_global) {
                        const auto label_symbol =
                            assembly_symbol(label->link_symbol);
                        output_ << ".globl " << label_symbol << "\n";
                        if (format_ == ObjectFormat::Elf) {
                            output_ << ".type " << label_symbol
                                    << ",@function\n";
                        } else if (format_ == ObjectFormat::Coff) {
                            output_ << ".def " << label_symbol
                                    << "; .scl 2; .type 32; .endef\n";
                        }
                        output_ << label_symbol << ":\n";
                    }
                }
            }
            for (const auto& instruction : candidate.instructions) {
                emit_instruction(function, instruction);
            }
            if (!candidate.instructions.empty() &&
                candidate.instructions.back().form->control ==
                    InstructionControlEffect::ConditionalBranch &&
                candidate.successors.size() == 2) {
                const auto false_target = candidate.successors[1];
                const auto next = layout_index + 1 < function.layout.size()
                                      ? std::optional<mir::BlockId>(
                                            function.layout[layout_index + 1])
                                      : std::nullopt;
                if (!next || *next != false_target) {
                    output_ << "\tjmp\t"
                            << block_symbol(function, false_target) << '\n';
                }
            }
        }
        if (format_ == ObjectFormat::Elf) {
            output_ << ".size " << symbol << ", .-" << symbol << "\n";
        }
        if (entity.retain && format_ == ObjectFormat::Coff &&
            entity.linkage == Linkage::Global) {
            output_ << ".section .drectve\n.ascii \" -include:"
                    << entity.link_symbol << "\"\n";
        } else if (entity.retain && format_ == ObjectFormat::MachO) {
            output_ << ".no_dead_strip " << symbol << '\n';
        }
    }

    void emit_instruction(const mir::RawFunction& function,
                           const mir::Instruction& instruction) {
        output_ << '\t' << instruction.form->assembly_mnemonic;
        if (!instruction.operands.empty() ||
            !instruction.form->assembly_operand_prefix.empty()) {
            output_ << '\t';
        }
        const auto operand = [&](std::size_t index) {
            const auto& value = instruction.operands[index];
            if (value.kind == mir::Operand::Kind::Register) {
                const auto prefix =
                    index < instruction.form->operands.size() &&
                            instruction.form->operands[index].assembly_indirect
                        ? "*%"
                        : "%";
                if (const auto position = x87_position(value.reg.storage)) {
                    return *position == 0
                               ? std::string("%st")
                               : "%st(" + std::to_string(*position) + ')';
                }
                return std::string(prefix) + value.reg.name;
            }
            if (value.kind == mir::Operand::Kind::Memory) {
                const auto displacement = value.memory.displacement == 0
                                              ? std::string{}
                                              : std::to_string(
                                                    value.memory.displacement);
                auto address = displacement + "(%" + value.memory.base.name;
                if (value.memory.index) {
                    address += ",%" + value.memory.index->name + ',' +
                               std::to_string(value.memory.scale);
                }
                address += ')';
                if (instruction.form->assembly_broadcast_operand == index) {
                    address += "{1to" + std::to_string(
                        instruction.form->assembly_broadcast_count) + '}';
                }
                return address;
            }
            if (value.kind == mir::Operand::Kind::Label) {
                return block_symbol(function, value.label.target);
            }
            if (index < instruction.form->operands.size()) {
                const auto& specification = instruction.form->operands[index];
                if (specification.immediate_signed &&
                    static_cast<std::int64_t>(value.immediate.value) < 0) {
                    return "$" + std::to_string(
                        static_cast<std::int64_t>(value.immediate.value));
                }
            }
            return "$" + std::to_string(value.immediate.value);
        };
        // Registry forms use semantic destination-first order.  GNU/LLVM
        // x86 AT&T syntax prints sources before destinations, so reverse the
        // complete list instead of special-casing one- and two-operand forms.
        // This also permits future VEX/EVEX forms with three or more operands.
        bool emitted_operand = false;
        if (!instruction.form->assembly_operand_prefix.empty()) {
            output_ << instruction.form->assembly_operand_prefix;
            emitted_operand = true;
        }
        for (std::size_t index = instruction.operands.size(); index-- > 0;) {
            if (instruction.form->assembly_mask_operand == index) continue;
            if (emitted_operand) output_ << ", ";
            output_ << operand(index);
            emitted_operand = true;
        }
        if (instruction.form->assembly_mask_operand) {
            const auto index = *instruction.form->assembly_mask_operand;
            const auto& mask = instruction.operands.at(index);
            output_ << " {%" << mask.reg.name << '}';
            if (instruction.form->assembly_zeroing) output_ << " {z}";
        }
        output_ << '\n';
        for (const auto& operand_value : instruction.operands) {
            if (operand_value.kind != mir::Operand::Kind::Immediate ||
                !operand_value.immediate.patch ||
                !operand_value.immediate.patch_sink) {
                continue;
            }
            const auto label = ".Lcross.patch." +
                               std::to_string(operand_value.immediate.patch_id) +
                               ".end";
            output_ << label << ":\n";
            patches_.push_back(
                {label, operand_value.immediate.bits / 8,
                 *operand_value.immediate.patch_sink});
        }
    }

    struct PatchEmission {
        std::string end_label;
        unsigned field_bytes{};
        mir::PatchSink sink;
    };

    const mir::RawModule& module_;
    const mir::ManagedModule& managed_;
    const hir::Module& hir_;
    const CompilerOptions& options_;
    Diagnostics& diagnostics_;
    ObjectFormat format_;
    mir::AssemblyBundle bundle_;
    std::ostringstream output_;
    std::vector<PatchEmission> patches_;
};

} // namespace

mir::RawModule lower_raw(const hir::Module& hir_module, const Subtarget& subtarget,
                         Diagnostics& diagnostics) {
    return RawLowerer(hir_module, subtarget, diagnostics).run();
}

mir::AssemblyBundle emit_raw_assembly(const mir::RawModule& mir_module,
                                      const mir::ManagedModule& managed_module,
                                      const hir::Module& hir_module,
                                      const Subtarget& subtarget,
                                      const CompilerOptions& options,
                                      Diagnostics& diagnostics) {
    return RawAssemblyEmitter(mir_module, managed_module, hir_module,
                              subtarget, options, diagnostics).run();
}

} // namespace cross::x86_64
