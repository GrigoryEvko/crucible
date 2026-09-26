// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::record_event refuses the hot foreground context, whose row is
// empty.  Recording writes HEAD and appends to the log, which needs IO
// and Block.  A foreground caller that recorded an event would do a
// blocking file write on each iteration and break replay determinism.
// The caller also holds an open view here, so the refusal comes from the
// context and not from a missing view.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    auto cipher = ::crucible::Cipher::open(store_ctx, ::fixy::mint_tagged<::fixy::tags::source::External>(
                                                          std::filesystem::path{"/tmp/crucible_neg_record_event"}));
    const auto view = cipher.mint_open_view(store_ctx);
    cipher.record_event(::foundation::effects::testing::foreground(), view, ::crucible::ContentHash{1u},
                        std::uint64_t{1u});
    return 0;
}
