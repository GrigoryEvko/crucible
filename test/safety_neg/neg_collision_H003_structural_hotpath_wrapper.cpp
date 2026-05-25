// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// CollisionCatalog rule H003 — STRUCTURAL-tier trigger path
// (FIXY-FOUND-069 sub-HS14 closure).
//
// Companion fixtures already shipped:
//   * neg_collision_H003_hotpath_alloc_unbounded.cpp — MARKER-tier × Alloc arm
//   * neg_collision_H003_hotpath_io_unbounded.cpp    — MARKER-tier × IO arm
//
// This fixture closes the STRUCTURAL-tier axis.  H003_OK gates on the
// 3-axis conjunction:
//
//   concept H003_OK = !(is_hot_path_v<F> &&
//                       (row_has_effect_v<effect_row_t, Effect::Alloc> ||
//                        row_has_effect_v<effect_row_t, Effect::IO>) &&
//                       is_unbounded_cost<cost_t>::value);
//
// `is_hot_path_v<F>` is the FOUND-067 OR-fold over BOTH:
//
//   * `marks_hot_path<F>::value` (marker-tier — author specializes)
//   * `extract::is_hot_path_v<F::type_t>` (structural — payload is
//     §XVI-canonical `HotPath<Hot, T>` wrapper)
//
// The two shipped fixtures exercise the OR-fold's MARKER-tier path
// through both Alloc and IO arms; THIS fixture exercises the
// STRUCTURAL-tier path with payload = HotPath<Hot, int>, Row<Alloc>,
// and NO `marks_hot_path` specialization.  The Alloc arm is chosen
// (vs IO) to keep diff vs the canonical first H003 fixture minimal
// — the tier-axis is what's being closed here, not the OR-arm axis.
//
// Why both tier-arms are required per HS14:
//
//   * A refactor that drops the marker-tier read from the OR-fold
//     breaks the MARKER-tier path → caught by the original two
//     marker-tier fixtures.
//   * A refactor that drops the structural extract::is_hot_path_v
//     term breaks the STRUCTURAL-tier path → caught by THIS fixture.
//   * Without both, the OR-fold can silently degenerate to a
//     single-tier rule and ship — reopening the FOUND-067 dormant-
//     rule defect for H003.
//
// Mismatch class: HotPath<Hot, T> wrapper × Row<Alloc> × Unbounded
// cost, no marker.  Distinct from the marker-tier class because here
// the HotPath signal comes structurally from the payload wrapper,
// not from an author-controlled trait specialization.  As with the
// marker fixture: H001 ALSO fires (HotPath × Unbounded, the strict
// superset).  H002 also fires (HotPath × pred::True).  The
// static_assert chain runs all H0xx asserts so "H003:" appears in
// the diagnostic regardless of first_failure ordering.  H010 silent
// (Row<Alloc>, no Bg).
//
// Expected diagnostic substring: "H003:".

#include <crucible/effects/EffectRow.h>
#include <crucible/safety/Fn.h>
#include <crucible/safety/HotPath.h>

namespace fn = crucible::safety::fn;
namespace fx = crucible::effects;

namespace neg_collision_h003_structural {

// Fn payload is HotPath<Hot, int> — outermost wrapper declares hot
// tier.  EffectRow engages Alloc.  Cost is Unbounded.  All three
// conjuncts of H003_OK fail in concert: structural HotPath, Alloc
// row atom, unbounded cost.  NO marks_hot_path specialization: the
// structural detector alone drives the HotPath read.
using HotInt = ::crucible::safety::HotPath<
    ::crucible::safety::HotPathTier_v::Hot, int>;

using Bad = fn::Fn<
    HotInt,                                    // 1  Type — HotPath<Hot, int>
    fn::pred::True,                            // 2  Refinement (H002 co-fire benign,
                                                //                mirrors marker-tier choice)
    fn::UsageMode::Linear,                     // 3  Usage
    fx::Row<fx::Effect::Alloc>,                // 4  EffectRow — Alloc atom (H003 OR-arm trigger
                                                //                paired with structural HotPath)
    fn::SecLevel::Public,                      // 5  Security
    fn::proto::None,                           // 6  Protocol
    fn::lifetime::Static,                      // 7  Lifetime
    fn::source::FromInternal,                  // 8  Source
    fn::trust::Verified,                       // 9  Trust
    fn::ReprKind::Opaque,                      // 10 Repr
    fn::cost::Unbounded,                       // 11 Cost — UNBOUNDED (H003 trigger paired
                                                //                with structural HotPath + Alloc)
    fn::precision::Exact,                      // 12 Precision
    fn::space::Bounded<sizeof(int)>,           // 13 Space
    fn::OverflowMode::Trap,                    // 14 Overflow
    fn::MutationMode::Immutable,               // 15 Mutation
    fn::ReentrancyMode::NonReentrant,          // 16 Reentrancy
    fn::size_pol::Sized<sizeof(int)>,          // 17 Size
    /*Version=*/1,                             // 18 Version
    fn::stale::Fresh                           // 19 Staleness
>;

}  // namespace neg_collision_h003_structural

// NO marks_hot_path specialization — H003 fires via the structural
// path alone.  This is the FOUND-067 structural witness for H003,
// parallel to the H001 / H002 / H010 / R001 structural wrapper
// fixtures.  Fifth FOUND-067 family rule to reach full-HS14.

[[maybe_unused]] neg_collision_h003_structural::Bad the_fixture{};

int main() { return 0; }
