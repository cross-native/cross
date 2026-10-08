// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "frontend/ast.hpp"
#include "frontend/lexer.hpp"
#include "frontend/syntax.hpp"
#include "frontend/evaluation_task.hpp"

#include <functional>
#include <algorithm>
#include <optional>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cross {

// Internal expansion records are not public syntax nodes or textual groups.
// They keep an already executed owner bounded until header types are known.
struct PreparedSyntaxFragment {
    SyntaxExecution::Output output;
    SyntaxParseCategory category{SyntaxParseCategory::Expression};
    std::shared_ptr<const SyntaxContext> context;
    TokenIdentity original_position;
    std::uint64_t storage{};
    bool deferred{};
};

// A monotonic dependency: captures can retain this header before its generic
// list is complete, then use that same list after relocation. It owns no AST
// expressions or contexts and therefore cannot form a provenance cycle.
struct SyntaxHeaderBindings {
    bool complete{};
    std::vector<GenericParameter> parameters;
    std::uint64_t storage{64};
    void finish(std::vector<GenericParameter> values, SyntaxExecution& execution,
                SourceLocation location);
};

class Parser {
public:
    Parser(std::vector<Token> tokens, Diagnostics& diagnostics,
           std::shared_ptr<SyntaxExecution> execution = {},
           unsigned address_bits = 0);
    Program parse();
    EvaluationTask<Program> parse_async();
    // Parse one bounded expansion declaration with the ordinary function
    // grammar, without recursively registering it or publishing its body.
    std::unique_ptr<FunctionDecl> parse_expansion_declaration(
        std::shared_ptr<const SyntaxContext> definition_context,
        Program& declarations);
    EvaluationTask<std::unique_ptr<FunctionDecl>> parse_expansion_declaration_async(
        std::shared_ptr<const SyntaxContext> definition_context,
        Program& declarations);
    // Read-only, bounded recognition using the same core grammar. The returned
    // tree preserves source tokens; speculative state never escapes this call.
    std::optional<SyntaxParsedFragment> parse_syntax_fragment(
        SyntaxPatternElement::Kind kind, std::size_t first) const;
    EvaluationTask<std::optional<SyntaxParsedFragment>> parse_syntax_fragment_async(
        SyntaxPatternElement::Kind kind, std::size_t first) const;
    // Complete bounded input in this parser's environment. Unlike pattern
    // recognition, no prefix may be accepted while leaving trailing input.
    std::shared_ptr<const SyntaxNode> parse_syntax_tokens(
        SyntaxParseCategory category, std::vector<Token> input) const;
    static std::shared_ptr<const SyntaxNode> parse_syntax_tokens(
        SyntaxParseCategory category, std::vector<Token> input,
        std::shared_ptr<const SyntaxContext> context, Diagnostics& diagnostics);
    EvaluationTask<std::shared_ptr<const SyntaxNode>> parse_syntax_tokens_async(
        SyntaxParseCategory category, std::vector<Token> input) const;
    static EvaluationTask<std::shared_ptr<const SyntaxNode>> parse_syntax_tokens_async(
        SyntaxParseCategory category, std::vector<Token> input,
        std::shared_ptr<const SyntaxContext> context, Diagnostics& diagnostics);
    std::shared_ptr<const SyntaxContext> syntax_context(SourceLocation location) const;
    std::shared_ptr<const SyntaxContext> effective_context(SourceLocation location,
        const SyntaxParseEnvironment* environment = nullptr) const;

private:
    friend struct SyntaxParseEnvironment;
    friend struct ProvenanceOwnershipTestAccess;
    enum class PublicDeclarationContext { File, Block, DirectFunction };
    EvaluationTask<bool> recognize_declaration_splice_async(
        const Token& item, PublicDeclarationContext context) const;
    std::shared_ptr<const SyntaxParseEnvironment> snapshot_environment(
        std::shared_ptr<const FragmentNamespaceLookup> fragment_context = {}) const;
    std::shared_ptr<const SyntaxParseEnvironment> refine_namespace_environment(
        const SyntaxParseEnvironment& retained,
        std::shared_ptr<const FragmentNamespaceLookup> lookup, SourceLocation location) const;
    // Bounded context-capture operations do not mutate parser tables. Share
    // equivalent namespace refinements only within one such operation.
    mutable std::size_t namespace_context_cache_depth_{};
    mutable std::vector<std::pair<const SyntaxParseEnvironment*,
        std::shared_ptr<const SyntaxParseEnvironment>>> public_namespace_contexts_;
    struct NamespaceContextCacheScope {
        const Parser& parser;
        explicit NamespaceContextCacheScope(const Parser& parser) : parser(parser) {
            ++parser.namespace_context_cache_depth_;
        }
        ~NamespaceContextCacheScope() {
            if (--parser.namespace_context_cache_depth_ == 0) parser.public_namespace_contexts_.clear();
        }
    };
    void restore_environment(const SyntaxParseEnvironment& environment,
                             const SyntaxContext& context);
    void restore_deferred_environment(const SyntaxParseEnvironment& environment,
                                      const SyntaxContext& context,
                                      TokenIdentity original_position);
    const Token& current(std::size_t lookahead = 0) const;
    const Token& recognition_current(std::size_t lookahead = 0) const;
    // A ready ordinary/raw read needs no task frame. A reached macro awaits
    // its expansion on the caller's schedule and returns an owned token value,
    // never a reference into a vector that a later expansion may reallocate.
    class CursorRead {
    public:
        CursorRead(Parser& parser, std::size_t lookahead)
            : parser_(parser), lookahead_(lookahead) {}
        bool await_ready();
        template<class Promise>
        void await_suspend(std::coroutine_handle<Promise> parent) noexcept {
            exposure_->await_suspend(parent);
        }
        Token await_resume();
    private:
        Parser& parser_;
        std::size_t lookahead_;
        std::optional<EvaluationTask<void>> exposure_;
    };
    CursorRead current_async(std::size_t lookahead = 0) {
        return CursorRead(*this, lookahead);
    }
    // A new shared resource error ends this parse cursor, not all later
    // independent recognition using the same executor or retained environment.
    mutable std::uint64_t resource_epoch_{};
    bool resource_failed() const {
        return syntax_ && syntax_->execution()->resource_errors() != resource_epoch_;
    }
    struct ResourceEpoch {
        const Parser& parser;
        std::uint64_t previous;
        explicit ResourceEpoch(const Parser& parser)
            : parser(parser), previous(parser.resource_epoch_) {
            parser.resource_epoch_ = parser.syntax_ ? parser.syntax_->execution()->resource_errors() : 0;
        }
        ~ResourceEpoch() { parser.resource_epoch_ = previous; }
    };
    EvaluationTask<bool> consume_async(std::string_view spelling);
    EvaluationTask<std::optional<Token>> consume_kind_async(TokenKind kind);
    EvaluationTask<bool> expect_async(std::string_view spelling, std::string_view context = {});
    void error_here(std::string message);
    EvaluationTask<void> error_here_async(std::string message);
    void synchronize_external();
    EvaluationTask<bool> parse_syntax_registration_async(Program* program);
    EvaluationTask<std::vector<SyntaxActivation>> parse_syntax_entries_async(std::string_view end);
    const SyntaxDefinition* active_syntax(bool item) const;
    bool macro_start() const;
    EvaluationTask<std::optional<SyntaxExecution::Output>> expand_at_position_async(bool item);
    EvaluationTask<void> expand_inline_macro_fragments_async();
    EvaluationTask<void> normalize_qualified_name_async();
    EvaluationTask<void> prepare_header_async(const std::vector<Token>* shared_specifiers = nullptr);
    std::optional<std::size_t> prepared_declarator_start_;
    // Retain already expanded shared specifiers without stamping one
    // declarator's newly inferred token bindings onto their replay input.
    bool retaining_shared_specifiers_{};
    std::shared_ptr<const SyntaxHeaderBindings> shared_specifier_header_;
    TokenIdentity shared_specifier_declarator_{};
    std::shared_ptr<SharedSpecifierOwners> shared_specifier_owners_;
    EvaluationTask<void> remember_shared_declarator_async();
    std::vector<std::pair<std::size_t, Token>> shared_specifier_inputs_;
    void remember_specifier_input(std::size_t index);
    std::vector<Token> specifier_input_tokens(std::size_t first, std::size_t end) const;
    EvaluationTask<bool> probe_header_type_async(bool generic_argument = false);
    struct HeaderProbeInvocation {
        std::size_t position;
        bool macro;
        bool operator==(const HeaderProbeInvocation&) const = default;
    };
    struct HeaderProbeRejected {};
    struct HeaderPrepared {};
    void retain_prepared_fragment(std::size_t first, SyntaxExecution::Output output,
        SyntaxParseCategory category, std::shared_ptr<const SyntaxContext> context = {},
        bool deferred = false, TokenIdentity original_position = {});
    std::unique_ptr<Parser> prepared_fragment_parser(const Token& token);
    EvaluationTask<std::shared_ptr<const SyntaxNode>> parse_opaque_invocation_async(SyntaxKind category);
    bool opaque_statement_has_expression_continuation() const;
    struct DeferredNameRecognition {};
    enum class PublicNameDomain { Ordinary, Tag };
    void require_public_name_context(std::string_view name, SourceLocation location = {},
                                    PublicNameDomain domain = PublicNameDomain::Ordinary) const;
    void mark_public_binding_uncertainty();
    std::optional<std::size_t> bounded_group_end(std::size_t first);
    std::optional<std::size_t> fenced_fragment_end(std::size_t first, bool expression);
    EvaluationTask<std::optional<std::size_t>> bounded_statement_end_async(
        std::size_t first, unsigned depth = 0);
    EvaluationTask<std::optional<std::size_t>> bounded_declaration_end_async(std::size_t first);
    std::shared_ptr<const SyntaxContext> public_fragment_context(std::size_t first) const;
    std::shared_ptr<const SyntaxContext> public_token_context(SourceLocation location,
        const std::shared_ptr<const SyntaxContext>& parsed_context) const;
    std::shared_ptr<const SyntaxNode> deferred_node(std::size_t first, std::size_t end,
        SyntaxProduction slot, SyntaxParseCategory category,
        std::shared_ptr<const SyntaxContext> context);
    std::unique_ptr<Parser> replacement_parser(SyntaxExecution::Output output,
                                                Diagnostics* diagnostics = nullptr) const;
    void adopt_replacement(Parser& child);
    std::unique_ptr<Statement> parse_statement_replacement(bool block_item);
    std::unique_ptr<Expr> parse_expression_replacement();

