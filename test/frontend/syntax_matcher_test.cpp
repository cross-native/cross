// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later
#include "frontend/lexer.hpp"
#include "frontend/syntax.hpp"

#include <cstdlib>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {
using namespace cross;

// One character denotes one source token. The reference works with languages
// and endpoint multiplicities, not the compiler's FIRST sets or match records.
using Endpoints = std::vector<std::size_t>;
using Reference = std::function<Endpoints(std::string_view)>;

struct Fixture {
    SourceManager sources;
    std::ostringstream messages;
    Diagnostics diagnostics{messages};
    std::shared_ptr<SyntaxExecution> execution;
    SyntaxState state;
    std::string name;
    unsigned bits;
    unsigned cases{};

    static EvaluationLimits limits() {
        EvaluationLimits result;
        // This fixture checks many independent invocations in one executor.
        // Keep the normal per-match depth/storage limits; this is not a change
        // to the compiler's production defaults.
        result.steps = 50000000;
        return result;
    }

    Fixture(std::string label, unsigned address_bits, std::string rules,
            std::string pattern)
        : execution(std::make_shared<SyntaxExecution>(sources, diagnostics, address_bits,
              [](const TypePtr&) -> std::optional<std::uint64_t> { return {}; },
              [](const TypePtr&) -> std::optional<std::uint64_t> { return {}; },
              limits(), EvaluationLayout{})), state(execution), name(std::move(label)), bits(address_bits) {
        const auto* source = sources.add(name + "-grammar.x", R"(
[[syntax_expander]] static $::meta::tokens expand(in $::meta::syntax_match input) {
    return $::quote { 1u32 };
}
)" + rules + "syntax Owner : expression { prefix \"owner\"; match " + pattern + "; expand expand; }");
        const auto input = execution->prepare(*source);
        std::size_t at{};
        require(execution->define_function(input, at, "", {}, state.bindings()) != nullptr,
                "expander definition failed");
        while (input[at].kind != TokenKind::End)
            require(state.declare(input, at, "", diagnostics), "grammar declaration failed");
        const SyntaxActivation activation{"Owner", {}, input.front().location};
        require(state.activate(std::span(&activation, 1), "", diagnostics), "grammar activation failed");
        require(diagnostics.errors() == 0, "grammar produced diagnostics");
    }

    void require(bool condition, std::string_view detail) const {
        if (condition) return;
        std::cerr << name << "/" << bits << ": " << detail << '\n' << messages.str();
        std::exit(1);
    }

    void check(std::string_view word, const Endpoints& expected, bool fenced) {
        messages.str({});
        messages.clear();
        std::string source = fenced ? "owner ( " : "owner ";
        for (const char token : word) { source += token; source += ' '; }
        if (fenced) source += ")";
        const auto input = execution->prepare(*sources.add(name + "-input-" +
            std::to_string(cases++) + ".x", source));
        const auto* definition = state.selected(input.front(), false);
        require(definition != nullptr, "prefix disappeared");
        const auto errors = diagnostics.errors();
        const auto matched = state.match(*definition, input, 0, diagnostics);
        const auto description = "input '" + source + "'";
        if (expected.size() == 1) {
            require(matched.has_value() && diagnostics.errors() == errors,
                    description + " did not have its unique expected derivation");
            const auto end = expected.front() + (fenced ? 3 : 1);
            require(matched->end == end && matched->value->input.size() == end,
                    description + " consumed the wrong endpoint");
            for (std::size_t at = 0; at < end; ++at)
                require(matched->value->input[at].text == input[at].text &&
                        matched->value->input[at].origin.identity == token_origin(input[at].location).identity,
                        description + " lost matched token spelling or identity");
        } else {
            require(!matched && diagnostics.errors() == errors + 1,
                    description + " unexpectedly selected a derivation");
            const auto output = messages.str();
            require((output.find("ambiguous syntax invocation") != std::string::npos) == (expected.size() > 1),
                    description + " disagreed with reference derivation multiplicity");
            require(output.find("budget") == std::string::npos && output.find("depth exceeded") == std::string::npos,
                    description + " exhausted resources instead of checking the language");
            if (expected.empty())
                require(output.find("syntax-match error") != std::string::npos ||
                        output.find("malformed syntax repetition after committed start") != std::string::npos ||
                        output.find("malformed syntax item after committed separator") != std::string::npos,
                        description + " failed for a reason other than an ordinary or committed mismatch");
            require(output.find("syntax 'Owner' defined here") != std::string::npos,
                    description + " omitted owner provenance");
        }
    }
};

