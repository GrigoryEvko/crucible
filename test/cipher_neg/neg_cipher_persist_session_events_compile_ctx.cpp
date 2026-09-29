// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::persist_session_events refuses the background compile context.
// Its row holds IO but not Block, and the call flushes a batch file and an
// index line to storage, which blocks.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>

#include <span>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    auto cipher =
        ::crucible::Cipher::open(store_ctx, ::fixy::mint_tagged<::fixy::tags::source::External>(
                                                std::filesystem::path{"/tmp/crucible_neg_persist_events_compile"}));
    const auto view = cipher.mint_open_view(store_ctx);
    const ::fixy::BgCompileCtx compile{::foundation::effects::testing::bg()};
    const auto hash = cipher.persist_session_events(compile, view, std::span<const ::crucible::Cipher::SessionEvent>{});
    return static_cast<bool>(hash) ? 0 : 1;
}