    enum class AttributeParseMode { Semantic, SyntaxOnly };
    std::vector<Attribute> parse_attributes(bool one_specifier = false,
        AttributeParseMode mode = AttributeParseMode::Semantic);
    EvaluationTask<std::vector<Attribute>> parse_attributes_async(bool one_specifier = false,
        AttributeParseMode mode = AttributeParseMode::Semantic);
    EvaluationTask<std::vector<Attribute>> parse_attributes_impl_async(bool one_specifier,
        AttributeParseMode mode);
    EvaluationTask<bool> defer_public_header_group_async(std::optional<std::size_t> end,
        std::function<EvaluationTask<void>()> parse);
    std::optional<std::size_t> generic_parameter_group_end(std::size_t first);
    EvaluationTask<void> apply_type_attributes_async(
        TypePtr& type,
        std::optional<std::pair<std::uint32_t, SourceLocation>>*
            pending_address_space = nullptr);
    void apply_type_attribute(
        TypePtr& type, const Attribute& attribute,
        std::optional<std::pair<std::uint32_t, SourceLocation>>*
            pending_address_space = nullptr);
    EvaluationTask<std::optional<std::string>> parse_qualified_name_async(
        SyntaxProduction production = SyntaxProduction::QualifiedName);
    EvaluationTask<std::string> peek_qualified_name_async();
    std::string peek_qualified_name() const;
    AliasDefinitionPtr bound_type_alias(std::string_view name) const;
    AliasDefinitionPtr resolve_type_alias(std::string_view name) const;
    AliasDefinitionPtr make_alias_definition(const TypePtr& type) const;
    void remember_alias_binding(std::size_t token_index, std::string_view spelling,
                                AliasDefinitionPtr definition, bool declaration);
    void transfer_alias_rebindings(const Parser& child, const AliasDefinitionPtr& definition);
    TypePtr resolve_tag_type(std::string_view name, const Token& token) const;
    // Block-scope typedef and tag visible at the current token, if any.
    AliasDefinitionPtr block_type_alias(std::string_view name) const;
    TypePtr block_tag_type(std::string_view name) const;
    // This function's block-scope definitions that `type` needs, if any.
    std::shared_ptr<const CarriedDefinitions> block_definitions(const TypePtr& type) const;
    void import_carried(const std::shared_ptr<const CarriedDefinitions>& carried);
    TypePtr rebind_tag_types(TypePtr type) const;
    void remember_tag_binding(std::size_t token_index, std::string_view spelling,
                              const TypePtr& type, bool declaration);
    TypePtr declare_local_tag(TypePtr type, SourceLocation location, bool complete);
    TypePtr declare_retained_implicit_tag(std::string_view name, SourceLocation location, bool is_union);
    std::shared_ptr<const NominalTypeIdentity> new_nominal_identity(SourceLocation location);
    void remember_label_binding(Statement& statement, std::size_t token_index);
    void bind_label_references(Statement& statement, bool complete_function = false);
    void bind_generic_type(Type& type, const std::vector<GenericParameter>& parameters) const;
    ValueBinding retained_value_binding(std::string_view name, SourceLocation location) const;
    struct SpecifierAttributes {
        enum class Context { Entity, Member, Parameter };
        std::vector<Attribute>& declaration;
        Context context{Context::Entity};
        std::optional<std::size_t> inline_record{};
        struct Candidate { Attribute attribute; std::size_t position; };
        std::vector<Candidate> candidates{};
    };
    void resolve_specifier_attributes(SpecifierAttributes& attributes, bool declares_entity);
    using StorageSpecifier = std::function<EvaluationTask<bool>()>;
    TypePtr parse_type(bool record_specifiers = true,
                       StorageSpecifier storage_specifier = {},
                       SpecifierAttributes* attributes = nullptr,
                       bool tag_declaration = false);
    EvaluationTask<TypePtr> parse_type_async(bool record_specifiers = true,
        StorageSpecifier storage_specifier = {},
        SpecifierAttributes* attributes = nullptr, bool tag_declaration = false);
    // TypePrefix ends before the separately written angle-generic value name.
    enum class DeclaratorContext { Named, Parameter, TypeName, TypePrefix };
    TypePtr
    parse_declarator(TypePtr base, std::optional<std::string>& name,
                     DeclaratorContext context = DeclaratorContext::Named,
                     std::unique_ptr<Expr>* dynamic_outer_bound = nullptr,
                     SourceLocation* name_location = nullptr,
                     std::vector<FunctionDecl::GenericParameter>*
                         angle_parameters = nullptr,
                     std::size_t* name_token_index = nullptr,
                     bool share_prototype_scope = false);
    EvaluationTask<TypePtr> parse_declarator_async(TypePtr base, std::optional<std::string>& name,
        DeclaratorContext context = DeclaratorContext::Named,
        std::unique_ptr<Expr>* dynamic_outer_bound = nullptr,
        SourceLocation* name_location = nullptr,
        std::vector<FunctionDecl::GenericParameter>* angle_parameters = nullptr,
        std::size_t* name_token_index = nullptr, bool share_prototype_scope = false,
        std::vector<Attribute>* entity_suffix_attributes = nullptr,
        bool parenthesized_component = false);
    EvaluationTask<std::vector<FunctionDecl::GenericParameter>> parse_angle_generic_parameters_async();
    EvaluationTask<std::vector<FunctionDecl::GenericParameter>> parse_angle_generic_parameters_impl_async();
    EvaluationTask<std::vector<std::string>> preview_generic_types_async(bool* pending_fragments = nullptr,
                                                  bool* generic_header = nullptr,
                                                  bool declarator_only = false,
                                                  TokenIdentity* first_declarator = nullptr);
    EvaluationTask<bool> consume_generic_close_async();
    bool known_generic_name(const Expr& name) const;
    void apply_callable_attributes(TypePtr& type,
                                   const std::vector<Attribute>& attributes);
    EvaluationTask<TypePtr> parse_array_suffix_async(
        TypePtr element, bool parameter = false,
        std::unique_ptr<Expr>* dynamic_outer_bound = nullptr);
    enum class TypeProbe {
        Required, ExpressionAlternative, GenericArgumentAlternative,
        GenericTypeArgumentAlternative,
    };
    EvaluationTask<bool> type_start_async(TypeProbe probe = TypeProbe::Required);
    void parse_external(Program& program, const std::string& name_space);
    void parse_external_impl(Program& program, const std::string& name_space);
    EvaluationTask<void> parse_external_async(Program& program, std::string name_space);
    EvaluationTask<void> parse_external_impl_async(Program& program, std::string name_space);
    EvaluationTask<void> parse_external_node_splice_async(Program& program,
                                                        std::string name_space);
    std::optional<std::size_t> function_header_splice_position();
    EvaluationTask<void> parse_function_header_splice_async(Program& program, std::string name_space,
                                                          std::size_t production_event, std::size_t header_index);
    EvaluationTask<void> parse_typedef_async(std::string name_space,
                       std::vector<Attribute> attributes, TypePtr base_type,
                       SourceLocation location, bool consume_semicolon = true);
    AliasDefinitionPtr register_typedef(SourceLocation location, std::string name, TypePtr type,
                          const std::vector<Attribute>& attributes);
    EvaluationTask<void> parse_enum_declaration_async(Program& program, std::string name_space,
                                std::vector<Attribute> attributes);
    void drain_pending_tags(Program& program);
    BuiltinType enum_underlying(const std::vector<Attribute>& attributes,
        std::shared_ptr<const CapturedTypeErrors>& errors);
    void type_error(TypePtr& type, SourceLocation location, std::string message);
    EvaluationTask<void> validate_captured_type_async(TypePtr type);
    void parse_enumerators(EnumDecl& declaration, const std::string& name_space);
    EvaluationTask<void> parse_enumerators_async(EnumDecl& declaration, std::string name_space);
    EvaluationTask<void> parse_record_declaration_async(Program& program,
                                  std::string name_space,
                                  std::vector<Attribute> attributes);
    void parse_record_members(RecordDecl& declaration);
    EvaluationTask<void> parse_record_members_async(RecordDecl& declaration);
    EvaluationTask<void> parse_global_label_declaration_async(
        Program& program, std::string owner,
        std::vector<Attribute> attributes);
    EvaluationTask<std::optional<std::string>> global_label_owner_async(std::size_t first);
    bool parse_static_assertion(Statement* block_statement = nullptr);
    EvaluationTask<bool> parse_static_assertion_async(Statement* block_statement = nullptr);
    EvaluationTask<std::unique_ptr<Statement>> parse_using_declaration_async(bool file_scope);
    void apply_import(std::string name, TokenIdentity declaration, bool file_scope);
    EvaluationTask<std::unique_ptr<FunctionDecl>>
    parse_function_async(std::size_t header_first, SourceLocation location, std::string name,
                   std::string name_space, TypePtr return_type, Linkage linkage,
                   bool inline_hint, std::vector<Attribute> attributes,
                   std::shared_ptr<FunctionType> signature = {},
                   std::vector<FunctionDecl::GenericParameter>
                       angle_parameters = {});
    EvaluationTask<std::unique_ptr<ObjectDecl>> parse_object_async(
        SourceLocation location, std::string name, TypePtr type, Linkage linkage,
        std::vector<Attribute> attributes, bool consume_semicolon = true);
    void parse_parameter_list(std::vector<ParameterDecl>& parameters, bool& variadic);
    EvaluationTask<ParameterDecl> parse_parameter_async(unsigned ordinal);
    EvaluationTask<void> parse_parameter_list_async(std::vector<ParameterDecl>& parameters, bool& variadic);

