// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a SchemaTable::SealedView to register_name.
// The typed overload only accepts MutableView — the Sealed tag cannot
// convert to the Mutable tag, and there is no Sealed overload.  This
// is the core state-discipline guarantee: once the table is sealed,
// no call site can reach register_name via the typed path.

#include <crucible/SchemaTable.h>

int main() {
    crucible::SchemaTable t;
    t.seal();
    auto sv = t.mint_sealed_view();

    // No register_name(SealedView, ...) overload exists.  The typed
    // overload takes MutableView, and Sealed → Mutable is not a
    // convertible type substitution.  Diagnostic cites "no matching
    // function for call to".
    t.register_name(sv,
                    crucible::SchemaHash{0x42},
                    crucible::SchemaTable::SanitizedName{"aten::mm"});
    return 0;
}
