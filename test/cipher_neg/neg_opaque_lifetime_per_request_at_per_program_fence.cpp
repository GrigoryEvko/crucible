// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// Cipher::commit_per_program refuses a region whose lifetime band is
// PER_REQUEST.  The warm tier survives a program restart, so a
// request-scoped value written there would come back on the next start
// and be replayed as if it were new.  PER_PROGRAM sits above PER_REQUEST,
// so the gate refuses the call.

#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <fixy/Bands.h>
#include <fixy/Ctx.h>

#include <utility>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    auto cipher = ::crucible::Cipher::open(store_ctx, store_ctx.cap().alloc,
                                           ::fixy::mint_tagged<::fixy::tags::source::External>(
                                               ::fixy::TextView{"/tmp/crucible_neg_request_at_program"}))
                      .expect("the fixture opens its store");
    const auto view = cipher.mint_open_view(store_ctx);

    // The band holds a Ref of a region, so the scope is the one fault.
    using RegionRef = ::fixy::Ref<const ::crucible::RegionNode>;
    const ::crucible::RegionNode region{};
    auto request_scoped = ::fixy::mint_band<::fixy::opaque_lifetime::PerRequest<RegionRef>>(
        RegionRef{::fixy::erase_brand(::fixy::to(region))});
    auto result = cipher.commit_per_program(store_ctx, view, std::move(request_scoped));
    return static_cast<int>(static_cast<bool>(std::move(result).consume()));
}
