// Copyright (C) 2026 Cross contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "frontend/embed.hpp"
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

int main(int argc, char** argv) {
    using namespace cross;
    require(argc == 2, "expected an existing asset path");
    const std::filesystem::path asset_path(argv[1]);
    const auto source_path = asset_path.parent_path() / "embed-identity.x";
    CompilerOptions options;
    const LayoutQuery no_layout = [](const TypePtr&) -> std::optional<std::uint64_t> {
        return std::nullopt;
    };

    {
        SourceManager sources;
        std::ostringstream output;
        Diagnostics diagnostics(output);
        const auto* source = sources.add(source_path, R"(
[[macro]] static $::meta::tokens duplicate(in $::meta::tokens input) {
    return $::meta::concat(input, input);
}
duplicate! { $::embed("preprocessor.x") }
)");
        const auto discovery = discover_embeds(sources, *source, {}, options,
                                                diagnostics, true);
        require(diagnostics.errors() == 0, output.str().c_str());
        require(discovery.dependencies.size() == 1, "one discovered asset expected");
        const auto* expanded = expand_procedural_macros(
            sources, *discovery.source, diagnostics, 64, no_layout, no_layout);
        require(diagnostics.errors() == 0, output.str().c_str());
        require(validate_embeds(*expanded, diagnostics), output.str().c_str());
        const auto tokens = Lexer(*expanded, diagnostics).lex();
        std::shared_ptr<const EmbedIdentity> identity;
        unsigned copies{};
        for (std::size_t index = 0; index + 3 < tokens.size(); ++index) {
            if (!tokens[index].is("$::embed")) continue;
            const auto found = token_origin(tokens[index].location).embed;
            require(found != nullptr, "copied embed lost dependency identity");
            if (!identity) identity = found;
            require(identity == found, "copies changed the selected dependency");
            for (unsigned piece = 0; piece < 4; ++piece) {
                const auto origin = token_origin(tokens[index + piece].location);
                require(origin.embed == found && origin.embed_piece == piece,
                        "copied embed lost an expression piece");
            }
            ++copies;
        }
        require(copies == 2, "procedural token copying did not duplicate embed");
    }

    {
        SourceManager sources;
        std::ostringstream output;
        Diagnostics diagnostics(output);
        const auto* source = sources.add(source_path, R"(
[[macro]] static $::meta::tokens forge(in $::meta::tokens input) {
    return $::quote { $::embed("preprocessor.x") };
}
forge! {}
)");
        const auto discovery = discover_embeds(sources, *source, {}, options,
                                                diagnostics, true);
        require(discovery.dependencies.empty(), "quote template became an asset operation");
        const auto* expanded = expand_procedural_macros(
            sources, *discovery.source, diagnostics, 64, no_layout, no_layout);
        require(diagnostics.errors() == 0, output.str().c_str());
        require(!validate_embeds(*expanded, diagnostics),
                "newly constructed embed acquired an undeclared dependency");
        require(output.str().find("lacks its declared dependency identity") !=
                    std::string::npos,
                "forged embed diagnostic is missing");
    }
}
