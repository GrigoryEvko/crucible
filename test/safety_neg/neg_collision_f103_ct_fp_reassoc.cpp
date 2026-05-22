// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule F103 (FIXY-V-091):
//
//     marks_ct<F>::value == true
//   ∧ F::type_t is FpReassociatePinned<UnrestrictedRewrite, U>
//   ⇒ ill-formed
//
// Plain English: a constant-time function MUST NOT wrap its type in
// FpReassociatePinned<UnrestrictedRewrite>.  Reassociation introduces a
// data-dependent reduction-tree topology — the compiler is free to fold
// (a+b)+c as (a+b)+c on one input and a+(b+c) on another based on
// magnitude / sparsity / register-pressure heuristics.  Each topology
// has a different critical-path length and a different number of
// dependent FP adds, so the wall-clock cycle count diverges by input
// — a textbook FP timing side-channel even when the bit result is
// IEEE-tolerable.
//
// HS14 substrate-side rejection gate per CLAUDE.md §XVI: V-091 hardening
// patch — companion to F101/F102/F104/F105 fixtures.  Per-rule
// neg-compile witnesses now cover all five new rules.  This fixture
// pins F103 specifically — same SUB-AXIS as F101 (FpReassociate) but
// DIFFERENT TOP-AXIS (CT vs Replay-required).  A refactor that
// surgically relaxes F103's gate (drops the `ct &&
// fp_reassoc_unrestricted` term from `validate()`) reddens CI with
// F103's diagnostic specifically — F101's fixture stays green because
// it doesn't toggle marks_ct, and F103's surfaces.
//
// Concrete bug-class this catches: a contributor sees F101 and assumes
// "if Reassoc is gated on Replay, the same Reassoc value is fine on CT
// paths".  This is wrong — Replay is about cross-vendor bit-exactness;
// CT is about data-independent timing.  They reject reassociation for
// DIFFERENT reasons and BOTH gates must be intact.  V-091's F103 is the
// type-system witness for the CT-side rejection.
//
// Expected diagnostic substring: "F103:"

#include <crucible/safety/Fn.h>
#include <crucible/safety/FpMode.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;
namespace sf = crucible::safety;

namespace neg_collision_f103 {

// CT Fn whose Type is FpReassociatePinned<UnrestrictedRewrite, double>
// — the F103 rejected combination.  Marker trait specialization at
// file scope (below) flips marks_ct to true; the FpReassociate wrapper
// provides the inner FP-mode pinning the rule rejects.  Inner T =
// double to discriminate from F101's float carrier in case both
// fixture TUs land in the same translation-unit footprint during a
// project-wide rebuild — keeps trait specializations independent.
using Bad = fn::Fn<
    sf::FpReassociatePinned<sf::FpReassociate::UnrestrictedRewrite, double>,
                                               // 1  Type — triggers F103
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage
    fx::Row<>,                                 // 4  EffectRow
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(double)>,        // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(double)>,       // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_f103

// Mark Bad as CT-typed — required to fire F103 (rule guards has_ct_v
// AND FpReassociate<UnrestrictedRewrite>).  Specialization at file
// scope, paralleling F101/F102/F104/F105 fixtures.
namespace crucible::safety::fn::collision {
    template <> struct marks_ct<::neg_collision_f103::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

// Instantiating Bad forces CollisionRules::validate() to fire F103.
[[maybe_unused]] neg_collision_f103::Bad the_fixture{};

int main() { return 0; }
