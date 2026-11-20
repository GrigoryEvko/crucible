#pragma once

// The process doors of fixy.
//
// THE IDENTITY DOOR
//   The id of the calling process, the id of the calling thread and the
//   effective user id of the process, as the kernel gives them.  The door file
//   src/fixy/os/Process.cpp issues getpid, gettid and geteuid with the raw
//   primitive of foundation/os/Syscall.h.  The three calls cannot fail.  Each
//   read takes a context that admits the row of the family of the calls in the
//   syscall catalog, the family of the reads of the process state.
//
// THE PROCESS PROBE
//   probe_process(ctx, process) tells if a process of the id process lives.
//   The door file opens a descriptor of the process with the raw pidfd_open,
//   and it closes the descriptor at once.  The answer is one of three:
//     alive     the open gave a descriptor.  A child that ended and that its
//               parent did not wait for, a zombie, holds its id, so it is
//               alive
//     gone      the kernel holds no process of the id (esrch)
//     unknown   each other error, such as emfile when the process holds its
//               largest count of descriptors
//   detail::judge_probe_error_ gives the answer of each error.  An error
//   eperm counts as alive, because the kernel refuses only a process that it
//   holds.  The kernel reads the id as a signed word of 32 bits.  So the id
//   zero, an id above 2^31 - 1, and the id of a thread that is not the first
//   thread of its process give an error other than esrch, and the answer
//   unknown.
//
//   The answer is a fact of the time of the call.  The kernel can give a
//   free id to a new process, so an id that is gone can be alive at the next
//   call.  The kernel reads the id in the pid namespace of the caller, so a
//   process of another namespace is gone or a different process.  The probe
//   reads the process state, so it takes the context of an identity read.
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
// THE WAIT WITH A LIMIT
//   wait_within(std::move(child), limit) waits for the end of the child for
//   at most limit, and it reads no byte of the standard error of the child
//   while the child runs.  So a child that fills its pipe keeps it full, and
//   its next write blocks.  When the limit passes, the door ends the child
//   with SIGKILL (kLimitSignal), and is_ended_at_limit() of the report is
//   true.  After the end, the wait reads the pipe to its end, and the report
//   holds its last bytes.  The wait looks at the child one time in each tick
//   of 100 microseconds, so it sees an end at most one tick late.
//
// THE TRACED CHILD
//   spawn_traced_child(test, memory, body) starts a child as spawn_child
//   does, and the calling thread becomes its tracer.  The child calls
//   PTRACE_TRACEME and stops itself with SIGSTOP before its body runs.  A
//   host that refuses the trace gives the error eperm.  The child has the
//   option PTRACE_O_EXITKILL, so it ends when its tracer ends.
//
//   memory tells what the tracer can read.  TracedMemory::sealed keeps the
//   child not dumpable (THE CORE DUMP of foundation/os/ChildProcess.h): the
//   tracer reads the stops of the child, and the kernel refuses a read of its
//   memory and of the links of /proc/PID/fd.  TracedMemory::readable makes
//   the child dumpable before it stops, so the tracer reads them too, and a
//   signal that ends the child can write a core dump.
//
//   wait_within(std::move(traced), limit) runs the traced child to its end.
//   trace_within(std::move(traced), limit, visit) also stops the child at
//   each entry and each exit of a system call, and calls visit with the stop.
//   Each wait of a traced child delivers each signal of the child, so a
//   signal ends a traced child as it ends a child with no tracer.  The one
//   exception is a stop signal: the tracer drops it, so a traced child never
//   stays in a stop.  Each wait of a traced child has a limit, and it reads
//   no output while the child runs (THE WAIT WITH A LIMIT).  A TracedChild
//   that no wait took ends its child with SIGKILL in its destructor.
//
//   The kernel takes the requests of a tracer only from the thread that
//   started the child.  So the start and the wait of a traced child run on
//   one thread.
//
// This header includes no system header.  The door file src/fixy/os/Process.cpp
// issues each ptrace call and the open of the probe, and src/fixy/os/Signal.cpp
// raises SIGSTOP and sends SIGKILL.

