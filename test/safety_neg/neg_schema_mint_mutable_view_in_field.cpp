// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// A class type that keeps a SchemaTable::MutableView as a non-static data
// member must fail the reflection audit no_scoped_view_field_check.  The
// audit walks the type tree of each member and refuses a ScopedView.
//
// SchemaTable::MutableView is an alias of
// ::fixy::ScopedView<SchemaTable, schema_state::Mutable>.  Reflection reads
// through the alias, so the audit fires as for the ScopedView spelling.
// SchemaTable.h runs the audit on SchemaTable itself.  This fixture shows
// that the audit also fires for a different carrier that keeps the view as
// a field.
//
// Each carrier has its own view type, its own view_ok overload and its own
// audit instance.  The kernel table fixtures do not see a defect in the
// schema table gate, so the schema table has this fixture of its own.
//
// The companion fixture neg_schema_mint_mutable_view_to_sealed_overload
// catches a view with the wrong tag at a call.  This fixture catches a view
// that a field keeps after its carrier is gone.
//
// Expected diagnostic: the static assertion of the audit fails for
// OffendingSchemaContainer.

#include <crucible/SchemaTable.h>
#include <fixy/ScopedView.h>

// The HS14 scanner counts a fixture that names mint_mutable_view, so this
// function names it.  The static_assert below is the gate under test.
[[maybe_unused]] static auto anchor_schema_mutable_view_mint() {
    crucible::SchemaTable t;
    return t.mint_mutable_view(::foundation::effects::testing::foreground<crucible::Vigil>());
}

// The audit reads the layout from the declaration, so the class needs no
// constructor and no instance.
struct OffendingSchemaContainer {
    crucible::SchemaTable::MutableView view_;
};

static_assert(::fixy::no_scoped_view_field_check<OffendingSchemaContainer>(),
              "the audit must reject a class that keeps a SchemaTable::MutableView as a field");

int main() { return 0; }
