// Negative-compile test.  This file must fail to compile.
//
// Violation: the result of `data_ptr_typed(...)`, the key of the data
// pointer of a record, goes into a pointer under the tag Sanitized.  The key
// gives no pointer back, and no conversion makes a tagged pointer from it, so
// raw bytes of a foreign caller cannot pass as Sanitized memory that the
// dispatcher trusts.  Only the members of TensorMeta re-mint the address of
// the key.

#include "../../vessel/torch/vessel_api_typed.h"

#include <fixy/Core.h>
#include <fixy/Tagged.h>

int main() {
    // Build a typed-meta view from a single-element array.  The door admits
    // an aligned array with a count above 0.
    ::fixy::FixedArray<CrucibleMeta, 1> metas{};
    auto typed = crucible::vessel::as_meta_typed(&metas.at(::fixy::index<0>()), 1).expect("the door admits one record");

    ::fixy::Tagged<void*, ::fixy::tags::source::Sanitized> wrong = crucible::vessel::data_ptr_typed(typed, 0);
    return wrong.value() == nullptr ? 0 : 1;
}
