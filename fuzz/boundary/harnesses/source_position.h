#pragma once

// The location string of a diagnostic, "file:line:column@function".  The
// parser must survive any text, and the file and function it returns are
// views into the text it was given.

#include "../harness.h"

#include <foundation/diag/JsonEmitter.h>

namespace crucible::fuzz::boundary {

[[nodiscard]] inline Seeds seeds_source_position() {
    return {text_bytes("src/x.cpp:10:5@void f()"), text_bytes("a.h:1:1@"), text_bytes(":::@@")};
}

inline void run_source_position(std::span<const std::uint8_t> bytes) {
    const std::string_view text = as_text(bytes);
    const auto position = ::foundation::diag::parse_source_position(text);
    const auto inside = [text](std::string_view part) {
        return part.empty() || (part.data() >= text.data() && part.data() + part.size() <= text.data() + text.size());
    };
    CRUCIBLE_FUZZ_CLAIM("source_position", inside(position.file));
    CRUCIBLE_FUZZ_CLAIM("source_position", inside(position.function));
}

}  // namespace crucible::fuzz::boundary