    std::unique_ptr<Statement> parse_statement(bool block_item = false);
    using StatementTask = EvaluationTask<std::unique_ptr<Statement>>;
    StatementTask parse_statement_async(bool block_item = false);
    StatementTask parse_unattributed_statement_async(std::vector<Attribute> attributes, bool block_item);
    StatementTask parse_global_label_statement_async(std::vector<Attribute> attributes = {});
    StatementTask parse_compound_async();
    StatementTask parse_statement_replacement_async(bool block_item);
    std::unique_ptr<Statement> parse_unattributed_statement(std::vector<Attribute> attributes, bool block_item);
    std::unique_ptr<Statement>
    parse_global_label_statement(std::vector<Attribute> attributes = {});
    std::unique_ptr<Statement> parse_compound();
    std::unique_ptr<Statement> parse_local_declaration(std::vector<Attribute> attributes = {},
        bool consume_semicolon = true,
        SyntaxProduction production = SyntaxProduction::Declaration);
    StatementTask parse_local_declaration_async(std::vector<Attribute> attributes = {},
                            bool consume_semicolon = true,
                            SyntaxProduction production = SyntaxProduction::Declaration);
    EvaluationTask<bool> local_declaration_start_async();

    std::unique_ptr<Expr> parse_expression(std::unique_ptr<Expr> seed = {});
    std::unique_ptr<Expr> parse_constant_expression();
    std::unique_ptr<Expr> parse_initializer();
    std::unique_ptr<Expr> parse_assignment(std::unique_ptr<Expr> seed = {});
    std::unique_ptr<Expr> parse_conditional(std::unique_ptr<Expr> seed = {});
    std::unique_ptr<Expr> parse_binary(int minimum_precedence, std::unique_ptr<Expr> seed = {});
    std::unique_ptr<Expr> parse_cast();
    std::unique_ptr<Expr> parse_unary();
    std::unique_ptr<Expr> parse_postfix(std::unique_ptr<Expr> seed = {});
    std::unique_ptr<Expr> parse_primary();
    std::unique_ptr<Expr> parse_quote();
    using ExpressionTask = EvaluationTask<std::unique_ptr<Expr>>;
    ExpressionTask parse_expression_async(std::unique_ptr<Expr> seed = {});
    ExpressionTask parse_constant_expression_async();
    ExpressionTask parse_initializer_async();
    ExpressionTask parse_assignment_async(std::unique_ptr<Expr> seed = {});
    ExpressionTask parse_conditional_async(std::unique_ptr<Expr> seed = {});
    ExpressionTask parse_binary_async(int minimum_precedence, std::unique_ptr<Expr> seed = {});
    ExpressionTask parse_cast_async();
    ExpressionTask parse_unary_async();
    ExpressionTask parse_postfix_async(std::unique_ptr<Expr> seed = {});
    ExpressionTask parse_primary_async();
    ExpressionTask parse_quote_async();
    ExpressionTask parse_expression_replacement_async();
    std::vector<FunctionDecl::GenericParameter> generic_parameters(
        const std::vector<Attribute>& attributes);
    static int precedence(std::string_view operation);

