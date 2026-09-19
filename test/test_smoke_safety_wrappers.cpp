// Sentinel TU: calls the per-header runtime_smoke_test hook on the core safety
// wrappers.  A static_assert cannot catch a consteval-versus-constexpr
// regression, an SFINAE path that never instantiates its inline body, or a
// warning that fires only when the project flags run over the body tokens.

#include <crucible/handles/Once.h>
#include <crucible/safety/_Affine.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/ConstantTime.h>
#include <crucible/safety/Cyclic.h>
#include <crucible/safety/CyclicBuffer.h>
#include <crucible/safety/_Linear.h>
#include <crucible/safety/_Mutation.h>
#include <crucible/safety/OwnedRegion.h>
#include <crucible/safety/_Refined.h>
#include <crucible/safety/_SealedRefined.h>
#include <crucible/safety/_Secret.h>
#include <crucible/safety/_Tagged.h>
#include <crucible/safety/WeakRef.h>

int main() {
    using namespace crucible::safety;

    detail::affine_self_test::runtime_smoke_test();
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
    detail::weak_ref_self_test::runtime_smoke_test();

    ct::detail::ct_self_test::runtime_smoke_test();

    detail::lazy_self_test::runtime_smoke_test();

    return 0;
}
