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
// two lines.  At 24 bytes a straddling reader can see a committed
// ts_ns in the second line while the fields in the first line are
// still stale, which breaks the completion-marker rule above.
struct TimelineSchedEvent {
    uint64_t off_cpu_ns;
    uint32_t tid;
    uint32_t on_cpu;
    uint64_t ts_ns;  // kernel monotonic clock
    uint64_t _pad;
};

struct TimelineHeader {
    uint64_t write_idx;
    uint64_t _pad[7];
};

// This value mirrors the ring size the kernel-side program was built
// with.  Changing one side alone breaks the shared layout.
constexpr uint32_t TIMELINE_CAPACITY = 4096;
[[maybe_unused]] constexpr uint32_t TIMELINE_MASK = TIMELINE_CAPACITY - 1;

class SchedSwitch {
public:
    // Both fields count upward forever, so an ordered post-minus-pre
    // subtraction never underflows.  The saturation below guards
    // against a caller that swapped the two snapshots.
    struct Snapshot {
        uint64_t ctx_switches = 0;
        uint64_t timeline_index = 0;

        [[nodiscard]] Snapshot operator-(const Snapshot& older) const noexcept {
            Snapshot r;
            if (__builtin_sub_overflow(ctx_switches, older.ctx_switches, &r.ctx_switches)) [[unlikely]] {
                r.ctx_switches = 0;
            }
            if (__builtin_sub_overflow(timeline_index, older.timeline_index, &r.timeline_index)) [[unlikely]] {
                r.timeline_index = 0;
            }
            return r;
        }
    };

    [[nodiscard]] Snapshot snapshot() const noexcept;

    // Returns nullopt when the kernel refuses the load: no CAP_BPF or
    // CAP_PERFMON, a kernel without the tracepoint, or a verifier
    // rejection.  A diagnostic line goes to stderr unless
    // CRUCIBLE_PERF_QUIET=1 is set in the environment, and
    // CRUCIBLE_PERF_VERBOSE=1 forwards the libbpf INFO and WARN
    // messages as well.
    //
    // The load takes the startup load context.  Its row carries Block,
    // because the load waits in the kernel while the verifier examines
    // the program.
    [[nodiscard]] static std::optional<SchedSwitch> load(::fixy::InitLoadCtx const&) noexcept;

    // Counts only this process, from the load onward.  Reading it
    // costs a map-lookup syscall: the counter lives in a one-element
    // array map, and the kernel-side program does not declare that
    // map mmap-able.
    [[nodiscard]] uint64_t context_switches() const noexcept;

    // Only the loading thread is registered with the kernel-side
    // program, so off-CPU events on the other threads of this process
    // never reach the ring.
    //
    // The element type is const rather than const volatile because
    // libstdc++ cannot instantiate std::span over a volatile
    // non-scalar element.  The kernel writes this memory while the
    // reader walks it, so read ts_ns through an acquire load of its
    // own and trust the rest of the slot only when ts_ns is non-zero.
    [[nodiscard]] ::fixy::Borrowed<const TimelineSchedEvent, SchedSwitch> timeline_view() const noexcept;

    // The index counts events forever, so the most recently written
    // slot is `(write_idx - 1) & TIMELINE_MASK`.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    [[nodiscard]] ::fixy::MaxBounded<8, std::size_t> attached_programs() const noexcept;

    // Set CRUCIBLE_PERF_VERBOSE=1 in the environment to see why an
    // attach failed.
    [[nodiscard]] ::fixy::MaxBounded<8, std::size_t> attach_failures() const noexcept;

    SchedSwitch(const SchedSwitch&) = delete("SchedSwitch owns unique BPF object + mmap — copying would double-close");
    SchedSwitch&
    operator=(const SchedSwitch&) = delete("SchedSwitch owns unique BPF object + mmap — copying would double-close");
    SchedSwitch(SchedSwitch&&) noexcept;
    SchedSwitch& operator=(SchedSwitch&&) noexcept;
    ~SchedSwitch();

private:
    struct State;
    SchedSwitch() noexcept;

    std::unique_ptr<State> state_;
};

// The load attaches to the sched_switch tracepoint, maps the timeline
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

using sched_switch_required_row =
    ::foundation::effects::Row<::foundation::effects::Effect::Alloc, ::foundation::effects::Effect::IO,
                               ::foundation::effects::Effect::Block>;

template <class Ctx>
concept CtxFitsSchedSwitchMint = ::foundation::effects::IsExecCtx<Ctx>
                              && ::foundation::effects::Subrow<sched_switch_required_row, typename Ctx::row_type>;

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSchedSwitchMint<Ctx>
// §XXI carve-out: cx=alloc — the load path maps the timeline ring and
// heap-allocates State.  Compile-time evaluation would lie about the
// runtime cost.
[[nodiscard]] inline std::optional<SchedSwitch> mint_sched_switch(Ctx const&,
                                                                  ::fixy::InitLoadCtx const& init) noexcept {
    return SchedSwitch::load(init);
}

}  // namespace crucible::perf
