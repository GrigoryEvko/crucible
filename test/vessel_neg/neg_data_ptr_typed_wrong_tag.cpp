// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning the result of `data_ptr_typed(...)`, which is
// `::fixy::Tagged<void*, source::External>`, into a slot of
// `::fixy::Tagged<void*, source::Sanitized>`.  Two tags make two
// distinct classes of the one template, and no conversion joins them.
//
// The accessor carries EXTERNAL provenance to the consumers downstream.
// A refactor that widened the source tag, or that retagged on
// assignment, would let raw FFI bytes pass as Sanitized memory that the
// dispatcher trusts.  This fixture shows that the type system refuses
// that laundering.  Both tags come from the ::fixy tree, so the refusal
// is the tag mismatch and not a mismatch between the two trees.
//
// [GCC-WRAPPER-TEXT] — function-argument type-mismatch rejection.

#include "../../vessel/torch/vessel_api_typed.h"

#include <fixy/Tagged.h>

int main() {
    // Build a typed-meta view from a single-element array (n_metas=1
    // satisfies the (non-null, n>0) ABI plausibility check).
    CrucibleMeta arr[1]{};
    auto typed = crucible::vessel::as_meta_typed(arr, 1);

    // data_ptr_typed returns ::fixy::Tagged<void*, source::External>.
    // Should FAIL: the target ::fixy::Tagged<void*, source::Sanitized>
    // is a DIFFERENT class; only retag along the catalog edge reaches it.
    ::fixy::Tagged<void*, ::fixy::tags::source::Sanitized> wrong = crucible::vessel::data_ptr_typed(typed, 0);
    return wrong.value() == nullptr ? 0 : 1;
}
