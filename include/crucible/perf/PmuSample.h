#pragma once

#include <fixy/Core.h>
#include <fixy/Ctx.h>
#include <fixy/Refined.h>
#include <fixy/os/OwnedMmap.h>
#include <foundation/core/Array.h>
#include <foundation/effects/Ctx.h>
#include <foundation/effects/Row.h>

#include <cstddef>
#include <cstdint>

namespace crucible::perf {

namespace detail {
struct FacadeDoor;
}  // namespace detail

// These numeric values are wire protocol with the kernel-side BPF
// program and must not be renumbered.  Values 0 and 1 are reserved
// for cycles and L1D misses, which fire far too often to route
// through BPF, so this facade never emits them.  A reader that
// switches on the discriminator treats 0 and 1 as unknown rather
// than unreachable.
enum class PmuEventType : uint8_t {
    LlcMiss = 2,
    BranchMiss = 3,
    DtlbMiss = 4,
    IbsOp = 5,  // AMD only, skipped elsewhere
    IbsFetch = 6,  // AMD only, skipped elsewhere
    MajorPageFault = 7,
    CpuMigration = 8,
    AlignmentFault = 9,
};

// commit is the commit word of a slot, and it holds the ticket of the write
// (the ring ticket of fixy/os/OwnedMmap.h).  The other fields of a slot are
// a record only for a reader that asks for that ticket.
//
// The struct is 32 bytes, which divides the 64-byte cache line evenly, so
// no slot in the ring straddles two lines.
struct PmuSampleEvent {
    uint64_t ip;
    uint32_t tid;
    uint8_t event_type;
    ::fixy::FixedArray<uint8_t, 3> _pad;
    uint64_t ts_ns;  // kernel monotonic clock
    uint64_t commit;  // the ticket of the write
};

// write_idx counts each claim of a slot, and dropped counts the claims whose
// write dropped (the ring ticket of fixy/os/OwnedMmap.h).
struct PmuSampleHeader {
    uint64_t write_idx;
    uint64_t dropped;
    ::fixy::FixedArray<uint64_t, 6> _pad;
};

// This value mirrors the ring size the kernel-side program was built
// with.  Changing one side alone breaks the shared layout.
constexpr uint32_t PMU_SAMPLE_CAPACITY = 32768;
[[maybe_unused]] constexpr uint32_t PMU_SAMPLE_MASK = PMU_SAMPLE_CAPACITY - 1;

class PmuSample {
public:
    // One claim of a slot is one sample, so the write index is also the
    // sample count and no separate scalar accessor exists.
    struct Snapshot {
        uint64_t samples = 0;

        [[nodiscard]] Snapshot operator-(const Snapshot& older) const noexcept {
            return Snapshot{.samples = ::fixy::saturating_sub(samples, older.samples)};
        }
    };

    [[nodiscard]] Snapshot snapshot() const noexcept;

    // The kernel samples the task whose TID equals the process TGID,
    // so only the main thread of this process appears here.
    //
    // The kernel writes this memory while the reader walks it, so read
    // the record of a ticket through its commit word, with
    // read_ticket<&PmuSampleEvent::commit>.  A record that the ring wrote
    // over, that is not whole yet, or whose write dropped gives no value.
    [[nodiscard]] ::fixy::mmap::SharedView<const PmuSampleEvent> timeline_view() const noexcept;

    // Slot order, not ts_ns, is the order in which samples were
    // recorded.  Cores overflow in parallel, so two samples can carry
    // the same ts_ns while landing at different indices.  Walk a
    // window in index order and use ts_ns only as an absolute time.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    // The count of the claims whose write dropped.  A count that grows
    // with each lap of the ring shows a slot that a program never left.
    [[nodiscard]] uint64_t timeline_dropped() const noexcept;

    // The count is coarse and does not say which event types
    // attached.  Set CRUCIBLE_PERF_VERBOSE=1 in the environment to
    // get that per-type detail on stderr at load time.
    [[nodiscard]] ::fixy::Refined<::fixy::bounded_above<8>, std::size_t> attached_programs() const noexcept;

    [[nodiscard]] ::fixy::Refined<::fixy::bounded_above<8>, std::size_t> attach_failures() const noexcept;

    PmuSample(const PmuSample&) = delete("PmuSample owns unique BPF object + perf_event FDs + mmap");
    PmuSample& operator=(const PmuSample&) = delete("PmuSample owns unique BPF object + perf_event FDs + mmap");
    PmuSample(PmuSample&&) noexcept;
    PmuSample& operator=(PmuSample&&) noexcept;
    ~PmuSample();

private:
    // The door builds the facade in each load.
    friend struct detail::FacadeDoor;
    struct State;
    explicit PmuSample(::fixy::Box<State> state) noexcept;

    // A moved-from facade holds no state.  Each constructor sets the member,
    // so it has no default initializer: one here would end a Box in each unit
    // that sees only the declaration of State.
    ::fixy::Option<::fixy::Box<State>> state_;
};

// An event type the machine does not support is skipped, so a partial
// attach still yields a usable facade.  Returns none only when
// perf_event_open refuses every event type, when CAP_PERFMON is missing on
// a host with perf_event_paranoid above 2, or when the verifier rejects
// the program.
//
// Three environment variables override the per-event sample period.  The
// environment door reads them one time at the start of the process
// (fixy/os/Environment.h), so a change after the start has no effect.
//
//   CRUCIBLE_PERF_PMU_PERIOD_HW    cache, branch and TLB, default 10000
//   CRUCIBLE_PERF_PMU_PERIOD_IBS   the two AMD events, default 100000
//   CRUCIBLE_PERF_PMU_PERIOD_SW    the software events, default 1
//
// A smaller period samples more densely.  No floor is enforced, because a
// diagnostic run may legitimately want a very dense sample, but a hardware
// period below 1000 can drive a sustained NMI flood across every CPU on
// the machine.
//
// The load takes the startup load context.  Its row carries Block,
// because the load waits in the kernel while the verifier examines the
// program.
[[nodiscard]] ::fixy::Option<PmuSample> load_pmu_sample(::fixy::InitLoadCtx const&) noexcept;

// The load opens per-CPU perf event descriptors, maps the sample ring,
// and calls bpf(BPF_PROG_LOAD).  That last call enters the kernel and
// waits while the verifier walks the program.  The wait is why the row
// carries Block.  IO covers the bpf, perf_event_open and mmap traffic.
// Alloc covers the state the load path takes from the heap.
//
// The startup load context and the background load context each claim
// Block on top of Alloc and IO, so each passes this gate.  The cold init
// context and the compile context stop at IO, and the gate refuses
// both.  The second argument is the startup load context that the load
// takes.

using pmu_sample_required_row =
    ::foundation::effects::Row<::foundation::effects::Effect::Alloc, ::foundation::effects::Effect::IO,
                               ::foundation::effects::Effect::Block>;

template <class Ctx>
concept CtxFitsPmuSampleMint = ::foundation::effects::IsExecCtx<Ctx>
                            && ::foundation::effects::Subrow<pmu_sample_required_row, typename Ctx::row_type>;

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsPmuSampleMint<Ctx>
[[nodiscard]] inline ::fixy::Option<PmuSample> mint_pmu_sample(Ctx const&, ::fixy::InitLoadCtx const& init) noexcept {
    return load_pmu_sample(init);
}

}  // namespace crucible::perf
