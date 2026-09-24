// Sentinel TU: calls the per-header runtime_smoke_test hook on the algebra and
// effects headers.  A static_assert cannot catch a consteval-versus-constexpr
// regression, an SFINAE path that never instantiates its inline body, or a
// warning that fires only when the project flags run over the body tokens.

#include <crucible/algebra/_GradedTrait.h>
#include <crucible/algebra/_Lattice.h>
#include <crucible/algebra/_Modality.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/Concurrent.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/effects/Resources.h>

int main() {
    ::crucible::algebra::detail::lattice_self_test::runtime_smoke_test();
    ::crucible::algebra::detail::modality_self_test::runtime_smoke_test();
    ::crucible::algebra::detail::is_graded_specialization_self_test::runtime_smoke_test();

    ::crucible::effects::detail::capabilities_self_test::runtime_smoke_test();
    ::crucible::effects::detail::resources_self_test::runtime_smoke_test();
    ::crucible::effects::detail::concurrent_row_self_test::runtime_smoke_test();
    ::crucible::effects::detail::effect_row_self_test::runtime_smoke_test();
    ::crucible::effects::detail::exec_ctx_self_test::runtime_smoke_test();

    return 0;
}
