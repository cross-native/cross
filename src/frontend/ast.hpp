// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/pointer_join.hpp"
#include "common/continuation_query.hpp"

#include "common/source.hpp"
#include "common/code_address.hpp"
#include "common/floating_semantics.hpp"
#include "common/uint128.hpp"
#include "frontend/token.hpp"
#include "frontend/name.hpp"
#include "frontend/nominal.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

namespace cross {

enum class BuiltinType {
    Void, Bool, I8, U8, I16, U16, I32, U32, I64, U64, I128, U128, Iptr, Uptr,
    F32, F64, F80, F128, Fptr, Label,
};

std::optional<BuiltinType> builtin_kind(std::string_view spelling);

struct Type;
struct Expr;
struct FunctionType;
using TypePtr = std::shared_ptr<Type>;

namespace detail {
class AstRelease;
using AstReleaseLink = OwnerReleaseLink;
} // namespace detail

struct Type {
    Type() = default;
    Type(const Type&) = default;
    Type(Type&&) = default;
    Type& operator=(const Type&) = default;
    Type& operator=(Type&&) = default;
    ~Type();
    enum class Kind {
        Builtin,
        Pointer,
        Generic,
        Vector,
        Array,
        Record,
        Function,
        // Translation-only; never assigned a runtime layout or ABI channel.
        Tokens,
        SyntaxMatch,
        Syntax,
        Span,
        Context,
        Bytes,
        Buffer
    } kind{Kind::Builtin};
    BuiltinType builtin{BuiltinType::Void};
    TypePtr pointee;
    TypePtr element;
    std::shared_ptr<FunctionType> function;
    std::uint32_t lanes{};
    // Retained required source bound, never a VLA. Positive lanes caches its
    // resolved extent; the expression remains available for source validation.
    std::shared_ptr<const Expr> array_bound{};
    // A provisional lexical type used only while checking a translation-only
    // definition. This is neither a VLA nor a guessed extent, and never enters
    // runtime layout. Copies must retain the reason its inferred size is unknown.
    enum class ArrayExtentDependency { None, ExpansionContext };
    ArrayExtentDependency array_extent_dependency{ArrayExtentDependency::None};
    enum class VectorBoundUnit { Lanes, Bytes };
    // Required source expression, retained through generic substitution. A
    // zero lane count means construction has not yet been resolved.
    std::shared_ptr<const Expr> vector_bound{};
    VectorBoundUnit vector_bound_unit{VectorBoundUnit::Lanes};
    enum class VectorExtentDependency { None, ExpansionContext };
    VectorExtentDependency vector_extent_dependency{VectorExtentDependency::None};
    // Minimum alignment requested by `aligned` on a typedef of this type: the
    // largest resolved request, or zero. Requests whose required constant is
    // not resolved yet stay in `alignment_requests`. Values ignore both.
    unsigned alignment{};
    std::vector<std::shared_ptr<const Expr>> alignment_requests{};
    bool scalable{};
    std::string generic_name;
    SourceLocation generic_location;
    ValueBinding generic_binding;
    // A written shared specifier is interpreted in each owning declarator,
    // unlike an already parsed type splice. Cleared when the header binds it.
    bool generic_header_view{};
    std::string nominal_name;
    std::shared_ptr<const NominalTypeIdentity> nominal_identity{};
    [[nodiscard]] NominalTypeKey nominal_key() const { return {nominal_name, nominal_identity}; }
    bool is_union{};
    bool is_const{};
    bool is_volatile{};
    bool is_atomic{};
    bool is_restrict{};
    // Accesses through this type may alias incompatible effective types. Like
    // an alias, the attribute does not make a distinct type.
    bool may_alias{};
    // Only pointer types carry an address-space number. Zero is the ordinary
    // generic address space; the source location supports target diagnostics.
    std::uint32_t address_space{};
    SourceLocation address_space_location;
    // Parser-only qualifier waiting for a grouped pointer declarator's `*`.
    std::optional<std::pair<std::uint32_t, SourceLocation>>
        pending_address_space;
    CapturedTypeErrors captured_errors;
    // Kept separately so copying a repaired tag declaration can rebind its
    // definition obligations without dropping qualifiers on this type use.
    std::shared_ptr<const CapturedTypeErrors> captured_tag_errors{};
private:
    friend class detail::AstRelease;
    friend class detail::OwnerRelease;
    mutable detail::AstReleaseLink teardown_;
};

inline NameKey generic_type_key(const Type& type) {
    NameKey key(type.generic_name, type.generic_location);
    key.bind(type.generic_binding);
    return key;
}

TypePtr builtin_type(BuiltinType kind, bool is_const = false,
                     bool is_volatile = false, bool is_atomic = false);
TypePtr tokens_type();
TypePtr syntax_match_type();
TypePtr syntax_type();
TypePtr span_type();
TypePtr context_type();
// Copy a mutable type graph without sharing nested callable/element state.
// Preserve graph sharing inside the copy, including any recursive edges.
TypePtr copy_type(const TypePtr& type);
bool has_pending_type_bound(const TypePtr& type);
std::span<const std::string_view> core_keyword_names();
bool is_reserved_identifier(std::string_view name);
TypePtr bytes_type();
TypePtr buffer_type();
TypePtr pointer_type(TypePtr pointee, bool is_const = false,
                     bool is_volatile = false, bool is_atomic = false);
TypePtr generic_type(std::string name, bool is_const = false,
                     bool is_volatile = false, bool is_atomic = false);
TypePtr vector_type(TypePtr element, std::uint32_t lanes, bool scalable = false,
                    bool is_const = false, bool is_volatile = false,
                    bool is_atomic = false);
TypePtr array_type(TypePtr element, std::uint32_t elements,
                   bool is_const = false, bool is_volatile = false);
TypePtr record_type(std::string name, bool is_union = false,
                    bool is_const = false, bool is_volatile = false);
TypePtr enum_type(std::string name, BuiltinType underlying,
                  bool is_const = false, bool is_volatile = false,
                  bool is_atomic = false);
std::string type_name(const TypePtr& type);
// Stable structural spelling supplied to model DSLs. Qualifiers and pointers
// are prefix-coded (K, V, P), so recipes can rewrite leaf type names without
// compiler-owned mangling decisions.
std::string canonical_type_name(const TypePtr& type);
bool same_type(const TypePtr& left, const TypePtr& right);
bool has_context_dependent_type_bound(const TypePtr& type);
enum class TypeComparison { Different, Same, DeferredBound };
// Exact source identity, except for extents proven to need expansion context.
// All independently known qualifiers, nominal and callable contracts still match.
TypeComparison compare_source_types(const TypePtr& left, const TypePtr& right);
// Generic declaration comparison preserves the complete surrounding type and
// callable contract, deferring retained unresolved extents to used instances.
TypeComparison compare_generic_types(const TypePtr& left, const TypePtr& right);
// Pointee compatibility for qualification-preserving object/void conversions.
// Source validation may defer only an expansion-dependent extent comparison;
// execution and lowering require the strict boolean form below.
enum class PointeeCompatibility { Incompatible, Compatible, DeferredExtent };
PointeeCompatibility compare_pointee(const TypePtr& source, const TypePtr& destination,
                                     unsigned depth = 0, bool nested_qualification = true);
bool compatible_pointee(const TypePtr& source, const TypePtr& destination,
                        unsigned depth = 0, bool nested_qualification = true);
PointerJoinResult<TypePtr> common_pointer_type(const TypePtr& left, const TypePtr& right,
                                              const AddressSpaceJoin& spaces = {});
// Array/vector object qualifiers apply to their elements, not to a pointer
// used to reach the object. The returned type does not mutate the container.
TypePtr qualified_element_type(const TypePtr& container);
bool is_integer(const TypePtr& type);
bool is_floating(const TypePtr& type);
bool is_scalar(const TypePtr& type);
bool is_meta_type(const TypePtr& type);
bool is_vector(const TypePtr& type);
bool is_nominal(const TypePtr& type);
unsigned type_bits(const TypePtr& type);
// Natural storage of builtin scalars and fixed vectors, shared by target layout
// and translation-time evaluation. Void has no storage.
std::optional<std::uint64_t> builtin_storage_size(BuiltinType type, unsigned address_bits,
                                                  unsigned f80_storage_bytes);
std::uint64_t natural_storage_alignment(std::uint64_t size, bool f80,
                                        unsigned alignment_limit, unsigned f80_alignment);
struct StorageLayout {
    std::uint64_t size{};
    std::uint64_t alignment{1};
};
// An object of a type requesting `requested` alignment over its natural
// layout takes the larger alignment and rounds its size up to it, so array
// elements and members keep that alignment. Empty when the size overflows.
std::optional<StorageLayout> requested_storage(StorageLayout natural, std::uint64_t requested);

enum class Linkage { Group, Static, Global };
enum class ParameterMode { In, Out, InOut };

// The top-level const of an in parameter and the requested alignment of any
// parameter qualify its local cell, not the callable boundary. Preserve all
// nested and non-const qualifiers.
TypePtr callable_parameter_type(const TypePtr& type, ParameterMode mode);
// A copy of the type without its own requested (typedef) alignment.
TypePtr without_alignment(const TypePtr& type);
// A result's requested alignment qualifies only the object receiving the
// value; callable identity uses the result's base type.
TypePtr callable_result_type(const TypePtr& type);

struct Expr;
struct ObjectDecl;
struct FunctionDecl;
// A compile-time address names source entities until HIR assigns their IDs.
// Keeping declaration identity prevents substitution from rebinding a static
// object in the generic definition's namespace or source unit.
struct AddressConstant {
    enum class Kind { Absolute, Object, Function } kind{Kind::Absolute};
    UInt128 absolute;
    const ObjectDecl* object{};
    const FunctionDecl* function{};
    std::int64_t addend{};
    bool operator==(const AddressConstant&) const = default;
};
struct GenericParameter {
    std::string name;
    TypePtr value_type;
    SourceLocation location{};
    ValueBinding binding{};
};

struct Attribute {
    std::string name;
    std::vector<std::string> arguments;
    SourceLocation location;
    // Structured when an attribute argument is a required constant
    // expression. Other attributes retain their original token spelling.
    std::shared_ptr<Expr> expression_argument{};
    // Source types/binders survive generic substitution and required queries;
    // the selected target model validates each named state's exact interface.
    struct VariadicBinding {
        SourceLocation location;
        std::string name;
        TypePtr type;
        std::string state;
        ValueBinding binding{};
    };
    std::vector<VariadicBinding> variadic_bindings{};
};

struct Expr {
    Expr() = default;
    Expr(Expr&&) = default;
    Expr& operator=(Expr&&) = default;
    Expr(const Expr&) = delete;
    Expr& operator=(const Expr&) = delete;
    ~Expr();

