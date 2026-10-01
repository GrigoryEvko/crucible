// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::commit_per_request refuses a region in a band of another
// lattice.  A cipher tier band says where persisted bytes live, not how
// long the region lives, so it is no lifetime band and states no scope.

#include <crucible/Cipher.h>
#include <crucible/MetaLog.h>
#include <fixy/Bands.h>
#include <fixy/Ctx.h>

#include <utility>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    auto cipher =
        ::crucible::Cipher::open(store_ctx, ::fixy::mint_tagged<::fixy::tags::source::External>(
                                                std::filesystem::path{"/tmp/crucible_neg_request_wrong_band"}));
    const auto view = cipher.mint_open_view(store_ctx);
    ::crucible::MetaLog log;

    auto hot_tier = ::fixy::mint_band<::fixy::cipher_tier::Hot<const ::crucible::RegionNode*>>(nullptr);
    auto result = cipher.commit_per_request(view, std::move(hot_tier), &log);
    return static_cast<int>(static_cast<bool>(std::move(result).consume()));
}
