// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #6 of 7 for safety/Pre.h + safety/Post.h.
//
// Premise: CRUCIBLE_PRE on a function taking a STRUCT CONST-POINTER
// must fire at consteval when the predicate is violated.  Probe
// Shape #5 — interestingly, this is the ONE shape where P2900 native
// `pre()` worked even on the un-patched build (because the predicate
// `s != nullptr && s->nz()` includes a literal pointer-equality
// comparison that the consteval evaluator handled correctly).  We
// pin the same shape for CRUCIBLE_PRE to preserve cross-shape
// uniformity: every shape, same behavior.
//
// Distinct mismatch class: pointer indirection with explicit non-null
// check in the predicate.  Production sites use this shape for FFI
// boundaries and arena-allocated handles where the caller may pass
// nullptr but the function body assumes non-null + valid state.
//
// The fixture violates the second conjunct (`s->nz()`) by passing a
// pointer to a default-constructed S where `lo == 0`; the first
// conjunct (`s != nullptr`) is satisfied.  This pins that the macro
// fires on partial-predicate violation, not just trivial nullptr.
//
// Expected diagnostic: "non-constant condition for static assertion".

#include <crucible/safety/Pre.h>

#include <cstdint>

namespace {

struct S {
    std::uint64_t lo = 0;
    [[nodiscard]] constexpr bool nz() const noexcept { return lo != 0; }
};

[[nodiscard]] constexpr std::uint64_t pre_cptr(S const* s) noexcept {
    CRUCIBLE_PRE(s != nullptr && s->nz());
    return s->lo;
}

constexpr S ZERO{};

// Non-null but predicate violates the second conjunct: this is the
// load-bearing distinction from "null pointer" — proves the macro
// fires on the FULL predicate, not just on the non-null short-circuit.
static_assert(pre_cptr(&ZERO) == 0,
    "CRUCIBLE_PRE on a struct const-pointer with conjunctive predicate "
    "MUST fire at consteval when the SECOND conjunct (the data check) "
    "is violated, not just when the first (non-null) is.  If this "
    "static_assert ever evaluates successfully, Pre.h's consteval "
    "enforcement is broken for compound-predicate shapes.");

}  // namespace

int main() { return 0; }
