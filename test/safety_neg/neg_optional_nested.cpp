// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: nesting a ScopedView inside an optional inside a struct
// field.  Tier 2 audit recurses into optional via sv_unwrap_single
// and catches the violation even through one layer of indirection.

#include <crucible/safety/ScopedView.h>

#include <optional>

struct Carrier { int v = 0; };
struct Tag     {};

constexpr bool view_ok(Carrier const&, std::type_identity<Tag>) noexcept {
    return true;
}

using View = crucible::safety::ScopedView<Carrier, Tag>;

// Audit must recurse into std::optional and find the View.
struct Bad {
    std::optional<View> maybe_view;  // escape via optional wrapper
};

static_assert(crucible::safety::no_scoped_view_field_check<Bad>());

int main() { return 0; }
