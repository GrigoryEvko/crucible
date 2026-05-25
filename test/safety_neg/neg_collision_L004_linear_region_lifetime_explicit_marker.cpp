// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule L004 — EXPLICIT-MARKER trigger path
// (FIXY-FOUND-069 sub-HS14 closure).
//
// Companion fixture to neg_collision_L004_linear_region_lifetime.cpp
// (which relies on the default-TRUE behavior of the
// marks_lifetime_region_unprotected anti-marker).  L004_OK gates on
// the 3-axis conjunction:
//
//   concept L004_OK = !(F::usage_v == UsageMode::Linear &&
//                       is_region_lifetime<F::lifetime_t>::value &&
//                       marks_lifetime_region_unprotected<F>::value);
//
// The `marks_lifetime_region_unprotected` anti-marker defaults TRUE —
// the "guilty until proven innocent" rule.  A Permission-threaded
// binding specializes the marker to FALSE to claim "I carry the
// proof."  The shipped fixture engages L004 implicitly: no
// specialization, default-TRUE behavior fires the third conjunct.
// THIS fixture engages L004 EXPLICITLY: the anti-marker is
// specialized to std::true_type at file scope below.
//
// Why both fixtures are required per HS14:
//
//   * A refactor changing the marker's default from TRUE to FALSE
//     (e.g., flipping the "guilty by default" policy) would silently
//     pass the shipped fixture's Fn instantiation — caught only by
//     THIS fixture's explicit specialization.
//   * A refactor that auto-derives the marker from another axis
//     (e.g., reading source_v or trust_v) would break the explicit
//     specialization path while keeping the default path intact —
//     caught only by THIS fixture if the explicit specialization is
//     respected, otherwise caught by the shipped fixture.
//   * Without both fixtures, a default-policy flip OR a derivation-
//     based refactor can silently slip past L004.
//
// Mismatch class: Linear + lifetime::In<Tag> + EXPLICIT
// marks_lifetime_region_unprotected specialization (= true_type).
// Distinct from the shipped fixture's class because the third
// conjunct's truth here is provably explicit, not provably default.
// All other axes match the shipped fixture for sole-firing.
//
// Expected diagnostic substring: "L004:".

#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_l004_explicit_marker {

// Distinct phantom region tag so this fixture's Fn doesn't collide
// with the shipped fixture's Bad type at namespace scope.
struct ExplicitlyUnprovenRegionTag {};

// Linear × lifetime::In<Tag> — same shape as the shipped fixture,
// but the anti-marker is engaged via explicit specialization below
// rather than via default-TRUE behavior.
using Bad = fn::Fn<
    int,                                                  // 1  Type
    fn::pred::True,                                       // 2  Refinement
    fn::UsageMode::Linear,                                // 3  Usage — triggers L004
    fx::Row<>,                                            // 4  EffectRow
    fn::SecLevel::Public,                                 // 5  Security
    fn::proto::None,                                      // 6  Protocol
    fn::lifetime::In<ExplicitlyUnprovenRegionTag{}>,      // 7  Lifetime — triggers L004
    fn::source::Sanitized,                                // 8  Source
    fn::trust::Tested,                                    // 9  Trust
    fn::ReprKind::Opaque,                                 // 10 Repr
    fn::cost::Constant,                                   // 11 Cost (bounded — B001 silent)
    fn::precision::Exact,                                 // 12 Precision
    fn::space::Bounded<sizeof(int)>,                      // 13 Space
    fn::OverflowMode::Trap,                               // 14 Overflow
    fn::MutationMode::Immutable,                          // 15 Mutation
    fn::ReentrancyMode::NonReentrant,                     // 16 Reentrancy — non-Coroutine (R-family silent)
    fn::size_pol::Sized<sizeof(int)>,                     // 17 Size
    /*Version=*/1,                                        // 18 Version
    fn::stale::Fresh                                      // 19 Staleness
>;

}  // namespace neg_collision_l004_explicit_marker

// EXPLICITLY engage the anti-marker.  The default-TRUE value would
// already fire L004; this specialization makes the engagement
// provably explicit rather than provably default — defending against
// a future default-flip OR derivation-based refactor of the marker.
namespace crucible::safety::fn::collision {
    template <>
    struct marks_lifetime_region_unprotected<
        ::neg_collision_l004_explicit_marker::Bad
    > : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_l004_explicit_marker::Bad the_fixture{};

int main() { return 0; }
