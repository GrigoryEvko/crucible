// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #5 of 7 for safety/Pre.h + safety/Post.h.
//
// Premise: CRUCIBLE_PRE on a function taking a STRUCT BY (mutable)
// VALUE must fire at consteval when the predicate is violated.
// Probe Shape #4 — distinct from #3 (const-by-value) because the
// local parameter is mutable; the function may rewrite `s` in its
// body before reading it again.  The pre-clause runs at function
// entry, before any body-level mutation, but P2900's parser-level
// clause binding has historically had subtle issues with mutable
// by-value parameters.
//
// Distinct mismatch class: mutable by-value parameter.  Production
// sites use this shape when the function's body needs to normalize,
// canonicalize, or otherwise rewrite the input — common in IR-level
// transformations and serialization round-trips.
//
// Expected diagnostic: "non-constant condition for static assertion".

#include <crucible/safety/Pre.h>

#include <cstdint>

namespace {

struct S {
    std::uint64_t lo = 0;
    [[nodiscard]] constexpr bool nz() const noexcept { return lo != 0; }
};

[[nodiscard]] constexpr std::uint64_t pre_byval(S s) noexcept {
    CRUCIBLE_PRE(s.nz());
    return s.lo;
}

constexpr S ZERO{};

static_assert(pre_byval(ZERO) == 0,
    "CRUCIBLE_PRE on a mutable struct by-value parameter MUST fire at "
    "consteval when the predicate is violated.  If this static_assert "
    "ever evaluates successfully, Pre.h's consteval enforcement is "
    "broken for mutable-by-value shapes (Probe Shape #4) — the most "
    "common shape for IR-level transformation entry points.");

}  // namespace

int main() { return 0; }
