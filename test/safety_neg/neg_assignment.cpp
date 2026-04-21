// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning one ScopedView to another.  Tier 1 enforcement
// deletes operator= so that views cannot be late-bound — if reassignment
// were allowed, a view minted at state-A could be overwritten with one
// minted at state-B without a state-transition boundary in between.

#include <crucible/safety/ScopedView.h>

struct Carrier { int v = 0; };
struct Tag     {};

constexpr bool view_ok(Carrier const&, std::type_identity<Tag>) noexcept {
    return true;
}

int main() {
    Carrier c1{};
    Carrier c2{};
    auto v1 = crucible::safety::mint_view<Tag>(c1);
    auto v2 = crucible::safety::mint_view<Tag>(c2);

    // Tier 1 deleted-operator= fires here.
    v1 = v2;

    return 0;
}