    struct ProductionEvent {
        SyntaxProduction production;
        std::size_t first{};
        std::size_t end{};
        std::vector<std::size_t> children;
        std::shared_ptr<const SyntaxNode> opaque;
        std::shared_ptr<const SyntaxContext> context;
    };
    struct ProductionScope {
        Parser& parser;
        std::size_t event;
        ProductionScope(Parser& parser, SyntaxProduction production);
        ~ProductionScope();
        void finish();
    };
    std::size_t begin_production(SyntaxProduction production);
    void end_production(std::size_t event);
    void flatten_production(std::size_t event,
        std::size_t parent = std::numeric_limits<std::size_t>::max());
    std::shared_ptr<const SyntaxNode> public_node(std::size_t event) const;
    void record_balanced_sequence(std::size_t first, std::size_t end,
        SyntaxProduction production = SyntaxProduction::BalancedTokenSequence);

    std::vector<Token> tokens_;
    Diagnostics& diagnostics_;
    // Zero means a standalone parser has no resolved target layout.
    unsigned address_bits_{};
    std::size_t index_{};
    std::optional<SyntaxState> syntax_;
    unsigned raw_token_depth_{};
    bool preparing_header_{};
    bool probing_header_type_{};
    Diagnostics* expansion_diagnostics_{};
    std::shared_ptr<const SyntaxHeaderBindings> header_bindings_;
    struct PreparedFragmentFrame {
        std::shared_ptr<SyntaxExecution> execution;
        ~PreparedFragmentFrame() { execution->end_fragment(); }
    };
    std::unique_ptr<PreparedFragmentFrame> prepared_fragment_frame_;
    bool replacement_{};
    std::vector<std::string> active_imports_;
    std::vector<TokenIdentity> active_import_declarations_;
    std::size_t current_scope_imports_{};
    std::vector<std::string> active_generic_types_;
    using GenericParameterKind = ValuePlacementIdentity::GenericParameterKind;
    using GenericSignature = std::vector<GenericParameterKind>;
    struct GenericFunction {
        GenericSignature parameters;
        ValueBinding binding;
        bool operator==(const GenericFunction&) const = default;
    };
    // Only generic functions need parameter-kind vectors. Ordinary function
    // names share the value classifier instead of duplicating every snapshot.
    std::unordered_map<std::string, GenericFunction> known_functions_;
    void remember_function(const FunctionDecl& function);
    const GenericSignature* known_generic_parameters(const Expr& name) const;
    enum class OrdinaryNameKind { Object, Function };
    struct OrdinaryName {
        OrdinaryNameKind kind;
        ValueBinding binding;
        bool operator==(const OrdinaryName&) const = default;
    };
    struct OrdinaryNames {
        std::unordered_map<std::string, OrdinaryName> entries;
        std::uint64_t storage{32};
    };
    // Context snapshots share this immutable view. Declarations detach before
    // mutation; snapshot work is constant while retained storage stays charged.
    std::shared_ptr<OrdinaryNames> known_ordinary_values_ = std::make_shared<OrdinaryNames>();
    void remember_ordinary_name(std::string_view name, OrdinaryNameKind kind,
                                SourceLocation location = {}, ValueBinding binding = {});
    ValueBinding nonlocal_value_binding(std::string_view name, SourceLocation location,
                                       const NameLookupContext& context) const;
    ValueBinding bind_value(std::string_view name, SourceLocation location,
                            std::size_t first, ValueBinding::Kind kind,
                            GenericSignature parameters = {});
    const ValueBinding* destination_value_binding(const std::string& name) const;
    // Classifier spelling/mark -> exact local value declaration identity.
    std::vector<NameMap<ValueBinding>> local_scopes_;
    // Parameter lookup belongs to one function declarator, independently of
    // lexical block/tag scope indices. Snapshots retain names, not mutable ASTs.
    struct PrototypeName {
        ValueBinding binding;
        SourceLocation location;
    };
    struct PrototypeScope {
        TokenRegion region;
        NameMap<PrototypeName> values;
    };
    std::vector<PrototypeScope> prototype_scopes_;
    struct PrototypeScopeFrame {
        Parser& parser;
        std::size_t depth;
        bool shared{};
        explicit PrototypeScopeFrame(Parser& parser, bool shared = false)
            : parser(parser), depth(parser.prototype_scopes_.size()), shared(shared) {}
        void finish() {
            if (parser.prototype_scopes_.size() > depth) parser.prototype_scopes_.resize(depth);
        }
        ~PrototypeScopeFrame() { if (!shared) finish(); }
    };
    std::vector<NameMap<AliasDefinitionPtr>> local_type_scopes_;
    struct LocalTag {
        TypePtr type;
        bool complete{};
    };
    struct HeaderNominalScope {
        NameMap<ValueBinding> values;
        NameMap<LocalTag> tags;
    };
    class HeaderNominalFrame;
    std::optional<std::size_t> generic_header_depth_;
    // A bounded header parser hands these names to the composing body parser.
    // This is transient parser state, not part of a public syntax node.
    std::optional<HeaderNominalScope> completed_header_nominals_;
    std::vector<NameMap<LocalTag>> local_tag_scopes_;
    std::shared_ptr<std::uint64_t> nominal_occurrence_ = std::make_shared<std::uint64_t>();
    std::shared_ptr<const GenericTagOwner> generic_tag_owner_;
    std::shared_ptr<const FunctionScopeIdentity> function_scope_;
    std::optional<NameMap<LocalTag>> tag_destination_;
    std::size_t tag_destination_depth_{};
    // A moved declaration keeps source scopes for lookup, but its binders may
    // belong to file scope. Nested blocks still introduce ordinary local scopes.
    std::optional<std::size_t> external_binding_depth_;
    // A bounded splice's declaration placement is separate from its restored
    // source lookup environment. This parser outlives the child parse; the
    // pointer never enters persistent syntax contexts or ASTs.
    const Parser* declaration_destination_{};
    bool binds_at_file_scope() const {
        return local_scopes_.empty() || external_binding_depth_ == local_scopes_.size();
    }
    // Only declarations reintroduced by the currently placed subtree remap
    // captured value uses. Unrelated destination names never do so.
    std::unordered_map<ValueBinding, ValueBinding, ValueBindingHash> value_rebindings_;
    std::unordered_map<NominalTypeKey, std::shared_ptr<const TagBinding>, NominalTypeKeyHash>
        tag_rebindings_;
    // Transparent typedef identity is parser provenance, not nominal type identity.
    std::unordered_map<AliasDefinitionPtr, AliasDefinitionPtr> alias_rebindings_;
    // Original opening-token identities distinguish a captured lexical block
    // from a different destination block with the same local names.
    std::vector<TokenIdentity> scope_origins_;
    std::vector<TokenIdentity> scope_ends_;
    // Restored node-root scopes can be more specific than a fresh quote
    // identifier's definition context (which precedes the constructed block).
    std::vector<TokenIdentity> restored_scope_origins_;
    std::size_t restored_scope_event_base_{};
    // Physical placement is separate from immutable captured lexical identity.
    // It stays parser-private and is not restored with source lookup tables.
    struct ScopePlacement {
        ScopePlacement() = default;
        ScopePlacement(const ScopePlacement&) = default;
        ScopePlacement(ScopePlacement&&) = default;
        ScopePlacement& operator=(const ScopePlacement&) = default;
        ScopePlacement& operator=(ScopePlacement&&) = default;
        ~ScopePlacement() {
            detail::OwnerRelease::run([&](detail::OwnerRelease& release) { release.take(parent); });
        }
        TokenIdentity block;
        TokenIdentity source_end;
        std::shared_ptr<const GenericTagOwner> generic_owner;
        std::shared_ptr<const FunctionScopeIdentity> function_scope;
        std::shared_ptr<const ScopePlacement> parent;
        std::size_t depth{};
        std::size_t first_event{};
        std::size_t end_event{std::numeric_limits<std::size_t>::max()};
    private:
        friend class detail::OwnerRelease;
        mutable detail::OwnerReleaseLink teardown_;
    };
    class ScopePlacementFrame;
    std::shared_ptr<const ScopePlacement> scope_placement_;
    using ScopePlacements = std::unordered_map<TokenIdentity,
        std::vector<std::shared_ptr<const ScopePlacement>>, TokenIdentityHash>;
    std::shared_ptr<ScopePlacements> scope_placements_ = std::make_shared<ScopePlacements>();
    // Implicit binders in retained uses belong to source lookup, not declaration
    // placement. This private table is shared only by composing parsers; public
    // snapshots retain exact bindings rather than a mutable table handle.
    struct ForwardTagScope {
        TokenIdentity block;
        std::shared_ptr<const ScopePlacement> placement;
        std::shared_ptr<const GenericTagOwner> generic_owner;
        std::shared_ptr<const FunctionScopeIdentity> function_scope;
        std::string name_space;
        bool operator==(const ForwardTagScope&) const = default;
    };
    struct ForwardTagScopeHash {
        std::size_t operator()(const ForwardTagScope& scope) const {
            auto result = TokenIdentityHash{}(scope.block);
            const auto mix = [&](std::size_t value) { result ^= value + (result << 6) + (result >> 2); };
            mix(std::hash<const ScopePlacement*>{}(scope.placement.get()));
            mix(std::hash<const GenericTagOwner*>{}(scope.generic_owner.get()));
            mix(std::hash<const FunctionScopeIdentity*>{}(scope.function_scope.get()));
            mix(std::hash<std::string>{}(scope.name_space));
            return result;
        }
    };
    struct ForwardTags {
        std::unordered_map<ForwardTagScope, NameMap<TypePtr>, ForwardTagScopeHash> scopes;
        std::uint64_t storage{};
    };
    std::shared_ptr<ForwardTags> forward_tags_ = std::make_shared<ForwardTags>();
    ForwardTagScope retained_forward_tag_scope(SourceLocation location) const;
    ForwardTagScope destination_forward_tag_scope() const;
    struct ScopePlacementSelection {
        std::vector<std::shared_ptr<const ScopePlacement>> closest;
        bool ambiguous() const { return closest.size() > 1; }
        bool contains(const std::shared_ptr<const ScopePlacement>& placement) const {
            return std::find(closest.begin(), closest.end(), placement) != closest.end();
        }
    };
    ScopePlacementSelection select_scope_placements(
        const TokenIdentity& block, std::size_t event_base, SourceLocation location) const;
    // Only names that depend on replay from equally-close copies are blocked.
    // Retained bindings and unrelated free names keep their normal lookup.
    struct ScopeAmbiguity {
        TokenIdentity block;
        std::size_t depth{};
        NameSet ordinary;
        NameSet tags;
    };
    std::vector<ScopeAmbiguity> scope_ambiguities_;
    bool diagnose_scope_ambiguity(std::size_t depth, const NameKey& key,
        PublicNameDomain domain, SourceLocation location) const;
    void diagnose_lexical_association(const NameKey& key, SourceLocation location) const;
    bool diagnose_retained_scope_ambiguity(std::string_view name, SourceLocation location,
        PublicNameDomain domain) const;
    struct RetainedTypeName {
        enum class Kind { Absent, Value, Alias, Tag, Ambiguous } kind{Kind::Absent};
        AliasDefinitionPtr alias;
        TypePtr tag;
    };
    RetainedTypeName retained_type_name(std::string_view name, SourceLocation location,
        PublicNameDomain domain) const;
    bool retained_type_binding_visible(std::size_t depth, const RetainedTypeName& binding,
        SourceLocation location) const;
    struct ScopeEvent {
        TokenIdentity block;
        TokenIdentity statement;
        std::shared_ptr<const SyntaxContext> statement_context;
        NameMap<ValueBinding> values;
        NameMap<AliasDefinitionPtr> aliases;
        NameMap<LocalTag> tags;
        std::shared_ptr<const ScopePlacement> placement;
    };
    bool declaration_precedes(TokenIdentity position, std::shared_ptr<const SyntaxContext> context,
                              const TokenIdentity& use, TokenIdentity after = {}) const;
    // Shared by replacement parsers; speculative recognition does not publish.
    std::shared_ptr<std::vector<ScopeEvent>> scope_events_ =
        std::make_shared<std::vector<ScopeEvent>>();
    struct ScopeImportEvent {
        enum class Scope { Braced, File };
        TokenIdentity block;
        TokenIdentity declaration;
        std::vector<std::string> imports;
        Scope scope{Scope::Braced};
    };
    std::shared_ptr<std::vector<ScopeImportEvent>> scope_import_events_ =
        std::make_shared<std::vector<ScopeImportEvent>>();
    // Namespace and transparent item scopes are separate from local-value
    // scope indices, which are also used for deferred declaration replay.
    std::vector<TokenRegion> import_regions_;
    // First-parse namespace introductions are separate from the retained
    // definition/call-site lookup. Copy-on-write keeps capture probes and
    // immutable environments from publishing declarations into their owner.
    std::vector<FragmentNamespaceScope> fragment_namespaces_;
    struct FragmentNamespacePlacement {
        NameKey name;
        TokenIdentity stream;
        std::string source_namespace;
        std::string destination;
        std::shared_ptr<const FragmentNamespaceIdentity> identity;
    };
    // Reopened blocks still have separate imports, but declarations from the
    // same source fragment share the namespace's completed name domain.
    // This parser-only registry never enters retained contexts/public trees.
    std::vector<FragmentNamespacePlacement> fragment_namespace_placements_;
    void open_fragment_namespace(std::string_view spelling, const std::string& destination,
        SourceLocation location);
    void remember_fragment_name(std::string_view spelling, std::string_view destination,
        SourceLocation location, FragmentNameDomain domain, ValueBinding value = {});
    std::optional<FragmentNamespaceName> fragment_name(std::string_view spelling,
        SourceLocation location, FragmentNameDomain domain) const;
    std::shared_ptr<const FragmentNamespaceLookup> fragment_lookup(
        std::string_view spelling, SourceLocation location) const;
    bool retain_fragment_lookup(MetaToken& token, std::size_t at, std::size_t end) const;
    std::string active_namespace_;
    struct EnumTag {
        BuiltinType underlying;
        std::shared_ptr<const CapturedTypeErrors> errors{};
        bool operator==(const EnumTag&) const = default;
    };
    std::unordered_map<std::string, EnumTag> enum_types_;
    std::vector<EnumDecl> pending_enumerations_;
    std::vector<RecordDecl> pending_records_;
    struct RecordTag {
        bool is_union{};
        bool complete{};
        bool operator==(const RecordTag&) const = default;
    };
    std::unordered_map<std::string, RecordTag> record_types_;
    struct TagState {
        std::unordered_map<std::string, RecordTag> records;
        NameMap<LocalTag> local;
        NameMap<ValueBinding> values;
        std::size_t depth{};
    };
    TagState tag_state() const;
    void prepare_tag_destination(const Parser& destination);
    bool transfer_spliced_tags(Parser& child,
        const TagState& before,
        SourceLocation location);
    std::unordered_map<std::string, AliasDefinitionPtr> type_aliases_;
    struct DeclaredAlias {
        std::string name;
        AliasDefinitionPtr definition;
        std::size_t scope_depth{};
    };
    std::vector<DeclaredAlias> declared_aliases_;
    std::vector<StaticAssertDecl> static_assertions_;
    std::vector<Program::RequiredType> required_types_;
    std::unordered_set<const Expr*> header_type_bounds_;
    FunctionDecl* active_function_{};
    // Own a restored context's function metadata; never retain the original
    // mutable function/AST when parsing through a saved environment.
    std::unique_ptr<FunctionDecl> restored_function_context_;
    // Raw explicit-parse input starts here and then sees declarations made by
    // that parse. Opaque spliced children retain their own settled contexts.
    std::shared_ptr<const SyntaxContext> explicit_parse_context_;
    bool parsing_generic_argument_{};
    bool parsing_procedural_body_{};
    bool parsing_expansion_declaration_{};
    bool parsing_public_fragment_{};
    bool parsing_public_function_header_{};
    // Known parameter/attribute/generic groups can be opaque while declarator
    // binding is still recognized by the ordinary parser. These flags are
    // speculative only; no placeholder signature may leave a deferred capture.
    bool allow_public_header_deferral_{};
    bool public_deferred_header_{};
    bool public_header_uncertain_names_{};
    bool recording_public_tree_{};
    bool public_tree_failed_{};
    std::vector<std::size_t> public_uncertain_binding_depths_;
    std::vector<ProductionEvent> production_events_;
    std::vector<std::size_t> production_stack_;
    // Public-fragment token splitting must not move the owner's match boundary.
    std::vector<std::size_t> public_input_indices_;
    unsigned switch_depth_{};
    std::vector<bool> switch_default_seen_;
};

