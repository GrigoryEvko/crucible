// The inputs of a parallel loop go to one call (reads() of
// fixy/concurrent/Spawn.h).  A name would keep the references to the inputs past
// the life of an input, so a Reads does not copy and does not move, and a named
// one does not reach a loop.

#include "../parallel_loop_rig.h"

namespace rig = ::parallel_loop_rig;

int main() {
    ::fixy::HeapArray<rig::Word> const input = rig::make_items();
    ::fixy::HeapArray<rig::Word> items = rig::make_items();
    items = ::fixy::with_region<rig::Written>(std::move(items), [&input](auto region) noexcept -> decltype(region) {
        rig::BgCtx const ctx{rig::eff::testing::bg()};
        auto inputs = ::fixy::spawn::reads(input);
        return ::fixy::spawn::mint_parallel_for<::fixy::spawn::BodyCoupling::Independent, rig::kShards>(
            ctx, ::fixy::concurrent::WorkBudget{.read_bytes = 64}, std::move(region), std::move(inputs),
            [](auto shard, auto, auto run) noexcept {
                static_cast<void>(shard);
                static_cast<void>(run);
            });
    });
    return 0;
}
