// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The printf and scanf call checkers that cc provides as $::format::printf
// and $::format::scanf. Their rules follow GCC's -Wformat. The C types that
// conversions and length modifiers name come from the type.NAME properties
// of the called function's ABI model.

namespace format {

// Every function here runs only during translation.
[[eval_only]] {

// --- Messages ---

static $::meta::bytes text(in const u8 *string) {
    uptr length = 0;
    while (string[length] != 0) ++length;
    $::meta::buffer buffer = $::meta::alloc(length);
    u8 *bytes = $::meta::data(buffer);
    for (uptr i = 0; i < length; ++i) bytes[i] = string[i];
    return $::meta::freeze(buffer, length);
}

static $::meta::bytes join(in $::meta::bytes left, in const u8 *right) {
    return $::meta::concat(left, text(right));
}

static $::meta::bytes character(in u32 value) {
    $::meta::buffer buffer = $::meta::alloc(1);
    $::meta::data(buffer)[0] = (u8)value;
    return $::meta::freeze(buffer, 1);
}

// A character in quotes, or in hexadecimal when it does not print.
static $::meta::bytes shown(in u32 value) {
    if (value >= 0x20 && value < 0x7f) return join($::meta::concat(text("'"), character(value)), "'");
    $::meta::buffer buffer = $::meta::alloc(4);
    u8 *digits = $::meta::data(buffer);
    u32 high = (value >> 4) & 15;
    u32 low = value & 15;
    digits[0] = '0';
    digits[1] = 'x';
    digits[2] = (u8)(high < 10 ? '0' + high : 'a' + high - 10);
    digits[3] = (u8)(low < 10 ? '0' + low : 'a' + low - 10);
    return $::meta::freeze(buffer, 4);
}

static $::meta::bytes number(in uptr value) {
    $::meta::buffer buffer = $::meta::alloc(20);
    u8 *digits = $::meta::data(buffer);
    uptr count = 0;
    do {
        digits[count] = (u8)('0' + value % 10);
        ++count;
        value = value / 10;
    } while (value != 0);
    for (uptr i = 0; i < count / 2; ++i) {
        u8 swap = digits[i];
        digits[i] = digits[count - 1 - i];
        digits[count - 1 - i] = swap;
    }
    return $::meta::freeze(buffer, count);
}

static bool starts_with(in $::meta::bytes bytes, in const u8 *prefix) {
    for (uptr i = 0; prefix[i] != 0; ++i)
        if (i >= $::meta::len(bytes) || $::meta::at(bytes, i) != prefix[i]) return 0;
    return 1;
}

// --- C types ---

// The length modifiers, in GCC's order.
enum length { length_none, length_hh, length_h, length_l, length_ll, length_L, length_z, length_t, length_j };

static $::meta::bytes length_spelling(in u32 length) {
    switch (length) {
    case length_hh: return text("hh");
    case length_h: return text("h");
    case length_l: return text("l");
    case length_ll: return text("ll");
    case length_L: return text("L");
    case length_z: return text("z");
    case length_t: return text("t");
    case length_j: return text("j");
    default: return text("");
    }
}

// The C types that conversions take, which an ABI names with type.NAME
// properties.
enum c_name {
    name_none, name_char, name_short, name_int, name_long, name_long_long, name_size_t, name_ptrdiff_t,
    name_intmax_t, name_wchar_t, name_wint_t, name_float, name_double, name_long_double
};

// The type that the callee's ABI names `name`, or void.
static $::meta::type abi(in $::meta::call call, in u32 name) {
    switch (name) {
    case name_char: return $::meta::abi_type(call, "char");
    case name_short: return $::meta::abi_type(call, "short");
    case name_int: return $::meta::abi_type(call, "int");
    case name_long: return $::meta::abi_type(call, "long");
    case name_long_long: return $::meta::abi_type(call, "long_long");
    case name_size_t: return $::meta::abi_type(call, "size_t");
    case name_ptrdiff_t: return $::meta::abi_type(call, "ptrdiff_t");
    case name_intmax_t: return $::meta::abi_type(call, "intmax_t");
    case name_wchar_t: return $::meta::abi_type(call, "wchar_t");
    case name_wint_t: return $::meta::abi_type(call, "wint_t");
    case name_float: return $::meta::abi_type(call, "float");
    case name_double: return $::meta::abi_type(call, "double");
    case name_long_double: return $::meta::abi_type(call, "long_double");
    default: return $::meta::abi_type(call, "void");
    }
}

// How C spells a type name.
static $::meta::bytes c_spelling(in u32 name) {
    switch (name) {
    case name_char: return text("char");
    case name_short: return text("short");
    case name_int: return text("int");
    case name_long: return text("long");
    case name_long_long: return text("long long");
    case name_size_t: return text("size_t");
    case name_ptrdiff_t: return text("ptrdiff_t");
    case name_intmax_t: return text("intmax_t");
    case name_wchar_t: return text("wchar_t");
    case name_wint_t: return text("wint_t");
    case name_float: return text("float");
    case name_double: return text("double");
    default: return text("long double");
    }
}

// The C type of the integer that a length modifier selects; GCC's `%Ld` and
// BSD's `%qd` mean long long.
static u32 integer_name(in u32 length) {
    switch (length) {
    case length_hh: return name_char;
    case length_h: return name_short;
    case length_l: return name_long;
    case length_ll: case length_L: return name_long_long;
    case length_z: return name_size_t;
    case length_t: return name_ptrdiff_t;
    case length_j: return name_intmax_t;
    default: return name_int;
    }
}

// "C 'long' (i64)": a C type and the Cross type that the ABI gives it.
static $::meta::bytes c_type(in $::meta::call call, in u32 name) {
    $::meta::bytes result = join($::meta::concat(text("C '"), c_spelling(name)), "'");
    $::meta::type type = abi(call, name);
    if ($::meta::is_type_kind(type, "void")) return result;
    return join($::meta::concat(join(result, " ("), $::meta::type_spelling(type)), ")");
}

static $::meta::bytes pointer_to(in u32 name) {
    return join($::meta::concat(text("a pointer to C '"), c_spelling(name)), "'");
}

static bool integer_like(in $::meta::type type) {
    return $::meta::is_type_kind(type, "integer") || $::meta::is_type_kind(type, "enum") ||
        $::meta::is_type_kind(type, "bool");
}

// The width of an integer argument after the default argument promotions.
static u32 promoted_bits(in $::meta::type type) {
    u32 bits = $::meta::bits(type);
    return bits < 32 ? 32 : bits;
}

// Whether `type` is an integer of the width of the C type `name`; as in
// GCC, signedness does not matter.
static bool integer_of(in $::meta::call call, in $::meta::type type, in u32 name) {
    return integer_like(type) && $::meta::bits(type) == $::meta::bits(abi(call, name));
}

static bool floating_conversion(in u32 conversion) {
    switch (conversion) {
    case 'a': case 'A': case 'e': case 'E': case 'f': case 'F': case 'g': case 'G': return 1;
    default: return 0;
    }
}

static bool integer_conversion(in u32 conversion) {
    switch (conversion) {
    case 'd': case 'i': case 'o': case 'u': case 'x': case 'X': return 1;
    default: return 0;
    }
}

// Whether a conversion character is one that printf, or scanf, knows.
static bool known_conversion(in u32 conversion, in bool scan) {
    if (integer_conversion(conversion) || floating_conversion(conversion)) return 1;
    switch (conversion) {
    case 'c': case 's': case 'p': case 'n': return 1;
    case '[': return scan;
    case 'm': case 'C': case 'S': return !scan;
    default: return 0;
    }
}

static bool printf_flag(in u32 value) {
    return value == '-' || value == '+' || value == ' ' || value == '#' || value == '0' || value == '\'';
}

static bool character_conversion(in u32 conversion) {
    return conversion == 's' || conversion == 'c' || conversion == '[';
}

// --- What a directive takes ---

// The C type of what a printf directive's value has, or of what a scanf
// directive's pointer points to; none for a pointer argument.
static u32 wanted_name(in u32 conversion, in u32 length, in bool scan) {
    if (floating_conversion(conversion))
        return length == length_L ? name_long_double : length == length_l || !scan ? name_double : name_float;
    if (conversion == 'S' || (character_conversion(conversion) && length == length_l) ||
        (conversion == 'C' && !scan))
        return scan || conversion == 's' || conversion == 'S' ? name_wchar_t : name_wint_t;
    if (character_conversion(conversion)) return scan || conversion == 's' ? name_char : name_int;
    if (conversion == 'p') return name_none;
    if (!scan && conversion != 'n' && (length == length_hh || length == length_h)) return name_int;
    return integer_name(length);
}

// How a message names what a directive takes.
static $::meta::bytes wanted(in $::meta::call call, in u32 conversion, in u32 length, in bool scan,
                             in bool allocate) {
    u32 name = wanted_name(conversion, length, scan);
    if (conversion == 'p') return text(scan ? "a pointer to a pointer" : "a pointer");
    if (scan && allocate && character_conversion(conversion)) return text("a pointer to a pointer");
    if (scan || conversion == 'n' || conversion == 's' || conversion == 'S') return pointer_to(name);
    return c_type(call, name);
}

// Whether a printf value argument suits its directive.
static bool printf_matches(in $::meta::call call, in $::meta::type type, in u32 conversion, in u32 length) {
    u32 name = wanted_name(conversion, length, 0);
    if (conversion == 'p') return $::meta::is_type_kind(type, "pointer");
    if (conversion == 'n' || conversion == 's' || conversion == 'S')
        return $::meta::is_type_kind(type, "pointer") && integer_of(call, $::meta::element(type), name);
    $::meta::type want = abi(call, name);
    if (floating_conversion(conversion)) {
        if (!$::meta::is_type_kind(type, "floating")) return 0;
        // An f32 argument is promoted to f64.
        if ($::meta::bits(type) == 32)
            return length != length_L && $::meta::is_type_kind(want, "floating") && $::meta::bits(want) == 64;
        return $::meta::same_type(type, want);
    }
    return integer_like(type) && promoted_bits(type) == promoted_bits(want);
}

// Whether a scanf pointer argument suits its directive; a constant object
// is reported separately.
static bool scanf_matches(in $::meta::call call, in $::meta::type type, in u32 conversion, in u32 length,
                          in bool allocate) {
    if (!$::meta::is_type_kind(type, "pointer")) return 0;
    $::meta::type pointee = $::meta::element(type);
    if (conversion == 'p' || (allocate && character_conversion(conversion)))
        return $::meta::is_type_kind(pointee, "pointer");
    u32 name = wanted_name(conversion, length, 1);
    if (floating_conversion(conversion)) return $::meta::same_type(pointee, abi(call, name));
    return integer_of(call, pointee, name);
}

// Whether a length modifier suits a conversion.
static bool length_valid(in u32 conversion, in u32 length) {
    if (length == length_none || integer_conversion(conversion)) return 1;
    if (conversion == 'n') return length != length_L;
    if (character_conversion(conversion)) return length == length_l;
    if (floating_conversion(conversion)) return length == length_l || length == length_L;
    return 0;
}

// Whether a printf conversion accepts a flag, as in GCC's table.
static bool flag_allowed(in u32 conversion, in u32 flag) {
    if (flag == '-') return 1;
    switch (conversion) {
    case 'd': case 'i': return flag == '+' || flag == ' ' || flag == '0' || flag == '\'';
    case 'o': case 'x': case 'X': return flag == '0' || flag == '#';
    case 'u': return flag == '0' || flag == '\'';
    case 'f': case 'F': case 'g': case 'G': return flag != '-';
    case 'e': case 'E': case 'a': case 'A': return flag != '\'';
    default: return 0;
    }
}

// --- Reading a directive ---

// The length modifier at `at`, and the position after it.
static u32 length_at(in $::meta::bytes format, in uptr size, in uptr at) {
    if (at >= size) return length_none;
    u8 first = $::meta::at(format, at);
    u8 second = at + 1 < size ? $::meta::at(format, at + 1) : 0;
    if (first == 'h') return second == 'h' ? length_hh : length_h;
    if (first == 'l') return second == 'l' ? length_ll : length_l;
    if (first == 'L') return length_L;
    if (first == 'q') return length_ll;
    if (first == 'z') return length_z;
    if (first == 't') return length_t;
    if (first == 'j') return length_j;
    return length_none;
}

static uptr length_end(in $::meta::bytes format, in uptr size, in uptr at) {
    u32 length = length_at(format, size, at);
    if (length == length_none) return at;
    return length == length_hh || (length == length_ll && $::meta::at(format, at) == 'l') ? at + 2 : at + 1;
}

static uptr digits_end(in $::meta::bytes format, in uptr size, in uptr at) {
    while (at < size && $::meta::at(format, at) >= '0' && $::meta::at(format, at) <= '9') ++at;
    return at;
}

// The operand number `N$` at `at`, or 0 without one, and the position after.
static uptr position_at(in $::meta::bytes format, in uptr size, in uptr at) {
    uptr end = digits_end(format, size, at);
    if (end == at || end >= size || $::meta::at(format, end) != '$') return 0;
    uptr value = 0;
    for (uptr i = at; i < end; ++i) value = value * 10 + ($::meta::at(format, i) - '0');
    return value;
}

static uptr position_end(in $::meta::bytes format, in uptr size, in uptr at) {
    return position_at(format, size, at) == 0 ? at : digits_end(format, size, at) + 1;
}

// --- Reports ---

// A problem of the format itself, reported at the format argument.
static void bad_format(in $::meta::call call, in uptr format_argument, in $::meta::bytes message) {
    $::meta::warning($::meta::argument_span(call, format_argument), message);
}

// "format '%ld' ": the start of a message about one directive.
static $::meta::bytes directive(in $::meta::bytes format, in uptr start, in uptr end) {
    return join($::meta::concat(text("format '"), $::meta::slice(format, start, end - start)), "' ");
}

static void mismatch(in $::meta::call call, in $::meta::bytes format, in uptr start, in uptr end,
                     in $::meta::bytes want, in uptr argument) {
    $::meta::bytes message = $::meta::concat(join(directive(format, start, end), "expects "), want);
    message = $::meta::concat(join(message, ", but argument "), number(argument + 1));
    message = $::meta::concat(join(message, " has type '"),
                              $::meta::type_spelling($::meta::argument_type(call, argument)));
    $::meta::warning($::meta::argument_span(call, argument), join(message, "'"));
}

// Reports a directive whose operand the call lacks.
static void missing(in $::meta::call call, in $::meta::bytes format, in uptr start, in uptr end,
                    in uptr format_argument, in $::meta::bytes want) {
    bad_format(call, format_argument,
               join($::meta::concat(join(directive(format, start, end), "expects a matching "), want), " argument"));
}

// --- The checkers ---

static void check(in $::meta::call call, in bool scan) {
    // The format: the last named parameter that points to an 8-bit integer.
    uptr format_argument = $::meta::parameter_count(call);
    for (uptr i = 0; i < $::meta::parameter_count(call); ++i) {
        $::meta::type type = $::meta::parameter_type(call, i);
        if ($::meta::is_type_kind(type, "pointer") && integer_like($::meta::element(type)) &&
            $::meta::bits($::meta::element(type)) == 8) format_argument = i;
    }
    if (format_argument >= $::meta::parameter_count(call)) {
        $::meta::error($::meta::call_span(call),
            "a format checker needs a format parameter that points to an 8-bit integer");
        return;
    }
    // A format that is not a string literal is not checked, as with GCC's
    // default -Wno-format-nonliteral.
    $::meta::bytes format = $::meta::argument_string(call, format_argument);
    if ($::meta::len(format) == 0) return;
    uptr size = $::meta::len(format) - 1;
    if (size == 0) {
        bad_format(call, format_argument, text("zero-length format string"));
        return;
    }
    // Without the C types of the ABI, such as under a Cross ABI, only the
    // format and the number of arguments are checked.
    $::meta::type int_type = $::meta::abi_type(call, "int");
    bool typed = !$::meta::is_type_kind(int_type, "void");
    // A function without unnamed arguments, such as vprintf, takes the format
    // alone, as with GCC's format(printf, N, 0).
    bool arguments = $::meta::is_variadic(call);
    uptr next = 0;
    uptr highest = 0;
    bool positional = 0;
    bool sequential = 0;
    for (uptr at = 0; at < size; ++at) {
        u32 current = $::meta::at(format, at);
        if (current == 0) {
            bad_format(call, format_argument, text("embedded '\\0' in format"));
            return;
        }
        if (current != '%') continue;
        uptr start = at;
        ++at;
        if (at >= size) {
            bad_format(call, format_argument, text("spurious trailing '%' in format"));
            return;
        }
        if ($::meta::at(format, at) == '%') continue;
        uptr position = position_at(format, size, at);
        at = position_end(format, size, at);
        bool suppressed = 0;
        if (scan && at < size && $::meta::at(format, at) == '*') {
            suppressed = 1;
            ++at;
        }
        u8 flags[6];
        uptr flag_count = 0;
        while (!scan && at < size && printf_flag($::meta::at(format, at))) {
            u8 flag = $::meta::at(format, at);
            bool repeated = 0;
            for (uptr i = 0; i < flag_count; ++i)
                if (flags[i] == flag) repeated = 1;
            if (repeated)
                bad_format(call, format_argument,
                           join($::meta::concat(text("repeated '"), character(flag)), "' flag in format"));
            else if (flag_count < 6) {
                flags[flag_count] = flag;
                ++flag_count;
            }
            ++at;
        }
        // A printf width or precision is digits, or `*` or `*N$`, which takes
        // an int operand.
        uptr stars = 0;
        uptr star_positions[2];
        if (!scan && at < size && $::meta::at(format, at) == '*') {
            ++at;
            star_positions[stars] = position_at(format, size, at);
            at = position_end(format, size, at);
            ++stars;
        } else {
            at = digits_end(format, size, at);
        }
        bool precision = 0;
        if (!scan && at < size && $::meta::at(format, at) == '.') {
            precision = 1;
            ++at;
            if (at < size && $::meta::at(format, at) == '*') {
                ++at;
                star_positions[stars] = position_at(format, size, at);
                at = position_end(format, size, at);
                ++stars;
            } else {
                at = digits_end(format, size, at);
            }
        }
        bool allocate = 0;
        if (scan && at < size && $::meta::at(format, at) == 'm') {
            allocate = 1;
            ++at;
        }
        u32 length = length_at(format, size, at);
        at = length_end(format, size, at);
        if (at >= size) {
            bad_format(call, format_argument, text("conversion lacks type at end of format"));
            return;
        }
        u32 conversion = $::meta::at(format, at);
        if (!known_conversion(conversion, scan)) {
            bad_format(call, format_argument,
                join($::meta::concat(text("unknown conversion type character "), shown(conversion)), " in format"));
            continue;
        }
        if (conversion == '[') {
            uptr close = at + 1;
            if (close < size && $::meta::at(format, close) == '^') ++close;
            if (close < size && $::meta::at(format, close) == ']') ++close;
            while (close < size && $::meta::at(format, close) != ']') ++close;
            if (close >= size) {
                bad_format(call, format_argument, text("no closing ']' for '%[' format"));
                return;
            }
            at = close;
        }
        uptr end = at + 1;
        if (!length_valid(conversion, length))
            bad_format(call, format_argument, join($::meta::concat(join($::meta::concat(text("use of '"),
                length_spelling(length)), "' length modifier with '"), character(conversion)), "' type character"));
        if (!scan) {
            bool minus = 0;
            bool zero = 0;
            bool space = 0;
            bool plus = 0;
            for (uptr i = 0; i < flag_count; ++i) {
                if (flags[i] == '-') minus = 1;
                if (flags[i] == '0') zero = 1;
                if (flags[i] == ' ') space = 1;
                if (flags[i] == '+') plus = 1;
                if (!flag_allowed(conversion, flags[i]))
                    bad_format(call, format_argument,
                        join($::meta::concat(join($::meta::concat(text("'"), character(flags[i])), "' flag used with '"),
                                             $::meta::slice(format, start, end - start)), "' in format"));
            }
            if (precision && (conversion == 'c' || conversion == 'p' || conversion == 'n' || conversion == 'C'))
                bad_format(call, format_argument, join(directive(format, start, end), "does not take a precision"));
            if (zero && minus) bad_format(call, format_argument, text("'0' flag ignored with '-' flag in format"));
            if (space && plus) bad_format(call, format_argument, text("' ' flag ignored with '+' flag in format"));
            if (zero && precision && integer_conversion(conversion))
                bad_format(call, format_argument, join(directive(format, start, end),
                                                       "ignores the '0' flag because it has a precision"));
        }
        if (!arguments || conversion == 'm') continue;
        for (uptr i = 0; i < stars; ++i) {
            uptr operand = star_positions[i];
            if (operand == 0) {
                sequential = 1;
                ++next;
                operand = next;
            } else {
                positional = 1;
            }
            if (operand > highest) highest = operand;
            uptr argument = format_argument + operand;
            if (argument >= $::meta::argument_count(call)) {
                missing(call, format, start, end, format_argument, c_type(call, name_int));
                continue;
            }
            if (!typed) continue;
            $::meta::type type = $::meta::argument_type(call, argument);
            if (!integer_like(type) || promoted_bits(type) != promoted_bits(int_type))
                mismatch(call, format, start, end, join(c_type(call, name_int), " for '*'"), argument);
        }
        if (suppressed) continue;
        uptr operand = position;
        if (operand == 0) {
            sequential = 1;
            ++next;
            operand = next;
        } else {
            positional = 1;
        }
        if (operand > highest) highest = operand;
        uptr argument = format_argument + operand;
        if (argument >= $::meta::argument_count(call)) {
            missing(call, format, start, end, format_argument, wanted(call, conversion, length, scan, allocate));
            continue;
        }
        if (!typed) continue;
        $::meta::type type = $::meta::argument_type(call, argument);
        if (scan && $::meta::is_type_kind(type, "pointer") &&
            starts_with($::meta::type_spelling($::meta::element(type)), "const ")) {
            $::meta::warning($::meta::argument_span(call, argument),
                $::meta::concat(text("writing into a constant object (argument "), join(number(argument + 1), ")")));
            continue;
        }
        if (scan ? !scanf_matches(call, type, conversion, length, allocate)
                 : !printf_matches(call, type, conversion, length))
            mismatch(call, format, start, end, wanted(call, conversion, length, scan, allocate), argument);
    }
    if (positional && sequential) {
        bad_format(call, format_argument, text("missing $ operand number in format"));
        return;
    }
    if (arguments && format_argument + highest + 1 < $::meta::argument_count(call))
        $::meta::warning($::meta::argument_span(call, format_argument + highest + 1), "too many arguments for format");
}

[[checker("format")]]
static void printf(in $::meta::call call) {
    check(call, 0);
}

[[checker("format")]]
static void scanf(in $::meta::call call) {
    check(call, 1);
}

}

}
