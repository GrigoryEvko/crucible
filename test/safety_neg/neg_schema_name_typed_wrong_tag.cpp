// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: assigning the result of `schema_name_typed(...)`, which
// is `Tagged<const char*, source::Sanitized>`, into a slot expecting
// `Tagged<const char*, source::External>`.
//
// The Sanitized tag witnesses that the SchemaTable's interned name
// has been validated at registration (length-bounded, NUL-walked,
// prefix-stripped).  Confusing a Sanitized name with raw External
// FFI input would let visualization / diagnostic / exporter code
// treat unsanitized bytes as if they had passed the gate.  This
// fixture pins the type system rejecting the confusion.
//
// Pairs with neg_data_ptr_typed_wrong_tag.cpp — together the two
// witness both directions of the GAPS-096 typed-accessor surface
// (the typed-handle direction is covered by the GAPS-094 fixture).
//
// [GCC-WRAPPER-TEXT] — function-argument type-mismatch rejection.

#include "../../vessel/torch/vessel_api_typed.h"

#include <crucible/Types.h>
#include <crucible/safety/Tagged.h>

using crucible::safety::Tagged;
using crucible::safety::source::External;

int main() {
    // schema_name_typed returns Tagged<const char*, source::Sanitized>.
    // Should FAIL: the conversion target Tagged<const char*,
    // source::External> is a DIFFERENT class instantiation; no
    // implicit retag from Sanitized → External exists (and the
    // converse direction is also rejected — see GAPS-094 fixture).
    Tagged<const char*, External> wrong =
        crucible::vessel::schema_name_typed(crucible::SchemaHash{0});
    return wrong.value() == nullptr ? 0 : 1;
}
