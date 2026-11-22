// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The fan-out of mint_parallel_for starts one thread per shard.  That is
// the work of the mint, so only the mint may reach it: the fan-out is a
// private static member of ParallelForRunner, and the mint is its only
// friend.  A free function in a detail namespace once held it, and any
// translation unit could start threads through it with no context at all.
//
// This caller hands over a background context and the shards of a split,
// so the only rejection left is the access check.
//
// Expected diagnostic: the fan-out is private within this context.

#include <fixy/concurrent/Spawn.h>
#include <foundation/permissions/Permission.h>

#include "../owned_region_unchecked_arena.h"

#include <utility>

namespace eff = foundation::effects;

namespace {
struct RegionWhole {
    using permission_row = ::foundation::effects::Row<>;
};
}  // namespace

int main() {
    eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    test_owned_region::UncheckedArena arena{};
    auto parts = fixy::mint_split<2>(fixy::mint_owned_region<int>(
        eff::testing::test().alloc, arena, 0uz, foundation::permissions::mint_permission_root<RegionWhole>()));
    [[maybe_unused]] auto started = fixy::spawn::ParallelForRunner::run_shards_(
        ctx, parts.shards, [](auto shard, auto) noexcept { static_cast<void>(shard); }, std::size_t{2},
        fixy::thread::ThreadOptions{}, std::make_index_sequence<2>{});
    return 0;
}
