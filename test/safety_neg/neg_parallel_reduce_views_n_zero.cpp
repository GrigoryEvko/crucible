// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F04-AUDIT — locks in the `static_assert(N > 0)` body assertion
// in safety/Workload.h's `parallel_reduce_views<N, R>` definition.
//
// Violation: instantiating parallel_reduce_views with N=0.  N=0 would
// degenerate the map-reduce into "fold init alone, no shards" — a
// pathological case where the result equals init but no work is done.
// Worse, `split_into<0>()` on the OwnedRegion would return an empty
// std::tuple, and the partials std::array<R, 0> would be a zero-sized
// array — defensible C++ but useless and almost certainly a caller
// mistake.  The static_assert refuses N=0 at the call site, naming
// the precondition explicitly so the diagnostic points at the cause.
//
// Why this neg-compile fixture is load-bearing: a future refactor that
// re-typedef'd N or accidentally widened the assertion (e.g.
// `static_assert(N >= 0)` after a DR-driven cleanup) would silently
// admit N=0, making the function type-correct but semantically wrong.
// This fixture compiles only when N=0 is rejected, locking in the
// strict positivity contract.
//
// [GCC-WRAPPER-TEXT] — static_assert with the `requires N > 0` text.

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
    auto perm = safety::permission_root_mint<DataNeg>();
    constexpr std::size_t N = 8;
    auto region = safety::OwnedRegion<std::uint64_t, DataNeg>::adopt(
        effects::Test{}.alloc, arena, N, std::move(perm));
    for (std::size_t i = 0; i < N; ++i) region.span()[i] = 1;

    // Should FAIL: N=0 violates the `static_assert(N > 0)` precondition
    // in safety/Workload.h's parallel_reduce_views definition.
    auto [total, _] = safety::parallel_reduce_views<0, std::uint64_t>(
        std::move(region),
        std::uint64_t{0},
        [](auto sub) noexcept {
            std::uint64_t s = 0;
            for (auto x : sub.cspan()) s += x;
            return s;
        },
        [](std::uint64_t a, std::uint64_t b) noexcept { return a + b; }
    );
    (void)total;

    return 0;
}
