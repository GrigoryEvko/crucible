// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: storing ScopedViews in a std::vector.  Once A is popped
// or vector resizes, other stored views may still reference carriers
// that have transitioned out of state.  Tier 2 audit catches this via
// the sv_unwrap_single<vector> recursion.

#include <crucible/safety/ScopedView.h>

#include <vector>

struct Carrier { int v = 0; };
struct Tag     {};

constexpr bool view_ok(Carrier const&, std::type_identity<Tag>) noexcept {
    return true;
}

using View = crucible::safety::ScopedView<Carrier, Tag>;

// Container of Views: forbidden.
struct Bad {
    std::vector<View> views;  // escape vector
};

static_assert(crucible::safety::no_scoped_view_field_check<Bad>());

int main() { return 0; }
