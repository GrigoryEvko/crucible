// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling mint_affine<T>(args...) with args that cannot
// construct T.  The requires-clause
// `std::is_constructible_v<T, Args...>` MUST reject the call.
//
// HS14 mint-pattern gate per CLAUDE.md §XXI: every mint factory's
// requires-clause is the SINGLE load-bearing soundness check.  A
// requires-clause without a witness that it fires is just a comment;
// this fixture forces compilation to fail when T's ctor universe
// excludes the supplied args, proving the gate is wired.
//
// Concrete bug-class this catches: a refactor that loosened
// mint_affine's requires-clause — e.g. dropped it altogether, or
// replaced it with `true` — would silently accept any args, deferring
// the ctor error to a SFINAE-failure deep inside the Graded substrate
// where the diagnostic is opaque.  This fixture pins the rejection
// AT the mint boundary, matching the §XXI promise that
// `grep "mint_affine"` finds every cross-tier composition where the
// constructibility constraint fires.
//
// Pairs with neg_affine_wrap_permission.cpp for the 2-fixture
// HS14 floor — one fixture per distinct mismatch class:
//   1. mint-wrong-arg:   substrate-side constructibility gate (this).
//   2. wrap-permission:  Permission-family rejection table.
//
// [GCC-WRAPPER-TEXT] — requires-clause rejection on mint_affine.

#include <crucible/safety/Affine.h>

using namespace crucible::safety;

// A class T whose only constructor takes an `int` — definitely not
// constructible from `const char*`.
struct OnlyIntCtor {
    int value;
    constexpr explicit OnlyIntCtor(int v) noexcept : value{v} {}
};

int main() {
    // Should FAIL: mint_affine<OnlyIntCtor> forwarded with a `const
    // char*` arg cannot construct OnlyIntCtor — the requires-clause
    // `std::is_constructible_v<T, Args...>` is FALSE.
    auto bad = mint_affine<OnlyIntCtor>("not_an_integer");
    return bad.peek().value;
}
