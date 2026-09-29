#pragma once

// Lets a foundation test prove that an always-on guard aborts, without
// spawning a process to die in.
//
// CRUCIBLE_FATAL_INVARIANT and a violated contract end the process through
// std::abort, so a test that wants to watch one fire has to survive it.  A
// forked child is the usual answer, and the tree bans raw process spawn: fork
// carries no permission token and no effect row into the child, and
// scripts/check-banned-calls.py rejects it.  The abort is therefore
// caught where it happens: a SIGABRT handler jumps back to the arming point on
// the same thread.
//
// The jump target and the arming flag are thread-local, so two threads can arm
// independently.  The signal disposition itself is process-wide, which is why
// the previous one is put back before this returns.
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
// Use it only for a guard that is expected to fire.  An abort raised while
// nothing is armed ends the process, as it should.
//
// This is the foundation-layer copy of test/test_abort_probe.h; the two are
// kept identical in shape and differ only in namespace.

#include <csetjmp>
#include <csignal>
#include <cstdlib>
#include <pthread.h>
#include <unistd.h>

namespace foundation::test {

inline thread_local std::jmp_buf abort_probe_return;
inline thread_local volatile sig_atomic_t abort_probe_armed = 0;

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

// Runs body and reports whether it aborted.  A body that returns normally
// reports false, which is the failure the caller is usually looking for.
//
// The control-flow-redundancy hardening the project builds with cannot
// instrument a function that calls setjmp, and says so as an error.  The
// attribute turns it off for this one function rather than for the test.
template <typename Body>
[[nodiscard, gnu::optimize("no-harden-control-flow-redundancy")]] bool aborts(Body&& body) {
    struct sigaction want{};
    struct sigaction previous{};
    want.sa_handler = abort_probe_handler;
    sigemptyset(&want.sa_mask);
    want.sa_flags = 0;
    if (sigaction(SIGABRT, &want, &previous) != 0) std::abort();
    // SIGABRT stays blocked while the handler operates, and longjmp does not
    // change the mask. Save the mask here and set it again after the jump.
    sigset_t saved_mask;
    if (pthread_sigmask(SIG_SETMASK, nullptr, &saved_mask) != 0) std::abort();

    bool did_abort = false;
    if (setjmp(abort_probe_return) == 0) {
        abort_probe_armed = 1;
        body();
        abort_probe_armed = 0;
    } else {
        did_abort = true;
        if (pthread_sigmask(SIG_SETMASK, &saved_mask, nullptr) != 0) std::abort();
    }

    if (sigaction(SIGABRT, &previous, nullptr) != 0) std::abort();
    return did_abort;
}

}  // namespace foundation::test
