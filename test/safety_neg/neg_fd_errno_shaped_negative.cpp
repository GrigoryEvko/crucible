// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-012 HS14 fixture #1: Fd ctor rejects errno-shaped negatives.
//
// Pre-fix, `FileHandle::FileHandle(int)` accepted arbitrary negative
// values without validation.  A caller mistakenly passing `-EBADF`
// (-9) where an fd was expected — say from a code path that returned
// errno-as-negative-int via the same channel — constructed an
// apparently-closed FileHandle.  The error was silently swallowed:
// `is_open()` returned false, downstream `read_full` / `write_full`
// returned `-EBADF` exactly as if a *legitimate* closed handle had
// been passed.  The caller never learned that the original errno
// was -9, not -1.
//
// fixy-A1-012 introduces the `Fd` newtype with `CRUCIBLE_PRE` on the
// int-taking ctor: `value == -1 || value >= 0`.  -1 (closed sentinel)
// and the POSIX fd range pass; every other negative — every encoded
// errno — fires the precondition at consteval AND runtime.
//
// VIOLATION: consteval `Fd{-9}` (errno-shaped) fires CRUCIBLE_PRE,
// poisoning the surrounding static_assert via __builtin_trap.
//
// Expected diagnostic: "non-constant condition for static assertion",
// "__builtin_trap", "contract violation", or equivalent — anything
// proving the consteval invocation was refused.

#include <crucible/handles/FileHandle.h>

int main() {
    namespace safe = ::crucible::safety;

    // VIOLATION: -9 (≈ -EBADF) is not a valid fd pattern.  The Fd
    // ctor's CRUCIBLE_PRE fires at consteval, refusing the call.
    constexpr safe::Fd smuggled{-9};
    static_assert(smuggled.raw() == -9, "this must not compile");
    return 0;
}
