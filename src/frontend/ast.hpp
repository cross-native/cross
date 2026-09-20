// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/source.hpp"
#include "common/uint128.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace cross {

enum class BuiltinType {
    Void, Bool, I8, U8, I16, U16, I32, U32, I64, U64, I128, U128, Iptr, Uptr,
    F32, F64, F80, F128, Fptr, Label,
};

struct Type;
struct FunctionType;
using TypePtr = std::shared_ptr<Type>;

struct Type {
    enum class Kind {
        Builtin,
        Pointer,
        Generic,
        Vector,
        Array,
        Record,
        Function
    } kind{Kind::Builtin};
    BuiltinType builtin{BuiltinType::Void};
    TypePtr pointee;
    TypePtr element;
    std::shared_ptr<FunctionType> function;
    std::uint32_t lanes{};
    bool scalable{};
    std::string generic_name;
    std::string nominal_name;
    bool is_union{};
    bool is_const{};
    bool is_volatile{};
    bool is_atomic{};
    bool is_restrict{};
    // Only pointer types carry an address-space number. Zero is the ordinary
    // generic address space; the source location supports target diagnostics.
    std::uint32_t address_space{};
    SourceLocation address_space_location;
    // Parser-only qualifier waiting for a grouped pointer declarator's `*`.
    std::optional<std::pair<std::uint32_t, SourceLocation>>
        pending_address_space;
};

TypePtr builtin_type(BuiltinType kind, bool is_const = false,
                     bool is_volatile = false, bool is_atomic = false);
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
bool is_integer(const TypePtr& type);
bool is_floating(const TypePtr& type);
bool is_scalar(const TypePtr& type);
bool is_vector(const TypePtr& type);
bool is_nominal(const TypePtr& type);
unsigned type_bits(const TypePtr& type);

enum class Linkage { Group, Static, Global };
enum class ParameterMode { In, Out, InOut };

struct Expr;
struct GenericParameter {
    std::string name;
    TypePtr value_type;
};

struct Attribute {
    std::string name;
    std::vector<std::string> arguments;
    SourceLocation location;
    // Structured when an attribute argument is a required constant
    // expression. Other attributes retain their original token spelling.
    std::shared_ptr<Expr> expression_argument{};
    // Parameter declarations are parsed with the ordinary type/declarator
    // grammar; their spelling is never reparsed by semantic expansion.
    std::vector<GenericParameter> generic_parameters{};
};

struct Expr {
    struct GenericArgument {
        TypePtr type;
        std::unique_ptr<Expr> value;
    };

    enum class Kind {
        Integer, Floating, String, Character, Name, Unary, Binary, Assign,
        Conditional, Call, Parenthesized, Cast, Sizeof, Alignof,
        AggregateInitializer,
    } kind{Kind::Integer};
    struct InitializerDesignator {
        enum class Kind { Member, Index } kind{Kind::Member};
        SourceLocation location;
        std::string member;
        std::unique_ptr<Expr> index;
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
    // A source type operand retained for casts and type-form sizeof.  Keeping
    // this structured avoids reparsing a textual type in HIR/MIR lowering.
    TypePtr type;
    std::unique_ptr<Expr> left;
    std::unique_ptr<Expr> right;
    std::unique_ptr<Expr> third;
    std::vector<std::unique_ptr<Expr>> arguments;
    std::vector<GenericArgument> generic_arguments;
    std::vector<InitializerEntry> initializer_entries;
};

struct VariableDecl {
    SourceLocation location;
    std::string name;
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
    enum class Kind {
        Compound, Declaration, Expression, Return, If, Switch, Case, Default,
        While, DoWhile, For, Break, Continue, Label, Goto, Empty,
    } kind{Kind::Empty};
    SourceLocation location;
    std::vector<std::unique_ptr<Statement>> statements;
    std::unique_ptr<VariableDecl> declaration;
    std::unique_ptr<Expr> expression;
    std::unique_ptr<Expr> condition;
    std::unique_ptr<Expr> increment;
    std::unique_ptr<Statement> first;
    std::unique_ptr<Statement> second;
    std::string label_name;
    std::vector<Attribute> attributes;
    bool global_label{};
};

struct ParameterDecl {
    SourceLocation location;
    std::string name;
    TypePtr type;
    ParameterMode mode{ParameterMode::InOut};
    bool explicit_mode{};
    std::optional<std::string> location_name;
};

// Source-level callable identity. Names and source locations aid diagnostics;
// parameter modes, types, variadicness, and ABI determine compatibility.
struct FunctionType {
    TypePtr result;
    std::vector<ParameterDecl> parameters;
    bool variadic{};
    std::string abi;
};

TypePtr function_type(TypePtr result, std::vector<ParameterDecl> parameters,
                      bool variadic = false, std::string abi = {});

struct FunctionDecl {
    using GenericParameter = cross::GenericParameter;

    SourceLocation location;
    std::string name;
    std::string source_namespace;
    std::string source_unit;
    std::vector<std::string> imports;
    TypePtr return_type;
    std::vector<ParameterDecl> parameters;
    std::vector<Attribute> attributes;
    std::vector<GenericParameter> generic_parameters;
    std::optional<std::string> result_location;
    std::unique_ptr<Statement> body;
    Linkage linkage{Linkage::Group};
    bool variadic{};
    bool inline_hint{};

    [[nodiscard]] bool definition() const { return body != nullptr; }
    [[nodiscard]] const Attribute* attribute(std::string_view name) const;
};

struct ObjectDecl {
    SourceLocation location;
    std::string name;
    std::string source_unit;
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
    };

    SourceLocation location;
    std::string name;
    BuiltinType underlying{BuiltinType::I32};
    std::vector<Attribute> attributes;
    std::vector<Enumerator> enumerators;
};

struct RecordMemberDecl {
    SourceLocation location;
    std::string name;
    TypePtr type;
    std::unique_ptr<Expr> bit_width;
    std::vector<Attribute> attributes;
};

struct RecordDecl {
    SourceLocation location;
    std::string name;
    bool is_union{};
    bool complete{};
    std::vector<Attribute> attributes;
    std::vector<RecordMemberDecl> members;
};

struct StaticAssertDecl {
    SourceLocation location;
    std::string source_namespace;
    std::unique_ptr<Expr> condition;
    std::string message;
};

struct GlobalLabelDecl {
    SourceLocation location;
    std::string qualified_name;
    std::vector<Attribute> attributes;
};

struct Program {
    unsigned address_bits{64};
    std::vector<RecordDecl> records;
    std::vector<EnumDecl> enumerations;
    std::vector<StaticAssertDecl> static_assertions;
    std::vector<GlobalLabelDecl> global_labels;
    std::vector<std::unique_ptr<FunctionDecl>> functions;
    std::vector<std::unique_ptr<ObjectDecl>> objects;
};

std::string encode_link_name(std::string_view qualified_name, bool label = false,
                             std::string_view mangling_name = "default");
std::optional<std::string> decode_string_literal(std::string_view text);
std::optional<std::uint32_t> decode_character_literal(std::string_view text);

} // namespace cross
