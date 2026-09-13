#pragma once

// The detector's two phases as types, so a caller can carry proof of which
// one a detector is in rather than re-testing.
//
// The phases are exclusive and exhaustive: the signature is either still
// being collected or complete, and reset() returns a detector to the first.
//
// The tags live here rather than beside the detector itself because the view
// machinery they need pulls in reflection and several containers, while the
// detector sits next to a hot loop and keeps a slim include set. A caller
// that wants the proof pays for that footprint. Everyone else does not.

#include <crucible/IterationDetector.h>
#include <crucible/safety/ScopedView.h>

#include <type_traits>

namespace crucible {

// The tags sit in their own namespace so that another carrier may name its
// own phases the same way without colliding.
namespace iter_det_state {

// Still collecting the signature. A fresh detector and one just reset are
// both in this phase.
struct Building {};

// The signature is complete and the matcher is running. Only a reset returns
// a detector from here to the other phase.
struct Steady {};

}  // namespace iter_det_state

// Found by argument-dependent lookup from the view-minting template, which
// calls one of these as its precondition. Both are constexpr, so minting the
// wrong view during constant evaluation is ill-formed rather than merely
// checked at run time.

[[nodiscard]] constexpr bool view_ok(IterationDetector const& detector,
                                     std::type_identity<iter_det_state::Building>) noexcept {
    return detector.signature_len.get() < IterationDetector::K;
}

[[nodiscard]] constexpr bool view_ok(IterationDetector const& detector,
                                     std::type_identity<iter_det_state::Steady>) noexcept {
    return detector.signature_len.get() == IterationDetector::K;
}

static_assert(::crucible::safety::no_scoped_view_field_check<IterationDetector>(),
              "IterationDetector must not contain a ScopedView field. A view is a non-owning witness bounded "
              "by its carrier's lifetime, and storing one as a member defeats that bound.");

}  // namespace crucible
