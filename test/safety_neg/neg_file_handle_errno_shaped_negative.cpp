// NEGATIVE-COMPILE TEST.  This file MUST FAIL TO COMPILE.
//
// fixy-A1-012 HS14 fixture #2: FileHandle int ctor rejects negatives.
//
// The companion fixture neg_fd_errno_shaped_negative.cpp covers the
// Fd newtype ctor.  This fixture is the FileHandle boundary: the
// backward-compatible `FileHandle(int)` ctor is the one a legacy
// caller would reach for first, and was the exact site the pre-fix
// bug fired at — `FileHandle{-9}` (encoded errno) constructed an
// apparently-closed handle that silently swallowed the real error.
// fixy-A1-012 plants a `CRUCIBLE_PRE(Fd::is_valid_pattern(fd))` in
// the int ctor body to reject every negative other than -1 (the
// closed sentinel that POSIX `::open` returns on error).
//
// VIOLATION: consteval `FileHandle{-9}` invokes the int ctor, which
// fires CRUCIBLE_PRE before the field assignment.  The
// `__builtin_trap()` planted in the macro's consteval path poisons
// the surrounding static_assert with "non-constant condition".
//
// Expected diagnostic: "non-constant condition for static assertion",
// "__builtin_trap", "contract violation", or equivalent — anything
// proving the ctor refused the consteval call.

#include <crucible/handles/FileHandle.h>

int main() {
    namespace safe = ::crucible::safety;

    // VIOLATION: -9 isn't a valid fd pattern.  The ctor's
    // CRUCIBLE_PRE fires at consteval, refusing the call.
    constexpr safe::FileHandle h{-9};
    static_assert(!h.is_open(), "this must not compile");
    return 0;
}
