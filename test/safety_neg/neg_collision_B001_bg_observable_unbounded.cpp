// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule B001 (Phase B per-Fn collision asserts):
//
//     row_has_effect_v<F::effect_row_t, Effect::Bg> == true
//   ∧ marks_externally_observable<F>::value == true
//   ∧ is_unbounded_cost<F::cost_t>::value == true
//   ⇒ ill-formed
//
// Plain English: a Bg-context binding that is externally observable
// AND carries unbounded cost is a back-pressure trap.  Bg threads
// produce observable output (logs, metrics, signals) that downstream
// consumers depend on; unbounded cost means the output cadence is
// unbounded too, which back-presses any consumer with a finite queue.
// Declare cost::Linear<N> (or stricter) to bound the production rate.
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>`, so Fn's
// own `static_assert(ValidComposition<Fn>)` runs the validate() leg.
// FIXY-FOUND-068 first-fixture: this graduates B001 from ZERO HS14
// fixture coverage.
//
// Expected diagnostic substring: "B001:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_b001 {

// A Fn with Row<Bg> + marks_externally_observable + cost::Unbounded.
// marks_hot_path is FALSE so H001 / H003 / H010 do not fire.
// B001 catches the Bg-observable-unbounded back-pressure shape.
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
    fn::cost::Unbounded,                       // 11 Cost — UNBOUNDED (B001 trigger paired
                                                //                with Bg + externally_observable)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_b001

// Mark Bad as externally observable AT FILE SCOPE — composed with
// Row<Bg> + cost::Unbounded already in the type.
namespace crucible::safety::fn::collision {
    template <> struct marks_externally_observable<::neg_collision_b001::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_b001::Bad the_fixture{};

int main() { return 0; }
