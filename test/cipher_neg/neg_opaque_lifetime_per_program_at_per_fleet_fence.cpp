// Negative compile test.  This file must fail to compile.
//
// Cipher::commit_per_fleet refuses a region whose lifetime band is
// PER_PROGRAM.  Program-scoped state, such as the scratch of one model
// instance, written to the fleet-wide cold tier would reach other
// instances on other relays, including a canary that runs another model
// version.  PER_FLEET sits above PER_PROGRAM, so the gate refuses the
// call.

#include <crucible/Cipher.h>
#include <crucible/MerkleDag.h>
#include <fixy/Bands.h>
#include <fixy/Ctx.h>

#include <utility>

int main() {
    const ::fixy::TestRunnerCtx store_ctx{::foundation::effects::testing::test()};
    auto cipher = ::crucible::Cipher::open(store_ctx, store_ctx.cap().alloc,
                                           ::fixy::mint_tagged<::fixy::tags::source::External>(
                                               ::fixy::TextView{"/tmp/crucible_neg_program_at_fleet"}))
                      .expect("the fixture opens its store");
    const auto view = cipher.mint_open_view(store_ctx);

    // The band holds a Ref of a region, so the scope is the one fault.
    using RegionRef = ::crucible::DagRef<const ::crucible::RegionNode>;
    ::fixy::BumpArena<::crucible::DagArena> arena = ::fixy::mint_bump_arena<::crucible::DagArena>(
        ::fixy::Quantity<::fixy::unit::Byte, std::size_t>{std::size_t{4096}});
    const RegionRef region = ::crucible::mint_dag_node<::crucible::RegionNode>(store_ctx.cap().alloc, arena);
    auto program_scoped = ::fixy::mint_band<::fixy::opaque_lifetime::PerProgram<RegionRef>>(region);
    auto result = cipher.commit_per_fleet(view, std::move(program_scoped));
    return static_cast<int>(static_cast<bool>(std::move(result).consume()));
}
