// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: storing a ScopedView as a struct field.  Tier 2 reflection
// audit catches this — the static_assert on no_scoped_view_field_check
// fires with a diagnostic naming the offending type.

#include <crucible/safety/ScopedView.h>

struct Carrier { int v = 0; };
struct Tag     {};

constexpr bool view_ok(Carrier const&, std::type_identity<Tag>) noexcept {
    return true;
}

using View = crucible::safety::ScopedView<Carrier, Tag>;

// Storing a View as a field: forbidden.
struct Bad {
    View v;  // escape vector
};

// Audit opt-in — static_assert fires here.
static_assert(crucible::safety::no_scoped_view_field_check<Bad>());

int main() { return 0; }