// Immutable once attached to a context. Types are detached graph copies, and
// syntax state retains only stable registry identity and lexical activations.
// Its weak executor link prevents contexts stored by expansion functions from
// keeping their owning executor alive through a reference cycle.
struct SyntaxParseEnvironment {
    friend struct SyntaxParseEnvironmentTestAccess;
    friend struct ProvenanceOwnershipTestAccess;
private:
    friend class Parser;
    friend class SyntaxExecution;
    friend std::uint64_t syntax_environment_storage(const SyntaxParseEnvironment&);
    friend bool syntax_environment_same_lookup(const SyntaxParseEnvironment&, const SyntaxParseEnvironment&);
    friend std::optional<SyntaxEntityId> syntax_resolve_entity(
        const SyntaxNode&, std::string_view, Diagnostics&, SourceLocation);
    // A namespace refinement shares its immutable lexical snapshot's identity.
    // Independent snapshots never compare equal merely by spelling or layout.
    struct Identity {};
    std::shared_ptr<const Identity> identity = std::make_shared<const Identity>();
    unsigned address_bits{};
    std::optional<SyntaxState> syntax;
    std::weak_ptr<SyntaxExecution> execution;
    std::size_t scope_imports{};
    std::vector<TokenIdentity> import_declarations;
    std::vector<std::string> generic_types;
    std::unordered_map<std::string, Parser::GenericFunction> functions;
    std::shared_ptr<Parser::OrdinaryNames> ordinary_values;
    std::vector<NameMap<ValueBinding>> values;
    std::vector<Parser::PrototypeScope> prototypes;
    std::vector<NameMap<AliasDefinitionPtr>> local_aliases;
    std::vector<NameMap<Parser::LocalTag>> local_tags;
    std::vector<Parser::ScopeAmbiguity> scope_ambiguities;
    std::shared_ptr<std::uint64_t> nominal_occurrence;
    std::vector<TokenIdentity> scope_origins;
    std::vector<TokenIdentity> scope_ends;
    std::vector<TokenRegion> import_regions;
    std::vector<FragmentNamespaceScope> fragment_namespaces;
    // A context's namespace lookup is independent of the tokens subsequently
    // parsed with it. Scope provenance retains its original marks/stream.
    std::shared_ptr<const FragmentNamespaceLookup> fragment_context;
    std::size_t scope_event_base{};
    std::unordered_map<std::string, AliasDefinitionPtr> aliases;
    std::unordered_map<std::string, Parser::EnumTag> enumerations;
    std::unordered_map<std::string, Parser::RecordTag> records;
    std::vector<ParameterDecl> parameters;
    std::vector<GenericParameter> generic_parameters;
    std::vector<Attribute::VariadicBinding> variadic_bindings;
    std::shared_ptr<const GenericTagOwner> generic_tag_owner;
    std::optional<std::size_t> generic_header_depth;
    std::shared_ptr<const FunctionScopeIdentity> function_scope;
    std::vector<std::size_t> uncertain_depths;
    bool function_context{};
    bool procedural_body{};
    std::shared_ptr<const SyntaxHeaderBindings> header_bindings;
    unsigned switch_depth{};
    std::vector<bool> switch_defaults;
    std::uint64_t storage{};
};

} // namespace cross
