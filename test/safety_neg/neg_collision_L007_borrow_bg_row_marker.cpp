// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule L007 — MARKER-tier trigger path (FIXY-FOUND-069
// sub-HS14 closure).
//
// Companion fixture to neg_collision_L007_borrow_bg_row.cpp (which
// covers the USAGE-tier trigger via `UsageMode::Borrow`).  L007_OK
// gates on:
//
//   concept L007_OK = !(has_borrow_capture_v<F> &&
//                       row_has_effect_v<F::effect_row_t, Effect::Bg>);
//
// `has_borrow_capture_v<F>` is a 3-way OR-fold over:
//
//   * `F::usage_v == UsageMode::Borrow` (USAGE-tier — direct Fn axis)
//   * `is_borrowed_carrier<F::type_t>::value` (CARRIER-tier — wrapper
//     on type_t)
//   * `marks_borrow_capture<F>::value` (MARKER-tier — author
//     specializes the trait on the offending Fn)
//
// The shipped fixture exercises the USAGE-tier arm; THIS fixture
// exercises the MARKER-tier arm — Usage is Linear (not Borrow), type_t
// is bare int (no Borrowed carrier wrapper), and `marks_borrow_capture`
// is specialized.  The CARRIER-tier arm (Borrowed wrapper on type_t)
// remains uncovered today and is a future sub-HS14 follow-up.
//
// Why both fixtures are required per HS14:
//
//   * A refactor that drops the `F::usage_v == UsageMode::Borrow`
//     check from the OR-fold breaks the USAGE-tier path → caught by
//     the original fixture.
//   * A refactor that drops the `marks_borrow_capture<F>::value` term
//     breaks the MARKER-tier path → caught by THIS fixture.
//   * Without both fixtures, the OR-fold could silently degenerate to
//     a single-arm rule and ship; a downstream Fn that intentionally
//     opts in via the marker (e.g., a custom adapter wrapping a borrow
//     for callback-style invocation) would slip past L007.
//
// Mismatch class: marks_borrow_capture-engaged Linear-usage × Row<Bg>.
// Distinct from the usage-tier class because here the borrow-capture
// signal comes from the trait specialization, not the Fn template
// parameter.  L002 / L003 silent (no marks_async / marks_unscoped_spawn).
// H001/H002/H003/H010 silent (no hot-path marker, no HotPath wrapper).
// R003 silent (Reentrancy is NonReentrant).  B001 silent (cost::Constant).
//
// Expected diagnostic substring: "L007:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_l007_marker {

// A Linear-usage Fn (NOT Borrow → has_borrow_capture_v's Usage arm is
// FALSE) with bare int payload (NOT borrowed-carrier → carrier arm is
// FALSE) and Row<Bg>.  The L007 trigger fires ONLY via the marker arm,
// engaged by the specialization at file scope below.
using Bad = fn::Fn<
    int,                                       // 1  Type — bare int (no Borrowed carrier)
    fn::pred::True,                            // 2  Refinement (trivial)
    fn::UsageMode::Linear,                     // 3  Usage — Linear (NOT Borrow → usage arm
                                                //                of has_borrow_capture_v FALSE)
    fx::Row<fx::Effect::Bg>,                   // 4  EffectRow — Bg atom (L007 secondary trigger)
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost (bounded — H001/B001 silent)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy — non-Coroutine (R003 silent)
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_l007_marker

// Opt in to the MARKER-tier trigger.  has_borrow_capture_v's Usage arm
// is FALSE (Linear) and carrier arm is FALSE (bare int), so the marker
// alone supplies the borrow-capture signal.  Combined with Row<Bg>,
// L007 fires.
namespace crucible::safety::fn::collision {
    template <> struct marks_borrow_capture<::neg_collision_l007_marker::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_l007_marker::Bad the_fixture{};

int main() { return 0; }
