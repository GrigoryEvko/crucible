// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Violation: passing a ScopedView with the WRONG state tag to a Cipher
// operation.  Cipher::store(OpenView const&, ...) only accepts a view
// tagged cipher_state::Open.  A SchemaTable::SealedView (tagged with
// schema_state::Sealed) must not convert — the two views parametrize
// ScopedView on different Carrier types AND different Tag types.
//
// This proves the type-state discipline is per-carrier, not a single
// global "any valid state is fine" — each carrier's phase gate is its
// own parametric lock.

#include <crucible/Cipher.h>
#include <crucible/ir001/SchemaTable.h>

int main() {
    crucible::SchemaTable st;
    st.seal();
    auto st_view = st.mint_sealed_view();

    crucible::Cipher c;   // Closed; we never open it — we never reach
                          // the typed store() body anyway: compilation
                          // fails at the overload resolution step.

    // Cipher::store requires an OpenView over Cipher.  st_view is a
    // sealed SchemaTable view, so GCC rejects the call before the typed
    // store() body is reachable.
    (void)c.store(st_view, nullptr, nullptr);
    return 0;
}
