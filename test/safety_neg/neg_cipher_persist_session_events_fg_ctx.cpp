// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::persist_session_events refuses the hot foreground context.  The
// call writes a batch file and appends an index line, which needs IO and
// Block, and the foreground row is empty.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>

#include <span>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    auto cipher = ::crucible::Cipher::open(store_ctx, ::fixy::mint_tagged<::fixy::tags::source::External>(
                                                          std::filesystem::path{"/tmp/crucible_neg_persist_events_fg"}));
    const auto view = cipher.mint_open_view(store_ctx);
    const auto hash = cipher.persist_session_events(::foundation::effects::testing::foreground(), view,
                                                    std::span<const ::crucible::Cipher::SessionEvent>{});
    return static_cast<bool>(hash) ? 0 : 1;
}
