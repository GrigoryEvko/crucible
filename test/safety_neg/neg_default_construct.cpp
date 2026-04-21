// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: default-constructing a ScopedView without going through
// mint_view.  Tier 1 enforcement: constructor is private + only
// mint_view<> is friended.  No public default ctor exists.

#include <crucible/safety/ScopedView.h>

struct Carrier { int v = 0; };
struct Tag     {};

constexpr bool view_ok(Carrier const&, std::type_identity<Tag>) noexcept {
    return true;
}

using View = crucible::safety::ScopedView<Carrier, Tag>;

int main() {
    // Private ctor — this line must fail to compile.
    View v;
    (void)v;
    return 0;
}
