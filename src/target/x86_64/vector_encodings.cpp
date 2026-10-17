// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/x86_64/encodings.hpp"

#include <deque>
#include <initializer_list>
#include <string>
#include <vector>

namespace cross::x86_64::encoding {
namespace {

using K = Kind;
using L = Layout;
using P = Prefix;
using Mp = Map;
using T = Tuple;
namespace f = flag;

// EVEX availability and attributes of one SIMD instruction.
struct Evex {
    bool present{};
    bool w1{};
    std::uint32_t flags{};
    T tuple{T::Full};
};
constexpr Evex no_evex{};
constexpr Evex evex32{true, false, f::mask | f::zero | f::broadcast32, T::Full};
constexpr Evex evex64{true, true, f::mask | f::zero | f::broadcast64, T::Full};
constexpr Evex evex32_round{true, false, f::mask | f::zero | f::broadcast32 | f::rounding, T::Full};
constexpr Evex evex64_round{true, true, f::mask | f::zero | f::broadcast64 | f::rounding, T::Full};
constexpr Evex evex32_sae{true, false, f::mask | f::zero | f::broadcast32 | f::sae, T::Full};
// Byte and word elements: no broadcast, full-vector memory.
constexpr Evex evex_bw{true, false, f::mask | f::zero, T::FullMem};

class Builder {
public:
    Builder(std::vector<Form>& forms, std::deque<std::string>& names) : forms_(forms), names_(names) {}

    std::string_view name(std::string text) {
        names_.push_back(std::move(text));
        return names_.back();
    }
    std::string_view v(std::string_view base) { return name("v" + std::string(base)); }

    void add(std::string_view mnemonic, std::initializer_list<K> operands, Space space, Mp map, P prefix,
             std::uint8_t opcode, std::int16_t digit, L layout, std::uint32_t flags = 0,
             T tuple = T::None) {
        Form form;
        form.mnemonic = mnemonic;
        std::copy(operands.begin(), operands.end(), form.operands.begin());
        form.space = space;
        form.map = map;
        form.prefix = prefix;
        form.opcode = opcode;
        form.digit = digit;
        form.layout = layout;
        form.flags = flags;
        form.tuple = tuple;
        forms_.push_back(form);
    }
    void legacy(std::string_view mnemonic, std::initializer_list<K> operands, Mp map, P prefix,
                std::uint8_t opcode, std::int16_t digit, L layout, std::uint32_t flags = 0) {
        add(mnemonic, operands, Space::Legacy, map, prefix, opcode, digit, layout, flags);
    }
    void vex(std::string_view mnemonic, std::initializer_list<K> operands, Mp map, P prefix,
             std::uint8_t opcode, std::int16_t digit, L layout, std::uint32_t flags = 0) {
        add(mnemonic, operands, Space::Vex, map, prefix, opcode, digit, layout, flags);
    }
    void evex(std::string_view mnemonic, std::initializer_list<K> operands, Mp map, P prefix,
              std::uint8_t opcode, std::int16_t digit, L layout, std::uint32_t flags, T tuple) {
        add(mnemonic, operands, Space::Evex, map, prefix, opcode, digit, layout, flags, tuple);
    }

    // A packed operation on xmm (legacy and VEX), ymm (VEX), and
    // xmm/ymm/zmm (EVEX): `name xmm1, xmm2/m128` in legacy form and
    // `vname dest, src1, src2/mem` otherwise.
    void packed(std::string_view base, P prefix, Mp map, std::uint8_t opcode, const Evex& e,
                bool legacy_form = true, bool vex_form = true, std::uint32_t vex_flags = 0,
                bool vex_w1 = false) {
        if (legacy_form) legacy(base, {K::Xmm, K::Xm128}, map, prefix, opcode, -1, L::RM);
        const auto name_v = v(base);
        const auto vw = vex_w1 ? f::w : 0U;
        if (vex_form) {
            vex(name_v, {K::Xmm, K::Xmm, K::Xm128}, map, prefix, opcode, -1, L::RVM, vex_flags | vw);
            vex(name_v, {K::Ymm, K::Ymm, K::Ym256}, map, prefix, opcode, -1, L::RVM, vex_flags | vw | f::l256);
        }
        if (e.present) {
            const auto w = e.w1 ? f::w : 0U;
            const auto rounding = e.flags & (f::rounding | f::sae);
            const auto base_flags = (e.flags & ~(f::rounding | f::sae)) | w;
            evex(name_v, {K::Xmm, K::Xmm, K::Xm128}, map, prefix, opcode, -1, L::RVM, base_flags, e.tuple);
            evex(name_v, {K::Ymm, K::Ymm, K::Ym256}, map, prefix, opcode, -1, L::RVM, base_flags | f::l256, e.tuple);
            evex(name_v, {K::Zmm, K::Zmm, K::Zm512}, map, prefix, opcode, -1, L::RVM,
                 base_flags | f::l512 | rounding, e.tuple);
        }
    }

    // `name xmm1, xmm2/m128` (legacy), `vname xmm1, xmm2/m128` and ymm (VEX),
    // and xmm/ymm/zmm (EVEX): one source.
    void packed_unary(std::string_view base, P prefix, Mp map, std::uint8_t opcode, const Evex& e,
                      bool legacy_form = true, bool vex_form = true, bool vex_w1 = false) {
        if (legacy_form) legacy(base, {K::Xmm, K::Xm128}, map, prefix, opcode, -1, L::RM);
        const auto name_v = v(base);
        const auto vw = vex_w1 ? f::w : 0U;
        if (vex_form) {
            vex(name_v, {K::Xmm, K::Xm128}, map, prefix, opcode, -1, L::RM, vw);
            vex(name_v, {K::Ymm, K::Ym256}, map, prefix, opcode, -1, L::RM, vw | f::l256);
        }
        if (e.present) {
            const auto w = e.w1 ? f::w : 0U;
            const auto rounding = e.flags & (f::rounding | f::sae);
            const auto base_flags = (e.flags & ~(f::rounding | f::sae)) | w;
            evex(name_v, {K::Xmm, K::Xm128}, map, prefix, opcode, -1, L::RM, base_flags, e.tuple);
            evex(name_v, {K::Ymm, K::Ym256}, map, prefix, opcode, -1, L::RM, base_flags | f::l256, e.tuple);
            evex(name_v, {K::Zmm, K::Zm512}, map, prefix, opcode, -1, L::RM, base_flags | f::l512 | rounding,
                 e.tuple);
        }
    }

    // Scalar float operations: `name xmm1, xmm2/m32` (legacy) and
    // `vname xmm1, xmm2, xmm3/m32` (VEX, EVEX).
    void scalar(std::string_view base, P prefix, Mp map, std::uint8_t opcode, bool double_precision,
                std::uint32_t evex_extra, bool evex_form = true, std::uint32_t vex_flags = 0) {
        const auto memory = double_precision ? K::Xm64 : K::Xm32;
        legacy(base, {K::Xmm, memory}, map, prefix, opcode, -1, L::RM);
        const auto name_v = v(base);
        vex(name_v, {K::Xmm, K::Xmm, memory}, map, prefix, opcode, -1, L::RVM, vex_flags);
        if (evex_form) {
            evex(name_v, {K::Xmm, K::Xmm, memory}, map, prefix, opcode, -1, L::RVM,
                 f::mask | f::zero | evex_extra | (double_precision ? f::w | f::element64 : f::element32),
                 T::Tuple1Scalar);
        }
    }

    // A packed operation with an immediate: `name xmm1, xmm2/m128, imm8`.
    void packed_imm(std::string_view base, P prefix, Mp map, std::uint8_t opcode, const Evex& e,
                    bool legacy_form = true, bool vex_form = true, bool vex_w1 = false) {
        if (legacy_form) legacy(base, {K::Xmm, K::Xm128, K::I8}, map, prefix, opcode, -1, L::RM);
        const auto name_v = v(base);
        const auto vw = vex_w1 ? f::w : 0U;
        if (vex_form) {
            vex(name_v, {K::Xmm, K::Xmm, K::Xm128, K::I8}, map, prefix, opcode, -1, L::RVM, vw);
            vex(name_v, {K::Ymm, K::Ymm, K::Ym256, K::I8}, map, prefix, opcode, -1, L::RVM, vw | f::l256);
        }
        if (e.present) {
            const auto w = e.w1 ? f::w : 0U;
            const auto rounding = e.flags & (f::rounding | f::sae);
            const auto base_flags = (e.flags & ~(f::rounding | f::sae)) | w;
            evex(name_v, {K::Xmm, K::Xmm, K::Xm128, K::I8}, map, prefix, opcode, -1, L::RVM, base_flags, e.tuple);
            evex(name_v, {K::Ymm, K::Ymm, K::Ym256, K::I8}, map, prefix, opcode, -1, L::RVM,
                 base_flags | f::l256, e.tuple);
            evex(name_v, {K::Zmm, K::Zmm, K::Zm512, K::I8}, map, prefix, opcode, -1, L::RVM,
                 base_flags | f::l512 | rounding, e.tuple);
        }
    }

    // A one-source operation with an immediate: `name xmm1, xmm2/m128, imm8`
    // in every encoding (pshufd, roundps).
    void unary_imm(std::string_view base, P prefix, Mp map, std::uint8_t opcode, const Evex& e,
                   bool legacy_form = true, bool vex_form = true, bool vex_w1 = false) {
        if (legacy_form) legacy(base, {K::Xmm, K::Xm128, K::I8}, map, prefix, opcode, -1, L::RM);
        const auto name_v = v(base);
        const auto vw = vex_w1 ? f::w : 0U;
        if (vex_form) {
            vex(name_v, {K::Xmm, K::Xm128, K::I8}, map, prefix, opcode, -1, L::RM, vw);
            vex(name_v, {K::Ymm, K::Ym256, K::I8}, map, prefix, opcode, -1, L::RM, vw | f::l256);
        }
        if (e.present) {
            const auto w = e.w1 ? f::w : 0U;
            const auto rounding = e.flags & (f::rounding | f::sae);
            const auto base_flags = (e.flags & ~(f::rounding | f::sae)) | w;
            evex(name_v, {K::Xmm, K::Xm128, K::I8}, map, prefix, opcode, -1, L::RM, base_flags, e.tuple);
            evex(name_v, {K::Ymm, K::Ym256, K::I8}, map, prefix, opcode, -1, L::RM, base_flags | f::l256, e.tuple);
            evex(name_v, {K::Zmm, K::Zm512, K::I8}, map, prefix, opcode, -1, L::RM,
                 base_flags | f::l512 | rounding, e.tuple);
        }
    }

