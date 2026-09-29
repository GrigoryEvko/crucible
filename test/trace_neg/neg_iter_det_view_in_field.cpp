// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: a class that holds a ::fixy::ScopedView<IterationDetector, ...>
// as a non-static data member, which fires the reflection audit
// no_scoped_view_field_check.
//
// A ScopedView is a non-owning witness bounded by its carrier's lifetime,
// and its only legitimate storage is the stack frame that minted it.  A
// view stored in a field, an optional, a container or a smart pointer
// defeats that bound, and the recursive member walk names the offending
// member type at compile time.
//
// The static_assert in IterationDetectorState.h locks the carrier itself
// against this pattern.  This fixture checks that the audit also fires on
// an independent class that embeds a detector view.
//
// Companion fixture to neg_iter_det_view_steady_on_building.cpp:
//   - This one is the structural check at field-audit time.  It catches a
//     regression where the recursive unwrap misses a new container shape,
//     or a refactor that adds a cached view field.
//   - That one is the value-level check at mint time.

#include <crucible/IterationDetectorState.h>
#include <fixy/ScopedView.h>

// The class is never instantiated: the audit only reads the declaration.
struct OffendingContainer {
    ::fixy::ScopedView<crucible::IterationDetector, crucible::iter_det_state::Steady> view_;
};

static_assert(::fixy::no_scoped_view_field_check<OffendingContainer>(),
              "The audit must reject a class that stores a ScopedView<IterationDetector, ...> as a field.");

int main() { return 0; }
