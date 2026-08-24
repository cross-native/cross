// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/target.hpp"

#include "model/model.hpp"
#include "target/x86_64/target.hpp"

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
        &x86_64_target(),
    };
    return targets;
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

} // namespace cross