#include <fixy/atoms/Syscall.h>
#include <foundation/contracts/Pre.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Quantity.h>
#include <foundation/core/Text.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Lift.h>
#include <foundation/effects/Row.h>
#include <foundation/os/ChildProcess.h>
#include <foundation/os/Errno.h>

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace fixy::process {

namespace eff = ::foundation::effects;

// The id of a process: the id of its thread group, which getpid gives.
enum class ProcessId : std::uint32_t {
};

// The id of a thread, which gettid gives.  The id of the first thread of a
// process is the id of the process.
enum class ThreadId : std::uint32_t {
};

// The id of a user.  The effective user id of a process owns each file that
// the process makes, with no change of its file system id.
enum class UserId : std::uint32_t {
};

// The answer of the probe of a process (THE PROCESS PROBE).
enum class ProcessState : std::uint8_t {
    alive,
    gone,
    unknown,
};

namespace detail {

// The answer of the probe for an error of the open (THE PROCESS PROBE).
[[nodiscard]] constexpr ProcessState judge_probe_error_(::foundation::os::Errno error) noexcept {
    if (error == ::foundation::os::Errno::esrch) return ProcessState::gone;
    if (error == ::foundation::os::Errno::eperm) return ProcessState::alive;
    return ProcessState::unknown;
}

// The calls of the door file.  current_user_ gives the effective user id of
// the process (geteuid), which cannot fail.
[[nodiscard]] ProcessId current_process_() noexcept;
[[nodiscard]] ThreadId current_thread_() noexcept;
[[nodiscard]] ProcessState probe_process_(ProcessId process) noexcept;
[[nodiscard]] UserId current_user_() noexcept;

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

// The effective user id of the process.
template <eff::IsExecCtx Ctx>
    requires CtxFitsIdentityRead<Ctx>
[[nodiscard]] inline UserId current_user(Ctx const&) noexcept {
    return detail::current_user_();
}

// The state of the process of the id process (THE PROCESS PROBE).
template <eff::IsExecCtx Ctx>
    requires CtxFitsIdentityRead<Ctx>
[[nodiscard]] inline ProcessState probe_process(Ctx const&, ProcessId process) noexcept {
    return detail::probe_process_(process);
}

// A context that can start, wait for and end a child process: one that owns
// the Test capability.  A unit that ships has no such context, because only
// the testing door of foundation/effects/Effect.h mints one.
template <typename Ctx>
concept CtxFitsChildProcess = eff::IsExecCtx<Ctx> && eff::CtxOwnsCapability<Ctx, eff::Effect::Test>;

// The count of the last bytes of the standard error of a child that a
// ChildReport holds.
inline constexpr std::size_t kChildOutputCapacity = 16384;

// The longest time that a wait with a limit waits for its child (THE WAIT
// WITH A LIMIT).
using WaitLimit = ::foundation::core::Quantity<::foundation::core::unit::Millisecond, std::uint32_t>;

// The signal that ends a child at the limit of its wait: SIGKILL.
// src/fixy/os/Signal.cpp checks the value against <signal.h>.
inline constexpr std::uint8_t kLimitSignal = 9;

// The exit code of a child that the host does not let its parent trace.  The
// child ends before its body runs.
inline constexpr std::uint8_t kChildTraceRefused = 126;

// What the tracer of a traced child can read (THE TRACED CHILD): only its
// stops, or also its memory and the links of /proc/PID/fd.
enum class TracedMemory : std::uint8_t {
    sealed,
    readable,
};

namespace detail {

// The door of the child door to the parts of a Child, of a TracedChild, of a
// SyscallStop and of a ChildReport.  Only the functions of this header and
// of src/fixy/os/Process.cpp use it.
struct ChildDoor;

// The signals that a wait of a traced child reads by their numbers: SIGTRAP,
// which a stop of a system call carries, and the four stop signals, which
// the tracer drops.  src/fixy/os/Signal.cpp checks each value against
// <signal.h>.
inline constexpr std::uint8_t kTrapSignal = 5;
inline constexpr std::uint8_t kStopSignal = 19;
inline constexpr std::uint8_t kTerminalStopSignal = 20;
inline constexpr std::uint8_t kTerminalInputSignal = 21;
inline constexpr std::uint8_t kTerminalOutputSignal = 22;

// How a wait ended: the child ended by itself, or the door ended it at the
// limit of the wait.
enum class WaitEnd : std::uint8_t {
    by_child,
    at_limit,
};

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
        : end_{other.end_}, wait_end_{other.wait_end_}, dropped_bytes_{other.dropped_bytes_}, output_{other.output_} {}
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

    // True when the child ran to the limit of a wait with a limit, and the
    // door ended it with kLimitSignal (THE WAIT WITH A LIMIT).
    [[nodiscard]] bool is_ended_at_limit() const noexcept { return wait_end_ == detail::WaitEnd::at_limit; }

    [[nodiscard]] ::foundation::core::TextView output() const& noexcept { return output_.view(); }
    ::foundation::core::TextView
    output() const&& = delete("the output of a temporary report points into an object that the full expression ends");

    [[nodiscard]] std::uint64_t dropped_bytes() const noexcept { return dropped_bytes_; }

private:
    ChildReport() noexcept = default;

    friend struct detail::ChildDoor;

    ::foundation::os::ChildEnd end_{};
    detail::WaitEnd wait_end_ = detail::WaitEnd::by_child;
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

// One child process that the calling thread traces, and the read end of the
// pipe of its standard error (THE TRACED CHILD).  Only spawn_traced_child
// makes a TracedChild that names a process, and a wait takes it.  A
// TracedChild that no wait took ends its child with SIGKILL in its
// destructor.
class [[nodiscard]] TracedChild {
public:
    // The empty TracedChild names no process and holds no descriptor.  A wait
    // for it gives the error echild.
    TracedChild() noexcept = default;

    TracedChild(TracedChild&& other) noexcept : id_{other.id_}, error_output_{other.error_output_} {
        other.id_ = 0;
        other.error_output_ = -1;
    }
    TracedChild& operator=(TracedChild&&) = delete("a TracedChild names one process: wait for it, and keep the report");
    TracedChild(TracedChild const&) = delete("a TracedChild names one process, and one thread traces it");
    TracedChild& operator=(TracedChild const&) = delete("a TracedChild names one process, and one thread traces it");

    ~TracedChild();

    [[nodiscard]] ProcessId id() const noexcept { return static_cast<ProcessId>(static_cast<std::uint32_t>(id_)); }

private:
    TracedChild(std::int32_t id, std::int32_t error_output) noexcept : id_{id}, error_output_{error_output} {}

    friend struct detail::ChildDoor;

    std::int32_t id_ = 0;
    std::int32_t error_output_ = -1;
};

// Which side of a system call a stop of a traced child is on.
enum class SyscallStopKind : std::uint8_t {
    entry,
    exit,
};

// The number of a system call on the architecture of the build, as the
// kernel gives it to a tracer.
enum class SyscallNumber : std::uint64_t {
};

// An address in the memory of a traced child.
enum class ChildAddress : std::uint64_t {
};

// The position of an argument of a system call.
enum class SyscallArgument : std::uint8_t {
    first,
    second,
    third,
    fourth,
    fifth,
    sixth,
};

// The count of the arguments of a system call.
inline constexpr std::size_t kSyscallArguments = 6;

// One stop of a traced child at the entry or at the exit of a system call,
// which trace_within gives its visitor.  The entry has the number and the
// arguments of the call, and the exit has the word that the call gives back.
// Only trace_within makes a stop.  A stop is valid only in the call of the
// visitor that gets it, while the child stays in the stop.
class SyscallStop {
public:
    // The copy is user-provided, so a stop is not trivially copyable, and no
    // copy of bytes builds one.
    SyscallStop(SyscallStop const& other) noexcept
        : child_{other.child_},
          kind_{other.kind_},
          number_{other.number_},
          arguments_{other.arguments_},
          result_{other.result_} {}
    SyscallStop& operator=(SyscallStop const&) = delete("a stop is the view of one stop of the child");

    [[nodiscard]] SyscallStopKind kind() const noexcept { return kind_; }

    [[nodiscard]] ProcessId child() const noexcept {
        return static_cast<ProcessId>(static_cast<std::uint32_t>(child_));
    }

    // The number of the call.  Only an entry has it.
    [[nodiscard]] SyscallNumber number() const noexcept {
        CRUCIBLE_PRE(kind_ == SyscallStopKind::entry);
        return number_;
    }

    // An argument word of the call.  Only an entry has them.
    [[nodiscard]] std::uint64_t argument(SyscallArgument which) const noexcept {
        CRUCIBLE_PRE(kind_ == SyscallStopKind::entry);
        return *arguments_.get(static_cast<std::size_t>(which)).expect("each SyscallArgument names one of six words");
    }

    // The word that the call gives back: a value, or -E for the error number
    // E.  Only an exit has it.
    [[nodiscard]] std::int64_t result() const noexcept {
        CRUCIBLE_PRE(kind_ == SyscallStopKind::exit);
        return result_;
    }

    // Reads the text at address in the memory of the child, up to its zero,
    // into text, which it empties first.  A text longer than the capacity
    // gives enametoolong, and a word that does not read gives the error of the
    // read.  Only the memory of a readable child reads (TracedMemory), and a
    // sealed child gives eio.  The platform is little-endian, so the first byte of each word of
    // the read comes first.  Complexity: O(Capacity), one ptrace call for each
    // eight bytes.
    template <std::size_t Capacity>
    [[nodiscard]] ::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
    read_text(ChildAddress address, ::foundation::core::FixedText<Capacity>& text) const noexcept;

private:
    SyscallStop() noexcept {}

    friend struct detail::ChildDoor;

    std::int32_t child_ = 0;
    SyscallStopKind kind_ = SyscallStopKind::entry;
    SyscallNumber number_{};
    ::foundation::core::FixedArray<std::uint64_t, kSyscallArguments> arguments_{};
    std::int64_t result_ = 0;
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

    [[nodiscard]] static TracedChild make_traced(std::int32_t child, std::int32_t error_output) noexcept {
        return TracedChild{child, error_output};
    }

    // The parts of the traced child.  The TracedChild then names no process.
    [[nodiscard]] static ChildParts take(TracedChild& child) noexcept {
        ChildParts const parts{child.id_, child.error_output_};
        child.id_ = 0;
        child.error_output_ = -1;
        return parts;
    }

    [[nodiscard]] static SyscallStop
    make_entry_stop(std::int32_t child, SyscallNumber number,
                    ::foundation::core::FixedArray<std::uint64_t, kSyscallArguments> const& arguments) noexcept {
        SyscallStop stop{};
        stop.child_ = child;
        stop.kind_ = SyscallStopKind::entry;
        stop.number_ = number;
        stop.arguments_ = arguments;
        return stop;
    }

    [[nodiscard]] static SyscallStop make_exit_stop(std::int32_t child, std::int64_t result) noexcept {
        SyscallStop stop{};
        stop.child_ = child;
        stop.kind_ = SyscallStopKind::exit;
        stop.result_ = result;
        return stop;
    }

    [[nodiscard]] static std::int32_t child_of(SyscallStop const& stop) noexcept { return stop.child_; }

    [[nodiscard]] static ChildReport make_report() noexcept { return ChildReport{}; }
    static void set_end(ChildReport& report, ::foundation::os::ChildEnd end) noexcept { report.end_ = end; }
    static void set_wait_end(ChildReport& report, WaitEnd wait_end) noexcept { report.wait_end_ = wait_end; }
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

// Waits for the end of the child for at most limit, ends it with
// kLimitSignal at the limit, then reads its output to its end and gives its
// report (THE WAIT WITH A LIMIT).  The descriptor is closed in each case.
// Complexity: one look at the child in each tick, and O(n) in the bytes of
// the output.
[[nodiscard]] ::foundation::core::Result<ChildReport, ::foundation::os::Errno>
collect_child_within_(ChildParts parts, WaitLimit limit) noexcept;

// The first step of the child of spawn_traced_child: PTRACE_TRACEME, the
// dumpable flag of a readable child, then a stop by SIGSTOP.  A refused trace
// ends the child with kChildTraceRefused.
void enter_trace_(TracedMemory memory) noexcept;

// The parent side of spawn_traced_child: waits for the first stop of the
// child, and gives it the options PTRACE_O_TRACESYSGOOD and
// PTRACE_O_EXITKILL.  A child that ended before its stop gives eperm when it
// exited with kChildTraceRefused, and eio on each other end.  On each error,
// the child is ended and collected, and the descriptor is closed.
[[nodiscard]] ::foundation::core::Result<TracedChild, ::foundation::os::Errno>
attach_tracer_(ChildParts parts) noexcept;

// Ends the traced child with SIGKILL, collects it, and closes the descriptor.
void discard_traced_child_(ChildParts parts) noexcept;

// Reads the word at address in the memory of the traced child with
// PTRACE_PEEKDATA.
[[nodiscard]] ::foundation::core::Result<std::uint64_t, ::foundation::os::Errno>
peek_child_word_(std::int32_t child, ChildAddress address) noexcept;

// The stops at which a wait of a traced child calls its visitor: none, or each
// entry and each exit of a system call.
enum class TraceStops : std::uint8_t {
    signals_only,
    system_calls,
};

// Where a wait of a traced child is: the child runs, or the wait has its end.
enum class TraceState : std::uint8_t {
    running,
    finished,
};

// The state of a wait of a traced child between its steps.  deadline is the
// time of CLOCK_MONOTONIC in nanoseconds at which the wait ends the child.
// pending_signal is the signal that the next resume delivers.  error holds
// the first error of the wait, and the wait then ended the child.
struct TraceRun {
    ChildParts parts{};
    TraceStops stops = TraceStops::signals_only;
    TraceState state = TraceState::running;
    std::uint8_t pending_signal = 0;
    WaitEnd wait_end = WaitEnd::by_child;
    ::foundation::os::ChildEnd end{};
    std::uint64_t deadline = 0;
    ::foundation::core::Option<::foundation::os::Errno> error{};
};

// Starts a wait of the traced child with the deadline of now plus limit.
[[nodiscard]] TraceRun begin_trace_(ChildParts parts, TraceStops stops, WaitLimit limit) noexcept;

// Resumes the child, and gives its next stop at a system call.  The wait
// delivers each signal of the child, other than a stop signal.  No stop
// means that the run is finished: the child ended, the limit passed, or an
// error came.
[[nodiscard]] ::foundation::core::Option<SyscallStop> next_syscall_stop_(TraceRun& run) noexcept;

// Reads the output of the child of a finished run to its end, closes the
// descriptor, and gives the report, or the first error of the run.
[[nodiscard]] ::foundation::core::Result<ChildReport, ::foundation::os::Errno> end_trace_(TraceRun& run) noexcept;

}  // namespace detail

inline Child::~Child() {
    if (id_ > 0) {
        static_cast<void>(detail::collect_child_(detail::ChildDoor::take(*this)));
    }
}

inline TracedChild::~TracedChild() {
    if (id_ > 0) {
        detail::discard_traced_child_(detail::ChildDoor::take(*this));
    }
}

template <std::size_t Capacity>
::foundation::core::Result<::foundation::core::Unit, ::foundation::os::Errno>
SyscallStop::read_text(ChildAddress address, ::foundation::core::FixedText<Capacity>& text) const noexcept {
    namespace core = ::foundation::core;
    namespace os = ::foundation::os;
    constexpr std::uint64_t word_bytes = 8;
    constexpr std::uint64_t byte_bits = 8;
    text = core::FixedText<Capacity>{};
    std::uint64_t at = static_cast<std::uint64_t>(address);
    // Each word either holds the zero or adds eight characters, and a text of
    // Capacity characters ends the loop, so the loop is bounded.
    for (;;) {
        core::Result<std::uint64_t, os::Errno> read = detail::peek_child_word_(child_, ChildAddress{at});
        if (read.is_err()) {
            return core::err(read.err().value_or(os::Errno::eio));
        }
        std::uint64_t const word = std::move(read).value_or(0);
        for (std::uint64_t index = 0; index < word_bytes; ++index) {
            char const character = static_cast<char>((word >> (index * byte_bits)) & 0xFFU);
            if (character == '\0') {
                return core::Unit{};
            }
            if (text.append(character).is_err()) {
                return core::err(os::Errno::enametoolong);
            }
        }
        at += word_bytes;
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

// Waits for the end of the child for at most limit, and reads no output
// while the child runs (THE WAIT WITH A LIMIT).  At the limit, the door ends
// the child with kLimitSignal, and is_ended_at_limit() of the report is true.
// The empty Child gives echild with no system call.
[[nodiscard]] inline ::foundation::core::Result<ChildReport, ::foundation::os::Errno>
wait_within(Child&& child, WaitLimit limit) noexcept {
    detail::ChildParts const parts = detail::ChildDoor::take(child);
    if (parts.child <= 0) {
        return ::foundation::core::err(::foundation::os::Errno::echild);
    }
    return detail::collect_child_within_(parts, limit);
}

// Starts a child process that the calling thread traces, and that runs body
// and then ends with the exit code 0 (THE TRACED CHILD).  memory tells what
// the tracer can read.  The parent gets the TracedChild, or the error of the
// pipe, of the fork or of the trace.  The body runs only in the child, and it
// may use only what is safe after a fork.
template <eff::IsExecCtx Ctx, typename Body>
    requires CtxFitsChildProcess<Ctx> && std::is_invocable_v<Body&>
[[nodiscard]] ::foundation::core::Result<TracedChild, ::foundation::os::Errno>
spawn_traced_child(Ctx const& /*test*/, TracedMemory memory, Body&& body) noexcept {
    namespace os = ::foundation::os;
    using Spawned = ::foundation::core::Result<TracedChild, os::Errno>;
    return os::detail::start_child_().match(
        [&body, memory](os::detail::ChildStart start) noexcept -> Spawned {
            if (start.child == 0) {
                detail::enter_trace_(memory);
                body();
                os::detail::exit_child_(0);
            }
            return detail::attach_tracer_(detail::ChildParts{start.child, start.error_output});
        },
        [](os::Errno error) noexcept -> Spawned { return ::foundation::core::err(error); });
}

// Runs the traced child to its end for at most limit, and delivers each of
// its signals other than a stop signal (THE TRACED CHILD).  The empty
// TracedChild gives echild with no system call.
[[nodiscard]] inline ::foundation::core::Result<ChildReport, ::foundation::os::Errno>
wait_within(TracedChild&& child, WaitLimit limit) noexcept {
    detail::ChildParts const parts = detail::ChildDoor::take(child);
    if (parts.child <= 0) {
        return ::foundation::core::err(::foundation::os::Errno::echild);
    }
    detail::TraceRun run = detail::begin_trace_(parts, detail::TraceStops::signals_only, limit);
    while (detail::next_syscall_stop_(run).is_some()) {}
    return detail::end_trace_(run);
}

// Runs the traced child to its end for at most limit, as wait_within does,
// and stops it at each entry and each exit of a system call.  visit gets each
// stop, while the child stays in it.  The empty TracedChild gives echild
// with no system call.  Complexity: two stops for each system call of the
// child.
template <typename Visit>
    requires std::is_invocable_v<Visit&, SyscallStop const&>
[[nodiscard]] ::foundation::core::Result<ChildReport, ::foundation::os::Errno>
trace_within(TracedChild&& child, WaitLimit limit, Visit&& visit) noexcept {
    detail::ChildParts const parts = detail::ChildDoor::take(child);
    if (parts.child <= 0) {
        return ::foundation::core::err(::foundation::os::Errno::echild);
    }
    detail::TraceRun run = detail::begin_trace_(parts, detail::TraceStops::system_calls, limit);
    for (;;) {
        ::foundation::core::Option<SyscallStop> stop = detail::next_syscall_stop_(run);
        if (stop.is_none()) {
            break;
        }
        stop.match([&visit](SyscallStop const& seen) noexcept { visit(seen); }, [] noexcept {});
    }
    return detail::end_trace_(run);
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
