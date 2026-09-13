// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/x86_64/raw_backend.hpp"

#include "target/assembly_format.hpp"

#include "target/x86_64/features.hpp"
#include "target/x86_64/manual_endpoint.hpp"

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <unordered_set>

namespace cross::x86_64 {
namespace {

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
        label_blocks_.clear();
        laid_out_.clear();
        loops_.clear();
        entry_ordered_depth_ = 0;
        return_ordered_depth_ = 0;
        current_ = {};
        current_.source = function.id;
        current_.location = function.location;
        current_.symbol = function.link_symbol;
        current_.linkage = function.linkage;
        current_.section = function.section;
        current_.entry = new_block(function.location, "entry");
        current_block_ = current_.entry;
        append_layout(current_.entry);
        for (const auto label_id : function.labels) {
            const auto& label = hir_.labels.at(label_id.value);
            label_blocks_.emplace(label_id.value,
                                  new_block(label.location, label.source_name));
        }
        bind_parameters(function);
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
            activate_label(statement);
            return;
        }
        switch (statement.kind) {
        case Statement::Kind::Compound:
            for (const auto& child : statement.statements) lower_statement(*child);
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
            if (require_current(statement.location)) lower_goto(statement);
            return;
        case Statement::Kind::Empty:
            return;
        case Statement::Kind::Return:
            diagnostics_.error(statement.location,
                               "ordinary return is not permitted in a naked function; use an explicit target control-transfer built-in");
            return;
        case Statement::Kind::If:
            lower_if(statement);
            return;
        case Statement::Kind::Switch:
        case Statement::Kind::Case:
        case Statement::Kind::Default:
            diagnostics_.error(statement.location,
                               "switch statements are not permitted in a naked function");
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
            if (loops_.empty()) {
                diagnostics_.error(statement.location,
                                   "raw break is not inside a loop");
            } else if (require_current(statement.location)) {
                (void)terminate_jump(loops_.back().break_target,
                                     statement.location);
            }
            return;
        case Statement::Kind::Continue:
            if (loops_.empty()) {
                diagnostics_.error(statement.location,
                                   "raw continue is not inside a loop");
            } else if (require_current(statement.location)) {
                (void)terminate_jump(loops_.back().continue_target,
                                     statement.location);
            }
            return;
        case Statement::Kind::Label:
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
        binding_types_[declaration.name] = declaration.type;
        binding_signed_[declaration.name] = signed_integer_type(declaration.type);
        if (declaration.type->kind == Type::Kind::Builtin &&
            declaration.type->builtin == BuiltinType::Label) {
            label_bindings_.insert(declaration.name);
        }
        if (!declaration.initializer) return;
        const auto value = integer_literal(*declaration.initializer);
        if (!value) {
            diagnostics_.error(declaration.initializer->location,
                               "raw register initializer must be an integer constant");
            return;
        }
        const auto* form = find_instruction(target_, "$::_movabs");
        if (!form) return;
        mir::Instruction instruction{declaration.location, form, {}};
        instruction.operands.push_back(register_operand(*entry, declaration.location));
        instruction.operands.push_back(immediate_operand(*value, 64,
                                                         declaration.initializer->location));
        append_instruction(std::move(instruction));
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

    const hir::Object* resolve_patch_sink(const Expr& expression) {
        if (!current_function_ || expression.kind != Expr::Kind::Name) {
            diagnostics_.error(
                expression.location,
                "bootstrap $::patch address sink must be one named static-duration uptr object");
            return nullptr;
        }
        const hir::Object* result = nullptr;
        const auto consider = [&](const hir::Object& object) {
            if (object.source_name != expression.text) return;
            if (object.linkage == Linkage::Static &&
                object.source_unit != current_function_->source_unit) {
                return;
            }
            result = &object;
        };
        for (const auto& object : hir_.objects) consider(object);
        if (!result && expression.text.find("::") == std::string::npos) {
            const auto separator = current_function_->source_name.rfind("::");
            if (separator != std::string::npos) {
                const auto qualified =
                    current_function_->source_name.substr(0, separator + 2) +
                    expression.text;
                for (const auto& object : hir_.objects) {
                    if (object.source_name == qualified &&
                        (object.linkage != Linkage::Static ||
                         object.source_unit == current_function_->source_unit)) {
                        result = &object;
                    }
                }
            }
        }
        if (!result) {
            diagnostics_.error(expression.location,
                               "unknown $::patch address sink '" +
                                   expression.text + "'");
            return nullptr;
        }
        const auto& type = hir_.type(result->type);
        if (type.kind != hir::Type::Kind::Builtin ||
            type.builtin != BuiltinType::Uptr || type.is_const ||
            type.is_volatile || type.is_atomic) {
            diagnostics_.error(
                expression.location,
                "$::patch address sink must have unqualified type uptr");
            return nullptr;
        }
        if (!result->definition || result->definition->initializer) {
            diagnostics_.error(
                expression.location,
                "$::patch address sink must be an uninitialized static-duration definition");
            return nullptr;
        }
        if (!patch_sinks_.insert(result->id.value).second) {
            diagnostics_.error(expression.location,
                               "$::patch address sink is used by more than one site");
            return nullptr;
        }
        module_.object_definitions.insert(result->id.value);
        return result;
    }

    std::optional<mir::Operand> lower_operand(const Expr& expression,
                                             const InstructionOperandEntry& specification) {
        const Expr* source = &expression;
        const hir::Object* patch_sink = nullptr;
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
                        operand.immediate.patch_sink = patch_sink->id;
                        operand.immediate.patch_sink_symbol = patch_sink->link_symbol;
                        operand.immediate.patch_sink_linkage = patch_sink->linkage;
                        operand.immediate.patch_sink_section = patch_sink->section;
                    }
                }
                return operand;
            }
        }
        diagnostics_.error(expression.location,
                           "operand does not match the target instruction registry form");
        return std::nullopt;
    }

    void lower_expression_statement(const Expr& expression) {
        if (expression.kind == Expr::Kind::Assign) {
            lower_raw_assignment(expression);
            return;
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

    bool lower_raw_comparison(const Expr& expression,
                              mir::BlockId true_target,
                              mir::BlockId false_target) {
        const Expr* left = &unparenthesized(*expression.left);
        const Expr* right = &unparenthesized(*expression.right);
        auto operation = std::string_view(expression.text);
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
        if (left->kind != Expr::Kind::Name) {
            diagnostics_.error(
                left->location,
                "raw condition comparison requires a hard-bound register on one side");
            return false;
        }
        const auto left_binding = bindings_.find(left->text);
        if (left_binding == bindings_.end() ||
            left_binding->second->register_class != "integer" ||
            left_binding->second->bits != 64) {
            diagnostics_.error(
                left->location,
                "raw structured conditions currently require a 64-bit hard-bound GPR");
            return false;
        }
        const auto* compare = find_instruction(target_, "$::_cmp");
        if (!compare || compare->operands.size() != 2) {
            diagnostics_.error(expression.location,
                               "selected target has no raw integer comparison form");
            return false;
        }
        auto left_operand = lower_operand(*left, compare->operands[0]);
        auto right_operand = lower_operand(*right, compare->operands[1]);
        if (!left_operand || !right_operand) return false;
        mir::Instruction instruction{expression.location, compare, {}};
        instruction.operands.push_back(std::move(*left_operand));
        instruction.operands.push_back(std::move(*right_operand));
        append_instruction(std::move(instruction));

        bool signed_comparison = binding_signed_.at(left->text);
        if (right->kind == Expr::Kind::Name) {
            const auto found = binding_signed_.find(right->text);
            signed_comparison = signed_comparison &&
                                found != binding_signed_.end() && found->second;
        } else if (explicitly_unsigned_literal(*right)) {
            signed_comparison = false;
        }

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
        if (const auto constant = integer_literal(source)) {
            return terminate_jump(*constant != 0 ? true_target : false_target,
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
        if (source.kind != Expr::Kind::Name) {
            diagnostics_.error(
                source.location,
                "raw structured condition must be a constant, hard-bound register, comparison, or short-circuit expression");
            return false;
        }
        const auto found = bindings_.find(source.text);
        if (found == bindings_.end() ||
            found->second->register_class != "integer" ||
            found->second->bits != 64) {
            diagnostics_.error(
                source.location,
                "raw structured conditions currently require a 64-bit hard-bound GPR");
            return false;
        }
        const auto* compare = find_instruction(target_, "$::_cmp");
        if (!compare || compare->operands.size() != 2) return false;
        auto register_value = lower_operand(source, compare->operands[0]);
        auto zero = immediate_operand(0, 32, source.location);
        if (!register_value) return false;
        mir::Instruction instruction{source.location, compare, {}};
        instruction.operands.push_back(std::move(*register_value));
        instruction.operands.push_back(std::move(zero));
        append_instruction(std::move(instruction));
        return terminate_condition("$::_jne", true_target, false_target,
                                   source.location);
    }

    void lower_if(const Statement& statement) {
        if (!statement.condition || !statement.first ||
            !require_current(statement.location)) return;
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

    void lower_while(const Statement& statement) {
        if (!statement.condition || !statement.first ||
            !require_current(statement.location)) return;
        const auto test = new_block(statement.condition->location);
        const auto body = new_block(statement.first->location);
        const auto end = new_block(statement.location);
        (void)terminate_jump(test, statement.location);
        enter_block(test);
        if (!lower_raw_condition(*statement.condition, body, end)) return;
        enter_block(body);
        loops_.push_back({end, test});
        lower_statement(*statement.first);
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
        lower_statement(*statement.first);
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
        lower_statement(*statement.second);
        loops_.pop_back();
        if (current_block_) (void)terminate_jump(increment, statement.location);
        enter_block(increment);
        if (statement.increment) lower_expression_statement(*statement.increment);
        if (current_block_) (void)terminate_jump(test, statement.location);
        enter_block(end);
    }

    void lower_raw_assignment(const Expr& expression) {
        if (expression.text != "=" || !expression.left || !expression.right ||
            expression.left->kind != Expr::Kind::Name) {
            diagnostics_.error(
                expression.location,
                "raw-compatible inlining requires a simple register assignment");
            return;
        }
        const auto destination_found = bindings_.find(expression.left->text);
        if (destination_found == bindings_.end()) {
            diagnostics_.error(expression.left->location,
                               "raw assignment destination is not a hard-bound register");
            return;
        }
        const auto* destination = destination_found->second;
        const auto& source = unparenthesized(*expression.right);
        if (const auto immediate = integer_literal(source)) {
            const auto* form = find_instruction(target_, "$::_movabs");
            if (!form) return;
            mir::Instruction instruction{expression.location, form, {}};
            instruction.operands.push_back(
                register_operand(*destination, expression.left->location));
            instruction.operands.push_back(
                immediate_operand(*immediate, 64, source.location));
            append_instruction(std::move(instruction));
            return;
        }
        if (source.kind != Expr::Kind::Binary || !source.left || !source.right) {
            diagnostics_.error(
                source.location,
                "raw-inline result requires an expression legalizable without "
                "a call, stack slot, or spill");
            return;
        }
        const auto& binary_left = unparenthesized(*source.left);
        if (binary_left.kind != Expr::Kind::Name) {
            diagnostics_.error(
                binary_left.location,
                "raw-inline binary expression requires the destination register "
                "as its left operand");
            return;
        }
        const auto left_found = bindings_.find(binary_left.text);
        if (left_found == bindings_.end() ||
            left_found->second->storage != destination->storage) {
            diagnostics_.error(
                binary_left.location,
                "raw-inline binary expression would require an undeclared "
                "scratch register or spill");
            return;
        }
        std::string_view instruction_name;
        if (source.text == "+") instruction_name = "$::_add";
        else if (source.text == "-") instruction_name = "$::_sub";
        else if (source.text == "^") instruction_name = "$::_xor";
        else if (source.text == "&") instruction_name = "$::_and";
        else if (source.text == "|") instruction_name = "$::_or";
        else if (source.text == "*") instruction_name = "$::_imul";
        else if (source.text == "<<") instruction_name = "$::_shl";
        else if (source.text == ">>") instruction_name = "$::_shr";
        else {
            diagnostics_.error(source.location,
                               "raw-inline operator '" + source.text +
                                   "' has no spill-free x86-64 legalization");
            return;
        }
        const auto* form = find_instruction(target_, instruction_name);
        if (!form) return;
        mir::Instruction instruction{expression.location, form, {}};
        instruction.operands.push_back(
            register_operand(*destination, expression.left->location));
        auto operand = lower_operand(unparenthesized(*source.right),
                                     form->operands[1]);
        if (!operand) {
            diagnostics_.error(
                source.right->location,
                "raw-inline operand requires an unavailable scratch resource");
            return;
        }
        instruction.operands.push_back(std::move(*operand));
        append_instruction(std::move(instruction));
    }

    struct LoopContext {
        mir::BlockId break_target;
        mir::BlockId continue_target;
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
                for (const auto& operand : instruction.operands) {
                    if (operand.kind != mir::Operand::Kind::Register) continue;
                    const auto position = x87_position(operand.reg.storage);
                    if (position && *position >= state.ordered_depth) {
                        diagnostics_.error(
                            operand.location,
                            "raw x87 operand st" + std::to_string(*position) +
                                " is above the current dense stack depth " +
                                std::to_string(state.ordered_depth));
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
                    if (!tracked_resource(resource) ||
                        state.defined_resources.contains(std::string(resource))) continue;
                    const auto key = std::to_string(instruction.location.offset) + ':' +
                                     std::string(resource);
                    if (undefined_resource_diagnostics.insert(key).second) {
                        diagnostics_.error(instruction.location,
                                           "raw instruction reads undefined machine resource '" +
                                               std::string(resource) + "'");
                    }
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
    std::unordered_map<std::uint32_t, mir::BlockId> label_blocks_;
    std::unordered_set<std::uint32_t> laid_out_;
    std::vector<LoopContext> loops_;
    std::unordered_set<std::uint32_t> patch_sinks_;
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
        emit_patch_sinks();
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
                const auto& object = hir_.object(*value.patch_sink);
                if (!bundle_.object_definitions.insert(object.id.value).second) {
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
                     bits / 8, object.link_symbol, object.linkage,
                     object.section});
            }
        }
    }

    void emit_function(const mir::RawFunction& function) {
        if (!safe_assembly_text(function.symbol) ||
            (function.section && !safe_assembly_text(*function.section))) {
            diagnostics_.error(function.location,
                               "raw symbol or section name cannot be represented by the selected assembler");
            return;
        }
        const auto symbol = assembly_symbol(function.symbol);
        const auto patch_function = function_has_patch(function);
        const auto section = function.section
            ? *function.section
            : patch_function
                  ? std::string(format_ == ObjectFormat::Coff
                                    ? ".text$cross.patch."
                                    : ".text.cross.patch.") +
                        std::to_string(function.source.value)
            : options_.function_sections
                  ? std::string(format_ == ObjectFormat::Coff ? ".text$" : ".text.") +
                        function.symbol
                  : std::string(".text");
        std::string section_error;
        const auto directive = assembly_section_directive(
            format_, {section, AssemblySectionKind::Code,
                      function.section.has_value(), false},
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
        output_ << ".p2align " << alignment_power << "\n";
        if (function.linkage == Linkage::Global) output_ << ".globl " << symbol << "\n";
        else if (format_ == ObjectFormat::Elf) output_ << ".local " << symbol << "\n";
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
                 operand_value.immediate.patch_sink_symbol,
                 operand_value.immediate.patch_sink_linkage,
                 operand_value.immediate.patch_sink_section});
        }
    }

    void emit_patch_sinks() {
        for (const auto& patch : patches_) {
            if (!safe_assembly_text(patch.symbol) ||
                (patch.section && !safe_assembly_text(*patch.section))) {
                diagnostics_.error({},
                                   "$::patch sink symbol or section cannot be "
                                   "represented by the selected assembler");
                continue;
            }
            const auto symbol = assembly_symbol(patch.symbol);
            const auto section = patch.section
                ? *patch.section
                : options_.data_sections
                      ? std::string(format_ == ObjectFormat::Coff ? ".data$" : ".data.") +
                            patch.symbol
                      : std::string(".data");
            std::string section_error;
            const auto directive = assembly_section_directive(
                format_, {section, AssemblySectionKind::WritableData,
                          patch.section.has_value(), false},
                section_error);
            if (!directive) {
                diagnostics_.error({}, section_error);
                continue;
            }
            output_ << *directive << '\n';
            output_ << ".p2align 3\n";
            if (patch.linkage == Linkage::Global) output_ << ".globl " << symbol << "\n";
            else if (format_ == ObjectFormat::Elf) output_ << ".local " << symbol << "\n";
            if (format_ == ObjectFormat::Elf) {
                output_ << ".type " << symbol << ",@object\n";
            } else if (format_ == ObjectFormat::Coff) {
                output_ << ".def " << symbol << "; .scl "
                        << (patch.linkage == Linkage::Global ? "2" : "3")
                        << "; .type 0; .endef\n";
            }
            output_ << symbol << ":\n\t.quad " << patch.end_label << '-'
                    << patch.field_bytes << "\n";
            if (format_ == ObjectFormat::Elf) {
                output_ << ".size " << symbol << ", 8\n";
            }
        }
    }

    struct PatchEmission {
        std::string end_label;
        unsigned field_bytes{};
        std::string symbol;
        Linkage linkage{Linkage::Group};
        std::optional<std::string> section;
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
