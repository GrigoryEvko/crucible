// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #4 of 7 for safety/Pre.h + safety/Post.h.
//
// Premise: CRUCIBLE_PRE on a function taking a STRUCT BY CONST-VALUE
// (`S const s`, copy-but-immutable) must fire at consteval when the
// predicate is violated.  Probe Shape #3 — distinct from #2 (const-ref)
// because the parameter is copied into the function's frame, and from
// #4 (mutable by-value) because the local copy is const-qualified.
//
// Distinct mismatch class: by-value with const-qualifier on the
// parameter binding.  Production sites use this shape when they want
// (a) value semantics for predictable lifetimes, AND (b) the local
// copy to be immutable as a discipline against accidental mutation
// in the body.  CogMimic constructors and many recipe-validation
// paths follow this pattern.
//
// Expected diagnostic: "non-constant condition for static assertion".

#include <crucible/safety/Pre.h>

#include <cstdint>

namespace {

struct S {
    std::uint64_t lo = 0;
    [[nodiscard]] constexpr bool nz() const noexcept { return lo != 0; }
};

[[nodiscard]] constexpr std::uint64_t pre_cval(S const s) noexcept {
    CRUCIBLE_PRE(s.nz());
    return s.lo;
}

constexpr S ZERO{};

static_assert(pre_cval(ZERO) == 0,
    "CRUCIBLE_PRE on a struct const-by-value parameter MUST fire at "
    "consteval when the predicate is violated.  If this static_assert "
    "ever evaluates successfully, Pre.h's consteval enforcement is "
    "broken for value-with-const-binding shapes (Probe Shape #3).");

}  // namespace

int main() { return 0; }
