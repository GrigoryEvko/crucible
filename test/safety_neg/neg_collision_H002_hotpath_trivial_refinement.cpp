// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule H002 (Phase B per-Fn collision asserts):
//
//     marks_hot_path<F>::value == true
//   ∧ is_trivial_refinement<F::refinement_t>::value == true
//     (i.e., refinement_t == pred::True)
//   ⇒ ill-formed
//
// Plain English: a hot-path function MUST attach a Refined<predicate,
// Type> witness floor — review rejects pred::True (no invariant) on
// HotPath bindings.  The hot path assumes invariants (aligned, in-range,
// non-zero) that the body uses for fast-path branches; pred::True is
// the "no witness" sentinel.
//
// PRODUCTION-PATH: this instantiates a real `fn::Fn<...>`, so Fn's
// own `static_assert(ValidComposition<Fn>)` runs the validate() leg.
// FIXY-FOUND-068 first-fixture: this graduates H002 from ZERO HS14
// fixture coverage.
//
// Expected diagnostic substring: "H002:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_h002 {

// A Fn with marks_hot_path + pred::True refinement + cost::Constant.
// H001 does not fire (cost is bounded).  H002 catches the trivial-
// refinement-on-hot-path shape.
using Bad = fn::Fn<
    int,                                       // 1  Type
    fn::pred::True,                            // 2  Refinement — TRIVIAL (H002 trigger
                                                //                paired with marks_hot_path)
    fn::UsageMode::Linear,                     // 3  Usage
    fx::Row<>,                                 // 4  EffectRow — empty (no Bg/Alloc/IO)
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost (bounded — H001 silent)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_h002

namespace crucible::safety::fn::collision {
    template <> struct marks_hot_path<::neg_collision_h002::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_h002::Bad the_fixture{};

int main() { return 0; }
