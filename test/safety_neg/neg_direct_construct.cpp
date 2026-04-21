// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: calling the explicit ScopedView(Carrier&) constructor
// directly from user code, bypassing mint_view<>.  Tier 1 enforcement
// makes that ctor private and only friends mint_view<>, so access
// control rejects this call with a "private" diagnostic — distinct
// from the "no default ctor" path covered by neg_default_construct.

#include <crucible/safety/ScopedView.h>

struct Carrier { int v = 0; };
struct Tag     {};

constexpr bool view_ok(Carrier const&, std::type_identity<Tag>) noexcept {
    return true;
}

using View = crucible::safety::ScopedView<Carrier, Tag>;

int main() {
    Carrier c;
    // Private explicit ctor — GCC rejects with an access-control error,
    // NOT a "no matching function" error.  This proves that even when
    // the caller knows the ctor's signature, they can't invoke it
    // without going through mint_view<>.
    View v{c};
    (void)v;
    return 0;
}
