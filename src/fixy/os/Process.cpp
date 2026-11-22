// The process doors of fixy (fixy/os/Process.h).  The identity door issues
// the raw primitive of foundation/os/Syscall.h: the kernel gives the id of the
// process and of the thread as a positive word and the effective user id as a
// word that is not negative, and the three calls cannot fail.  The child door reads the
// output of a child and waits for it through the steps of
// foundation/os/ChildProcess.h.  The trace door issues each ptrace call raw,
// and this file is the one includer of <linux/ptrace.h>, which gives the
// requests, the options and struct ptrace_syscall_info.  The signal door of
// src/fixy/os/Signal.cpp raises SIGSTOP and sends SIGKILL.  The probe opens a
// descriptor of a process with the raw pidfd_open.  The rule table denies a
// raw kill, and a kill of the C library gives its error in errno, which only
// src/foundation/os/Intercepted.cpp reads.

#include <fixy/os/Process.h>

#include <fixy/os/Fd.h>
#include <fixy/os/MonotonicClock.h>
#include <fixy/os/Signal.h>
#include <foundation/core/Array.h>
#include <foundation/core/Choice.h>
#include <foundation/core/Text.h>
#include <foundation/os/ChildProcess.h>
#include <foundation/os/Errno.h>
#include <foundation/os/FutexWords.h>
#include <foundation/os/Syscall.h>

#include <linux/ptrace.h>

#include <cstddef>
#include <cstdint>
#include <utility>

namespace fixy::process::detail {

namespace {

namespace core = ::foundation::core;
namespace os = ::foundation::os;

// The bytes of one read of the output of a child.
constexpr std::size_t kReadChunk = 4096;

// The time between two looks at a child in a wait with a limit.
constexpr std::uint64_t kTickNanos = 100000;

constexpr std::uint64_t kNanosPerMilli = 1000000;
constexpr std::uint64_t kNanosPerSecond = 1000000000;

// The bit that PTRACE_O_TRACESYSGOOD adds to SIGTRAP at a stop of a system
// call, so the stop differs from a SIGTRAP that the child gets.
constexpr std::uint8_t kSystemCallBit = 0x80;

// The last kChildOutputCapacity bytes of a run of reads.  Each byte goes to
// the position of its count modulo the capacity, so a new byte replaces the
// oldest byte when the ring is full.
struct OutputRing {
    core::FixedArray<char, kChildOutputCapacity> bytes{};
    std::uint64_t total = 0;

