// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "middle/patch_sink.hpp"

#include "common/diagnostic.hpp"
#include "frontend/ast.hpp"
#include "middle/initializer.hpp"
#include "target/target.hpp"

#include <algorithm>
#include <limits>

namespace cross::mir {
namespace {

struct Selection {
    const hir::Object* object{};
    hir::TypeId type;
    std::uint64_t offset{};
    bool is_const{};
    bool is_volatile{};
    bool is_atomic{};
    bool bit_field{};
};

std::optional<Selection> select(
    const Expr& expression, const hir::Module& module,
    const TargetInfo& target, const PatchSinkObjectResolver& resolve_object,
    Diagnostics& diagnostics) {
    if (expression.kind == Expr::Kind::Parenthesized && expression.left) {
        return select(*expression.left, module, target, resolve_object,
                      diagnostics);
    }
    if (expression.kind == Expr::Kind::Name) {
        const auto* object = resolve_object(expression);
        if (!object) {
            diagnostics.error(expression.location,
                              "unknown $::patch address sink '" +
                                  expression.text + "'");
            return std::nullopt;
        }
        const auto& type = module.type(object->type);
        return Selection{object, object->type, 0, type.is_const,
                         type.is_volatile, type.is_atomic, false};
    }
    if (expression.kind != Expr::Kind::Binary || !expression.left ||
        !expression.right ||
        (expression.text != "member" && expression.text != "index")) {
        diagnostics.error(
            expression.location,
            "$::patch address sink must be a static object followed only by direct member or constant array selections");
        return std::nullopt;
    }

    auto base = select(*expression.left, module, target, resolve_object,
                       diagnostics);
    if (!base) return std::nullopt;
    const auto& owner = module.type(base->type);
    if (expression.text == "member") {
        if (expression.right->kind != Expr::Kind::Name ||
            owner.kind != hir::Type::Kind::Record || !owner.record) {
            diagnostics.error(
                expression.location,
                "$::patch member sink requires a direct record member selection");
            return std::nullopt;
        }
        const auto* member = module.member(*owner.record,
                                           member_name(*expression.right));
        if (!member) {
            diagnostics.error(
                expression.right->location,
                "record has no $::patch sink member named '" +
                    expression.right->text + "'");
            return std::nullopt;
        }
        if (member->offset >
            std::numeric_limits<std::uint64_t>::max() - base->offset) {
            diagnostics.error(expression.location,
                              "$::patch address sink offset overflows target storage");
            return std::nullopt;
        }
        const auto& selected = module.type(member->type);
        base->type = member->type;
        base->offset += member->offset;
        base->is_const = base->is_const || selected.is_const;
        base->is_volatile = base->is_volatile || selected.is_volatile;
        base->is_atomic = base->is_atomic || selected.is_atomic;
        base->bit_field = member->bit_width.has_value();
        return base;
    }

    if (owner.kind != hir::Type::Kind::Array || !owner.element) {
        diagnostics.error(expression.location,
                          "$::patch array sink requires a direct array object");
        return std::nullopt;
    }
    const Expr* index = expression.right.get();
    while (index->kind == Expr::Kind::Parenthesized && index->left) {
        index = index->left.get();
    }
    if (!index->evaluated_integer ||
        index->evaluated_integer->value.high != 0) {
        diagnostics.error(
            expression.right->location,
            "$::patch array sink requires a nonnegative integer constant index");
        return std::nullopt;
    }
    const auto selected = index->evaluated_integer->value.low;
    if (owner.lanes == 0 || selected >= owner.lanes) {
        diagnostics.error(expression.right->location,
                          "$::patch array sink index is out of range");
        return std::nullopt;
    }
    const auto element_size = hir::layout_size(module, *owner.element, target);
    if (!element_size ||
        selected > std::numeric_limits<std::uint64_t>::max() / *element_size ||
        selected * *element_size >
            std::numeric_limits<std::uint64_t>::max() - base->offset) {
        diagnostics.error(expression.location,
                          "$::patch address sink offset overflows target storage");
        return std::nullopt;
    }
    const auto& element = module.type(*owner.element);
    base->type = *owner.element;
    base->offset += selected * *element_size;
    base->is_const = base->is_const || element.is_const;
    base->is_volatile = base->is_volatile || element.is_volatile;
    base->is_atomic = base->is_atomic || element.is_atomic;
    return base;
}

bool explicitly_initialized(const Selection& sink, const hir::Module& module,
                            const TargetInfo& target,
                            Diagnostics& diagnostics) {
    const auto* declaration = sink.object->definition;
    if (!declaration || !declaration->initializer) return false;
    const auto& root = module.type(sink.object->type);
    if (root.kind != hir::Type::Kind::Array &&
        root.kind != hir::Type::Kind::Record) {
        return true;
    }
    if (declaration->initializer->kind !=
        Expr::Kind::AggregateInitializer) {
        return true;
    }
    const auto plan = initializer::build(
        *declaration->initializer, sink.object->type, module, target,
        diagnostics);
    if (!plan.valid) return true;
    const auto sink_size = hir::layout_size(module, sink.type, target);
    if (!sink_size) return true;
    return std::any_of(
        plan.items.begin(), plan.items.end(),
        [&](const initializer::Item& item) {
            const auto item_size = hir::layout_size(module, item.type, target);
            if (!item_size) return true;
            return item.offset < sink.offset + *sink_size &&
                   sink.offset < item.offset + *item_size;
        });
}

} // namespace

std::optional<PatchSink> resolve_patch_sink_designator(
    const Expr& expression, const hir::Module& module,
    const TargetInfo& target, PatchAddressRepresentation representation,
    const PatchSinkObjectResolver& resolve_object, Diagnostics& diagnostics) {
    if (representation != PatchAddressRepresentation::FlatUptr) {
        diagnostics.error(expression.location,
                          "selected target patch entry has no supported address-sink representation");
        return std::nullopt;
    }
    const auto selected = select(expression, module, target, resolve_object,
                                 diagnostics);
    if (!selected) return std::nullopt;
    const auto& type = module.type(selected->type);
    if (type.kind != hir::Type::Kind::Builtin ||
        type.builtin != BuiltinType::Uptr || type.nominal_key() != NominalTypeKey{} ||
        type.is_restrict || selected->is_const ||
        selected->is_volatile || selected->is_atomic ||
        selected->bit_field) {
        diagnostics.error(
            expression.location,
            "$::patch address sink must designate a complete unqualified non-atomic uptr subobject");
        return std::nullopt;
    }
    if (selected->object->is_thread_local) {
        diagnostics.error(
            expression.location,
            "$::patch address sink cannot designate thread-local storage");
        return std::nullopt;
    }
    const auto object_size =
        hir::layout_size(module, selected->object->type, target);
    const auto sink_size = hir::layout_size(module, selected->type, target);
    const auto storage_bytes = patch_address_storage_bytes(representation, module.address_bits);
    if (!sink_size || !storage_bytes || *sink_size != storage_bytes) {
        diagnostics.error(expression.location,
                          "target has no $::patch sink relocation for this address representation");
        return std::nullopt;
    }
    if (!object_size || selected->offset > *object_size ||
        *sink_size > *object_size - selected->offset) {
        diagnostics.error(expression.location,
                          "$::patch address sink exceeds its static object");
        return std::nullopt;
    }
    if (!selected->object->definition ||
        explicitly_initialized(*selected, module, target, diagnostics)) {
        diagnostics.error(
            expression.location,
            "$::patch address sink must designate an uninitialized static-duration subobject");
        return std::nullopt;
    }
    return PatchSink{selected->object->id, selected->offset, representation,
                     static_cast<unsigned>(*sink_size)};
}

} // namespace cross::mir
