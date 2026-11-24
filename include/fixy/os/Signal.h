#pragma once

// The signal door of fixy: the handler of each fatal signal, the alternate
// signal stack of a thread, and the two signal calls of the child door (the
// child signals, below).
//
// Handlers
//   install_fatal_handlers puts one handler on SIGABRT, SIGSEGV, SIGBUS,
//   SIGILL and SIGFPE, and keeps the action that each handler replaces.  The
//   handler calls the report of the caller with the number of the signal.
//   Then it puts back the action that was there before, and it lets that
//   action run: a signal that a process sent (si_code at or below zero) is
//   raised again, and it arrives when the handler returns, because the
//   handler blocks it while it runs.  A fault is not raised again: the
//   faulting instruction runs again after the return and faults into the
//   action that the handler put back.  A signal that the process ignored
//   keeps that action.
//
//   A door changes a signal disposition only in a call that takes an Init
//   context, or in its own handler to put back the disposition that such a
//   call replaced (rule R20 of misc/01_10_2026_quarantine.md).  So the gate
//   of install_fatal_handlers asks for a context that owns Effect::Init, and
//   the door installs the handlers one time for each process.  A second call
//   gives Errno::ebusy and changes nothing.  The child reset is the one
//   other case.
//
// Child reset
//   reset_to_default(test, child, signal) puts back the default action of
//   one fatal signal in a child of the child door of fixy/os/Process.h, so a
//   death test sees the end by that signal.  Without it, the handler of
//   AddressSanitizer turns a fault into the exit code 1, and the handler of
//   The handlers write a report first.  The call takes a context that owns
//   the Test capability, and the ForkedChild proof that only the child door
//   gives its body.  So only a test calls it, and only in a child.  The child
//   ends after its body, and no disposition of the parent changes.
//
// Alternate stack
//   The kernel delivers a signal with SA_ONSTACK on the alternate stack of
//   the thread, when the thread has one.  So the handler of a fault that
//   overflows the stack of a thread runs, and writes its report.
//   install_fatal_handlers sets an alternate stack on the calling thread,
//   and that stack stays until the process ends.  The thread door of
//   fixy/os/ThreadTasks.h holds a ThreadSignalStack in each thread that it
//   starts, for the life of the thread.  A thread that the base does not
//   start gets no alternate stack, and a stack overflow on that thread ends
//   the process with no report.
//
//   An alternate stack has at least 64 KiB and at least four times the
//   smallest stack of a signal that the kernel gives in the auxiliary vector
//   (AT_MINSIGSTKSZ), with a floor of 2048 bytes for that value.  This is the
//   value of SIGSTKSZ of the C library, with the floor of 64 KiB.  The door
//   reads the vector one time for each process.  The memory comes from the
//   guarded memory door of foundation/os/GuardedRegion.h, with a guard of no
//   access below the stack.  A thread that has an alternate stack of at
//   least that size keeps it, and the door maps no stack for it.
//
// This header is in the layer X3_os of utils/rule-table.txt.  The door file
// is src/fixy/os/Signal.cpp, the one includer of <signal.h>.

#include <fixy/os/Process.h>
#include <foundation/core/Choice.h>
#include <foundation/effects/Ctx.h>
#include <foundation/os/Errno.h>
#include <foundation/os/GuardedRegion.h>

#include <cstddef>
#include <cstdint>

