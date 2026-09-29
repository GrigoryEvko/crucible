// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The result of SchemaTable::mint_mutable_view() goes to a helper that takes
// a SchemaTable::SealedView.  The two views have the same carrier
// (SchemaTable) and different tags (schema_state::Mutable and
// schema_state::Sealed).  ScopedView<C, A> does not convert to
// ScopedView<C, B> when A and B are different, so the call does not compile.
//
// The kernel table has the same fixture.  Each carrier has its own view type
// and its own view_ok overload, so a defect in the schema table gate needs
// a witness of its own.
//
// The companion fixture neg_schema_mint_mutable_view_in_field catches a view
// that a field keeps.  This fixture catches a view with the wrong tag at a
// call.

#include <crucible/SchemaTable.h>

// No production function has this shape.  The helper asks the type system
// to convert a MutableView to a SealedView, which it must refuse.
static void requires_sealed_view(crucible::SchemaTable::SealedView const&) noexcept {}

int main() {
    crucible::SchemaTable t;
    // A default-constructed table is not sealed, so mint_mutable_view
    // returns a view.
    const auto mv = t.mint_mutable_view(::foundation::effects::testing::foreground<crucible::Vigil>());

    requires_sealed_view(*mv);
    return 0;
}
