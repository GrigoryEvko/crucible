// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule F104 (FIXY-V-091):
//
//     marks_ct<F>::value == true
//   ∧ F::type_t is FpDenormalInputPinned<HonorDenormals, U>
//   ⇒ ill-formed
//
// Plain English: a constant-time function MUST NOT wrap its type in
// FpDenormalInputPinned<HonorDenormals>.  DAZ=0 (denormals NOT flushed
// at input) introduces a 30-100× cycle-count delta whenever the input
// IS a denormal — a textbook FP timing side-channel that leaks
// information about input magnitude through wall-clock latency on
// every supported CPU/GPU pipeline (x86 SSE/AVX, ARM SVE, NVIDIA
// Hopper/Blackwell, AMD CDNA3+, Intel XMX).
//
// HS14 substrate-side rejection gate per CLAUDE.md §XVI: V-091's load-
// bearing claim is "CT carriers REJECT toxic FP-mode values along the
// FpDenormalInput sub-axis".  Pairs with
// neg_collision_f101_replay_fp_reassoc.cpp for the 2-fixture HS14
// floor — one fixture per distinct mismatch class:
//   1. F101 — Replay axis × FpReassociate sub-axis.
//   2. F104 — CT axis × FpDenormalInput sub-axis (this).
// Distinct axes (Replay vs CT) AND distinct sub-axes (Reassociate vs
// DenormalInput) — the two fixtures occupy genuinely different cells
// in the rejection lattice, not duplicate witnesses of one mismatch
// class.
//
// Concrete bug-class this catches: a contributor relaxes F104_OK in
// CollisionCatalog (drops the has_ct_v term, switches the
// FpDenormalInput detector to permissive, or weakens the
// wraps_fp_axis_mode partial spec) — CT carriers then silently accept
// HonorDenormals wrappers and CT-discipline regressions ship without
// a type-system witness pointing at the offending Fn.  This fixture
// pins F104 at the source-code declaration boundary where the
// timing-independence contract begins.
//
// Expected diagnostic substring: "F104:"

#include <crucible/safety/Fn.h>
#include <crucible/safety/FpMode.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;
namespace sf = crucible::safety;

namespace neg_collision_f104 {

// CT Fn whose Type is FpDenormalInputPinned<HonorDenormals, float> —
// the F104 rejected combination.  Marker trait specialization at file
// scope (below) flips marks_ct to true; the FpDenormalInput wrapper
// provides the inner FP-mode pinning the rule rejects.
using Bad = fn::Fn<
    sf::FpDenormalInputPinned<sf::FpDenormalInput::HonorDenormals, float>,
                                               // 1  Type — triggers F104
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
    fn::space::Bounded<sizeof(float)>,         // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(float)>,        // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_f104

// Mark Bad as CT-typed — required to fire F104 (rule guards
// has_ct_v AND FpDenormalInput<HonorDenormals>).  Specialization at
// file scope, paralleling W001/W002/F101 fixtures.
namespace crucible::safety::fn::collision {
    template <> struct marks_ct<::neg_collision_f104::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

// Instantiating Bad forces CollisionRules::validate() to fire F104.
[[maybe_unused]] neg_collision_f104::Bad the_fixture{};

int main() { return 0; }
