// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/target.hpp"
#include "target/instruction_constraints.hpp"
#include "target/subtarget.hpp"
#include "common/integer_semantics.hpp"

#include "model/model.hpp"
#include "target/mips/target.hpp"
#include "target/x86_64/target.hpp"

#include <algorithm>

namespace cross {

bool TargetInfo::matches(std::string_view triple) const {
    for (const auto prefix : triple_prefixes) {
        if (triple.starts_with(prefix)) return true;
    }
    return false;
}

std::string_view TargetInfo::default_abi(std::string_view triple) const {
    return model_registry().default_abi(architecture, triple);
}

const std::vector<const TargetInfo*>& all_targets() {
    static const std::vector<const TargetInfo*> targets{
        // Little-endian spellings begin with "mips" too, so keep the more
        // specific variant first.
        &mipsel_target(),
        &mips_target(),
        &x86_64_target(),
    };
    return targets;
}

std::vector<std::string_view> language_features(const CompilerOptions& options) {
    std::vector<std::string_view> result{
        "runtime_free_intrinsics", "control_intrinsics", "evaluation",
        "automatic_evaluation", "generics", "procedural_macros", "syntax_extensions",
        "embedded_assets",
        "patchable_values", "patchable_operands", "raw_inline",
        "contextual_attributes", "external_models", "operator_binding"};
    const auto* target = target_for_triple(options.target);
    if (!target) return result;
    if (std::any_of(target->address_spaces.begin(), target->address_spaces.end(),
                    [](const AddressSpaceEntry& entry) {
                        return entry.number != 0 && entry.native_lowering;
                    }))
        result.push_back("address_spaces");
    for (const auto& feature : target->language_features)
        if (feature.option.empty() || resolved_bool(options, feature.option))
            result.push_back(feature.name);
    return result;
}

const TargetInfo* target_for_triple(std::string_view triple) {
    for (const auto* target : all_targets()) {
        if (target->matches(triple)) return target;
    }
    return nullptr;
}

const AbiEntry* find_abi(const TargetInfo& target, std::string_view name,
                         std::string_view triple) {
    return model_registry().find_abi(target.architecture, name, triple);
}

const AbiEntry* find_abi(const TargetInfo& target, AbiId id) {
    const auto* abi = model_registry().find_abi(id);
    return abi && abi->architecture == target.architecture ? abi : nullptr;
}

const RegisterEntry* find_register(const TargetInfo& target, std::string_view name) {
    for (const auto& entry : target.registers) {
        if (entry.name == name) return &entry;
    }
    return nullptr;
}

const PatchValueMaterializerEntry* find_patch_value_materializer(
    const TargetInfo& target, std::string_view type_name) {
    for (const auto& entry : target.patch_value_materializers) {
        if (entry.type_name == type_name) return &entry;
    }
    return nullptr;
}

const InstructionEntry* find_instruction(const TargetInfo& target, std::string_view name) {
    for (const auto& instruction : target.instructions) {
        if (instruction.name == name) return &instruction;
    }
    return nullptr;
}

bool patch_operand_accepts_type(const InstructionOperandEntry& operand, std::string_view type_name,
                               unsigned source_bits) {
    return operand.patchable && operand.immediate_bits == source_bits &&
        std::find(operand.patch_types.begin(), operand.patch_types.end(), type_name) != operand.patch_types.end();
}

bool instruction_immediate_fits(std::uint64_t value, const InstructionOperandEntry& operand) {
    const auto bits = operand.immediate_bits;
    if (!bits || bits >= 64) return true;
    const auto unsigned_fits = value < (std::uint64_t{1} << bits);
    if (!operand.immediate_signed) return unsigned_fits;
    const auto positive_max = (std::uint64_t{1} << (bits - 1)) - 1;
    const auto negative_min = std::uint64_t{0} - (std::uint64_t{1} << (bits - 1));
    // Full-width integer fields also accept their unsigned bit spelling.
    return value <= positive_max || value >= negative_min ||
        (operand.allow_register && operand.register_bits == bits && unsigned_fits);
}

InstructionAddressSelection select_instruction_address(const TargetInfo& target, const Subtarget& subtarget,
    const EvaluationInstructionOperand::Memory& source) {
    using Shape = EvaluationInstructionOperand::Memory::Shape;
    const auto fail = [](std::string message) { return InstructionAddressSelection{std::move(message)}; };
    if (source.shape == Shape::Other)
        return fail("instruction memory operand requires '*pointer' or 'pointer[index]'");
    if (!source.base.object || !source.base.type || source.base.type->kind != Type::Kind::Pointer)
        return fail("instruction memory base must name a typed pointer object");
    if (source.shape == Shape::Index && !source.index_deferred && !source.constant_index &&
        (!source.index.object || !source.index.integer))
        return fail("instruction memory index requires an integer constant or integer register object");
    const bool index_register = source.shape == Shape::Index && source.index.object;
    InstructionAddressSelection last = fail("selected target has no instruction address mode for this pointer representation");
    for (const auto& mode : target.instruction_address_modes) {
        if (mode.pointer_bits != source.base.bits || !subtarget.supports_registry_feature(mode.feature) ||
            std::find(mode.address_spaces.begin(), mode.address_spaces.end(), source.base.type->address_space) == mode.address_spaces.end()) continue;
        const auto register_matches = [&](const auto& value, bool index) {
            const auto accepts = [&](const RegisterEntry& reg) {
                return reg.address_capable && reg.bits == mode.register_bits &&
                    (mode.register_class.empty() || reg.register_class == mode.register_class) &&
                    subtarget.supports_registry_feature(reg.feature) &&
                    (!index || std::find(mode.forbidden_index_storage.begin(), mode.forbidden_index_storage.end(),
                        reg.storage) == mode.forbidden_index_storage.end());
            };
            if (!value.fixed.empty()) {
                const auto* reg = find_register(target, value.fixed);
                return reg && accepts(*reg);
            }
            return std::any_of(target.registers.begin(), target.registers.end(), accepts);
        };
        if (!register_matches(source.base, false)) {
            last = fail("instruction memory base requires an encodable address register");
            continue;
        }
        if (index_register && (source.index.bits != mode.register_bits || !register_matches(source.index, true))) {
            last = fail("instruction memory index requires an encodable " + std::to_string(mode.register_bits) + "-bit integer register");
            continue;
        }
        if (!source.layout_deferred && (!source.element_bytes || !*source.element_bytes)) {
            last = fail("instruction memory operand requires a complete byte-sized pointee type");
            continue;
        }
        InstructionAddressSelection result;
        result.deferred = source.layout_deferred || source.index_deferred;
        if (index_register && source.element_bytes) {
            if (std::find(mode.index_scales.begin(), mode.index_scales.end(), *source.element_bytes) == mode.index_scales.end()) {
                last = fail("instruction register index has no encodable target scale for this pointee size");
                continue;
            }
            result.scale = static_cast<unsigned>(*source.element_bytes);
        } else if (source.constant_index && source.element_bytes) {
            const auto index = relocation_addend(source.constant_index->value,
                {source.constant_bits, source.constant_signed, false});
            const auto displacement = offset_relocation_addend({}, index, *source.element_bytes, true);
            if (!displacement || !relocation_addend_fits_signed(*displacement, mode.displacement_bits)) {
                last = fail("instruction memory displacement does not fit the selected target address mode");
                continue;
            }
            result.displacement = *displacement;
        }
        return result;
    }
    return last;
}

std::optional<std::string> instruction_source_error(const TargetInfo& target, const Subtarget& subtarget,
    std::string_view name, std::span<const EvaluationInstructionOperand> arguments) {
    const auto forms = find_instruction_forms(target, name);
    const auto prefix = "target instruction '" + std::string(name) + "' ";
    if (forms.empty()) return prefix + "is not registered for the selected target";
    std::optional<std::string> memory_error;
    bool label_scope_error{};
    const auto register_matches = [&](const RegisterEntry& reg, const InstructionOperandEntry& field) {
        return subtarget.supports_registry_feature(reg.feature) && reg.bits == field.register_bits &&
            (field.register_class.empty() || reg.register_class == field.register_class) &&
            (field.register_storage.empty() || reg.storage == field.register_storage) &&
            (field.role == InstructionOperandRole::Input || reg.instruction_writable);
    };
    const auto operand_matches = [&](const EvaluationInstructionOperand& source,
                                     const InstructionOperandEntry& field, EvaluationInstructionFormId id) {
        using Kind = EvaluationInstructionOperand::Kind;
        if (source.kind == Kind::Deferred)
            return field.role == InstructionOperandRole::Input || source.writable;
        if (source.kind == Kind::Patch)
            return std::find(source.patch_forms.begin(), source.patch_forms.end(), id) != source.patch_forms.end();
        if (source.kind == Kind::Label) {
            const bool accepted = instruction_label_accepts(field, source.label_same_function);
            label_scope_error = label_scope_error || (field.allow_label && !accepted);
            return accepted;
        }
        if (source.kind == Kind::Memory) {
            const bool typed = field.allow_memory && (!field.memory_bits || source.deferred || field.memory_bits == source.bits) &&
                (!source.type || !source.type->is_atomic || field.allow_atomic_memory) &&
                (field.role == InstructionOperandRole::Input || source.writable);
            if (!typed) return false;
            const auto selected = select_instruction_address(target, subtarget, source.memory);
            if (!selected.error.empty()) { if (!memory_error) memory_error = selected.error; return false; }
            return true;
        }
        if (source.kind == Kind::Register) {
            if (!field.allow_register || (field.role != InstructionOperandRole::Input && !source.writable)) return false;
            if (field.value_bits && !source.deferred && source.bits != field.value_bits) return false;
            const auto carries_type = [&](const RegisterEntry& reg) {
                if (!source.type || source.deferred) return true;
                if (source.type->is_atomic) return false;
                if (source.type->kind == Type::Kind::Vector)
                    return !source.type->scalable && source.bits == reg.bits && reg.instruction_vector_values;
                return std::any_of(reg.instruction_scalar_modes.begin(), reg.instruction_scalar_modes.end(),
                    [&](const auto& mode) { return mode.bits == source.bits && mode.floating == source.floating; });
            };
            if (!source.fixed_register.empty()) {
                const auto* reg = find_register(target, source.fixed_register);
                return reg && register_matches(*reg, field) && carries_type(*reg);
            }
            if (source.deferred) return true;
            if (!source.type || source.type->is_atomic) return false;
            return std::any_of(target.registers.begin(), target.registers.end(), [&](const RegisterEntry& reg) {
                return register_matches(reg, field) && carries_type(reg);
            });
        }
        if (source.kind != Kind::Immediate || !field.allow_immediate) return false;
        if (source.deferred) return true;
        if (!source.integer) return false;
        const auto value = convert_integer(source.integer->value,
            {source.integer_bits, source.integer_signed, false}, {128, true, false});
        if (value.high && (value.high != UINT64_MAX || value.low < (std::uint64_t{1} << 63))) return false;
        return instruction_immediate_fits(value.low, field);
    };
    unsigned matches{};
    bool arity{};
    std::optional<InstructionFeatureConflict> feature_conflict;
    for (const auto* form : forms) {
        if (form->operands.size() != arguments.size()) continue;
        arity = true;
        const EvaluationInstructionFormId id{static_cast<std::size_t>(form - target.instructions.data())};
        bool match = true;
        for (std::size_t i = 0; i < arguments.size(); ++i)
            if (!operand_matches(arguments[i], form->operands[i], id)) { match = false; break; }
        if (!match) continue;
        const auto conflict = instruction_feature_conflict(*form,
            [&](std::string_view feature) { return subtarget.supports_registry_feature(feature); });
        if (conflict) { if (!feature_conflict) feature_conflict = conflict; }
        else ++matches;
    }
    if (matches == 1) return {};
    if (matches > 1) {
        if (std::any_of(arguments.begin(), arguments.end(), [](const auto& argument) { return argument.deferred; })) return {};
        return prefix + "has ambiguous typed forms for these operands";
    }
    if (feature_conflict)
        return prefix + (feature_conflict->forbidden ? "is unavailable with feature '" : "requires feature '") +
            std::string(feature_conflict->feature) + "'";
    if (!arity) return prefix + "has no form accepting " + std::to_string(arguments.size()) + " operands";
    if (memory_error) return memory_error;
    if (label_scope_error) return prefix + "requires a same-function label operand";
    return "no typed form of " + prefix + "matches these operands";
}

std::vector<const InstructionEntry*> find_instruction_forms(
    const TargetInfo& target, std::string_view name) {
    std::vector<const InstructionEntry*> result;
    for (const auto& instruction : target.instructions) {
        if (instruction.name == name) result.push_back(&instruction);
    }
    return result;
}

bool target_has_instruction(const TargetInfo& target, std::string_view name) {
    return find_instruction(target, name) != nullptr;
}

const AddressSpaceEntry* find_address_space(const TargetInfo& target,
                                            std::uint32_t number) {
    for (const auto& entry : target.address_spaces) {
        if (entry.number == number) return &entry;
    }
    return nullptr;
}

std::optional<std::string> address_space_type_error(const TargetInfo& target,
                                                   std::uint32_t number) {
    const auto* entry = find_address_space(target, number);
    if (entry && entry->native_lowering) return {};
    return "address space " + std::to_string(number) +
        (entry ? " has no native lowering on target '" : " is not registered for target '") +
        std::string(target.architecture) + "'";
}

} // namespace cross