    struct GenericArgument {
        TypePtr type;
        std::unique_ptr<Expr> value;
    };

    enum class Kind {
        Integer, Floating, String, Character, Name, Unary, Binary, Assign,
        Conditional, Call, Parenthesized, Cast, Sizeof, Alignof,
        // `$::offsetof`: the record in `type`; its member designator is the
        // designator list of one initializer entry without a value.
        Offsetof,
        AggregateInitializer, Address, Quote, ByteSequence,
        // Internal result of successfully evaluating a void expression.
        // It has no scalar bits and no source-token spelling.
        VoidValue,
    } kind{Kind::Integer};
    std::shared_ptr<const NameLookupContext> name_context;
    // Token constructors retain their lexical site, including block imports
    // and syntax activations. The evaluator supplies the expansion identity.
    std::shared_ptr<const SyntaxContext> translation_context;
    struct InitializerDesignator {
        enum class Kind { Member, Index } kind{Kind::Member};
        SourceLocation location;
        std::string member;
        std::unique_ptr<Expr> index;
        std::shared_ptr<const FreshIdentifier> member_fresh;
        [[nodiscard]] MemberName member_name() const { return {member, member_fresh}; }
    };
    struct InitializerEntry {
        SourceLocation location;
        std::vector<InitializerDesignator> designators;
        std::unique_ptr<Expr> value;
    };
    SourceLocation location;
    std::string text;
    std::string string_value;
    struct IntegerConstant {
        UInt128 value;
        BuiltinType type;
    };
    std::optional<IntegerConstant> evaluated_integer;
    struct FloatingConstant {
        UInt128 bits;
        BuiltinType type;
    };
    std::optional<FloatingConstant> evaluated_floating;
    std::optional<AddressConstant> evaluated_address;
    struct ObjectRelocation {
        std::uint64_t offset{};
        std::uint64_t length{};
        TypePtr type;
        std::variant<AddressConstant, LabelAddressConstant> address;
    };
    // Evaluated object images retain symbolic addresses, never host pointer bits.
    std::vector<ObjectRelocation> object_relocations;
    // A source type operand retained for casts and type-form sizeof.  Keeping
    // this structured avoids reparsing a textual type in HIR/MIR lowering.
    TypePtr type;
    // Source-only callable view for a generic designator whose actual extent
    // or value argument needs expansion context. Never a concrete instance.
    TypePtr deferred_generic_signature;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    std::unique_ptr<Expr> third;
    std::vector<std::unique_ptr<Expr>> arguments;
    // Quote literals alternate with token-valued unquotes in arguments.
    // Literal token sequences retain definition spans separately from splices.
    // A `$::meta::parse` or `$::meta::token` call instead keeps the block-scope
    // typedef and tag names visible there, bound as a quote binds them.
    std::vector<TokenSequence> quote_fragments;
    std::vector<GenericArgument> generic_arguments;
    std::vector<InitializerEntry> initializer_entries;
    // Whether a generic declaration was visible when this call or explicit
    // generic function-address application was parsed.
    // Semantic expansion must not turn a later/group-only definition into an
    // implicit declaration.
    bool generic_visible_at_call{};
    // The label named by `name<arguments>::label` in that generic instance.
    std::string instance_label;
private:
    friend class detail::AstRelease;
    friend class detail::OwnerRelease;
    mutable detail::AstReleaseLink teardown_;
};

inline NameKey name_key(const Expr& expression) {
    NameKey result(expression.text, expression.location);
    result.bind(expression.name_context ? expression.name_context->value_binding
                                       : token_origin(expression.location).value_binding);
    return result;
}

inline NameUse::NameUse(const Expr& expression)
    : spelling(expression.text), location(expression.location),
      context(expression.name_context.get()) {
    const auto* node = &expression;
    while (node->kind == Expr::Kind::Parenthesized && node->left) node = node->left.get();
    spelling = node->text;
    location = node->location;
    context = node->name_context.get();
}

inline void bind_exact_name(Expr& expression, std::string name) {
    expression.text = std::move(name);
    auto context = std::make_shared<NameLookupContext>();
    context->kind = NameLookupContext::Kind::Exact;
    expression.name_context = std::move(context);
}

struct VariableDecl {
    SourceLocation location;
    std::string name;
    ValueBinding binding;
    TypePtr type;
    // Present only when the outermost array bound is evaluated at block
    // entry. A zero lane count on `type` marks that dynamic outer bound.
    std::unique_ptr<Expr> dynamic_array_bound;
    std::unique_ptr<Expr> initializer;
    bool storage_register{};
    bool storage_stack{};
    bool storage_static{};
    std::vector<Attribute> attributes;
    unsigned explicit_alignment{1};
    std::optional<std::string> location_name;
};

struct Statement {
    Statement() = default;
    Statement(Statement&&) = default;
    Statement& operator=(Statement&&) = default;
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    ~Statement();
    enum class Kind {
        Compound, DeclarationList, Declaration, Expression, Return, If, Switch, Case, Default,
        While, DoWhile, For, Break, Continue, Label, Goto, StaticAssert, Empty,
    } kind{Kind::Empty};
    SourceLocation location;
    std::vector<std::unique_ptr<Statement>> statements;
    // DeclarationList is ordered, like Compound, but introduces no scope.
    std::unique_ptr<VariableDecl> declaration;
    std::unique_ptr<Expr> expression;
    std::unique_ptr<Expr> condition;
    // The increment clause of a for statement, evaluated left to right.
    std::vector<std::unique_ptr<Expr>> increments;
    std::unique_ptr<Statement> first;
    std::unique_ptr<Statement> second;
    std::string assertion_message;
    std::string label_name;
    SourceLocation label_location;
    LabelBinding label_binding;
    std::shared_ptr<const FreshIdentifier> label_fresh;
    std::vector<Attribute> attributes;
    bool global_label{};
private:
    // Detached owned statements only; never part of source identity/layout.
    Statement* teardown_next_{};
};

// The source form of a mandatory tail return is a call, allowing only
// redundant parentheses around it. This does not establish ABI legality.
inline const Expr* returned_call(const Statement& statement) {
    if (statement.kind != Statement::Kind::Return) return nullptr;
    const auto* expression = statement.expression.get();
    while (expression && expression->kind == Expr::Kind::Parenthesized && expression->left)
        expression = expression->left.get();
    return expression && expression->kind == Expr::Kind::Call ? expression : nullptr;
}

struct ParameterDecl {
    SourceLocation location;
    std::string name;
    TypePtr type;
    ParameterMode mode{ParameterMode::In};
    bool explicit_mode{};
    std::optional<std::string> location_name;
    // Written array parameters adjust to pointers only after the complete
    // declarator. Keep their source type for required-bound validation.
    TypePtr declared_array_type;
    ValueBinding binding{};
};

// Source-level callable identity. Names and source locations aid diagnostics;
// parameter modes, types, endpoints, variadicness, ABI, clobbers, and cleanup
// ownership determine compatibility.
struct FunctionType {
    FunctionType() = default;
    FunctionType(const FunctionType&) = default;
    FunctionType(FunctionType&&) = default;
    FunctionType& operator=(const FunctionType&) = default;
    FunctionType& operator=(FunctionType&&) = default;
    ~FunctionType();
    TypePtr result;
    std::vector<ParameterDecl> parameters;
    bool variadic{};
    std::string abi;
    std::optional<std::string> result_location;
    std::vector<std::string> clobbers;
    std::optional<std::string> stack_cleanup;
};

TypePtr function_type(TypePtr result, std::vector<ParameterDecl> parameters,
                      bool variadic = false, std::string abi = {});

struct StaticAssertDecl {
    SourceLocation location;
    std::string source_namespace;
    std::unique_ptr<Expr> condition;
    std::string message;
    // A declaration-time block assertion may inspect local types, never local
    // runtime values. Retain the concrete lexical owner without borrowing AST.
    std::shared_ptr<const FunctionScopeIdentity> lexical_function{};
    std::vector<std::pair<NameKey, TypePtr>> local_types{};
};

struct TypeRequirement {
    TypePtr type;
    TypePtr compatible_with;
    SourceLocation location;
    std::string source_namespace;
    std::string name;
    enum class Kind { Alias, GenericInterface } kind{Kind::Alias};
};

struct FunctionDecl {
    using GenericParameter = cross::GenericParameter;

