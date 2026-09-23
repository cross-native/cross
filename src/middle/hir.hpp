// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/diagnostic.hpp"
#include "common/options.hpp"
#include "frontend/ast.hpp"
#include "target/target.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cross::hir {

struct TypeId {
    std::uint32_t value{};
    friend bool operator==(TypeId, TypeId) = default;
};

struct FunctionId {
    std::uint32_t value{};
    friend bool operator==(FunctionId, FunctionId) = default;
};

struct ObjectId {
    std::uint32_t value{};
    friend bool operator==(ObjectId, ObjectId) = default;
};

struct RecordId {
    std::uint32_t value{};
    friend bool operator==(RecordId, RecordId) = default;
};

struct LabelId {
    std::uint32_t value{};
    friend bool operator==(LabelId, LabelId) = default;
};

struct Parameter {
    SourceLocation location;
    std::string name;
    TypeId type;
    ParameterMode mode{ParameterMode::In};
    std::optional<std::string> physical_location;
};

struct FunctionSignature {
    TypeId result_type;
    std::vector<Parameter> parameters;
    AbiId abi;
    bool variadic{};
    friend bool operator==(const FunctionSignature& a,
                           const FunctionSignature& b) {
        if (a.result_type != b.result_type || a.abi != b.abi ||
            a.variadic != b.variadic ||
            a.parameters.size() != b.parameters.size())
            return false;
        for (std::size_t index = 0; index < a.parameters.size(); ++index) {
            if (a.parameters[index].type != b.parameters[index].type ||
                a.parameters[index].mode != b.parameters[index].mode ||
                a.parameters[index].physical_location !=
                    b.parameters[index].physical_location)
                return false;
        }
        return true;
    }
};

struct Type {
    enum class Kind {
        Builtin,
        Pointer,
        Vector,
        Array,
        Record,
        Function
    } kind{Kind::Builtin};
    BuiltinType builtin{BuiltinType::Void};
    std::optional<TypeId> pointee;
    bool is_const{};
    bool is_volatile{};
    std::string nominal_name;
    std::optional<TypeId> element;
    std::uint32_t lanes{};
    bool scalable{};
    bool is_atomic{};
    std::optional<RecordId> record;
    std::optional<FunctionSignature> function{};
    bool is_restrict{};
    std::uint32_t address_space{};
};

struct RecordMember {
    SourceLocation location;
    std::string name;
    TypeId type;
    std::uint64_t offset{};
    unsigned alignment{1};
    std::optional<unsigned> bit_width;
    unsigned bit_offset{};
    const Expr* pending_bit_width{};
    bool packed{};
};

struct Record {
    RecordId id;
    SourceLocation location;
    std::string source_name;
    bool is_union{};
    bool complete{};
    bool packed{};
    unsigned explicit_alignment{1};
    std::uint64_t size{};
    unsigned alignment{1};
    std::vector<RecordMember> members;
    std::vector<const RecordDecl*> declarations;
    const RecordDecl* definition{};
};

struct VariadicBinding {
    SourceLocation location;
    std::string name;
    TypeId type;
    AbiStateId state;
};

enum class BodyOwnership { None, ManagedAst, ManagedMir, RawMir };
enum class AbiContract { Dynamic, Registered };
enum class FunctionTemperature { Normal, Hot, Cold };
enum class SymbolVisibility { Default, Hidden, Protected, Internal };

struct Function {
    FunctionId id;
    SourceLocation location;
    std::string source_name;
    std::string source_unit;
    std::string link_symbol;
    Linkage linkage{Linkage::Group};
    TypeId result_type;
    std::vector<Parameter> parameters;
    std::optional<std::string> result_location;
    AbiId abi;
    AbiContract abi_contract{AbiContract::Registered};
    bool abi_explicit{};
    std::optional<std::string> section;
    unsigned minimum_alignment{1};
    SymbolVisibility visibility{SymbolVisibility::Default};
    bool weak{};
    std::optional<std::string> alias_target;
    std::optional<std::string> weakref_target;
    FunctionTemperature temperature{FunctionTemperature::Normal};
    bool used{};
    bool retain{};
    bool no_stack_protector{};
    std::vector<std::string> no_sanitize;
    std::vector<std::string> clobbers;
    bool variadic{};
    std::vector<VariadicBinding> variadic_bindings;
    bool naked{};
    BodyOwnership ownership{BodyOwnership::None};
    std::vector<LabelId> labels;
    std::vector<const FunctionDecl*> declarations;
    const FunctionDecl* definition{};
};

struct Label {
    LabelId id;
    FunctionId owner;
    SourceLocation location;
    std::string source_name;
    std::string qualified_name;
    std::string link_symbol;
    std::vector<const GlobalLabelDecl*> declarations;
    const Statement* definition{};
    bool is_global{};
};

