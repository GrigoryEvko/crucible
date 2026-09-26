// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::commit_per_request refuses a bare region pointer.  Each commit
// entry point takes a region in a lifetime band, so the caller states the
// scope the region lives for.  A bare pointer states no scope, so it does
// not pass even the widest gate.

#include <crucible/Arena.h>
#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <fixy/Ctx.h>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    auto cipher = ::crucible::Cipher::open(store_ctx, ::fixy::mint_tagged<::fixy::tags::source::External>(
                                                          std::filesystem::path{"/tmp/crucible_neg_request_bare_pointer"}));
    const auto view = cipher.mint_open_view(store_ctx);
    ::crucible::MetaLog log;

    const ::crucible::RegionNode* bare = nullptr;
    auto result = cipher.commit_per_request(view, bare, &log);
    return static_cast<int>(static_cast<bool>(std::move(result).consume()));
}
