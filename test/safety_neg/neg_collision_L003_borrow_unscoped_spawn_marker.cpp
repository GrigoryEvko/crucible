// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule L003 — MARKER × MARKER composition path
// (FIXY-FOUND-069 sub-HS14 closure).
//
// Companion fixture to neg_collision_L003_unscoped_spawn_borrow_capture.cpp
// (which covers the USAGE-arm × MARKER-arm composition via
// UsageMode::Borrow + marks_unscoped_spawn).  L003_OK gates on:
//
//   concept L003_OK = !(has_borrow_capture_v<F> &&
//                       marks_unscoped_spawn<F>::value);
//
// `has_borrow_capture_v<F>` is a 3-way OR-fold over:
//
//   * `F::usage_v == UsageMode::Borrow` (USAGE-tier — direct Fn axis)
//   * `is_borrowed_carrier<F::type_t>::value` (CARRIER-tier — wrapper
//     on type_t)
//   * `marks_borrow_capture<F>::value` (MARKER-tier — author
//     specializes the trait on the offending Fn)
//
// The spawn side is single-arm: marks_unscoped_spawn is itself the
// only signal — there is no Fn axis equivalent.  So L003's tier grid
// is 3×1, and the shipped fixture closes the (usage × marker) cell.
// THIS fixture closes the (marker × marker) cell, exercising BOTH
// the borrow-capture marker arm AND the unscoped-spawn marker.  The
// (carrier × marker) cell — BorrowedRef<T> on type_t — remains
// uncovered today and is a future sub-HS14 follow-up (BorrowedRef's
// deleted default ctor complicates the fixture's value-init pattern;
// see L007 carrier-tier follow-up).
//
// Why both fixtures are required per HS14:
//
//   * A refactor that drops the marker arm from has_borrow_capture_v
//     breaks the MARKER-tier path → caught by THIS fixture.
//   * A refactor that drops the Usage::Borrow arm breaks the
//     USAGE-tier path → caught by the original fixture.
//   * Without both, the OR-fold could silently degenerate to a
//     single-arm rule and ship; a downstream Fn opting in via
//     marks_borrow_capture (e.g. a callback-style borrow adapter)
//     would slip past L003.
//
// Mismatch class: Linear usage + bare int + marks_borrow_capture
// + marks_unscoped_spawn.  Distinct from the shipped fixture's
// class because the borrow-capture signal comes from the trait
// specialization, not the Fn template parameter.  L002 silent
// (no marks_async, Row<> empty no Bg, Reentrancy::NonReentrant
// not Coroutine).  L007 silent (Row<> empty, no Bg).
// L004/L006 silent (no region lifetime / no longjmp).  R-family
// silent (Reentrancy::NonReentrant).  H/W/V/S silent (no respective
// wrappers/markers).
//
// Expected diagnostic substring: "L003:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>

#include <type_traits>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_l003_marker {

// A Linear-usage Fn (NOT Borrow → has_borrow_capture_v's Usage arm
// FALSE) with bare int payload (NOT borrowed-carrier → carrier arm
// FALSE), Row<> empty (L007 silent, L002 Bg-arm silent), Reentrancy::
// NonReentrant (L002 Coroutine-arm silent).  has_borrow_capture_v
// fires ONLY via the marker arm; marks_unscoped_spawn supplies the
// spawn signal.  Both markers engaged at file scope below.
using Bad = fn::Fn<
    int,                                       // 1  Type — bare int (no Borrowed carrier)
    fn::pred::True,                            // 2  Refinement
    fn::UsageMode::Linear,                     // 3  Usage — Linear (NOT Borrow → usage arm
                                                //                of has_borrow_capture_v FALSE)
    fx::Row<>,                                 // 4  EffectRow — empty (L007 silent; L002 Bg-arm
                                                //                silent; B001 silent)
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
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy — NOT Coroutine (L002 Coroutine
                                                //                arm silent; R-family silent)
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_l003_marker

// Engage BOTH markers — has_borrow_capture_v via marks_borrow_capture
// and the spawn signal via marks_unscoped_spawn.  L003 fires on the
// conjunction.  L002 silent (no marks_async, no Bg row, no Coroutine).
namespace crucible::safety::fn::collision {
    template <> struct marks_borrow_capture<::neg_collision_l003_marker::Bad>
        : std::true_type {};
    template <> struct marks_unscoped_spawn<::neg_collision_l003_marker::Bad>
        : std::true_type {};
}  // namespace crucible::safety::fn::collision

[[maybe_unused]] neg_collision_l003_marker::Bad the_fixture{};

int main() { return 0; }
