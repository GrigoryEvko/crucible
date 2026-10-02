// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::store refuses a scoped view of another carrier.  store takes a
// view of a Cipher tagged cipher_state::Open.  A sealed SchemaTable view
// names another carrier and another tag, so it does not convert.  Each
// carrier's phase gate is its own lock, not one global "some valid state"
// check.

#include <crucible/Cipher.h>
#include <crucible/SchemaTable.h>

int main() {
    crucible::SchemaTable st;
    st.seal();
    auto st_view = st.mint_sealed_view();

    // The store is never opened.  Overload resolution refuses the call
    // before the body of store could run.
    crucible::Cipher c;
    (void)c.store(st_view, ::crucible::Cipher::content_addressed(nullptr));
    return 0;
}
