// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling mint_join_policy<Tier, T>(args...) with args
// that cannot construct T.  The requires-clause
// `std::is_constructible_v<T, Args...>` MUST reject the call.
//
// HS14 mint-pattern gate per CLAUDE.md §XXI: every mint factory's
// requires-clause is the SINGLE load-bearing soundness check.  A
// requires-clause without a witness that it fires is just a comment;
// this fixture forces compilation to fail when T's ctor universe
// excludes the supplied args, proving the gate is wired.
//
// Concrete bug-class this catches: a refactor that loosened
// mint_join_policy's requires-clause — e.g. dropped it altogether, or
// replaced it with `true` — would silently accept any args, deferring
// the ctor error to a SFINAE-failure deep inside the Graded substrate
// where the diagnostic is opaque.  This fixture pins the rejection
// AT the mint boundary, matching the §XXI promise that
// `grep "mint_join_policy"` finds every cross-tier composition where
// the constructibility constraint fires.
//
// HS14 #2 of 2 for V-079 — pairs with neg_join_policy_relax_to_stricter
// for the 2-fixture floor across distinct mismatch classes:
//   1. relax-to-stricter: substrate-tier subsumption violation.
//   2. mint-wrong-arg (this): substrate constructibility gate.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on mint_join_policy.

#include <crucible/safety/JoinPolicy.h>

using namespace crucible::safety;

// A class T whose only constructor takes an `int` — definitely not
// constructible from `const char*`.
struct OnlyIntCtor {
    int value;
    constexpr explicit OnlyIntCtor(int v) noexcept : value{v} {}
};

int main() {
    // Should FAIL: mint_join_policy<JOIN_ALL, OnlyIntCtor> forwarded
    // with a `const char*` arg cannot construct OnlyIntCtor — the
    // requires-clause `std::is_constructible_v<T, Args...>` is FALSE.
    auto bad = mint_join_policy<JoinPolicy_v::JOIN_ALL, OnlyIntCtor>(
        "not_an_integer");
    return bad.peek().value;
}
