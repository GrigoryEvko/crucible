// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule B001 — UNSTATED-cost trigger path
// (FIXY-FOUND-069 sub-HS14 closure).
//
// Companion fixture to neg_collision_B001_bg_observable_unbounded.cpp
// (which engages B001 via cost::Unbounded).  B001_OK gates on:
//
//   concept B001_OK = !(row_has_effect_v<effect_row_t, Effect::Bg> &&
//                       marks_externally_observable<F>::value &&
//                       is_unbounded_cost<F::cost_t>::value);
//
// The third conjunct `is_unbounded_cost<C>` is a 2-way OR-fold over
// TWO distinct cost types:
//
//   template <> struct is_unbounded_cost<cost::Unstated>  : std::true_type {};
//   template <> struct is_unbounded_cost<cost::Unbounded> : std::true_type {};
//
// The shipped fixture exercises the `cost::Unbounded` arm; THIS
// fixture exercises the `cost::Unstated` arm.  All other axes match
// the shipped fixture for direct comparability and sole-firing.
//
// Why both fixtures are required per HS14:
//
//   * A refactor that drops the `cost::Unstated` specialization from
//     is_unbounded_cost (e.g., reclassifying Unstated as "treat as
//     0-cost until measured") would silently let Bg-observable
//     Unstated-cost bindings slip past B001 — caught ONLY by THIS
//     fixture.
//   * A refactor that drops the `cost::Unbounded` specialization
//     would symmetrically miss Bg-observable Unbounded-cost
//     bindings — caught by the shipped fixture.
//   * Without both, the 2-way OR-fold can silently degenerate to a
//     single-arm rule and ship; either flavor of "unbounded" cost
//     would slip through B001's gate, reopening the back-pressure
//     trap the rule was designed to catch.
//
// Mismatch class: Bg-row + marks_externally_observable + cost::Unstated.
// Distinct from the shipped fixture's class because the third
// conjunct's truth here comes from the Unstated specialization, not
// from the Unbounded specialization.  Sole-firing: no HotPath signal
// (H001/H003 silent), no marks_federation_peer (F002 silent), no
// other markers (L/R/W/V/S/I/E/M/N/P silent under shipped-fixture-
// matching axes).
//
// Expected diagnostic substring: "B001:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_b001_unstated {

// A Fn with Row<Bg> + marks_externally_observable + cost::Unstated.
// marks_hot_path is FALSE so H001 / H003 / H010 do not fire.
// marks_federation_peer is FALSE so F002 does not fire.
// B001 catches the Bg-observable-Unstated back-pressure shape
// (unmeasured cost is conservatively treated as unbounded for the
// purpose of B001's back-pressure analysis).
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage
    fx::Row<fx::Effect::Bg>,                   // 4  EffectRow — Bg engaged (B001 trigger)
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Unstated,                        // 11 Cost — UNSTATED (B001 trigger paired
                                                //                with Bg + externally_observable;
                                                //                distinct arm of is_unbounded_cost
                                                //                from the shipped Unbounded fixture)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_b001_unstated

// Mark Bad as externally observable AT FILE SCOPE — composed with
// Row<Bg> + cost::Unstated already in the type.  Three-conjunct
// composition fires B001 via the Unstated arm of is_unbounded_cost.
namespace crucible::safety::fn::collision {
    template <> struct marks_externally_observable<
        ::neg_collision_b001_unstated::Bad
    > : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_b001_unstated::Bad the_fixture{};

int main() { return 0; }
