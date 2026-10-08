// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/semantic.hpp"
#include "frontend/record_constraints.hpp"
#include "middle/hir.hpp"
#include "middle/initializer.hpp"

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
    {
        // An exact local placement must not depend on diagnostic spelling.
        // Unresolved names, including paths through a private namespace, still
        // need every component of their spelling to select a declaration.
        const auto placement = std::make_shared<ValuePlacementIdentity>();
        placement->name = "local";
        ValueBinding binding;
        binding.kind = ValueBinding::Kind::Local;
        binding.placement = placement;
        NameKey original("local");
        original.bind(binding);
        auto renamed = original;
        renamed.spelling = "display_only";
        require(original == renamed);
        require(NameKeyHash{}(original) == NameKeyHash{}(renamed));
        NameMap<unsigned> locals{{original, 17}};
        require(locals.at(renamed) == 17);
        auto other = renamed;
        other.binding.placement = std::make_shared<ValuePlacementIdentity>(*placement);
        require(other != original);
        other = renamed;
        other.context.value = 1;
        require(other != original);
        const auto fresh = std::make_shared<const FreshIdentifier>();
        original.fresh = renamed.fresh = fresh;
        require(original == renamed && NameKeyHash{}(original) == NameKeyHash{}(renamed));
        other = renamed;
        other.fresh = std::make_shared<const FreshIdentifier>();
        require(other != original);

        NameKey left("private_ns::left"), right("private_ns::right");
        left.fresh = right.fresh = fresh;
        require(left != right && NameSet{left, right}.size() == 2);
        // An incomplete/synthetic binding is not an exact placement.
        left.binding.kind = right.binding.kind = ValueBinding::Kind::Local;
        require(left != right && NameSet{left, right}.size() == 2);
        left.bind(binding);
        right.bind(binding);
        left.binding.kind = right.binding.kind = ValueBinding::Kind::Object;
        left.spelling = "private_ns::left";
        right.spelling = "private_ns::right";
        require(left != right && NameSet{left, right}.size() == 2);
    }
    {
        // Identical display/serialization metadata does not make independently
        // allocated private identities equal. A copy keeps the opaque handle.
        const auto first = std::make_shared<const FreshIdentifier>();
        const auto second = std::make_shared<const FreshIdentifier>();
        const MemberName a{"field", first}, b{"field", second}, ordinary{"field", {}};
        require(a != b && a != ordinary && a == MemberName{"display-only", first});
        std::unordered_set<MemberName, MemberNameHash> names{a, b, ordinary};
        require(names.size() == 3 && names.contains(MemberName{"different display", first}));
        RecordDecl source_record;
        source_record.name = "PrivateMembers";
        source_record.complete = true;
        source_record.members.push_back({{}, "field", builtin_type(BuiltinType::U32), {}, {}, first});
        source_record.members.push_back({{}, "field", builtin_type(BuiltinType::U32), {}, {}, second});
        Program source_program;
        require(!record_source_error(source_record, source_program));
        const auto copied_record = copy_evaluation_declaration(source_record);
        require(copied_record.members[0].member_name() == a && copied_record.members[1].member_name() == b);
        source_record.members.push_back({{}, "field", builtin_type(BuiltinType::U32), {}, {}, first});
        require(record_source_error(source_record, source_program).has_value());
        hir::Module members;
        members.records.emplace_back();
        for (const auto& source_member : copied_record.members) {
            hir::RecordMember member;
            member.name = source_member.name;
            member.fresh = source_member.fresh;
            members.records[0].members.push_back(std::move(member));
        }
        require(members.member(hir::RecordId{0}, a) == &members.records[0].members[0]);
        require(members.member(hir::RecordId{0}, b) == &members.records[0].members[1]);
        require(!members.member(hir::RecordId{0}, ordinary));
        ObjectDecl object;
        object.type = builtin_type(BuiltinType::U32);
        object.initializer = std::make_unique<Expr>();
        object.initializer->kind = Expr::Kind::AggregateInitializer;
        object.initializer->initializer_entries.emplace_back();
        auto& entry = object.initializer->initializer_entries.back();
        entry.value = std::make_unique<Expr>();
        entry.value->kind = Expr::Kind::Integer;
        entry.designators.emplace_back();
        entry.designators.back().member = "field";
        entry.designators.back().member_fresh = first;
        const auto copy = copy_evaluation_declaration(object);
        require(copy->initializer->initializer_entries[0].designators[0].member_name() == a);
    }
    FunctionDecl lexical_owner;
    ObjectDecl lifted;
    lifted.type = builtin_type(BuiltinType::Label);
    lifted.lexical_function = lexical_owner.function_scope;
    auto copied_lifted = copy_evaluation_declaration(lifted);
    require(copied_lifted->lexical_function == lexical_owner.function_scope);

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

    // Independently of lexical parser binding, deliberately rename two parsed
    // nominal declarations to the same display name. Every
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
        program.evaluation_member_layout = [&](const TypePtr& type, const MemberName& name)
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

        const auto* anonymous_source = sources.add("anonymous_nominal_identity.x", R"(
            global struct { u32 value; } first, second;
            global struct { u32 value; } third;
            global union { u32 value; u8 byte; } variant;
            typedef enum [[underlying(u16)]] { A = 1u16 } First;
            typedef enum [[underlying(u16)]] { B = 2u16 } Second;
            global First first_enum;
            global Second second_enum;
        )");
        Parser anonymous_parser(Lexer(*anonymous_source, diagnostics).lex(), diagnostics, {}, model->address_bits);
        auto anonymous_program = anonymous_parser.parse();
        require(diagnostics.errors() == 0 && anonymous_program.records.size() == 3 &&
                anonymous_program.enumerations.size() == 2 && anonymous_program.objects.size() == 6);
        require(same_type(anonymous_program.objects[0]->type, anonymous_program.objects[1]->type));
        require(!same_type(anonymous_program.objects[0]->type, anonymous_program.objects[2]->type));
        require(!same_type(anonymous_program.objects[4]->type, anonymous_program.objects[5]->type));
        for (const auto& record : anonymous_program.records)
            require(record.name.empty() && record.nominal_identity);
        for (const auto& anonymous_enum : anonymous_program.enumerations)
            require(anonymous_enum.name.empty() && anonymous_enum.nominal_identity && !anonymous_enum.local);
        anonymous_program.address_bits = model->address_bits;
        require(expand_semantics(anonymous_program, diagnostics, false, "default", abi));
        const auto anonymous_module = hir::build(anonymous_program, options, *target, diagnostics);
        require(diagnostics.errors() == 0 && anonymous_module.records.size() == 3);

        const auto* generic_source = sources.add("generic_nominal_identity.x", R"(
            [[noinline]] static T pass<T>(in T value) { return value; }
            [[noinline]] static T local<T>(in T input) {
                struct Fixed { u16 value; } fixed = {3u16};
                struct Fixed copy = pass(fixed);
                struct { u16 value; } anonymous = {5u16}, anonymous_copy = pass(anonymous);
                struct Node { T value; struct Node *next; } node;
                node.value = input;
                node.next = &node;
                enum E [[underlying(u32)]] { A = (u32)sizeof(T) } item = pass(A);
                return node.next->value + (T)copy.value + (T)item + (T)anonymous_copy.value;
            }
            static T unused<T>(in T input) { struct Unused { T value; }; return input; }
            global u32 entry() {
                return local(7u32) + (u32)local(9u16) + local(11u32);
            }
        )");
        Parser generic_parser(Lexer(*generic_source, diagnostics).lex(), diagnostics, {}, model->address_bits);
        auto generic_program = generic_parser.parse();
        require(diagnostics.errors() == 0 && generic_program.records.size() == 4);
        generic_program.address_bits = model->address_bits;
        generic_program.evaluation_initializer_plan = [&](const Expr& expression, const TypePtr& destination) {
            auto concrete_layout = hir::build_required_layout_context(
                generic_program, options, *target, diagnostics, destination);
            return initializer::build_for_evaluation(
                expression, destination, generic_program, concrete_layout, *target);
        };
        require(expand_semantics(generic_program, diagnostics, false, "default", abi));
        require(generic_program.records.size() == 6 && generic_program.enumerations.size() == 2);
        std::unordered_set<NominalTypeKey, NominalTypeKeyHash> keys;
        std::unordered_set<std::string> serializations;
        unsigned fixed_instances = 0;
        for (const auto& record : generic_program.records) {
            require(record.nominal_identity && !record.nominal_identity->generic_owner);
            const NominalTypeKey key{record.name, record.nominal_identity};
            require(keys.insert(key).second && serializations.insert(key.canonical_name()).second);
            if (record.name == "Node") {
                require(record.members[1].type->pointee->nominal_identity == record.nominal_identity);
            } else if (!record.name.empty()) {
                require(record.name == "Fixed");
                ++fixed_instances;
            }
        }
        require(fixed_instances == 2);
        for (const auto& declaration : generic_program.enumerations) {
            require(declaration.nominal_identity && !declaration.nominal_identity->generic_owner);
            require(declaration.enumerators.front().binding.enumeration == declaration.nominal_identity);
            require(declaration.enumerators.front().value.has_value());
        }
        unsigned fixed_passes = 0;
        unsigned anonymous_passes = 0;
        for (const auto& function : generic_program.functions)
            if (function->name.starts_with("pass$")) {
                if (function->return_type->nominal_name == "Fixed") ++fixed_passes;
                if (function->return_type->kind == Type::Kind::Record &&
                    function->return_type->nominal_name.empty()) ++anonymous_passes;
            }
        require(fixed_passes == 2); // Same spelling/layout, different enclosing instances.
        require(anonymous_passes == 2);
        const auto generic_module = hir::build(generic_program, options, *target, diagnostics);
        require(diagnostics.errors() == 0 && generic_module.records.size() == 6);

        const auto* header_source = sources.add("header_nominal_identity.x", R"(
            static struct { T value; } *header<T>() { return (void *)0uptr; }
            static struct { u16 value; } *fixed<T>() { return (void *)0uptr; }
            global u32 entry() {
                header<u32>(); header<u16>(); header<u32>();
                fixed<u32>(); fixed<u16>(); fixed<u16>();
                return 0u32;
            }
        )");
        Parser header_parser(Lexer(*header_source, diagnostics).lex(), diagnostics, {}, model->address_bits);
        auto header_program = header_parser.parse();
        require(diagnostics.errors() == 0 && header_program.records.size() == 2);
        for (const auto& record : header_program.records)
            require(record.nominal_identity && record.nominal_identity->generic_owner);
        header_program.address_bits = model->address_bits;
        require(expand_semantics(header_program, diagnostics, false, "default", abi));
        require(header_program.records.size() == 4 && header_program.functions.size() == 5);
        keys.clear();
        serializations.clear();
        for (const auto& record : header_program.records) {
            require(record.nominal_identity && !record.nominal_identity->generic_owner);
            const NominalTypeKey key{record.name, record.nominal_identity};
            require(keys.insert(key).second && serializations.insert(key.canonical_name()).second);
        }
        for (const auto& function : header_program.functions) {
            if (function->name == "entry") continue;
            require(function->return_type->kind == Type::Kind::Pointer);
            const auto& result = function->return_type->pointee;
            require(keys.contains(result->nominal_key()) &&
                result->nominal_identity->function_scope == function->function_scope);
        }
        const auto header_module = hir::build(header_program, options, *target, diagnostics);
        require(diagnostics.errors() == 0 && header_module.records.size() == 4);
        const auto* named_header_source = sources.add("named_header_nominal_identity.x", R"(
            static struct Named { T value; } *header<T>() { return (void *)0uptr; }
            static enum Code { code = N } named_enum<u32 N>() { return code; }
            global u32 named_entry() {
                header<u32>(); header<u16>(); header<u32>();
                return (u32)named_enum<3u32>() + (u32)named_enum<7u32>();
            }
        )");
        Parser named_header_parser(Lexer(*named_header_source, diagnostics).lex(),
                                   diagnostics, {}, model->address_bits);
        auto named_header_program = named_header_parser.parse();
        require(diagnostics.errors() == 0 && named_header_program.records.size() == 1 &&
                named_header_program.enumerations.size() == 1);
        require(named_header_program.records[0].nominal_identity->generic_owner &&
                named_header_program.enumerations[0].nominal_identity->generic_owner &&
                named_header_program.enumerations[0].local);
        named_header_program.address_bits = model->address_bits;
        require(expand_semantics(named_header_program, diagnostics, false, "default", abi));
        require(named_header_program.records.size() == 2 && named_header_program.enumerations.size() == 2);
        require(named_header_program.records[0].nominal_key() != named_header_program.records[1].nominal_key());
        require(named_header_program.enumerations[0].nominal_key() != named_header_program.enumerations[1].nominal_key());
        require(named_header_program.functions.size() == 5);
        const auto named_header_module = hir::build(named_header_program, options, *target, diagnostics);
        require(diagnostics.errors() == 0 && named_header_module.records.size() == 2);

        const auto* list_source = sources.add("shared_header_nominal_identity.x", R"(
            typedef u8 T;
            global struct Shared { T value; } *first<T>(), *plain(), *second<T>(), *plain_again();
            global struct Counted { u8 bytes[N]; } *first_count<u32 N>(), *second_count<u32 N>();
        )");
        Parser list_parser(Lexer(*list_source, diagnostics).lex(), diagnostics, {}, model->address_bits);
        auto list_program = list_parser.parse();
        require(diagnostics.errors() == 0 && list_program.records.size() == 5 &&
                list_program.functions.size() == 6);
        const auto& first_result = list_program.functions[0]->return_type->pointee;
        const auto& plain_result = list_program.functions[1]->return_type->pointee;
        const auto& second_result = list_program.functions[2]->return_type->pointee;
        require(first_result->nominal_identity && second_result->nominal_identity &&
                first_result->nominal_identity != second_result->nominal_identity);
        require(first_result->nominal_identity->generic_owner == list_program.functions[0]->generic_tag_owner &&
                second_result->nominal_identity->generic_owner == list_program.functions[2]->generic_tag_owner);
        require(!plain_result->nominal_identity &&
                plain_result->nominal_key() == list_program.functions[3]->return_type->pointee->nominal_key());
        for (std::size_t index = 0; index < 3; ++index) {
            const auto& record = list_program.records[index];
            const auto& member = record.members.front().type;
            if (!record.nominal_identity) {
                require(member->kind == Type::Kind::Builtin && member->builtin == BuiltinType::U8);
                continue;
            }
            const auto& owner = record.nominal_identity->generic_owner == list_program.functions[0]->generic_tag_owner
                ? list_program.functions[0] : list_program.functions[2];
            require(member->kind == Type::Kind::Generic && !member->generic_header_view &&
                    generic_type_key(*member) == name_key(owner->generic_parameters.front()));
        }
        for (std::size_t index = 0; index < 2; ++index) {
            const auto& bound = list_program.records[index + 3].members.front().type->array_bound;
            require(bound && bound->name_context &&
                    bound->name_context->value_binding == list_program.functions[index + 4]->generic_parameters[0].binding);
        }
    }
}
