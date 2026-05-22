// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule F105 (FIXY-V-091):
//
//     marks_ct<F>::value == true
//   ∧ F::type_t is FpFtzPinned<PreserveSubnormals, U>
//   ⇒ ill-formed
//
// Plain English: a constant-time function MUST NOT wrap its type in
// FpFtzPinned<PreserveSubnormals>.  FTZ=0 (output denormals NOT
// flushed) introduces a 30-100× slowdown when an FP operation
// PRODUCES a denormal result — the microcode trap that materializes
// the gradual-underflow value is data-dependent on the result
// magnitude, leaking result-bucket information through cycle count.
// Dual to F104 (which catches INPUT-side denormal trapping); F105
// catches the OUTPUT-side dual.
//
// HS14 substrate-side rejection gate per CLAUDE.md §XVI: V-091 hardening
// patch — completes per-rule neg-compile coverage for all five new
// rules (F101..F105).  This fixture pins F105 specifically — same
// TOP-AXIS as F104 (CT) but DIFFERENT FP SUB-AXIS (FpFtz vs
// FpDenormalInput, i.e. result-side vs input-side gradual-underflow
// handling).  A refactor that surgically relaxes F105's gate (drops
// the `ct && fp_ftz_preserved` term from `validate()`) reddens CI with
// F105's diagnostic — F104's fixture stays green because it tests the
// input-side rule.
//
// Concrete bug-class this catches: a contributor sees F104 catches the
// DAZ=0 timing side-channel and assumes "FTZ=0 is symmetric so the
// same rule covers it".  Wrong — F104 keys on FpDenormalInput axis
// (INPUT denormal handling), F105 keys on FpFtz axis (OUTPUT denormal
// handling).  They reject DIFFERENT toxic values on DIFFERENT sub-axes
// and BOTH gates must be intact.  The CPU pipelines them
// independently: AMD Zen5 hardware fast-paths input denormal handling
// via MXCSR.DAZ but still microcodes output flush via MXCSR.FZ; the
// timing leak appears on EITHER axis if its DAZ/FZ bit is unset.
// V-091's F105 is the type-system witness for the output-side
// rejection.
//
// Expected diagnostic substring: "F105:"

#include <crucible/safety/Fn.h>
#include <crucible/safety/FpMode.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;
namespace sf = crucible::safety;

namespace neg_collision_f105 {

// CT Fn whose Type is FpFtzPinned<PreserveSubnormals, double> — the
// F105 rejected combination.  Marker trait specialization at file
// scope (below) flips marks_ct to true; the FpFtz wrapper provides
// the inner FP-mode pinning the rule rejects.  Inner T = double
// to discriminate from F104's float carrier — the two fixtures land
// in the same neg-compile harness sweep; using different inner Ts
// keeps the trait specializations and Fn instantiations strictly
// disjoint across fixture TUs.
using Bad = fn::Fn<
    sf::FpFtzPinned<sf::FpFtz::PreserveSubnormals, double>,
                                               // 1  Type — triggers F105
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

}  // namespace neg_collision_f105

// Mark Bad as CT-typed — required to fire F105 (rule guards has_ct_v
// AND FpFtz<PreserveSubnormals>).  Specialization at file scope,
// paralleling F101/F102/F103/F104 fixtures.
namespace crucible::safety::fn::collision {
    template <> struct marks_ct<::neg_collision_f105::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

// Instantiating Bad forces CollisionRules::validate() to fire F105.
[[maybe_unused]] neg_collision_f105::Bad the_fixture{};

int main() { return 0; }
