// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/lexer.hpp"
#include "frontend/procedural.hpp"

#include <cstdlib>
#include <iostream>
#include <sstream>

namespace {
void require(bool condition, const char* message) {
    if (!condition) {
        std::cerr << message << '\n';
        std::abort();
    }
}
}

int main() {
    using namespace cross;
    SourceManager sources;
    std::ostringstream output;
    Diagnostics diagnostics(output);
    const LayoutQuery no_layout = [](const TypePtr&) -> std::optional<std::uint64_t> {
        return std::nullopt;
    };
    const std::string source = R"(
namespace definitions {
    [[macro]] static $::meta::tokens duplicate(in $::meta::tokens input) {
        return $::meta::concat(input, input);
    }
    [[macro]] static $::meta::tokens forward(in $::meta::tokens input) {
        return $::quote { duplicate! { $::unquote(input) } quoted };
    }
    [[macro]] static $::meta::tokens generated(in $::meta::tokens input) {
        $::meta::tokens tokens = $::quote { $::patch(3u32) };
        return $::meta::concat(tokens, tokens);
    }
    [[macro]] static $::meta::tokens parsed(in $::meta::tokens input) {
        return $::meta::parse("parsed_identifier");
    }
    [[macro]] static $::meta::tokens generated_forward(in $::meta::tokens input) {
        return $::quote { duplicate! { freshly_forwarded $::patch(5u32) } };
    }
    [[macro]] static $::meta::tokens separated(in $::meta::tokens input) {
        return $::meta::concat($::meta::parse("+"), $::meta::parse("+"));
    }
    [[macro]] static $::meta::tokens retarget(in $::meta::tokens input) {
        return $::meta::call_site(input);
    }
    [[macro]] static $::meta::tokens fresh(in $::meta::tokens input) {
        $::meta::tokens first = $::meta::gensym("private");
        $::meta::tokens second = $::meta::gensym("private");
        $::meta::tokens retargeted = $::meta::call_site(first);
        return $::meta::concat($::meta::concat(first, retargeted), second);
    }
}
namespace caller {
    [[macro]] static $::meta::tokens duplicate(in $::meta::tokens input) {
        return $::quote { wrong };
    }
    definitions::forward! { copied $::patch(1u32) }
    definitions::generated! {}
    definitions::generated! {}
    definitions::parsed! {}
    definitions::generated_forward! {}
    definitions::separated! {}
    definitions::retarget! { caller_identifier }
    definitions::fresh! {}
}
)";
    std::vector<TokenIdentity> first_unit;
    for (const unsigned width : {32U, 64U}) {
        const auto* expanded = expand_procedural_macros(
            sources, "tokens.x", source, diagnostics, width, no_layout, no_layout);
        require(diagnostics.errors() == 0, output.str().c_str());
        const auto tokens = Lexer(*expanded, diagnostics).lex();
        std::vector<TokenOrigin> copied;
        std::vector<TokenOrigin> patches;
        std::vector<TokenOrigin> quoted;
        std::vector<TokenOrigin> parsed;
        std::vector<TokenOrigin> forwarded;
        std::vector<TokenOrigin> retargeted;
        std::vector<TokenOrigin> fresh;
        unsigned plus_count{};
        for (const auto& token : tokens) {
            require(!token.is("wrong"), "quoted macro name rebound at invocation site");
            require(!token.is("++"), "concatenation fused adjacent punctuators");
            if (token.is("+")) ++plus_count;
            if (token.is("copied")) copied.push_back(token_origin(token.location));
            if (token.is("$::patch")) patches.push_back(token_origin(token.location));
            if (token.is("quoted")) quoted.push_back(token_origin(token.location));
            if (token.is("parsed_identifier")) parsed.push_back(token_origin(token.location));
            if (token.is("freshly_forwarded")) forwarded.push_back(token_origin(token.location));
            if (token.is("caller_identifier")) retargeted.push_back(token_origin(token.location));
            if (token.is("private")) fresh.push_back(token_origin(token.location));
            if (token.kind != TokenKind::End) {
                const auto* origin = expanded->token_origin_at(token.location.offset);
                require(origin != nullptr, "serialized token lost its origin");
                require(origin->identity.source_unit != nullptr, "token identity has no source unit");
            }
        }
        require(copied.size() == 2 && patches.size() == 8 && quoted.size() == 1 && parsed.size() == 1 &&
                forwarded.size() == 2 && retargeted.size() == 1 && fresh.size() == 3 && plus_count == 2,
                "unexpected expansion token counts");
        require(copied[0].identity == copied[1].identity, "copied token lost lexical identity");
        require(copied[0].context == copied[1].context, "copied token lost lookup context");
        require(copied[0].context && copied[0].context->kind == SyntaxContext::Kind::CallSite &&
                copied[0].context->name_space == "caller", "input lost its call-site context");
        require(copied[0].identity.expansion.value == 0, "input token was treated as newly constructed");
        require(copied[0].span.file->text.substr(copied[0].span.offset, 6) == "copied",
                "copied token did not preserve its supplied span");
        require(retargeted[0].identity.expansion.value == 0 &&
                retargeted[0].span.file->text.substr(retargeted[0].span.offset, 17) == "caller_identifier" &&
                retargeted[0].context &&
                retargeted[0].context->kind == SyntaxContext::Kind::CallSite &&
                retargeted[0].context->name_space == "caller",
                "call_site changed copied identifier identity/span or lost invocation context");
        require(fresh[0].fresh && fresh[0].fresh == fresh[1].fresh &&
                fresh[0].fresh != fresh[2].fresh &&
                fresh[0].fresh->prefix == "private" &&
                fresh[0].context && fresh[0].context->kind == SyntaxContext::Kind::DefinitionSite &&
                fresh[1].context && fresh[1].context->kind == SyntaxContext::Kind::CallSite &&
                fresh[1].context->name_space == "caller" &&
                fresh[0].span.file == fresh[1].span.file &&
                fresh[0].span.offset == fresh[1].span.offset,
                "gensym did not preserve opaque fresh identity across token copies");
        require(patches[0].identity == patches[1].identity, "copied patch token lost lexical identity");
        require(patches[2].identity != patches[3].identity, "constructed output positions are not distinct");
        require(patches[2].identity.expansion == patches[3].identity.expansion,
                "one expansion received inconsistent identity");
        require(patches[2].identity.expansion != patches[4].identity.expansion,
                "separate invocations share expansion identity");
        require(patches[6].identity == patches[7].identity,
                "forwarding a generated patch token lost its outer expansion identity");
        require(forwarded[0].identity == forwarded[1].identity &&
                forwarded[0].identity.expansion == patches[6].identity.expansion &&
                forwarded[0].context == forwarded[1].context &&
                forwarded[0].context->kind == SyntaxContext::Kind::DefinitionSite,
                "nested input capture changed constructed token identity or context");
        for (const auto& value : {quoted[0], parsed[0]}) {
            require(value.context && value.context->kind == SyntaxContext::Kind::DefinitionSite &&
                    value.context->name_space == "definitions", "constructed token lost definition context");
            require(value.identity.expansion.value != 0, "constructed token has no expansion identity");
            require(value.span.file->text.substr(value.span.offset, 11) == "definitions",
                    "constructed token did not default to its invocation span");
        }
        if (width == 32) {
            for (const auto& patch : patches) first_unit.push_back(patch.identity);
        } else {
            for (std::size_t index = 0; index < patches.size(); ++index)
                require(patches[index].identity != first_unit[index], "primary source units share token identity");
        }
    }
    require(diagnostics.errors() == 0, output.str().c_str());
}
