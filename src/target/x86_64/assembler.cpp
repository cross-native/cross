// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "target/x86_64/assembler.hpp"

#include "assembler/assembler.hpp"
#include "target/subtarget.hpp"
#include "target/target.hpp"
#include "target/x86_64/encodings.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <string>
#include <string_view>

namespace cross::x86_64 {
namespace {

using assembler::Assembler;
using assembler::Cursor;
using assembler::ExpressionId;
using assembler::FixupKind;
using assembler::Format;
using assembler::Statement;
namespace elf = assembler::elf;
namespace coff = assembler::coff;
namespace enc = encoding;

std::string lower(std::string_view text) {
    std::string result(text);
    for (auto& c : result) {
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    }
    return result;
}

// Relocation specifiers written after '@'.
enum Specifier : std::uint8_t {
    spec_none,
    spec_plt,
    spec_gotpcrel,
    spec_gotpcrel_norelax,
    spec_got,
    spec_gotoff,
    spec_gotpc,
    spec_pltoff,
    spec_tpoff,
    spec_gottpoff,
    spec_dtpoff,
    spec_tlsgd,
    spec_tlsld,
    spec_tlsdesc,
    spec_tlscall,
    spec_size,
    spec_secrel32,
    spec_imgrel,
    spec_tlvp,
    spec_abs8,
};

struct SpecifierName {
    std::string_view name;
    Specifier value;
};

constexpr SpecifierName specifier_names[] = {
    {"plt", spec_plt},         {"gotpcrel", spec_gotpcrel}, {"gotpcrel_norelax", spec_gotpcrel_norelax},
    {"got", spec_got},         {"gotoff", spec_gotoff},     {"gotpc", spec_gotpc},
    {"pltoff", spec_pltoff},   {"tpoff", spec_tpoff},       {"gottpoff", spec_gottpoff},
    {"dtpoff", spec_dtpoff},   {"tlsgd", spec_tlsgd},       {"tlsld", spec_tlsld},
    {"tlsdesc", spec_tlsdesc}, {"tlscall", spec_tlscall},   {"size", spec_size},
    {"secrel32", spec_secrel32}, {"imgrel", spec_imgrel},   {"tlvp", spec_tlvp},
    {"abs8", spec_abs8},
};

// Target fixup kinds.
enum : std::uint16_t {
    fixup_pcrel1 = FixupKind::target_base,
    fixup_pcrel4,
    // A rel32 to a plain symbol: ELF PLT32.
    fixup_branch4,
    fixup_riprel4,
    // RIP-relative operands of instructions a linker may rewrite when
    // a GOT load resolves locally (GOTPCRELX, REX_GOTPCRELX, Mach-O
    // GOT_LOAD).
    fixup_riprel4_relax,
    fixup_riprel4_relax_rex,
    fixup_riprel4_movq_load,
    // A 32-bit field sign-extended to 64 bits (displacements, 64-bit
    // operations' immediates).
    fixup_signed4,
};

bool riprel(std::uint16_t kind) {
    return kind == fixup_riprel4 || kind == fixup_riprel4_relax || kind == fixup_riprel4_relax_rex ||
           kind == fixup_riprel4_movq_load;
}

// Registers.
enum class RegClass : std::uint8_t {
    None, Gpr8, Gpr8High, Gpr16, Gpr32, Gpr64, Rip, Xmm, Ymm, Zmm, Mask, Segment, St, Mmx, Control, Debug,
};

struct Register {
    RegClass cls{RegClass::None};
    std::uint8_t number{};
    [[nodiscard]] bool valid() const { return cls != RegClass::None; }
};

std::optional<Register> parse_register_name(std::string_view text) {
    const auto name = lower(text);
    static constexpr std::array<std::string_view, 16> gpr64{"rax", "rcx", "rdx", "rbx", "rsp", "rbp", "rsi", "rdi",
                                                            "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15"};
    static constexpr std::array<std::string_view, 8> gpr32{"eax", "ecx", "edx", "ebx", "esp", "ebp", "esi", "edi"};
    static constexpr std::array<std::string_view, 8> gpr16{"ax", "cx", "dx", "bx", "sp", "bp", "si", "di"};
    static constexpr std::array<std::string_view, 8> gpr8{"al", "cl", "dl", "bl", "spl", "bpl", "sil", "dil"};
    static constexpr std::array<std::string_view, 4> high8{"ah", "ch", "dh", "bh"};
    static constexpr std::array<std::string_view, 6> segments{"es", "cs", "ss", "ds", "fs", "gs"};
    for (std::uint8_t index = 0; index < 16; ++index) {
        if (name == gpr64[index]) return Register{RegClass::Gpr64, index};
    }
    for (std::uint8_t index = 0; index < 8; ++index) {
        if (name == gpr32[index]) return Register{RegClass::Gpr32, index};
        if (name == gpr16[index]) return Register{RegClass::Gpr16, index};
        if (name == gpr8[index]) return Register{RegClass::Gpr8, index};
    }
    for (std::uint8_t index = 0; index < 4; ++index) {
        if (name == high8[index]) return Register{RegClass::Gpr8High, static_cast<std::uint8_t>(index + 4)};
    }
    for (std::uint8_t index = 0; index < 6; ++index) {
        if (name == segments[index]) return Register{RegClass::Segment, index};
    }
    if (name == "rip") return Register{RegClass::Rip, 0};
    // r8d, r8w, r8b (and r8l), numbered registers of every class.
    const auto numbered = [&](std::string_view prefix, std::string_view suffix,
                              unsigned limit) -> std::optional<std::uint8_t> {
        if (!name.starts_with(prefix) || !name.ends_with(suffix) ||
            name.size() <= prefix.size() + suffix.size()) {
            return std::nullopt;
        }
        const auto digits = std::string_view(name).substr(prefix.size(), name.size() - prefix.size() - suffix.size());
        unsigned value = 0;
        const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || value >= limit ||
            (digits.size() > 1 && digits[0] == '0')) {
            return std::nullopt;
        }
        return static_cast<std::uint8_t>(value);
    };
    for (const auto& [suffix, cls] : {std::pair{std::string_view{"d"}, RegClass::Gpr32},
                                      std::pair{std::string_view{"w"}, RegClass::Gpr16},
                                      std::pair{std::string_view{"b"}, RegClass::Gpr8},
                                      std::pair{std::string_view{"l"}, RegClass::Gpr8}}) {
        if (const auto number = numbered("r", suffix, 16); number && *number >= 8) return Register{cls, *number};
    }
    if (const auto number = numbered("xmm", "", 32)) return Register{RegClass::Xmm, *number};
    if (const auto number = numbered("ymm", "", 32)) return Register{RegClass::Ymm, *number};
    if (const auto number = numbered("zmm", "", 32)) return Register{RegClass::Zmm, *number};
    if (const auto number = numbered("k", "", 8)) return Register{RegClass::Mask, *number};
    if (const auto number = numbered("mm", "", 8)) return Register{RegClass::Mmx, *number};
    if (const auto number = numbered("cr", "", 16)) return Register{RegClass::Control, *number};
    if (const auto number = numbered("db", "", 16)) return Register{RegClass::Debug, *number};
    if (const auto number = numbered("dr", "", 16)) return Register{RegClass::Debug, *number};
    if (name == "st") return Register{RegClass::St, 0};
    return std::nullopt;
}

unsigned register_size(Register reg) {
    switch (reg.cls) {
    case RegClass::Gpr8:
    case RegClass::Gpr8High: return 8;
    case RegClass::Gpr16: return 16;
    case RegClass::Gpr32: return 32;
    case RegClass::Gpr64: return 64;
    default: return 0;
    }
}

// One parsed AT&T operand.
struct Operand {
    enum class Type : std::uint8_t { Register, Immediate, Memory, Target } type{};
    Register reg;
    // The immediate, displacement, or branch target; none for a memory
    // operand without displacement.
    std::optional<ExpressionId> expression;
    // The expression's value when it involves no label.
    std::optional<std::int64_t> constant;
    Register segment;
    Register base;
    Register index;
    std::uint8_t scale{1};
    bool indirect{};
    // AVX-512 decorations.
    std::uint8_t mask{};
    bool zeroing{};
    std::uint8_t broadcast{};
    SourceLocation location;
    [[nodiscard]] bool memory() const { return type == Type::Memory; }
};

// Rounding control of an AVX-512 instruction: {rn,rd,ru,rz}-sae or {sae}.
constexpr int rounding_sae_only = 4;

struct Instruction {
    std::string mnemonic;
    std::vector<Operand> operands;  // AT&T order
    int rounding{-1};
    // Explicit prefixes.
    bool lock{};
    bool rep{};
    bool repne{};
    bool data16{};
    bool address32{};
    bool rex64{};
    bool notrack{};
    bool force_vex3{};
    bool force_evex{};
    bool force_disp32{};
    SourceLocation location;
};

// One pending SEH frame (`.seh_proc` to `.seh_endproc`).
struct UnwindCode {
    std::uint8_t operation{};
    std::uint8_t reg{};
    std::uint32_t offset{};
    assembler::SymbolId label{};
};

struct UnwindFrame {
    assembler::SymbolId function{};
    assembler::SymbolId begin{};
    std::optional<assembler::SymbolId> prolog_end;
    std::optional<assembler::SymbolId> end;
    assembler::SectionId text{};
    std::vector<UnwindCode> codes;
    std::optional<std::size_t> frame_code;
    std::optional<assembler::SymbolId> handler;
    bool handles_unwind{};
    bool handles_exceptions{};
    std::optional<assembler::SymbolId> info;  // the UNWIND_INFO label
    bool format_emitted_{};
    SourceLocation location;
};

constexpr std::uint8_t uop_push_nonvol = 0;
constexpr std::uint8_t uop_alloc_large = 1;
constexpr std::uint8_t uop_alloc_small = 2;
constexpr std::uint8_t uop_set_fpreg = 3;
constexpr std::uint8_t uop_save_nonvol = 4;
constexpr std::uint8_t uop_save_xmm128 = 8;
constexpr std::uint8_t uop_push_machframe = 10;

struct Model {
    Format format{Format::Elf};
};

class X86Target final : public assembler::Target {
public:
    explicit X86Target(Model model) : model_(model) {}

    [[nodiscard]] assembler::ObjectInfo object() const override {
        assembler::ObjectInfo info;
        info.elf64 = true;
        info.big_endian = false;
        info.format = model_.format;
        switch (model_.format) {
        case Format::Elf:
            info.machine = 62;  // EM_X86_64
            info.rela = true;
            break;
        case Format::Coff:
            info.machine = 0x8664;  // IMAGE_FILE_MACHINE_AMD64
            break;
        case Format::MachO:
            info.cpu_type = 0x01000007;  // CPU_TYPE_X86_64
            info.cpu_subtype = 3;         // CPU_SUBTYPE_X86_64_ALL
            break;
        }
        return info;
    }
    [[nodiscard]] std::uint32_t elf_flags() const override { return 0; }
    [[nodiscard]] assembler::FrameFacts frame_facts() const override {
        assembler::FrameFacts facts;
        facts.code_alignment = 1;
        // COFF .debug_frame keeps the MCAsmInfo default slot size (4); ELF
        // and Mach-O use the 64-bit slot size (8), as LLVM MC does.
        facts.data_alignment = model_.format == Format::Coff ? -4 : -8;
        facts.return_address = 16;
        // DW_CFA_def_cfa %rsp, 8; DW_CFA_offset %rip, cfa-8 (the offset is
        // 8 / |data_alignment|).
        const std::uint8_t factor = model_.format == Format::Coff ? 2 : 1;
        facts.initial_instructions = {0x0c, 0x07, 0x08, 0x90, factor};
        facts.initial_cfa_offset = 8;
        // Mach-O: pcrel absptr (8 bytes); ELF and COFF: pcrel sdata4.
        facts.eh_pointer_encoding = model_.format == Format::MachO ? 0x10 : 0x1b;
        return facts;
    }
    [[nodiscard]] unsigned word_size() const override { return 2; }
    [[nodiscard]] bool power_of_two_align() const override { return model_.format == Format::MachO; }
    [[nodiscard]] std::uint32_t section_type(std::string_view name, std::uint32_t type) const override {
        // SHT_X86_64_UNWIND.
        if (model_.format == Format::Elf && name == ".eh_frame") return 0x70000001;
        return type;
    }
    [[nodiscard]] std::optional<unsigned> dwarf_register(std::string_view name) const override;
    [[nodiscard]] assembler::FixupInfo fixup_info(FixupKind kind) const override {
        switch (kind.value) {
        case fixup_pcrel1: return {1, true};
        case fixup_signed4: return {4, false};
        default: return {4, true};
        }
    }
    [[nodiscard]] std::optional<std::uint8_t> specifier(std::string_view name) const override {
        const auto text = lower(name);
        for (const auto& entry : specifier_names) {
            if (entry.name == text) return entry.value;
        }
        return std::nullopt;
    }
    void code_padding(std::span<std::uint8_t> bytes) const override;

