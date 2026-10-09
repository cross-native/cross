// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "middle/codegen_module.hpp"

#include "common/diagnostic.hpp"

#include <algorithm>
#include <set>
#include <unordered_set>

namespace cross::codegen {

const mir::ManagedFunction* ModuleView::managed_body(hir::FunctionId id) const {
    return managed_.find(id);
}

const mir::RawFunction* ModuleView::raw_body(hir::FunctionId id) const {
    const auto found = std::find_if(
        raw_.functions.begin(), raw_.functions.end(),
        [id](const mir::RawFunction& function) { return function.source == id; });
    return found == raw_.functions.end() ? nullptr : &*found;
}

FunctionBodyKind ModuleView::body_kind(hir::FunctionId id) const {
    const auto& function = hir_.function(id);
    if (!function.definition) return FunctionBodyKind::Declaration;
    if (managed_body(id)) return FunctionBodyKind::ManagedMir;
    if (raw_body(id)) return FunctionBodyKind::RawMir;
    return FunctionBodyKind::Unlowered;
}

bool ModuleView::fully_lowered() const {
    return std::all_of(
        hir_.functions.begin(), hir_.functions.end(),
        [this](const hir::Function& function) {
            return body_kind(function.id) != FunctionBodyKind::Unlowered;
        });
}

bool verify(const ModuleView& module, Diagnostics& diagnostics) {
    bool valid = true;
    std::unordered_set<std::uint32_t> managed_ids;
    std::unordered_set<std::uint32_t> raw_ids;
    std::unordered_set<std::uint32_t> data_ids;

    for (const auto& object : module.data().objects) {
        if (object.source.value >= module.hir().objects.size()) {
            diagnostics.error(object.location,
                              "data IR refers to an unknown HIR object");
            valid = false;
            continue;
        }
        if (!data_ids.insert(object.source.value).second) {
            diagnostics.error(object.location,
                              "data IR contains a duplicate object");
            valid = false;
        }
        const auto& entity = module.hir().object(object.source);
        if (entity.type != object.type || object.size == 0 ||
            object.alignment == 0 ||
            (object.alignment & (object.alignment - 1)) != 0) {
            diagnostics.error(object.location,
                              "data IR disagrees with HIR object layout");
            valid = false;
        }
        const auto& type = module.hir().type(object.type);
        const bool integer =
            type.kind == hir::Type::Kind::Builtin &&
            type.builtin >= BuiltinType::Bool &&
            type.builtin <= BuiltinType::Uptr;
        const bool floating =
            type.kind == hir::Type::Kind::Builtin &&
            type.builtin >= BuiltinType::F32 &&
            type.builtin <= BuiltinType::Fptr;
        const bool numeric_label = type.kind == hir::Type::Kind::Builtin &&
            type.builtin == BuiltinType::Label;
        const bool address =
            type.kind == hir::Type::Kind::Pointer ||
            (type.kind == hir::Type::Kind::Builtin &&
             (type.builtin == BuiltinType::Label ||
              (integer && type.builtin != BuiltinType::Bool &&
               object.size * 8 == module.hir().address_bits)));
        const bool byte_array =
            type.kind == hir::Type::Kind::Array && type.element &&
            module.hir().type(*type.element).kind == hir::Type::Kind::Builtin &&
            module.hir().type(*type.element).builtin == BuiltinType::U8;
        // A scalar padded to its requested alignment is stored as an image.
        const bool aggregate = type.kind == hir::Type::Kind::Array ||
                               type.kind == hir::Type::Kind::Record ||
                               (type.kind == hir::Type::Kind::Vector && !type.scalable) ||
                               type.alignment != 0;
        const bool declaration =
            object.initializer == data::InitializerKind::Declaration;
        if (declaration != (entity.definition == nullptr) ||
            (object.initializer == data::InitializerKind::Integer &&
             !integer && !numeric_label && type.kind != hir::Type::Kind::Pointer) ||
            (object.initializer == data::InitializerKind::Floating &&
             !floating) ||
            (object.initializer == data::InitializerKind::Address &&
             !address) ||
            (object.initializer == data::InitializerKind::Bytes &&
             (!byte_array || object.bytes.size() != object.size)) ||
            (object.initializer == data::InitializerKind::Aggregate &&
             (!aggregate || object.bytes.size() != object.size)) ||
            (object.initializer != data::InitializerKind::Address &&
             object.address) ||
            (object.initializer != data::InitializerKind::Bytes &&
             object.initializer != data::InitializerKind::Aggregate &&
             !object.bytes.empty()) ||
            (object.initializer != data::InitializerKind::Aggregate &&
             !object.relocations.empty())) {
            diagnostics.error(object.location,
                              "data IR initializer disagrees with HIR object");
            valid = false;
        }
        if (object.initializer == data::InitializerKind::Address) {
            if (!object.address) {
                diagnostics.error(object.location,
                                  "data IR address initializer has no target");
                valid = false;
            } else if (object.address->kind == data::AddressKind::Object &&
                       (!object.address->object || object.address->function ||
                        object.address->label ||
                        object.address->object->value >=
                            module.hir().objects.size())) {
                diagnostics.error(object.location,
                                  "data IR refers to an unknown object address");
                valid = false;
            } else if (object.address->kind == data::AddressKind::Function &&
                       (!object.address->function || object.address->object ||
                        object.address->label ||
                        object.address->function->value >=
                            module.hir().functions.size())) {
                diagnostics.error(object.location,
                                  "data IR refers to an unknown function address");
                valid = false;
            } else if (object.address->kind == data::AddressKind::Label &&
                       (!object.address->function || !object.address->label ||
                        object.address->object ||
                        object.address->function->value >=
                            module.hir().functions.size() ||
                        object.address->label->value >=
                            module.hir().labels.size() ||
                        module.hir().labels[object.address->label->value].owner !=
                            *object.address->function)) {
                diagnostics.error(object.location,
                                  "data IR refers to an unknown label address");
                valid = false;
            }
        }
        for (const auto& relocation : object.relocations) {
            if ((relocation.size != 4 && relocation.size != 8) ||
                relocation.offset > object.size ||
                relocation.size > object.size - relocation.offset) {
                diagnostics.error(object.location,
                                  "data IR aggregate relocation is out of range");
                valid = false;
            }
            const auto& target_address = relocation.address;
            if (target_address.kind == data::AddressKind::Object &&
                (!target_address.object || target_address.function ||
                 target_address.label ||
                 target_address.object->value >= module.hir().objects.size())) {
                diagnostics.error(object.location,
                                  "data IR refers to an unknown object address");
                valid = false;
            } else if (target_address.kind == data::AddressKind::Function &&
                       (!target_address.function || target_address.object ||
                        target_address.label ||
                        target_address.function->value >=
                            module.hir().functions.size())) {
                diagnostics.error(object.location,
                                  "data IR refers to an unknown function address");
                valid = false;
            } else if (target_address.kind == data::AddressKind::Label &&
                       (!target_address.function || !target_address.label ||
                        target_address.object ||
                        target_address.function->value >=
                            module.hir().functions.size() ||
                        target_address.label->value >=
                            module.hir().labels.size() ||
                        module.hir()
                                .labels[target_address.label->value]
                                .owner != *target_address.function)) {
                diagnostics.error(object.location,
                                  "data IR refers to an unknown label address");
                valid = false;
            }
        }
    }
    for (const auto& entity : module.hir().objects) {
        if (!data_ids.contains(entity.id.value)) {
            diagnostics.error(entity.location, "HIR object has no data IR entry");
            valid = false;
        }
    }

    for (const auto& function : module.managed().functions) {
        if (function.source.value >= module.hir().functions.size()) {
            diagnostics.error(function.location,
                              "managed MIR refers to an unknown HIR function");
            valid = false;
            continue;
        }
        if (!managed_ids.insert(function.source.value).second) {
            diagnostics.error(function.location,
                              "managed MIR contains a duplicate function body");
            valid = false;
        }
        const auto& entity = module.hir().function(function.source);
        if (entity.ownership != hir::BodyOwnership::ManagedMir ||
            !module.managed().owns(function.source)) {
            diagnostics.error(function.location,
                              "managed MIR disagrees with HIR body ownership");
            valid = false;
        }
    }

    for (const auto& function : module.raw().functions) {
        if (function.source.value >= module.hir().functions.size()) {
            diagnostics.error(function.location,
                              "raw MIR refers to an unknown HIR function");
            valid = false;
            continue;
        }
        if (!raw_ids.insert(function.source.value).second) {
            diagnostics.error(function.location,
                              "raw MIR contains a duplicate function body");
            valid = false;
        }
        const auto& entity = module.hir().function(function.source);
        if (entity.ownership != hir::BodyOwnership::RawMir ||
            !module.raw().owns(function.source) ||
            !module.raw_assembly().owns(function.source)) {
            diagnostics.error(function.location,
                              "raw MIR disagrees with HIR or assembly ownership");
            valid = false;
        }
        if (managed_ids.contains(function.source.value)) {
            diagnostics.error(function.location,
                              "function body is owned by both managed and raw MIR");
            valid = false;
        }
    }

    for (const auto id : module.managed().definitions) {
        if (!managed_ids.contains(id)) {
            diagnostics.command_error(
                "managed MIR ownership set contains no matching function");
            valid = false;
        }
    }
    for (const auto id : module.raw().definitions) {
        if (!raw_ids.contains(id)) {
            diagnostics.command_error(
                "raw MIR ownership set contains no matching function");
            valid = false;
        }
    }

    for (const auto& entity : module.hir().functions) {
        const bool has_managed = managed_ids.contains(entity.id.value);
        const bool has_raw = raw_ids.contains(entity.id.value);
        if (entity.ownership == hir::BodyOwnership::ManagedMir &&
            !has_managed) {
            diagnostics.error(entity.location,
                              "HIR claims a missing managed MIR body");
            valid = false;
        }
        if (entity.ownership == hir::BodyOwnership::RawMir && !has_raw) {
            diagnostics.error(entity.location, "HIR claims a missing raw MIR body");
            valid = false;
        }
        if ((entity.ownership == hir::BodyOwnership::None ||
             entity.ownership == hir::BodyOwnership::ManagedAst) &&
            (has_managed || has_raw)) {
            diagnostics.error(entity.location,
                              "lowered function body has no matching HIR owner");
            valid = false;
        }
    }

    for (const auto id : module.raw_assembly().definitions) {
        if (!raw_ids.contains(id)) {
            diagnostics.command_error(
                "raw assembly ownership set contains no matching raw MIR body");
            valid = false;
        }
    }
    for (const auto id : module.raw_assembly().object_definitions) {
        if (id >= module.hir().objects.size() || !data_ids.contains(id)) {
            diagnostics.command_error(
                "raw assembly owns an unknown data IR object");
            valid = false;
        }
    }
    std::set<std::pair<std::uint32_t, std::uint64_t>> patch_sinks;
    const auto patch_address_size =
        (module.hir().address_bits + 7U) / 8U;
    for (const auto& relocation :
         module.raw_assembly().patch_relocations) {
        const auto object_id = relocation.sink.object.value;
        const auto* object = module.data().find(relocation.sink.object);
        if (!object ||
            !module.raw_assembly().object_definitions.contains(object_id) ||
            relocation.end_label.empty() || relocation.field_bytes == 0 ||
            relocation.sink.offset > object->size ||
            patch_address_size > object->size - relocation.sink.offset) {
            diagnostics.command_error(
                "raw assembly contains an invalid patch sink relocation");
            valid = false;
            continue;
        }
        if (!patch_sinks.emplace(object_id, relocation.sink.offset).second) {
            diagnostics.command_error(
                "raw assembly contains a duplicate patch sink relocation");
            valid = false;
        }
    }
    return valid;
}

std::optional<SourceLocation> requested_alignment_location(const ModuleView& module) {
    const auto& hir = module.hir();
    const auto requests = [&](hir::TypeId id) {
        for (;;) {
            const auto& type = hir.type(id);
            if (type.alignment) return true;
            if (type.pointee) id = *type.pointee;
            else if (type.element) id = *type.element;
            else return false;
        }
    };
    for (const auto& object : hir.objects)
        if (requests(object.type)) return object.location;
    for (const auto& function : module.managed().functions) {
        for (const auto& slot : function.slots)
            if (requests(slot.type)) return slot.location;
        for (const auto& value : function.values)
            if (requests(value.type)) return value.location;
    }
    return std::nullopt;
}

} // namespace cross::codegen
