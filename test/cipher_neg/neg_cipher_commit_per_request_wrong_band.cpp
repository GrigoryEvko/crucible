// Negative compile test.  This file must fail to compile.
//
// Cipher::commit_per_request refuses a region in a band of another
// lattice.  A cipher tier band says where persisted bytes live, not how
// long the region lives, so it is no lifetime band and states no scope.

#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <fixy/Bands.h>
#include <fixy/Ctx.h>

#include <utility>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    auto cipher = ::crucible::Cipher::open(store_ctx, store_ctx.cap().alloc,
                                           ::fixy::mint_tagged<::fixy::tags::source::External>(
                                               ::fixy::TextView{"/tmp/crucible_neg_request_wrong_band"}))
                      .expect("the fixture opens its store");
    const auto view = cipher.mint_open_view(store_ctx);

    // The band holds a Ref of a region, so the lattice is the one fault.
    using RegionRef = ::crucible::DagRef<const ::crucible::RegionNode>;
    ::fixy::BumpArena<::crucible::DagArena> arena = ::fixy::mint_bump_arena<::crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{4096}});
    const RegionRef region = ::crucible::mint_dag_node<::crucible::RegionNode>(store_ctx.cap().alloc, arena);
    auto hot_tier = ::fixy::mint_band<::fixy::cipher_tier::Hot<RegionRef>>(region);
    auto result = cipher.commit_per_request(view, std::move(hot_tier));
    return static_cast<int>(static_cast<bool>(std::move(result).consume()));
}
