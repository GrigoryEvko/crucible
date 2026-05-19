// FIXY-U-084 HS14 strengthening fixture (7 of 9).
//
// Demonstrates the SECOND mismatch class for mint_hardening: the
// CtxFitsHardeningMint concept is `IsExecCtx<Ctx> ∧ row_contains_v<…,
// Init>`.  fixtures 1-2 (Bg, HotFg) exercise the row-membership half;
// THIS fixture exercises the IsExecCtx half by passing a raw struct
// that does not satisfy IsExecCtx<>.  Short-circuit semantics make
// the diagnostic name `IsExecCtx` directly, distinguishing this
// rejection path from the row-mismatch path.

#include <crucible/warden/Hardening.h>

struct NotAnExecCtx {};

int main() {
    crucible::warden::Policy p{};
    auto applied = crucible::warden::mint_hardening(NotAnExecCtx{}, p);
    (void)applied;
    return 0;
}
