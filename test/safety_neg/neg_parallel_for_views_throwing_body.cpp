// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F01-AUDIT — locks in the body-noexcept static_assert on
// safety/Workload.h's `parallel_for_views<N>`.  Crucible compiles
// with -fno-exceptions; a body that propagates an exception would
// terminate inside a worker jthread, taking down the entire process
// with std::terminate (no propagation across thread boundaries).
//
// The static_assert catches non-noexcept bodies at the call site
// BEFORE thread spawn.  Mirrors
// neg_parallel_reduce_views_throwing_mapper for the sibling
// primitive.
//
// [GCC-WRAPPER-TEXT] — static_assert with "body must be noexcept-
// invocable" diagnostic text.

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

    // Should FAIL: body is NOT noexcept (no `noexcept` qualifier on
    // the lambda).  parallel_for_views requires a noexcept-invocable
    // body because workers run inside jthread bodies that cannot
    // propagate exceptions across the thread boundary.
    auto recombined = safety::parallel_for_views<2>(
        std::move(region),
        [](auto sub) /* NOT noexcept */ {
            for (auto& x : sub.span()) x = 1;
        }
    );
    (void)recombined;

    return 0;
}