namespace fixy::signal {

// The report that the handler of a fatal signal calls first, with the
// number of the signal.  It runs in the handler, so it calls only functions
// that are safe in a signal handler.
using FatalReport = void (*)(int signal_number) noexcept;

// A fatal signal: its default action ends the process.  The values are the
// numbers of Linux on x86_64 and on aarch64, and src/fixy/os/Signal.cpp
// checks each against <signal.h>.  The handlers take each signal other than
// breakpoint_trap, which a trap instruction of aarch64 raises, such as the
// trap of a check of a Release build.
enum class FatalSignal : std::uint8_t {
    illegal_instruction = 4,
    breakpoint_trap = 5,
    abort = 6,
    bus_error = 7,
    floating_point = 8,
    segmentation_fault = 11,
};

// The number of the signal, as a ChildReport of fixy/os/Process.h reads it.
[[nodiscard]] constexpr std::uint8_t number_of(FatalSignal fatal_signal) noexcept {
    return static_cast<std::uint8_t>(fatal_signal);
}

namespace detail {

// The alternate stack that a thread had before a ThreadSignalStack replaced
// it: its first byte, its count of bytes and its flags.
struct PreviousStack {
    void* first = nullptr;
    std::size_t bytes = 0;
    int flags = 0;
};

[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
install_fatal_handlers_(FatalReport report) noexcept;

// Sets an alternate stack on the calling thread, and writes the stack that
// it replaces into previous.  The Option is empty when the thread keeps the
// alternate stack that it has.
[[nodiscard]] ::foundation::core::Option<::foundation::os::GuardedRegion>
install_alternate_stack_(PreviousStack& previous) noexcept;

// Puts previous back as the alternate stack of the calling thread.
void restore_alternate_stack_(PreviousStack const& previous) noexcept;

// Child signals.  The two signal calls of the child door of
// fixy/os/Process.h, which only a test reaches.  stop_self_ raises SIGSTOP in
// the calling process, so a child that its parent traces stops before its
// body runs.  end_process_ sends SIGKILL to the process with that id, a child that
// ran to the limit of its wait.  A child that ended already ignores the
// signal, and the wait of the door collects its end.
void stop_self_() noexcept;
void end_process_(std::int32_t process) noexcept;

// Puts back the default action of fatal_signal in the calling process (the
// child reset).  A refused sigaction ends the process.
void reset_to_default_(FatalSignal fatal_signal) noexcept;

}  // namespace detail

// Puts back the default action of fatal_signal in the child of the child
// door that runs the call (the child reset).  Only a test calls it, and only
// in the body of a child, which holds the proof.
template <class Ctx>
    requires ::fixy::process::CtxFitsChildProcess<Ctx>
void reset_to_default(Ctx const& /*test*/, ::fixy::process::ForkedChild const& /*child*/,
                      FatalSignal fatal_signal) noexcept {
    detail::reset_to_default_(fatal_signal);
}

// A context that can change a signal disposition: one that owns Effect::Init.
template <class Ctx>
concept CtxInstallsSignalHandlers = ::foundation::effects::CtxOwnsCapability<Ctx, ::foundation::effects::Effect::Init>;

// Installs the handler of each fatal signal with report (the handlers), and
// sets the alternate stack of the calling thread until the process ends.
template <class Ctx>
    requires CtxInstallsSignalHandlers<Ctx>
[[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
install_fatal_handlers(Ctx const& /*init*/, FatalReport report) noexcept {
    return detail::install_fatal_handlers_(report);
}

// The alternate signal stack of the calling thread, for the life of the
// object (the alternate stack).  The destructor puts back the stack that the
// thread had before, and then it unmaps the stack of the object.  The object
// lives on the stack of its thread: it does not copy or move, and new
// refuses it.  A failed map ends the process, as each failed allocation of
// the tree does.
class ThreadSignalStack final {
public:
    [[nodiscard]] static ThreadSignalStack install_on_calling_thread() noexcept { return ThreadSignalStack{}; }

    ThreadSignalStack(ThreadSignalStack const&) =
        delete("the stack belongs to one thread, and one object puts it back");
    ThreadSignalStack(ThreadSignalStack&&) = delete("the stack belongs to one thread, and one object puts it back");
    ThreadSignalStack& operator=(ThreadSignalStack const&) = delete("the stack belongs to one thread");
    ThreadSignalStack& operator=(ThreadSignalStack&&) = delete("the stack belongs to one thread");
    static void* operator new(std::size_t) = delete("a ThreadSignalStack lives on the stack of its thread");
    static void* operator new[](std::size_t) = delete("a ThreadSignalStack lives on the stack of its thread");

    ~ThreadSignalStack() {
        if (region_.is_some()) detail::restore_alternate_stack_(previous_);
    }

private:
    ThreadSignalStack() noexcept : region_{detail::install_alternate_stack_(previous_)} {}

    // previous_ comes before region_, so the install that makes region_
    // writes into a constructed member.
    detail::PreviousStack previous_{};
    ::foundation::core::Option<::foundation::os::GuardedRegion> region_;
};

}  // namespace fixy::signal
