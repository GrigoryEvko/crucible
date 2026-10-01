#pragma once

#include <fixy/Borrowed.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace crucible::perf {

// The kernel-side program writes into the same byte offsets userspace
// reads, and it writes ts_ns last.  A slot with ts_ns == 0 is still
// being filled and its other fields are meaningless.
//
// The trailing pad brings the struct to 32 bytes, which divides the
// 64-byte cache line evenly, so no slot in the events array straddles
// two lines.  A straddling reader can see a committed ts_ns in the
// second line while the fields in the first line are still stale,
// which breaks the completion-marker rule above.
struct TimelineSyscallEvent {
    uint64_t duration_ns;
    uint32_t tid;
    uint32_t syscall_nr;
    uint64_t ts_ns;  // kernel monotonic clock
    uint64_t _pad;
};

}  // namespace crucible::perf

#include <crucible/perf/SchedSwitch.h>  // for TimelineHeader, TIMELINE_CAPACITY

namespace crucible::perf {

class SyscallLatency {
public:
    // Both fields count upward forever, so an ordered post-minus-pre
    // subtraction never underflows.  The saturation below guards
    // against a caller that swapped the two snapshots.  A
    // timeline_index delta larger than the ring capacity means the
    // window overwrote its own oldest events.
    struct Snapshot {
        uint64_t total_syscalls = 0;
        uint64_t timeline_index = 0;

        [[nodiscard]] Snapshot operator-(const Snapshot& older) const noexcept {
            Snapshot r;
            if (__builtin_sub_overflow(total_syscalls, older.total_syscalls, &r.total_syscalls)) [[unlikely]] {
                r.total_syscalls = 0;
            }
            if (__builtin_sub_overflow(timeline_index, older.timeline_index, &r.timeline_index)) [[unlikely]] {
                r.timeline_index = 0;
            }
            return r;
        }
    };

    [[nodiscard]] Snapshot snapshot() const noexcept;

    // Both the syscall-enter and the syscall-exit tracepoint must
    // attach.  A half attach records entry timestamps that no exit
    // ever consumes, so the load fails instead.
    //
    // Returns nullopt when the kernel refuses: no CAP_BPF or
    // CAP_PERFMON, a kernel without the raw syscall tracepoints, or a
    // verifier rejection.  A diagnostic line goes to stderr unless
    // CRUCIBLE_PERF_QUIET=1 is set in the environment, and
    // CRUCIBLE_PERF_VERBOSE=1 forwards the libbpf INFO and WARN
    // messages as well.
    //
    // The load takes the startup load context.  Its row carries Block,
    // because the load waits in the kernel while the verifier examines
    // the program.
    [[nodiscard]] static std::optional<SyscallLatency> load(::fixy::InitLoadCtx const&) noexcept;

    // Reading the count costs a map-lookup syscall, and that syscall
    // is itself counted.  A tight loop over this accessor therefore
    // measures the loop rather than the workload.  Take one count
    // before the region and one after it.
    [[nodiscard]] uint64_t total_syscalls() const noexcept;

    // The element type is const rather than const volatile because
    // libstdc++ cannot instantiate std::span over a volatile
    // non-scalar element.  The kernel writes this memory while the
    // reader walks it, so read ts_ns through an acquire load of its
    // own and trust the rest of the slot only when ts_ns is non-zero.
    [[nodiscard]] ::fixy::Borrowed<const TimelineSyscallEvent, SyscallLatency> timeline_view() const noexcept;

    // The index counts events forever, so the most recently written
    // slot is `(write_idx - 1) & TIMELINE_MASK`.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    [[nodiscard]] ::fixy::Refined<::fixy::bounded_above<8>, std::size_t> attached_programs() const noexcept;

    // Set CRUCIBLE_PERF_VERBOSE=1 in the environment to see which
    // tracepoint was unavailable.
    [[nodiscard]] ::fixy::Refined<::fixy::bounded_above<8>, std::size_t> attach_failures() const noexcept;

    SyscallLatency(const SyscallLatency&) =
        delete("SyscallLatency owns unique BPF object + mmap — copying would double-close");
    SyscallLatency& operator=(const SyscallLatency&) =
        delete("SyscallLatency owns unique BPF object + mmap — copying would double-close");
    SyscallLatency(SyscallLatency&&) noexcept;
    SyscallLatency& operator=(SyscallLatency&&) noexcept;
    ~SyscallLatency();

private:
    struct State;
    SyscallLatency() noexcept;

    std::unique_ptr<State> state_;
};

// The load attaches to the raw syscall tracepoints, maps the timeline
// ring, and calls bpf(BPF_PROG_LOAD).  That last call enters the kernel
// and waits while the verifier walks the program.  The wait is why the
// row carries Block.  IO covers the bpf, perf_event_open and mmap
// traffic.  Alloc covers the state the load path takes from the heap.
//
// The startup load context and the background load context each claim
// Block on top of Alloc and IO, so each passes this gate.  The cold init
// context and the compile context stop at IO, and the gate refuses
// both.  The second argument is the startup load context that the load
// takes.

using syscall_latency_required_row =
    ::foundation::effects::Row<::foundation::effects::Effect::Alloc, ::foundation::effects::Effect::IO,
                               ::foundation::effects::Effect::Block>;

template <class Ctx>
concept CtxFitsSyscallLatencyMint = ::foundation::effects::IsExecCtx<Ctx>
                                 && ::foundation::effects::Subrow<syscall_latency_required_row, typename Ctx::row_type>;

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSyscallLatencyMint<Ctx>
// §XXI carve-out: cx=alloc — the load path maps the timeline ring and
// heap-allocates State.  Compile-time evaluation would lie about the
// runtime cost.
[[nodiscard]] inline std::optional<SyscallLatency> mint_syscall_latency(Ctx const&,
                                                                        ::fixy::InitLoadCtx const& init) noexcept {
    return SyscallLatency::load(init);
}

}  // namespace crucible::perf
