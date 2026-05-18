// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-004 (CLAUDE.md §XVI Is*.h cv-ref convention): IsEffectRow
// strips top-level cv-ref to match the IsLinear family.  The widening
// must NOT accidentally accept non-Row types just because they carry
// cv-ref qualifiers.
//
// Violation: asserts `IsEffectRow<int&&>` — `std::remove_cvref_t<int&&>`
// is `int`, which is not a `Row<Es...>` specialization.  Must reject
// just as the unqualified `IsEffectRow<int>` did pre-A3-004.
//
// Expected diagnostic: "static assertion failed" pointing at the
// failed IsEffectRow assertion, or "fixy-A3-004" / "IsEffectRow" /
// "is_effect_row".

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

static_assert(eff::IsEffectRow<int&&>,
    "fixy-A3-004: cv-ref-stripped non-Row must still reject IsEffectRow");

int main() { return 0; }
