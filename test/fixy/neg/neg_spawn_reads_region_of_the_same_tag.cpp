// A region of the tag of the written region is no input of a parallel loop
// (reads() of fixy/concurrent/Spawn.h).  Its type does not tell it apart from
// the written region, which is the same object here.  The caller gives the View
// of its elements, which takes the compare of bytes.

#include "../parallel_loop_rig.h"

namespace rig = ::parallel_loop_rig;

int main() {
    ::fixy::HeapArray<rig::Word> items = rig::make_items();
    items = ::fixy::with_region<rig::Written>(std::move(items), [](auto region) noexcept -> decltype(region) {
        rig::BgCtx const ctx{rig::eff::testing::bg()};
        return ::fixy::spawn::mint_parallel_for<::fixy::spawn::BodyCoupling::Independent, rig::kShards>(
            ctx, ::fixy::concurrent::WorkBudget{.read_bytes = 64}, std::move(region), ::fixy::spawn::reads(region),
            [](auto shard, auto, auto run) noexcept {
                static_cast<void>(shard);
                static_cast<void>(run);
            });
    });
    return 0;
}