    void instruction(Assembler& as, Statement& statement) override;
    bool directive(Assembler& as, Statement& statement) override;
    void finish(Assembler& as) override;
    void finish_tables(Assembler& as) override;
    bool apply_fixup(Assembler& as, const assembler::Fixup& fixup, std::int64_t value,
                     std::span<std::uint8_t> bytes, bool relocated) const override;
    std::optional<assembler::RelocationChoice> relocation(
        Assembler& as, const assembler::Fixup& fixup, const assembler::Value& value,
        const assembler::Symbol& symbol, const assembler::Section* symbol_section) const override;
    bool macho_relocation(Assembler& as, assembler::SectionId section, const assembler::Fixup& fixup,
                          std::uint64_t place, std::uint64_t address, const assembler::Value& value,
                          std::int64_t& field, std::vector<assembler::Relocation>& relocations) const override;

private:
    struct Encoded {
        std::vector<std::uint8_t> bytes;
        struct Fixup {
            std::uint32_t offset{};
            FixupKind kind{};
            ExpressionId expression{};
        };
        std::vector<Fixup> fixups;
    };

    bool parse_operands(Assembler& as, Cursor& cursor, Instruction& instruction);
    std::optional<Operand> parse_operand(Assembler& as, Cursor& cursor);
    std::optional<Register> parse_register(Assembler& as, Cursor& cursor);
    bool parse_decorations(Assembler& as, Cursor& cursor, Operand& operand);
    void rewrite_aliases(Assembler& as, Instruction& instruction);
    const enc::Form* match(Assembler& as, Instruction& instruction, std::vector<const Operand*>& intel);
    bool operand_matches(const enc::Form& form, enc::Kind kind, const Operand& operand,
                         const Instruction& instruction) const;
    void encode(Assembler& as, Instruction& instruction, const enc::Form& form,
                std::vector<const Operand*> operands);
    bool encode_memory(Assembler& as, Encoded& out, unsigned reg_field, const Operand& memory,
                       FixupKind rip_kind, FixupKind disp_kind, bool force_disp32, bool evex,
                       unsigned scale_bytes, unsigned trailing_bytes);
    void emit(Assembler& as, const Encoded& encoded, SourceLocation location);
    void emit_branch(Assembler& as, const Instruction& instruction, const enc::Form& form,
                     const Operand& target);

    // SEH.
    bool seh_directive(Assembler& as, std::string_view name, Statement& statement);
    UnwindFrame* current_frame(Assembler& as, SourceLocation location);
    void emit_unwind_info(Assembler& as, UnwindFrame& frame);
    assembler::SectionId unwind_section(Assembler& as, bool pdata, assembler::SectionId text);

    Model model_;
    std::vector<UnwindFrame> frames_;
    std::optional<std::size_t> open_frame_;
    // Unique unwind sections per text section, by text section.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> unwind_ids_;
};

std::optional<unsigned> X86Target::dwarf_register(std::string_view text) const {
    auto name = text;
    if (name.starts_with("%")) name.remove_prefix(1);
    const auto reg = parse_register_name(name);
    if (!reg) return std::nullopt;
    static constexpr std::array<unsigned, 8> low{0, 2, 1, 3, 7, 6, 4, 5};
    switch (reg->cls) {
    case RegClass::Gpr64:
    case RegClass::Gpr32:
        return reg->number < 8 ? low[reg->number] : reg->number;
    case RegClass::Rip: return 16;
    case RegClass::Xmm: return reg->number < 16 ? 17U + reg->number : 67U + (reg->number - 16U);
    case RegClass::St: return 33U + reg->number;
    case RegClass::Mmx: return 41U + reg->number;
    case RegClass::Mask: return 118U + reg->number;
    case RegClass::Segment: return 50U + reg->number;
    default: return std::nullopt;
    }
}

void X86Target::code_padding(std::span<std::uint8_t> bytes) const {
    // The no-op sequences LLVM MC uses for x86-64, up to 15 bytes each
    // (longer ones repeat the 0x66 prefix).
    static constexpr std::array<std::array<std::uint8_t, 10>, 10> nops{{
        {0x90},
        {0x66, 0x90},
        {0x0f, 0x1f, 0x00},
        {0x0f, 0x1f, 0x40, 0x00},
        {0x0f, 0x1f, 0x44, 0x00, 0x00},
        {0x66, 0x0f, 0x1f, 0x44, 0x00, 0x00},
        {0x0f, 0x1f, 0x80, 0x00, 0x00, 0x00, 0x00},
        {0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x66, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
        {0x66, 0x2e, 0x0f, 0x1f, 0x84, 0x00, 0x00, 0x00, 0x00, 0x00},
    }};
    std::size_t position = 0;
    while (position < bytes.size()) {
        const auto length = std::min<std::size_t>(bytes.size() - position, 15);
        const auto prefixes = length > 10 ? length - 10 : 0;
        for (std::size_t index = 0; index < prefixes; ++index) bytes[position++] = 0x66;
        const auto rest = length - prefixes;
        std::copy_n(nops[rest - 1].begin(), rest, bytes.begin() + static_cast<std::ptrdiff_t>(position));
        position += rest;
    }
}

std::optional<Register> X86Target::parse_register(Assembler& as, Cursor& cursor) {
    const auto location = cursor.location();
    if (!cursor.consume("%")) return std::nullopt;
    const auto name = cursor.identifier();
    if (!name) {
        as.error(location, "expected a register name after '%'");
        return std::nullopt;
    }
    auto reg = parse_register_name(*name);
    if (!reg) {
        as.error(location, "unknown register '%" + std::string(*name) + "'");
        return std::nullopt;
    }
    if (reg->cls == RegClass::St && cursor.rest().starts_with("(")) {
        cursor.consume("(");
        const auto index = as.parse_absolute(cursor);
        if (!index || !cursor.consume(")")) {
            as.error(location, "expected '%st(N)'");
            return std::nullopt;
        }
        if (*index < 0 || *index > 7) {
            as.error(location, "x87 stack register index out of range");
            return std::nullopt;
        }
        reg->number = static_cast<std::uint8_t>(*index);
    }
    return reg;
}

bool X86Target::parse_decorations(Assembler& as, Cursor& cursor, Operand& operand) {
    while (cursor.peek() == '{') {
        const auto location = cursor.location();
        cursor.consume("{");
        cursor.skip_space();
        if (cursor.peek() == '%') {
            const auto reg = parse_register(as, cursor);
            if (!reg) return false;
            if (reg->cls != RegClass::Mask || reg->number == 0) {
                as.error(location, "expected a mask register %k1-%k7");
                return false;
            }
            operand.mask = reg->number;
        } else {
            const auto rest = cursor.rest();
            const auto close = rest.find('}');
            const auto word = lower(rest.substr(0, close));
            if (word == "z") {
                operand.zeroing = true;
            } else if (word.starts_with("1to")) {
                unsigned count = 0;
                const auto digits = std::string_view(word).substr(3);
                const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), count);
                if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() ||
                    !std::has_single_bit(count) || count < 2 || count > 32) {
                    as.error(location, "invalid broadcast '{" + word + "}'");
                    return false;
                }
                operand.broadcast = static_cast<std::uint8_t>(count);
            } else {
                as.error(location, "unknown operand decoration '{" + word + "}'");
                return false;
            }
            cursor.set_position(cursor.position() + (close == std::string_view::npos ? rest.size() : close));
        }
        if (!cursor.consume("}")) {
            as.error(cursor.location(), "expected '}'");
            return false;
        }
    }
    return true;
}

std::optional<Operand> X86Target::parse_operand(Assembler& as, Cursor& cursor) {
    Operand operand;
    cursor.skip_space();
    operand.location = cursor.location();
    if (cursor.consume("*")) operand.indirect = true;
    cursor.skip_space();
    std::optional<Register> segment;
    if (cursor.peek() == '%') {
        const auto reg = parse_register(as, cursor);
        if (!reg) return std::nullopt;
        if (cursor.peek() == ':') {
            if (reg->cls != RegClass::Segment) {
                as.error(operand.location, "expected a segment register before ':'");
                return std::nullopt;
            }
            cursor.consume(":");
            segment = reg;
        } else {
            operand.type = Operand::Type::Register;
            operand.reg = *reg;
            if (!parse_decorations(as, cursor, operand)) return std::nullopt;
            return operand;
        }
    } else if (cursor.peek() == '$') {
        cursor.consume("$");
        const auto expression = as.parse_expression(cursor);
        if (!expression) return std::nullopt;
        operand.type = Operand::Type::Immediate;
        operand.expression = *expression;
        operand.constant = as.constant_now(*expression);
        return operand;
    }
    // A displacement, unless a '(' starts the base and index.
    cursor.skip_space();
    bool base_follows = false;
    if (cursor.peek() == '(') {
        auto probe = cursor;
        probe.consume("(");
        const char next = probe.peek();
        base_follows = next == '%' || next == ',';
    }
    if (!base_follows) {
        const auto expression = as.parse_expression(cursor);
        if (!expression) return std::nullopt;
        operand.expression = *expression;
        operand.constant = as.constant_now(*expression);
    }
    if (cursor.peek() != '(') {
        operand.type = segment ? Operand::Type::Memory : Operand::Type::Target;
        if (segment) operand.segment = *segment;
        if (!parse_decorations(as, cursor, operand)) return std::nullopt;
        return operand;
    }
    cursor.consume("(");
    operand.type = Operand::Type::Memory;
    if (segment) operand.segment = *segment;
    if (cursor.peek() == '%') {
        const auto base = parse_register(as, cursor);
        if (!base) return std::nullopt;
        operand.base = *base;
    }
    if (cursor.consume(",")) {
        if (cursor.peek() == '%') {
            const auto index = parse_register(as, cursor);
            if (!index) return std::nullopt;
            operand.index = *index;
        }
        if (cursor.consume(",")) {
            const auto location = cursor.location();
            const auto scale = as.parse_absolute(cursor);
            if (!scale) return std::nullopt;
            if (*scale != 1 && *scale != 2 && *scale != 4 && *scale != 8) {
                as.error(location, "scale factor must be 1, 2, 4, or 8");
                return std::nullopt;
            }
            operand.scale = static_cast<std::uint8_t>(*scale);
        }
    }
    if (!cursor.consume(")")) {
        as.error(cursor.location(), "expected ')' after the memory operand");
        return std::nullopt;
    }
    const auto valid_base = [](Register reg) {
        return reg.cls == RegClass::Gpr64 || reg.cls == RegClass::Gpr32 || reg.cls == RegClass::Rip;
    };
    if (operand.base.valid() && !valid_base(operand.base)) {
        as.error(operand.location, "invalid base register");
        return std::nullopt;
    }
    if (operand.index.valid()) {
        const bool vector = operand.index.cls == RegClass::Xmm || operand.index.cls == RegClass::Ymm ||
                            operand.index.cls == RegClass::Zmm;
        if (!vector && ((operand.index.cls != RegClass::Gpr64 && operand.index.cls != RegClass::Gpr32) ||
                        operand.index.number == 4)) {
            as.error(operand.location, "invalid index register");
            return std::nullopt;
        }
        if (operand.base.cls == RegClass::Rip) {
            as.error(operand.location, "%rip cannot be used with an index register");
            return std::nullopt;
        }
    }
    if (operand.base.valid() && operand.index.valid() && !(operand.index.cls == RegClass::Xmm ||
        operand.index.cls == RegClass::Ymm || operand.index.cls == RegClass::Zmm) &&
        operand.base.cls != operand.index.cls) {
        as.error(operand.location, "base and index registers must have the same size");
        return std::nullopt;
    }
    if (!parse_decorations(as, cursor, operand)) return std::nullopt;
    return operand;
}

