// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <span>
#include <string_view>

namespace cross::x86_64::encoding {

// Operand kinds of an instruction form, in Intel order (destination first).
enum class Kind : std::uint8_t {
    None,
    // General registers by size; Rm kinds also accept memory.
    R8, R16, R32, R64, Rm8, Rm16, Rm32, Rm64,
    // Memory of any size (lea, prefetch) or of one size.
    Mem, M8, M16, M32, M64, M80, M128, M256, M512,
    // Fixed registers.
    Al, Ax, Eax, Rax, Cl, Dx, St0, Xmm0,
    // Immediates: any 8-bit value, a constant that fits a sign-extended
    // byte at the form's operand size, 16 and 32 bits, a 32-bit field
    // sign-extended to 64 bits (constants must fit), 64 bits, and the
    // constant 1.
    I8, S8, I16, I32, S32, I64, One,
    // Branch targets.
    Rel8, Rel32,
    // SIMD registers, and registers or memory of a size.
    Xmm, Ymm, Zmm, Xm8, Xm16, Xm32, Xm64, Xm128, Ym256, Zm512,
    // AVX-512 mask registers, alone or with memory.
    K, Km8, Km16, Km32, Km64,
    // x87 stack registers, MMX registers, segment registers.
    St, Mm, Mmm64, Seg,
};

enum class Space : std::uint8_t { Legacy, Vex, Evex };
enum class Map : std::uint8_t { One, M0F, M0F38, M0F3A };
enum class Prefix : std::uint8_t { None, P66, PF3, PF2 };

// Where operands go, by Intel operand index.
enum class Layout : std::uint8_t {
    None,  // no ModRM; immediates only
    O,     // register 0 in the opcode's low bits
    M,     // ModRM.rm = 0, ModRM.reg = digit
    RM,    // ModRM.reg = 0, ModRM.rm = 1
    MR,    // ModRM.rm = 0, ModRM.reg = 1
    RVM,   // reg = 0, vvvv = 1, rm = 2
    RMV,   // reg = 0, rm = 1, vvvv = 2
    VM,    // vvvv = 0, rm = 1, reg = digit
    MVR,   // rm = 0, vvvv = 1, reg = 2
    RVMR,  // reg = 0, vvvv = 1, rm = 2, register 3 in imm8[7:4]
    D,     // operand 0 is a relative branch target
    // A register in ModRM.rm alone with a fixed ModRM.reg (`/digit`) and
    // mod = 3, such as 0F 01 F9 (rdtscp): encoded through `digit` and the
    // opcode's ModRM byte.
};

// EVEX compressed-displacement tuple types.
enum class Tuple : std::uint8_t {
    None, Full, Half, FullMem, Tuple1Scalar, Tuple1Fixed, Tuple2, Tuple4, Tuple8,
    HalfMem, QuarterMem, EighthMem, Mem128, Dup,
};

namespace flag {
inline constexpr std::uint32_t w = 1U << 0;              // REX.W, VEX.W1, EVEX.W1
inline constexpr std::uint32_t operand16 = 1U << 1;      // legacy 0x66 operand-size prefix
inline constexpr std::uint32_t l256 = 1U << 2;           // VEX.L / EVEX.L'L = 256 bits
inline constexpr std::uint32_t l512 = 1U << 3;           // EVEX.L'L = 512 bits
inline constexpr std::uint32_t mask = 1U << 4;           // EVEX merge masking allowed
inline constexpr std::uint32_t zero = 1U << 5;           // EVEX zero masking allowed
inline constexpr std::uint32_t broadcast32 = 1U << 6;    // {1toN} of 32-bit elements
inline constexpr std::uint32_t broadcast64 = 1U << 7;    // {1toN} of 64-bit elements
inline constexpr std::uint32_t rounding = 1U << 8;       // {rn-sae} etc.
inline constexpr std::uint32_t sae = 1U << 9;            // {sae}
inline constexpr std::uint32_t commute = 1U << 10;       // VEX: commute vvvv and rm for a 2-byte VEX
inline constexpr std::uint32_t reverse = 1U << 11;       // VEX: use the store form for a 2-byte VEX
inline constexpr std::uint32_t fixed_modrm = 1U << 12;   // `digit` is the whole ModRM byte
inline constexpr std::uint32_t element16 = 1U << 13;     // EVEX scalar element of 16 bits
inline constexpr std::uint32_t element8 = 1U << 14;      // EVEX scalar element of 8 bits
inline constexpr std::uint32_t no_rex_w_default = 1U << 15;
inline constexpr std::uint32_t element32 = 1U << 16;     // EVEX scalar or tuple element of 32 bits
inline constexpr std::uint32_t element64 = 1U << 17;     // EVEX scalar or tuple element of 64 bits
inline constexpr std::uint32_t lock_ok = 1U << 18;       // the form accepts a lock prefix
inline constexpr std::uint32_t rev_only = 1U << 19;      // only the 2-byte VEX choice selects it
inline constexpr std::uint32_t relaxable = 1U << 20;     // a short branch with a long form
inline constexpr std::uint32_t vsib = 1U << 21;          // memory operand uses a vector index
} // namespace flag

struct Form {
    std::string_view mnemonic;
    std::array<Kind, 4> operands{};
    // The operand size an AT&T suffix (b, w, l, q) names; 0 when the form
    // takes no suffix.
    std::uint8_t size{};
    Space space{Space::Legacy};
    Map map{Map::One};
    Prefix prefix{Prefix::None};
    std::uint8_t opcode{};
    // ModRM.reg extension (0-7), the whole ModRM byte with fixed_modrm, or
    // -1 when no extension applies.
    std::int16_t digit{-1};
    Layout layout{Layout::None};
    std::uint32_t flags{};
    Tuple tuple{Tuple::None};
    [[nodiscard]] unsigned operand_count() const {
        unsigned count = 0;
        while (count < operands.size() && operands[count] != Kind::None) ++count;
        return count;
    }
};

// Every form of the mnemonic, in matching priority order.
[[nodiscard]] std::span<const Form> forms(std::string_view mnemonic);

// Condition-code suffixes and their numbers (o = 0 ... g = 15).
[[nodiscard]] int condition_code(std::string_view suffix);

} // namespace cross::x86_64::encoding
