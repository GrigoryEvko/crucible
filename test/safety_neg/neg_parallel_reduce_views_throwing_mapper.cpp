// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F04-AUDIT — locks in the mapper-noexcept static_assert on
// safety/Workload.h's `parallel_reduce_views<N, R>`.  Crucible
// compiles with -fno-exceptions; a mapper that propagates an
// exception would terminate inside a worker jthread, taking down
// the entire process with std::terminate (no propagation across
// thread boundaries).  The static_assert catches this at the call
// site BEFORE thread spawn.
//
// Without the assert, a non-noexcept mapper would compile fine
// (jthread accepts any callable), then std::terminate at runtime
// the moment the mapper threw.  The assert turns a runtime crash
// into a compile error naming the violating signature.
//
// [GCC-WRAPPER-TEXT] — static_assert with the "Mapper must be
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

    // Should FAIL: mapper is NOT noexcept (no `noexcept` qualifier on
    // the lambda).  parallel_reduce_views requires a noexcept-invocable
    // mapper because workers run inside jthread bodies that cannot
    // propagate exceptions across the thread boundary.
    auto [total, _] = safety::parallel_reduce_views<2, std::uint64_t>(
        std::move(region),
        std::uint64_t{0},
        [](auto sub) /* NOT noexcept */ {
            std::uint64_t s = 0;
            for (auto x : sub.cspan()) s += x;
            return s;
        },
        [](std::uint64_t a, std::uint64_t b) noexcept { return a + b; }
    );
    (void)total;

    return 0;
}