bool X86Target::parse_operands(Assembler& as, Cursor& cursor, Instruction& instruction) {
    if (cursor.at_end()) return true;
    for (;;) {
        cursor.skip_space();
        if (cursor.peek() == '{') {
            // A rounding operand: {rn-sae}, {rd-sae}, {ru-sae}, {rz-sae}, {sae}.
            const auto location = cursor.location();
            const auto rest = cursor.rest();
            const auto close = rest.find('}');
            const auto word = lower(rest.substr(1, close == std::string_view::npos ? rest.size() : close - 1));
            static constexpr std::array<std::string_view, 5> names{"rn-sae", "rd-sae", "ru-sae", "rz-sae", "sae"};
            const auto found = std::find(names.begin(), names.end(), word);
            if (close == std::string_view::npos || found == names.end()) {
                as.error(location, "expected a rounding operand such as {rn-sae}");
                return false;
            }
            instruction.rounding = static_cast<int>(found - names.begin());
            cursor.set_position(cursor.position() + close + 1);
        } else {
            auto operand = parse_operand(as, cursor);
            if (!operand) return false;
            instruction.operands.push_back(*operand);
        }
        if (cursor.consume(",")) continue;
        if (!cursor.at_end()) {
            as.error(cursor.location(), "unexpected text after operand");
            return false;
        }
        return true;
    }
}

namespace {

struct PredicateName {
    std::string_view name;
    std::uint8_t value;
};

// SSE and AVX comparison predicates of the cmpXXps pseudo mnemonics.
constexpr PredicateName compare_predicates[] = {
    {"eq", 0},        {"lt", 1},        {"le", 2},        {"unord", 3},     {"neq", 4},
    {"nlt", 5},       {"nle", 6},       {"ord", 7},       {"eq_oq", 0},     {"lt_os", 1},
    {"le_os", 2},     {"unord_q", 3},   {"neq_uq", 4},    {"nlt_us", 5},    {"nle_us", 6},
    {"ord_q", 7},     {"eq_uq", 8},     {"nge", 9},       {"nge_us", 9},    {"ngt", 10},
    {"ngt_us", 10},   {"false", 11},    {"false_oq", 11}, {"neq_oq", 12},   {"ge", 13},
    {"ge_os", 13},    {"gt", 14},       {"gt_os", 14},    {"true", 15},     {"true_uq", 15},
    {"eq_os", 16},    {"lt_oq", 17},    {"le_oq", 18},    {"unord_s", 19},  {"neq_us", 20},
    {"nlt_uq", 21},   {"nle_uq", 22},   {"ord_s", 23},    {"eq_us", 24},    {"nge_uq", 25},
    {"ngt_uq", 26},   {"false_os", 27}, {"neq_os", 28},   {"ge_oq", 29},    {"gt_oq", 30},
    {"true_us", 31},
};

// AVX-512 integer comparison predicates of the vpcmpXX pseudo mnemonics.
constexpr PredicateName integer_predicates[] = {
    {"eq", 0}, {"lt", 1}, {"le", 2}, {"false", 3}, {"neq", 4}, {"nlt", 5}, {"nle", 6}, {"true", 7},
};

} // namespace

void X86Target::rewrite_aliases(Assembler& as, Instruction& instruction) {
    auto& mnemonic = instruction.mnemonic;
    const auto prepend_immediate = [&](std::int64_t value) {
        Operand immediate;
        immediate.type = Operand::Type::Immediate;
        immediate.expression = as.constant(value, instruction.location);
        immediate.constant = value;
        immediate.location = instruction.location;
        instruction.operands.insert(instruction.operands.begin(), immediate);
    };
    if (!enc::forms(mnemonic).empty()) {
        // `imul $imm, %reg` multiplies the register in place.
        if (mnemonic.starts_with("imul") && instruction.operands.size() == 2 &&
            instruction.operands[0].type == Operand::Type::Immediate &&
            instruction.operands[1].type == Operand::Type::Register) {
            instruction.operands.push_back(instruction.operands[1]);
        }
        return;
    }
    // Suffixed imul: imull $3, %ecx.
    if (mnemonic.size() == 5 && mnemonic.starts_with("imul") && instruction.operands.size() == 2 &&
        instruction.operands[0].type == Operand::Type::Immediate &&
        instruction.operands[1].type == Operand::Type::Register) {
        instruction.operands.push_back(instruction.operands[1]);
        return;
    }
    // cmp<predicate>{ps,pd,ss,sd} and the v-prefixed forms.
    for (const bool vex : {false, true}) {
        const std::string_view prefix = vex ? "vcmp" : "cmp";
        if (!mnemonic.starts_with(prefix) || mnemonic.size() < prefix.size() + 3) continue;
        const auto type = mnemonic.substr(mnemonic.size() - 2);
        if (type != "ps" && type != "pd" && type != "ss" && type != "sd") continue;
        const auto predicate = mnemonic.substr(prefix.size(), mnemonic.size() - prefix.size() - 2);
        for (const auto& [name, value] : compare_predicates) {
            if (name != predicate || (!vex && value > 7)) continue;
            mnemonic = std::string(prefix) + type;
            prepend_immediate(value);
            return;
        }
    }
    // vpcmp<predicate>[u]{b,w,d,q}.
    if (mnemonic.starts_with("vpcmp") && mnemonic.size() > 6) {
        auto rest = std::string_view(mnemonic).substr(5);
        const char element = rest.back();
        if (element == 'b' || element == 'w' || element == 'd' || element == 'q') {
            rest.remove_suffix(1);
            bool is_unsigned = false;
            if (rest.ends_with('u')) {
                is_unsigned = true;
                rest.remove_suffix(1);
            }
            for (const auto& [name, value] : integer_predicates) {
                if (name != rest) continue;
                mnemonic = std::string("vpcmp") + (is_unsigned ? "u" : "") + element;
                prepend_immediate(value);
                return;
            }
        }
    }
}

bool X86Target::operand_matches(const enc::Form& form, enc::Kind kind, const Operand& operand,
                                const Instruction& instruction) const {
    using K = enc::Kind;
    const auto is_reg = [&](RegClass cls) {
        return operand.type == Operand::Type::Register && operand.reg.cls == cls;
    };
    const bool memory = operand.type == Operand::Type::Memory ||
                        (operand.type == Operand::Type::Target &&
                         (operand.indirect || form.layout != enc::Layout::D));
    const bool plain = operand.mask == 0 && !operand.zeroing && operand.broadcast == 0;
    const auto gpr8 = [&]() { return is_reg(RegClass::Gpr8) || is_reg(RegClass::Gpr8High); };
    const auto immediate = [&](std::int64_t low, std::int64_t high) {
        if (operand.type != Operand::Type::Immediate) return false;
        if (!operand.constant) return true;
        return *operand.constant >= low && *operand.constant <= high;
    };
    switch (kind) {
    case K::None: return false;
    case K::R8: return gpr8();
    case K::R16: return is_reg(RegClass::Gpr16);
    case K::R32: return is_reg(RegClass::Gpr32);
    case K::R64: return is_reg(RegClass::Gpr64);
    case K::Rm8: return gpr8() || (memory && plain);
    case K::Rm16: return is_reg(RegClass::Gpr16) || (memory && plain);
    case K::Rm32: return is_reg(RegClass::Gpr32) || (memory && plain);
    case K::Rm64: return is_reg(RegClass::Gpr64) || (memory && plain);
    case K::Mem:
    case K::M8:
    case K::M16:
    case K::M32:
    case K::M64:
    case K::M80:
    case K::M128:
    case K::M256:
    case K::M512: return memory && plain;
    case K::Al: return is_reg(RegClass::Gpr8) && operand.reg.number == 0;
    case K::Ax: return is_reg(RegClass::Gpr16) && operand.reg.number == 0;
    case K::Eax: return is_reg(RegClass::Gpr32) && operand.reg.number == 0;
    case K::Rax: return is_reg(RegClass::Gpr64) && operand.reg.number == 0;
    case K::Cl: return is_reg(RegClass::Gpr8) && operand.reg.number == 1;
    case K::Dx: return is_reg(RegClass::Gpr16) && operand.reg.number == 2;
    case K::St0: return is_reg(RegClass::St) && operand.reg.number == 0;
    case K::St: return is_reg(RegClass::St);
    case K::Xmm0: return is_reg(RegClass::Xmm) && operand.reg.number == 0;
    case K::I8: return immediate(-128, 255);
    case K::S8: {
        if (operand.type != Operand::Type::Immediate || !operand.constant) return false;
        const auto value = *operand.constant;
        if (value >= -128 && value <= 127) return true;
        // A value written as the operation's unsigned width.
        if (form.size == 16) return value >= 0xff80 && value <= 0xffff;
        if (form.size == 32) return value >= 0xffffff80LL && value <= 0xffffffffLL;
        return false;
    }
    case K::I16: return immediate(-32768, 65535);
    case K::I32: return immediate(INT32_MIN, 0xffffffffLL);
    case K::S32: return immediate(INT32_MIN, INT32_MAX);
    case K::I64: return operand.type == Operand::Type::Immediate;
    case K::One: return operand.type == Operand::Type::Immediate && operand.constant && *operand.constant == 1;
    case K::Rel8:
    case K::Rel32: return operand.type == Operand::Type::Target && !operand.indirect && plain;
    case K::Xmm: return is_reg(RegClass::Xmm);
    case K::Ymm: return is_reg(RegClass::Ymm);
    case K::Zmm: return is_reg(RegClass::Zmm);
    case K::Xm8:
    case K::Xm16:
    case K::Xm32:
    case K::Xm64:
    case K::Xm128: return is_reg(RegClass::Xmm) || memory;
    case K::Ym256: return is_reg(RegClass::Ymm) || memory;
    case K::Zm512: return is_reg(RegClass::Zmm) || memory;
    case K::K: return is_reg(RegClass::Mask);
    case K::Km8:
    case K::Km16:
    case K::Km32:
    case K::Km64: return is_reg(RegClass::Mask) || (memory && plain);
    case K::Mm: return is_reg(RegClass::Mmx);
    case K::Mmm64: return is_reg(RegClass::Mmx) || (memory && plain);
    case K::Seg: return is_reg(RegClass::Segment);
    }
    (void)instruction;
    return false;
}

