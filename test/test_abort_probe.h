#pragma once

// Lets a test prove that an always-on guard aborts, without spawning a
// process to die in.
//
// CRUCIBLE_FATAL_INVARIANT ends the process through std::abort.  A test that
// monitors one must continue after it.  The usual method is a child process,
// but CLAUDE.md §IX bans a raw process start.  A fork gives the child no
// Permission<Tag> proof and no Met(X) effect row, and the quarantine plugin
// refuses a fork in a test file.  This header catches the abort where it
// occurs.  A SIGABRT handler jumps back to the arming point on the same
// thread.  The method works for a guard on a worker thread and for a guard on
// the thread of the caller.
//
// The arming flag is thread-local, so only the thread that armed the probe
// jumps. An abort on a different thread ends the process. The jump buffer and
// the signal disposition are process-wide, and the probe puts back the
// previous disposition before it returns. Thus one thread at a time arms the
// probe, and a second arm at the same time ends the process. The buffer is
// not thread-local, because each thread-local byte of a shared library that
// includes this header goes into its TLS block. The rule static-tls holds
// that block small.
//
// ThreadSanitizer must see the jump. If it does not, it thinks that the
// thread is still in the handler, and it gives a signal-unsafe report for
// each subsequent allocation on that thread. Two things prevent the view.
// _FORTIFY_SOURCE changes the name of longjmp and siglongjmp to
// __longjmp_chk, which ThreadSanitizer does not intercept. The jump uses a
// declaration of the plain longjmp symbol. ThreadSanitizer intercepts setjmp
// and not __sigsetjmp. The buffer comes from setjmp, and the probe saves the
// signal mask and sets it again after the jump.
//
// Use it only for a guard that is expected to fire. An abort raised while
// nothing is armed ends the process, as it should.

#include "test_assert.h"

#include <fixy/Core.h>

#include <csetjmp>
#include <csignal>
#include <pthread.h>
#include <unistd.h>

namespace crucible::test {

inline std::jmp_buf abort_probe_return;
inline thread_local volatile sig_atomic_t abort_probe_armed = 0;
// True while a thread is inside aborts().
constinit inline ::fixy::Atomic<bool> abort_probe_busy{false};

// This declaration names the plain longjmp symbol, which the sanitizers
// intercept. The fortified name in <csetjmp> links to __longjmp_chk.
extern "C" [[noreturn]] void abort_probe_longjmp(std::jmp_buf, int) noexcept __asm__("longjmp");

extern "C" inline void abort_probe_handler(int) {
    if (abort_probe_armed) {
        abort_probe_armed = 0;
        abort_probe_longjmp(abort_probe_return, 1);
    }
    // Nobody armed for this one. Leave without running any more test code.
    _exit(128 + SIGABRT);
}

// Runs body and reports whether it aborted. A body that returns normally
// reports false, which is the failure the caller is usually looking for.
template <typename Body>
[[nodiscard]] bool aborts(Body&& body) {
    if (abort_probe_busy.exchange_acq_rel(true)) {
        ::fixy::fatal("two threads armed the abort probe at the same time");
    }
    struct sigaction want{};
    struct sigaction previous{};
    want.sa_handler = abort_probe_handler;
    sigemptyset(&want.sa_mask);
    want.sa_flags = 0;
    assert(sigaction(SIGABRT, &want, &previous) == 0);
    // SIGABRT stays blocked while the handler operates, and longjmp does not
    // change the mask. Save the mask here and set it again after the jump.
    sigset_t saved_mask;
    assert(pthread_sigmask(SIG_SETMASK, nullptr, &saved_mask) == 0);

    bool did_abort = false;
    if (setjmp(abort_probe_return) == 0) {
        abort_probe_armed = 1;
        body();
        abort_probe_armed = 0;
    } else {
        did_abort = true;
        assert(pthread_sigmask(SIG_SETMASK, &saved_mask, nullptr) == 0);
    }

    assert(sigaction(SIGABRT, &previous, nullptr) == 0);
    abort_probe_busy.store_release(false);
    return did_abort;
}

}  // namespace crucible::test
