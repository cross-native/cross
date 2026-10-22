// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

// Runs the operations of c_forms.x in C++ with the same inputs and compares
// the results; C++ promotes variadic arguments as C does.

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#if defined(_WIN32)
#define CROSS_ABI __attribute__((ms_abi))
#else
#define CROSS_ABI __attribute__((sysv_abi))
#endif

struct Sample {
    std::int32_t value;
    std::uint8_t byte;
    std::int16_t small;
    std::uint16_t wide;
    bool flag;
    float ratio;
    std::int64_t big;
};

static std::vector<std::string> log_entries;

extern "C" CROSS_ABI int record(const char *format, ...) {
    va_list arguments;
    va_start(arguments, format);
    char text[64];
    for (const char *kind = format; *kind; ++kind) {
        if (*kind == 'i') std::snprintf(text, sizeof text, "i%d", va_arg(arguments, int));
        else if (*kind == 'd') std::snprintf(text, sizeof text, "d%a", va_arg(arguments, double));
        else std::snprintf(text, sizeof text, "l%lld", va_arg(arguments, long long));
        log_entries.push_back(text);
    }
    va_end(arguments);
    return 1;
}

extern "C" {
CROSS_ABI std::int32_t qualified_variadic(const Sample *s, const std::uint8_t *bytes,
                                          std::int16_t negative, bool choose);
CROSS_ABI std::int32_t check(const char *s, std::int64_t *v);
CROSS_ABI std::int32_t discard_height(std::int32_t size);
CROSS_ABI std::intptr_t qualified_difference(const std::uint8_t *end, std::uint8_t *begin);
CROSS_ABI double leading_dot(double scale);
CROSS_ABI std::int32_t voided(std::int32_t x);
}

static int reference_variadic(const Sample *s, const std::uint8_t *bytes,
                              const std::int16_t negative, bool choose) {
    const float local = 2.5f;
    volatile std::uint8_t port = 200;
    int count = record("i", s->value);
    count += record("i", bytes[1]);
    count += record("i", s->value + 0);
    count += record("i", +s->value);
    count += record("i", choose ? s->value : 0);
    count += record("i", s->byte);
    count += record("i", s->small);
    count += record("i", s->wide);
    count += record("i", s->flag);
    count += record("i", negative);
    count += record("d", s->ratio);
    count += record("d", local);
    count += record("l", static_cast<long long>(s->big));
    count += record("i", port);
    return count;
}

static int reference_parse(const char *s, std::int64_t &v) {
    std::int64_t sign = 1;
    v = 0;
    if (*s == '-') {
        sign = -1;
        ++s;
    }
    if (*s < '0' || *s > '9') return 1;
    while (*s >= '0' && *s <= '9') v = v * 10 + (*s++ - '0');
    v *= sign;
    return *s != 0;
}

static int reference_check(const char *s, std::int64_t &v) {
    if (!s || reference_parse(s, v) || v < 0) {
        v = 0;
        return -1;
    }
    return 0;
}

static int reference_voided(int x) {
    ++x;
    if (x) ++x;
    return x;
}

int main() {
    int failures = 0;
    const auto expect = [&](bool ok, const char *what) {
        if (!ok) {
            std::fprintf(stderr, "mismatch: %s\n", what);
            ++failures;
        }
    };
    const Sample sample{-7, 250, -300, 65000, true, 0.375f, -(1LL << 40)};
    const std::uint8_t bytes[] = {1, 233, 3};
    for (const bool choose : {false, true}) {
        log_entries.clear();
        const int count = qualified_variadic(&sample, bytes, -2, choose);
        const auto cross = log_entries;
        log_entries.clear();
        const int reference = reference_variadic(&sample, bytes, -2, choose);
        expect(count == reference && cross == log_entries, "qualified variadic arguments");
        for (std::size_t i = 0; i < cross.size() && i < log_entries.size(); ++i)
            if (cross[i] != log_entries[i])
                std::fprintf(stderr, "  %zu: %s != %s\n", i, cross[i].c_str(), log_entries[i].c_str());
    }
    for (const char *input : {static_cast<const char *>(nullptr), "12", "x", "-5", "7z", "0"}) {
        std::int64_t cross_value = 99, reference_value = 99;
        const int cross_result = check(input, &cross_value);
        const int reference_result = reference_check(input, reference_value);
        expect(cross_result == reference_result && cross_value == reference_value,
               "short-circuit definite assignment");
    }
    for (const int size : {0, 4, -3})
        expect(discard_height(size) == (size + 1) * 100 + size + 1000, "discarded out argument");
    std::uint8_t buffer[16] = {};
    expect(qualified_difference(&buffer[11], &buffer[2]) == 9, "pointer difference across qualification");
    expect(qualified_difference(&buffer[1], &buffer[5]) == -4, "negative pointer difference");
    for (const double scale : {0.0, 3.0, -1.5})
        expect(leading_dot(scale) == .5 * scale + .25e1 + static_cast<double>(.125f), "leading-dot literals");
    for (const int x : {0, 5, -1})
        expect(voided(x) == reference_voided(x), "void casts");
    return failures == 0 ? 0 : 1;
}