    SourceLocation location;
    std::string name;
    ValueBinding binding;
    std::shared_ptr<const FreshIdentifier> fresh;
    std::string source_namespace;
    std::string source_unit;
    std::vector<std::string> imports;
    TypePtr return_type;
    // Definition context for quotation performed by an ordinary evaluated helper.
    std::shared_ptr<const SyntaxContext> translation_context;
    std::vector<ParameterDecl> parameters;
    std::vector<Attribute> attributes;
    std::vector<GenericParameter> generic_parameters;
    std::shared_ptr<const GenericTagOwner> generic_tag_owner{};
    // Records and enumerations defined by a generic header, in definition order.
    std::vector<NominalTypeKey> header_types;
    std::shared_ptr<const FunctionScopeIdentity> function_scope{
        std::make_shared<const FunctionScopeIdentity>()};
    std::vector<StaticAssertDecl> deferred_static_assertions;
    // Typedefs have no runtime declaration, but their type constraints must
    // survive even when the alias is never used.
    std::vector<TypeRequirement> required_types;
    std::optional<std::string> result_location;
    std::unique_ptr<Statement> body;
    Linkage linkage{Linkage::Group};
    bool variadic{};
    bool inline_hint{};
    // Created while preparing an invocation, not an emitted-use obligation.
    // Surviving runtime references promote it through the ordinary call graph.
    bool invocation_specialization{};
    // A concrete instance of a generic function.
    bool generic_instance{};

