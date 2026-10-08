// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"
#include "frontend/parser.hpp"
#include "frontend/record_constraints.hpp"
#include "frontend/semantic.hpp"
#include "frontend/syntax.hpp"
#include "middle/hir.hpp"
#include "middle/initializer.hpp"
#include "target/subtarget.hpp"

#include <cstdlib>
#include <iostream>
#include <source_location>
#include <sstream>

int main(int argc, char** argv) {
    using namespace cross;
    if (argc != 4) return 2;
    std::ostringstream output;
    Diagnostics diagnostics(output);
    const auto require = [&](bool condition, std::source_location at = std::source_location::current()) {
        if (!condition) {
            std::cerr << argv[1] << ':' << at.line() << '\n' << output.str();
            std::abort();
        }
    };
    CompilerOptions options;
    options.target = argv[1];
    options.abi = argv[2];
    auto target = *target_for_triple(options.target);
    const auto bits = find_abi(target, options.abi, options.target)->address_bits;
    // Inject a non-default data-layout policy, independent of physical calls.
    if (std::string_view(argv[3]) == "custom") target.data_layout.natural_alignment_limit = 2;
    const EvaluationLayout evaluation_layout{
        target.data_layout.byte_order == ByteOrder::Big ? EvaluationByteOrder::Big : EvaluationByteOrder::Little,
        target.data_layout.natural_alignment_limit, target.data_layout.f80_storage_bytes,
        target.data_layout.f80_alignment, target.data_layout.code_addresses};
    SourceManager sources;
    const auto* source = sources.add("record-view.x", R"(
        struct Inner [[aligned(8)]] { u8 bytes[1]; uptr tail; u32 bits : 1; };
        struct Outer { struct Inner value; };
        namespace Other { struct Inner { u8 bytes[7]; }; }
        static $::meta::tokens expand(in $::meta::tokens input) {
            struct Outer object = { {{1u8, 2u8, 3u8}, 7uptr, 3u32} };
            if (sizeof(object.value.bytes) != $::meta::len(input) ||
                object.value.bytes[2] != 3u8 || object.value.tail != 7uptr || object.value.bits != 3u32)
                return $::quote { wrong };
            object.value.bytes[2] = 9u8;
            object.value.bits = 2u32;
            if (object.value.bytes[2] != 9u8 || object.value.bits != 2u32) return $::quote { wrong };
            return input;
        }
        static uptr inferred_size<T>(in T *value) { return sizeof(T); }
        static $::meta::tokens automatic(in $::meta::tokens input) {
            struct Local [[aligned($::meta::len($::quote { x }) * $::alignof(u32))]] {
                u8 bytes[$::meta::len($::quote { x }) * $::alignof(u32)];
                u32 bits : $::meta::len($::quote { x }) * $::alignof(u32);
            };
            struct Local object = {{1u8, 2u8}, 3u32};
            $::static_assert(sizeof(object.bytes) == $::alignof(u32), "private extent");
            $::static_assert($::alignof(struct Local) == $::alignof(u32), "private alignment");
            object.bits = 7u32;
            if (object.bits != ($::alignof(u32) == 2uptr ? 3u32 : 7u32)) return $::quote { wrong };
            u8 inferred[] = {[$::meta::len($::meta::gensym("extent")) * $::alignof(u32) - 1uptr] = 9u8};
            if (inferred_size(&inferred) != $::alignof(u32) ||
                inferred[sizeof(inferred) - 1uptr] != 9u8) return $::quote { wrong };
            return $::meta::gensym("after");
        }
    )");
    Parser parser(Lexer(*source, diagnostics).lex(), diagnostics, {}, bits);
    auto program = parser.parse();
    program.address_bits = bits;
    require(!diagnostics.errors() && program.records.size() == 4 && program.functions.size() == 3);
    const auto inner_key = program.records[0].nominal_key();
    const auto outer = record_type(program.records[1]);
    const auto other_key = program.records[2].nominal_key();
    require(program.record_definition(inner_key).get() == &program.records[0]);
    require(!program.record_definition(NominalTypeKey{"absent", {}}));
    const NominalTypeKey unrelated_identity{inner_key.name, std::make_shared<const NominalTypeIdentity>()};
    require(!program.record_definition(unrelated_identity));

    const auto layout_for_async = [&](TypePtr type, EvaluationLayoutKind kind = EvaluationLayoutKind::Complete)
        -> ContinuationTask<std::shared_ptr<hir::Module>> {
        auto module = std::make_shared<hir::Module>(
            co_await hir::build_required_layout_context_async(program, options, target, diagnostics, type, kind));
        require(!diagnostics.errors());
        co_return module;
    };
    const auto layout_for = [&](const TypePtr& type, EvaluationLayoutKind kind = EvaluationLayoutKind::Complete) {
        return layout_for_async(type, kind).run();
    };
    const LayoutQuery size_of = [&](const TypePtr& type) -> ContinuationTask<std::optional<std::uint64_t>> {
        const auto layout = co_await layout_for_async(type);
        co_return hir::layout_size(*layout, layout->intern_type(type), target);
    };
    const LayoutQuery align_of = [&](const TypePtr& type) -> ContinuationTask<std::optional<std::uint64_t>> {
        const auto layout = co_await layout_for_async(type, EvaluationLayoutKind::Alignment);
        co_return hir::layout_alignment(*layout, layout->intern_type(type), target);
    };
    program.evaluation_size_of = size_of;
    program.evaluation_align_of = align_of;
    unsigned member_queries{}, initializer_queries{}, type_queries{};
    program.evaluation_member_layout = [&](const TypePtr& type, const MemberName& name)
        -> ContinuationTask<std::optional<EvaluationMemberLayout>> {
        const auto layout = co_await layout_for_async(type);
        ++member_queries;
        const auto record = layout->type(layout->intern_type(type)).record;
        if (!record) co_return {};
        const auto* member = layout->member(*record, name);
        if (!member) co_return {};
        co_return EvaluationMemberLayout{member->offset, member->alignment, member->bit_width, member->bit_offset};
    };
    program.evaluation_initializer_plan = [&](const Expr& expression, const TypePtr& type)
        -> ContinuationTask<EvaluationInitializerPlan> {
        auto layout = co_await layout_for_async(type);
        ++initializer_queries;
        co_return initializer::build_for_evaluation(expression, type, program, *layout, target);
    };
    program.evaluation_initializer_types = [&](const Expr& expression, const TypePtr& type,
                                              std::span<const Expr* const> deferred)
        -> ContinuationTask<EvaluationInitializerTypePlan> {
        auto layout = co_await layout_for_async(builtin_type(BuiltinType::U8));
        ++type_queries;
        co_return initializer::types_for_evaluation(expression, type, program, *layout, target, deferred);
    };
    const auto& function = *program.functions.front();
    auto context = std::make_shared<SyntaxContext>();
    context->kind = SyntaxContext::Kind::DefinitionSite;
    context->definition = context->invocation = function.location;
    context->expansion = ExpansionId{99};
    const auto original_bound = program.records[0].members[0].type->lanes;
    const auto original_width = program.records[0].members[2].bit_width->text;
    for (const unsigned extent : {3U, 17U, 3U}) {
        auto view = std::make_shared<RecordDecl>(copy_evaluation_declaration(program.records[0]));
        view->members[0].type->lanes = extent;
        view->members[2].bit_width->evaluated_integer = Expr::IntegerConstant{UInt128{3}, BuiltinType::U32};
        auto alignment = copy_expression(*view->attributes[0].expression_argument);
        alignment->evaluated_integer = Expr::IntegerConstant{UInt128{32}, BuiltinType::U32};
        view->attributes[0].expression_argument = std::move(alignment);
        program.evaluation_record_definition = [view, inner_key](const NominalTypeKey& key)
            -> std::shared_ptr<const RecordDecl> { return key == inner_key ? view : nullptr; };
        require(program.record_definition(inner_key) == view);
        require(!program.record_definition(unrelated_identity));
        require(program.record_definition(other_key).get() == &program.records[2]);
        require(!record_source_error(program.records[1], program));
        const auto layout = layout_for(outer);
        const auto inner_id = layout->intern_type(record_type(*view));
        const auto& inner = layout->record(*layout->type(inner_id).record);
        const auto alignment_bytes = std::min(bits / 8, target.data_layout.natural_alignment_limit);
        const auto expected_tail = (extent + alignment_bytes - 1) / alignment_bytes * alignment_bytes;
        require(inner.alignment == 32 && inner.members[1].offset == expected_tail);
        require(inner.members[2].bit_width == 3);
        require(layout->type(inner.members[0].type).lanes == extent);
        require(inner.definition == view.get() && inner.retained_definition == view);

        TokenSequence input;
        for (unsigned index = 0; index < extent; ++index)
            input.emplace_back(Token{TokenKind::Identifier, "kept", function.location});
        const auto result = evaluate_procedural_body(function, input, bits, size_of, align_of,
            context, diagnostics, {}, evaluation_layout, {}, {}, &program);
        require(result && result->size() == extent && !diagnostics.errors());
        require(program.records[0].members[0].type->lanes == original_bound);
        require(program.records[0].members[2].bit_width->text == original_width);
        require(!program.records[0].members[2].bit_width->evaluated_integer ||
            program.records[0].members[2].bit_width->evaluated_integer->value == UInt128{1});

        // A by-value traversal must use the same view as layout and execution.
        auto bad = std::make_shared<RecordDecl>(copy_evaluation_declaration(*view));
        bad->members[0].type = builtin_type(BuiltinType::Void);
        auto saved = std::move(program.evaluation_record_definition);
        program.evaluation_record_definition = [bad, inner_key](const NominalTypeKey& key)
            -> std::shared_ptr<const RecordDecl> { return key == inner_key ? bad : nullptr; };
        const auto issue = record_source_error(program.records[1], program);
        require(issue && issue->message.find("non-object") != std::string::npos);
        program.evaluation_record_definition = std::move(saved);
        require(program.record_definition(inner_key) == view);
        program.evaluation_record_definition = {};
        std::weak_ptr<const RecordDecl> retained = view;
        view.reset();
        require(!retained.expired() && inner.definition == retained.lock().get());
        require(program.record_definition(inner_key).get() == &program.records[0]);
    }
    const auto& automatic = *program.functions[2];
    const auto outer_layout_scope = std::make_shared<EvaluationLayoutScopeIdentity>();
    program.evaluation_layout_scope = outer_layout_scope;
    const auto unchanged = [&] {
        const auto& record = program.records[3];
        require(record.members[0].type->lanes == 0);
        require(!record.members[1].bit_width->evaluated_integer);
        require(!record.attributes[0].expression_argument->evaluated_integer);
        for (const auto& statement : automatic.body->statements) {
            if (!statement->declaration || statement->declaration->name != "inferred") continue;
            const auto& declaration = *statement->declaration;
            require(declaration.type->lanes == 0);
            const auto& index = declaration.initializer->initializer_entries.front().designators.front().index;
            require(index->kind == Expr::Kind::Binary && !index->evaluated_integer);
        }
        require(!program.evaluation_record_definition && !program.evaluation_prepare_layout &&
                program.evaluation_layout_scope == outer_layout_scope);
    };
    for (const auto alignment : {2U, 4U, 2U}) {
        target.data_layout.natural_alignment_limit = alignment;
        ExpansionSemantics semantics(program, diagnostics, "default", {});
        require(semantics.validate_source(automatic));
        unchanged();
        auto active = std::make_shared<SyntaxContext>();
        active->kind = SyntaxContext::Kind::DefinitionSite;
        active->definition = active->invocation = automatic.location;
        active->expansion = ExpansionId{100 + alignment};
        TokenSequence input;
        input.emplace_back(Token{TokenKind::Identifier, "kept", automatic.location});
        auto active_layout = evaluation_layout;
        active_layout.natural_alignment_limit = alignment;
        const auto result = evaluate_procedural_body(automatic, input, bits, size_of, align_of,
            active, diagnostics, {}, active_layout, {}, {}, &program);
        require(result && result->size() == 1 && result->front().text == "after" && !diagnostics.errors());
        require(result->front().origin.fresh && result->front().origin.fresh->ordinal == 2);
        unchanged();
    }
    program.evaluation_layout_scope.reset();

    require(member_queries && initializer_queries && type_queries);

    const auto* late_source = sources.add("late-layout-helper.x", R"(
        namespace Helpers { [[eval_only]] static u32 alignment() { return 1u32; } }
        struct Aligned [[aligned(Helpers::alignment())]] { u8 value; };
        global u32 entry() { return 0u32; }
    )");
    std::ostringstream late_output;
    Diagnostics late_diagnostics(late_output);
    Parser late_parser(Lexer(*late_source, late_diagnostics).lex(), late_diagnostics, {}, bits);
    auto late_program = late_parser.parse();
    late_program.address_bits = bits;
    require(!late_diagnostics.errors());
    require(expand_semantics(late_program, late_diagnostics, false, "default", options.abi));
    require(late_program.functions.size() == 1 && late_program.evaluation_definitions.size() == 1);
    auto late_layout = hir::build(late_program, options, target, late_diagnostics);
    if (late_diagnostics.errors()) std::cerr << late_output.str();
    require(!late_diagnostics.errors() && late_layout.functions.size() == 1 &&
            late_layout.functions.front().source_name == "entry");
    const auto* aligned = late_layout.record(late_program.records.front().nominal_key());
    require(aligned && aligned->complete && aligned->size == 1 && aligned->alignment == 1);

    // A shared physical view must not override a temporarily selected prepared
    // definition with the same nominal key. Returning to the original provider
    // restores that view; leaving the query restores the original service too.
    const auto* isolation_source = sources.add("layout-service-isolation.x", R"(
        struct Leaf { u8 bytes[1]; uptr tail; };
        struct Root [[aligned(1uptr + 0uptr)]] { struct Leaf value; };
    )");
    std::ostringstream isolation_output;
    Diagnostics isolation_diagnostics(isolation_output);
    Parser isolation_parser(Lexer(*isolation_source, isolation_diagnostics).lex(), isolation_diagnostics, {}, bits);
    auto isolation_program = isolation_parser.parse();
    isolation_program.address_bits = bits;
    require(!isolation_diagnostics.errors() && isolation_program.records.size() == 2);
    const auto leaf_type = record_type(isolation_program.records[0]);
    const auto leaf_key = leaf_type->nominal_key();
    const auto one = std::make_shared<RecordDecl>(copy_evaluation_declaration(isolation_program.records[0]));
    const auto many = std::make_shared<RecordDecl>(copy_evaluation_declaration(*one));
    many->members[0].type->lanes = 17;
    std::shared_ptr<const RecordDecl> selected_view = one;
    isolation_program.evaluation_record_definition = [&](const NominalTypeKey& key)
        -> std::shared_ptr<const RecordDecl> { return key == leaf_key ? selected_view : nullptr; };
    unsigned isolated_fallbacks{}, isolated_requirements{};
    isolation_program.evaluation_member_layout = [&](const TypePtr& owner, const MemberName& name)
        -> ContinuationTask<std::optional<EvaluationMemberLayout>> {
        ++isolated_fallbacks;
        auto layout = co_await hir::build_required_layout_context_async(
            isolation_program, options, target, isolation_diagnostics, owner);
        const auto id = layout.intern_type(owner);
        const auto record = layout.type(id).record;
        if (!record) co_return {};
        const auto* member = layout.member(*record, name);
        if (!member) co_return {};
        co_return EvaluationMemberLayout{member->offset, member->alignment, member->bit_width, member->bit_offset};
    };
    const auto tail_alignment = std::min(bits / 8, target.data_layout.natural_alignment_limit);
    isolation_program.evaluation_required_integer = [&](const Expr&, Diagnostics&,
        const LayoutQuery& size, const LayoutQuery&, std::string_view, const FunctionDecl*,
        std::span<const std::pair<NameKey, TypePtr>>, EvaluationIntegerContext)
        -> ContinuationTask<EvaluationIntegerResult> {
        ++isolated_requirements;
        require((co_await size.async(leaf_type)).has_value());
        const MemberName tail{"tail", {}};
        const auto first = co_await isolation_program.evaluation_member_layout.async(leaf_type, tail);
        require(first && first->offset == tail_alignment && !isolated_fallbacks);
        selected_view = many;
        const auto second = co_await isolation_program.evaluation_member_layout.async(leaf_type, tail);
        require(second && second->offset == (17 + tail_alignment - 1) / tail_alignment * tail_alignment &&
                isolated_fallbacks == 1);
        selected_view = one;
        co_return EvaluationIntegerResult{EvaluationIntegerResult::Status::Value,
            Expr::IntegerConstant{UInt128{1}, BuiltinType::Uptr}};
    };
    auto isolated_layout = hir::build_required_layout_context(isolation_program, options, target,
        isolation_diagnostics, record_type(isolation_program.records[1]));
    if (isolation_diagnostics.errors()) std::cerr << isolation_output.str();
    require(!isolation_diagnostics.errors() && isolated_requirements == 1 && isolated_fallbacks == 1);
    require(isolated_layout.record(leaf_key)->retained_definition == one);
    require(hir::layout_view_covers(isolated_layout, isolation_program, leaf_type));
    require(hir::layout_view_covers(isolated_layout, isolation_program,
        record_type(isolation_program.records[1])));
    selected_view = many;
    require(!hir::layout_view_covers(isolated_layout, isolation_program, leaf_type));
    require(!hir::layout_view_covers(isolated_layout, isolation_program,
        record_type(isolation_program.records[1]))); // A nested owner changed.
    require(hir::layout_view_covers(isolated_layout, isolation_program, pointer_type(leaf_type)));
    require(isolation_program.evaluation_member_layout(leaf_type, MemberName{"tail", {}}).has_value() &&
            isolated_fallbacks == 2);
    selected_view = one;
    const auto leaf_id = isolated_layout.record(leaf_key)->id;
    isolated_layout.record(leaf_id).complete = false;
    require(!hir::layout_view_covers(isolated_layout, isolation_program, leaf_type));
    isolated_layout.record(leaf_id).complete = true;
    isolation_program.address_bits = bits == 32 ? 64 : 32;
    require(!hir::layout_view_covers(isolated_layout, isolation_program, leaf_type));
    isolation_program.address_bits = bits;
    auto missing_type = copy_type(leaf_type);
    missing_type->nominal_identity = std::make_shared<NominalTypeIdentity>();
    require(!hir::layout_view_covers(isolated_layout, isolation_program, missing_type));
    const auto local_source = sources.add("private-layout-coverage.x", R"(
        [[eval_only]] static u32 helper() {
            struct Private { u8 value; };
            struct PrivateRoot { struct Private child; };
            return 1u32;
        }
    )");
    Parser local_parser(Lexer(*local_source, isolation_diagnostics).lex(), isolation_diagnostics, {}, bits);
    auto local_program = local_parser.parse();
    local_program.address_bits = bits;
    require(!isolation_diagnostics.errors() && local_program.records.size() == 2);
    const auto private_type = record_type(local_program.records.front());
    const auto private_layout = hir::build_required_layout_context(local_program, options, target,
        isolation_diagnostics, private_type);
    require(!isolation_diagnostics.errors() && private_layout.record(private_type->nominal_key())->complete);
    require(!hir::layout_view_covers(private_layout, local_program, private_type));
    require(hir::layout_view_covers(private_layout, local_program, pointer_type(private_type)));

    // The same nominal identity cannot cross prepared private graphs, even if
    // definition owners and sizes happen to match. Within one graph, completed
    // records are reusable; replacing a nested owner still invalidates them.
    const auto private_scope = std::make_shared<EvaluationLayoutScopeIdentity>();
    local_program.evaluation_layout_scope = private_scope;
    auto private_one = std::make_shared<RecordDecl>(copy_evaluation_declaration(local_program.records[0]));
    auto private_many = std::make_shared<RecordDecl>(copy_evaluation_declaration(local_program.records[0]));
    private_many->members[0].type = array_type(builtin_type(BuiltinType::U8), 17);
    auto private_selected = private_one;
    local_program.evaluation_record_definition = [&](const NominalTypeKey& key) -> std::shared_ptr<const RecordDecl> {
        return key == private_type->nominal_key() ? private_selected : nullptr;
    };
    const auto private_root = record_type(local_program.records[1]);
    const auto scoped_private_layout = hir::build_required_layout_context(local_program, options, target,
        isolation_diagnostics, private_root);
    require(!isolation_diagnostics.errors());
    require(hir::layout_view_covers(scoped_private_layout, local_program, private_type));
    require(hir::layout_view_covers(scoped_private_layout, local_program, private_root));
    local_program.evaluation_layout_scope = std::make_shared<EvaluationLayoutScopeIdentity>();
    require(!hir::layout_view_covers(scoped_private_layout, local_program, private_root));
    local_program.evaluation_layout_scope.reset();
    require(!hir::layout_view_covers(scoped_private_layout, local_program, private_root));
    local_program.evaluation_layout_scope = private_scope;
    private_selected = private_many;
    require(!hir::layout_view_covers(scoped_private_layout, local_program, private_root));
    private_selected = private_one;
    require(hir::layout_view_covers(scoped_private_layout, local_program, private_root));
    require(!hir::layout_view_covers(private_layout, local_program, private_type));

    // The reusable source index caches positions, not successful validation or
    // private owners. Replacements, in-place field/completeness changes and
    // source publication must still be observed by each full traversal.
    RecordSourceIndex source_index;
    require(!record_source_error(local_program.records[1], local_program, source_index));
    private_selected = private_many;
    private_many->members[0].type = builtin_type(BuiltinType::Void);
    require(record_source_error(local_program.records[1], local_program, source_index).has_value());
    private_selected = private_one;
    require(!record_source_error(local_program.records[1], local_program, source_index));
    private_one->members[0].type = builtin_type(BuiltinType::Void);
    require(record_source_error(local_program.records[1], local_program, source_index).has_value());
    private_one->members[0].type = builtin_type(BuiltinType::U8);
    require(!record_source_error(local_program.records[1], local_program, source_index));
    local_program.records[0].complete = false;
    require(record_source_error(local_program.records[1], local_program, source_index).has_value());
    local_program.records[0].complete = true;
    require(!record_source_error(local_program.records[1], local_program, source_index));
    auto published = copy_evaluation_declaration(*private_one);
    published.name = "Published";
    published.nominal_identity = std::make_shared<NominalTypeIdentity>();
    local_program.records.push_back(std::move(published));
    require(source_index.definition(local_program, local_program.records.back().nominal_key()) ==
            &local_program.records.back());
    require(!record_source_error(local_program.records[1], local_program, source_index));

    // A required-layout traversal owns its resource baseline, not a global
    // sticky failure. Stop after a new failure and permit a fresh successful
    // builder while retaining the earlier independent diagnostic/sequence.
    const auto* resource_source = sources.add("layout-resource-recovery.x", R"(
        struct First [[aligned(1uptr + 0uptr)]] { u8 value; };
        struct Second [[aligned(1uptr + 0uptr)]] { u8 value; };
    )");
    std::ostringstream resource_output;
    Diagnostics resource_diagnostics(resource_output);
    Parser resource_parser(Lexer(*resource_source, resource_diagnostics).lex(), resource_diagnostics, {}, bits);
    auto resource_program = resource_parser.parse();
    resource_program.address_bits = bits;
    require(!resource_diagnostics.errors() && resource_program.records.size() == 2);
    unsigned requirements{};
    resource_program.evaluation_required_integer = [&](const Expr& expression, Diagnostics& active,
        const LayoutQuery&, const LayoutQuery&, std::string_view, const FunctionDecl*,
        std::span<const std::pair<NameKey, TypePtr>>, EvaluationIntegerContext) {
        ++requirements;
        ++resource_program.evaluation_resource_errors;
        active.error(expression.location, "forced resource failure");
        return EvaluationIntegerResult{};
    };
    (void)hir::build_record_layout_context(resource_program, options, target, resource_diagnostics);
    require(requirements == 1 && resource_program.evaluation_resource_errors == 1 &&
            resource_diagnostics.errors() == 1);
    resource_program.evaluation_required_integer = [&](const Expr&, Diagnostics&,
        const LayoutQuery&, const LayoutQuery&, std::string_view, const FunctionDecl*,
        std::span<const std::pair<NameKey, TypePtr>>, EvaluationIntegerContext) {
        ++requirements;
        return EvaluationIntegerResult{EvaluationIntegerResult::Status::Value,
            Expr::IntegerConstant{UInt128{1}, BuiltinType::Uptr}};
    };
    auto recovered = hir::build_record_layout_context(resource_program, options, target, resource_diagnostics);
    require(requirements == 3 && resource_program.evaluation_resource_errors == 1 &&
            resource_diagnostics.errors() == 1 && recovered.address_bits == bits);
    for (const auto& declaration : resource_program.records) {
        const auto* record = recovered.record(declaration.nominal_key());
        require(record && record->complete && record->size == 1 && record->alignment == 1);
    }

    const auto* patch_source = sources.add("patch-resource-traversal.x", R"(
        global uptr cells[2][2];
        global u32 entry() { return $::patch(7u32, cells[1uptr + 0uptr][1uptr + 0uptr]); }
    )");
    std::ostringstream patch_output;
    Diagnostics patch_diagnostics(patch_output);
    Parser patch_parser(Lexer(*patch_source, patch_diagnostics).lex(), patch_diagnostics, {}, bits);
    auto patch_program = patch_parser.parse();
    patch_program.address_bits = bits;
    require(!patch_diagnostics.errors());
    unsigned indices{};
    patch_program.evaluation_required_integer = [&](const Expr& expression, Diagnostics& active,
        const LayoutQuery&, const LayoutQuery&, std::string_view, const FunctionDecl*,
        std::span<const std::pair<NameKey, TypePtr>>, EvaluationIntegerContext) {
        ++indices;
        ++patch_program.evaluation_resource_errors;
        active.error(expression.location, "forced index resource failure");
        return EvaluationIntegerResult{};
    };
    (void)hir::build(patch_program, options, target, patch_diagnostics);
    if (indices != 1 || patch_program.evaluation_resource_errors != 1 || patch_diagnostics.errors() != 1)
        std::cerr << "patch indices=" << indices << " resources=" << patch_program.evaluation_resource_errors
                  << " errors=" << patch_diagnostics.errors() << '\n' << patch_output.str();
    require(indices == 1 && patch_program.evaluation_resource_errors == 1 && patch_diagnostics.errors() == 1);
}
