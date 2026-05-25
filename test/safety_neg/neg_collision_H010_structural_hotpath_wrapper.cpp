// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule H010 — STRUCTURAL-tier trigger path
// (FIXY-FOUND-069 sub-HS14 closure).
//
// Companion fixture to neg_collision_H010_hotpath_bg.cpp (which
// covers the MARKER-tier trigger via `marks_hot_path` template
// specialization).  H010_OK gates on the conjunction:
//
//   concept H010_OK = !(is_hot_path_v<F> &&
//                       row_has_effect_v<F::effect_row_t, Effect::Bg>);
//
// `is_hot_path_v<F>` is the FOUND-067 OR-fold over BOTH:
//
//   * `marks_hot_path<F>::value` (marker-tier — author specializes)
//   * `extract::is_hot_path_v<F::type_t>` (structural — payload is
//     §XVI-canonical `HotPath<Hot, T>` wrapper)
//
// The shipped fixture exercises the MARKER-tier path; THIS fixture
// exercises the STRUCTURAL-tier path — Fn's type_t is HotPath<Hot, int>
// and there is no `marks_hot_path` specialization.  The structural
// detector graduates H010 from "dormant unless author specializes" to
// "fires automatically on production-shape HotPath wrapper combined
// with a Bg-row binding".
//
// Why both fixtures are required per HS14:
//
//   * A refactor that drops the marker-tier read from the OR-fold
//     breaks the MARKER-tier path → caught by the original fixture.
//   * A refactor that drops the structural extract::is_hot_path_v term
//     breaks the STRUCTURAL-tier path → caught by THIS fixture.
//   * Without both, the OR-fold can silently degenerate to a
//     single-trigger rule and ship — reopening the FOUND-067 dormant-
//     rule defect for H010.
//
// Mismatch class: HotPath<Hot, T> wrapper × Row<Bg>, no marker.
// Distinct from the marker-tier class because here the HotPath
// signal comes structurally from the payload wrapper, not from an
// author-controlled trait specialization.  As with the marker
// fixture: pred::True ALSO trips H002 under first_failure
// ordering; the static_assert chain in validate() runs all H0xx
// asserts so H010's substring appears regardless.  H001 stays
// silent because cost::Constant keeps `is_unbounded_cost` false;
// H003 stays silent because the row has no Alloc/IO atoms.
//
// Expected diagnostic substring: "H010:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/HotPath.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_h010_structural {

// Fn payload is HotPath<Hot, int> — outermost wrapper declares hot
// tier.  EffectRow engages Bg (H010 trigger paired with structural
// HotPath signal).  Cost is Constant; no Alloc/IO atoms — H001/H003
// silent.  NO marks_hot_path specialization: the structural detector
// alone drives the HotPath read.
using HotInt = ::crucible::safety::HotPath<
    ::crucible::safety::HotPathTier_v::Hot, int>;

using Bad = fn::Fn<
    HotInt,                                    // 1  Type — HotPath<Hot, int>
    fn::pred::True,                            // 2  Refinement (trivial — pre-existing
                                                //                H002 co-fire is benign,
                                                //                mirrors marker fixture's
                                                //                pred::True choice)
    fn::UsageMode::Linear,                     // 3  Usage
    fx::Row<fx::Effect::Bg>,                   // 4  EffectRow — Bg engaged (H010 trigger
                                                //                paired with structural HotPath)
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Constant,                        // 11 Cost (bounded — H001/H003 silent)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_h010_structural

// NO marks_hot_path specialization — H010 fires via the structural
// path alone.  This is the FOUND-067 structural witness for H010,
// parallel to the H001 / H002 structural wrapper fixtures.

[[maybe_unused]] neg_collision_h010_structural::Bad the_fixture{};

int main() { return 0; }
