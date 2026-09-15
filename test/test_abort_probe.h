#pragma once

// Lets a test prove that an always-on guard aborts, without spawning a
// process to die in.
//
// CRUCIBLE_FATAL_INVARIANT ends the process through std::abort, so a test
// that wants to watch one fire has to survive it. A forked child is the usual
// answer, and CLAUDE.md §IX bans raw process spawn outright: fork carries no
// Permission<Tag> linearity proof and no Met(X) effect row into the child, so
// the check-fixy-spawn-discipline guard rejects it. The abort is therefore
// caught where it happens. A SIGABRT handler jumps back to the arming point
// on the same thread, which works for a guard that fires on a worker thread
// as well as one that fires on the caller's.
//
// The jump target and the arming flag are thread-local, so two threads can
// arm independently. The signal disposition itself is process-wide, which is
// why the previous one is put back before this returns.
//
// Use it only for a guard that is expected to fire. An abort raised while
// nothing is armed ends the process, as it should.

#include "test_assert.h"

#include <csetjmp>
#include <csignal>
#include <unistd.h>

namespace crucible::test {

inline thread_local sigjmp_buf abort_probe_return;
inline thread_local volatile sig_atomic_t abort_probe_armed = 0;

extern "C" inline void abort_probe_handler(int) {
    if (abort_probe_armed) {
        abort_probe_armed = 0;
        // The buffer was filled with savemask set, so this restores the
        // signal mask abort() widened on its way in.
        siglongjmp(abort_probe_return, 1);
    }
    // Nobody armed for this one. Leave without running any more test code.
    _exit(128 + SIGABRT);
}

// Runs body and reports whether it aborted. A body that returns normally
// reports false, which is the failure the caller is usually looking for.
//
// The control-flow-redundancy hardening the project builds with cannot
// instrument a function that calls setjmp, and says so as an error. The
// attribute turns it off for this one function rather than for the test.
template <typename Body>
[[nodiscard, gnu::optimize("no-harden-control-flow-redundancy")]] bool aborts(Body&& body) {
    struct sigaction want{};
    struct sigaction previous{};
    want.sa_handler = abort_probe_handler;
    sigemptyset(&want.sa_mask);
    want.sa_flags = 0;
    assert(sigaction(SIGABRT, &want, &previous) == 0);

    bool did_abort = false;
    if (sigsetjmp(abort_probe_return, 1) == 0) {
        abort_probe_armed = 1;
        body();
        abort_probe_armed = 0;
    } else {
        did_abort = true;
    }

    assert(sigaction(SIGABRT, &previous, nullptr) == 0);
    return did_abort;
}

}  // namespace crucible::test
