// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule L006 — MARKER-TIER trigger path (FIXY-FOUND-069).
//
// Companion fixture to neg_collision_L006_linear_longjmp.cpp (which
// covers the WRAPPER-TIER trigger via `sf::ControlFlowPinned<MayLongjmp,
// int>`).  L006_OK rejects on EITHER of two OR-folded paths:
//
//   concept L006_OK = !(F::usage_v == UsageMode::Linear &&
//                       (marks_longjmp_unsafe<F>::value ||
//                        cf_at_or_above_v<MayLongjmp, F::type_t>));
//
// The shipped fixture exercises the second arm
// (`cf_at_or_above_v<MayLongjmp, type_t>`).  THIS fixture exercises the
// FIRST arm (`marks_longjmp_unsafe<F>::value`) — a Linear-usage Fn
// whose payload Type carries NO ControlFlow wrapper (bare int) but is
// opted in via the `marks_longjmp_unsafe` specialization.
//
// Why both fixtures are required per HS14:
//
//   * A refactor that drops the `cf_at_or_above_v<MayLongjmp, type_t>`
//     term breaks the WRAPPER-tier path → caught by the original
//     fixture.
//   * A refactor that drops the `marks_longjmp_unsafe<F>::value` term
//     breaks the MARKER-tier path → caught by THIS fixture.
//   * Without both fixtures, the OR-fold can silently degenerate to a
//     single-trigger rule and ship.
//
// Mismatch class: Linear × marker-engaged-without-wrapper.  Distinct
// from the wrapper-tier class because here `F::type_t` is bare `int` —
// `cf_at_or_above_v<MayLongjmp, int>` is FALSE; only the marker arm
// supplies the longjmp signal.
//
// Expected diagnostic substring: "L006:".

#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_l006_marker {

using Bad = fn::Fn<
    int,                                       // 1  Type — bare int (no ControlFlow wrapper)
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage — the Linear axis (load-bearing)
    fx::Row<>,                                 // 4  EffectRow
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost — bounded (avoids H-family)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy — non-Coroutine (avoids R001/R002/R003/E044)
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_l006_marker

// Opt in to the marker-tier trigger.  No ControlFlow wrapper on Type;
// the marker alone supplies the longjmp signal that, combined with
// Linear usage, trips L006.
namespace crucible::safety::fn::collision {
    template <> struct marks_longjmp_unsafe<::neg_collision_l006_marker::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_l006_marker::Bad the_fixture{};

int main() { return 0; }