const enc::Form* X86Target::match(Assembler& as, Instruction& instruction,
                                  std::vector<const Operand*>& intel) {
    const auto& mnemonic = instruction.mnemonic;
    struct Candidate {
        std::span<const enc::Form> forms;
        unsigned suffix;
    };
    std::vector<Candidate> candidates;
    candidates.push_back({enc::forms(mnemonic), 0});
    if (mnemonic.size() > 1) {
        const char last = mnemonic.back();
        const unsigned size = last == 'b' ? 8 : last == 'w' ? 16 : last == 'l' ? 32 : last == 'q' ? 64 : 0;
        if (size != 0) candidates.push_back({enc::forms(mnemonic.substr(0, mnemonic.size() - 1)), size});
    }
    if (candidates[0].forms.empty() && (candidates.size() < 2 || candidates[1].forms.empty())) {
        as.error(instruction.location, "unknown instruction '" + instruction.mnemonic + "'");
        return nullptr;
    }
    intel.clear();
    for (auto operand = instruction.operands.rbegin(); operand != instruction.operands.rend(); ++operand) {
        intel.push_back(&*operand);
    }
    const auto count = static_cast<unsigned>(intel.size());
    const bool decorated = instruction.rounding >= 0 ||
                           std::any_of(intel.begin(), intel.end(), [](const Operand* operand) {
                               return operand->mask != 0 || operand->zeroing || operand->broadcast != 0;
                           });
    const auto high_register = [](const Operand* operand) {
        const auto& reg = operand->type == Operand::Type::Register ? operand->reg : operand->index;
        return (reg.cls == RegClass::Xmm || reg.cls == RegClass::Ymm || reg.cls == RegClass::Zmm) &&
               reg.number >= 16;
    };
    const bool uses_high = std::any_of(intel.begin(), intel.end(), high_register);
    const enc::Form* first = nullptr;
    unsigned first_size = 0;
    bool size_pinned = false;
    bool ambiguous = false;
    for (const auto& [forms, suffix] : candidates) {
        for (const auto& form : forms) {
            if (form.operand_count() != count || (form.flags & enc::flag::rev_only) != 0) continue;
            if (suffix != 0 && form.size != suffix) continue;
            if (suffix == 0 && (form.flags & enc::flag::no_rex_w_default) != 0) continue;
            if (form.space != enc::Space::Evex && (decorated || uses_high || instruction.force_evex)) continue;
            if (form.space == enc::Space::Evex) {
                if (instruction.rounding >= 0) {
                    const auto allowed = instruction.rounding == rounding_sae_only
                                             ? (form.flags & (enc::flag::sae | enc::flag::rounding)) != 0
                                             : (form.flags & enc::flag::rounding) != 0;
                    if (!allowed) continue;
                    if (std::any_of(intel.begin(), intel.end(), [](const Operand* operand) {
                            return operand->type != Operand::Type::Register &&
                                   operand->type != Operand::Type::Immediate;
                        })) {
                        continue;
                    }
                }
                bool fits = true;
                for (unsigned index = 0; index < count && fits; ++index) {
                    const auto* operand = intel[index];
                    if (operand->mask != 0 && (index != 0 || (form.flags & enc::flag::mask) == 0)) fits = false;
                    if (operand->zeroing && (index != 0 || (form.flags & enc::flag::zero) == 0)) fits = false;
                    if (operand->broadcast != 0) {
                        const unsigned element =
                            (form.flags & enc::flag::broadcast32) != 0   ? 32
                            : (form.flags & enc::flag::broadcast64) != 0 ? 64
                                                                         : 0;
                        const unsigned length = (form.flags & enc::flag::l512) != 0   ? 512
                                                : (form.flags & enc::flag::l256) != 0 ? 256
                                                                                      : 128;
                        if (element == 0 || operand->type == Operand::Type::Register ||
                            operand->broadcast != length / element) {
                            fits = false;
                        }
                    }
                }
                if (!fits) continue;
            }
            bool ok = true;
            for (unsigned index = 0; index < count && ok; ++index) {
                ok = operand_matches(form, form.operands[index], *intel[index], instruction);
            }
            if (!ok) continue;
            // The accumulator exchange cannot name %eax: 0x90 is nop.
            if (form.opcode == 0x90 && form.layout == enc::Layout::O && form.size == 32) {
                const bool accumulator_twice = std::all_of(intel.begin(), intel.end(), [](const Operand* operand) {
                    return operand->reg.number == 0;
                });
                if (accumulator_twice) continue;
            }
            const bool pinned =
                suffix != 0 || form.size == 0 ||
                std::any_of(intel.begin(), intel.end(), [](const Operand* operand) {
                    return operand->type == Operand::Type::Register && register_size(operand->reg) != 0;
                });
            if (!first) {
                first = &form;
                first_size = form.size;
                size_pinned = pinned;
            } else if (!size_pinned && form.size != first_size && form.size != 0) {
                ambiguous = true;
            }
        }
    }
    if (!first) {
        as.error(instruction.location, "invalid operands for instruction '" + instruction.mnemonic + "'");
        return nullptr;
    }
    if (ambiguous) {
        as.error(instruction.location, "ambiguous operand size for '" + instruction.mnemonic +
                                           "'; add a b, w, l, or q suffix");
        return nullptr;
    }
    return first;
}

namespace {

std::uint8_t modrm(unsigned mod, unsigned reg, unsigned rm) {
    return static_cast<std::uint8_t>((mod << 6U) | ((reg & 7U) << 3U) | (rm & 7U));
}

// The byte count of a compressed-displacement scale for an EVEX tuple.
unsigned compressed_scale(const enc::Form& form, const Operand& memory, unsigned length_bytes) {
    using T = enc::Tuple;
    const unsigned element = (form.flags & enc::flag::element64) != 0 || (form.flags & enc::flag::broadcast64) != 0
                                 ? 8
                             : (form.flags & enc::flag::element16) != 0 ? 2
                             : (form.flags & enc::flag::element8) != 0  ? 1
                                                                        : 4;
    switch (form.tuple) {
    case T::None: return 1;
    case T::Full: return memory.broadcast != 0 ? element : length_bytes;
    case T::Half: return memory.broadcast != 0 ? element : length_bytes / 2;
    case T::FullMem: return length_bytes;
    case T::Tuple1Scalar: return element;
    case T::Tuple1Fixed: return (form.flags & enc::flag::element64) != 0 ? 8 : 4;
    case T::Tuple2: return (form.flags & enc::flag::w) != 0 ? 16 : 8;
    case T::Tuple4: return (form.flags & enc::flag::w) != 0 ? 32 : 16;
    case T::Tuple8: return 32;
    case T::HalfMem: return length_bytes / 2;
    case T::QuarterMem: return length_bytes / 4;
    case T::EighthMem: return length_bytes / 8;
    case T::Mem128: return 16;
    case T::Dup: return length_bytes;
    }
    return 1;
}

} // namespace

bool X86Target::encode_memory(Assembler& as, Encoded& out, unsigned reg_field, const Operand& memory,
                              FixupKind rip_kind, FixupKind disp_kind, bool force_disp32,
                              bool evex, unsigned scale_bytes, unsigned trailing_bytes) {
    const bool rip = memory.base.cls == RegClass::Rip;
    const bool has_base = memory.base.valid() && !rip;
    const bool has_index = memory.index.valid();
    const bool symbolic = memory.expression.has_value() && !memory.constant.has_value();
    const std::int64_t displacement = memory.constant.value_or(0);
    const auto emit_disp32 = [&](FixupKind kind, std::int64_t bias) {
        if (symbolic) {
            auto expression = *memory.expression;
            if (bias != 0) {
                expression = as.binary(assembler::Operator::Add, expression,
                                       as.constant(bias, memory.location));
            }
            out.fixups.push_back({static_cast<std::uint32_t>(out.bytes.size()), kind, expression});
        }
        for (int byte = 0; byte < 4; ++byte) {
            out.bytes.push_back(static_cast<std::uint8_t>(displacement >> (8 * byte)));
        }
    };
    if (rip) {
        out.bytes.push_back(modrm(0, reg_field, 5));
        // A RIP-relative field is measured from the end of the instruction,
        // so an immediate that follows biases the stored displacement.
        emit_disp32(rip_kind, -static_cast<std::int64_t>(trailing_bytes));
        return true;
    }
    if (!has_base && !has_index) {
        out.bytes.push_back(modrm(0, reg_field, 4));
        out.bytes.push_back(modrm(0, 4, 5));
        emit_disp32(disp_kind, 0);
        return true;
    }
    const unsigned base_num = has_base ? memory.base.number : 5;
    const bool needs_sib = has_index || (base_num & 7U) == 4 || !has_base;
    unsigned mode = 0;
    unsigned disp_bytes = 0;
    bool compressed = false;
    const bool is_ebp = (base_num & 7U) == 5;
    if (symbolic) {
        mode = 2;
        disp_bytes = 4;
    } else if (!has_base) {
        mode = 0;
        disp_bytes = 4;
    } else if (displacement == 0 && !is_ebp && !force_disp32) {
        mode = 0;
    } else if (!force_disp32 && evex && scale_bytes > 1 &&
               displacement % static_cast<std::int64_t>(scale_bytes) == 0 &&
               displacement / static_cast<std::int64_t>(scale_bytes) >= -128 &&
               displacement / static_cast<std::int64_t>(scale_bytes) <= 127) {
        mode = 1;
        disp_bytes = 1;
        compressed = true;
    } else if (!force_disp32 && !evex && displacement >= -128 && displacement <= 127) {
        mode = 1;
        disp_bytes = 1;
    } else {
        mode = 2;
        disp_bytes = 4;
    }
    if (needs_sib) {
        out.bytes.push_back(modrm(mode, reg_field, 4));
        const unsigned scale_log = memory.scale == 8 ? 3 : memory.scale == 4 ? 2 : memory.scale == 2 ? 1 : 0;
        const unsigned index_num = has_index ? memory.index.number : 4;
        out.bytes.push_back(modrm(scale_log, index_num & 7U, base_num & 7U));
    } else {
        out.bytes.push_back(modrm(mode, reg_field, base_num & 7U));
    }
    if (disp_bytes == 1) {
        const auto value = compressed ? displacement / static_cast<std::int64_t>(scale_bytes) : displacement;
        out.bytes.push_back(static_cast<std::uint8_t>(value));
    } else if (disp_bytes == 4) {
        emit_disp32(disp_kind, 0);
    }
    return true;
}

namespace {

unsigned reg_number(Register reg) { return reg.number; }

unsigned immediate_bytes(enc::Kind kind, unsigned size) {
    switch (kind) {
    case enc::Kind::I8:
    case enc::Kind::S8:
    case enc::Kind::One: return 1;
    case enc::Kind::I16: return 2;
    case enc::Kind::I32:
    case enc::Kind::S32: return 4;
    case enc::Kind::I64: return size == 16 ? 2 : size == 32 ? 4 : 8;
    default: return 0;
    }
}

// The roles operands play, derived from the form's layout.
struct Roles {
    const Operand* reg{};   // ModRM.reg register (null when a digit)
    const Operand* rm{};    // ModRM.rm (register or memory)
    const Operand* vvvv{};  // VEX/EVEX vvvv register
    const Operand* opcode_reg{};  // Layout::O
    const Operand* imm_reg{};     // Layout::RVMR
    std::vector<const Operand*> immediates;
};

Roles assign_roles(const enc::Form& form, const std::vector<const Operand*>& intel) {
    using L = enc::Layout;
    Roles roles;
    const auto at = [&](unsigned index) { return index < intel.size() ? intel[index] : nullptr; };
    switch (form.layout) {
    case L::None: break;
    // `xchg %acc, %reg` names the register that is not the accumulator.
    case L::O:
        roles.opcode_reg =
            intel.size() == 2 && (form.operands[0] == enc::Kind::Ax ||
                                  form.operands[0] == enc::Kind::Eax ||
                                  form.operands[0] == enc::Kind::Rax)
                ? at(1)
                : at(0);
        break;
    case L::M: roles.rm = at(0); break;
    case L::RM: roles.reg = at(0); roles.rm = at(1); break;
    case L::MR: roles.rm = at(0); roles.reg = at(1); break;
    case L::RVM: roles.reg = at(0); roles.vvvv = at(1); roles.rm = at(2); break;
    case L::RMV: roles.reg = at(0); roles.rm = at(1); roles.vvvv = at(2); break;
    case L::VM: roles.vvvv = at(0); roles.rm = at(1); break;
    case L::MVR: roles.rm = at(0); roles.vvvv = at(1); roles.reg = at(2); break;
    case L::RVMR: roles.reg = at(0); roles.vvvv = at(1); roles.rm = at(2); roles.imm_reg = at(3); break;
    case L::D: break;
    }
    for (unsigned index = 0; index < intel.size(); ++index) {
        // The implicit shift count $1 is encoded in the opcode, not emitted.
        if (intel[index]->type == Operand::Type::Immediate && intel[index] != roles.imm_reg &&
            form.operands[index] != enc::Kind::One) {
            roles.immediates.push_back(intel[index]);
        }
    }
    return roles;
}

} // namespace

