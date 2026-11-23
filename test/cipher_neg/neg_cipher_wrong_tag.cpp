// Negative compile test.  This file must fail to compile.
//
// Cipher::store refuses a scoped view of another carrier.  store takes a
// view of a Cipher tagged cipher_state::Open.  A sealed SchemaTable view
// names another carrier and another tag, so it does not convert.  Each
// carrier's phase gate is its own lock, not one global "some valid state"
// check.  The context here is valid, so the refusal names the view.

#include <crucible/Cipher.h>
#include <crucible/SchemaTable.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    crucible::SchemaTable st;
    st.seal(store_ctx);
    auto st_view = st.mint_sealed_view();

    // The store is never opened.  Overload resolution refuses the call
    // before the body of store could run.
    crucible::Cipher c{::foundation::effects::testing::test().alloc};
    (void)c.store(
        store_ctx, st_view,
        ::crucible::cipher::content_addressed_payload<::crucible::RegionNode, ::crucible::DagArena>(::fixy::none));
    return 0;
}
