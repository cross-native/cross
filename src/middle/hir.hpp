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

struct Type {
    enum class Kind { Builtin, Pointer, Vector, Array, Record } kind{Kind::Builtin};
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
};

struct RecordMember {
    SourceLocation location;
    std::string name;
    TypeId type;
    std::uint64_t offset{};
    unsigned alignment{1};
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

struct Parameter {
    SourceLocation location;
    std::string name;
    TypeId type;
    ParameterMode mode{ParameterMode::InOut};
    std::optional<std::string> physical_location;
};

struct VariadicBinding {
    SourceLocation location;
    std::string name;
    TypeId type;
    std::string state;
};

enum class BodyOwnership { None, ManagedAst, ManagedMir, RawMir };
enum class AbiContract { Dynamic, Registered };

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
    std::string abi;
    AbiContract abi_contract{AbiContract::Registered};
    bool abi_explicit{};
    std::optional<std::string> section;
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
    [[nodiscard]] TypeId pointer_to(TypeId pointee);
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
    [[nodiscard]] bool raw_owned(const FunctionDecl& declaration) const;

    // Canonical pointer-sized language modes for this compilation.  Keeping
    // the selected ABI width on HIR prevents the middle end from silently
    // assuming x86-64 when a 32-bit or capability-oriented target is added.
    unsigned address_bits{64};
    std::vector<Type> types;
    std::vector<Record> records;
    std::vector<Function> functions;
    std::vector<Object> objects;
    std::vector<Label> labels;
    std::unordered_map<const FunctionDecl*, FunctionId> function_ids;
    std::unordered_map<const ObjectDecl*, ObjectId> object_ids;
    std::unordered_map<std::string, RecordId> record_ids;
};

Module build(const Program& program, const CompilerOptions& options,
             const TargetInfo& target, Diagnostics& diagnostics);
[[nodiscard]] std::string type_name(const Module& module, TypeId type);

} // namespace cross::hir