struct Object {
    ObjectId id;
    SourceLocation location;
    std::string source_name;
    std::string source_unit;
    std::string link_symbol;
    Linkage linkage{Linkage::Group};
    TypeId type;
    std::optional<std::string> section;
    unsigned minimum_alignment{1};
    SymbolVisibility visibility{SymbolVisibility::Default};
    bool weak{};
    std::optional<std::string> alias_target;
    std::optional<std::string> weakref_target;
    bool is_thread_local{};
    std::string tls_model;
    std::vector<const ObjectDecl*> declarations;
    const ObjectDecl* definition{};
};

class Module {
public:
    [[nodiscard]] const Type& type(TypeId id) const { return types.at(id.value); }
    [[nodiscard]] std::optional<TypeId> builtin(BuiltinType kind) const;
    [[nodiscard]] TypeId intern_type(const TypePtr& source);
    [[nodiscard]] TypeId function_type(FunctionSignature signature);
    [[nodiscard]] TypeId pointer_to(TypeId pointee);
    [[nodiscard]] TypeId without_top_level_const(TypeId type);
    [[nodiscard]] TypeId unqualified(TypeId type);
    [[nodiscard]] TypeId add_qualifiers(TypeId type, bool is_const,
                                        bool is_volatile);
    [[nodiscard]] TypeId vector_of(TypeId element, std::uint32_t lanes,
                                   bool scalable = false);
    [[nodiscard]] TypeId array_of(TypeId element, std::uint32_t elements);
    [[nodiscard]] const Record& record(RecordId id) const {
        return records.at(id.value);
    }
    [[nodiscard]] Record& record(RecordId id) { return records.at(id.value); }
    [[nodiscard]] const Record* record(std::string_view name) const;
    [[nodiscard]] const RecordMember* member(RecordId record,
                                             std::string_view name) const;
    [[nodiscard]] const Function& function(FunctionId id) const {
        return functions.at(id.value);
    }
    [[nodiscard]] Function& function(FunctionId id) { return functions.at(id.value); }
    [[nodiscard]] const Function* function(const FunctionDecl& declaration) const;
    [[nodiscard]] const Object* object(const ObjectDecl& declaration) const;
    [[nodiscard]] const Object& object(ObjectId id) const {
        return objects.at(id.value);
    }
    [[nodiscard]] const Label* label(FunctionId function, std::string_view name) const;
    [[nodiscard]] const Label* global_label(std::string_view qualified_name) const;
    [[nodiscard]] bool raw_owned(const FunctionDecl& declaration) const;

    // Canonical pointer-sized language modes for this compilation.  Keeping
    // the selected ABI width on HIR prevents the middle end from silently
    // assuming x86-64 when a 32-bit or capability-oriented target is added.
    unsigned address_bits{64};
    AbiId default_abi;
    // Source spelling is resolved only at the AST-to-HIR interning boundary.
    std::unordered_map<std::string, AbiId> abi_names;
    std::vector<Type> types;
    std::vector<Record> records;
    std::vector<Function> functions;
    std::vector<Object> objects;
    std::vector<Label> labels;
    std::unordered_map<const FunctionDecl*, FunctionId> function_ids;
    std::unordered_map<const ObjectDecl*, ObjectId> object_ids;
    std::unordered_map<std::string, RecordId> record_ids;
};

Module build(Program& program, const CompilerOptions& options,
             const TargetInfo& target, Diagnostics& diagnostics);
// Target layout and entity metadata for required constants before generic
// bodies have been instantiated. Template declarations are deliberately absent.
Module build_constant_context(Program& program, const CompilerOptions& options,
                              const TargetInfo& target, Diagnostics& diagnostics);
bool validate_source_address_spaces(Program& program,
                                    const CompilerOptions& options,
                                    const TargetInfo& target,
                                    Diagnostics& diagnostics);
[[nodiscard]] std::optional<FunctionSignature>
call_signature(const Module& module, std::optional<FunctionId> direct,
               std::optional<TypeId> indirect);
// An observable entry address uses its registered interface, never a private
// dynamically selected transport. Explicit endpoint adapters remain deferred.
bool stabilize_function_address(Module& module, FunctionId function,
                                SourceLocation location,
                                Diagnostics& diagnostics);
[[nodiscard]] std::string type_name(const Module& module, TypeId type);
[[nodiscard]] std::optional<std::uint64_t>
layout_size(const Module& module, TypeId type, const TargetInfo& target);
[[nodiscard]] std::optional<std::uint64_t>
layout_alignment(const Module& module, TypeId type,
                 const TargetInfo& target);

} // namespace cross::hir
