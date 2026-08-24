// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "common/uint128.hpp"
#include "middle/hir.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace cross {
class Diagnostics;
class Subtarget;

namespace data {

// Exact host-independent conversion shared by static Data IR and managed MIR.
[[nodiscard]] std::optional<UInt128> parse_binary128_literal(std::string text);

enum class InitializerKind {
    Declaration,
    Zero,
    Uninitialized,
    Integer,
    Floating,
    Address,
};

enum class AddressKind { Object, Function, Label };

struct AddressConstant {
    AddressKind kind{AddressKind::Object};
    std::optional<hir::ObjectId> object;
    std::optional<hir::FunctionId> function;
    std::optional<hir::LabelId> label;
    std::int64_t addend{};
};

struct Object {
    hir::ObjectId source;
    SourceLocation location;
    hir::TypeId type;
    InitializerKind initializer{InitializerKind::Declaration};
    UInt128 bits;
    std::optional<AddressConstant> address;
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
// scalar initializer. No backend needs to inspect source expressions or parse
// literal text after this boundary.
[[nodiscard]] Module lower(const hir::Module& hir_module,
                           const Subtarget& subtarget,
                           Diagnostics& diagnostics);

} // namespace data
} // namespace cross