void X86Target::encode(Assembler& as, Instruction& instruction, const enc::Form& form,
                       std::vector<const Operand*> intel) {
    using L = enc::Layout;
    namespace fl = enc::flag;
    if (form.layout == L::D) {
        emit_branch(as, instruction, form, *intel[0]);
        return;
    }
    Roles roles = assign_roles(form, intel);
    const unsigned size = form.size;
    const bool w = (form.flags & fl::w) != 0;
    const bool operand16 = (form.flags & fl::operand16) != 0;
    const bool evex = form.space == enc::Space::Evex;
    const bool vex = form.space == enc::Space::Vex;

    // Operand encodings.
    const auto number = [&](const Operand* operand) -> unsigned {
        if (!operand) return 0;
        if (operand->type == Operand::Type::Register) return reg_number(operand->reg);
        return 0;
    };
    unsigned reg = roles.reg ? number(roles.reg) : (form.digit >= 0 && (form.flags & fl::fixed_modrm) == 0
                                                        ? static_cast<unsigned>(form.digit)
                                                        : 0);
    const Operand* rm_operand = roles.rm;
    unsigned rm_reg = 0;
    bool rm_is_memory = false;
    const Operand* memory = nullptr;
    if (rm_operand) {
        if (rm_operand->type == Operand::Type::Register) {
            rm_reg = reg_number(rm_operand->reg);
        } else {
            rm_is_memory = true;
            memory = rm_operand;
        }
    }
    if (roles.opcode_reg) rm_reg = number(roles.opcode_reg);
    unsigned vvvv = roles.vvvv ? number(roles.vvvv) : 0;
    unsigned index_reg = (memory && memory->index.valid()) ? memory->index.number : 0;
    unsigned base_reg = (memory && memory->base.valid() && memory->base.cls != RegClass::Rip)
                            ? memory->base.number
                            : 0;

    // VEX two-operand register moves: when the rm register is extended but
    // the reg is not, re-encode with the reverse (store) opcode so a
    // two-byte VEX prefix fits, as LLVM MC does.
    std::uint8_t opcode = form.opcode;
    if (vex && (form.flags & fl::reverse) != 0 && !rm_is_memory && roles.reg && roles.rm &&
        rm_reg >= 8 && reg < 8 && !instruction.force_vex3 && form.layout == L::RM) {
        std::swap(reg, rm_reg);
        opcode = form.opcode == 0x6f ? 0x7f : static_cast<std::uint8_t>(form.opcode + 1);
    }
    // A commutable VEX op on the 0F map whose rm register is extended but
    // vvvv is not swaps the two, so a two-byte VEX prefix fits.
    if (vex && (form.flags & fl::commute) != 0 && form.map == enc::Map::M0F && !w &&
        form.layout == L::RVM && !rm_is_memory && roles.vvvv && rm_reg >= 8 && vvvv < 8 &&
        !instruction.force_vex3) {
        std::swap(rm_reg, vvvv);
    }

    // REX bits (also VEX/EVEX R/X/B).
    const unsigned rex_r = reg >= 8;
    const unsigned rex_x = index_reg >= 8;
    const unsigned rex_b = (rm_is_memory ? base_reg : rm_reg) >= 8;

    Encoded out;
    // Prefixes from the statement.
    if (instruction.lock) out.bytes.push_back(0xf0);
    if (instruction.rep) out.bytes.push_back(0xf3);
    if (instruction.repne) out.bytes.push_back(0xf2);
    if (instruction.notrack) out.bytes.push_back(0x3e);
    if (memory && memory->segment.valid()) {
        static constexpr std::array<std::uint8_t, 6> overrides{0x26, 0x2e, 0x36, 0x3e, 0x64, 0x65};
        out.bytes.push_back(overrides[memory->segment.number]);
    }
    if (memory && (memory->base.cls == RegClass::Gpr32 || memory->index.cls == RegClass::Gpr32)) {
        out.bytes.push_back(0x67);
    }

    const auto map_prefix = [&](enc::Prefix prefix) -> std::optional<std::uint8_t> {
        switch (prefix) {
        case enc::Prefix::P66: return 0x66;
        case enc::Prefix::PF3: return 0xf3;
        case enc::Prefix::PF2: return 0xf2;
        default: return std::nullopt;
        }
    };
    const auto map_pp = [&](enc::Prefix prefix) -> unsigned {
        switch (prefix) {
        case enc::Prefix::P66: return 1;
        case enc::Prefix::PF3: return 2;
        case enc::Prefix::PF2: return 3;
        default: return 0;
        }
    };
    const auto map_mm = [&](enc::Map map) -> unsigned {
        switch (map) {
        case enc::Map::One: return 0;
        case enc::Map::M0F: return 1;
        case enc::Map::M0F38: return 2;
        case enc::Map::M0F3A: return 3;
        }
        return 0;
    };

    if (!vex && !evex) {
        if (operand16 || instruction.data16) out.bytes.push_back(0x66);
        if (const auto prefix = map_prefix(form.prefix)) out.bytes.push_back(*prefix);
        // REX.
        const bool needs_rex_8bit = [&]() {
            for (const auto* operand : intel) {
                if (operand->type == Operand::Type::Register && operand->reg.cls == RegClass::Gpr8 &&
                    operand->reg.number >= 4) {
                    return true;
                }
            }
            return false;
        }();
        const bool has_high8 = std::any_of(intel.begin(), intel.end(), [](const Operand* operand) {
            return operand->type == Operand::Type::Register && operand->reg.cls == RegClass::Gpr8High;
        });
        const unsigned rex = (w ? 8U : 0U) | (rex_r << 2U) | (rex_x << 1U) | rex_b;
        if (rex != 0 || needs_rex_8bit || instruction.rex64) {
            if (has_high8) {
                as.error(instruction.location, "cannot use a high 8-bit register with a REX prefix");
                return;
            }
            out.bytes.push_back(static_cast<std::uint8_t>(0x40 | rex));
        }
        // Opcode map escape.
        if (form.map == enc::Map::M0F || form.map == enc::Map::M0F38 || form.map == enc::Map::M0F3A) {
            out.bytes.push_back(0x0f);
        }
        if (form.map == enc::Map::M0F38) out.bytes.push_back(0x38);
        if (form.map == enc::Map::M0F3A) out.bytes.push_back(0x3a);
        out.bytes.push_back(static_cast<std::uint8_t>(opcode + (roles.opcode_reg ? (rm_reg & 7U) : 0U)));
        if (form.flags & fl::fixed_modrm) {
            // An x87 stack register (Kind::St) adds its index to the ModRM byte.
            unsigned st_extra = 0;
            for (unsigned index = 0; index < intel.size(); ++index) {
                if (form.operands[index] == enc::Kind::St) st_extra = intel[index]->reg.number;
            }
            out.bytes.push_back(static_cast<std::uint8_t>(static_cast<unsigned>(form.digit) + st_extra));
        }
    } else {
        const unsigned pp = map_pp(form.prefix);
        const unsigned mm = map_mm(form.map);
        const unsigned length = (form.flags & fl::l512) != 0 ? 2U : (form.flags & fl::l256) != 0 ? 1U : 0U;
        if (vex) {
            const bool two_byte = !evex && mm == 1 && !w && rex_x == 0 && rex_b == 0 &&
                                  !instruction.force_vex3 && (form.flags & fl::rev_only) == 0;
            if (two_byte) {
                out.bytes.push_back(0xc5);
                out.bytes.push_back(static_cast<std::uint8_t>(((rex_r ? 0U : 1U) << 7U) |
                                                              ((~vvvv & 0xfU) << 3U) | (length << 2U) | pp));
            } else {
                out.bytes.push_back(0xc4);
                out.bytes.push_back(static_cast<std::uint8_t>(((rex_r ? 0U : 1U) << 7U) |
                                                              ((rex_x ? 0U : 1U) << 6U) |
                                                              ((rex_b ? 0U : 1U) << 5U) | mm));
                out.bytes.push_back(static_cast<std::uint8_t>(((w ? 1U : 0U) << 7U) | ((~vvvv & 0xfU) << 3U) |
                                                              (length << 2U) | pp));
            }
        } else {
            // EVEX. The register fields split across P1 (bits 3 and 4) and,
            // for vvvv, P2 and P3 (bit 4).
            const bool broadcast = memory && memory->broadcast != 0;
            const unsigned ll = instruction.rounding >= 0 && instruction.rounding != rounding_sae_only
                                    ? static_cast<unsigned>(instruction.rounding)
                                    : length;
            const unsigned mask = intel.empty() ? 0 : intel[0]->mask;
            const unsigned zeroing = (!intel.empty() && intel[0]->zeroing) ? 1U : 0U;
            const unsigned b_bit = broadcast || instruction.rounding >= 0 ? 1U : 0U;
            const unsigned reg_b3 = (reg >> 3U) & 1U;
            const unsigned reg_b4 = (reg >> 4U) & 1U;
            const unsigned rm_b3 = rm_is_memory ? (base_reg >> 3U) & 1U : (rm_reg >> 3U) & 1U;
            const unsigned rm_b4 = rm_is_memory ? (index_reg >> 3U) & 1U : (rm_reg >> 4U) & 1U;
            const unsigned v_b4 = (vvvv >> 4U) & 1U;
            out.bytes.push_back(0x62);
            out.bytes.push_back(static_cast<std::uint8_t>(((reg_b3 ? 0U : 1U) << 7U) | ((rm_b4 ? 0U : 1U) << 6U) |
                                                          ((rm_b3 ? 0U : 1U) << 5U) | ((reg_b4 ? 0U : 1U) << 4U) |
                                                          mm));
            out.bytes.push_back(static_cast<std::uint8_t>(((w ? 1U : 0U) << 7U) | ((~vvvv & 0xfU) << 3U) |
                                                          (1U << 2U) | pp));
            out.bytes.push_back(static_cast<std::uint8_t>((zeroing << 7U) | (ll << 5U) | (b_bit << 4U) |
                                                          ((v_b4 ? 0U : 1U) << 3U) | (mask & 7U)));
        }
        if (form.map == enc::Map::One) out.bytes.push_back(0x0f);  // only for XOP-like, unused here
        out.bytes.push_back(opcode);
    }

    // Trailing immediate bytes, for the RIP displacement bias.
    unsigned trailing = 0;
    for (const auto* immediate : roles.immediates) {
        trailing += immediate_bytes(form.operands[static_cast<std::size_t>(
            std::find(intel.begin(), intel.end(), immediate) - intel.begin())], size);
    }
    if (roles.imm_reg) trailing += 1;

    // ModRM / SIB / displacement.
    if (form.layout != L::None && form.layout != L::O) {
        if (rm_is_memory) {
            FixupKind rip_kind{fixup_riprel4};
            if (memory->expression) {
                const auto& expression = as.expression(*memory->expression);
                const auto specifier = expression.specifier;
                if (specifier == spec_gotpcrel) {
                    rip_kind = form.opcode == 0x8b && !vex && !evex && w ? FixupKind{fixup_riprel4_movq_load}
                                                                        : FixupKind{fixup_riprel4_relax_rex};
                }
            }
            const unsigned vector_length = (form.flags & fl::l512) != 0 ? 2U
                                           : (form.flags & fl::l256) != 0 ? 1U : 0U;
            const unsigned scale = evex ? compressed_scale(form, *memory, 16U << vector_length) : 0U;
            encode_memory(as, out, reg, *memory, rip_kind, FixupKind{fixup_signed4},
                          instruction.force_disp32, evex, scale, trailing);
        } else {
            out.bytes.push_back(modrm(3, reg, rm_reg));
        }
    }

    // Immediates.
    for (const auto* immediate : roles.immediates) {
        const auto kind = form.operands[static_cast<std::size_t>(
            std::find(intel.begin(), intel.end(), immediate) - intel.begin())];
        const unsigned bytes = immediate_bytes(kind, size);
        const bool is_signed = kind == enc::Kind::S8 || kind == enc::Kind::S32;
        FixupKind fixup = bytes == 1 ? FixupKind{assembler::fixup::data1}
                          : bytes == 2 ? FixupKind{assembler::fixup::data2}
                          : bytes == 8 ? FixupKind{assembler::fixup::data8}
                          : FixupKind{fixup_signed4};
        if (immediate->constant) {
            for (unsigned byte = 0; byte < bytes; ++byte) {
                out.bytes.push_back(static_cast<std::uint8_t>(*immediate->constant >> (8 * byte)));
            }
        } else if (immediate->expression) {
            out.fixups.push_back({static_cast<std::uint32_t>(out.bytes.size()), fixup, *immediate->expression});
            for (unsigned byte = 0; byte < bytes; ++byte) out.bytes.push_back(0);
        }
        (void)is_signed;
    }
    if (roles.imm_reg) {
        out.bytes.push_back(static_cast<std::uint8_t>((number(roles.imm_reg) & 0xfU) << 4U));
    }
    emit(as, out, instruction.location);
}

