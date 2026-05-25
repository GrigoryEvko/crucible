// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule H001 (Phase B per-Fn collision asserts):
//
//     marks_hot_path<F>::value == true
//   ∧ is_unbounded_cost<F::cost_t>::value == true
//   ⇒ ill-formed
//
// Plain English: a hot-path function MUST justify its compute envelope;
// declaring cost::Unstated or cost::Unbounded on a HotPath binding
// breaches the ≤40ns intra-socket discipline (CLAUDE.md §IX).  The
// hot path is the production p99 envelope — every operation declares
// a bound or is review-rejected.  H001 closes the gap structurally:
// no HotPath binding compiles with an unbounded cost claim.
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>`, so Fn's
// own `static_assert(ValidComposition<Fn>)` runs the validate() leg.
// FIXY-FOUND-068 first-fixture: this graduates H001 from ZERO HS14
// fixture coverage.
//
// Expected diagnostic substring: "H001:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_h001 {

// A Fn with marks_hot_path + cost::Unbounded.  H002 (hot_path ×
// trivial refinement) also fires per the H010-fixture precedent;
// validate() static_assert chain hits H001 regardless of first_failure
// ordering, and the WILL_FAIL PASS_REGULAR_EXPRESSION matches "H001:"
// in the diagnostic chain.
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage
    fx::Row<>,                                 // 4  EffectRow — empty (no Bg/Alloc/IO)
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Unbounded,                       // 11 Cost — UNBOUNDED (H001 trigger
                                                //                paired with marks_hot_path)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_h001

// Mark Bad as hot-path AT FILE SCOPE — composed with cost::Unbounded
// already in the type.  H001 fires on the (marks_hot_path,
// unbounded_cost) pair.
namespace crucible::safety::fn::collision {
    template <> struct marks_hot_path<::neg_collision_h001::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_h001::Bad the_fixture{};

int main() { return 0; }