unsigned enumerate(Fixture& fixture, const Reference& reference, bool fenced) {
    // Exhaust every word of length 0..5, including malformed continuations.
    // Check the empty word separately so nullable EOF derivations participate.
    std::vector<std::string> level{std::string{}};
    for (unsigned length = 0; length <= 5; ++length) {
        std::vector<std::string> next;
        for (const auto& word : level) {
            fixture.check(word, reference(word), fenced);
            if (length != 5)
                for (const char token : std::string_view("abc,")) next.push_back(word + token);
        }
        level = std::move(next);
    }
    return fixture.cases;
}

// A pair-list language has an elementary iterative recognizer independent of
// the production matcher's recursive candidate/commitment implementation.
Endpoints pair_list(std::string_view word, bool separated, bool require_one) {
    Endpoints result;
    if (!require_one) result.push_back(0);
    std::size_t at{};
    while (at < word.size()) {
        if (at && separated) {
            if (word[at] != ',') break;
            ++at;
            if (at == word.size() || word[at] != 'a') return {};
        } else if (word[at] != 'a') break;
        if (at + 1 >= word.size() || word[at + 1] != 'b') return {};
        at += 2;
        result.push_back(at);
    }
    return result;
}

unsigned languages(unsigned bits) {
    unsigned total{};
    {
        Fixture f("mutual", bits, R"(
syntax A : rule { match branch:choice(stop:("c") | next:("a" rule(B))); }
syntax B : rule { match branch:choice(stop:("c") | next:("b" rule(A))); }
)", "\"(\" rule(A) \")\"");
        total += enumerate(f, [](std::string_view word) -> Endpoints {
            if (word.empty() || word.back() != 'c') return {};
            for (std::size_t at = 0; at + 1 < word.size(); ++at)
                if (word[at] != (at % 2 ? 'b' : 'a')) return {};
            return {word.size()};
        }, true);
    }
    {
        Fixture f("recursive_multiplicity", bits, R"(
syntax R : rule { match branch:choice(stop:("c") | left:("a" rule(R)) | right:("a" rule(R))); }
)", "\"(\" rule(R) \")\"");
        total += enumerate(f, [](std::string_view word) -> Endpoints {
            if (word.empty() || word.back() != 'c') return {};
            for (std::size_t at = 0; at + 1 < word.size(); ++at)
                if (word[at] != 'a') return {};
            return Endpoints(std::size_t{1} << (word.size() - 1), word.size());
        }, true);
    }
    {
        Fixture f("nullable_multiplicity", bits, {},
                  "\"(\" first:optional(\"a\") second:optional(\"a\") \")\"");
        total += enumerate(f, [](std::string_view word) -> Endpoints {
            if (word.empty() || word == "aa") return {word.size()};
            if (word == "a") return {1, 1};
            return {};
        }, true);
    }
    for (const bool reverse : {false, true}) {
        Fixture f(reverse ? "prefixes_reverse" : "prefixes", bits, {}, reverse
            ? "branch:choice(long:(\"a\" \"b\") | short:(\"a\"))"
            : "branch:choice(short:(\"a\") | long:(\"a\" \"b\"))");
        total += enumerate(f, [](std::string_view word) -> Endpoints {
            if (word.empty() || word.front() != 'a') return {};
            if (word.size() > 1 && word[1] == 'b') return {1, 2};
            return {1};
        }, false);
    }
    for (const bool separated : {false, true}) {
        for (const bool require_one : {false, true}) {
            const std::string kind = std::string(separated ? "separated" : "repeat") + (require_one ? "1" : "0");
            Fixture f(kind, bits, {}, "parts:" + kind + "(\"a\" \"b\"" + (separated ? ", \",\")" : ")"));
            total += enumerate(f, [=](std::string_view word) {
                return pair_list(word, separated, require_one);
            }, false);
        }
    }
    return total;
}
} // namespace

int main() {
    unsigned total{};
    for (const unsigned bits : {32u, 64u}) total += languages(bits);
    std::cout << total << " bounded syntax/reference comparisons passed\n";
}
