// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// P1-AUDIT (#852): with_cap<NewCap>() concept gate witness.
//
// Violation: ExecCtx<>{}.with_cap<int>() — `int` is not a recognised
// capability source.  The builder method's `requires IsCapType<NewCap>`
// rejects this at the call site rather than substituting nonsense
// into the resulting ExecCtx<int, ...>.
//
// Expected diagnostic: GCC's "associated constraints are not
// satisfied" pointing at the IsCapType clause on with_cap<>(),
// or "no matching function for call to" enumerating the
// concept-violation requires.

#include <crucible/effects/ExecCtx.h>

namespace eff = crucible::effects;

int main() {
    constexpr auto bad = eff::ExecCtx<>{}.with_cap<int>();
    (void)bad;
    return 0;
}
