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

// The trailing pad brings the struct to 32 bytes, which divides the
// 64-byte cache line evenly, so no slot in the ring straddles two
// lines.  A straddling reader can see a committed ts_ns in the second
// line while the fields in the first line are still stale, which
// breaks the rule that ts_ns marks a complete slot.
struct PmuSampleEvent {
    uint64_t ip;
    uint32_t tid;
    uint8_t event_type;
    uint8_t _pad[3];
    uint64_t ts_ns;  // kernel monotonic clock
    uint64_t _pad8;
};

struct PmuSampleHeader {
    uint64_t write_idx;
    uint64_t _pad[7];
};

// This value mirrors the ring size the kernel-side program was built
// with.  Changing one side alone breaks the shared layout.
constexpr uint32_t PMU_SAMPLE_CAPACITY = 32768;
[[maybe_unused]] constexpr uint32_t PMU_SAMPLE_MASK = PMU_SAMPLE_CAPACITY - 1;

class PmuSample {
public:
    // One ring slot is one sample, so the write index is also the
    // sample count and no separate scalar accessor exists.
    struct Snapshot {
        uint64_t samples = 0;

        [[nodiscard]] Snapshot operator-(const Snapshot& older) const noexcept {
            Snapshot r;
            if (__builtin_sub_overflow(samples, older.samples, &r.samples)) [[unlikely]] {
                r.samples = 0;
            }
            return r;
        }
    };

    [[nodiscard]] Snapshot snapshot() const noexcept;

    // An event type the machine does not support is skipped, so a
    // partial attach still yields a usable facade.  Returns nullopt
    // only when perf_event_open refuses every event type, when
    // CAP_PERFMON is missing on a host with perf_event_paranoid above
    // 2, or when the verifier rejects the program.
    //
    // Three environment variables override the per-event sample
    // period.  They are read once during the load, so setting them
    // afterwards has no effect.
    //
    //   CRUCIBLE_PERF_PMU_PERIOD_HW    cache, branch and TLB, default 10000
    //   CRUCIBLE_PERF_PMU_PERIOD_IBS   the two AMD events, default 100000
    //   CRUCIBLE_PERF_PMU_PERIOD_SW    the software events, default 1
    //
    // A smaller period samples more densely.  No floor is enforced,
    // because a diagnostic run may legitimately want a very dense
    // sample, but a hardware period below 1000 can drive a sustained
    // NMI flood across every CPU on the machine.
    //
    // The load takes the startup load context.  Its row carries Block,
    // because the load waits in the kernel while the verifier examines
    // the program.
    [[nodiscard]] static std::optional<PmuSample> load(::fixy::InitLoadCtx const&) noexcept;

    // The kernel samples the task whose TID equals the process TGID,
    // so only the main thread of this process appears here.
    //
    // The element type is const rather than const volatile because
    // libstdc++ cannot instantiate std::span over a volatile
    // non-scalar element.  The kernel writes this memory while the
    // reader walks it, so read ts_ns through an acquire load of its
    // own and trust the rest of the slot only when ts_ns is non-zero.
    [[nodiscard]] ::fixy::Borrowed<const PmuSampleEvent, PmuSample> timeline_view() const noexcept;

    // Slot order, not ts_ns, is the order in which samples were
    // recorded.  Cores overflow in parallel, so two samples can carry
    // the same ts_ns while landing at different indices.  Walk a
    // window in index order and use ts_ns only as an absolute time.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

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
    struct State;
    PmuSample() noexcept;
    std::unique_ptr<State> state_;
};

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
// §XXI carve-out: cx=alloc — the load path opens per-CPU perf event
// descriptors, maps the sample ring, and heap-allocates State.
// Compile-time evaluation would lie about the runtime cost.
[[nodiscard]] inline std::optional<PmuSample> mint_pmu_sample(Ctx const&, ::fixy::InitLoadCtx const& init) noexcept {
    return PmuSample::load(init);
}

}  // namespace crucible::perf
