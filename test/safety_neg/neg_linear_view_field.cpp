// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: storing a LinearScopedView<C, T> (= Linear<ScopedView<C, T>>)
// as a struct field.  Proves the Tier 2 reflection audit sees THROUGH
// Linear<> and still catches the nested ScopedView — the audit's
// recursive class-field walk reaches Linear's `value_` member and
// observes it is a ScopedView, firing the same diagnostic.

#include <crucible/safety/ScopedView.h>

struct Carrier { int v = 0; };
struct Tag     {};

constexpr bool view_ok(Carrier const&, std::type_identity<Tag>) noexcept {
    return true;
}

using LinearView = crucible::safety::LinearScopedView<Carrier, Tag>;

// Storing a linear view as a field: forbidden just like the unwrapped
// ScopedView case — composition does not evade the audit.
struct Bad {
    LinearView v{crucible::safety::mint_linear_view<Tag>(Carrier{})};
};

// Audit opt-in — static_assert fires here.
static_assert(crucible::safety::no_scoped_view_field_check<Bad>());

int main() { return 0; }
