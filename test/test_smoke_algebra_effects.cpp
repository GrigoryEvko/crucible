// ── fixy-A3-021 sentinel TU ───────────────────────────────────────────
//
// Closes the header-only static_assert blind spot
// (feedback_header_only_static_assert_blind_spot.md) for the eight
// algebra/* and effects/* headers that previously shipped only
// consteval / static_assert coverage of their inline bodies.  Each
// header now declares `inline void runtime_smoke_test()` in its
// detail-namespace `*_self_test` block; this TU includes each header
// (forcing the project warning flags through the inline bodies) and
// calls each smoke test from main(), proving the runtime branch
// stays callable.
//
// Per feedback_algebra_runtime_smoke_test_discipline.md, pure
// static_assert coverage masks three real bug classes:
//   1. consteval-vs-constexpr regressions in helpers downstream code
//      calls at runtime (e.g. Graded<>'s contract precondition path),
//   2. SFINAE-only paths that compile cleanly under static_assert but
//      fail to instantiate the inline body, and
//   3. shadow / unused-but-set diagnostics that only fire when the
//      project's `-W*` flags actually run over the body's tokens.
//
// All eight runtime_smoke_test() callees are noexcept-ish (defined
// `inline void`); their failure mode is compile / link error, not
// runtime trap, so `main()` just sequences them and returns 0.

#include <crucible/algebra/GradedTrait.h>
#include <crucible/algebra/Lattice.h>
#include <crucible/algebra/Modality.h>
#include <crucible/effects/Capabilities.h>
#include <crucible/effects/Concurrent.h>
#include <crucible/effects/EffectRow.h>
#include <crucible/effects/ExecCtx.h>
#include <crucible/effects/Resources.h>

int main() {
    // ── algebra/ smoke tests ─────────────────────────────────────
    ::crucible::algebra::detail::lattice_self_test::runtime_smoke_test();
    ::crucible::algebra::detail::modality_self_test::runtime_smoke_test();
    ::crucible::algebra::detail::is_graded_specialization_self_test::
        runtime_smoke_test();

    // ── effects/ smoke tests ─────────────────────────────────────
    ::crucible::effects::detail::capabilities_self_test::runtime_smoke_test();
    ::crucible::effects::detail::resources_self_test::runtime_smoke_test();
    ::crucible::effects::detail::concurrent_row_self_test::
        runtime_smoke_test();
    ::crucible::effects::detail::effect_row_self_test::runtime_smoke_test();
    ::crucible::effects::detail::exec_ctx_self_test::runtime_smoke_test();

    return 0;
}
