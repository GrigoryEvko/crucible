#pragma once

// The process doors of fixy.
//
// THE IDENTITY DOOR
//   The id of the calling process and the id of the calling thread, as the
//   kernel gives them.  The door file src/fixy/os/Process.cpp issues getpid
//   and gettid with the raw primitive of foundation/os/Syscall.h.  The two
//   calls cannot fail.  Each read takes a context that admits the row of the
//   family of the two calls in the syscall catalog, the family of the reads
//   of the process state.
//
// THE CHILD DOOR
//   spawn_child(test, body) starts a child process that runs body and then
//   ends with the exit code 0.  The standard error of the child goes to a
//   pipe.  wait(std::move(child)) reads that pipe to its end, waits for the
//   end of the child, and gives a ChildReport: the exit code or the signal,
//   and the last kChildOutputCapacity bytes of the standard error.
//   exit_now(test, code) ends the calling process with code, and runs no
//   exit hook and no static destructor.
//
//   Each call of the door takes a context that owns the Test capability, so
//   only a test starts a child.  The steps of the child door of
//   foundation/os/ChildProcess.h do the work: the C library forks, waits and
//   ends the process, and the raw primitive opens the pipe and moves its
//   write end onto the standard error.
//
//   A child of a process with threads has only the thread that called
//   spawn_child.  A lock that a different thread held at the fork stays held
//   in the child, and the memory of each other thread stays as it was.  So a
//   body may use only what is safe after a fork: no lock that a different
//   thread can hold, no start of a thread, and no wait for a different
//   thread.  A check that fails and ends the process is safe: the report
//   writes with no lock and no allocation.
//
//   A Child that no wait took waits for its child in its destructor, so no
//   child stays in the process table.  Each copy of the write end of the
//   pipe holds the output open: a different thread that forks while a child
//   runs gives its own child a copy, and a wait then also waits for the end
//   of that second child.
//
// This header includes no system header.

#include <fixy/atoms/Syscall.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>
#include <foundation/os/ChildProcess.h>
#include <foundation/os/Errno.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace fixy::process {

namespace eff = ::foundation::effects;

// The id of a process: the id of its thread group, which getpid gives.
enum class ProcessId : std::uint32_t {
};

// The id of a thread, which gettid gives.  The id of the first thread of a
// process is the id of the process.
enum class ThreadId : std::uint32_t {
};

namespace detail {

// The two calls of the door file.
[[nodiscard]] ProcessId current_process_() noexcept;
[[nodiscard]] ThreadId current_thread_() noexcept;

}  // namespace detail

// The row comes from the syscall atom, so the gate and the catalog in
// fixy/atoms/Syscall.h cannot disagree.  The catalog puts getpid and gettid
// in the family of the reads of the process state, which lifts IO and
// Nondet: the id of a process or a thread changes from run to run.
using identity_row_t =
    eff::lift_row_t<::fixy::atom::syscall::family<::fixy::atom::syscall::SyscallFamily::ReadOnlyState>>;

template <typename Ctx>
concept CtxFitsIdentityRead = eff::IsExecCtx<Ctx> && eff::CtxAdmits<Ctx, identity_row_t>;

// The id of the calling process.
template <eff::IsExecCtx Ctx>
    requires CtxFitsIdentityRead<Ctx>
[[nodiscard]] inline ProcessId current_process(Ctx const&) noexcept {
    return detail::current_process_();
}

// The id of the calling thread.
template <eff::IsExecCtx Ctx>
    requires CtxFitsIdentityRead<Ctx>
[[nodiscard]] inline ThreadId current_thread(Ctx const&) noexcept {
    return detail::current_thread_();
}

// A context that can start, wait for and end a child process: one that owns
// the Test capability.  A unit that ships has no such context, because only
// the testing door of foundation/effects/Effect.h mints one.
template <typename Ctx>
concept CtxFitsChildProcess = eff::IsExecCtx<Ctx> && eff::CtxOwnsCapability<Ctx, eff::Effect::Test>;

// The count of the last bytes of the standard error of a child that a
// ChildReport holds.
inline constexpr std::size_t kChildOutputCapacity = 16384;

namespace detail {

// The door of the child door to the parts of a Child and of a ChildReport.
// Only spawn_child, wait, the destructor of a Child and the report of
// src/fixy/os/Process.cpp use it.
struct ChildDoor;

}  // namespace detail

// What a wait gives: the end of the child, and the last bytes of its standard
// error.  dropped_bytes() gives the count of the earlier bytes that the
// report does not hold.  Only a wait makes a report, so a report attests to
// the end of a real child: the constructor is private, and the witness row of
// utils/rule-table.txt holds the refusal of a forger.  The move is
// user-provided, so a report is not trivially copyable, and no copy of bytes
// builds one.  The copy is deleted, so no test copies 16 KiB by mistake.
class ChildReport {
public:
    ChildReport(ChildReport&& other) noexcept
        : end_{other.end_}, dropped_bytes_{other.dropped_bytes_}, output_{other.output_} {}
    ChildReport& operator=(ChildReport&&) = delete("a report is the one result of one wait");
    ChildReport(ChildReport const&) = delete("a report holds 16 KiB of output: move it");
    ChildReport& operator=(ChildReport const&) = delete("a report holds 16 KiB of output: move it");

    [[nodiscard]] ::foundation::os::ChildEnd end() const noexcept { return end_; }

    // True when the signal ended the child.
    [[nodiscard]] bool is_signaled_by(std::uint8_t signal) const noexcept {
        return end_.kind == ::foundation::os::ChildEndKind::signaled && end_.number == signal;
    }

    // True when the child exited with the code.
    [[nodiscard]] bool is_exit_with(std::uint8_t code) const noexcept {
        return end_.kind == ::foundation::os::ChildEndKind::exited && end_.number == code;
    }

