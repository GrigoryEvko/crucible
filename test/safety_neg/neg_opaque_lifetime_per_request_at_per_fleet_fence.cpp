// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::commit_per_fleet refuses a region whose lifetime band is
// PER_REQUEST.  The entry point writes to the cold tier, which is durable
// and replicated across the fleet, so it demands a PER_FLEET band.
//
// The scopes order PER_REQUEST below PER_PROGRAM below PER_FLEET.  A band
// satisfies a requirement when the requirement sits at or below its own
// scope, and PER_FLEET sits above PER_REQUEST, so the gate refuses the
// call.  Without the gate, request-scoped state, such as the grammar
// state of one constrained decode, would reach fleet-durable storage and
// come back as input to another request on the next replay.

#include <crucible/Arena.h>
#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <crucible/MetaLog.h>
#include <fixy/Bands.h>
#include <fixy/Ctx.h>

#include <utility>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    auto cipher = ::crucible::Cipher::open(store_ctx, ::fixy::mint_tagged<::fixy::tags::source::External>(
                                                          std::filesystem::path{"/tmp/crucible_neg_request_at_fleet"}));
    const auto view = cipher.mint_open_view(store_ctx);
    ::crucible::MetaLog log;

    auto request_scoped =
        ::fixy::mint_band<::fixy::opaque_lifetime::PerRequest<const ::crucible::RegionNode*>>(nullptr);
    auto result = cipher.commit_per_fleet(view, std::move(request_scoped), &log);
    return static_cast<int>(static_cast<bool>(std::move(result).consume()));
}
