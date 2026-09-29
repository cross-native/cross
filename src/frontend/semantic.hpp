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

// The single registry used by semantic validation, source queries, and driver
// inspection. Attribute spellings are contextual and therefore omit "$::".
std::span<const std::string_view> core_attribute_names();
bool is_known_attribute(std::string_view name);

using GenericPointerResolver = std::function<bool(
    std::unique_ptr<Expr>&, const TypePtr&, const FunctionDecl*,
    std::span<const NameKey>)>;
using GenericAbiCanonicalizer =
    std::function<std::optional<std::string>(std::string_view)>;
using EvaluationLayoutInstaller = std::function<void(Program&)>;

// Expands explicit generic instances and deterministic translation-time calls.
// Ordinary visible functions are evaluated opportunistically when requested;
// forced evaluation is independent of that option. On success no generic type,
// generic call, staging wrapper, or evaluation-only function remains.
bool expand_semantics(Program& program, Diagnostics& diagnostics,
                      bool evaluate_calls = true,
                      std::string_view mangling = "default",
                      std::string_view default_abi = "default",
                      const GenericPointerResolver& pointer_resolver = {},
                      const GenericAbiCanonicalizer& canonical_abi = {},
                      const EvaluationLayoutInstaller& install_layout = {});

using LayoutQuery =
    std::function<std::optional<std::uint64_t>(const TypePtr&)>;

// Expansion evaluation owns snapshots of reached source declarations. These
// copies preserve source identities without borrowing a replacement parser's AST.
std::unique_ptr<FunctionDecl> copy_evaluation_declaration(const FunctionDecl& declaration);
std::unique_ptr<ObjectDecl> copy_evaluation_declaration(const ObjectDecl& declaration);
RecordDecl copy_evaluation_declaration(const RecordDecl& declaration);
EnumDecl copy_evaluation_declaration(const EnumDecl& declaration);

struct SyntaxNode;
enum class SyntaxParseCategory;
using SyntaxParseCallback = std::function<std::shared_ptr<const SyntaxNode>(
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
std::optional<TokenSequence> evaluate_syntax_body(
    const FunctionDecl& function, std::shared_ptr<const SyntaxMatchValue> input,
    unsigned address_bits, const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::shared_ptr<const SyntaxContext> context, Diagnostics& diagnostics,
    EvaluationLimits limits, EvaluationLayout layout, const SyntaxParseCallback& parse = {},
    std::shared_ptr<const SyntaxContext> call_context = {}, Program* declarations = nullptr);

// Static assertions are retained until target HIR has established nominal
// layouts.  The callbacks keep target layout ownership out of the frontend.
bool finalize_target_constants(Program& program, Diagnostics& diagnostics,
                               const LayoutQuery& size_of,
                               const LayoutQuery& align_of);

// Evaluates one required integer expression once target layout callbacks are
// available. HIR uses this for bit-field widths and static patch-sink indices
// because record layout owns the allocation policy and can resolve dependent
// records lazily.
std::optional<Expr::IntegerConstant> evaluate_target_integer_constant(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace = {});

// A required fixed extent uses the ordinary evaluator and target queries.
std::optional<std::uint32_t> evaluate_fixed_array_bound(
    Program& program, const Expr& expression, Diagnostics& diagnostics,
    const LayoutQuery& size_of, const LayoutQuery& align_of,
    std::string_view source_namespace = {});

// Required pointer calls use the ordinary bounded evaluator. The resolver
// supplies target-owned address operations; no runtime storage is read.
std::unique_ptr<Expr> evaluate_target_pointer_constant(
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

} // namespace cross
