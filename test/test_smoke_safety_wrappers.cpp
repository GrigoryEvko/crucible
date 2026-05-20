// fixy-A1-016 — Sentinel TU exercising the per-header `runtime_smoke_test()`
// hook on the nine core safety wrappers.
//
// Each header ships `inline void runtime_smoke_test()` inside a nested
// `detail::<wrapper>_self_test` namespace, exercising every named op with
// non-constant input plus a move-only T witness where applicable.  Pure
// `static_assert` tests cannot catch consteval/SFINAE/inline-body bugs that
// only surface when the header is compiled under project warning flags and
// linked into a real TU; this sentinel closes that blind spot.
//
// Discipline reference: feedback_algebra_runtime_smoke_test_discipline +
// feedback_header_only_static_assert_blind_spot.

#include <crucible/handles/Once.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/ConstantTime.h>
#include <crucible/safety/Cyclic.h>
#include <crucible/safety/CyclicBuffer.h>
#include <crucible/safety/Linear.h>
#include <crucible/safety/Mutation.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/Refined.h>
#include <crucible/safety/SealedRefined.h>
#include <crucible/safety/Secret.h>
#include <crucible/safety/Tagged.h>

int main() {
    using namespace crucible::safety;

    detail::borrowed_self_test::runtime_smoke_test();
    detail::cyclic_self_test::runtime_smoke_test();
    detail::cyclic_buffer_self_test::runtime_smoke_test();
    detail::linear_self_test::runtime_smoke_test();
    detail::mutation_self_test::runtime_smoke_test();
    detail::owned_region_self_test::runtime_smoke_test();
    detail::refined_self_test::runtime_smoke_test();
    detail::sealed_refined_self_test::runtime_smoke_test();
    detail::secret_self_test::runtime_smoke_test();
    detail::tagged_self_test::runtime_smoke_test();

    ct::detail::ct_self_test::runtime_smoke_test();

    // fixy-A1-017: Lazy<T> get_or_init / no-arg get split.  Witnesses
    // that the renamed surface preserves "f runs exactly once".
    detail::lazy_self_test::runtime_smoke_test();

    return 0;
}
