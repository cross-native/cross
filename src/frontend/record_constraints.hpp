// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend/ast.hpp"
#include "frontend/vector_constraints.hpp"

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cross {

struct RecordSourceError {
    SourceLocation location;
    std::string message;
};

inline const char* bit_field_width_error(const Expr::IntegerConstant& width,
    unsigned address_bits, std::uint64_t storage_bits, bool named) {
    const auto type = width.type;
    const bool signed_width = type == BuiltinType::I8 || type == BuiltinType::I16 ||
        type == BuiltinType::I32 || type == BuiltinType::I64 ||
        type == BuiltinType::I128 || type == BuiltinType::Iptr;
    const auto bits = type == BuiltinType::Iptr || type == BuiltinType::Uptr
        ? address_bits : type_bits(builtin_type(type));
    if (signed_width && bits && (shift_right(width.value, bits - 1).low & 1U))
        return "bit-field width must be nonnegative";
    if (width.value.high || width.value.low > storage_bits)
        return "bit-field width exceeds its base type";
    if (!width.value.low && named) return "a zero-width bit-field must be unnamed";
    return nullptr;
}

// Definitions whose whole by-value closure passed record_source_error. While it
// holds them, the owner edits declarations in place only in ways that cannot
// change that result: it resolves extents and rewrites bound and width
// expressions and attributes. A proof is keyed by the definition object, its
// member vector and the evaluation layout scope it was made in, so a
// substituted private view or a replaced declaration is checked again. It
// retains a private view so the address is not reused, and source publication
// drops every proof.
class RecordSourceProofs {
public:
    bool contains(const Program& program, const RecordDecl& record) {
        refresh(program);
        const auto found = proofs_.find(&record);
        return found != proofs_.end() && found->second.members == record.members.data() &&
            found->second.count == record.members.size() &&
            found->second.scope == program.evaluation_layout_scope;
    }
    void insert(const Program& program, std::shared_ptr<const RecordDecl> record) {
        refresh(program);
        const auto* key = record.get();
        proofs_[key] = {record->members.data(), record->members.size(),
            program.evaluation_layout_scope, std::move(record)};
    }
private:
    struct Proof {
        const RecordMemberDecl* members{};
        std::size_t count{};
        std::shared_ptr<const EvaluationLayoutScopeIdentity> scope;
        std::shared_ptr<const RecordDecl> owner;
    };
    void refresh(const Program& program) {
        if (program_ == &program && records_ == program.records.data() && count_ == program.records.size())
            return;
        proofs_.clear();
        program_ = &program;
        records_ = program.records.data();
        count_ = program.records.size();
    }
    const Program* program_{};
    const RecordDecl* records_{};
    std::size_t count_{};
    std::unordered_map<const RecordDecl*, Proof> proofs_;
};

// Structural record constraints do not request target layout or evaluate a
// bound, width or attribute. A deferred value must not hide an already-invalid
// member. Nominal value edges are followed, but pointers do not require a
// complete pointee; generic leaves remain obligations of their instantiation.
// Every traversal selects the current scoped owner and checks its members;
// RecordSourceProofs is the only owner of successful results.
inline std::optional<RecordSourceError> record_source_error(const RecordDecl& source, const Program& program,
    RecordSourceIndex& index, RecordSourceProofs* proofs = nullptr) {
    if (!source.complete || (proofs && proofs->contains(program, source))) return {};
    // Keep private views alive while traversing nominal value edges. A view
    // changes preparation state, never the identity used to detect cycles.
    std::vector<std::shared_ptr<const RecordDecl>> views;
    struct Frame {
        std::shared_ptr<const RecordDecl> record;
        std::size_t next{};
        std::unordered_set<MemberName, MemberNameHash> names;
    };
    // The caller owns the root, which is therefore not recorded as a proof.
    std::vector<Frame> frames;
    frames.push_back({{std::shared_ptr<const RecordDecl>{}, &source}, 0, {}});
    std::unordered_set<NominalTypeKey, NominalTypeKeyHash> active{source.nominal_key()}, complete;
    std::vector<TypePtr> pending;
    std::unordered_set<const Type*> seen;
    while (!frames.empty()) {
        auto& frame = frames.back();
        const auto& record = *frame.record;
        if (record.members.empty())
            return RecordSourceError{record.location, "a complete record requires at least one member"};
        if (frame.next == record.members.size()) {
            auto key = record.nominal_key();
            active.erase(key);
            complete.insert(std::move(key));
            if (proofs && frames.size() > 1) proofs->insert(program, std::move(frame.record));
            frames.pop_back();
            continue;
        }
        const auto& member = record.members[frame.next++];
        const auto error = [&](std::string message) -> std::optional<RecordSourceError> {
            return RecordSourceError{member.location, std::move(message)};
        };
        if (!member.name.empty() && !frame.names.insert(member.member_name()).second)
            return error("duplicate record member '" + member.name + "'");
        pending.assign(1, member.type);
        seen.clear();
        while (!pending.empty()) {
            auto type = std::move(pending.back());
            pending.pop_back();
            if (!type || !seen.insert(type.get()).second) continue;
            if (is_meta_type(type)) return error("meta values cannot be record members");
            if (type->is_atomic && (type->kind == Type::Kind::Array || type->kind == Type::Kind::Vector ||
                type->kind == Type::Kind::Record || (type->kind == Type::Kind::Builtin &&
                    (type->builtin == BuiltinType::Void || type->builtin == BuiltinType::Label))))
                return error("atomic qualifier requires an integer, floating, or pointer object type");
            if ((type->kind == Type::Kind::Array || type->kind == Type::Kind::Vector) &&
                type->element && type->element->is_atomic)
                return error(type->kind == Type::Kind::Array ? "an array element type cannot be atomic-qualified"
                                                           : "a vector element type cannot be atomic-qualified");
            if (type->kind == Type::Kind::Vector)
                if (const auto* reason = vector_element_error(type->element)) return error(reason);
            pending.push_back(type->element);
            pending.push_back(type->pointee);
            if (type->function) {
                pending.push_back(type->function->result);
                for (const auto& parameter : type->function->parameters) pending.push_back(parameter.type);
            }
        }
        if (member.bit_width) {
            if (!member.type || (!is_integer(member.type) && member.type->kind != Type::Kind::Generic))
                return error("bit-field base type must be bool, an integer, or an enumeration");
            if (member.type->is_atomic) return error("a bit-field cannot have atomic type");
        }
        auto type = member.type;
        seen.clear();
        while (type && (type->kind == Type::Kind::Array || type->kind == Type::Kind::Vector)) {
            if (!seen.insert(type.get()).second)
                return error("record contains itself by value through a member cycle");
            if (type->kind == Type::Kind::Array) {
                if (!type->element || (!type->lanes && !type->array_bound))
                    return error("record member cannot have variable-length or incomplete array type");
            } else if (type->scalable || !type->element || (!type->lanes && !type->vector_bound)) {
                return error("record member cannot have scalable or incomplete vector type");
            }
            type = type->element;
        }
        if (!type || (type->kind == Type::Kind::Builtin && type->builtin == BuiltinType::Void) ||
            type->kind == Type::Kind::Function)
            return error("record member has an incomplete or non-object type");
        if (type->kind != Type::Kind::Record) continue;
        const auto key = type->nominal_key();
        if (active.contains(key)) return error("record contains itself by value through a member cycle");
        if (complete.contains(key)) continue;
        std::shared_ptr<const RecordDecl> definition{std::shared_ptr<const RecordDecl>{},
            index.definition(program, key)};
        if (!definition)
            return error("incomplete record type '" + key.name + "' cannot be used as an object or member");
        if (program.evaluation_record_definition) {
            const auto query = program.evaluation_record_definition;
            auto view = query(key);
            if (view) {
                definition = view;
                views.push_back(std::move(view));
            }
        }
        // A proven closure has no cycle and so cannot reach an active record.
        if (proofs && proofs->contains(program, *definition)) continue;
        active.insert(key);
        frames.push_back({std::move(definition), 0, {}});
    }
    return {};
}

inline std::optional<RecordSourceError> record_source_error(const RecordDecl& source, const Program& program) {
    RecordSourceIndex index;
    return record_source_error(source, program, index);
}

} // namespace cross
