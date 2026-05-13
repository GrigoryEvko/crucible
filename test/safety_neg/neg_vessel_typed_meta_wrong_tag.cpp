// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a `Tagged<const TensorMeta*, source::External>`
// (or any tag other than `source::ABIBoundary`) to
// `crucible::vessel::metas_from_typed()`, which accepts only
// `TypedMeta = Tagged<const TensorMeta*, ABIBoundary>`.
//
// Tagged<T, Tag> is parameterized by Tag — different tags produce
// different class instantiations with no implicit conversion between
// them.  A refactor that loosens the helper to accept
// `Tagged<const TensorMeta*, AnyTag>` would let provenance from raw
// FFI bytes (External) silently masquerade as the validated
// ABIBoundary provenance the dispatch path expects.  This fixture
// witnesses that the type system rejects the laundering attempt.
//
// Pairs with neg_vessel_typed_handle_wrong_tag.cpp — the two together
// pin both directions of the typed Vessel ABI helper API.
//
// [GCC-WRAPPER-TEXT] — function-argument type-mismatch rejection.

#include "../../vessel/torch/vessel_api_typed.h"

#include <crucible/ir001/TensorMeta.h>
#include <crucible/safety/Tagged.h>

using crucible::TensorMeta;
using crucible::safety::Tagged;
using crucible::safety::source::External;
using crucible::vessel::metas_from_typed;

int main() {
    // Construct a `Tagged<const TensorMeta*, External>` — same payload
    // type as TypedMeta but a DIFFERENT tag, so a different type
    // entirely.
    Tagged<const TensorMeta*, External> wrong_tag{nullptr};

    // Should FAIL: metas_from_typed expects TypedMeta
    // (Tagged<const TensorMeta*, ABIBoundary>), not
    // Tagged<const TensorMeta*, External>.  No converting constructor /
    // no implicit retag exists.
    return metas_from_typed(wrong_tag) == nullptr ? 0 : 1;
}
