// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// FOUND-F02-AUDIT — locks in the noexcept-invocable-shape static_assert
// in safety/Workload.h's `parallel_apply_pair<N>` for the case where
// the body's parameter shape DOES NOT match the regions' tags.
//
// Specifically: the test passes regions with tags DataNegA and
// DataNegB, but the body takes both parameters with tag DataNegA
// (i.e., `Slice<DataNegA, I>` for both positions).  The static_assert
// expects `void(OwnedRegion<T1, Slice<W1, I>>&&, OwnedRegion<T2,
// Slice<W2, I>>&&)`, where W1 == DataNegA and W2 == DataNegB.  A body
// that takes Slice<DataNegA, I> for the second parameter is NOT
// invocable with the actual sub_b type Slice<DataNegB, I>.
//
// Without the static_assert, the failure would surface deep inside
// spawn_workers_pair_'s lambda invocation as a "no matching
// function" diagnostic with no clear contract reference.  The static
// assert at the API boundary names the exact contract: the body's
// parameter shape must match the regions' tags.
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

    // Should FAIL: body takes both parameters as
    // OwnedRegion<std::uint64_t, safety::Slice<DataNegA, I>>&& —
    // i.e., uses DataNegA for the SECOND parameter, but the actual
    // sub_b type is Slice<DataNegB, I>.  The noexcept-invocable
    // static_assert at the API boundary catches this mismatch.
    auto recombined = safety::parallel_apply_pair<2>(
        std::move(region_a), std::move(region_b),
        [](safety::OwnedRegion<std::uint64_t, safety::Slice<DataNegA, 0>>&& sub_a,
           safety::OwnedRegion<std::uint64_t, safety::Slice<DataNegA, 0>>&& sub_b)
            noexcept {
            (void)sub_a; (void)sub_b;
        }
    );
    (void)recombined;

    return 0;
}
