// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `Tagged<Vigil*, source::External>` (or any tag
// other than `source::ABIBoundary`) to `crucible::vessel::from_typed()`,
// which is declared to accept `TypedHandle = Tagged<Vigil*, ABIBoundary>`
// only.
//
// Tagged<T, Tag> is parameterized by Tag — different tags produce
// different class instantiations, with no implicit conversion between
// them.  This fixture witnesses that the type system rejects the
// wrong-direction flow at the C-ABI boundary helper.
//
// Concrete bug-class this catches: a refactor that "promotes" the
// helper to accept any Tagged<Vigil*, AnyTag> would let provenance
// from an unrelated call site (e.g., `External` for raw FFI bytes)
// silently flow into the Vigil dispatch path that requires
// `ABIBoundary` provenance.
//
// [GCC-WRAPPER-TEXT] — function-argument type-mismatch rejection.

#include "../../vessel/torch/vessel_api_typed.h"

#include <crucible/safety/Tagged.h>

using crucible::safety::Tagged;
using crucible::safety::source::External;
using crucible::vessel::from_typed;

int main() {
    // Construct a `Tagged<Vigil*, External>` — same payload type as
    // TypedHandle but a DIFFERENT tag, so a different type entirely.
    Tagged<crucible::Vigil*, External> wrong_tag{nullptr};

    // Should FAIL: from_typed expects TypedHandle (Tagged<Vigil*,
    // ABIBoundary>), not Tagged<Vigil*, External>.  No converting
    // constructor / no implicit retag exists.
    return from_typed(wrong_tag) == nullptr ? 0 : 1;
}
