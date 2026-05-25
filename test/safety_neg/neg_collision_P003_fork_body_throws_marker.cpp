// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule P003 — MARKER-TIER trigger path (FIXY-FOUND-069).
//
// Companion fixture to neg_collision_P003_fork_body_throws.cpp (which
// covers the WRAPPER-TIER trigger via `sf::ControlFlowPinned<ThrowOnly,
// int>`).  P003_OK rejects on EITHER of two OR-folded inner-arm paths
// (gated by the outer marks_fork_worker marker):
//
//   concept P003_OK = !(marks_fork_worker<F>::value &&
//                       (marks_throws<F>::value ||
//                        cf_at_or_above_v<ThrowOnly, F::type_t>));
//
// The shipped fixture exercises the second inner arm
// (`cf_at_or_above_v<ThrowOnly, type_t>`).  THIS fixture exercises the
// FIRST inner arm (`marks_throws<F>::value`) — a fork-worker-marked Fn
// whose payload Type carries NO ControlFlow wrapper (bare int) but is
// opted in via the `marks_throws` specialization.
//
// Why both fixtures are required per HS14:
//
//   * A refactor that drops the `cf_at_or_above_v<ThrowOnly, type_t>`
//     term breaks the WRAPPER-tier path → caught by the original
//     fixture.
//   * A refactor that drops the `marks_throws<F>::value` term breaks
//     the MARKER-tier path → caught by THIS fixture.
//   * Without both fixtures, the OR-fold can silently degenerate to a
//     single-trigger rule and the jthread-terminate path V-087 closed
//     could re-open.
//
// Mismatch class: fork-worker × marks_throws marker without wrapper.
// Distinct from the wrapper-tier class because here `F::type_t` is
// bare `int` — `cf_at_or_above_v<ThrowOnly, int>` is FALSE; only the
// marker arm supplies the throw signal.
//
// Expected diagnostic substring: "P003:".

#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;

namespace neg_collision_p003_marker {

// Bare int — no ControlFlow wrapper.  Defaults to safe ControlFlow tier
// (Pure / 0) so cf_at_or_above_v<ThrowOnly, int> is FALSE.  All other
// Fn axes take their Fn-template defaults.
using Bad = fn::Fn<int>;

}  // namespace neg_collision_p003_marker

// Two marker specializations, both required:
//   (1) marks_fork_worker — the outer gate (this binding IS a
//       permission_fork worker body)
//   (2) marks_throws — the MARKER-tier inner arm (this binding throws,
//       structurally NOT typed via ControlFlowPinned)
//
// Combined: P003 must fire via the marker-OR arm.
namespace crucible::safety::fn::collision {
    template <> struct marks_fork_worker<::neg_collision_p003_marker::Bad>
        : std::true_type {};
    template <> struct marks_throws<::neg_collision_p003_marker::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_p003_marker::Bad the_fixture{};

int main() { return 0; }
