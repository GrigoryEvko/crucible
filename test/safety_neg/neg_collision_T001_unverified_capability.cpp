// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule T001 (FIXY-FOUND-070):
//
//     F::usage_v == UsageMode::Capability
//   ∧ F::trust_t is trust::Unverified
//   ⇒ ill-formed
//
// Plain English: UsageMode::Capability mints a non-revocable
// authorization token that downstream consumers treat as legitimate
// proof of authority.  A binding whose provenance is Unverified
// (the FOUND-034 Biba-safe default) cannot establish that authority
// chain — the binding's call path may have originated from untrusted
// code that constructed the capability shape without earning the
// underlying authorization.  This is the canonical privilege-escalation
// pattern: untrusted code mints an authority token, downstream
// consumers honor it.
//
// Gap closed by FOUND-070: before T001 the Trust axis was destructured
// by Fn but read by ZERO §6.8 collision rules.  Every binding could
// declare any UsageMode regardless of provenance, including
// UsageMode::Capability silently passing with Trust::Unverified.
// T001 binds the Trust axis to the Capability shape structurally.
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>` with
// UsageMode::Capability and Trust=Unverified, so Fn's own
// `static_assert(ValidComposition<Fn>)` runs the validate() leg —
// the concept layer ALONE does not gate Fn<> instantiation
// (feedback_collision_catalog_dual_wiring).
//
// Expected diagnostic substring: "T001:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_t001 {

// A Capability binding whose Trust is Unverified — fires T001.
// To isolate T001, the binding avoids every other dimension trip:
//   - Refinement: pred::True (no refinement violation).
//   - EffectRow: empty Row<> (no Bg/Alloc/IO/Block contradiction).
//   - Security: Public (no Classified-flow rule).
//   - Cost: cost::Constant (no HotPath cost-bound rule).
//   - replay marker NOT engaged (S011 stays silent).
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement (trivial)
    fn::UsageMode::Capability,                 // 3  Usage — Capability (mints
                                                //                authority token)
    fx::Row<>,                                 // 4  EffectRow — empty
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Unverified,                     // 9  Trust — UNVERIFIED (the
                                                //                FOUND-034 default;
                                                //                T001 trigger)
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost (bounded)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_t001

[[maybe_unused]] neg_collision_t001::Bad the_fixture{};

int main() { return 0; }
