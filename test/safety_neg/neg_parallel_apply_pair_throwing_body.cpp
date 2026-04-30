// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F02 — locks in the body-noexcept static_assert on
// safety/Workload.h's `parallel_apply_pair<N>`.  Crucible compiles
// with -fno-exceptions; a body that propagates an exception would
// terminate inside a worker jthread, taking down the entire process
// with std::terminate (no propagation across thread boundaries).
//
// The static_assert catches non-noexcept bodies at the call site
// BEFORE thread spawn.  Mirrors
// neg_parallel_for_views_throwing_body / neg_parallel_reduce_views_throwing_mapper
// for the sibling primitives.
//
// [GCC-WRAPPER-TEXT] — static_assert with "body must be noexcept-
// invocable" diagnostic text.

#include <crucible/Arena.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/permissions/Permission.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/Workload.h>

#include <cstdint>

struct DataNegA {};
struct DataNegB {};

int main() {
    using namespace crucible;

    Arena arena;
    constexpr std::size_t N = 8;
    auto region_a = safety::OwnedRegion<std::uint64_t, DataNegA>::adopt(
        effects::Test{}.alloc, arena, N,
        safety::permission_root_mint<DataNegA>());
    auto region_b = safety::OwnedRegion<std::uint64_t, DataNegB>::adopt(
        effects::Test{}.alloc, arena, N,
        safety::permission_root_mint<DataNegB>());

    // Should FAIL: body is NOT noexcept (no `noexcept` qualifier on
    // the lambda).  parallel_apply_pair requires a noexcept-invocable
    // body because workers run inside jthread bodies that cannot
    // propagate exceptions across the thread boundary.
    auto recombined = safety::parallel_apply_pair<2>(
        std::move(region_a), std::move(region_b),
        [](auto sub_a, auto sub_b) /* NOT noexcept */ {
            (void)sub_a; (void)sub_b;
        }
    );
    (void)recombined;

    return 0;
}
