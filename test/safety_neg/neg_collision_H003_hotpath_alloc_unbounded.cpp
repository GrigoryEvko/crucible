// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule H003 (Phase B per-Fn collision asserts):
//
//     marks_hot_path<F>::value == true
//   ∧ (row_has_effect<Alloc> ∨ row_has_effect<IO>)
//   ∧ is_unbounded_cost<F::cost_t>::value == true
//   ⇒ ill-formed
//
// Plain English: a hot-path function with Alloc or IO in its row AND
// unbounded cost is structurally inadmissible.  H001 catches the
// generic HotPath × unbounded cost; H003 specifically catches the
// HotPath × (Alloc | IO) × unbounded cost three-axis composition,
// where the unbounded cost would manifest through the Alloc/IO
// pathway specifically.  Either the binding should run in Bg/Init
// context (where Alloc/IO is welcome) OR the cost must be bounded.
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>`, so Fn's
// own `static_assert(ValidComposition<Fn>)` runs the validate() leg.
// FIXY-FOUND-068 first-fixture: this graduates H003 from ZERO HS14
// fixture coverage.
//
// Expected diagnostic substring: "H003:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_h003 {

// A Fn with marks_hot_path + Row<Alloc> + cost::Unbounded.  H001
// co-fires (any HotPath × unbounded); H003 alone catches the
// Alloc-path specific shape.  Validate() static_assert chain runs
// all asserts so "H003:" appears in the diagnostic regardless of
// first_failure ordering.
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage
    fx::Row<fx::Effect::Alloc>,                // 4  EffectRow — Alloc engaged (H003 trigger
                                                //                paired with HotPath + Unbounded)
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Unbounded,                       // 11 Cost — UNBOUNDED (H003 trigger paired
                                                //                with marks_hot_path + Alloc)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_h003

namespace crucible::safety::fn::collision {
    template <> struct marks_hot_path<::neg_collision_h003::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_h003::Bad the_fixture{};

int main() { return 0; }
