// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F04-AUDIT — locks in the reducer-noexcept static_assert on
// safety/Workload.h's `parallel_reduce_views<N, R>`.  Mirrors the
// mapper-noexcept fixture; the reducer runs on the main thread
// AFTER worker join, but it still must be noexcept because
// Crucible's -fno-exceptions rule applies fleet-wide AND because
// throwing during the post-join fold would leave the recombined
// OwnedRegion in an indeterminate state (workers' partial writes
// already complete, but the fold is mid-flight).
//
// Without the assert, a non-noexcept reducer would compile but
// std::terminate the moment it threw.  The assert turns this into
// a compile error pointing at the violating reducer signature.
//
// [GCC-WRAPPER-TEXT] — static_assert with the "Reducer must be
// noexcept-invocable" diagnostic text.

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/Workload.h>

#include <cstdint>

struct DataNeg {};

int main() {
    using namespace crucible;

    Arena arena;
    auto perm = safety::mint_permission_root<DataNeg>();
    constexpr std::size_t N = 8;
    auto region = safety::OwnedRegion<std::uint64_t, DataNeg>::adopt(
        effects::Test{}.alloc, arena, N, std::move(perm));

    // Should FAIL: reducer is NOT noexcept (no `noexcept` qualifier).
    // parallel_reduce_views requires a noexcept-invocable reducer
    // because the post-join fold runs on the main thread under
    // -fno-exceptions; a throw would std::terminate the process.
    auto [total, _] = safety::parallel_reduce_views<2, std::uint64_t>(
        std::move(region),
        std::uint64_t{0},
        [](auto sub) noexcept {
            std::uint64_t s = 0;
            for (auto x : sub.cspan()) s += x;
            return s;
        },
        [](std::uint64_t a, std::uint64_t b) /* NOT noexcept */ {
            return a + b;
        }
    );
    (void)total;

    return 0;
}
