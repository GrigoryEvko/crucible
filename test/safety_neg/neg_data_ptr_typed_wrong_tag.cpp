// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning the result of `data_ptr_typed(...)`, which is
// `Tagged<void*, source::External>`, into a slot expecting
// `Tagged<void*, source::Sanitized>`.  Different tags produce
// different `Tagged<>` instantiations with no implicit conversion
// between them.
//
// The point of GAPS-096's data_ptr_typed accessor is to thread
// EXTERNAL provenance into downstream consumers.  A future refactor
// that loosens the helper to widen the source tag (or worse,
// retag::declassify-on-assignment) would let raw FFI bytes silently
// masquerade as Sanitized memory the dispatcher trusts.  This fixture
// witnesses that the type system rejects the laundering attempt.
//
// [GCC-WRAPPER-TEXT] — function-argument type-mismatch rejection.

#include "../../vessel/torch/vessel_api_typed.h"

#include <crucible/safety/Tagged.h>

using crucible::safety::Tagged;
using crucible::safety::source::Sanitized;

int main() {
    // Build a typed-meta view from a single-element array (n_metas=1
    // satisfies the (non-null, n>0) ABI plausibility check).
    CrucibleMeta arr[1]{};
    auto typed = crucible::vessel::as_meta_typed(arr, 1);

    // data_ptr_typed returns Tagged<void*, source::External>.
    // Should FAIL: the conversion target Tagged<void*, source::Sanitized>
    // is a DIFFERENT class instantiation; no implicit retag exists.
    Tagged<void*, Sanitized> wrong = crucible::vessel::data_ptr_typed(typed, 0);
    return wrong.value() == nullptr ? 0 : 1;
}
