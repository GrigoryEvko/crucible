// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule R001 — STRUCTURAL-tier trigger path
// (FIXY-FOUND-069 sub-HS14 closure).
//
// Companion fixture to neg_collision_R001_coroutine_hot_path.cpp
// (which covers the MARKER-tier trigger via `marks_hot_path` template
// specialization).  R001_OK gates on the conjunction:
//
//   concept R001_OK = !(F::reentrancy_v == ReentrancyMode::Coroutine &&
//                       is_hot_path_v<F>);
//
// `is_hot_path_v<F>` is the FOUND-067 OR-fold over BOTH:
//
//   * `marks_hot_path<F>::value` (marker-tier — author specializes)
//   * `extract::is_hot_path_v<F::type_t>` (structural — payload is
//     §XVI-canonical `HotPath<Hot, T>` wrapper)
//
// The Reentrancy side of R001 is itself structural — `F::reentrancy_v`
// is a Fn template parameter, not a marker — so the only tier-split
// axis for R001 is the HotPath OR-fold.  The shipped fixture exercises
// the MARKER-tier HotPath path; THIS fixture exercises the STRUCTURAL-
// tier HotPath path — Fn's type_t is HotPath<Hot, int> and there is no
// `marks_hot_path` specialization.
//
// Why both fixtures are required per HS14:
//
//   * A refactor that drops the marker-tier read from the OR-fold
//     breaks the MARKER-tier path → caught by the original fixture.
//   * A refactor that drops the structural extract::is_hot_path_v term
//     breaks the STRUCTURAL-tier path → caught by THIS fixture.
//   * Without both, the OR-fold can silently degenerate to a
//     single-trigger rule and ship — reopening the FOUND-067 dormant-
//     rule defect for R001.
//
// Mismatch class: ReentrancyMode::Coroutine × HotPath<Hot, T> wrapper,
// no marker.  Distinct from the marker-tier class because here the
// HotPath signal comes structurally from the payload wrapper, not from
// an author-controlled trait specialization.  Per the marker fixture's
// documented behavior: pred::True + HotPath ALSO trips H002 under
// first_failure ordering; the static_assert chain runs all R001/R002/
// R003 asserts so R001's substring appears regardless.  R002 silent
// (Usage is Linear, not Borrow).  R003 silent (Row<> empty, no Bg).
// H001 silent (cost::Constant, not Unbounded).
//
// Expected diagnostic substring: "R001:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/HotPath.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_r001_structural {

// Fn payload is HotPath<Hot, int> — outermost wrapper declares hot
// tier.  Reentrancy is Coroutine (R001 trigger paired with structural
// HotPath).  Cost is Constant — H001 silent.  EffectRow is empty —
// R003 silent.  Usage is Linear — R002 silent.  NO marks_hot_path
// specialization: the structural detector alone drives the HotPath
// read.
using HotInt = ::crucible::safety::HotPath<
    ::crucible::safety::HotPathTier_v::Hot, int>;

using Bad = fn::Fn<
    HotInt,                                    // 1  Type — HotPath<Hot, int>
    fn::pred::True,                            // 2  Refinement (trivial — pre-existing
                                                //                H002 co-fire is benign,
                                                //                mirrors marker fixture's
                                                //                pred::True choice)
    fn::UsageMode::Linear,                     // 3  Usage (NOT Borrow → R002 silent)
    fx::Row<>,                                 // 4  EffectRow — empty (R003 silent)
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
    fn::ReentrancyMode::Coroutine,             // 16 Reentrancy — COROUTINE (R001 trigger
                                                //                 paired with structural HotPath)
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_r001_structural

// NO marks_hot_path specialization — R001 fires via the structural
// path alone.  This is the FOUND-067 structural witness for R001,
// parallel to the H001 / H002 / H010 structural wrapper fixtures.

[[maybe_unused]] neg_collision_r001_structural::Bad the_fixture{};

int main() { return 0; }
