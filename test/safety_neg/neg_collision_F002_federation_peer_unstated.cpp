// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule F002 — UNSTATED-cost arm (FIXY-FOUND-069 sub-HS14
// closure).
//
// Companion fixture to neg_collision_F002_federation_peer_unbounded.cpp
// (which covers the cost::Unbounded arm).  F002_OK gates on:
//
//   concept F002_OK = !(marks_federation_peer<F>::value &&
//                       is_unbounded_cost<F::cost_t>::value);
//
// `is_unbounded_cost<C>` is a 2-specialization OR-detector:
//
//   * is_unbounded_cost<cost::Unbounded> : true_type   (explicit ∞ budget)
//   * is_unbounded_cost<cost::Unstated>  : true_type   (NO budget declared)
//
// The shipped fixture exercises the cost::Unbounded arm; THIS fixture
// exercises the cost::Unstated arm.  Both are "unbounded" to F002 — a
// federation peer that never declares a wall-clock budget is exactly as
// inadmissible as one that declares an explicit infinite budget.
//
// Why both fixtures are required per HS14:
//
//   * A refactor that drops the `is_unbounded_cost<cost::Unbounded>`
//     specialization breaks the explicit-∞ path → caught by the original
//     fixture.
//   * A refactor that drops the `is_unbounded_cost<cost::Unstated>`
//     specialization breaks the no-budget path → caught by THIS fixture.
//   * Without both, the detector could silently degenerate to a
//     single-value rule and ship; a federation peer that simply omits
//     its budget (the common authoring mistake) would then slip past
//     F002 and be admitted into the mesh unbounded.
//
// Mismatch class: marks_federation_peer × cost::Unstated.  Distinct from
// the Unbounded class because the "unbounded" signal comes from the
// ABSENCE of a budget declaration, not an explicit infinite one.  H001 /
// H002 / H003 silent (no hot-path marker).  B001 silent (no Bg row, no
// observable).
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>`, so Fn's own
// `static_assert(ValidComposition<Fn>)` runs the validate() leg — the
// concept layer ALONE does not gate Fn<> instantiation
// (feedback_collision_catalog_dual_wiring).
//
// Expected diagnostic substring: "F002:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_f002_unstated {

// A Fn with marks_federation_peer + cost::Unstated (NO budget declared).
// marks_hot_path is FALSE so H001 / H002 / H003 do not fire; Row<> is
// empty so B001 (Bg × observable × unbounded) does not fire.  F002 alone
// catches the federation-peer-without-budget shape.
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage
    fx::Row<>,                                 // 4  EffectRow — empty
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Unstated,                        // 11 Cost — UNSTATED (F002 trigger via the
                                                //                no-budget arm of is_unbounded_cost)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_f002_unstated

// Mark Bad as federation peer AT FILE SCOPE — composed with cost::Unstated
// already in the type.
namespace crucible::safety::fn::collision {
    template <> struct marks_federation_peer<::neg_collision_f002_unstated::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_f002_unstated::Bad the_fixture{};

int main() { return 0; }
