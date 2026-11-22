// Negative compile test.  This file must fail to compile.
//
// Cipher::persist_session_events refuses the hot foreground context.  The
// call writes a batch file and appends an index line, which needs IO and
// Block, and the foreground row is empty.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    auto cipher = ::crucible::Cipher::open(store_ctx, store_ctx.cap().alloc,
                                           ::fixy::mint_tagged<::fixy::tags::source::External>(
                                               ::fixy::TextView{"/tmp/crucible_neg_persist_events_fg"}))
                      .expect("the fixture opens its store");
    const auto view = cipher.mint_open_view(store_ctx);
    const auto hash =
        cipher.persist_session_events(::foundation::effects::testing::foreground(), view, store_ctx.cap().alloc,
                                      ::fixy::View<const ::crucible::Cipher::SessionEvent>{});
    return hash.is_ok() ? 0 : 1;
}
