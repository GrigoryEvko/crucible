#pragma once

// The one file under fuzz/ that names the old substrate.
//
// The region, branch, Cipher and federation boundaries take capability,
// context and identity types of the old tree.  This header builds those
// values, so the harnesses call the boundaries and name none of the old
// tree.  When a boundary flips to include/foundation and include/fixy, its
// part of this header goes, and scripts/flip-list.txt drops the header
// when nothing is left in it.

#include <crucible/Cipher.h>
#include <crucible/effects/_Capabilities.h>

#include <cstdint>
#include <string>

namespace crucible::fuzz::boundary::old_tree {

// The allocation capability of a test context, for an arena and a loader.
[[nodiscard]] inline auto test_alloc() noexcept { return effects::testing::test().alloc; }

// The atom count of this build's effect universe, which is what a receiver
// states when it reads a federation entry.
inline constexpr std::uint16_t kReceiverAtomCount = static_cast<std::uint16_t>(effects::OsUniverse::cardinality);

[[nodiscard]] inline Cipher open_cipher(const std::string& root) {
    return Cipher::open(fixy::wrap::Path<fixy::tags::source::External>{root});
}

[[nodiscard]] inline Cipher::OpenView open_view(Cipher& cipher) {
    return cipher.mint_open_view(effects::TestRunnerCtx{effects::testing::test()});
}

[[nodiscard]] constexpr auto session_tag(std::uint64_t raw) noexcept { return fixy::sess::eventlog::SessionTagId{raw}; }
[[nodiscard]] constexpr auto step(std::uint64_t raw) noexcept { return fixy::sess::eventlog::StepId{raw}; }

}  // namespace crucible::fuzz::boundary::old_tree
