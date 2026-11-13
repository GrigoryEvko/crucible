// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// schema_name_typed(...) returns an Option of
// Tagged<CText, source::Interned>.  This fixture puts that value in a slot
// that expects the same text with source::External.
//
// The Interned tag says that the schema table owns the characters, and that
// the table admitted them only as Sanitized or FromInternal names.  If an
// interned name could pass as raw External input, a lane that reads only
// sanitizer output could not tell the two apart.  The type system refuses
// the conversion.
//
// The fixture neg_data_ptr_typed_wrong_tag refuses the other direction of
// the typed accessors.

#include "../../vessel/torch/vessel_api_typed.h"

#include <crucible/Types.h>
#include <fixy/Tagged.h>
#include <fixy/Tags.h>

int main() {
    ::fixy::Option<::fixy::Tagged<::fixy::CText, ::fixy::tags::source::External>> wrong =
        crucible::vessel::schema_name_typed(crucible::SchemaHash{0});
    return wrong.is_some() ? 0 : 1;
}
