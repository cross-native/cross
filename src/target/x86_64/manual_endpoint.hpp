// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cross::x86_64 {

// A register view is a source-spellable slice of one physical register.
// storage_id is unique within this target and is shared by all aliases.
enum class RegisterClass : std::uint8_t {
    integer,
    simd,
    mask,
    x87,
};

enum class RegisterFeature : std::uint8_t {
    base,
    avx,
    avx512f,
};

struct RegisterView {
    std::string_view name;
    std::string_view storage_name;
    std::uint16_t storage_id{};
    RegisterClass register_class{RegisterClass::integer};
    RegisterFeature required_feature{RegisterFeature::base};
    std::uint16_t bits{};
    std::uint16_t bit_offset{};
    bool address_capable{};
    bool zero_extends_storage_on_write{};
};

// Returns every source-spellable x86-64 GPR, XMM/YMM/ZMM view, AVX-512 opmask
// register, and x87 stack position. Pointers to
// elements of this span remain valid for the life of the process.
[[nodiscard]] std::span<const RegisterView> register_views() noexcept;
[[nodiscard]] const RegisterView* find_register_view(std::string_view name) noexcept;

// shares_register_storage is the appropriate conservative test for ABI
// endpoint conflicts. register_bit_ranges_overlap additionally accounts for
// disjoint slices such as al and ah.
[[nodiscard]] bool shares_register_storage(const RegisterView& left,
                                           const RegisterView& right) noexcept;
[[nodiscard]] bool register_bit_ranges_overlap(const RegisterView& left,
                                               const RegisterView& right) noexcept;

enum class ManualEndpointKind : std::uint8_t {
    automatic,
    direct_register,
    indirect_register,
    stack,
    indirect_stack,
    lifo,
    endpoint_pair,
    invalid,
};

enum class ManualEndpointStatus : std::uint8_t {
    supported,
    not_implemented,
    invalid,
};

enum class LifoEndpointForm : std::uint8_t {
    none,
    push,
    push_pop,
    push_discard,
    reserve_pop,
};

enum class ManualEndpointIssue : std::uint8_t {
    none,
    empty_location,
    unknown_register,
    missing_indirect_register,
    indirect_register_must_be_address_capable,
    malformed_stack_offset,
    stack_offset_overflow,
    endpoint_pair_not_implemented,
    unsupported_form,
};

struct ManualEndpoint {
    ManualEndpointKind kind{ManualEndpointKind::invalid};
    const RegisterView* register_view{};
    ManualEndpointKind input_kind{ManualEndpointKind::invalid};
    ManualEndpointKind output_kind{ManualEndpointKind::invalid};
    const RegisterView* input_register_view{};
    const RegisterView* output_register_view{};
    std::uint64_t stack_offset{};
    bool has_fixed_stack_offset{};
    std::uint64_t input_stack_offset{};
    std::uint64_t output_stack_offset{};
    bool input_has_fixed_stack_offset{};
    bool output_has_fixed_stack_offset{};
    LifoEndpointForm lifo_form{LifoEndpointForm::none};
    LifoEndpointForm input_lifo_form{LifoEndpointForm::none};
    LifoEndpointForm output_lifo_form{LifoEndpointForm::none};
};

struct ManualEndpointParseResult {
    ManualEndpointStatus status{ManualEndpointStatus::invalid};
    ManualEndpointIssue issue{ManualEndpointIssue::unsupported_form};
    ManualEndpoint endpoint{};

    [[nodiscard]] explicit operator bool() const noexcept {
        return status == ManualEndpointStatus::supported;
    }
};

// Parses one complete location annotation. Automatic/direct/indirect register
// A=>B pairs with at least one fixed side are supported, including stack sides.
// Indirect endpoints require a 64-bit integer register view.
[[nodiscard]] ManualEndpointParseResult parse_manual_endpoint(std::string_view text) noexcept;

[[nodiscard]] std::string_view manual_endpoint_issue_name(ManualEndpointIssue issue) noexcept;
[[nodiscard]] std::string_view manual_endpoint_issue_message(ManualEndpointIssue issue) noexcept;

} // namespace cross::x86_64
