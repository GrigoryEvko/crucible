// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::commit_per_program refuses a region whose lifetime band is
// PER_REQUEST.  The warm tier survives a program restart, so a
// request-scoped value written there would come back on the next start
// and be replayed as if it were new.  PER_PROGRAM sits above PER_REQUEST,
// so the gate refuses the call.

#include <crucible/Cipher.h>
#include <fixy/Bands.h>
#include <fixy/Ctx.h>

#include <utility>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    auto cipher =
        ::crucible::Cipher::open(store_ctx, ::fixy::mint_tagged<::fixy::tags::source::External>(
                                                std::filesystem::path{"/tmp/crucible_neg_request_at_program"}));
    const auto view = cipher.mint_open_view(store_ctx);

    auto request_scoped =
        ::fixy::mint_band<::fixy::opaque_lifetime::PerRequest<const ::crucible::RegionNode*>>(nullptr);
    auto result = cipher.commit_per_program(view, std::move(request_scoped));
    return static_cast<int>(static_cast<bool>(std::move(result).consume()));
}