    std::vector<Form>& forms() { return forms_; }

private:
    std::vector<Form>& forms_;
    std::deque<std::string>& names_;
};

void float_arithmetic(Builder& b) {
    struct Operation {
        std::string_view name;
        std::uint8_t opcode;
        bool commute;
        std::uint32_t evex_rounding;  // rounding, sae, or none for the 512-bit and scalar forms
    };
    for (const auto& [name, opcode, commute, rounding] :
         {Operation{"add", 0x58, true, f::rounding}, Operation{"mul", 0x59, true, f::rounding},
          Operation{"sub", 0x5c, false, f::rounding}, Operation{"min", 0x5d, false, f::sae},
          Operation{"div", 0x5e, false, f::rounding}, Operation{"max", 0x5f, false, f::sae}}) {
        const std::uint32_t vex_flags = commute ? f::commute : 0U;
        const Evex ps{true, false, f::mask | f::zero | f::broadcast32 | rounding, T::Full};
        const Evex pd{true, true, f::mask | f::zero | f::broadcast64 | rounding, T::Full};
        b.packed(b.name(std::string(name) + "ps"), P::None, Mp::M0F, opcode, ps, true, true, vex_flags);
        b.packed(b.name(std::string(name) + "pd"), P::P66, Mp::M0F, opcode, pd, true, true, vex_flags);
        // Scalar ops take their upper lanes from the first source, so they
        // are not commutable for the two-byte VEX optimization.
        b.scalar(b.name(std::string(name) + "ss"), P::PF3, Mp::M0F, opcode, false, rounding);
        b.scalar(b.name(std::string(name) + "sd"), P::PF2, Mp::M0F, opcode, true, rounding);
    }
    // Bitwise operations (AVX512DQ for EVEX).
    struct Logic {
        std::string_view name;
        std::uint8_t opcode;
        bool commute;
    };
    for (const auto& [name, opcode, commute] :
         {Logic{"and", 0x54, true}, Logic{"andn", 0x55, false}, Logic{"or", 0x56, true}, Logic{"xor", 0x57, true}}) {
        const std::uint32_t vex_flags = commute ? f::commute : 0U;
        b.packed(b.name(std::string(name) + "ps"), P::None, Mp::M0F, opcode, evex32, true, true, vex_flags);
        b.packed(b.name(std::string(name) + "pd"), P::P66, Mp::M0F, opcode, evex64, true, true, vex_flags);
    }
    b.packed("unpcklps", P::None, Mp::M0F, 0x14, evex32);
    b.packed("unpcklpd", P::P66, Mp::M0F, 0x14, evex64);
    b.packed("unpckhps", P::None, Mp::M0F, 0x15, evex32);
    b.packed("unpckhpd", P::P66, Mp::M0F, 0x15, evex64);
    b.packed("addsubps", P::PF2, Mp::M0F, 0xd0, no_evex);
    b.packed("addsubpd", P::P66, Mp::M0F, 0xd0, no_evex);
    b.packed("haddps", P::PF2, Mp::M0F, 0x7c, no_evex);
    b.packed("haddpd", P::P66, Mp::M0F, 0x7c, no_evex);
    b.packed("hsubps", P::PF2, Mp::M0F, 0x7d, no_evex);
    b.packed("hsubpd", P::P66, Mp::M0F, 0x7d, no_evex);
    b.packed_imm("shufps", P::None, Mp::M0F, 0xc6, evex32);
    b.packed_imm("shufpd", P::P66, Mp::M0F, 0xc6, evex64);
    b.packed_imm("blendps", P::P66, Mp::M0F3A, 0x0c, no_evex);
    b.packed_imm("blendpd", P::P66, Mp::M0F3A, 0x0d, no_evex);
    b.packed_imm("dpps", P::P66, Mp::M0F3A, 0x40, no_evex);
    b.legacy("dppd", {K::Xmm, K::Xm128, K::I8}, Mp::M0F3A, P::P66, 0x41, -1, L::RM);
    b.vex("vdppd", {K::Xmm, K::Xmm, K::Xm128, K::I8}, Mp::M0F3A, P::P66, 0x41, -1, L::RVM);
    b.unary_imm("roundps", P::P66, Mp::M0F3A, 0x08, no_evex);
    b.unary_imm("roundpd", P::P66, Mp::M0F3A, 0x09, no_evex);
    for (const auto& [name, opcode, wide] :
         {std::tuple{std::string_view{"roundss"}, std::uint8_t{0x0a}, false},
          std::tuple{std::string_view{"roundsd"}, std::uint8_t{0x0b}, true}}) {
        const auto memory = wide ? K::Xm64 : K::Xm32;
        b.legacy(name, {K::Xmm, memory, K::I8}, Mp::M0F3A, P::P66, opcode, -1, L::RM);
        b.vex(b.v(name), {K::Xmm, K::Xmm, memory, K::I8}, Mp::M0F3A, P::P66, opcode, -1, L::RVM);
    }
    // Square roots and approximations.
    b.packed_unary("sqrtps", P::None, Mp::M0F, 0x51, evex32_round);
    b.packed_unary("sqrtpd", P::P66, Mp::M0F, 0x51, evex64_round);
    b.scalar("sqrtss", P::PF3, Mp::M0F, 0x51, false, f::rounding);
    b.scalar("sqrtsd", P::PF2, Mp::M0F, 0x51, true, f::rounding);
    b.packed_unary("rsqrtps", P::None, Mp::M0F, 0x52, no_evex);
    b.scalar("rsqrtss", P::PF3, Mp::M0F, 0x52, false, 0, false);
    b.packed_unary("rcpps", P::None, Mp::M0F, 0x53, no_evex);
    b.scalar("rcpss", P::PF3, Mp::M0F, 0x53, false, 0, false);

    // Comparisons with an immediate predicate, and their pseudo mnemonics.
    b.packed_imm("cmpps", P::None, Mp::M0F, 0xc2, no_evex);
    b.packed_imm("cmppd", P::P66, Mp::M0F, 0xc2, no_evex);
    for (const auto& [name, prefix, wide] :
         {std::tuple{std::string_view{"cmpss"}, P::PF3, false}, std::tuple{std::string_view{"cmpsd"}, P::PF2, true}}) {
        const auto memory = wide ? K::Xm64 : K::Xm32;
        b.legacy(name, {K::Xmm, memory, K::I8}, Mp::M0F, prefix, 0xc2, -1, L::RM);
        b.vex(b.v(name), {K::Xmm, K::Xmm, memory, K::I8}, Mp::M0F, prefix, 0xc2, -1, L::RVM);
        b.evex(b.v(name), {K::K, K::Xmm, memory, K::I8}, Mp::M0F, prefix, 0xc2, -1, L::RVM,
               f::mask | f::sae | (wide ? f::w | f::element64 : f::element32), T::Tuple1Scalar);
    }
    for (const auto& [name, prefix, w1, broadcast] :
         {std::tuple{std::string_view{"cmpps"}, P::None, false, f::broadcast32},
          std::tuple{std::string_view{"cmppd"}, P::P66, true, f::broadcast64}}) {
        const auto w = w1 ? f::w : 0U;
        b.evex(b.v(name), {K::K, K::Xmm, K::Xm128, K::I8}, Mp::M0F, prefix, 0xc2, -1, L::RVM,
               f::mask | broadcast | w, T::Full);
        b.evex(b.v(name), {K::K, K::Ymm, K::Ym256, K::I8}, Mp::M0F, prefix, 0xc2, -1, L::RVM,
               f::mask | broadcast | w | f::l256, T::Full);
        b.evex(b.v(name), {K::K, K::Zmm, K::Zm512, K::I8}, Mp::M0F, prefix, 0xc2, -1, L::RVM,
               f::mask | broadcast | w | f::l512 | f::sae, T::Full);
    }

    b.legacy("movmskps", {K::R32, K::Xmm}, Mp::M0F, P::None, 0x50, -1, L::RM);
    b.legacy("movmskps", {K::R64, K::Xmm}, Mp::M0F, P::None, 0x50, -1, L::RM);
    b.legacy("movmskpd", {K::R32, K::Xmm}, Mp::M0F, P::P66, 0x50, -1, L::RM);
    b.legacy("movmskpd", {K::R64, K::Xmm}, Mp::M0F, P::P66, 0x50, -1, L::RM);
    for (const auto& [name, prefix] : {std::pair{std::string_view{"vmovmskps"}, P::None},
                                       std::pair{std::string_view{"vmovmskpd"}, P::P66}}) {
        for (const auto destination : {K::R32, K::R64}) {
            b.vex(name, {destination, K::Xmm}, Mp::M0F, prefix, 0x50, -1, L::RM);
            b.vex(name, {destination, K::Ymm}, Mp::M0F, prefix, 0x50, -1, L::RM, f::l256);
        }
    }
    for (const auto& [name, prefix, opcode, wide] :
         {std::tuple{std::string_view{"comiss"}, P::None, std::uint8_t{0x2f}, false},
          std::tuple{std::string_view{"comisd"}, P::P66, std::uint8_t{0x2f}, true},
          std::tuple{std::string_view{"ucomiss"}, P::None, std::uint8_t{0x2e}, false},
          std::tuple{std::string_view{"ucomisd"}, P::P66, std::uint8_t{0x2e}, true}}) {
        const auto memory = wide ? K::Xm64 : K::Xm32;
        b.legacy(name, {K::Xmm, memory}, Mp::M0F, prefix, opcode, -1, L::RM);
        b.vex(b.v(name), {K::Xmm, memory}, Mp::M0F, prefix, opcode, -1, L::RM);
        b.evex(b.v(name), {K::Xmm, memory}, Mp::M0F, prefix, opcode, -1, L::RM,
               f::sae | (wide ? f::w | f::element64 : f::element32), T::Tuple1Scalar);
    }
}

void float_conversions(Builder& b) {
    // Scalar precision changes.
    b.scalar("cvtss2sd", P::PF3, Mp::M0F, 0x5a, false, f::sae);
    b.scalar("cvtsd2ss", P::PF2, Mp::M0F, 0x5a, true, f::rounding);
    b.legacy("cvtps2pd", {K::Xmm, K::Xm64}, Mp::M0F, P::None, 0x5a, -1, L::RM);
    b.vex("vcvtps2pd", {K::Xmm, K::Xm64}, Mp::M0F, P::None, 0x5a, -1, L::RM);
    b.vex("vcvtps2pd", {K::Ymm, K::Xm128}, Mp::M0F, P::None, 0x5a, -1, L::RM, f::l256);
    b.evex("vcvtps2pd", {K::Xmm, K::Xm64}, Mp::M0F, P::None, 0x5a, -1, L::RM, f::mask | f::zero | f::broadcast32, T::Half);
    b.evex("vcvtps2pd", {K::Ymm, K::Xm128}, Mp::M0F, P::None, 0x5a, -1, L::RM,
           f::mask | f::zero | f::broadcast32 | f::l256, T::Half);
    b.evex("vcvtps2pd", {K::Zmm, K::Ym256}, Mp::M0F, P::None, 0x5a, -1, L::RM,
           f::mask | f::zero | f::broadcast32 | f::l512 | f::sae, T::Half);
    b.legacy("cvtpd2ps", {K::Xmm, K::Xm128}, Mp::M0F, P::P66, 0x5a, -1, L::RM);
    b.vex("vcvtpd2ps", {K::Xmm, K::Xm128}, Mp::M0F, P::P66, 0x5a, -1, L::RM);
    b.vex("vcvtpd2ps", {K::Xmm, K::Ym256}, Mp::M0F, P::P66, 0x5a, -1, L::RM, f::l256);
    b.evex("vcvtpd2ps", {K::Ymm, K::Zm512}, Mp::M0F, P::P66, 0x5a, -1, L::RM,
           f::mask | f::zero | f::broadcast64 | f::w | f::l512 | f::rounding, T::Full);
    b.packed_unary("cvtdq2ps", P::None, Mp::M0F, 0x5b, evex32_round);
    b.packed_unary("cvtps2dq", P::P66, Mp::M0F, 0x5b, evex32_round);
    b.packed_unary("cvttps2dq", P::PF3, Mp::M0F, 0x5b, evex32_sae);
    b.legacy("cvtdq2pd", {K::Xmm, K::Xm64}, Mp::M0F, P::PF3, 0xe6, -1, L::RM);
    b.vex("vcvtdq2pd", {K::Xmm, K::Xm64}, Mp::M0F, P::PF3, 0xe6, -1, L::RM);
    b.vex("vcvtdq2pd", {K::Ymm, K::Xm128}, Mp::M0F, P::PF3, 0xe6, -1, L::RM, f::l256);
    b.evex("vcvtdq2pd", {K::Zmm, K::Ym256}, Mp::M0F, P::PF3, 0xe6, -1, L::RM,
           f::mask | f::zero | f::broadcast32 | f::l512, T::Half);
    for (const auto& [name, prefix] : {std::pair{std::string_view{"cvtpd2dq"}, P::PF2},
                                       std::pair{std::string_view{"cvttpd2dq"}, P::P66}}) {
        b.legacy(name, {K::Xmm, K::Xm128}, Mp::M0F, prefix, 0xe6, -1, L::RM);
        b.vex(b.v(name), {K::Xmm, K::Xm128}, Mp::M0F, prefix, 0xe6, -1, L::RM);
        b.vex(b.v(name), {K::Xmm, K::Ym256}, Mp::M0F, prefix, 0xe6, -1, L::RM, f::l256);
    }
    // Integer to scalar float: the AT&T suffix (l, q) names the integer.
    for (const auto& [name, prefix] : {std::pair{std::string_view{"cvtsi2ss"}, P::PF3},
                                       std::pair{std::string_view{"cvtsi2sd"}, P::PF2}}) {
        for (const auto size : {32U, 64U}) {
            const auto source = size == 32 ? K::Rm32 : K::Rm64;
            const auto w = size == 64 ? f::w : 0U;
            const auto s = static_cast<std::uint8_t>(size);
            b.add(name, {K::Xmm, source}, Space::Legacy, Mp::M0F, prefix, 0x2a, -1, L::RM, w);
            b.forms().back().size = s;
            b.add(b.v(name), {K::Xmm, K::Xmm, source}, Space::Vex, Mp::M0F, prefix, 0x2a, -1, L::RVM, w);
            b.forms().back().size = s;
            b.add(b.v(name), {K::Xmm, K::Xmm, source}, Space::Evex, Mp::M0F, prefix, 0x2a, -1, L::RVM,
                  w | (size == 64 || name == "cvtsi2ss" ? f::rounding : 0U) | (size == 64 ? f::element64 : f::element32),
                  T::Tuple1Scalar);
            b.forms().back().size = s;
        }
    }
    // Scalar float to integer: the destination register names the size.
    for (const auto& [name, prefix, opcode, wide, rounding] :
         {std::tuple{std::string_view{"cvttss2si"}, P::PF3, std::uint8_t{0x2c}, false, f::sae},
          std::tuple{std::string_view{"cvttsd2si"}, P::PF2, std::uint8_t{0x2c}, true, f::sae},
          std::tuple{std::string_view{"cvtss2si"}, P::PF3, std::uint8_t{0x2d}, false, f::rounding},
          std::tuple{std::string_view{"cvtsd2si"}, P::PF2, std::uint8_t{0x2d}, true, f::rounding}}) {
        const auto source = wide ? K::Xm64 : K::Xm32;
        for (const auto size : {32U, 64U}) {
            const auto destination = size == 32 ? K::R32 : K::R64;
            const auto w = size == 64 ? f::w : 0U;
            const auto s = static_cast<std::uint8_t>(size);
            b.add(name, {destination, source}, Space::Legacy, Mp::M0F, prefix, opcode, -1, L::RM, w);
            b.forms().back().size = s;
            b.add(b.v(name), {destination, source}, Space::Vex, Mp::M0F, prefix, opcode, -1, L::RM, w);
            b.forms().back().size = s;
            b.add(b.v(name), {destination, source}, Space::Evex, Mp::M0F, prefix, opcode, -1, L::RM,
                  w | rounding | (wide ? f::element64 : f::element32), T::Tuple1Fixed);
            b.forms().back().size = s;
        }
    }
    // Half precision.
    b.vex("vcvtph2ps", {K::Xmm, K::Xm64}, Mp::M0F38, P::P66, 0x13, -1, L::RM);
    b.vex("vcvtph2ps", {K::Ymm, K::Xm128}, Mp::M0F38, P::P66, 0x13, -1, L::RM, f::l256);
    b.evex("vcvtph2ps", {K::Zmm, K::Ym256}, Mp::M0F38, P::P66, 0x13, -1, L::RM,
           f::mask | f::zero | f::l512 | f::sae, T::HalfMem);
    b.vex("vcvtps2ph", {K::Xm64, K::Xmm, K::I8}, Mp::M0F3A, P::P66, 0x1d, -1, L::MR);
    b.vex("vcvtps2ph", {K::Xm128, K::Ymm, K::I8}, Mp::M0F3A, P::P66, 0x1d, -1, L::MR, f::l256);
    b.evex("vcvtps2ph", {K::Ym256, K::Zmm, K::I8}, Mp::M0F3A, P::P66, 0x1d, -1, L::MR,
           f::mask | f::zero | f::l512 | f::sae, T::HalfMem);
}

void integer_simd(Builder& b) {
    struct Operation {
        std::string_view name;
        P prefix;
        Mp map;
        std::uint8_t opcode;
        Evex evex;
        std::uint32_t vex_flags;
    };
    // Two-source operations on xmm and ymm, with EVEX forms where AVX-512
    // has them under the same mnemonic.
    for (const auto& [name, prefix, map, opcode, evex, vex_flags] : {
             Operation{"paddb", P::P66, Mp::M0F, 0xfc, evex_bw, f::commute},
             Operation{"paddw", P::P66, Mp::M0F, 0xfd, evex_bw, f::commute},
             Operation{"paddd", P::P66, Mp::M0F, 0xfe, evex32, f::commute},
             Operation{"paddq", P::P66, Mp::M0F, 0xd4, evex64, f::commute},
             Operation{"psubb", P::P66, Mp::M0F, 0xf8, evex_bw, 0},
             Operation{"psubw", P::P66, Mp::M0F, 0xf9, evex_bw, 0},
             Operation{"psubd", P::P66, Mp::M0F, 0xfa, evex32, 0},
             Operation{"psubq", P::P66, Mp::M0F, 0xfb, evex64, 0},
             Operation{"paddsb", P::P66, Mp::M0F, 0xec, evex_bw, f::commute},
             Operation{"paddsw", P::P66, Mp::M0F, 0xed, evex_bw, f::commute},
             Operation{"paddusb", P::P66, Mp::M0F, 0xdc, evex_bw, f::commute},
             Operation{"paddusw", P::P66, Mp::M0F, 0xdd, evex_bw, f::commute},
             Operation{"psubsb", P::P66, Mp::M0F, 0xe8, evex_bw, 0},
             Operation{"psubsw", P::P66, Mp::M0F, 0xe9, evex_bw, 0},
             Operation{"psubusb", P::P66, Mp::M0F, 0xd8, evex_bw, 0},
             Operation{"psubusw", P::P66, Mp::M0F, 0xd9, evex_bw, 0},
             Operation{"pmullw", P::P66, Mp::M0F, 0xd5, evex_bw, f::commute},
             Operation{"pmulhw", P::P66, Mp::M0F, 0xe5, evex_bw, f::commute},
             Operation{"pmulhuw", P::P66, Mp::M0F, 0xe4, evex_bw, f::commute},
             Operation{"pmuludq", P::P66, Mp::M0F, 0xf4, evex64, f::commute},
             Operation{"pmaddwd", P::P66, Mp::M0F, 0xf5, evex_bw, f::commute},
             Operation{"pavgb", P::P66, Mp::M0F, 0xe0, evex_bw, f::commute},
             Operation{"pavgw", P::P66, Mp::M0F, 0xe3, evex_bw, f::commute},
             Operation{"pminub", P::P66, Mp::M0F, 0xda, evex_bw, f::commute},
             Operation{"pmaxub", P::P66, Mp::M0F, 0xde, evex_bw, f::commute},
             Operation{"pminsw", P::P66, Mp::M0F, 0xea, evex_bw, f::commute},
             Operation{"pmaxsw", P::P66, Mp::M0F, 0xee, evex_bw, f::commute},
             Operation{"psadbw", P::P66, Mp::M0F, 0xf6, Evex{true, false, 0, T::FullMem}, f::commute},
             Operation{"pand", P::P66, Mp::M0F, 0xdb, no_evex, f::commute},
             Operation{"pandn", P::P66, Mp::M0F, 0xdf, no_evex, 0},
             Operation{"por", P::P66, Mp::M0F, 0xeb, no_evex, f::commute},
             Operation{"pxor", P::P66, Mp::M0F, 0xef, no_evex, f::commute},
             Operation{"pcmpeqb", P::P66, Mp::M0F, 0x74, no_evex, f::commute},
             Operation{"pcmpeqw", P::P66, Mp::M0F, 0x75, no_evex, f::commute},
             Operation{"pcmpeqd", P::P66, Mp::M0F, 0x76, no_evex, f::commute},
             Operation{"pcmpgtb", P::P66, Mp::M0F, 0x64, no_evex, 0},
             Operation{"pcmpgtw", P::P66, Mp::M0F, 0x65, no_evex, 0},
             Operation{"pcmpgtd", P::P66, Mp::M0F, 0x66, no_evex, 0},
             Operation{"packsswb", P::P66, Mp::M0F, 0x63, evex_bw, 0},
             Operation{"packssdw", P::P66, Mp::M0F, 0x6b, evex32, 0},
             Operation{"packuswb", P::P66, Mp::M0F, 0x67, evex_bw, 0},
             Operation{"punpcklbw", P::P66, Mp::M0F, 0x60, evex_bw, 0},
             Operation{"punpcklwd", P::P66, Mp::M0F, 0x61, evex_bw, 0},
             Operation{"punpckldq", P::P66, Mp::M0F, 0x62, evex32, 0},
             Operation{"punpcklqdq", P::P66, Mp::M0F, 0x6c, evex64, 0},
             Operation{"punpckhbw", P::P66, Mp::M0F, 0x68, evex_bw, 0},
             Operation{"punpckhwd", P::P66, Mp::M0F, 0x69, evex_bw, 0},
             Operation{"punpckhdq", P::P66, Mp::M0F, 0x6a, evex32, 0},
             Operation{"punpckhqdq", P::P66, Mp::M0F, 0x6d, evex64, 0},
             Operation{"pshufb", P::P66, Mp::M0F38, 0x00, evex_bw, 0},
             Operation{"phaddw", P::P66, Mp::M0F38, 0x01, no_evex, 0},
             Operation{"phaddd", P::P66, Mp::M0F38, 0x02, no_evex, 0},
             Operation{"phaddsw", P::P66, Mp::M0F38, 0x03, no_evex, 0},
             Operation{"pmaddubsw", P::P66, Mp::M0F38, 0x04, evex_bw, 0},
             Operation{"phsubw", P::P66, Mp::M0F38, 0x05, no_evex, 0},
             Operation{"phsubd", P::P66, Mp::M0F38, 0x06, no_evex, 0},
             Operation{"phsubsw", P::P66, Mp::M0F38, 0x07, no_evex, 0},
             Operation{"psignb", P::P66, Mp::M0F38, 0x08, no_evex, 0},
             Operation{"psignw", P::P66, Mp::M0F38, 0x09, no_evex, 0},
             Operation{"psignd", P::P66, Mp::M0F38, 0x0a, no_evex, 0},
             Operation{"pmulhrsw", P::P66, Mp::M0F38, 0x0b, evex_bw, 0},
             Operation{"pmuldq", P::P66, Mp::M0F38, 0x28, evex64, 0},
             Operation{"pcmpeqq", P::P66, Mp::M0F38, 0x29, no_evex, 0},
             Operation{"packusdw", P::P66, Mp::M0F38, 0x2b, evex32, 0},
             Operation{"pcmpgtq", P::P66, Mp::M0F38, 0x37, no_evex, 0},
             Operation{"pminsb", P::P66, Mp::M0F38, 0x38, evex_bw, 0},
             Operation{"pminsd", P::P66, Mp::M0F38, 0x39, evex32, 0},
             Operation{"pminuw", P::P66, Mp::M0F38, 0x3a, evex_bw, 0},
             Operation{"pminud", P::P66, Mp::M0F38, 0x3b, evex32, 0},
             Operation{"pmaxsb", P::P66, Mp::M0F38, 0x3c, evex_bw, 0},
             Operation{"pmaxsd", P::P66, Mp::M0F38, 0x3d, evex32, 0},
             Operation{"pmaxuw", P::P66, Mp::M0F38, 0x3e, evex_bw, 0},
             Operation{"pmaxud", P::P66, Mp::M0F38, 0x3f, evex32, 0},
             Operation{"pmulld", P::P66, Mp::M0F38, 0x40, evex32, 0},
             Operation{"aesenc", P::P66, Mp::M0F38, 0xdc, Evex{true, false, 0, T::FullMem}, 0},
             Operation{"aesenclast", P::P66, Mp::M0F38, 0xdd, Evex{true, false, 0, T::FullMem}, 0},
             Operation{"aesdec", P::P66, Mp::M0F38, 0xde, Evex{true, false, 0, T::FullMem}, 0},
             Operation{"aesdeclast", P::P66, Mp::M0F38, 0xdf, Evex{true, false, 0, T::FullMem}, 0},
             Operation{"gf2p8mulb", P::P66, Mp::M0F38, 0xcf, evex_bw, 0},
         }) {
        b.packed(name, prefix, map, opcode, evex, true, true, vex_flags);
    }
    // VEX-only AVX2 operations and their AVX-512 extensions.
    b.packed("psllvd", P::P66, Mp::M0F38, 0x47, evex32, false);
    b.packed("psllvq", P::P66, Mp::M0F38, 0x47, evex64, false, true, 0, true);
    b.packed("psrlvd", P::P66, Mp::M0F38, 0x45, evex32, false);
    b.packed("psrlvq", P::P66, Mp::M0F38, 0x45, evex64, false, true, 0, true);
    b.packed("psravd", P::P66, Mp::M0F38, 0x46, evex32, false);
    b.packed("psravq", P::P66, Mp::M0F38, 0x46, evex64, false, false);
    b.packed("psllvw", P::P66, Mp::M0F38, 0x12, Evex{true, true, f::mask | f::zero, T::FullMem}, false, false);
    b.packed("psrlvw", P::P66, Mp::M0F38, 0x10, Evex{true, true, f::mask | f::zero, T::FullMem}, false, false);
    b.packed("psravw", P::P66, Mp::M0F38, 0x11, Evex{true, true, f::mask | f::zero, T::FullMem}, false, false);
    b.packed_imm("pblendd", P::P66, Mp::M0F3A, 0x02, no_evex, false);
    // Full permutes exist for 256 and 512 bits only.
    for (const auto& [name, opcode] : {std::pair{std::string_view{"vpermd"}, std::uint8_t{0x36}},
                                       std::pair{std::string_view{"vpermps"}, std::uint8_t{0x16}}}) {
        b.vex(name, {K::Ymm, K::Ymm, K::Ym256}, Mp::M0F38, P::P66, opcode, -1, L::RVM, f::l256);
        b.evex(name, {K::Ymm, K::Ymm, K::Ym256}, Mp::M0F38, P::P66, opcode, -1, L::RVM,
               f::l256 | f::mask | f::zero | f::broadcast32, T::Full);
        b.evex(name, {K::Zmm, K::Zmm, K::Zm512}, Mp::M0F38, P::P66, opcode, -1, L::RVM,
               f::l512 | f::mask | f::zero | f::broadcast32, T::Full);
    }
    for (const auto& [name, opcode] : {std::pair{std::string_view{"vpermq"}, std::uint8_t{0x00}},
                                       std::pair{std::string_view{"vpermpd"}, std::uint8_t{0x01}}}) {
        b.vex(name, {K::Ymm, K::Ym256, K::I8}, Mp::M0F3A, P::P66, opcode, -1, L::RM, f::w | f::l256);
        b.evex(name, {K::Ymm, K::Ym256, K::I8}, Mp::M0F3A, P::P66, opcode, -1, L::RM,
               f::w | f::l256 | f::mask | f::zero | f::broadcast64, T::Full);
        b.evex(name, {K::Zmm, K::Zm512, K::I8}, Mp::M0F3A, P::P66, opcode, -1, L::RM,
               f::w | f::l512 | f::mask | f::zero | f::broadcast64, T::Full);
    }
    b.packed("permilps", P::P66, Mp::M0F38, 0x0c, evex32, false);
    b.packed("permilpd", P::P66, Mp::M0F38, 0x0d, evex64, false);
    b.unary_imm("permilps", P::P66, Mp::M0F3A, 0x04, evex32, false);
    b.unary_imm("permilpd", P::P66, Mp::M0F3A, 0x05, evex64, false);
    b.packed("permb", P::P66, Mp::M0F38, 0x8d, Evex{true, false, f::mask | f::zero, T::FullMem}, false, false);
    b.packed("permw", P::P66, Mp::M0F38, 0x8d, Evex{true, true, f::mask | f::zero, T::FullMem}, false, false);
    for (const auto& [name, opcode] : {std::pair{std::string_view{"vperm2f128"}, std::uint8_t{0x06}},
                                       std::pair{std::string_view{"vperm2i128"}, std::uint8_t{0x46}}}) {
        b.vex(name, {K::Ymm, K::Ymm, K::Ym256, K::I8}, Mp::M0F3A, P::P66, opcode, -1, L::RVM, f::l256);
    }

    // Unary integer operations.
    b.packed_unary("pabsb", P::P66, Mp::M0F38, 0x1c, evex_bw);
    b.packed_unary("pabsw", P::P66, Mp::M0F38, 0x1d, evex_bw);
    b.packed_unary("pabsd", P::P66, Mp::M0F38, 0x1e, evex32);
    b.packed_unary("pabsq", P::P66, Mp::M0F38, 0x1f, evex64, false, false);
    b.legacy("ptest", {K::Xmm, K::Xm128}, Mp::M0F38, P::P66, 0x17, -1, L::RM);
    b.vex("vptest", {K::Xmm, K::Xm128}, Mp::M0F38, P::P66, 0x17, -1, L::RM);
    b.vex("vptest", {K::Ymm, K::Ym256}, Mp::M0F38, P::P66, 0x17, -1, L::RM, f::l256);
    b.vex("vtestps", {K::Xmm, K::Xm128}, Mp::M0F38, P::P66, 0x0e, -1, L::RM);
    b.vex("vtestps", {K::Ymm, K::Ym256}, Mp::M0F38, P::P66, 0x0e, -1, L::RM, f::l256);
    b.vex("vtestpd", {K::Xmm, K::Xm128}, Mp::M0F38, P::P66, 0x0f, -1, L::RM);
    b.vex("vtestpd", {K::Ymm, K::Ym256}, Mp::M0F38, P::P66, 0x0f, -1, L::RM, f::l256);
    b.legacy("phminposuw", {K::Xmm, K::Xm128}, Mp::M0F38, P::P66, 0x41, -1, L::RM);
    b.vex("vphminposuw", {K::Xmm, K::Xm128}, Mp::M0F38, P::P66, 0x41, -1, L::RM);
    b.legacy("aesimc", {K::Xmm, K::Xm128}, Mp::M0F38, P::P66, 0xdb, -1, L::RM);
    b.vex("vaesimc", {K::Xmm, K::Xm128}, Mp::M0F38, P::P66, 0xdb, -1, L::RM);
    b.legacy("aeskeygenassist", {K::Xmm, K::Xm128, K::I8}, Mp::M0F3A, P::P66, 0xdf, -1, L::RM);
    b.vex("vaeskeygenassist", {K::Xmm, K::Xm128, K::I8}, Mp::M0F3A, P::P66, 0xdf, -1, L::RM);
    // Sign and zero extension of packed integers.
    struct Extend {
        std::string_view name;
        std::uint8_t opcode;
        unsigned ratio;  // source bytes per 16 destination bytes
        T tuple;
        std::uint32_t evex_w;
    };
    for (const auto& [name, opcode, ratio, tuple, evex_w] :
         {Extend{"pmovsxbw", 0x20, 8, T::HalfMem, 0}, Extend{"pmovsxbd", 0x21, 4, T::QuarterMem, 0},
          Extend{"pmovsxbq", 0x22, 2, T::EighthMem, 0}, Extend{"pmovsxwd", 0x23, 8, T::HalfMem, 0},
          Extend{"pmovsxwq", 0x24, 4, T::QuarterMem, 0}, Extend{"pmovsxdq", 0x25, 8, T::HalfMem, 0},
          Extend{"pmovzxbw", 0x30, 8, T::HalfMem, 0}, Extend{"pmovzxbd", 0x31, 4, T::QuarterMem, 0},
          Extend{"pmovzxbq", 0x32, 2, T::EighthMem, 0}, Extend{"pmovzxwd", 0x33, 8, T::HalfMem, 0},
          Extend{"pmovzxwq", 0x34, 4, T::QuarterMem, 0}, Extend{"pmovzxdq", 0x35, 8, T::HalfMem, 0}}) {
        const auto memory = [](unsigned bytes) {
            return bytes == 2 ? K::Xm16 : bytes == 4 ? K::Xm32 : bytes == 8 ? K::Xm64 : bytes == 16 ? K::Xm128
                                                                                                    : K::Ym256;
        };
        b.legacy(name, {K::Xmm, memory(ratio)}, Mp::M0F38, P::P66, opcode, -1, L::RM);
        b.vex(b.v(name), {K::Xmm, memory(ratio)}, Mp::M0F38, P::P66, opcode, -1, L::RM);
        b.vex(b.v(name), {K::Ymm, memory(ratio * 2)}, Mp::M0F38, P::P66, opcode, -1, L::RM, f::l256);
        b.evex(b.v(name), {K::Xmm, memory(ratio)}, Mp::M0F38, P::P66, opcode, -1, L::RM, f::mask | f::zero | evex_w, tuple);
        b.evex(b.v(name), {K::Ymm, memory(ratio * 2)}, Mp::M0F38, P::P66, opcode, -1, L::RM,
               f::mask | f::zero | f::l256 | evex_w, tuple);
        b.evex(b.v(name), {K::Zmm, memory(ratio * 4)}, Mp::M0F38, P::P66, opcode, -1, L::RM,
               f::mask | f::zero | f::l512 | evex_w, tuple);
    }

    // Shifts by a register count (xmm/m128) and by an immediate.
    struct Shift {
        std::string_view name;
        std::uint8_t opcode;
        std::uint8_t immediate_opcode;
        std::uint8_t digit;
        Evex evex;
    };
    for (const auto& [name, opcode, immediate_opcode, digit, evex] :
         {Shift{"psllw", 0xf1, 0x71, 6, Evex{true, false, f::mask | f::zero, T::Mem128}},
          Shift{"pslld", 0xf2, 0x72, 6, Evex{true, false, f::mask | f::zero, T::Mem128}},
          Shift{"psllq", 0xf3, 0x73, 6, Evex{true, true, f::mask | f::zero, T::Mem128}},
          Shift{"psrlw", 0xd1, 0x71, 2, Evex{true, false, f::mask | f::zero, T::Mem128}},
          Shift{"psrld", 0xd2, 0x72, 2, Evex{true, false, f::mask | f::zero, T::Mem128}},
          Shift{"psrlq", 0xd3, 0x73, 2, Evex{true, true, f::mask | f::zero, T::Mem128}},
          Shift{"psraw", 0xe1, 0x71, 4, Evex{true, false, f::mask | f::zero, T::Mem128}},
          Shift{"psrad", 0xe2, 0x72, 4, Evex{true, false, f::mask | f::zero, T::Mem128}}}) {
        const auto w = evex.w1 ? f::w : 0U;
        const auto name_v = b.v(name);
        b.legacy(name, {K::Xmm, K::Xm128}, Mp::M0F, P::P66, opcode, -1, L::RM);
        b.legacy(name, {K::Xmm, K::I8}, Mp::M0F, P::P66, immediate_opcode, digit, L::M);
        b.vex(name_v, {K::Xmm, K::Xmm, K::Xm128}, Mp::M0F, P::P66, opcode, -1, L::RVM);
        b.vex(name_v, {K::Ymm, K::Ymm, K::Xm128}, Mp::M0F, P::P66, opcode, -1, L::RVM, f::l256);
        b.vex(name_v, {K::Xmm, K::Xmm, K::I8}, Mp::M0F, P::P66, immediate_opcode, digit, L::VM);
        b.vex(name_v, {K::Ymm, K::Ymm, K::I8}, Mp::M0F, P::P66, immediate_opcode, digit, L::VM, f::l256);
        b.evex(name_v, {K::Xmm, K::Xmm, K::Xm128}, Mp::M0F, P::P66, opcode, -1, L::RVM, evex.flags | w, T::Mem128);
        b.evex(name_v, {K::Ymm, K::Ymm, K::Xm128}, Mp::M0F, P::P66, opcode, -1, L::RVM,
               evex.flags | w | f::l256, T::Mem128);
        b.evex(name_v, {K::Zmm, K::Zmm, K::Xm128}, Mp::M0F, P::P66, opcode, -1, L::RVM,
               evex.flags | w | f::l512, T::Mem128);
        const bool words = name.ends_with('w');
        const auto imm_flags = evex.flags | w | (words ? 0U : (evex.w1 ? f::broadcast64 : f::broadcast32));
        const auto imm_tuple = words ? T::FullMem : T::Full;
        b.evex(name_v, {K::Xmm, K::Xm128, K::I8}, Mp::M0F, P::P66, immediate_opcode, digit, L::VM, imm_flags, imm_tuple);
        b.evex(name_v, {K::Ymm, K::Ym256, K::I8}, Mp::M0F, P::P66, immediate_opcode, digit, L::VM,
               imm_flags | f::l256, imm_tuple);
        b.evex(name_v, {K::Zmm, K::Zm512, K::I8}, Mp::M0F, P::P66, immediate_opcode, digit, L::VM,
               imm_flags | f::l512, imm_tuple);
    }
    for (const auto& [name, digit] : {std::pair{std::string_view{"pslldq"}, std::uint8_t{7}},
                                      std::pair{std::string_view{"psrldq"}, std::uint8_t{3}}}) {
        const auto name_v = b.v(name);
        b.legacy(name, {K::Xmm, K::I8}, Mp::M0F, P::P66, 0x73, digit, L::M);
        b.vex(name_v, {K::Xmm, K::Xmm, K::I8}, Mp::M0F, P::P66, 0x73, digit, L::VM);
        b.vex(name_v, {K::Ymm, K::Ymm, K::I8}, Mp::M0F, P::P66, 0x73, digit, L::VM, f::l256);
        b.evex(name_v, {K::Xmm, K::Xm128, K::I8}, Mp::M0F, P::P66, 0x73, digit, L::VM, 0, T::FullMem);
        b.evex(name_v, {K::Ymm, K::Ym256, K::I8}, Mp::M0F, P::P66, 0x73, digit, L::VM, f::l256, T::FullMem);
        b.evex(name_v, {K::Zmm, K::Zm512, K::I8}, Mp::M0F, P::P66, 0x73, digit, L::VM, f::l512, T::FullMem);
    }
    b.evex("vpsraq", {K::Xmm, K::Xmm, K::Xm128}, Mp::M0F, P::P66, 0xe2, -1, L::RVM, f::w | f::mask | f::zero, T::Mem128);
    b.evex("vpsraq", {K::Ymm, K::Ymm, K::Xm128}, Mp::M0F, P::P66, 0xe2, -1, L::RVM,
           f::w | f::mask | f::zero | f::l256, T::Mem128);
    b.evex("vpsraq", {K::Zmm, K::Zmm, K::Xm128}, Mp::M0F, P::P66, 0xe2, -1, L::RVM,
           f::w | f::mask | f::zero | f::l512, T::Mem128);

    // Shuffles and blends with immediates.
    b.unary_imm("pshufd", P::P66, Mp::M0F, 0x70, evex32);
    b.unary_imm("pshufhw", P::PF3, Mp::M0F, 0x70, evex_bw);
    b.unary_imm("pshuflw", P::PF2, Mp::M0F, 0x70, evex_bw);
    b.packed_imm("palignr", P::P66, Mp::M0F3A, 0x0f, evex_bw);
    b.packed_imm("pblendw", P::P66, Mp::M0F3A, 0x0e, no_evex);
    b.packed_imm("mpsadbw", P::P66, Mp::M0F3A, 0x42, no_evex);
    b.packed_imm("pclmulqdq", P::P66, Mp::M0F3A, 0x44, Evex{true, false, 0, T::FullMem});
    b.packed_imm("gf2p8affineqb", P::P66, Mp::M0F3A, 0xce, Evex{true, true, f::mask | f::zero | f::broadcast64, T::Full},
                 true, true, true);
    b.packed_imm("gf2p8affineinvqb", P::P66, Mp::M0F3A, 0xcf,
                 Evex{true, true, f::mask | f::zero | f::broadcast64, T::Full}, true, true, true);
    b.legacy("insertps", {K::Xmm, K::Xm32, K::I8}, Mp::M0F3A, P::P66, 0x21, -1, L::RM);
    b.vex("vinsertps", {K::Xmm, K::Xmm, K::Xm32, K::I8}, Mp::M0F3A, P::P66, 0x21, -1, L::RVM);
    b.evex("vinsertps", {K::Xmm, K::Xmm, K::Xm32, K::I8}, Mp::M0F3A, P::P66, 0x21, -1, L::RVM, f::element32,
           T::Tuple1Scalar);
    // Variable blends: legacy forms take xmm0 implicitly; VEX names the
    // selector in imm8[7:4].
    for (const auto& [name, opcode, vex_opcode] :
         {std::tuple{std::string_view{"blendvps"}, std::uint8_t{0x14}, std::uint8_t{0x4a}},
          std::tuple{std::string_view{"blendvpd"}, std::uint8_t{0x15}, std::uint8_t{0x4b}},
          std::tuple{std::string_view{"pblendvb"}, std::uint8_t{0x10}, std::uint8_t{0x4c}}}) {
        b.legacy(name, {K::Xmm, K::Xm128, K::Xmm0}, Mp::M0F38, P::P66, opcode, -1, L::RM);
        b.legacy(name, {K::Xmm, K::Xm128}, Mp::M0F38, P::P66, opcode, -1, L::RM);
        b.vex(b.v(name), {K::Xmm, K::Xmm, K::Xm128, K::Xmm}, Mp::M0F3A, P::P66, vex_opcode, -1, L::RVMR);
        b.vex(b.v(name), {K::Ymm, K::Ymm, K::Ym256, K::Ymm}, Mp::M0F3A, P::P66, vex_opcode, -1, L::RVMR, f::l256);
    }
    // SHA.
    b.legacy("sha1rnds4", {K::Xmm, K::Xm128, K::I8}, Mp::M0F3A, P::None, 0xcc, -1, L::RM);
    b.legacy("sha1nexte", {K::Xmm, K::Xm128}, Mp::M0F38, P::None, 0xc8, -1, L::RM);
    b.legacy("sha1msg1", {K::Xmm, K::Xm128}, Mp::M0F38, P::None, 0xc9, -1, L::RM);
    b.legacy("sha1msg2", {K::Xmm, K::Xm128}, Mp::M0F38, P::None, 0xca, -1, L::RM);
    b.legacy("sha256rnds2", {K::Xmm, K::Xm128, K::Xmm0}, Mp::M0F38, P::None, 0xcb, -1, L::RM);
    b.legacy("sha256rnds2", {K::Xmm, K::Xm128}, Mp::M0F38, P::None, 0xcb, -1, L::RM);
    b.legacy("sha256msg1", {K::Xmm, K::Xm128}, Mp::M0F38, P::None, 0xcc, -1, L::RM);
    b.legacy("sha256msg2", {K::Xmm, K::Xm128}, Mp::M0F38, P::None, 0xcd, -1, L::RM);

    // Element insertion and extraction.
    b.legacy("pmovmskb", {K::R32, K::Xmm}, Mp::M0F, P::P66, 0xd7, -1, L::RM);
    b.legacy("pmovmskb", {K::R64, K::Xmm}, Mp::M0F, P::P66, 0xd7, -1, L::RM);
    for (const auto destination : {K::R32, K::R64}) {
        b.vex("vpmovmskb", {destination, K::Xmm}, Mp::M0F, P::P66, 0xd7, -1, L::RM);
        b.vex("vpmovmskb", {destination, K::Ymm}, Mp::M0F, P::P66, 0xd7, -1, L::RM, f::l256);
    }
    b.legacy("pextrw", {K::R32, K::Xmm, K::I8}, Mp::M0F, P::P66, 0xc5, -1, L::RM);
    b.legacy("pextrw", {K::R64, K::Xmm, K::I8}, Mp::M0F, P::P66, 0xc5, -1, L::RM);
    b.legacy("pextrw", {K::M16, K::Xmm, K::I8}, Mp::M0F3A, P::P66, 0x15, -1, L::MR);
    b.vex("vpextrw", {K::R32, K::Xmm, K::I8}, Mp::M0F, P::P66, 0xc5, -1, L::RM);
    b.legacy("pinsrw", {K::Xmm, K::R32, K::I8}, Mp::M0F, P::P66, 0xc4, -1, L::RM);
    b.legacy("pinsrw", {K::Xmm, K::M16, K::I8}, Mp::M0F, P::P66, 0xc4, -1, L::RM);
    b.vex("vpinsrw", {K::Xmm, K::Xmm, K::R32, K::I8}, Mp::M0F, P::P66, 0xc4, -1, L::RVM);
    b.vex("vpinsrw", {K::Xmm, K::Xmm, K::M16, K::I8}, Mp::M0F, P::P66, 0xc4, -1, L::RVM);
    struct Element {
        std::string_view name;
        std::uint8_t opcode;
        K memory;
        std::uint32_t w;
    };
    for (const auto& [name, opcode, memory, w] :
         {Element{"pextrb", 0x14, K::M8, 0}, Element{"pextrd", 0x16, K::M32, 0}, Element{"pextrq", 0x16, K::M64, f::w},
          Element{"extractps", 0x17, K::M32, 0}}) {
        const auto destination = w != 0 ? K::Rm64 : K::Rm32;
        b.legacy(name, {destination, K::Xmm, K::I8}, Mp::M0F3A, P::P66, opcode, -1, L::MR, w);
        if (memory == K::M8) b.legacy(name, {K::M8, K::Xmm, K::I8}, Mp::M0F3A, P::P66, opcode, -1, L::MR);
        b.vex(b.v(name), {destination, K::Xmm, K::I8}, Mp::M0F3A, P::P66, opcode, -1, L::MR, w);
        if (memory == K::M8) b.vex(b.v(name), {K::M8, K::Xmm, K::I8}, Mp::M0F3A, P::P66, opcode, -1, L::MR);
    }
    for (const auto& [name, opcode, memory, w] :
         {Element{"pinsrb", 0x20, K::M8, 0}, Element{"pinsrd", 0x22, K::M32, 0}, Element{"pinsrq", 0x22, K::M64, f::w}}) {
        const auto source = w != 0 ? K::Rm64 : K::Rm32;
        b.legacy(name, {K::Xmm, source, K::I8}, Mp::M0F3A, P::P66, opcode, -1, L::RM, w);
        if (memory == K::M8) b.legacy(name, {K::Xmm, K::M8, K::I8}, Mp::M0F3A, P::P66, opcode, -1, L::RM);
        b.vex(b.v(name), {K::Xmm, K::Xmm, source, K::I8}, Mp::M0F3A, P::P66, opcode, -1, L::RVM, w);
        if (memory == K::M8) b.vex(b.v(name), {K::Xmm, K::Xmm, K::M8, K::I8}, Mp::M0F3A, P::P66, opcode, -1, L::RVM);
    }
}

void moves(Builder& b) {
    struct Move {
        std::string_view name;
        P prefix;
        std::uint8_t load;
        std::uint8_t store;
        bool vex_name;  // the VEX mnemonic is v + name
        Evex evex;
    };
    for (const auto& [name, prefix, load, store, vex_name, evex] :
         {Move{"movaps", P::None, 0x28, 0x29, true, Evex{true, false, f::mask | f::zero, T::FullMem}},
          Move{"movapd", P::P66, 0x28, 0x29, true, Evex{true, true, f::mask | f::zero, T::FullMem}},
          Move{"movups", P::None, 0x10, 0x11, true, Evex{true, false, f::mask | f::zero, T::FullMem}},
          Move{"movupd", P::P66, 0x10, 0x11, true, Evex{true, true, f::mask | f::zero, T::FullMem}},
          Move{"movdqa", P::P66, 0x6f, 0x7f, true, no_evex},
          Move{"movdqu", P::PF3, 0x6f, 0x7f, true, no_evex}}) {
        b.legacy(name, {K::Xmm, K::Xm128}, Mp::M0F, prefix, load, -1, L::RM);
        b.legacy(name, {K::Xm128, K::Xmm}, Mp::M0F, prefix, store, -1, L::MR);
        const auto name_v = b.v(name);
        b.vex(name_v, {K::Xmm, K::Xm128}, Mp::M0F, prefix, load, -1, L::RM, f::reverse);
        b.vex(name_v, {K::Xm128, K::Xmm}, Mp::M0F, prefix, store, -1, L::MR);
        b.vex(name_v, {K::Ymm, K::Ym256}, Mp::M0F, prefix, load, -1, L::RM, f::reverse | f::l256);
        b.vex(name_v, {K::Ym256, K::Ymm}, Mp::M0F, prefix, store, -1, L::MR, f::l256);
        if (!evex.present) continue;
        const auto w = evex.w1 ? f::w : 0U;
        for (const auto& [reg, mem, length] :
             {std::tuple{K::Xmm, K::Xm128, 0U}, std::tuple{K::Ymm, K::Ym256, f::l256},
              std::tuple{K::Zmm, K::Zm512, f::l512}}) {
            b.evex(name_v, {reg, mem}, Mp::M0F, prefix, load, -1, L::RM, evex.flags | w | length, T::FullMem);
            b.evex(name_v, {mem, reg}, Mp::M0F, prefix, store, -1, L::MR, f::mask | w | length, T::FullMem);
        }
    }
    // AVX-512 integer moves.
    for (const auto& [name, prefix, w1] :
         {std::tuple{std::string_view{"vmovdqa32"}, P::P66, false}, std::tuple{std::string_view{"vmovdqa64"}, P::P66, true},
          std::tuple{std::string_view{"vmovdqu8"}, P::PF2, false}, std::tuple{std::string_view{"vmovdqu16"}, P::PF2, true},
          std::tuple{std::string_view{"vmovdqu32"}, P::PF3, false}, std::tuple{std::string_view{"vmovdqu64"}, P::PF3, true}}) {
        const auto w = w1 ? f::w : 0U;
        for (const auto& [reg, mem, length] :
             {std::tuple{K::Xmm, K::Xm128, 0U}, std::tuple{K::Ymm, K::Ym256, f::l256},
              std::tuple{K::Zmm, K::Zm512, f::l512}}) {
            b.evex(name, {reg, mem}, Mp::M0F, prefix, 0x6f, -1, L::RM, f::mask | f::zero | w | length, T::FullMem);
            b.evex(name, {mem, reg}, Mp::M0F, prefix, 0x7f, -1, L::MR, f::mask | w | length, T::FullMem);
        }
    }
    // Scalar moves: two operands between registers in legacy form, three
    // in VEX and EVEX.
    for (const auto& [name, prefix, wide] :
         {std::tuple{std::string_view{"movss"}, P::PF3, false}, std::tuple{std::string_view{"movsd"}, P::PF2, true}}) {
        const auto memory = wide ? K::M64 : K::M32;
        const auto w = wide ? f::w | f::element64 : f::element32;
        b.legacy(name, {K::Xmm, K::Xmm}, Mp::M0F, prefix, 0x10, -1, L::RM);
        b.legacy(name, {K::Xmm, memory}, Mp::M0F, prefix, 0x10, -1, L::RM);
        b.legacy(name, {memory, K::Xmm}, Mp::M0F, prefix, 0x11, -1, L::MR);
        const auto name_v = b.v(name);
        b.vex(name_v, {K::Xmm, K::Xmm, K::Xmm}, Mp::M0F, prefix, 0x10, -1, L::RVM, f::reverse);
        b.vex(name_v, {K::Xmm, K::Xmm, K::Xmm}, Mp::M0F, prefix, 0x11, -1, L::MVR, f::rev_only);
        b.vex(name_v, {K::Xmm, memory}, Mp::M0F, prefix, 0x10, -1, L::RM);
        b.vex(name_v, {memory, K::Xmm}, Mp::M0F, prefix, 0x11, -1, L::MR);
        b.evex(name_v, {K::Xmm, K::Xmm, K::Xmm}, Mp::M0F, prefix, 0x10, -1, L::RVM, f::mask | f::zero | w, T::Tuple1Scalar);
        b.evex(name_v, {K::Xmm, memory}, Mp::M0F, prefix, 0x10, -1, L::RM, f::mask | f::zero | w, T::Tuple1Scalar);
        b.evex(name_v, {memory, K::Xmm}, Mp::M0F, prefix, 0x11, -1, L::MR, f::mask | w, T::Tuple1Scalar);
    }
    // movd and movq between general registers, memory, and xmm.
    b.legacy("movd", {K::Xmm, K::Rm32}, Mp::M0F, P::P66, 0x6e, -1, L::RM);
    b.legacy("movd", {K::Rm32, K::Xmm}, Mp::M0F, P::P66, 0x7e, -1, L::MR);
    b.legacy("movq", {K::Xmm, K::Xm64}, Mp::M0F, P::PF3, 0x7e, -1, L::RM);
    b.legacy("movq", {K::Xm64, K::Xmm}, Mp::M0F, P::P66, 0xd6, -1, L::MR);
    b.legacy("movq", {K::Xmm, K::Rm64}, Mp::M0F, P::P66, 0x6e, -1, L::RM, f::w);
    b.legacy("movq", {K::Rm64, K::Xmm}, Mp::M0F, P::P66, 0x7e, -1, L::MR, f::w);
    b.vex("vmovd", {K::Xmm, K::Rm32}, Mp::M0F, P::P66, 0x6e, -1, L::RM);
    b.vex("vmovd", {K::Rm32, K::Xmm}, Mp::M0F, P::P66, 0x7e, -1, L::MR);
    b.vex("vmovq", {K::Xmm, K::Xm64}, Mp::M0F, P::PF3, 0x7e, -1, L::RM, f::reverse);
    b.vex("vmovq", {K::Xm64, K::Xmm}, Mp::M0F, P::P66, 0xd6, -1, L::MR);
    b.vex("vmovq", {K::Xmm, K::Rm64}, Mp::M0F, P::P66, 0x6e, -1, L::RM, f::w);
    b.vex("vmovq", {K::Rm64, K::Xmm}, Mp::M0F, P::P66, 0x7e, -1, L::MR, f::w);
    b.evex("vmovd", {K::Xmm, K::Rm32}, Mp::M0F, P::P66, 0x6e, -1, L::RM, f::element32, T::Tuple1Scalar);
    b.evex("vmovd", {K::Rm32, K::Xmm}, Mp::M0F, P::P66, 0x7e, -1, L::MR, f::element32, T::Tuple1Scalar);
    b.evex("vmovq", {K::Xmm, K::Xm64}, Mp::M0F, P::PF3, 0x7e, -1, L::RM, f::w | f::element64, T::Tuple1Scalar);
    b.evex("vmovq", {K::Xm64, K::Xmm}, Mp::M0F, P::P66, 0xd6, -1, L::MR, f::w | f::element64, T::Tuple1Scalar);
    b.evex("vmovq", {K::Xmm, K::Rm64}, Mp::M0F, P::P66, 0x6e, -1, L::RM, f::w | f::element64, T::Tuple1Scalar);
    b.evex("vmovq", {K::Rm64, K::Xmm}, Mp::M0F, P::P66, 0x7e, -1, L::MR, f::w | f::element64, T::Tuple1Scalar);
    // Half-register moves.
    for (const auto& [name, prefix, load, store] :
         {std::tuple{std::string_view{"movhps"}, P::None, std::uint8_t{0x16}, std::uint8_t{0x17}},
          std::tuple{std::string_view{"movlps"}, P::None, std::uint8_t{0x12}, std::uint8_t{0x13}},
          std::tuple{std::string_view{"movhpd"}, P::P66, std::uint8_t{0x16}, std::uint8_t{0x17}},
          std::tuple{std::string_view{"movlpd"}, P::P66, std::uint8_t{0x12}, std::uint8_t{0x13}}}) {
        b.legacy(name, {K::Xmm, K::M64}, Mp::M0F, prefix, load, -1, L::RM);
        b.legacy(name, {K::M64, K::Xmm}, Mp::M0F, prefix, store, -1, L::MR);
        b.vex(b.v(name), {K::Xmm, K::Xmm, K::M64}, Mp::M0F, prefix, load, -1, L::RVM);
        b.vex(b.v(name), {K::M64, K::Xmm}, Mp::M0F, prefix, store, -1, L::MR);
    }
    b.legacy("movlhps", {K::Xmm, K::Xmm}, Mp::M0F, P::None, 0x16, -1, L::RM);
    b.legacy("movhlps", {K::Xmm, K::Xmm}, Mp::M0F, P::None, 0x12, -1, L::RM);
    b.vex("vmovlhps", {K::Xmm, K::Xmm, K::Xmm}, Mp::M0F, P::None, 0x16, -1, L::RVM);
    b.vex("vmovhlps", {K::Xmm, K::Xmm, K::Xmm}, Mp::M0F, P::None, 0x12, -1, L::RVM);
    b.legacy("movddup", {K::Xmm, K::Xm64}, Mp::M0F, P::PF2, 0x12, -1, L::RM);
    b.vex("vmovddup", {K::Xmm, K::Xm64}, Mp::M0F, P::PF2, 0x12, -1, L::RM);
    b.vex("vmovddup", {K::Ymm, K::Ym256}, Mp::M0F, P::PF2, 0x12, -1, L::RM, f::l256);
    b.packed_unary("movshdup", P::PF3, Mp::M0F, 0x16, Evex{true, false, f::mask | f::zero, T::FullMem});
    b.packed_unary("movsldup", P::PF3, Mp::M0F, 0x12, Evex{true, false, f::mask | f::zero, T::FullMem});
    b.legacy("movntps", {K::M128, K::Xmm}, Mp::M0F, P::None, 0x2b, -1, L::MR);
    b.legacy("movntpd", {K::M128, K::Xmm}, Mp::M0F, P::P66, 0x2b, -1, L::MR);
    b.legacy("movntdq", {K::M128, K::Xmm}, Mp::M0F, P::P66, 0xe7, -1, L::MR);
    b.legacy("movntdqa", {K::Xmm, K::M128}, Mp::M0F38, P::P66, 0x2a, -1, L::RM);
    b.legacy("lddqu", {K::Xmm, K::M128}, Mp::M0F, P::PF2, 0xf0, -1, L::RM);
    for (const auto size : {32U, 64U}) {
        b.add("movnti", {size == 32 ? K::M32 : K::M64, size == 32 ? K::R32 : K::R64}, Space::Legacy, Mp::M0F, P::None,
              0xc3, -1, L::MR, size == 64 ? f::w : 0U);
        b.forms().back().size = static_cast<std::uint8_t>(size);
    }

    // Broadcasts.
    b.vex("vbroadcastss", {K::Xmm, K::Xm32}, Mp::M0F38, P::P66, 0x18, -1, L::RM);
    b.vex("vbroadcastss", {K::Ymm, K::Xm32}, Mp::M0F38, P::P66, 0x18, -1, L::RM, f::l256);
    b.vex("vbroadcastsd", {K::Ymm, K::Xm64}, Mp::M0F38, P::P66, 0x19, -1, L::RM, f::l256);
    b.vex("vbroadcastf128", {K::Ymm, K::M128}, Mp::M0F38, P::P66, 0x1a, -1, L::RM, f::l256);
    b.vex("vbroadcasti128", {K::Ymm, K::M128}, Mp::M0F38, P::P66, 0x5a, -1, L::RM, f::l256);
    for (const auto& [reg, length] : {std::pair{K::Xmm, 0U}, std::pair{K::Ymm, f::l256}, std::pair{K::Zmm, f::l512}}) {
        b.evex("vbroadcastss", {reg, K::Xm32}, Mp::M0F38, P::P66, 0x18, -1, L::RM,
               f::mask | f::zero | length | f::element32, T::Tuple1Scalar);
        if (reg != K::Xmm) {
            b.evex("vbroadcastsd", {reg, K::Xm64}, Mp::M0F38, P::P66, 0x19, -1, L::RM,
                   f::mask | f::zero | length | f::w | f::element64, T::Tuple1Scalar);
        }
    }
    struct Broadcast {
        std::string_view name;
        std::uint8_t opcode;
        std::uint8_t gpr_opcode;
        K memory;
        std::uint32_t w;
        std::uint32_t element;
    };
    for (const auto& [name, opcode, gpr_opcode, memory, w, element] :
         {Broadcast{"vpbroadcastb", 0x78, 0x7a, K::Xm8, 0, f::element8},
          Broadcast{"vpbroadcastw", 0x79, 0x7b, K::Xm16, 0, f::element16},
          Broadcast{"vpbroadcastd", 0x58, 0x7c, K::Xm32, 0, f::element32},
          Broadcast{"vpbroadcastq", 0x59, 0x7c, K::Xm64, f::w, f::element64}}) {
        b.vex(name, {K::Xmm, memory}, Mp::M0F38, P::P66, opcode, -1, L::RM);
        b.vex(name, {K::Ymm, memory}, Mp::M0F38, P::P66, opcode, -1, L::RM, f::l256);
        const auto gpr = w != 0 ? K::R64 : K::R32;
        for (const auto& [reg, length] : {std::pair{K::Xmm, 0U}, std::pair{K::Ymm, f::l256}, std::pair{K::Zmm, f::l512}}) {
            b.evex(name, {reg, memory}, Mp::M0F38, P::P66, opcode, -1, L::RM,
                   f::mask | f::zero | length | w | element, T::Tuple1Scalar);
            b.evex(name, {reg, gpr}, Mp::M0F38, P::P66, gpr_opcode, -1, L::RM, f::mask | f::zero | length | w, T::None);
        }
    }

    // 128- and 256-bit lane extraction and insertion.
    struct Lane {
        std::string_view name;
        std::uint8_t opcode;
        bool insert;
        K small;
        K large;
        std::uint32_t length;
        Space space;
        std::uint32_t w;
        T tuple;
    };
    for (const auto& [name, opcode, insert, small, large, length, space, w, tuple] :
         {Lane{"vextractf128", 0x19, false, K::Xm128, K::Ymm, f::l256, Space::Vex, 0, T::None},
          Lane{"vextracti128", 0x39, false, K::Xm128, K::Ymm, f::l256, Space::Vex, 0, T::None},
          Lane{"vinsertf128", 0x18, true, K::Xm128, K::Ymm, f::l256, Space::Vex, 0, T::None},
          Lane{"vinserti128", 0x38, true, K::Xm128, K::Ymm, f::l256, Space::Vex, 0, T::None},
          Lane{"vextractf32x4", 0x19, false, K::Xm128, K::Ymm, f::l256, Space::Evex, 0, T::Tuple4},
          Lane{"vextractf32x4", 0x19, false, K::Xm128, K::Zmm, f::l512, Space::Evex, 0, T::Tuple4},
          Lane{"vextractf64x2", 0x19, false, K::Xm128, K::Ymm, f::l256, Space::Evex, f::w, T::Tuple2},
          Lane{"vextractf64x2", 0x19, false, K::Xm128, K::Zmm, f::l512, Space::Evex, f::w, T::Tuple2},
          Lane{"vextractf32x8", 0x1b, false, K::Ym256, K::Zmm, f::l512, Space::Evex, 0, T::Tuple8},
          Lane{"vextractf64x4", 0x1b, false, K::Ym256, K::Zmm, f::l512, Space::Evex, f::w, T::Tuple4},
          Lane{"vextracti32x4", 0x39, false, K::Xm128, K::Ymm, f::l256, Space::Evex, 0, T::Tuple4},
          Lane{"vextracti32x4", 0x39, false, K::Xm128, K::Zmm, f::l512, Space::Evex, 0, T::Tuple4},
          Lane{"vextracti64x2", 0x39, false, K::Xm128, K::Ymm, f::l256, Space::Evex, f::w, T::Tuple2},
          Lane{"vextracti64x2", 0x39, false, K::Xm128, K::Zmm, f::l512, Space::Evex, f::w, T::Tuple2},
          Lane{"vextracti32x8", 0x3b, false, K::Ym256, K::Zmm, f::l512, Space::Evex, 0, T::Tuple8},
          Lane{"vextracti64x4", 0x3b, false, K::Ym256, K::Zmm, f::l512, Space::Evex, f::w, T::Tuple4},
          Lane{"vinsertf32x4", 0x18, true, K::Xm128, K::Ymm, f::l256, Space::Evex, 0, T::Tuple4},
          Lane{"vinsertf32x4", 0x18, true, K::Xm128, K::Zmm, f::l512, Space::Evex, 0, T::Tuple4},
          Lane{"vinsertf64x4", 0x1a, true, K::Ym256, K::Zmm, f::l512, Space::Evex, f::w, T::Tuple4},
          Lane{"vinserti32x4", 0x38, true, K::Xm128, K::Ymm, f::l256, Space::Evex, 0, T::Tuple4},
          Lane{"vinserti32x4", 0x38, true, K::Xm128, K::Zmm, f::l512, Space::Evex, 0, T::Tuple4},
          Lane{"vinserti64x4", 0x3a, true, K::Ym256, K::Zmm, f::l512, Space::Evex, f::w, T::Tuple4}}) {
        const auto masking = space == Space::Evex ? f::mask | (insert ? f::zero : 0U) : 0U;
        if (insert) {
            b.add(name, {large, large, small, K::I8}, space, Mp::M0F3A, P::P66, opcode, -1, L::RVM,
                  length | w | masking, tuple);
        } else {
            b.add(name, {small, large, K::I8}, space, Mp::M0F3A, P::P66, opcode, -1, L::MR,
                  length | w | masking | (space == Space::Evex ? f::zero : 0U), tuple);
        }
    }
    b.vex("vzeroupper", {}, Mp::M0F, P::None, 0x77, -1, L::None);
    b.vex("vzeroall", {}, Mp::M0F, P::None, 0x77, -1, L::None, f::l256);
}

void fused_multiply_add(Builder& b) {
    struct Family {
        std::string_view name;
        std::uint8_t packed;  // 132 opcode of the packed form
        std::uint8_t scalar;  // 132 opcode of the scalar form, 0 when none
    };
    for (const auto& [name, packed, scalar] :
         {Family{"fmadd", 0x98, 0x99}, Family{"fmsub", 0x9a, 0x9b}, Family{"fnmadd", 0x9c, 0x9d},
          Family{"fnmsub", 0x9e, 0x9f}, Family{"fmaddsub", 0x96, 0}, Family{"fmsubadd", 0x97, 0}}) {
        for (const auto& [order, offset] :
             {std::pair{std::string_view{"132"}, 0}, std::pair{std::string_view{"213"}, 0x10},
              std::pair{std::string_view{"231"}, 0x20}}) {
            const auto stem = std::string(name) + std::string(order);
            const auto packed_opcode = static_cast<std::uint8_t>(packed + offset);
            b.packed(b.name(stem + "ps"), P::P66, Mp::M0F38, packed_opcode, evex32_round, false);
            b.packed(b.name(stem + "pd"), P::P66, Mp::M0F38, packed_opcode, evex64_round, false, true, 0, true);
            if (scalar == 0) continue;
            const auto scalar_opcode = static_cast<std::uint8_t>(scalar + offset);
            for (const auto wide : {false, true}) {
                const auto mnemonic = b.name("v" + stem + (wide ? "sd" : "ss"));
                const auto memory = wide ? K::Xm64 : K::Xm32;
                const auto w = wide ? f::w : 0U;
                b.vex(mnemonic, {K::Xmm, K::Xmm, memory}, Mp::M0F38, P::P66, scalar_opcode, -1, L::RVM, w);
                b.evex(mnemonic, {K::Xmm, K::Xmm, memory}, Mp::M0F38, P::P66, scalar_opcode, -1, L::RVM,
                       w | f::mask | f::zero | f::rounding | (wide ? f::element64 : f::element32), T::Tuple1Scalar);
            }
        }
    }
}

void avx512(Builder& b) {
    // Integer logic with element sizes.
    for (const auto& [name, opcode] :
         {std::pair{std::string_view{"pand"}, std::uint8_t{0xdb}}, std::pair{std::string_view{"pandn"}, std::uint8_t{0xdf}},
          std::pair{std::string_view{"por"}, std::uint8_t{0xeb}}, std::pair{std::string_view{"pxor"}, std::uint8_t{0xef}}}) {
        b.packed(b.name(std::string(name) + "d"), P::P66, Mp::M0F, opcode, evex32, false, false);
        b.packed(b.name(std::string(name) + "q"), P::P66, Mp::M0F, opcode, evex64, false, false);
    }
    b.packed_imm("pternlogd", P::P66, Mp::M0F3A, 0x25, evex32, false, false);
    b.packed_imm("pternlogq", P::P66, Mp::M0F3A, 0x25, evex64, false, false);
    b.packed("pmullq", P::P66, Mp::M0F38, 0x40, evex64, false, false);
    for (const auto& [name, opcode, w1] :
         {std::tuple{std::string_view{"pminsq"}, std::uint8_t{0x39}, true}, std::tuple{std::string_view{"pminuq"}, std::uint8_t{0x3b}, true},
          std::tuple{std::string_view{"pmaxsq"}, std::uint8_t{0x3d}, true}, std::tuple{std::string_view{"pmaxuq"}, std::uint8_t{0x3f}, true}}) {
        b.packed(name, P::P66, Mp::M0F38, opcode, w1 ? evex64 : evex32, false, false);
    }
    b.packed_unary("pconflictd", P::P66, Mp::M0F38, 0xc4, evex32, false, false);
    b.packed_unary("pconflictq", P::P66, Mp::M0F38, 0xc4, evex64, false, false);
    b.packed_unary("plzcntd", P::P66, Mp::M0F38, 0x44, evex32, false, false);
    b.packed_unary("plzcntq", P::P66, Mp::M0F38, 0x44, evex64, false, false);
    b.packed_unary("popcntb", P::P66, Mp::M0F38, 0x54, Evex{true, false, f::mask | f::zero, T::FullMem}, false, false);
    b.packed_unary("popcntw", P::P66, Mp::M0F38, 0x54, Evex{true, true, f::mask | f::zero, T::FullMem}, false, false);
    b.packed_unary("popcntd", P::P66, Mp::M0F38, 0x55, evex32, false, false);
    b.packed_unary("popcntq", P::P66, Mp::M0F38, 0x55, evex64, false, false);
    b.packed("pshldvw", P::P66, Mp::M0F38, 0x70, Evex{true, true, f::mask | f::zero, T::FullMem}, false, false);
    b.packed("pshldvd", P::P66, Mp::M0F38, 0x71, evex32, false, false);
    b.packed("pshldvq", P::P66, Mp::M0F38, 0x71, evex64, false, false);
    b.packed("pshrdvw", P::P66, Mp::M0F38, 0x72, Evex{true, true, f::mask | f::zero, T::FullMem}, false, false);
    b.packed("pshrdvd", P::P66, Mp::M0F38, 0x73, evex32, false, false);
    b.packed("pshrdvq", P::P66, Mp::M0F38, 0x73, evex64, false, false);
    b.packed_imm("pshldd", P::P66, Mp::M0F3A, 0x71, evex32, false, false);
    b.packed_imm("pshldq", P::P66, Mp::M0F3A, 0x71, evex64, false, false);
    b.packed_imm("pshrdd", P::P66, Mp::M0F3A, 0x73, evex32, false, false);
    b.packed_imm("pshrdq", P::P66, Mp::M0F3A, 0x73, evex64, false, false);
    b.packed("pdpbusd", P::P66, Mp::M0F38, 0x50, evex32, false, false);
    b.packed("pdpbusds", P::P66, Mp::M0F38, 0x51, evex32, false, false);
    b.packed("pdpwssd", P::P66, Mp::M0F38, 0x52, evex32, false, false);
    b.packed("pdpwssds", P::P66, Mp::M0F38, 0x53, evex32, false, false);

    // Comparisons into mask registers.
    struct Compare {
        std::string_view name;
        Mp map;
        std::uint8_t opcode;
        Evex evex;
        bool immediate;
    };
    const Evex mask_bw{true, false, f::mask, T::FullMem};
    const Evex mask_bw_w{true, true, f::mask, T::FullMem};
    const Evex mask32{true, false, f::mask | f::broadcast32, T::Full};
    const Evex mask64{true, true, f::mask | f::broadcast64, T::Full};
    for (const auto& [name, map, opcode, evex, immediate] :
         {Compare{"vpcmpeqb", Mp::M0F, 0x74, mask_bw, false}, Compare{"vpcmpeqw", Mp::M0F, 0x75, mask_bw, false},
          Compare{"vpcmpeqd", Mp::M0F, 0x76, mask32, false}, Compare{"vpcmpeqq", Mp::M0F38, 0x29, mask64, false},
          Compare{"vpcmpgtb", Mp::M0F, 0x64, mask_bw, false}, Compare{"vpcmpgtw", Mp::M0F, 0x65, mask_bw, false},
          Compare{"vpcmpgtd", Mp::M0F, 0x66, mask32, false}, Compare{"vpcmpgtq", Mp::M0F38, 0x37, mask64, false},
          Compare{"vptestmb", Mp::M0F38, 0x26, mask_bw, false}, Compare{"vptestmw", Mp::M0F38, 0x26, mask_bw_w, false},
          Compare{"vptestmd", Mp::M0F38, 0x27, mask32, false}, Compare{"vptestmq", Mp::M0F38, 0x27, mask64, false},
          Compare{"vpcmpb", Mp::M0F3A, 0x3f, mask_bw, true}, Compare{"vpcmpub", Mp::M0F3A, 0x3e, mask_bw, true},
          Compare{"vpcmpw", Mp::M0F3A, 0x3f, mask_bw_w, true}, Compare{"vpcmpuw", Mp::M0F3A, 0x3e, mask_bw_w, true},
          Compare{"vpcmpd", Mp::M0F3A, 0x1f, mask32, true}, Compare{"vpcmpud", Mp::M0F3A, 0x1e, mask32, true},
          Compare{"vpcmpq", Mp::M0F3A, 0x1f, mask64, true}, Compare{"vpcmpuq", Mp::M0F3A, 0x1e, mask64, true}}) {
        const auto w = evex.w1 ? f::w : 0U;
        for (const auto& [reg, mem, length] :
             {std::tuple{K::Xmm, K::Xm128, 0U}, std::tuple{K::Ymm, K::Ym256, f::l256},
              std::tuple{K::Zmm, K::Zm512, f::l512}}) {
            if (immediate) {
                b.evex(name, {K::K, reg, mem, K::I8}, map, P::P66, opcode, -1, L::RVM, evex.flags | w | length, evex.tuple);
            } else {
                b.evex(name, {K::K, reg, mem}, map, P::P66, opcode, -1, L::RVM, evex.flags | w | length, evex.tuple);
            }
        }
    }
    for (const auto& [name, w1] : {std::pair{std::string_view{"vptestnmd"}, false}, std::pair{std::string_view{"vptestnmq"}, true}}) {
        const auto w = w1 ? f::w | f::broadcast64 : f::broadcast32;
        for (const auto& [reg, mem, length] :
             {std::tuple{K::Xmm, K::Xm128, 0U}, std::tuple{K::Ymm, K::Ym256, f::l256},
              std::tuple{K::Zmm, K::Zm512, f::l512}}) {
            b.evex(name, {K::K, reg, mem}, Mp::M0F38, P::PF3, 0x27, -1, L::RVM, f::mask | w | length, T::Full);
        }
    }

    // Mask register instructions. The size letter selects prefix and W.
    struct MaskSize {
        char letter;
        P prefix;
        std::uint32_t w;
    };
    constexpr MaskSize sizes[] = {{'b', P::P66, 0}, {'w', P::None, 0}, {'d', P::P66, f::w}, {'q', P::None, f::w}};
    for (const auto& [letter, prefix, w] : sizes) {
        const auto suffix = std::string(1, letter);
        for (const auto& [name, opcode] :
             {std::pair{std::string_view{"kand"}, std::uint8_t{0x41}}, std::pair{std::string_view{"kandn"}, std::uint8_t{0x42}},
              std::pair{std::string_view{"kor"}, std::uint8_t{0x45}}, std::pair{std::string_view{"kxnor"}, std::uint8_t{0x46}},
              std::pair{std::string_view{"kxor"}, std::uint8_t{0x47}}, std::pair{std::string_view{"kadd"}, std::uint8_t{0x4a}}}) {
            b.vex(b.name(std::string(name) + suffix), {K::K, K::K, K::K}, Mp::M0F, prefix, opcode, -1, L::RVM,
                  w | f::l256);
        }
        b.vex(b.name("knot" + suffix), {K::K, K::K}, Mp::M0F, prefix, 0x44, -1, L::RM, w);
        b.vex(b.name("kortest" + suffix), {K::K, K::K}, Mp::M0F, prefix, 0x98, -1, L::RM, w);
        b.vex(b.name("ktest" + suffix), {K::K, K::K}, Mp::M0F, prefix, 0x99, -1, L::RM, w);
        const auto memory = letter == 'b' ? K::Km8 : letter == 'w' ? K::Km16 : letter == 'd' ? K::Km32 : K::Km64;
        const auto store = letter == 'b' ? K::M8 : letter == 'w' ? K::M16 : letter == 'd' ? K::M32 : K::M64;
        const auto kmov = b.name("kmov" + suffix);
        b.vex(kmov, {K::K, memory}, Mp::M0F, prefix, 0x90, -1, L::RM, w);
        b.vex(kmov, {store, K::K}, Mp::M0F, prefix, 0x91, -1, L::MR, w);
        // To and from general registers: kmovd and kmovq use F2.
        const auto gpr_prefix = letter == 'd' || letter == 'q' ? P::PF2 : prefix;
        const auto gpr_w = letter == 'q' ? f::w : 0U;
        const auto gpr = letter == 'q' ? K::R64 : K::R32;
        b.vex(kmov, {K::K, gpr}, Mp::M0F, gpr_prefix, 0x92, -1, L::RM, gpr_w);
        b.vex(kmov, {gpr, K::K}, Mp::M0F, gpr_prefix, 0x93, -1, L::RM, gpr_w);
    }
    b.vex("kunpckbw", {K::K, K::K, K::K}, Mp::M0F, P::P66, 0x4b, -1, L::RVM, f::l256);
    b.vex("kunpckwd", {K::K, K::K, K::K}, Mp::M0F, P::None, 0x4b, -1, L::RVM, f::l256);
    b.vex("kunpckdq", {K::K, K::K, K::K}, Mp::M0F, P::None, 0x4b, -1, L::RVM, f::l256 | f::w);
    for (const auto& [name, opcode, w] :
         {std::tuple{std::string_view{"kshiftlb"}, std::uint8_t{0x32}, 0U}, std::tuple{std::string_view{"kshiftlw"}, std::uint8_t{0x32}, f::w},
          std::tuple{std::string_view{"kshiftld"}, std::uint8_t{0x33}, 0U}, std::tuple{std::string_view{"kshiftlq"}, std::uint8_t{0x33}, f::w},
          std::tuple{std::string_view{"kshiftrb"}, std::uint8_t{0x30}, 0U}, std::tuple{std::string_view{"kshiftrw"}, std::uint8_t{0x30}, f::w},
          std::tuple{std::string_view{"kshiftrd"}, std::uint8_t{0x31}, 0U}, std::tuple{std::string_view{"kshiftrq"}, std::uint8_t{0x31}, f::w}}) {
        b.vex(name, {K::K, K::K, K::I8}, Mp::M0F3A, P::P66, opcode, -1, L::RM, w);
    }
}

void bit_manipulation(Builder& b) {
    // BMI1, BMI2: VEX forms on general registers.
    for (const auto size : {32U, 64U}) {
        const auto r = size == 32 ? K::R32 : K::R64;
        const auto rm = size == 32 ? K::Rm32 : K::Rm64;
        const auto w = size == 64 ? f::w : 0U;
        const auto s = static_cast<std::uint8_t>(size);
        const auto add = [&](std::string_view name, std::initializer_list<K> operands, P prefix, Mp map,
                             std::uint8_t opcode, std::int16_t digit, L layout) {
            b.vex(name, operands, map, prefix, opcode, digit, layout, w);
            b.forms().back().size = s;
        };
        add("andn", {r, r, rm}, P::None, Mp::M0F38, 0xf2, -1, L::RVM);
        add("bextr", {r, rm, r}, P::None, Mp::M0F38, 0xf7, -1, L::RMV);
        add("blsi", {r, rm}, P::None, Mp::M0F38, 0xf3, 3, L::VM);
        add("blsmsk", {r, rm}, P::None, Mp::M0F38, 0xf3, 2, L::VM);
        add("blsr", {r, rm}, P::None, Mp::M0F38, 0xf3, 1, L::VM);
        add("bzhi", {r, rm, r}, P::None, Mp::M0F38, 0xf5, -1, L::RMV);
        add("pdep", {r, r, rm}, P::PF2, Mp::M0F38, 0xf5, -1, L::RVM);
        add("pext", {r, r, rm}, P::PF3, Mp::M0F38, 0xf5, -1, L::RVM);
        add("mulx", {r, r, rm}, P::PF2, Mp::M0F38, 0xf6, -1, L::RVM);
        add("rorx", {r, rm, K::I8}, P::PF2, Mp::M0F3A, 0xf0, -1, L::RM);
        add("sarx", {r, rm, r}, P::PF3, Mp::M0F38, 0xf7, -1, L::RMV);
        add("shlx", {r, rm, r}, P::P66, Mp::M0F38, 0xf7, -1, L::RMV);
        add("shrx", {r, rm, r}, P::PF2, Mp::M0F38, 0xf7, -1, L::RMV);
    }
}

} // namespace

void vector_forms(std::vector<Form>& forms, std::deque<std::string>& names) {
    Builder b(forms, names);
    float_arithmetic(b);
    float_conversions(b);
    integer_simd(b);
    moves(b);
    fused_multiply_add(b);
    avx512(b);
    bit_manipulation(b);
}

} // namespace cross::x86_64::encoding
