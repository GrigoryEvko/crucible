// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule L004 (Phase B per-Fn rule, fixy-PRIOR-AUDIT):
//
//     Usage == UsageMode::Linear
//   ∧ Lifetime == lifetime::In<RegionTag>
//   ∧ marks_lifetime_region_unprotected<F>::value
//   ⇒ ill-formed
//
// Plain English: a linear-resource Fn whose storage lives in a tagged
// region cannot be moved across calls without a Permission<RegionTag>
// proof — otherwise the linear move could outlive the region and
// produce a dangling consumption.  The L004 anti-marker defaults TRUE
// (unprotected); a binding that has threaded a Permission specializes
// the marker to false, asserting "I carry the proof."
//
// This fixture pairs with example_cntp_frame.cpp: the example
// originally used UsageMode::Linear with lifetime::In<NetworkBufferTag>
// and silently broke compilation when L004 landed.  The fix in the
// example is `UsageMode::Borrow`.  This negative-compile witness pins
// the alternative: a binding that DOES claim Linear over a region tag,
// WITHOUT specializing the protection marker, MUST be rejected at
// instantiation.  Without this witness, a future loosening of L004
// (e.g. dropping `marks_lifetime_region_unprotected` from the gate)
// would silently let the example's old shape compile again.
//
// Expected diagnostic substring: "L004:"

#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_l004 {

// Phantom region tag — exactly the pedagogical shape from
// example_cntp_frame.cpp::NetworkBufferTag.
struct UnprovenRegionTag {};

// Linear × lifetime::In<Tag> — L004's exact combination.  No
// specialization of marks_lifetime_region_unprotected, so the default
// (true) holds and the rule fires.
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage — triggers L004
    fx::Row<>,                                 // 4  EffectRow
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::In<UnprovenRegionTag{}>,     // 7  Lifetime — triggers L004
    fn::source::Sanitized,                     // 8  Source (assume validated)
    fn::trust::Tested,                         // 9  Trust  (assume validated)
    fn::ReprKind::Opaque,                      // 10 Repr (layout-opaque default)
    fn::cost::Constant,                        // 11 Cost
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_l004

// Instantiating Bad forces the §6.8 CollisionRules check to fire L004.
[[maybe_unused]] neg_collision_l004::Bad the_fixture{};

int main() { return 0; }
