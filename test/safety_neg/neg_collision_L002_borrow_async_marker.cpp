// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule L002 — MARKER × MARKER composition path
// (FIXY-FOUND-069 sub-HS14 closure).
//
// Companion fixture to neg_collision_L002_borrow_async.cpp (which
// covers the USAGE-arm × MARKER-arm composition via UsageMode::Borrow
// + marks_async).  L002_OK gates on:
//
//   concept L002_OK = !(has_borrow_capture_v<F> && has_async_v<F>);
//
// Both predicates are 3-way OR-folds:
//
//   has_borrow_capture_v<F> = (Usage::Borrow ||
//                              is_borrowed_carrier<type_t> ||
//                              marks_borrow_capture<F>)
//
//   has_async_v<F>         = (Reentrancy::Coroutine ||
//                              row_has_effect<Bg> ||
//                              marks_async<F>)
//
// The shipped fixture exercises the (Usage-arm × marker-arm) cell of
// this 3×3 grid.  THIS fixture exercises the (marker-arm × marker-arm)
// cell — both predicates fire via their respective marker arms, with
// the Fn axes deliberately neutral on the other arms (Linear usage,
// bare int payload, Row<> empty, NonReentrant reentrancy).
//
// Why both fixtures are required per HS14:
//
//   * A refactor that drops the marker arm from has_borrow_capture_v
//     breaks the marker-arm path → caught by THIS fixture.
//   * A refactor that drops the Usage::Borrow arm breaks the
//     usage-arm path → caught by the original fixture.
//   * Without both, the OR-fold could silently degenerate; a
//     downstream adapter opting in via marks_borrow_capture would
//     slip past L002.
//
// Mismatch class: Linear usage + marks_borrow_capture + marks_async.
// Distinct from the shipped fixture's class because BOTH predicates
// here fire via their marker arms (not the usage arm).  L003 silent
// (no marks_unscoped_spawn).  L007 silent (Row<> empty, no Bg).
// L004/L006 silent (no region lifetime / no longjmp).  R-family
// silent (Reentrancy::NonReentrant).  H/W/V/S silent (no respective
// wrappers/markers).
//
// Expected diagnostic substring: "L002:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_l002_marker {

// A Linear-usage Fn (NOT Borrow → has_borrow_capture_v's Usage arm
// FALSE) with bare int payload (NOT borrowed-carrier → carrier arm
// FALSE), Row<> empty (has_async_v's Bg-row arm FALSE), Reentrancy::
// NonReentrant (has_async_v's Coroutine arm FALSE).  Both predicates
// fire ONLY via their marker arms, engaged at file scope below.
using Bad = fn::Fn<
    int,                                       // 1  Type — bare int (no Borrowed carrier)
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage — Linear (NOT Borrow → usage arm
                                                //                of has_borrow_capture_v FALSE)
    fx::Row<>,                                 // 4  EffectRow — empty (Bg-arm of has_async_v
                                                //                FALSE; L007 silent; B001 silent)
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost (bounded — H001/B001 silent)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy — NOT Coroutine (Coroutine arm
                                                //                of has_async_v FALSE; R-family silent)
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_l002_marker

// Engage BOTH marker arms — has_borrow_capture_v via marks_borrow_capture
// and has_async_v via marks_async.  L002 fires on the conjunction of
// these two marker-arm-engaged predicates.  L003 silent
// (no marks_unscoped_spawn).
namespace crucible::safety::fn::collision {
    template <> struct marks_borrow_capture<::neg_collision_l002_marker::Bad>
        : std::true_type {};
    template <> struct marks_async<::neg_collision_l002_marker::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_l002_marker::Bad the_fixture{};

int main() { return 0; }
