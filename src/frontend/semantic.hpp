// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "frontend/ast.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

namespace cross {

// Detach a source expression while preserving immutable binding identities.
std::unique_ptr<Expr> copy_expression(const Expr& source);

// The single registry used by semantic validation, source queries, and driver
// inspection. Attribute spellings are contextual and therefore omit "$::".
std::span<const std::string_view> core_attribute_names();
bool is_known_attribute(std::string_view name);
// Expansion declarations use ordinary source attribute constraints before
// registration, including when the expansion is never invoked.
bool validate_expansion_function_declaration(const FunctionDecl& function,
    bool syntax_expander, Diagnostics& diagnostics);

using GenericPointerResolver = EvaluationPointerQuery;
using GenericAbiCanonicalizer =
    std::function<std::optional<std::string>(std::string_view)>;
using EvaluationLayoutInstaller = std::function<void(Program&)>;

// The evaluation stage uses the caller's host continuation scheduler. Source
// declarations and installed model services must outlive the awaited task.
ContinuationTask<bool> expand_evaluation_async(Program& program,
    Diagnostics& diagnostics, bool opportunistic);

// Expands explicit generic instances and deterministic translation-time calls.
// Ordinary visible functions are evaluated opportunistically when requested;
// forced evaluation is independent of that option. Runtime declarations retain
// no generic type/call, staging wrapper, or evaluation-only function. Source-only
// helper declarations remain owned for later required target-layout/value proofs.
bool expand_semantics(Program& program, Diagnostics& diagnostics,
                      bool evaluate_calls = true,
                      std::string_view mangling = "default",
                      std::string_view default_abi = "default",
                      const GenericPointerResolver& pointer_resolver = {},
                      const GenericAbiCanonicalizer& canonical_abi = {},
                      const EvaluationLayoutInstaller& install_layout = {});

using LayoutQuery = EvaluationLayoutQuery;
// Connected host traversals await LayoutQuery::async and the *_async required
// value services below; synchronous entry points pump only at legacy boundaries.

// Expansion evaluation owns snapshots of reached source declarations. These
// copies preserve source identities without borrowing a replacement parser's AST.
std::unique_ptr<FunctionDecl> copy_evaluation_declaration(const FunctionDecl& declaration);
std::unique_ptr<ObjectDecl> copy_evaluation_declaration(const ObjectDecl& declaration);
RecordDecl copy_evaluation_declaration(const RecordDecl& declaration);
EnumDecl copy_evaluation_declaration(const EnumDecl& declaration);

// Persistent, demand-driven preparation for a growing expansion declaration
// view. It uses ordinary generic instantiation without removing templates or
// assigning any runtime transport to an expansion function.
class ExpansionSemantics {
public:
    ExpansionSemantics(Program& declarations, Diagnostics& diagnostics,
        std::string mangling, GenericAbiCanonicalizer canonical_abi);
    ~ExpansionSemantics();
    std::unique_ptr<FunctionDecl> prepare(const FunctionDecl& function);
    ContinuationTask<std::unique_ptr<FunctionDecl>> prepare_async(const FunctionDecl& function);
    bool validate_source(const FunctionDecl& function);
    ContinuationTask<bool> validate_source_async(const FunctionDecl& function);
    bool validate_assertions();
    ContinuationTask<bool> validate_assertions_async();
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

struct SyntaxNode;
enum class SyntaxParseCategory;
using SyntaxParseCallback = ContinuationQuery<std::shared_ptr<const SyntaxNode>(
    SyntaxParseCategory, const TokenSequence&, std::shared_ptr<const SyntaxContext>, SourceLocation)>;

// Mandatory macro execution shares the bounded target-scalar evaluator.
// Token values remain translation-only and cannot enter runtime lowering.
std::optional<TokenSequence> evaluate_procedural_body(
    const FunctionDecl& macro, const TokenSequence& input, unsigned address_bits,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::shared_ptr<const SyntaxContext> context, Diagnostics& diagnostics,
    EvaluationLimits limits = {}, EvaluationLayout layout = {},
    const SyntaxParseCallback& parse = {},
    std::shared_ptr<const SyntaxContext> call_context = {}, Program* declarations = nullptr);

struct SyntaxMatchValue;
ContinuationTask<std::optional<TokenSequence>> evaluate_procedural_body_async(
    const FunctionDecl& macro, TokenSequence input, unsigned address_bits,
    LayoutQuery size_of, LayoutQuery align_of,
    std::shared_ptr<const SyntaxContext> context, Diagnostics& diagnostics,
    EvaluationLimits limits = {}, EvaluationLayout layout = {},
    SyntaxParseCallback parse = {},
    std::shared_ptr<const SyntaxContext> call_context = {}, Program* declarations = nullptr);
std::optional<TokenSequence> evaluate_syntax_body(
    const FunctionDecl& function, std::shared_ptr<const SyntaxMatchValue> input,
    unsigned address_bits, const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::shared_ptr<const SyntaxContext> context, Diagnostics& diagnostics,
    EvaluationLimits limits, EvaluationLayout layout, const SyntaxParseCallback& parse = {},
    std::shared_ptr<const SyntaxContext> call_context = {}, Program* declarations = nullptr);
ContinuationTask<std::optional<TokenSequence>> evaluate_syntax_body_async(
    const FunctionDecl& function, std::shared_ptr<const SyntaxMatchValue> input,
    unsigned address_bits, LayoutQuery size_of, LayoutQuery align_of,
    std::shared_ptr<const SyntaxContext> context, Diagnostics& diagnostics,
    EvaluationLimits limits, EvaluationLayout layout, SyntaxParseCallback parse = {},
    std::shared_ptr<const SyntaxContext> call_context = {}, Program* declarations = nullptr);

// Static assertions are retained until target HIR has established nominal
// layouts.  The callbacks keep target layout ownership out of the frontend.
bool finalize_target_constants(Program& program, Diagnostics& diagnostics,
                               const LayoutQuery& size_of,
                               const LayoutQuery& align_of);
// Connected finalization awaits required proofs on the caller's host scheduler.
// The task owns its selected callbacks; Program/source owners outlive the task.
ContinuationTask<bool> finalize_target_constants_async(Program& program,
    Diagnostics& diagnostics, LayoutQuery size_of, LayoutQuery align_of);

// Evaluates one required integer expression once target layout callbacks are
// available. HIR uses this for bit-field widths and static patch-sink indices
// because record layout owns the allocation policy and can resolve dependent
// records lazily. Definition preparation never borrows an active invocation.
// Invocation-local clients may request CallerInvocation with a real lexical
// function definition; local bindings still provide types, not runtime values.
// StagedDefinition reports ContextUnavailable only for an actual translation-only
// definition. It neither borrows invocation capabilities nor invents a value.
EvaluationIntegerResult evaluate_target_integer_requirement(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace, const FunctionDecl* caller,
    std::span<const std::pair<NameKey, TypePtr>> local_types, EvaluationIntegerContext context);
ContinuationTask<EvaluationIntegerResult> evaluate_target_integer_requirement_async(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace, const FunctionDecl* caller,
    std::span<const std::pair<NameKey, TypePtr>> local_types, EvaluationIntegerContext context);
std::optional<Expr::IntegerConstant> evaluate_target_integer_constant(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace = {}, const FunctionDecl* caller = nullptr,
    std::span<const std::pair<NameKey, TypePtr>> local_types = {},
    EvaluationIntegerContext context = EvaluationIntegerContext::Definition);
ContinuationTask<std::optional<Expr::IntegerConstant>> evaluate_target_integer_constant_async(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace = {}, const FunctionDecl* caller = nullptr,
    std::span<const std::pair<NameKey, TypePtr>> local_types = {},
    EvaluationIntegerContext context = EvaluationIntegerContext::Definition);

// A required fixed extent uses the ordinary evaluator and target queries.
std::optional<std::uint32_t> evaluate_fixed_array_bound(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace = {}, const FunctionDecl* caller = nullptr,
    std::span<const std::pair<NameKey, TypePtr>> local_types = {});
ContinuationTask<std::optional<std::uint32_t>> evaluate_fixed_array_bound_async(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace = {}, const FunctionDecl* caller = nullptr,
    std::span<const std::pair<NameKey, TypePtr>> local_types = {});

// Resolve a retained vector attribute after generic substitution, using the
// same integer evaluator and selected data model as other required bounds.
bool resolve_vector_bound(Program& program, const TypePtr& type, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace = {}, const FunctionDecl* caller = nullptr,
    std::span<const std::pair<NameKey, TypePtr>> local_types = {},
    EvaluationIntegerContext context = EvaluationIntegerContext::Definition);
ContinuationTask<bool> resolve_vector_bound_async(Program& program, const TypePtr& type,
    Diagnostics& diagnostics, const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace = {}, const FunctionDecl* caller = nullptr,
    std::span<const std::pair<NameKey, TypePtr>> local_types = {},
    EvaluationIntegerContext context = EvaluationIntegerContext::Definition);

// Required pointer calls use the ordinary bounded evaluator. The resolver
// supplies target-owned address operations; no runtime storage is read.
std::unique_ptr<Expr> evaluate_target_pointer_constant(
    Program& program, const Expr& expression, const TypePtr& destination,
    const FunctionDecl* caller, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    const GenericPointerResolver& resolver);
ContinuationTask<std::unique_ptr<Expr>> evaluate_target_pointer_constant_async(
    Program& program, const Expr& expression, const TypePtr& destination,
    const FunctionDecl* caller, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    const GenericPointerResolver& resolver);

// Uses the same required-constant evaluator for each `aligned` placement.
// The caller supplies the subject only for a precise argument-count error.
std::optional<unsigned> evaluate_alignment_attribute(
    Program& program, const Attribute& attribute, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view subject, std::string_view source_namespace = {});
ContinuationTask<std::optional<unsigned>> evaluate_alignment_attribute_async(
    Program& program, const Attribute& attribute, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view subject, std::string_view source_namespace = {});

} // namespace cross
