// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// The fan-out of mint_parallel_for starts one thread per shard.  That is
// the work of the mint, so only the mint may reach it: the fan-out is a
// private static member of ParallelForRunner, and the mint is its only
// friend.  A free function in a detail namespace once held it, and any
// translation unit could start threads through it with no context at all.
//
// This caller hands over a background context and a tuple of values, so
// the only rejection left is the access check.
//
// Expected diagnostic: the fan-out is private within this context.

#include <fixy/os/Spawn.h>

#include <tuple>
#include <utility>

namespace eff = foundation::effects;

int main() {
    eff::detail::ctx_witnesses::BgWitness ctx{eff::testing::bg()};
    std::tuple<int, int> shards{0, 0};
    fixy::spawn::ParallelForRunner::run_shards_(ctx, shards, [](int& shard) noexcept { ++shard; },
                                                std::make_index_sequence<2>{});
    return 0;
}