    [[nodiscard]] bool definition() const { return body != nullptr; }
    [[nodiscard]] const Attribute* attribute(std::string_view name) const;
    [[nodiscard]] bool has_meta_signature() const;
};

struct ObjectDecl {
    SourceLocation location;
    std::string name;
    ValueBinding binding;
    std::shared_ptr<const FreshIdentifier> fresh;
    std::string source_unit;
    // Lifting a block static changes storage, not its lexical function (or
    // concrete generic instance). Resolve this identity in the owning Program;
    // do not borrow an AST pointer across expansion snapshots or helper erasure.
    std::shared_ptr<const FunctionScopeIdentity> lexical_function;
    TypePtr type;
    std::vector<Attribute> attributes;
    std::unique_ptr<Expr> initializer;
    Linkage linkage{Linkage::Group};
};

struct EnumDecl {
    struct Enumerator {
        SourceLocation location;
        std::string name;
        std::unique_ptr<Expr> initializer;
        std::optional<Expr::IntegerConstant> value;
        ValueBinding binding{};
    };

    SourceLocation location;
    std::string name;
    BuiltinType underlying{BuiltinType::I32};
    std::vector<Attribute> attributes;
    std::vector<Enumerator> enumerators;
    std::shared_ptr<const NominalTypeIdentity> nominal_identity{};
    std::shared_ptr<const CapturedTypeErrors> captured_type_errors{};
    // Lexical enumerator scope is independent of whether the tag has a private
    // identity: an anonymous file-scope enumeration also has such an identity.
    bool local{};
    // Brought by generated code like a carried record (see RecordDecl).
    bool carried{};
    [[nodiscard]] NominalTypeKey nominal_key() const { return {name, nominal_identity}; }
};

struct RecordMemberDecl {
    SourceLocation location;
    std::string name;
    TypePtr type;
    std::unique_ptr<Expr> bit_width;
    std::vector<Attribute> attributes;
    std::shared_ptr<const FreshIdentifier> fresh;
    [[nodiscard]] MemberName member_name() const { return {name, fresh}; }
};

struct RecordDecl {
    SourceLocation location;
    std::string name;
    bool is_union{};
    bool complete{};
    std::vector<Attribute> attributes;
    std::vector<RecordMemberDecl> members;
    std::shared_ptr<const NominalTypeIdentity> nominal_identity{};
    // A copy that generated code brought from the helper defining the record;
    // the program may also hold the original.
    bool carried{};
    [[nodiscard]] NominalTypeKey nominal_key() const { return {name, nominal_identity}; }
    [[nodiscard]] const Attribute* attribute(std::string_view name) const;
};

TypePtr record_type(const RecordDecl& declaration);
TypePtr enum_type(const EnumDecl& declaration);

struct GlobalLabelDecl {
    SourceLocation location;
    std::string qualified_name;
    std::shared_ptr<const FreshIdentifier> owner_fresh;
    std::shared_ptr<const FreshIdentifier> label_fresh;
    std::vector<Attribute> attributes;
};

struct EvaluationLimits {
    std::uint64_t bytes{16 * 1024 * 1024};
    std::uint64_t memory{64 * 1024 * 1024};
    std::uint64_t steps{1000000};
    unsigned depth{256};
    std::uint64_t generic_instances{4096};
    unsigned generic_depth{128};
};

enum class EvaluationByteOrder { Little, Big };

struct EvaluationLayout {
    EvaluationByteOrder byte_order{EvaluationByteOrder::Little};
    unsigned natural_alignment_limit{1};
    unsigned f80_storage_bytes{10};
    unsigned f80_alignment{1};
    CodeAddressRepresentation code_addresses{CodeAddressRepresentation::Opaque};
    // Signed +, -, *, and << wrap modulo 2^N, as at runtime (-fwrapv).
    bool wrap_signed{};
    // The profile's floating environment and the target's NaN encoding, as
    // at runtime.
    floating::Environment floating_environment{};
    floating::NanEncoding nan_encoding{floating::NanEncoding::Ieee2008};
};

struct EvaluationMemberLayout {
    std::uint64_t offset{};
    unsigned alignment{1};
    std::optional<unsigned> bit_width;
    unsigned bit_offset{};
};

struct EvaluationInitializerItem {
    const Expr* expression{};
    TypePtr type;
    EvaluationMemberLayout layout;
};

struct EvaluationInitializerPlan {
    std::vector<EvaluationInitializerItem> items;
    // Required outer extent when the destination array has an omitted/runtime bound.
    std::uint64_t minimum_elements{};
    bool valid{true};
    SourceLocation error_location;
    std::string error_message;
};

struct EvaluationInitializerTypePlan {
    std::vector<std::pair<const Expr*, TypePtr>> items;
    std::uint64_t minimum_elements{};
    bool outer_extent_deferred{};
    bool valid{true};
    SourceLocation error_location;
    std::string error_message;
};

struct RelocationAddend;

struct EvaluationInstructionFormId {
    std::size_t value{};
    friend bool operator==(EvaluationInstructionFormId, EvaluationInstructionFormId) = default;
};

struct EvaluationPatchValueCapabilities {
    // Null means no sink. The target supplies the exact required source type;
    // source validation does not assume an integer or a native address width.
    TypePtr address_sink_type;
    bool symbol_relocation{};
    std::optional<EvaluationInstructionFormId> instruction_form{};
};

// Source facts only: no physical allocation, emitted address, or ABI transport.
// Form IDs belong to the selected target's instruction registry.
struct EvaluationInstructionOperand {
    enum class Kind { Invalid, Register, Memory, Label, Immediate, Patch, Deferred } kind{Kind::Invalid};
    TypePtr type;
    unsigned bits{};
    std::string fixed_register;
    bool floating{};
    bool writable{};
    bool deferred{};
    std::optional<Expr::IntegerConstant> integer;
    unsigned integer_bits{};
    bool integer_signed{};
    std::vector<EvaluationInstructionFormId> patch_forms;
    bool label_same_function{};
    struct Memory {
        enum class Shape { Other, Dereference, Index } shape{Shape::Other};
        struct Register {
            TypePtr type;
            unsigned bits{};
            std::string fixed;
            bool object{};
            bool integer{};
        } base, index;
        std::optional<Expr::IntegerConstant> constant_index;
        unsigned constant_bits{};
        bool constant_signed{};
        bool index_deferred{};
        std::optional<std::uint64_t> element_bytes;
        bool layout_deferred{};
    } memory;
};

class Diagnostics;
using EvaluationLayoutQuery = ContinuationQuery<std::optional<std::uint64_t>(const TypePtr&)>;
enum class EvaluationIntegerContext {
    Definition,       // No expansion capabilities; every failure diagnoses.
    ProbeDefinition,  // Optional context-free proof; only resource failure escapes.
    StagedDefinition, // Still no capabilities; missing context is a typed outcome.
    CallerInvocation, // Explicitly borrow capabilities for a real lexical owner.
};
enum class EvaluationLayoutKind { Complete, Alignment };

// Host-only ownership of one evaluator's prepared private record graph. Nested
// invocation proofs may share it; definition-only probes and fresh invocations
// must not. This has no source representation, runtime layout or ABI channel.
struct EvaluationLayoutScopeIdentity final {};
struct EvaluationIntegerResult {
    enum class Status { Value, ContextUnavailable, Invalid } status{Status::Invalid};
    std::optional<Expr::IntegerConstant> value;
};
using EvaluationIntegerQuery = ContinuationQuery<EvaluationIntegerResult(
    const Expr&, Diagnostics&, const EvaluationLayoutQuery&, const EvaluationLayoutQuery&,
    std::string_view, const FunctionDecl*, std::span<const std::pair<NameKey, TypePtr>>,
    EvaluationIntegerContext)>;

struct EvaluationGenericValueResult {
    using Status = EvaluationIntegerResult::Status;
    Status status{Status::Invalid};
    // A normalized typed integer, symbolic address or label, never meta storage.
    std::unique_ptr<Expr> value;
};
using EvaluationGenericValueQuery = ContinuationQuery<EvaluationGenericValueResult(
    const Expr&, const TypePtr&, Diagnostics&, const FunctionDecl*,
    std::span<const std::pair<NameKey, TypePtr>>, EvaluationIntegerContext)>;
using EvaluationPointerQuery = ContinuationQuery<bool(std::unique_ptr<Expr>&,
    const TypePtr&, const FunctionDecl*, std::span<const NameKey>)>;

struct Program;

// Positions of published record declarations by nominal key, never a private
// view or a constraint result. A lookup tests completeness and returns the
// first complete definition; a change in the table's size or storage rebuilds
// the index.
class RecordSourceIndex {
public:
    const RecordDecl* definition(const Program& program, const NominalTypeKey& key);
private:
    const Program* program_{};
    const RecordDecl* records_{};
    std::size_t count_{};
    std::unordered_map<NominalTypeKey, std::vector<const RecordDecl*>, NominalTypeKeyHash> definitions_;
};

struct Program {
    using RequiredType = TypeRequirement;
    struct EnumerationPosition {
        std::size_t declaration{};
        std::size_t enumerator{};
    };
    unsigned address_bits{64};
    EvaluationLimits evaluation_limits;
    EvaluationLayout evaluation_layout;
    // Dynamically scoped required-value bridge. Layout may reenter evaluation;
    // the active evaluator retains invocation capabilities and shared budgets,
    // while the requesting declaration supplies lexical ownership and types only.
    EvaluationIntegerQuery evaluation_required_integer;
    EvaluationGenericValueQuery evaluation_generic_value;
    // Monotonic typed failure sequence, not a work budget or a sticky failure.
    // Expansion parsing and semantic preparation share this channel while the
    // failing evaluator/matcher retains its precise diagnostic and ancestry.
    std::uint64_t evaluation_resource_errors{};
    // Semantic service supplied by the resolved model. An empty spelling means
    // that model's default callable ABI, not a compiler-owned default profile.
    std::function<std::optional<std::string>(std::string_view)> canonical_callable_abi;
    // Installed only while semantic instantiation is active. A reached target
    // layout dependency may need its required expressions prepared first.
    ContinuationQuery<bool(const TypePtr&, EvaluationLayoutKind)> evaluation_prepare_type;
    ContinuationQuery<bool(FunctionDecl&)> evaluation_prepare_function;
    ContinuationQuery<bool(std::unique_ptr<Expr>&, const FunctionDecl*,
        std::span<const std::pair<NameKey, TypePtr>>)> evaluation_prepare_expression;
    // Optional demand-driven preparation for early expansion evaluation. The
    // active position keeps later enumerators unavailable to an initializer.
    ContinuationQuery<bool(EnumerationPosition)> evaluation_prepare_enumerator;
    std::optional<EnumerationPosition> evaluation_enumerator_position;
    // Installed after semantic declarations are available; the target owns
    // nominal layout even when evaluation precedes final HIR lowering.
    EvaluationLayoutQuery evaluation_size_of;
    EvaluationLayoutQuery evaluation_align_of;
    // Target capability, not an atomic access. Installed with layout queries
    // for both early expansion and ordinary required evaluation.
    ContinuationQuery<std::optional<bool>(const TypePtr&)>
        evaluation_atomic_is_lock_free;
    // Generic value materializers only. A complete direct machine operand is
    // governed by its selected instruction field, not this capability query.
    std::function<std::optional<EvaluationPatchValueCapabilities>(const TypePtr&)>
        evaluation_patch_value_capabilities;
    // Representability of a proven mathematical symbolic displacement. The
    // source checker must not infer this range from the pointer or host width.
    std::function<bool(const RelocationAddend&)> evaluation_relocation_addend;
    // Registry-owned source-type legality, including unused expansion bodies.
    std::function<std::optional<std::string>(std::uint32_t)> evaluation_address_space_type_error;
    // Necessary candidate-form check; final selection must also validate all
    // the other operands using target-owned register/memory constraints.
    std::function<std::vector<EvaluationPatchValueCapabilities>(
        std::string_view, std::size_t, std::size_t, const TypePtr&)>
        evaluation_patch_operand_capabilities;
    std::function<std::optional<std::string>(std::string_view,
        std::span<const EvaluationInstructionOperand>)> evaluation_instruction_source;
    ContinuationQuery<std::optional<EvaluationMemberLayout>(
        const TypePtr&, const MemberName&)> evaluation_member_layout;
    ContinuationQuery<EvaluationInitializerPlan(const Expr&, const TypePtr&)>
        evaluation_initializer_plan;
    ContinuationQuery<EvaluationInitializerTypePlan(const Expr&, const TypePtr&,
        std::span<const Expr* const>)> evaluation_initializer_types;
    // An active evaluator may supply a private prepared view of an existing
    // nominal definition. Null means no override, not an incomplete record.
    // This is lookup only: preparing values/publishing declarations is separate.
    // Providers retain returned views for their scope; consumers that keep
    // descriptor pointers beyond a query must retain the returned ownership.
    std::function<std::shared_ptr<const RecordDecl>(const NominalTypeKey&)>
        evaluation_record_definition;
    std::shared_ptr<const RecordDecl> record_definition(const NominalTypeKey& key) const;
    // Resolve only the required facts in private evaluator state. False means
    // the active evaluator owns the failure (including unavailable context).
    ContinuationQuery<bool(const TypePtr&, EvaluationLayoutKind)> evaluation_prepare_layout;
    std::shared_ptr<const EvaluationLayoutScopeIdentity> evaluation_layout_scope;
    EvaluationPointerQuery evaluation_pointer_resolver;
    std::vector<RecordDecl> records;
    // Serves record_definition and structural record checks. Code that removes
    // records resets it: a later append could restore the size it compares.
    mutable RecordSourceIndex record_index;
    // These owners undergo source validation before erasure. Their local
    // records are laid out on demand, not forced by the eager runtime pass.
    std::vector<std::shared_ptr<const FunctionScopeIdentity>> translation_only_record_scopes;
    std::vector<EnumDecl> enumerations;
    std::vector<StaticAssertDecl> static_assertions;
    std::vector<RequiredType> required_types;
    std::vector<GlobalLabelDecl> global_labels;
    std::vector<std::unique_ptr<FunctionDecl>> functions;
    // Source-only helpers remain available to required layout/value proofs
    // after runtime rewriting. They are not callable runtime symbols and are
    // excluded from runtime HIR, string lifting, inlining and code emission.
    std::vector<std::unique_ptr<FunctionDecl>> evaluation_definitions;
    // Lexical owners of local types in macro/expander definitions. These owned
    // snapshots are not ordinary callable symbols and never enter runtime HIR.
    std::vector<std::unique_ptr<FunctionDecl>> expansion_definitions;
    std::vector<std::unique_ptr<ObjectDecl>> objects;
    // Source-unit identities in command-line order; they order emission.
    std::vector<std::string> source_units;
    // Next ordinal of a lifted literal object in each source unit, so that
    // its internal name does not depend on other units.
    std::unordered_map<std::string, std::uint64_t> literal_ordinals;
};

std::string encode_link_name(std::string_view qualified_name, bool label = false,
                             std::string_view mangling_name = "default");
std::optional<std::string> decode_string_literal(std::string_view text);
std::optional<std::uint32_t> decode_character_literal(std::string_view text);

} // namespace cross
