#pragma once

#include <crucible/algebra/lattices/SyscallFamilyLattice.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/fixy/syscall/Per.h>
#include <crucible/fixy/wrap/Refined.h>
#include <crucible/safety/Borrowed.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <tuple>

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
static_assert(sizeof(TimelineSchedEvent) == 32, "TimelineSchedEvent must be 32 B (with 8 B trailing pad) to "
                                                "match the kernel-side event struct — wire contract with the "
                                                "BPF_F_MMAPABLE map.  32 divides 64 evenly, so events[N] never "
                                                "spans two cache lines.");

struct TimelineHeader {
    uint64_t write_idx;
    uint64_t _pad[7];
};
static_assert(sizeof(TimelineHeader) == 64, "TimelineHeader must be exactly one cache line so the events "
                                            "array starts at offset 64 — pinned by BPF program layout");

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
    [[nodiscard]] static std::optional<SchedSwitch> load(::crucible::effects::Init) noexcept;

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
    [[nodiscard]] safety::Borrowed<const TimelineSchedEvent, SchedSwitch> timeline_view() const noexcept;

    // The index counts events forever, so the most recently written
    // slot is `(write_idx - 1) & TIMELINE_MASK`.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    [[nodiscard]] fixy::wrap::MaxBounded<8, std::size_t> attached_programs() const noexcept;

    // Set CRUCIBLE_PERF_VERBOSE=1 in the environment to see why an
    // attach failed.
    [[nodiscard]] fixy::wrap::MaxBounded<8, std::size_t> attach_failures() const noexcept;

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

// Loading the program attaches to the sched_switch tracepoint and
// maps the timeline ring.  Those are startup-only operations, so only
// a context carrying the Init capability may reach this surface.
template <class Ctx>
concept CtxFitsSchedSwitchMint = ::crucible::effects::IsExecCtx<Ctx>
                              && ::crucible::effects::CtxOwnsCapability<Ctx, ::crucible::effects::Effect::Init>;

// These grants classify the privileged syscalls the load path issues.
// They do not tighten the effect row.  Init is a startup pass-through
// capability that admits blocking work without Block in the row.
using mint_sched_switch_syscall_grants =
    std::tuple<::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::bpf>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::perf_event_open>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::mmap>>;

namespace detail::v179_sched_switch_grant_check {
namespace fsc = ::crucible::fixy::grant::syscall;
namespace fll = ::crucible::algebra::lattices;
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::bpf>> == fll::SyscallFamily::Privilege);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::perf_event_open>>
              == fll::SyscallFamily::Privilege);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::mmap>>
              == fll::SyscallFamily::MemoryMapping);
static_assert(std::tuple_size_v<mint_sched_switch_syscall_grants> == 3,
              "mint_sched_switch_syscall_grants must list exactly 3 syscalls.");
}  // namespace detail::v179_sched_switch_grant_check

template <::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsSchedSwitchMint<Ctx>
// §XXI carve-out: cx=alloc — the load path maps the timeline ring and
// heap-allocates State.  Compile-time evaluation would lie about the
// runtime cost.
[[nodiscard]] inline std::optional<SchedSwitch> mint_sched_switch(Ctx const&, ::crucible::effects::Init init) noexcept {
    return SchedSwitch::load(init);
}

static_assert(CtxFitsSchedSwitchMint<::crucible::effects::ColdInitCtx>);
static_assert(!CtxFitsSchedSwitchMint<::crucible::effects::BgDrainCtx>);
static_assert(!CtxFitsSchedSwitchMint<::crucible::effects::HotFgCtx>);

}  // namespace crucible::perf
