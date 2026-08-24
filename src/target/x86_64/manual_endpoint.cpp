// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/x86_64/manual_endpoint.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <vector>

namespace cross::x86_64 {
namespace {

constexpr std::array<std::string_view, 16> gpr_storage_names{
    "rax", "rbx", "rcx", "rdx", "rsi", "rdi", "rsp", "rbp",
    "r8", "r9", "r10", "r11", "r12", "r13", "r14", "r15",
};

constexpr std::array<std::string_view, 32> simd_storage_names{
    "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5", "zmm6", "zmm7",
    "zmm8", "zmm9", "zmm10", "zmm11", "zmm12", "zmm13", "zmm14", "zmm15",
    "zmm16", "zmm17", "zmm18", "zmm19", "zmm20", "zmm21", "zmm22", "zmm23",
    "zmm24", "zmm25", "zmm26", "zmm27", "zmm28", "zmm29", "zmm30", "zmm31",
};

constexpr std::array<std::string_view, 32> xmm_names{
    "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
    "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
    "xmm16", "xmm17", "xmm18", "xmm19", "xmm20", "xmm21", "xmm22", "xmm23",
    "xmm24", "xmm25", "xmm26", "xmm27", "xmm28", "xmm29", "xmm30", "xmm31",
};

constexpr std::array<std::string_view, 32> ymm_names{
    "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7",
    "ymm8", "ymm9", "ymm10", "ymm11", "ymm12", "ymm13", "ymm14", "ymm15",
    "ymm16", "ymm17", "ymm18", "ymm19", "ymm20", "ymm21", "ymm22", "ymm23",
    "ymm24", "ymm25", "ymm26", "ymm27", "ymm28", "ymm29", "ymm30", "ymm31",
};

constexpr std::array<std::string_view, 32> zmm_names{
    "zmm0", "zmm1", "zmm2", "zmm3", "zmm4", "zmm5", "zmm6", "zmm7",
    "zmm8", "zmm9", "zmm10", "zmm11", "zmm12", "zmm13", "zmm14", "zmm15",
    "zmm16", "zmm17", "zmm18", "zmm19", "zmm20", "zmm21", "zmm22", "zmm23",
    "zmm24", "zmm25", "zmm26", "zmm27", "zmm28", "zmm29", "zmm30", "zmm31",
};

constexpr std::array<std::string_view, 8> x87_names{
    "st0", "st1", "st2", "st3", "st4", "st5", "st6", "st7",
};

constexpr std::array<std::string_view, 8> mask_names{
    "k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7",
};

void add_integer_family(std::vector<RegisterView>& views, std::uint16_t storage_id,
                        std::string_view byte, std::string_view word,
                        std::string_view dword, std::string_view qword) {
    const auto storage = gpr_storage_names[storage_id];
    views.push_back({byte, storage, storage_id, RegisterClass::integer,
                     RegisterFeature::base, 8, 0, false, false});
    views.push_back({word, storage, storage_id, RegisterClass::integer,
                     RegisterFeature::base, 16, 0, false, false});
    views.push_back({dword, storage, storage_id, RegisterClass::integer,
                     RegisterFeature::base, 32, 0, false, true});
    views.push_back({qword, storage, storage_id, RegisterClass::integer,
                     RegisterFeature::base, 64, 0, true, false});
}

const std::vector<RegisterView>& view_table() {
    static const auto table = [] {
        std::vector<RegisterView> views;
        views.reserve(180);

        add_integer_family(views, 0, "al", "ax", "eax", "rax");
        views.push_back({"ah", "rax", 0, RegisterClass::integer,
                         RegisterFeature::base, 8, 8, false, false});
        add_integer_family(views, 1, "bl", "bx", "ebx", "rbx");
        views.push_back({"bh", "rbx", 1, RegisterClass::integer,
                         RegisterFeature::base, 8, 8, false, false});
        add_integer_family(views, 2, "cl", "cx", "ecx", "rcx");
        views.push_back({"ch", "rcx", 2, RegisterClass::integer,
                         RegisterFeature::base, 8, 8, false, false});
        add_integer_family(views, 3, "dl", "dx", "edx", "rdx");
        views.push_back({"dh", "rdx", 3, RegisterClass::integer,
                         RegisterFeature::base, 8, 8, false, false});
        add_integer_family(views, 4, "sil", "si", "esi", "rsi");
        add_integer_family(views, 5, "dil", "di", "edi", "rdi");
        add_integer_family(views, 6, "spl", "sp", "esp", "rsp");
        add_integer_family(views, 7, "bpl", "bp", "ebp", "rbp");
        add_integer_family(views, 8, "r8b", "r8w", "r8d", "r8");
        add_integer_family(views, 9, "r9b", "r9w", "r9d", "r9");
        add_integer_family(views, 10, "r10b", "r10w", "r10d", "r10");
        add_integer_family(views, 11, "r11b", "r11w", "r11d", "r11");
        add_integer_family(views, 12, "r12b", "r12w", "r12d", "r12");
        add_integer_family(views, 13, "r13b", "r13w", "r13d", "r13");
        add_integer_family(views, 14, "r14b", "r14w", "r14d", "r14");
        add_integer_family(views, 15, "r15b", "r15w", "r15d", "r15");

        for (std::uint16_t index = 0; index < simd_storage_names.size(); ++index) {
            const auto storage_id = static_cast<std::uint16_t>(16 + index);
            const auto xmm_feature = index < 16 ? RegisterFeature::base
                                                : RegisterFeature::avx512f;
            const auto ymm_feature = index < 16 ? RegisterFeature::avx
                                                : RegisterFeature::avx512f;
            views.push_back({xmm_names[index], simd_storage_names[index], storage_id,
                             RegisterClass::simd, xmm_feature, 128, 0, false, false});
            views.push_back({ymm_names[index], simd_storage_names[index], storage_id,
                             RegisterClass::simd, ymm_feature, 256, 0, false, false});
            views.push_back({zmm_names[index], simd_storage_names[index], storage_id,
                             RegisterClass::simd, RegisterFeature::avx512f,
                             512, 0, false, false});
        }
        for (std::uint16_t index = 0; index < x87_names.size(); ++index) {
            views.push_back({x87_names[index], x87_names[index],
                             static_cast<std::uint16_t>(48 + index),
                             RegisterClass::x87, RegisterFeature::base,
                             80, 0, false, false});
        }
        for (std::uint16_t index = 0; index < mask_names.size(); ++index) {
            views.push_back({mask_names[index], mask_names[index],
                             static_cast<std::uint16_t>(56 + index),
                             RegisterClass::mask, RegisterFeature::avx512f,
                             64, 0, false, false});
        }
        return views;
    }();
    return table;
}

ManualEndpointParseResult supported(ManualEndpoint endpoint) noexcept {
    return {ManualEndpointStatus::supported, ManualEndpointIssue::none, endpoint};
}

ManualEndpointParseResult unavailable(ManualEndpoint endpoint,
                                      ManualEndpointIssue issue) noexcept {
    return {ManualEndpointStatus::not_implemented, issue, endpoint};
}

ManualEndpointParseResult invalid(ManualEndpointIssue issue,
                                  ManualEndpoint endpoint = {}) noexcept {
    endpoint.kind = ManualEndpointKind::invalid;
    return {ManualEndpointStatus::invalid, issue, endpoint};
}

ManualEndpointParseResult parse_stack(std::string_view text) noexcept {
    ManualEndpoint endpoint;
    endpoint.kind = ManualEndpointKind::stack;
    endpoint.input_kind = ManualEndpointKind::stack;
    endpoint.output_kind = ManualEndpointKind::stack;
    if (text == "stack") {
        return supported(endpoint);
    }
    if (!text.starts_with("stack+")) {
        return invalid(ManualEndpointIssue::unsupported_form);
    }

    const auto digits = text.substr(6);
    if (digits.empty()) return invalid(ManualEndpointIssue::malformed_stack_offset);
    std::uint64_t offset{};
    const auto result = std::from_chars(digits.data(), digits.data() + digits.size(), offset, 10);
    if (result.ec == std::errc::result_out_of_range) {
        return invalid(ManualEndpointIssue::stack_offset_overflow);
    }
    if (result.ec != std::errc{} || result.ptr != digits.data() + digits.size()) {
        return invalid(ManualEndpointIssue::malformed_stack_offset);
    }
    endpoint.stack_offset = offset;
    endpoint.has_fixed_stack_offset = true;
    endpoint.input_stack_offset = offset;
    endpoint.output_stack_offset = offset;
    endpoint.input_has_fixed_stack_offset = true;
    endpoint.output_has_fixed_stack_offset = true;
    return supported(endpoint);
}

ManualEndpointParseResult parse_lifo(std::string_view text) noexcept {
    ManualEndpoint endpoint;
    endpoint.kind = ManualEndpointKind::lifo;
    if (text == "push") endpoint.lifo_form = LifoEndpointForm::push;
    else if (text == "push=>pop") endpoint.lifo_form = LifoEndpointForm::push_pop;
    else if (text == "push=>discard") endpoint.lifo_form = LifoEndpointForm::push_discard;
    else if (text == "reserve=>pop") endpoint.lifo_form = LifoEndpointForm::reserve_pop;
    else return invalid(ManualEndpointIssue::unsupported_form);
    switch (endpoint.lifo_form) {
    case LifoEndpointForm::push:
    case LifoEndpointForm::push_pop:
        endpoint.input_kind = ManualEndpointKind::lifo;
        endpoint.output_kind = ManualEndpointKind::lifo;
        endpoint.input_lifo_form = endpoint.lifo_form;
        endpoint.output_lifo_form = endpoint.lifo_form;
        break;
    case LifoEndpointForm::push_discard:
        endpoint.input_kind = ManualEndpointKind::lifo;
        endpoint.input_lifo_form = endpoint.lifo_form;
        break;
    case LifoEndpointForm::reserve_pop:
        endpoint.output_kind = ManualEndpointKind::lifo;
        endpoint.output_lifo_form = endpoint.lifo_form;
        break;
    case LifoEndpointForm::none: break;
    }
    return supported(endpoint);
}

} // namespace

std::span<const RegisterView> register_views() noexcept {
    return view_table();
}

const RegisterView* find_register_view(std::string_view name) noexcept {
    const auto& views = view_table();
    const auto found = std::find_if(views.begin(), views.end(), [name](const RegisterView& view) {
        return view.name == name;
    });
    return found == views.end() ? nullptr : &*found;
}

bool shares_register_storage(const RegisterView& left, const RegisterView& right) noexcept {
    return left.register_class == right.register_class && left.storage_id == right.storage_id;
}

bool register_bit_ranges_overlap(const RegisterView& left, const RegisterView& right) noexcept {
    if (!shares_register_storage(left, right)) return false;
    const auto left_end = static_cast<unsigned>(left.bit_offset) + left.bits;
    const auto right_end = static_cast<unsigned>(right.bit_offset) + right.bits;
    return left.bit_offset < right_end && right.bit_offset < left_end;
}

ManualEndpointParseResult parse_manual_endpoint(std::string_view text) noexcept {
    if (text.empty()) return invalid(ManualEndpointIssue::empty_location);
    if (text == "auto") {
        ManualEndpoint endpoint;
        endpoint.kind = ManualEndpointKind::automatic;
        endpoint.input_kind = ManualEndpointKind::automatic;
        endpoint.output_kind = ManualEndpointKind::automatic;
        return supported(endpoint);
    }
    if ((text == "stack" || text.starts_with("stack+")) &&
        text.find("=>") == std::string_view::npos) return parse_stack(text);
    if (text == "push" || text == "push=>pop" || text == "push=>discard" ||
        text == "reserve=>pop") {
        return parse_lifo(text);
    }
    if (text.find("=>") != std::string_view::npos) {
        const auto separator = text.find("=>");
        ManualEndpoint endpoint;
        endpoint.kind = ManualEndpointKind::endpoint_pair;
        if (separator == 0 || separator + 2 == text.size() ||
            text.find("=>", separator + 2) != std::string_view::npos) {
            return invalid(ManualEndpointIssue::unsupported_form, endpoint);
        }
        const auto input = parse_manual_endpoint(text.substr(0, separator));
        const auto output = parse_manual_endpoint(text.substr(separator + 2));
        const auto allocatable_side = [](ManualEndpointKind kind) {
            return kind == ManualEndpointKind::automatic ||
                   kind == ManualEndpointKind::direct_register ||
                   kind == ManualEndpointKind::indirect_register ||
                   kind == ManualEndpointKind::stack;
        };
        const auto register_side = [](ManualEndpointKind kind) {
            return kind == ManualEndpointKind::direct_register ||
                   kind == ManualEndpointKind::indirect_register;
        };
        if (input && output && allocatable_side(input.endpoint.kind) &&
            allocatable_side(output.endpoint.kind) &&
            (register_side(input.endpoint.kind) || register_side(output.endpoint.kind))) {
            endpoint.input_kind = input.endpoint.kind;
            endpoint.output_kind = output.endpoint.kind;
            endpoint.input_register_view = input.endpoint.register_view;
            endpoint.output_register_view = output.endpoint.register_view;
            endpoint.input_stack_offset = input.endpoint.stack_offset;
            endpoint.output_stack_offset = output.endpoint.stack_offset;
            endpoint.input_has_fixed_stack_offset = input.endpoint.has_fixed_stack_offset;
            endpoint.output_has_fixed_stack_offset = output.endpoint.has_fixed_stack_offset;
            return supported(endpoint);
        }
        return unavailable(endpoint, ManualEndpointIssue::endpoint_pair_not_implemented);
    }

    if (text.front() == '*') {
        if (text.size() == 1) return invalid(ManualEndpointIssue::missing_indirect_register);
        const auto* view = find_register_view(text.substr(1));
        if (!view) return invalid(ManualEndpointIssue::unknown_register);
        ManualEndpoint endpoint;
        endpoint.kind = ManualEndpointKind::indirect_register;
        endpoint.register_view = view;
        endpoint.input_kind = ManualEndpointKind::indirect_register;
        endpoint.output_kind = ManualEndpointKind::indirect_register;
        endpoint.input_register_view = view;
        endpoint.output_register_view = view;
        if (!view->address_capable) {
            return {ManualEndpointStatus::invalid,
                    ManualEndpointIssue::indirect_register_must_be_address_capable, endpoint};
        }
        return supported(endpoint);
    }

    if (const auto* view = find_register_view(text)) {
        ManualEndpoint endpoint;
        endpoint.kind = ManualEndpointKind::direct_register;
        endpoint.register_view = view;
        endpoint.input_kind = ManualEndpointKind::direct_register;
        endpoint.output_kind = ManualEndpointKind::direct_register;
        endpoint.input_register_view = view;
        endpoint.output_register_view = view;
        return supported(endpoint);
    }
    return invalid(ManualEndpointIssue::unknown_register);
}

std::string_view manual_endpoint_issue_name(ManualEndpointIssue issue) noexcept {
    switch (issue) {
    case ManualEndpointIssue::none: return "none";
    case ManualEndpointIssue::empty_location: return "empty-location";
    case ManualEndpointIssue::unknown_register: return "unknown-register";
    case ManualEndpointIssue::missing_indirect_register: return "missing-indirect-register";
    case ManualEndpointIssue::indirect_register_must_be_address_capable:
        return "indirect-register-must-be-address-capable";
    case ManualEndpointIssue::malformed_stack_offset: return "malformed-stack-offset";
    case ManualEndpointIssue::stack_offset_overflow: return "stack-offset-overflow";
    case ManualEndpointIssue::endpoint_pair_not_implemented: return "endpoint-pair-not-implemented";
    case ManualEndpointIssue::unsupported_form: return "unsupported-form";
    }
    return "unknown-issue";
}

std::string_view manual_endpoint_issue_message(ManualEndpointIssue issue) noexcept {
    switch (issue) {
    case ManualEndpointIssue::none: return {};
    case ManualEndpointIssue::empty_location: return "manual endpoint is empty";
    case ManualEndpointIssue::unknown_register: return "unknown x86-64 register endpoint";
    case ManualEndpointIssue::missing_indirect_register:
        return "indirect endpoint is missing its address register";
    case ManualEndpointIssue::indirect_register_must_be_address_capable:
        return "indirect endpoint requires a 64-bit integer address register";
    case ManualEndpointIssue::malformed_stack_offset:
        return "manual stack offset must be a nonnegative decimal integer";
    case ManualEndpointIssue::stack_offset_overflow:
        return "manual stack offset is too large";
    case ManualEndpointIssue::endpoint_pair_not_implemented:
        return "x86-64 endpoint pair contains an unavailable endpoint form";
    case ManualEndpointIssue::unsupported_form:
        return "unsupported x86-64 manual endpoint form";
    }
    return "unknown x86-64 manual endpoint error";
}

} // namespace cross::x86_64
