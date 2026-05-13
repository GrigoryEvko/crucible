// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: a class type containing a safety::ScopedView<
// IterationDetector, ...> as a non-static data member — fires the
// Tier-2 reflection audit `no_scoped_view_field_check`.
//
// Per WRAP-IterDet-4 (#930) + safety/ScopedView.h Tier-2 discipline,
// ScopedView<C, T> is a non-owning lifetime-bounded witness; its
// only legitimate storage is the local stack frame that minted it.
// Storing the view in a struct field, std::optional, std::vector,
// std::array, std::pair, std::tuple, std::variant, std::unique_ptr,
// std::shared_ptr, std::weak_ptr, Linear<>, or a C array — all
// patterns that defeat the lifetime contract — fires the recursive
// reflection walk at compile time, naming the offending member type.
//
// The static_assert in IterationDetectorState.h locks the carrier
// itself (IterationDetector) against this pattern; this fixture
// verifies the audit is also live for INDEPENDENT carrier types
// that try to embed an iter_det view.
//
// Companion fixture to neg_iter_det_view_steady_on_building.cpp:
//   - This one is the structural Tier-2 lifetime check at FIELD AUDIT
//     time.  Catches a future regression where the audit's recursive
//     wrapper-unwrap (sv_unwrap_single / sv_pack_for) misses a new
//     container shape, OR where a refactor adds a "cached view" field
//     to a class.
//   - That one is the value-level boundary check at MINT TIME — pre
//     fires when view_ok rejects the asserted tag.
//
// Together they pin BOTH soundness gates of WRAP-IterDet-4.

#include <crucible/ir001/IterationDetectorState.h>

// A struct with a ScopedView field — the audit walks its members
// recursively and finds an iter_det Steady view.  No constructor is
// declared: the class is never instantiated, the audit only needs
// the layout (which reflection reads from the declaration).  An
// implicit default constructor is implicitly deleted because
// ScopedView's default ctor is deleted, but that's irrelevant
// here — we only trigger the file-scope static_assert below.
struct OffendingContainer {
    crucible::safety::ScopedView<
        crucible::IterationDetector,
        crucible::iter_det_state::Steady> view_;
};

// Trigger the audit at compile time.  The diagnostic identifies
// the wrapper type held in `view_` and the carrier
// `OffendingContainer` that violated the discipline.
static_assert(::crucible::safety::no_scoped_view_field_check<OffendingContainer>(),
    "WRAP-IterDet-4 Tier-2 audit must reject containers that "
    "store a ScopedView<IterationDetector, ...> as a field; this "
    "fixture exists so a future regression in contains_scoped_view's "
    "recursive walk is caught at compile time.");

int main() { return 0; }
