// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A3-004 (CLAUDE.md §XVI Is*.h cv-ref convention): IsExecCtx
// strips top-level cv-ref to match the IsLinear family.  The widening
// must NOT accidentally accept non-Ctx types just because they carry
// cv-ref qualifiers.
//
// Violation: asserts `IsExecCtx<int const&>` — `std::remove_cvref_t<int
// const&>` is `int`, which is not an ExecCtx specialization.  Must
// reject just as the unqualified `IsExecCtx<int>` did pre-A3-004.
//
// Expected diagnostic: "static assertion failed" pointing at the
// failed IsExecCtx assertion, or "fixy-A3-004" / "IsExecCtx" /
// "is_exec_ctx".

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

static_assert(eff::IsExecCtx<int const&>,
    "fixy-A3-004: cv-ref-stripped non-Ctx must still reject IsExecCtx");

int main() { return 0; }
