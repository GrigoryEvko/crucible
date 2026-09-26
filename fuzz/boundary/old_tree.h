#pragma once

// The one file under fuzz/ that names the old substrate.
//
// The Cipher boundary still takes context and identity types of the old
// tree.  This header builds those values, so the Cipher harness calls the
// boundary and names none of the old tree.  When Cipher flips to
// include/foundation and include/fixy, this header goes, and
// scripts/flip-list.txt drops it.

#include <crucible/Cipher.h>
#include <crucible/effects/_Capabilities.h>

#include <cstdint>
#include <string>

namespace crucible::fuzz::boundary::old_tree {

[[nodiscard]] inline Cipher open_cipher(const std::string& root) {
    return Cipher::open(fixy::wrap::Path<fixy::tags::source::External>{root});
}

[[nodiscard]] inline Cipher::OpenView open_view(Cipher& cipher) {
    return cipher.mint_open_view(effects::TestRunnerCtx{effects::testing::test()});
}

[[nodiscard]] constexpr auto session_tag(std::uint64_t raw) noexcept { return fixy::sess::eventlog::SessionTagId{raw}; }
[[nodiscard]] constexpr auto step(std::uint64_t raw) noexcept { return fixy::sess::eventlog::StepId{raw}; }

}  // namespace crucible::fuzz::boundary::old_tree
