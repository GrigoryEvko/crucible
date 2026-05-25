// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule F002 (Phase B per-Fn collision asserts):
//
//     marks_federation_peer<F>::value == true
//   ∧ is_unbounded_cost<F::cost_t>::value == true
//   ⇒ ill-formed
//
// Plain English: a federation peer (Canopy peer / Cipher cold-tier
// participant / cross-org federation worker) MUST declare a wall-clock
// budget.  An unbounded federation peer cannot be safely admitted into
// the mesh — it could monopolize the federation channel or run
// indefinitely without termination.  Declare cost::Linear<N> with a
// concrete bound matched to the federation protocol's deadline.
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>`, so Fn's
// own `static_assert(ValidComposition<Fn>)` runs the validate() leg.
// FIXY-FOUND-068 first-fixture: this graduates F002 from ZERO HS14
// fixture coverage.
//
// Expected diagnostic substring: "F002:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_f002 {

// A Fn with marks_federation_peer + cost::Unbounded.  marks_hot_path
// is FALSE so H001 / H002 / H003 do not fire.  F002 alone catches the
// federation-peer-unbounded shape.
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
    fn::cost::Unbounded,                       // 11 Cost — UNBOUNDED (F002 trigger paired
                                                //                with marks_federation_peer)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_f002

// Mark Bad as federation peer AT FILE SCOPE — composed with
// cost::Unbounded already in the type.
namespace crucible::safety::fn::collision {
    template <> struct marks_federation_peer<::neg_collision_f002::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_f002::Bad the_fixture{};

int main() { return 0; }