    void put(core::View<char const> run) noexcept {
        bytes.with_mut([this, run](auto slots) noexcept {
            for (char const character : run) {
                *slots.get(total % kChildOutputCapacity).expect("a position modulo the capacity is in the ring") =
                    character;
                ++total;
            }
        });
    }
};

// Reads the output of a child into the ring to its end, when the child and
// each copy of the write end are closed.  Gives the read error, or none.
// The descriptor stays open.  Complexity: O(n) in the bytes of the output.
[[nodiscard]] core::Option<os::Errno> read_output_to_end(std::int32_t error_output, OutputRing& ring) noexcept {
    core::FixedArray<char, kReadChunk> chunk{};
    return chunk.with_mut([error_output, &ring](auto run) noexcept -> core::Option<os::Errno> {
        for (;;) {
            core::Result<std::size_t, os::Errno> taken = os::detail::read_child_output_(error_output, run.view());
            if (taken.is_err()) {
                return taken.err();
            }
            std::size_t const count = std::move(taken).value_or(0);
            if (count == 0) {
                return core::none;
            }
            ring.put(
                core::View<char>{run.view()}.window(0, count).expect("a read gives at most the size of its buffer"));
        }
    });
}

// The report of a child that ended: its end, how the wait ended, and the last
// bytes of the ring in their order.
[[nodiscard]] ChildReport make_report(OutputRing const& ring, os::ChildEnd end, WaitEnd wait_end) noexcept {
    ChildReport report = ChildDoor::make_report();
    ChildDoor::set_end(report, end);
    ChildDoor::set_wait_end(report, wait_end);
    bool const has_wrapped = ring.total > kChildOutputCapacity;
    std::size_t const kept = has_wrapped ? kChildOutputCapacity : ring.total;
    std::size_t const oldest = has_wrapped ? ring.total % kChildOutputCapacity : 0;
    ChildDoor::set_dropped_bytes(report, ring.total - kept);
    core::FixedText<kChildOutputCapacity>& output = ChildDoor::output(report);
    char* const text = core::detail::text_bytes_(output);
    for (std::size_t index = 0; index < kept; ++index) {
        text[index] = *ring.bytes.get((oldest + index) % kChildOutputCapacity)
                           .expect("a position modulo the capacity is in the ring");
    }
    core::detail::text_resize_(output, kept);
    return report;
}

// The time of CLOCK_MONOTONIC in nanoseconds, or the error of the read.
[[nodiscard]] core::Result<std::uint64_t, os::Errno> monotonic_nanos() noexcept {
    return ::fixy::time::detail::read_clock_(::fixy::time::ClockId::monotonic)
        .map([](os::KernelTimespec time) noexcept {
            return static_cast<std::uint64_t>(time.seconds) * kNanosPerSecond
                 + static_cast<std::uint64_t>(time.nanoseconds);
        });
}

// The deadline of a wait that starts now and lasts limit.
[[nodiscard]] core::Result<std::uint64_t, os::Errno> deadline_after(WaitLimit limit) noexcept {
    return monotonic_nanos().map(
        [limit](std::uint64_t now) noexcept { return now + static_cast<std::uint64_t>(limit.raw()) * kNanosPerMilli; });
}

// Waits until the child changes, or until the deadline.  The change running
// means that the deadline passed.  Complexity: one look at the child in each
// tick until the change or the deadline.
[[nodiscard]] core::Result<os::detail::ChildEvent, os::Errno> wait_until(std::int32_t child,
                                                                         std::uint64_t deadline) noexcept {
    using Waited = core::Result<os::detail::ChildEvent, os::Errno>;
    for (;;) {
        Waited event = os::detail::wait_child_event_(child, os::detail::ChildWait::poll);
        if (event.is_err()) {
            return event;
        }
        os::detail::ChildEvent const seen = std::move(event).value_or(os::detail::ChildEvent{});
        if (seen.change != os::detail::ChildChange::running) {
            return seen;
        }
        core::Result<std::uint64_t, os::Errno> now = monotonic_nanos();
        if (now.is_err()) {
            return core::err(now.err().value_or(os::Errno::eio));
        }
        std::uint64_t const time = std::move(now).value_or(deadline);
        if (time >= deadline) {
            return os::detail::ChildEvent{};
        }
        std::uint64_t const left = deadline - time;
        core::Result<core::Unit, os::Errno> slept =
            ::fixy::time::detail::sleep_monotonic_(left < kTickNanos ? left : kTickNanos);
        if (slept.is_err()) {
            return core::err(slept.err().value_or(os::Errno::eio));
        }
    }
}

// Ends the child with SIGKILL, and waits for its end.  A traced child can
// report a stop that came before the signal, and the wait looks past it.
[[nodiscard]] core::Result<os::ChildEnd, os::Errno> end_child(std::int32_t child) noexcept {
    ::fixy::signal::detail::end_process_(child);
    for (;;) {
        core::Result<os::detail::ChildEvent, os::Errno> event =
            os::detail::wait_child_event_(child, os::detail::ChildWait::block);
        if (event.is_err()) {
            return core::err(event.err().value_or(os::Errno::eio));
        }
        os::detail::ChildEvent const seen = std::move(event).value_or(os::detail::ChildEvent{});
        if (seen.change == os::detail::ChildChange::ended) {
            return seen.end;
        }
    }
}

// Issues one ptrace request.  The word is zero or a value, or -E for the
// error number E.
[[nodiscard]] long issue_ptrace(long request, std::int32_t child, os::Word address, os::Word data) noexcept {
    return os::raw_syscall(os::Syscall::ptrace, request, child, address, data);
}

// True when the signal stops a process: the tracer drops it (the traced
// child of fixy/os/Process.h).
[[nodiscard]] bool is_stop_signal(std::uint8_t signal) noexcept {
    return signal == kStopSignal || signal == kTerminalStopSignal || signal == kTerminalInputSignal
        || signal == kTerminalOutputSignal;
}

// The stop of a system call of the child, with PTRACE_GET_SYSCALL_INFO.  A
// stop that is neither an entry nor an exit gives none.
[[nodiscard]] core::Result<core::Option<SyscallStop>, os::Errno> read_syscall_stop(std::int32_t child) noexcept {
    using Read = core::Result<core::Option<SyscallStop>, os::Errno>;
    ::ptrace_syscall_info info{};
    long const read = issue_ptrace(PTRACE_GET_SYSCALL_INFO, child, sizeof info, static_cast<void const*>(&info));
    if (read < 0) {
        return core::err(static_cast<os::Errno>(-read));
    }
    if (info.op == PTRACE_SYSCALL_INFO_ENTRY) {
        core::FixedArray<std::uint64_t, kSyscallArguments> arguments{};
        arguments.with_mut([&info](auto run) noexcept {
            for (std::size_t index = 0; index < kSyscallArguments; ++index) {
                *run.get(index).expect("the entry has six arguments") = info.entry.args[index];
            }
        });
        return Read{core::Option<SyscallStop>::some(
            ChildDoor::make_entry_stop(child, SyscallNumber{info.entry.nr}, arguments))};
    }
    if (info.op == PTRACE_SYSCALL_INFO_EXIT) {
        return Read{core::Option<SyscallStop>::some(ChildDoor::make_exit_stop(child, info.exit.rval))};
    }
    return Read{core::Option<SyscallStop>{}};
}

// Ends the run with its first error: ends the child with SIGKILL and
// collects it.
void fail_run(TraceRun& run, os::Errno error) noexcept {
    if (run.error.is_none()) {
        run.error = core::Option<os::Errno>::some(error);
    }
    static_cast<void>(end_child(run.parts.child));
    run.state = TraceState::finished;
}

}  // namespace

core::Result<ChildReport, os::Errno> collect_child_(ChildParts parts) noexcept {
    OutputRing ring{};
    core::Option<os::Errno> read_error = read_output_to_end(parts.error_output, ring);
    if (read_error.is_some()) {
        // A read error closes the descriptor first, so a child that still
        // writes ends, and the wait cannot block.
        os::detail::close_child_output_(parts.error_output);
        static_cast<void>(os::detail::wait_child_(parts.child));
        return core::err(std::move(read_error).value_or(os::Errno::eio));
    }
    os::detail::close_child_output_(parts.error_output);
    core::Result<os::ChildEnd, os::Errno> waited = os::detail::wait_child_(parts.child);
    if (waited.is_err()) {
        return core::err(waited.err().expect("the wait error is present"));
    }
    return make_report(ring, std::move(waited).value_or(os::ChildEnd{}), WaitEnd::by_child);
}

core::Result<ChildReport, os::Errno> collect_child_within_(ChildParts parts, WaitLimit limit) noexcept {
    core::Result<std::uint64_t, os::Errno> deadline = deadline_after(limit);
    core::Result<os::detail::ChildEvent, os::Errno> event =
        deadline.is_err()
            ? core::Result<os::detail::ChildEvent, os::Errno>{core::err(deadline.err().value_or(os::Errno::eio))}
            : wait_until(parts.child, std::move(deadline).value_or(0));
    if (event.is_err()) {
        static_cast<void>(end_child(parts.child));
        os::detail::close_child_output_(parts.error_output);
        return core::err(event.err().value_or(os::Errno::eio));
    }
    os::detail::ChildEvent const seen = std::move(event).value_or(os::detail::ChildEvent{});
    os::ChildEnd end = seen.end;
    WaitEnd wait_end = WaitEnd::by_child;
    if (seen.change != os::detail::ChildChange::ended) {
        // The limit passed.  A child with no tracer never stops, so each
        // change other than its end is the deadline.
        core::Result<os::ChildEnd, os::Errno> ended = end_child(parts.child);
        if (ended.is_err()) {
            os::detail::close_child_output_(parts.error_output);
            return core::err(ended.err().value_or(os::Errno::eio));
        }
        end = std::move(ended).value_or(os::ChildEnd{});
        wait_end = WaitEnd::at_limit;
    }
    OutputRing ring{};
    core::Option<os::Errno> read_error = read_output_to_end(parts.error_output, ring);
    os::detail::close_child_output_(parts.error_output);
    if (read_error.is_some()) {
        return core::err(std::move(read_error).value_or(os::Errno::eio));
    }
    return make_report(ring, end, wait_end);
}

void enter_trace_(TracedMemory memory) noexcept {
    if (issue_ptrace(PTRACE_TRACEME, 0, os::Word{}, os::Word{}) < 0) {
        os::detail::exit_child_(kChildTraceRefused);
    }
    if (memory == TracedMemory::readable) {
        os::detail::make_child_dumpable_();
    }
    ::fixy::signal::detail::stop_self_();
}

core::Result<TracedChild, os::Errno> attach_tracer_(ChildParts parts) noexcept {
    core::Result<os::detail::ChildEvent, os::Errno> first =
        os::detail::wait_child_event_(parts.child, os::detail::ChildWait::block);
    if (first.is_err()) {
        static_cast<void>(end_child(parts.child));
        os::detail::close_child_output_(parts.error_output);
        return core::err(first.err().value_or(os::Errno::eio));
    }
    os::detail::ChildEvent const seen = std::move(first).value_or(os::detail::ChildEvent{});
    if (seen.change == os::detail::ChildChange::ended) {
        os::detail::close_child_output_(parts.error_output);
        bool const is_refused = seen.end.kind == os::ChildEndKind::exited && seen.end.number == kChildTraceRefused;
        return core::err(is_refused ? os::Errno::eperm : os::Errno::eio);
    }
    long const options = PTRACE_O_TRACESYSGOOD | PTRACE_O_EXITKILL;
    long const set = issue_ptrace(PTRACE_SETOPTIONS, parts.child, os::Word{}, options);
    if (set < 0) {
        static_cast<void>(end_child(parts.child));
        os::detail::close_child_output_(parts.error_output);
        return core::err(static_cast<os::Errno>(-set));
    }
    return ChildDoor::make_traced(parts.child, parts.error_output);
}

void discard_traced_child_(ChildParts parts) noexcept {
    static_cast<void>(end_child(parts.child));
    os::detail::close_child_output_(parts.error_output);
}

core::Result<std::uint64_t, os::Errno> peek_child_word_(std::int32_t child, ChildAddress address) noexcept {
    std::uint64_t word = 0;
    long const read =
        issue_ptrace(PTRACE_PEEKDATA, child, static_cast<std::uint64_t>(address), static_cast<void const*>(&word));
    if (read < 0) {
        return core::err(static_cast<os::Errno>(-read));
    }
    return word;
}

TraceRun begin_trace_(ChildParts parts, TraceStops stops, WaitLimit limit) noexcept {
    TraceRun run{};
    run.parts = parts;
    run.stops = stops;
    deadline_after(limit).match([&run](std::uint64_t deadline) noexcept { run.deadline = deadline; },
                                [&run](os::Errno error) noexcept { fail_run(run, error); });
    return run;
}

core::Option<SyscallStop> next_syscall_stop_(TraceRun& run) noexcept {
    while (run.state == TraceState::running) {
        long const request = run.stops == TraceStops::system_calls ? PTRACE_SYSCALL : PTRACE_CONT;
        long const resumed = issue_ptrace(request, run.parts.child, os::Word{}, run.pending_signal);
        run.pending_signal = 0;
        if (resumed < 0) {
            fail_run(run, static_cast<os::Errno>(-resumed));
            break;
        }
        core::Result<os::detail::ChildEvent, os::Errno> event = wait_until(run.parts.child, run.deadline);
        if (event.is_err()) {
            fail_run(run, event.err().value_or(os::Errno::eio));
            break;
        }
        os::detail::ChildEvent const seen = std::move(event).value_or(os::detail::ChildEvent{});
        if (seen.change == os::detail::ChildChange::ended) {
            run.end = seen.end;
            run.state = TraceState::finished;
            break;
        }
        if (seen.change == os::detail::ChildChange::running) {
            core::Result<os::ChildEnd, os::Errno> ended = end_child(run.parts.child);
            run.state = TraceState::finished;
            run.wait_end = WaitEnd::at_limit;
            ended.match([&run](os::ChildEnd const& end) noexcept { run.end = end; },
                        [&run](os::Errno error) noexcept { run.error = core::Option<os::Errno>::some(error); });
            break;
        }
        if (seen.stop_signal == (kTrapSignal | kSystemCallBit)) {
            core::Result<core::Option<SyscallStop>, os::Errno> stop = read_syscall_stop(run.parts.child);
            if (stop.is_err()) {
                fail_run(run, stop.err().value_or(os::Errno::eio));
                break;
            }
            core::Option<SyscallStop> found = std::move(stop).value_or(core::Option<SyscallStop>{});
            if (found.is_some()) {
                return found;
            }
            continue;
        }
        run.pending_signal = is_stop_signal(seen.stop_signal) ? 0 : seen.stop_signal;
    }
    return core::none;
}

core::Result<ChildReport, os::Errno> end_trace_(TraceRun& run) noexcept {
    if (run.state == TraceState::running) {
        // Only a run that its caller left before the end gets here.
        core::Result<os::ChildEnd, os::Errno> ended = end_child(run.parts.child);
        run.state = TraceState::finished;
        run.wait_end = WaitEnd::at_limit;
        ended.match([&run](os::ChildEnd const& end) noexcept { run.end = end; },
                    [&run](os::Errno error) noexcept { run.error = core::Option<os::Errno>::some(error); });
    }
    OutputRing ring{};
    core::Option<os::Errno> read_error = read_output_to_end(run.parts.error_output, ring);
    os::detail::close_child_output_(run.parts.error_output);
    if (run.error.is_some()) {
        return core::err(std::move(run.error).value_or(os::Errno::eio));
    }
    if (read_error.is_some()) {
        return core::err(std::move(read_error).value_or(os::Errno::eio));
    }
    return make_report(ring, run.end, run.wait_end);
}

ProcessId current_process_() noexcept {
    return static_cast<ProcessId>(static_cast<std::uint32_t>(os::raw_syscall(os::Syscall::getpid)));
}

ThreadId current_thread_() noexcept {
    return static_cast<ThreadId>(static_cast<std::uint32_t>(os::raw_syscall(os::Syscall::gettid)));
}

UserId current_user_() noexcept {
    return static_cast<UserId>(static_cast<std::uint32_t>(os::raw_syscall(os::Syscall::geteuid)));
}

// The flags word of pidfd_open is zero: a descriptor of the process, with the
// close on exec that each pidfd has.
ProcessState probe_process_(ProcessId process) noexcept {
    return os::result_of(os::raw_syscall(os::Syscall::pidfd_open, std::to_underlying(process), 0))
        .match(
            [](std::uint64_t descriptor) noexcept {
                ::fixy::fs::detail::close_descriptor_(static_cast<int>(descriptor));
                return ProcessState::alive;
            },
            [](os::Errno error) noexcept { return judge_probe_error_(error); });
}

}  // namespace fixy::process::detail
