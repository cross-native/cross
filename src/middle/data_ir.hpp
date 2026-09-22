// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/uint128.hpp"
#include "middle/hir.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace cross {
class Diagnostics;
class Subtarget;
struct CompilerOptions;
struct Expr;

namespace data {

// Exact host-independent conversion shared by static Data IR and managed MIR.
[[nodiscard]] std::optional<UInt128> parse_binary128_literal(std::string text);

enum class InitializerKind {
    Declaration,
    Zero,
    Uninitialized,
    Integer, // Fixed bits, including a normalized absolute pointer value.
    Floating,
    Address,
    Bytes,
    Aggregate,
};

enum class AddressKind { Object, Function, Label };

struct AddressConstant {
    AddressKind kind{AddressKind::Object};
    std::optional<hir::ObjectId> object;
    std::optional<hir::FunctionId> function;
    std::optional<hir::LabelId> label;
    std::int64_t addend{};
};

struct AddressScope {
    std::string_view source_name;
    std::string_view source_unit;
};

// Shared source-to-typed-relocation recognition for static data and patch
// initials. `integer` requires an explicit address-width integer cast.
[[nodiscard]] std::optional<AddressConstant> relocatable_address(
    const hir::Module& module, AddressScope scope, const Expr& expression,
    const Subtarget& subtarget, bool integer);

bool normalize_generic_pointer(Program& program, std::unique_ptr<Expr>& expression,
                               const TypePtr& destination,
                               const FunctionDecl* caller,
                               std::span<const NameKey> locals,
                               const CompilerOptions& options,
                               const Subtarget& subtarget,
                               Diagnostics& diagnostics);

struct Relocation {
    unsigned offset{};
    unsigned size{};
    AddressConstant address;
};

struct Object {
    hir::ObjectId source;
    SourceLocation location;
    hir::TypeId type;
    InitializerKind initializer{InitializerKind::Declaration};
    UInt128 bits;
    std::optional<AddressConstant> address;
    std::vector<unsigned char> bytes;
    std::vector<Relocation> relocations;
    unsigned size{};
    unsigned alignment{1};
    bool read_only{};
    bool retain{};
    bool used{};
    bool is_thread_local{};
    std::string tls_model;
};

struct Module {
    [[nodiscard]] const Object* find(hir::ObjectId id) const;

    unsigned address_bits{};
    ByteOrder byte_order{ByteOrder::Little};
    std::vector<Object> objects;
    std::unordered_map<std::uint32_t, std::size_t> object_indices;
};

// Lowers every static-duration object into target-resolved storage and a typed
// target-resolved initializer. No backend needs to inspect source expressions
// or parse literal text after this boundary.
[[nodiscard]] Module lower(hir::Module& hir_module, const Subtarget& subtarget,
                           Diagnostics& diagnostics);

} // namespace data
} // namespace cross
