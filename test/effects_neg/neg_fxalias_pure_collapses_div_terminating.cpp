// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-019 audit fixture (pair 1/2): pins the load-bearing
// structural distinctness of the value-carrying F* aliases.
// `Pure<T>` is defined as `Progress<Terminating, DetSafe<Pure,
// Computation<Row<>, T>>>` per CLAUDE.md §XVI; a Progress class
// pinned to MayDiverge produces a DIFFERENT type that does NOT
// collapse to Pure<T>.  This fixture proves the discipline: the
// alias substitution is faithful, so an accidental MayDiverge
// pin at a call site cannot silently be retyped as Pure.
//
// Without this guarantee, a Div-pinned value could flow into a
// Pure-typed sink (e.g. Cipher::record_event under the DetSafe
// fence from 28_04_2026_effects.md §3.4) without the call-site
// rejecting it — the very bug the value-carrying aliases close.
//
// The static_assert fires because the two types are structurally
// distinct under canonical-order nesting (FOUND-I04): different
// outer-most ProgressClass means different `row_hash_contribution`
// and different federation cache slot.
//
// Expected diagnostic: "static assertion failed" / "static_assert"
// / "fixy-A3-019" / "value-carrying F* aliases must remain
// structurally distinct"

#include <crucible/effects/FxAliases.h>
#include <crucible/safety/DetSafe.h>
#include <crucible/safety/Progress.h>

#include <type_traits>

namespace eff = ::crucible::effects;
namespace saf = ::crucible::safety;

using DivLikePureShape = saf::Progress<
    saf::ProgressClass_v::MayDiverge,
    saf::DetSafe<
        saf::DetSafeTier_v::Pure,
        eff::Computation<eff::PureRow, int>>>;

// fixy-A3-019: Pure<int> pins Terminating; DivLikePureShape pins
// MayDiverge.  Same payload, same row, same DetSafe tier — but
// the Progress-class pin differs.  The alias substitution MUST
// preserve the discrimination.  Asserting same_v here fires the
// negative compile.
static_assert(
    std::is_same_v<eff::Pure<int>, DivLikePureShape>,
    "fixy-A3-019: value-carrying F* aliases must remain structurally "
    "distinct under ProgressClass discrimination — Pure<T> ≡ "
    "Progress<Terminating, ...> and MUST NOT silently collapse with "
    "Progress<MayDiverge, ...>.  Without this discipline a Div-pinned "
    "value could flow into a Pure-typed sink at a call site (per the "
    "Cipher::record_event DetSafe fence from 28_04_2026_effects.md "
    "§3.4) without the type system catching the substitution drift.");

int main() { return 0; }