    [[nodiscard]] ::foundation::core::TextView output() const& noexcept { return output_.view(); }
    ::foundation::core::TextView
    output() const&& = delete("the output of a temporary report points into an object that the full expression ends");

    [[nodiscard]] std::uint64_t dropped_bytes() const noexcept { return dropped_bytes_; }

private:
    ChildReport() noexcept = default;

    friend struct detail::ChildDoor;

    ::foundation::os::ChildEnd end_{};
    std::uint64_t dropped_bytes_ = 0;
    ::foundation::core::FixedText<kChildOutputCapacity> output_{};
};

// One child process that runs, and the read end of the pipe of its standard
// error.  Only spawn_child makes a Child that names a process, and wait
// takes it.  A Child that no wait took waits for its child in its
// destructor.
class [[nodiscard]] Child {
public:
    // The empty Child names no process and holds no descriptor, as a Child
    // after a move.  It stays public, because it claims nothing.  A wait for
    // it gives the error echild.
    Child() noexcept = default;

    Child(Child&& other) noexcept : id_{other.id_}, error_output_{other.error_output_} {
        other.id_ = 0;
        other.error_output_ = -1;
    }
    Child& operator=(Child&&) = delete("a Child names one process: wait for it, and keep the report");
    Child(Child const&) = delete("a Child names one process, and a copy would wait for it two times");
    Child& operator=(Child const&) = delete("a Child names one process, and a copy would wait for it two times");

    ~Child();

    [[nodiscard]] ProcessId id() const noexcept { return static_cast<ProcessId>(static_cast<std::uint32_t>(id_)); }

private:
    Child(std::int32_t id, std::int32_t error_output) noexcept : id_{id}, error_output_{error_output} {}

    friend struct detail::ChildDoor;

    std::int32_t id_ = 0;
    std::int32_t error_output_ = -1;
};

namespace detail {

// The id of a child and the read end of its output, which a Child gives up.
struct ChildParts {
    std::int32_t child = 0;
    std::int32_t error_output = -1;
};

struct ChildDoor {
    [[nodiscard]] static Child make(std::int32_t child, std::int32_t error_output) noexcept {
        return Child{child, error_output};
    }

    // The parts of the child.  The Child then names no process.
    [[nodiscard]] static ChildParts take(Child& child) noexcept {
        ChildParts const parts{child.id_, child.error_output_};
        child.id_ = 0;
        child.error_output_ = -1;
        return parts;
    }

    [[nodiscard]] static ChildReport make_report() noexcept { return ChildReport{}; }
    static void set_end(ChildReport& report, ::foundation::os::ChildEnd end) noexcept { report.end_ = end; }
    static void set_dropped_bytes(ChildReport& report, std::uint64_t count) noexcept { report.dropped_bytes_ = count; }
    [[nodiscard]] static ::foundation::core::FixedText<kChildOutputCapacity>& output(ChildReport& report) noexcept {
        return report.output_;
    }
};

// Reads the output of the child to its end, waits for the child, and gives
// its report.  The descriptor is closed in each case.  A read error closes
// the descriptor first, so a child that still writes ends, and the wait
// cannot block.  Complexity: O(n) in the bytes of the output.
[[nodiscard]] ::foundation::core::Result<ChildReport, ::foundation::os::Errno>
collect_child_(ChildParts parts) noexcept;

}  // namespace detail

inline Child::~Child() {
    if (id_ > 0) {
        static_cast<void>(detail::collect_child_(detail::ChildDoor::take(*this)));
    }
}

// Starts a child process that runs body, and then ends with the exit code 0
// (THE CHILD DOOR).  The parent gets the Child, or the error of the pipe or
// of the fork.  The body runs only in the child, and it may use only what is
// safe after a fork.
template <eff::IsExecCtx Ctx, typename Body>
    requires CtxFitsChildProcess<Ctx> && std::is_invocable_v<Body&>
[[nodiscard]] ::foundation::core::Result<Child, ::foundation::os::Errno> spawn_child(Ctx const& /*test*/,
                                                                                     Body&& body) noexcept {
    namespace os = ::foundation::os;
    // The arm of the start runs the body in the child and ends the child, so
    // the start is a match and not a map.
    using Spawned = ::foundation::core::Result<Child, os::Errno>;
    return os::detail::start_child_().match(
        [&body](os::detail::ChildStart start) noexcept -> Spawned {
            if (start.child == 0) {
                body();
                os::detail::exit_child_(0);
            }
            return detail::ChildDoor::make(start.child, start.error_output);
        },
        [](os::Errno error) noexcept -> Spawned { return ::foundation::core::err(error); });
}

// Reads the standard error of the child to its end, and waits for the end
// of the child.  The empty Child names no process, and gives echild with no
// system call.  Complexity: O(n) in the bytes of the output.
[[nodiscard]] inline ::foundation::core::Result<ChildReport, ::foundation::os::Errno> wait(Child&& child) noexcept {
    detail::ChildParts const parts = detail::ChildDoor::take(child);
    if (parts.child <= 0) {
        return ::foundation::core::err(::foundation::os::Errno::echild);
    }
    return detail::collect_child_(parts);
}

// Ends the calling process with code (THE CHILD DOOR).  No exit hook and no
// static destructor runs, so a child ends with no effect on the state that it
// shares with its parent, such as a file that the parent opened.
template <eff::IsExecCtx Ctx>
    requires CtxFitsChildProcess<Ctx>
[[noreturn]] inline void exit_now(Ctx const& /*test*/, std::uint8_t code) noexcept {
    ::foundation::os::detail::exit_child_(code);
}

}  // namespace fixy::process
