#pragma once

// The location string of a diagnostic, "file:line:column@function".  The
// parser must survive any text, and the file and function it returns are
// views into the text it was given.
//
// parse_source_position of foundation/diag/JsonEmitter.h takes a
// std::string_view, so the harness gives it a std::string copy of the input.
// The copy goes when the emitter takes a TextView.

#include "../harness.h"

#include <foundation/diag/JsonEmitter.h>

#include <string>
#include <string_view>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline SeedList seeds_source_position() noexcept {
    SeedList seeds = empty_seed_list();
    add_seed(seeds, text_seed("src/x.cpp:10:5@void f()"));
    add_seed(seeds, text_seed("a.h:1:1@"));
    add_seed(seeds, text_seed(":::@@"));
    return seeds;
}

inline void run_source_position(::fixy::View<std::byte const> bytes) noexcept {
    std::string copy{};
    for (const std::byte byte : bytes) {
        copy.push_back(static_cast<char>(byte));
    }
    const std::string_view text{copy};
    const auto position = ::foundation::diag::parse_source_position(text);
    const auto inside = [text](std::string_view part) {
        return part.empty() || (part.data() >= text.data() && part.data() + part.size() <= text.data() + text.size());
    };
    CRUCIBLE_FUZZ_CLAIM("source_position", inside(position.file));
    CRUCIBLE_FUZZ_CLAIM("source_position", inside(position.function));
}

}  // namespace crucible::fuzz::boundary
