// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling mint_witness<Tier, T>(args...) with args that
// cannot construct T.  The requires-clause
// `std::is_constructible_v<T, Args...>` MUST reject the call.
//
// HS14 mint-pattern gate per CLAUDE.md §XXI: every mint factory's
// requires-clause is the SINGLE load-bearing soundness check.  A
// requires-clause without a witness that it fires is just a comment;
// this fixture forces compilation to fail when T's ctor universe
// excludes the supplied args, proving the gate is wired.
//
// Concrete bug-class this catches: a refactor that loosened
// mint_witness's requires-clause — e.g. dropped it altogether, or
// replaced it with `true` — would silently accept any args, deferring
// the ctor error to a SFINAE-failure deep inside the Graded
// substrate where the diagnostic is opaque.  This fixture pins the
// rejection AT the mint boundary, matching the §XXI promise that
// `grep "mint_witness"` finds every cross-tier composition where
// the constructibility constraint fires.
//
// Pairs with neg_witness_relax_to_stronger.cpp for the 2-fixture
// HS14 floor — one fixture per distinct mismatch class:
//   1. relax-to-stronger: substrate-tier subsumption violation.
//   2. mint-wrong-arg:    substrate-side constructibility gate (this).
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on mint_witness.

#include <crucible/safety/Witness.h>

using namespace crucible::safety;

// A class T whose only constructor takes an `int` — definitely not
// constructible from `const char*`.
struct OnlyIntCtor {
    int value;
    constexpr explicit OnlyIntCtor(int v) noexcept : value{v} {}
};

int main() {
    // Should FAIL: mint_witness<FORMALLY_VERIFIED, OnlyIntCtor>
    // forwarded with a `const char*` arg cannot construct
    // OnlyIntCtor — the requires-clause `std::is_constructible_v<T,
    // Args...>` is FALSE.
    auto bad = mint_witness<Witness_v::FORMALLY_VERIFIED, OnlyIntCtor>(
        "not_an_integer");
    return bad.peek().value;
}
