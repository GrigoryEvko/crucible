// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::commit_per_request refuses a bare region pointer.  Each commit
// entry point takes a region in a lifetime band, so the caller states the
// scope the region lives for.  A bare pointer states no scope, so it does
// not pass even the widest gate.

#include <crucible/Cipher.h>
#include <fixy/Ctx.h>
#include <foundation/Quarantine.h>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    auto cipher = ::crucible::Cipher::open(store_ctx, store_ctx.cap().alloc,
                                           ::fixy::mint_tagged<::fixy::tags::source::External>(
                                               ::fixy::TextView{"/tmp/crucible_neg_request_bare_pointer"}))
                      .expect("the fixture opens its store");
    const auto view = cipher.mint_open_view(store_ctx);

    CRUCIBLE_I_KNOW_WHAT_IM_DOING("PROBE: the fixture gives a raw pointer to a typed door, and the door refuses it")
    const ::crucible::RegionNode* bare = nullptr;
    auto result = cipher.commit_per_request(view, bare);
    CRUCIBLE_END_I_KNOW_WHAT_IM_DOING
    return static_cast<int>(static_cast<bool>(std::move(result).consume()));
}