void X86Target::emit(Assembler& as, const Encoded& encoded, SourceLocation location) {
    as.begin_instruction();
    // Emit bytes, placing each fixup before the bytes it covers.
    std::size_t next = 0;
    for (const auto& fixup : encoded.fixups) {
        if (fixup.offset > next) {
            as.emit_bytes(std::span(encoded.bytes.data() + next, fixup.offset - next));
            next = fixup.offset;
        }
        as.add_fixup(fixup.kind, fixup.expression, location);
    }
    if (next < encoded.bytes.size()) {
        as.emit_bytes(std::span(encoded.bytes.data() + next, encoded.bytes.size() - next));
    }
}

void X86Target::emit_branch(Assembler& as, const Instruction& instruction, const enc::Form& form,
                            const Operand& target) {
    namespace fl = enc::flag;
    if (!target.expression) {
        as.error(instruction.location, "a branch needs a target");
        return;
    }
    as.begin_instruction();
    const bool relaxable = (form.flags & fl::relaxable) != 0;
    // The short form in `bytes`, with a one-byte PC-relative field.
    std::vector<std::uint8_t> short_bytes;
    std::uint32_t short_fixup = 0;
    std::vector<std::uint8_t> long_bytes;
    std::uint32_t long_fixup = 0;
    if (relaxable) {
        short_bytes.push_back(form.opcode);
        short_fixup = static_cast<std::uint32_t>(short_bytes.size());
        short_bytes.push_back(0);
        // The near form: E9 rel32 for jmp, 0F 8x rel32 for jcc.
        if (form.opcode == 0xeb) {
            long_bytes = {0xe9, 0, 0, 0, 0};
            long_fixup = 1;
        } else {
            long_bytes = {0x0f, static_cast<std::uint8_t>(0x80 + (form.opcode - 0x70)), 0, 0, 0, 0};
            long_fixup = 2;
        }
        as.emit_relaxable(std::move(short_bytes), short_fixup, FixupKind{fixup_pcrel1}, std::move(long_bytes),
                          long_fixup, FixupKind{fixup_branch4}, *target.expression, instruction.location);
        return;
    }
    // call rel32, jmp rel32 (jrcxz/loop are relaxable-less but short only).
    std::vector<std::uint8_t> bytes{form.opcode};
    as.emit_bytes(bytes);
    if (form.operands[0] == enc::Kind::Rel8) {
        as.add_fixup(FixupKind{fixup_pcrel1}, *target.expression, instruction.location);
        const std::array<std::uint8_t, 1> zero{};
        as.emit_bytes(zero);
    } else {
        as.add_fixup(FixupKind{fixup_branch4}, *target.expression, instruction.location);
        const std::array<std::uint8_t, 4> zero{};
        as.emit_bytes(zero);
    }
}

void X86Target::instruction(Assembler& as, Statement& statement) {
    Instruction instruction;
    instruction.location = statement.location;
    auto mnemonic = lower(statement.name);
    auto& cursor = statement.operands;
    // Prefix mnemonics that can stand alone or precede an instruction.
    for (;;) {
        bool prefix = true;
        if (mnemonic == "lock") instruction.lock = true;
        else if (mnemonic == "rep" || mnemonic == "repe" || mnemonic == "repz") instruction.rep = true;
        else if (mnemonic == "repne" || mnemonic == "repnz") instruction.repne = true;
        else if (mnemonic == "data16") instruction.data16 = true;
        else if (mnemonic == "notrack") instruction.notrack = true;
        else if (mnemonic == "rex.w" || mnemonic == "rex64") instruction.rex64 = true;
        else if (mnemonic == "data32" || mnemonic == "addr32") instruction.address32 = true;
        else if (mnemonic == "{vex3}" || mnemonic == "{vex}") instruction.force_vex3 = true;
        else if (mnemonic == "{evex}") instruction.force_evex = true;
        else if (mnemonic == "{disp32}") instruction.force_disp32 = true;
        else prefix = false;
        if (!prefix) break;
        cursor.skip_space();
        if (cursor.at_end()) {
            // A standalone prefix: emit the lone byte (lock/rep/data16).
            Encoded out;
            if (instruction.lock) out.bytes.push_back(0xf0);
            if (instruction.rep) out.bytes.push_back(0xf3);
            if (instruction.repne) out.bytes.push_back(0xf2);
            if (instruction.data16) out.bytes.push_back(0x66);
            if (!out.bytes.empty()) emit(as, out, instruction.location);
            return;
        }
        const auto next = cursor.identifier();
        if (!next) {
            as.error(cursor.location(), "expected an instruction after the prefix");
            return;
        }
        mnemonic = lower(*next);
    }
    // Waiting x87 forms are fwait (0x9b) followed by the no-wait form.
    static const std::pair<std::string_view, std::string_view> wait_forms[] = {
        {"fclex", "fnclex"}, {"finit", "fninit"}, {"fstcw", "fnstcw"},
        {"fstsw", "fnstsw"}, {"fstenv", "fnstenv"}, {"fsave", "fnsave"},
        {"fstenvs", "fnstenv"},
    };
    for (const auto& [waiting, bare] : wait_forms) {
        if (mnemonic == waiting) {
            Encoded prefix;
            prefix.bytes.push_back(0x9b);
            emit(as, prefix, instruction.location);
            mnemonic = std::string(bare);
            break;
        }
    }
    instruction.mnemonic = mnemonic;
    if (!parse_operands(as, cursor, instruction)) return;
    rewrite_aliases(as, instruction);
    std::vector<const Operand*> intel;
    const auto* form = match(as, instruction, intel);
    if (!form) return;
    encode(as, instruction, *form, intel);
}

bool X86Target::apply_fixup(Assembler& as, const assembler::Fixup& fixup, std::int64_t value,
                            std::span<std::uint8_t> bytes, bool relocated) const {
    const auto size = bytes.size();
    const bool pc_relative = fixup.kind == FixupKind{fixup_pcrel1} || fixup.kind == FixupKind{fixup_pcrel4} ||
                             fixup.kind == FixupKind{fixup_branch4} || riprel(fixup.kind.value);
    auto field = value;
    if (!relocated && pc_relative) {
        // The field is measured from the end of the instruction.
        field -= static_cast<std::int64_t>(size);
        if (fixup.kind == FixupKind{fixup_pcrel1} && (field < -128 || field > 127)) {
            as.error(fixup.location, "branch target is out of range for a short jump");
            return false;
        }
    }
    for (std::size_t byte = 0; byte < size; ++byte) {
        bytes[byte] = static_cast<std::uint8_t>(field >> (8 * byte));
    }
    return true;
}

namespace {

// ELF x86-64 relocation numbers.
enum : std::uint32_t {
    r_x86_64_64 = 1, r_x86_64_pc32 = 2, r_x86_64_got32 = 3, r_x86_64_plt32 = 4, r_x86_64_gotpcrel = 9,
    r_x86_64_32 = 10, r_x86_64_32s = 11, r_x86_64_16 = 12, r_x86_64_pc16 = 13, r_x86_64_8 = 14,
    r_x86_64_pc8 = 15, r_x86_64_dtpoff64 = 17, r_x86_64_tpoff64 = 18, r_x86_64_tlsgd = 19,
    r_x86_64_tlsld = 20, r_x86_64_dtpoff32 = 21, r_x86_64_gottpoff = 22, r_x86_64_tpoff32 = 23,
    r_x86_64_pc64 = 24, r_x86_64_gotoff64 = 25, r_x86_64_gotpc32 = 26, r_x86_64_got64 = 27,
    r_x86_64_gotpcrel64 = 28, r_x86_64_gotpc64 = 29, r_x86_64_pltoff64 = 31, r_x86_64_size32 = 32, r_x86_64_size64 = 33,
    r_x86_64_gotpc32_tlsdesc = 34, r_x86_64_tlsdesc_call = 35, r_x86_64_gotpcrelx = 41,
    r_x86_64_rex_gotpcrelx = 42,
};

// COFF AMD64 relocation numbers.
enum : std::uint32_t {
    coff_addr64 = 1, coff_addr32 = 2, coff_addr32nb = 3, coff_rel32 = 4, coff_section = 10,
    coff_secrel = 11,
};

} // namespace

