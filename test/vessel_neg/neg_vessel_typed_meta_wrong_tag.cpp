// Negative-compile test.  This file must fail to compile.
//
// Violation: passing a `Tagged<const CrucibleMeta*, source::External>`
// (or any tag other than `source::ABIBoundary`) to
// `crucible::vessel::metas_from_typed()`, which accepts only
// `TypedMeta = Tagged<const CrucibleMeta*, ABIBoundary>`.
//
// Tagged<T, Tag> is parameterized by Tag — different tags produce
// different class instantiations with no implicit conversion between
// them.  A refactor that loosens the helper to accept
// `Tagged<const CrucibleMeta*, AnyTag>` would let provenance from raw
// FFI bytes (External) silently masquerade as the ABIBoundary
// provenance the dispatch path expects.  This fixture shows that the
// type system refuses the laundering attempt.  The two types differ in
// the tag alone: both tags are in fixy::tags::source, and the payload is
// const CrucibleMeta* on each side.  The registration regex names the two
// tags, so only the refusal of the External-to-ABIBoundary conversion
// satisfies it.
//
// Pairs with neg_vessel_typed_handle_wrong_tag.cpp — the two together
// pin both directions of the typed Vessel ABI helper API.
//
// [GCC-WRAPPER-TEXT] — function-argument type-mismatch rejection.

#include "../../vessel/torch/vessel_api_typed.h"

#include <fixy/Tagged.h>
#include <fixy/Tags.h>

using crucible::vessel::metas_from_typed;

int main() {
    // Build a `Tagged<const CrucibleMeta*, External>` through the one door
    // — same payload type as TypedMeta but a different tag, so a
    // different type entirely.
    auto wrong_tag = ::fixy::mint_tagged<::fixy::tags::source::External>(static_cast<const CrucibleMeta*>(nullptr));

    // Should fail: metas_from_typed expects TypedMeta
    // (Tagged<const CrucibleMeta*, ABIBoundary>), not
    // Tagged<const CrucibleMeta*, External>.  No converting constructor /
    // no implicit retag exists.
    return metas_from_typed(wrong_tag) == nullptr ? 0 : 1;
}
