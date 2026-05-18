// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// HS14 fixture #3 of 4 for fixy-A3-027 (ExecCtx Progress axis).
//
// Premise: ExecCtx::with_progress<NewProgress>() carries a
// `requires IsProgressClass<NewProgress>` constraint at the member-
// function template signature.  This is a SEPARATE gate from the
// class-template-level `WellFormedExecCtxAxes` concept (fixture #2)
// — call-site builder typos must be rejected at the member call,
// before they would propagate into a returned ExecCtx<...> that
// would then re-fire the class-body static_assert.
//
// Witnesses that the builder method protects callers from accidental
// non-class arguments at the with_progress<>() invocation site.
//
// Expected diagnostic family (matched by CMakeLists regex):
//   "associated constraints are not satisfied" / "IsProgressClass" /
//   "with_progress" / "fixy-A3-027".

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

int main() {
    constexpr eff::ExecCtx<> ctx{};
    auto bad = ctx.template with_progress<int>();  // int is NOT a progress class
    (void)bad;
    return 0;
}