std::optional<assembler::RelocationChoice> X86Target::relocation(
    Assembler& as, const assembler::Fixup& fixup, const assembler::Value& value,
    const assembler::Symbol& symbol, const assembler::Section*) const {
    using assembler::fixup::data1;
    using assembler::fixup::data2;
    using assembler::fixup::data4;
    using assembler::fixup::data8;
    using assembler::fixup::data4_pc;
    using assembler::fixup::data8_pc;
    using assembler::fixup::section_offset4;
    using assembler::fixup::section_index2;
    using assembler::fixup::image_offset4;
    const auto kind = fixup.kind.value;
    const auto specifier = value.specifier;
    auto choice = [](std::uint32_t type, std::int64_t bias = 0, bool keep = false) {
        return assembler::RelocationChoice{type, keep, bias};
    };
    if (model_.format == Format::Coff) {
        switch (kind) {
        case data8.value: return choice(coff_addr64);
        case data4.value:
            if (specifier == spec_secrel32) return choice(coff_secrel);
            if (specifier == spec_imgrel) return choice(coff_addr32nb);
            return choice(coff_addr32);
        case data4_pc.value:
        case fixup_pcrel4:
        case fixup_branch4:
        case fixup_riprel4:
        case fixup_riprel4_relax:
        case fixup_riprel4_relax_rex:
        case fixup_riprel4_movq_load: return choice(coff_rel32);
        case section_offset4.value: return choice(coff_secrel);
        case section_index2.value: return choice(coff_section);
        case image_offset4.value: return choice(coff_addr32nb);
        case fixup_signed4: return choice(coff_addr32);
        default:
            as.error(fixup.location, "unsupported COFF relocation");
            return std::nullopt;
        }
    }
    // ELF. A TLS relocation marks its symbol STT_TLS, as LLVM MC does.
    if (value.add && (specifier == spec_tpoff || specifier == spec_dtpoff ||
                      specifier == spec_gottpoff || specifier == spec_tlsgd ||
                      specifier == spec_tlsld || specifier == spec_tlsdesc)) {
        as.symbol(*value.add).type = elf::stt_tls;
    }
    const bool is_got_base = symbol.name == "_GLOBAL_OFFSET_TABLE_";
    if (is_got_base && specifier == spec_none) {
        switch (kind) {
        case data8_pc.value: return choice(r_x86_64_gotpc64, 0, true);
        case data4_pc.value: return choice(r_x86_64_gotpc32, 0, true);
        case fixup_pcrel4:
        case fixup_riprel4:
        case fixup_riprel4_relax:
        case fixup_riprel4_relax_rex:
        case fixup_riprel4_movq_load: return choice(r_x86_64_gotpc32, -4, true);
        default: break;
        }
    }
    switch (kind) {
    case data1.value: return choice(r_x86_64_8);
    case data2.value: return choice(r_x86_64_16);
    case data8.value:
        switch (specifier) {
        case spec_none: return choice(r_x86_64_64);
        case spec_got: return choice(r_x86_64_got64);
        case spec_gotoff: return choice(r_x86_64_gotoff64);
        case spec_gotpcrel: return choice(r_x86_64_gotpcrel64, 0, true);
        case spec_pltoff: return choice(r_x86_64_pltoff64);
        case spec_size: return choice(r_x86_64_size64, value.constant);
        case spec_tpoff: return choice(r_x86_64_tpoff64);
        case spec_dtpoff: return choice(r_x86_64_dtpoff64);
        default: break;
        }
        break;
    case data8_pc.value: return choice(r_x86_64_pc64);
    case data4.value:
        switch (specifier) {
        case spec_none: return choice(r_x86_64_32);
        case spec_got: return choice(r_x86_64_got32, 0, true);
        case spec_gotoff: return choice(r_x86_64_gotoff64);  // 32-bit gotoff unsupported in x86-64
        case spec_size: return choice(r_x86_64_size32, value.constant);
        case spec_tpoff: return choice(r_x86_64_tpoff32, 0, true);
        case spec_dtpoff: return choice(r_x86_64_dtpoff32, 0, true);
        default: break;
        }
        break;
    case fixup_signed4:
        switch (specifier) {
        case spec_none: return choice(r_x86_64_32s);
        case spec_tpoff: return choice(r_x86_64_tpoff32, 0, true);
        case spec_dtpoff: return choice(r_x86_64_dtpoff32, 0, true);
        case spec_size: return choice(r_x86_64_size32, value.constant, true);
        case spec_got: return choice(r_x86_64_got32, 0, true);
        default: break;
        }
        break;
    case data4_pc.value: return choice(r_x86_64_pc32);
    case fixup_pcrel4: return choice(r_x86_64_pc32, -4);
    case fixup_pcrel1: return choice(r_x86_64_pc8, -1);
    case fixup_branch4:
        if (specifier == spec_plt) return choice(r_x86_64_plt32, value.constant - 4);
        return choice(r_x86_64_plt32, -4);
    case fixup_riprel4:
    case fixup_riprel4_relax:
    case fixup_riprel4_relax_rex:
    case fixup_riprel4_movq_load:
        switch (specifier) {
        case spec_none: return choice(r_x86_64_pc32, -4);
        case spec_plt: return choice(r_x86_64_plt32, -4);
        case spec_gotpcrel:
            return choice(kind == fixup_riprel4_relax ? r_x86_64_gotpcrelx
                          : kind == fixup_riprel4_relax_rex || kind == fixup_riprel4_movq_load
                              ? r_x86_64_rex_gotpcrelx
                              : r_x86_64_gotpcrel,
                          -4, true);
        case spec_gotpcrel_norelax: return choice(r_x86_64_gotpcrel, -4, true);
        case spec_gottpoff: return choice(r_x86_64_gottpoff, -4, true);
        case spec_tlsgd: return choice(r_x86_64_tlsgd, -4, true);
        case spec_tlsld: return choice(r_x86_64_tlsld, -4, true);
        case spec_tlsdesc: return choice(r_x86_64_gotpc32_tlsdesc, -4, true);
        default: break;
        }
        break;
    case section_offset4.value: return choice(r_x86_64_32);
    default: break;
    }
    as.error(fixup.location, "unsupported relocation for this operand");
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// COFF SEH unwind directives.

UnwindFrame* X86Target::current_frame(Assembler& as, SourceLocation location) {
    if (!open_frame_) {
        as.error(location, "this directive must appear inside a .seh_proc/.seh_endproc pair");
        return nullptr;
    }
    return &frames_[*open_frame_];
}

bool X86Target::seh_directive(Assembler& as, std::string_view name, Statement& statement) {
    auto& cursor = statement.operands;
    const auto seh_register = [&]() -> std::optional<unsigned> {
        const auto reg = parse_register(as, cursor);
        if (!reg) return std::nullopt;
        if (reg->cls == RegClass::Gpr64) {
            static constexpr std::array<unsigned, 16> seh{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};
            return seh[reg->number];
        }
        if (reg->cls == RegClass::Xmm) return reg->number;
        as.error(statement.location, "expected a general or XMM register");
        return std::nullopt;
    };
    if (name == ".seh_proc") {
        if (open_frame_) {
            as.error(statement.location, ".seh_proc before the previous .seh_endproc");
            return true;
        }
        const auto symbol_name = cursor.symbol_name();
        if (!symbol_name) {
            as.error(cursor.location(), "expected a function symbol");
            return true;
        }
        UnwindFrame frame;
        frame.function = as.symbol(*symbol_name, statement.location);
        frame.begin = as.temporary_label();
        frame.text = as.current_section();
        frame.location = statement.location;
        open_frame_ = frames_.size();
        frames_.push_back(std::move(frame));
        return true;
    }
    auto* frame = current_frame(as, statement.location);
    if (!frame) return true;
    const auto code_label = [&]() { return as.temporary_label(); };
    if (name == ".seh_endproc") {
        frame->end = as.temporary_label();
        emit_unwind_info(as, *frame);
        open_frame_.reset();
        return true;
    }
    if (name == ".seh_endprologue") {
        frame->prolog_end = code_label();
        return true;
    }
    if (name == ".seh_pushreg") {
        const auto reg = seh_register();
        if (!reg) return true;
        frame->codes.push_back({uop_push_nonvol, static_cast<std::uint8_t>(*reg), 0, code_label()});
        return true;
    }
    if (name == ".seh_stackalloc") {
        const auto size = as.parse_absolute(cursor);
        if (!size) return true;
        if (*size == 0 || (*size & 7) != 0) {
            as.error(statement.location, "stack allocation size must be a nonzero multiple of 8");
            return true;
        }
        frame->codes.push_back({uop_alloc_large, 0, static_cast<std::uint32_t>(*size), code_label()});
        return true;
    }
    if (name == ".seh_setframe") {
        const auto reg = seh_register();
        if (!reg || !cursor.consume(",")) {
            as.error(cursor.location(), "expected 'register, offset'");
            return true;
        }
        const auto offset = as.parse_absolute(cursor);
        if (!offset) return true;
        if ((*offset & 0x0f) != 0 || *offset > 240) {
            as.error(statement.location, "frame offset must be a multiple of 16 no greater than 240");
            return true;
        }
        frame->codes.push_back({uop_set_fpreg, static_cast<std::uint8_t>(*reg),
                                static_cast<std::uint32_t>(*offset), code_label()});
        frame->frame_code = frame->codes.size() - 1;
        return true;
    }
    if (name == ".seh_savereg" || name == ".seh_savexmm") {
        const auto reg = seh_register();
        if (!reg || !cursor.consume(",")) {
            as.error(cursor.location(), "expected 'register, offset'");
            return true;
        }
        const auto offset = as.parse_absolute(cursor);
        if (!offset) return true;
        const bool xmm = name == ".seh_savexmm";
        if ((xmm && (*offset & 0x0f) != 0) || (!xmm && (*offset & 7) != 0)) {
            as.error(statement.location, "save offset is not correctly aligned");
            return true;
        }
        frame->codes.push_back({static_cast<std::uint8_t>(xmm ? uop_save_xmm128 : uop_save_nonvol),
                                static_cast<std::uint8_t>(*reg), static_cast<std::uint32_t>(*offset),
                                code_label()});
        return true;
    }
    if (name == ".seh_pushframe") {
        std::uint32_t code = 0;
        if (const auto word = cursor.identifier(); word && *word == "code") code = 1;
        frame->codes.push_back({uop_push_machframe, 0, code, code_label()});
        return true;
    }
    if (name == ".seh_handler") {
        const auto handler = cursor.symbol_name();
        if (!handler) {
            as.error(cursor.location(), "expected a handler symbol");
            return true;
        }
        frame->handler = as.symbol(*handler, statement.location);
        while (cursor.consume(",")) {
            const auto word = cursor.identifier();
            if (word && *word == "@except") frame->handles_exceptions = true;
            else if (word && *word == "@unwind") frame->handles_unwind = true;
        }
        return true;
    }
    if (name == ".seh_handlerdata" || name == ".seh_savexmm128" || name == ".seh_pushframe") return true;
    return false;
}

assembler::SectionId X86Target::unwind_section(Assembler& as, bool pdata, assembler::SectionId text) {
    const char* base = pdata ? ".pdata" : ".xdata";
    const std::uint32_t characteristics = coff::scn_cnt_initialized_data | coff::scn_mem_read;
    const auto name = as.section(text).name;
    const auto comdat = as.section(text).group;
    if (comdat) {
        // A COMDAT function's unwind section is its own COMDAT named
        // `.pdata$<suffix>` (the part after the text section's first `$`), as
        // LLVM MC does without associative COMDATs.
        const auto dollar = name.find('$');
        const auto suffix = dollar == std::string::npos ? name : name.substr(dollar + 1);
        return as.coff_section(std::string(base) + "$" + suffix,
                               characteristics | coff::scn_lnk_comdat, std::nullopt,
                               coff::comdat_select_any);
    }
    return as.coff_section(base, characteristics);
}

void X86Target::emit_unwind_info(Assembler& as, UnwindFrame& frame) {
    if (frame.format_emitted_) return;
    const auto xdata = unwind_section(as, false, frame.text);
    const auto pdata = unwind_section(as, true, frame.text);
    const auto saved = as.current_section();

    // UNWIND_INFO in .xdata.
    as.push_section(xdata);
    as.section(xdata).alignment = std::max<std::uint64_t>(as.section(xdata).alignment, 4);
    as.emit_alignment(4, std::uint8_t{0}, 0);
    const auto info_label = as.temporary_label();
    frame.info = info_label;

    // Count unwind-code slots.
    const auto slot_count = [&](const UnwindCode& code) -> unsigned {
        switch (code.operation) {
        case uop_alloc_large:
            return code.offset <= 128 ? 1 : code.offset > 512 * 1024 - 8 ? 3 : 2;
        case uop_save_nonvol:
        case uop_save_xmm128: return code.offset > 512 * 1024 - 8 ? 3 : 2;
        default: return 1;
        }
    };
    unsigned codes = 0;
    for (const auto& code : frame.codes) codes += slot_count(code);
    std::uint8_t flags = 1;  // version 1
    if (frame.handler) {
        if (frame.handles_exceptions) flags |= 0x08;
        if (frame.handles_unwind) flags |= 0x10;
    }
    as.emit_integer(flags, 1);
    // Size of prolog.
    if (frame.prolog_end) {
        const auto expression = as.binary(assembler::Operator::Subtract,
                                          as.symbol_reference(*frame.prolog_end, {}),
                                          as.symbol_reference(frame.begin, {}));
        as.emit_value(expression, 1, frame.location);
    } else {
        as.emit_integer(0, 1);
    }
    as.emit_integer(codes, 1);
    std::uint8_t frame_register = 0;
    if (frame.frame_code) {
        const auto& code = frame.codes[*frame.frame_code];
        frame_register = static_cast<std::uint8_t>((code.reg & 0x0f) | (code.offset & 0xf0));
    }
    as.emit_integer(frame_register, 1);

    // Unwind codes in reverse order.
    for (auto code = frame.codes.rbegin(); code != frame.codes.rend(); ++code) {
        const auto offset_in_prolog = as.binary(assembler::Operator::Subtract,
                                                as.symbol_reference(code->label, {}),
                                                as.symbol_reference(frame.begin, {}));
        const auto emit_code = [&](std::uint8_t op_info) {
            as.emit_value(offset_in_prolog, 1, frame.location);
            as.emit_integer(static_cast<std::uint64_t>((op_info << 4U) | code->operation), 1);
        };
        switch (code->operation) {
        case uop_push_nonvol:
        case uop_set_fpreg:
            emit_code(static_cast<std::uint8_t>(code->operation == uop_set_fpreg ? 0 : code->reg));
            break;
        case uop_alloc_large:
            if (code->offset <= 128) {
                // UWOP_ALLOC_SMALL: op_info = size/8 - 1, no extra slot.
                as.emit_value(offset_in_prolog, 1, frame.location);
                as.emit_integer(static_cast<std::uint64_t>(((code->offset / 8 - 1) << 4U) | uop_alloc_small), 1);
            } else if (code->offset > 512 * 1024 - 8) {
                emit_code(1);
                as.emit_integer(code->offset & 0xffff, 2);
                as.emit_integer(code->offset >> 16U, 2);
            } else {
                emit_code(0);
                as.emit_integer(code->offset >> 3U, 2);
            }
            break;
        case uop_save_nonvol:
        case uop_save_xmm128: {
            const bool big = code->offset > 512 * 1024 - 8;
            emit_code(static_cast<std::uint8_t>(code->reg));
            if (big) {
                as.emit_integer(code->offset, 4);
            } else {
                as.emit_integer(code->operation == uop_save_xmm128 ? code->offset >> 4U : code->offset >> 3U, 2);
            }
            break;
        }
        case uop_push_machframe: emit_code(static_cast<std::uint8_t>(code->offset == 1 ? 1 : 0)); break;
        default: break;
        }
    }
    if ((codes & 1) != 0) as.emit_integer(0, 2);
    if (codes == 0 && !frame.handler) {
        as.emit_integer(0, 4);
    }
    if (frame.handler) {
        as.emit_value(as.symbol_reference(*frame.handler, {}, spec_imgrel), 4, frame.location);
    }
    as.pop_section({});

    // RUNTIME_FUNCTION in .pdata.
    as.push_section(pdata);
    as.section(pdata).alignment = std::max<std::uint64_t>(as.section(pdata).alignment, 4);
    as.emit_alignment(4, std::uint8_t{0}, 0);
    const auto begin_rva = as.symbol_reference(frame.begin, {}, spec_imgrel);
    const auto end_rva = as.binary(assembler::Operator::Add, as.symbol_reference(frame.begin, {}, spec_imgrel),
                                   as.binary(assembler::Operator::Subtract, as.symbol_reference(*frame.end, {}),
                                             as.symbol_reference(frame.begin, {})));
    as.emit_value(begin_rva, 4, frame.location);
    as.emit_value(end_rva, 4, frame.location);
    as.emit_value(as.symbol_reference(info_label, {}, spec_imgrel), 4, frame.location);
    as.pop_section({});
    as.switch_section(saved);
    frame.format_emitted_ = true;
}

bool X86Target::directive(Assembler& as, Statement& statement) {
    const auto name = lower(statement.name);
    if (name.starts_with(".seh_")) return seh_directive(as, name, statement);
    if (name == ".code64" || name == ".code32" || name == ".intel_syntax" || name == ".att_syntax") {
        if (name == ".code32" || (name == ".intel_syntax")) {
            as.error(statement.location, "only 64-bit AT&T syntax is supported");
        }
        return true;
    }
    if (name == ".value") {
        // `.value` is a 2-byte datum; the generic `.word` is also 2 bytes here.
        return false;
    }
    return false;
}

void X86Target::finish(Assembler& as) {
    if (open_frame_) {
        as.error(frames_[*open_frame_].location, "missing .seh_endproc");
    }
    if (model_.format != Format::Elf) return;
    // A symbol defined in a thread-local section is STT_TLS.
    for (std::uint32_t index = 0; index < as.symbols().size(); ++index) {
        const auto& symbol = as.symbol(assembler::SymbolId{index});
        if (symbol.section && symbol.type == elf::stt_notype &&
            (as.section(*symbol.section).flags & elf::shf_tls) != 0) {
            as.symbol(assembler::SymbolId{index}).type = elf::stt_tls;
        }
    }
}

void X86Target::finish_tables(Assembler&) {}

bool X86Target::macho_relocation(Assembler& as, assembler::SectionId section, const assembler::Fixup& fixup,
                                 std::uint64_t place, std::uint64_t address, const assembler::Value& value,
                                 std::int64_t& field, std::vector<assembler::Relocation>& relocations) const {
    // Mach-O x86-64 relocations (reloc.h); each is RELA-less and the field
    // holds the addend.
    enum : std::uint32_t {
        x86_64_unsigned = 0, x86_64_signed = 1, x86_64_branch = 2, x86_64_got_load = 3, x86_64_got = 4,
        x86_64_subtractor = 5, x86_64_signed1 = 6, x86_64_signed2 = 7, x86_64_signed4 = 8, x86_64_tlv = 9,
    };
    const auto kind = fixup.kind.value;
    const bool pc_relative = kind == fixup_pcrel1 || kind == fixup_pcrel4 || kind == fixup_branch4 ||
                             riprel(kind) || kind == assembler::fixup::data4_pc.value ||
                             kind == assembler::fixup::data8_pc.value;
    const unsigned length = kind == assembler::fixup::data8.value || kind == assembler::fixup::data8_pc.value ? 3U
                            : kind == assembler::fixup::data2.value ? 1U
                            : kind == fixup_pcrel1 || kind == assembler::fixup::data1.value ? 0U
                                                                                            : 2U;
    const bool debug = (as.section(section).characteristics & assembler::macho::s_attr_debug) != 0;
    const auto push = [&](std::uint32_t type, assembler::SymbolId symbol, bool external, bool pcrel) {
        relocations.push_back({place, type, symbol, 0, pcrel, static_cast<std::uint8_t>(length), external});
    };

    // A - B + constant: SUBTRACTOR then UNSIGNED, both section-relative when
    // local (debug sections), external otherwise.
    if (value.subtract) {
        if (!value.add) {
            as.error(fixup.location, "a Mach-O relocation needs a base symbol");
            return false;
        }
        auto& minuend = as.symbol(*value.add);
        auto& subtrahend = as.symbol(*value.subtract);
        const auto minuend_atom = as.atom(*value.add);
        const auto subtrahend_atom = as.atom(*value.subtract);
        field = value.constant + static_cast<std::int64_t>(as.address(minuend)) -
                static_cast<std::int64_t>(as.address(subtrahend));
        const bool sub_external = subtrahend_atom.has_value() && !debug;
        const auto sub_symbol = sub_external ? *subtrahend_atom
                                : subtrahend.section ? as.section(*subtrahend.section).symbol
                                                     : *value.subtract;
        if (sub_external && *subtrahend_atom != *value.subtract) {
            field += static_cast<std::int64_t>(as.address(subtrahend)) -
                     static_cast<std::int64_t>(as.address(as.symbol(*subtrahend_atom)));
        }
        push(x86_64_subtractor, sub_symbol, sub_external, false);
        const bool add_external = minuend_atom.has_value() && !debug;
        const auto add_symbol = add_external ? *minuend_atom
                                : minuend.section ? as.section(*minuend.section).symbol
                                                  : *value.add;
        if (add_external && *minuend_atom != *value.add) {
            field += static_cast<std::int64_t>(as.address(as.symbol(*minuend_atom))) -
                     static_cast<std::int64_t>(as.address(minuend));
        }
        push(x86_64_unsigned, add_symbol, add_external, false);
        return true;
    }
    if (!value.add) {
        as.error(fixup.location, "a Mach-O relocation needs a symbol");
        return false;
    }
    auto& symbol = as.symbol(*value.add);
    const auto atom = as.atom(*value.add);
    const auto* base = as.base_symbol(symbol);
    field = value.constant;
    std::uint32_t type = x86_64_unsigned;
    if (value.specifier == spec_gotpcrel) {
        type = kind == fixup_riprel4_movq_load ? x86_64_got_load : x86_64_got;
    } else if (value.specifier == spec_tlvp) {
        type = x86_64_tlv;
    } else if (pc_relative) {
        type = (kind == fixup_branch4 || kind == fixup_pcrel4 || kind == fixup_pcrel1) ? x86_64_branch
                                                                                       : x86_64_signed;
        // A PC-relative relocation records the PC adjustment itself, so the
        // in-place field carries no (1 << length) bias. SIGNED_1/2/4 describe
        // trailing immediate bytes after a RIP field.
        if (type == x86_64_signed) {
            switch (-value.constant) {
            case 1: type = x86_64_signed1; break;
            case 2: type = x86_64_signed2; break;
            case 4: type = x86_64_signed4; break;
            default: break;
            }
        }
    }
    const bool external = atom.has_value() && !(debug && base && base->section);
    if (external) {
        if (*atom != *value.add && base && base->section) {
            field += static_cast<std::int64_t>(as.address(symbol)) -
                     static_cast<std::int64_t>(as.address(as.symbol(*atom)));
        }
        push(type, *atom, true, pc_relative);
    } else if (base && base->section) {
        field += static_cast<std::int64_t>(as.address(symbol));
        if (pc_relative) field -= static_cast<std::int64_t>(address + (1LL << length));
        push(type, as.section(*base->section).symbol, false, pc_relative);
    } else {
        push(type, *value.add, true, pc_relative);
    }
    return true;
}


} // namespace

std::optional<std::vector<std::uint8_t>> assemble(
    const std::vector<const SourceFile*>& sources, const Subtarget& subtarget,
    const assembler::Options& options, SourceManager& manager, Diagnostics& diagnostics) {
    Model model;
    switch (subtarget.object_format()) {
    case ObjectFormat::Elf: model.format = Format::Elf; break;
    case ObjectFormat::Coff: model.format = Format::Coff; break;
    case ObjectFormat::MachO: model.format = Format::MachO; break;
    case ObjectFormat::Unsupported:
        diagnostics.command_error("the target's object format has no x86-64 assembler");
        return std::nullopt;
    }
    X86Target target(model);
    return assembler::assemble(sources, target, options, manager, diagnostics);
}

} // namespace cross::x86_64
