#pragma once

// The location string of a diagnostic, "file:line:column@function".  The
// parser must survive any text, and the file and function it returns are
// windows of the text it was given.  An empty part can be the empty text
// with no address, so the claim takes an empty part as a window.

#include "../harness.h"

#include <foundation/diag/JsonEmitter.h>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline SeedList seeds_source_position() noexcept {
    SeedList seeds = empty_seed_list();
    add_seed(seeds, text_seed("src/x.cpp:10:5@void f()"));
    add_seed(seeds, text_seed("a.h:1:1@"));
    add_seed(seeds, text_seed(":::@@"));
    return seeds;
}

inline void run_source_position(::fixy::View<std::byte const> bytes) noexcept {
    ::fixy::HeapText const copy = text_of(bytes);
    ::fixy::TextView const text = copy.view();
    ::foundation::diag::SourcePosition const position = ::foundation::diag::parse_source_position(text);
    auto const is_inside = [text](::fixy::TextView part) noexcept {
        return part.size() == 0 || ::fixy::is_window_of(::fixy::WindowPartText{part}, ::fixy::WindowWholeText{text});
    };
    CRUCIBLE_FUZZ_CLAIM("source_position", is_inside(position.file));
    CRUCIBLE_FUZZ_CLAIM("source_position", is_inside(position.function));
}

}  // namespace crucible::fuzz::boundary
