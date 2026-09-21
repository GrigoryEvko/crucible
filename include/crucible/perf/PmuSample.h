#pragma once

#include <crucible/algebra/lattices/SyscallFamilyLattice.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/fixy/syscall/Per.h>
#include <crucible/safety/_Borrowed.h>
#include <crucible/safety/_Refined.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>

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
static_assert(sizeof(PmuSampleEvent) == 32, "PmuSampleEvent must be 32 B = ip(8) + tid(4) + event_type(1) + "
                                            "_pad[3] + ts_ns(8) + _pad8(8); the trailing pad makes 32 divide "
                                            "64 evenly so each slot is cache-line-coresident");

struct PmuSampleHeader {
    uint64_t write_idx;
    uint64_t _pad[7];
};
static_assert(sizeof(PmuSampleHeader) == 64, "PmuSampleHeader must be exactly one cache line so the events "
                                             "array starts at offset 64");

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
    [[nodiscard]] static std::optional<PmuSample> load(::crucible::effects::Init) noexcept;

    // The kernel samples the task whose TID equals the process TGID,
    // so only the main thread of this process appears here.
    //
    // The element type is const rather than const volatile because
    // libstdc++ cannot instantiate std::span over a volatile
    // non-scalar element.  The kernel writes this memory while the
    // reader walks it, so read ts_ns through an acquire load of its
    // own and trust the rest of the slot only when ts_ns is non-zero.
    [[nodiscard]] safety::Borrowed<const PmuSampleEvent, PmuSample> timeline_view() const noexcept;

    // Slot order, not ts_ns, is the order in which samples were
    // recorded.  Cores overflow in parallel, so two samples can carry
    // the same ts_ns while landing at different indices.  Walk a
    // window in index order and use ts_ns only as an absolute time.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    // The count is coarse and does not say which event types
    // attached.  Set CRUCIBLE_PERF_VERBOSE=1 in the environment to
    // get that per-type detail on stderr at load time.
    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t> attached_programs() const noexcept;

    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t> attach_failures() const noexcept;

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
// An Init-capability context cannot reach this surface.  The permitted
// row of that capability is Row<Init, Alloc, IO> and carries no Block,
// so a startup context widens no further than IO and the gate is
// unsatisfiable there.  A background context permits all three atoms
// and widens to them.

using pmu_sample_required_row =
    ::crucible::effects::Row<::crucible::effects::Effect::Alloc, ::crucible::effects::Effect::IO,
                             ::crucible::effects::Effect::Block>;

template <class Ctx>
concept CtxFitsPmuSampleMint = ::crucible::effects::IsExecCtx<Ctx>
                            && ::crucible::effects::Subrow<pmu_sample_required_row, typename Ctx::row_type>;

// These grants classify the privileged syscalls the load path issues.
// They do not tighten the effect row.  The row gate above does that.
using mint_pmu_sample_syscall_grants =
    std::tuple<::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::bpf>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::perf_event_open>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::mmap>>;

namespace detail::v179_pmu_sample_grant_check {
namespace fsc = ::crucible::fixy::grant::syscall;
namespace fll = ::crucible::algebra::lattices;
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::bpf>> == fll::SyscallFamily::Privilege);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::perf_event_open>>
              == fll::SyscallFamily::Privilege);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::mmap>>
              == fll::SyscallFamily::MemoryMapping);
static_assert(std::tuple_size_v<mint_pmu_sample_syscall_grants> == 3,
              "mint_pmu_sample_syscall_grants must list exactly 3 syscalls.");
}  // namespace detail::v179_pmu_sample_grant_check

template <::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsPmuSampleMint<Ctx>
// §XXI carve-out: cx=alloc — the load path opens per-CPU perf event
// descriptors, maps the sample ring, and heap-allocates State.
// Compile-time evaluation would lie about the runtime cost.
[[nodiscard]] inline std::optional<PmuSample> mint_pmu_sample(Ctx const&, ::crucible::effects::Init init) noexcept {
    return PmuSample::load(init);
}

// Block is the atom that decides this gate.  ColdInitCtx and
// BgCompileCtx both carry Alloc and IO, and the gate rejects both for
// the same missing atom.  The gate reads the wait, not the capability
// source.
static_assert(!CtxFitsPmuSampleMint<::crucible::effects::ColdInitCtx>);
static_assert(!CtxFitsPmuSampleMint<::crucible::effects::BgCompileCtx>);
static_assert(!CtxFitsPmuSampleMint<::crucible::effects::BgDrainCtx>);
static_assert(!CtxFitsPmuSampleMint<::crucible::effects::HotFgCtx>);
static_assert(CtxFitsPmuSampleMint<::crucible::effects::TestRunnerCtx>);

// The two assertions below hold for a capability source rather than for
// one named context, so a new alias on either side cannot evade them.
static_assert(!::crucible::effects::Subrow<pmu_sample_required_row,
                                          ::crucible::effects::cap_permitted_row_t<::crucible::effects::Init>>,
              "The initialization capability must never permit every atom this gate demands.  It omits "
              "Block because a startup scope must not wait, and the BPF program load waits on the "
              "kernel verifier.  No widening rescues an initialization context.");
static_assert(::crucible::effects::Subrow<pmu_sample_required_row,
                                          ::crucible::effects::cap_permitted_row_t<::crucible::effects::Bg>>,
              "The background capability must permit every atom this gate demands, or no production "
              "context could reach this mint.");

}  // namespace crucible::perf
