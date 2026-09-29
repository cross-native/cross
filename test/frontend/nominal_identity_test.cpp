// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic.hpp"
#include "middle/hir.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>
#include <source_location>
#include <unordered_set>

int main() {
    using namespace cross;
    std::ostringstream output;
    Diagnostics diagnostics(output);
    const auto require = [&](bool condition,
                             std::source_location where = std::source_location::current()) {
        if (!condition) {
            std::cerr << "nominal identity check failed at line " << where.line()
                      << '\n' << output.str();
            std::abort();
        }
    };
    SourceManager sources;
    const auto* source = sources.add("nominal_identity.x", R"(
        struct First { u16 value; };
        struct Second { u32 value; };
        enum FirstEnum [[underlying(u16)]] { FirstValue = 5u16 };
        enum SecondEnum [[underlying(u16)]] { SecondValue = 9u16 };
        static T identity<T>(in T value) { return value; }
        static u32 enum_values() {
            return (u32)identity::<enum FirstEnum>(FirstValue) +
                   (u32)identity::<enum SecondEnum>(SecondValue);
        }
        global u32 run() {
            struct First first;
            first.value = 11u16;
            struct Second second;
            second.value = 65549u32;
            struct First a = identity::<struct First>(first);
            struct Second b = identity::<struct Second>(second);
            return (u32)a.value + b.value;
        }
        $::static_assert(run() == 65560u32, "private nominal member lookup");
        $::static_assert(enum_values() == 14u32, "private enum generic lookup");
    )");
    const auto identity = [&](std::uint64_t ordinal) {
        return std::make_shared<const NominalTypeIdentity>(NominalTypeIdentity{
            TokenIdentity{source, 10, {}, 0}, "nominal_identity.x", ordinal});
    };
    const auto first_identity = identity(1);
    const auto second_identity = identity(2);
    const auto first_enum_identity = identity(3);
    const auto second_enum_identity = identity(4);
    auto first = record_type("Tag");
    auto second = record_type("Tag");
    first->nominal_identity = first_identity;
    second->nominal_identity = second_identity;
    auto global = record_type("Tag");
    require(!same_type(first, second) && !same_type(first, global));
    require(type_name(first) == type_name(second));
    require(canonical_type_name(first) != canonical_type_name(second));
    require(canonical_type_name(first) != canonical_type_name(global));
    auto renamed = copy_type(first);
    renamed->nominal_name = "Moved::Tag";
    require(same_type(first, renamed));
    require(canonical_type_name(first) == canonical_type_name(renamed));
    require(NominalTypeKeyHash{}(first->nominal_key()) ==
            NominalTypeKeyHash{}(renamed->nominal_key()));

    auto enumeration = enum_type("", BuiltinType::U16);
    enumeration->nominal_identity = first_identity;
    require(!same_type(enumeration, builtin_type(BuiltinType::U16)));
    auto other_enum = copy_type(enumeration);
    other_enum->nominal_identity = second_identity;
    require(!same_type(enumeration, other_enum));
    require(canonical_type_name(enumeration) != canonical_type_name(other_enum));

    hir::Module interned;
    const auto first_id = interned.intern_type(first);
    const auto second_id = interned.intern_type(second);
    require(first_id != second_id);
    require(first_id == interned.intern_type(renamed));
    require(first_id != interned.intern_type(global));
    require(interned.record(first->nominal_key())->id !=
            interned.record(second->nominal_key())->id);
    for (auto type : {first, second, enumeration, other_enum}) {
        const auto original = interned.intern_type(type);
        auto qualified = copy_type(type);
        qualified->is_const = true;
        const auto with_const = interned.intern_type(qualified);
        require(interned.without_top_level_const(with_const) == original);
        require(interned.unqualified(with_const) == original);
        require(interned.add_qualifiers(original, true, false) == with_const);
        auto callable = function_type(type, {});
        auto copied = copy_type(callable);
        require(same_type(callable, copied));
        require(copied->function->result->nominal_identity == type->nominal_identity);
        require(interned.intern_type(callable) == interned.intern_type(copied));
    }

    // Until scoped parser bindings allocate these identities, deliberately
    // rename two parsed nominal declarations to the same display name. Every
    // downstream operation must select the retained identity, not that name.
    for (const auto& [triple, abi] : {
            std::pair{"x86_64-unknown-linux-gnu", "sysv_abi"},
            std::pair{"mips-unknown-elf", "o32"},
            std::pair{"mipsel-unknown-elf", "o32"},
            std::pair{"mips64-unknown-elf", "n64"}}) {
        CompilerOptions options;
        options.target = triple;
        options.abi = abi;
        const auto* target = target_for_triple(triple);
        require(target != nullptr);
        const auto* model = find_abi(*target, abi, triple);
        require(model != nullptr);
        Parser parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, model->address_bits);
        auto program = parser.parse();
        require(diagnostics.errors() == 0);
        program.address_bits = model->address_bits;
        program.evaluation_layout = {
            target->data_layout.byte_order == ByteOrder::Big
                ? EvaluationByteOrder::Big : EvaluationByteOrder::Little,
            target->data_layout.natural_alignment_limit};
        std::unordered_set<Type*> visited;
        const auto rebind = [&](auto& item, const std::string& name) {
            item.nominal_identity = name == "First" ? first_identity :
                name == "Second" ? second_identity :
                name == "FirstEnum" ? first_enum_identity : second_enum_identity;
        };
        const auto types = [&](const auto& self, const TypePtr& type) -> void {
            if (!type || !visited.insert(type.get()).second) return;
            if (!type->nominal_key().empty()) {
                rebind(*type, type->nominal_name);
                type->nominal_name = type->kind == Type::Kind::Record ? "Tag" : "EnumTag";
            }
            self(self, type->pointee);
            self(self, type->element);
            if (type->function) {
                self(self, type->function->result);
                for (auto& parameter : type->function->parameters) self(self, parameter.type);
            }
        };
        const auto expressions = [&](const auto& self, const std::unique_ptr<Expr>& value) -> void {
            if (!value) return;
            types(types, value->type);
            self(self, value->left); self(self, value->right); self(self, value->third);
            for (const auto& argument : value->arguments) self(self, argument);
            for (const auto& argument : value->generic_arguments) {
                types(types, argument.type); self(self, argument.value);
            }
        };
        const auto statements = [&](const auto& self, const Statement& statement) -> void {
            if (statement.declaration) {
                types(types, statement.declaration->type);
                expressions(expressions, statement.declaration->initializer);
            }
            expressions(expressions, statement.expression);
            expressions(expressions, statement.condition);
            expressions(expressions, statement.increment);
            if (statement.first) self(self, *statement.first);
            if (statement.second) self(self, *statement.second);
            for (const auto& child : statement.statements) self(self, *child);
        };
        for (auto& record : program.records) {
            rebind(record, record.name);
            record.name = "Tag";
            for (auto& member : record.members) types(types, member.type);
        }
        for (auto& declaration : program.enumerations) {
            rebind(declaration, declaration.name);
            declaration.name = "EnumTag";
        }
        for (auto& function : program.functions) {
            types(types, function->return_type);
            for (auto& parameter : function->parameters) types(types, parameter.type);
            if (function->body) statements(statements, *function->body);
        }
        auto layout = hir::build_record_layout_context(program, options, *target, diagnostics);
        require(diagnostics.errors() == 0 && layout.records.size() == 2);
        require(layout.record(first->nominal_key())->size == 2);
        require(layout.record(second->nominal_key())->size == 4);
        const auto size_of = [&](const TypePtr& type) {
            return hir::layout_size(layout, layout.intern_type(type), *target);
        };
        const auto align_of = [&](const TypePtr& type) {
            return hir::layout_alignment(layout, layout.intern_type(type), *target);
        };
        program.evaluation_size_of = size_of;
        program.evaluation_align_of = align_of;
        program.evaluation_member_layout = [&](const TypePtr& type, std::string_view name)
            -> std::optional<EvaluationMemberLayout> {
            const auto id = layout.intern_type(type);
            const auto record = layout.type(id).record;
            if (!record) return {};
            const auto* member = layout.member(*record, name);
            if (!member) return {};
            return EvaluationMemberLayout{member->offset, member->alignment,
                                         member->bit_width, member->bit_offset};
        };
        require(expand_semantics(program, diagnostics, false, "default", abi));
        unsigned instances = 0;
        for (const auto& function : program.functions) {
            if (!function->return_type->nominal_identity) continue;
            require(function->return_type->nominal_identity == first_identity ||
                    function->return_type->nominal_identity == second_identity ||
                    function->return_type->nominal_identity == first_enum_identity ||
                    function->return_type->nominal_identity == second_enum_identity);
            ++instances;
        }
        require(instances == 4);
        require(finalize_target_constants(program, diagnostics, size_of, align_of));
        const auto module = hir::build(program, options, *target, diagnostics);
        require(diagnostics.errors() == 0 && module.records.size() == 2);
    }
}
