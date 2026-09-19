#pragma once

#include <crucible/algebra/lattices/SyscallFamilyLattice.h>
#include <crucible/effects/_Capabilities.h>
#include <crucible/effects/_EffectRow.h>
#include <crucible/effects/_ExecCtx.h>
#include <crucible/fixy/syscall/Per.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/_Refined.h>

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
// The struct is 32 bytes, which divides the 64-byte cache line
// evenly, so no slot in the events array straddles two lines.  A
// straddling reader can see a committed ts_ns in the second line
// while the fields in the first line are still stale, which breaks
// the completion-marker rule above.
struct TimelineLockEvent {
    uint64_t futex_addr;
    uint64_t wait_ns;
    uint32_t tid;
    uint32_t _pad;
    uint64_t ts_ns;  // kernel monotonic clock
};
static_assert(sizeof(TimelineLockEvent) == 32, "TimelineLockEvent must be 32 B (futex_addr 8 + wait_ns 8 + "
                                               "tid 4 + _pad 4 + ts_ns 8) to match the kernel-side event "
                                               "struct — wire contract with the BPF_F_MMAPABLE map.  32 "
                                               "divides 64 evenly, so events[N] never spans two cache lines.");

}  // namespace crucible::perf

#include <crucible/perf/SchedSwitch.h>  // for TimelineHeader, TIMELINE_CAPACITY

namespace crucible::perf {

class LockContention {
public:
    // Both fields count upward forever, so an ordered post-minus-pre
    // subtraction never underflows.  The saturation below guards
    // against a caller that swapped the two snapshots.  A
    // timeline_index delta larger than the ring capacity means the
    // window overwrote its own oldest events.
    struct Snapshot {
        uint64_t wait_count = 0;
        uint64_t timeline_index = 0;

        [[nodiscard]] Snapshot operator-(const Snapshot& older) const noexcept {
            Snapshot r;
            if (__builtin_sub_overflow(wait_count, older.wait_count, &r.wait_count)) [[unlikely]] {
                r.wait_count = 0;
            }
            if (__builtin_sub_overflow(timeline_index, older.timeline_index, &r.timeline_index)) [[unlikely]] {
                r.timeline_index = 0;
            }
            return r;
        }
    };

    [[nodiscard]] Snapshot snapshot() const noexcept;

    // Both the futex-enter and the futex-exit tracepoint must attach.
    // A half attach records entry timestamps that no exit ever
    // consumes, so the load fails instead.
    //
    // Returns nullopt when the kernel refuses: no CAP_BPF or
    // CAP_PERFMON, a kernel without the syscall tracepoints, or a
    // verifier rejection.  A diagnostic line goes to stderr unless
    // CRUCIBLE_PERF_QUIET=1 is set in the environment, and
    // CRUCIBLE_PERF_VERBOSE=1 forwards the libbpf INFO and WARN
    // messages as well.
    [[nodiscard]] static std::optional<LockContention> load(::crucible::effects::Init) noexcept;

    // One count is one return from a blocking futex wait, so an
    // uncontended lock that never leaves userspace is invisible here.
    // Reading the count costs a map-lookup syscall: it lives in a
    // one-element array map, and the kernel-side program does not
    // declare that map mmap-able.
    [[nodiscard]] uint64_t wait_count() const noexcept;

    // The element type is const rather than const volatile because
    // libstdc++ cannot instantiate std::span over a volatile
    // non-scalar element.  The kernel writes this memory while the
    // reader walks it, so read ts_ns through an acquire load of its
    // own and trust the rest of the slot only when ts_ns is non-zero.
    [[nodiscard]] safety::Borrowed<const TimelineLockEvent, LockContention> timeline_view() const noexcept;

    // The index counts events forever, so the most recently written
    // slot is `(write_idx - 1) & TIMELINE_MASK`.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t> attached_programs() const noexcept;

    // Set CRUCIBLE_PERF_VERBOSE=1 in the environment to see which
    // tracepoint was unavailable.
    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t> attach_failures() const noexcept;

    LockContention(const LockContention&) =
        delete("LockContention owns unique BPF object + mmap — copying would double-close");
    LockContention& operator=(const LockContention&) =
        delete("LockContention owns unique BPF object + mmap — copying would double-close");
    LockContention(LockContention&&) noexcept;
    LockContention& operator=(LockContention&&) noexcept;
    ~LockContention();

private:
    struct State;
    LockContention() noexcept;

    std::unique_ptr<State> state_;
};

// Loading the program attaches to the futex tracepoints and maps the
// timeline ring.  Those are startup-only operations, so only a
// context carrying the Init capability may reach this surface.
template <class Ctx>
concept CtxFitsLockContentionMint = ::crucible::effects::IsExecCtx<Ctx>
                                 && ::crucible::effects::CtxOwnsCapability<Ctx, ::crucible::effects::Effect::Init>;

// These grants classify the privileged syscalls the load path issues.
// They do not tighten the effect row.  Init is a startup pass-through
// capability that admits blocking work without Block in the row.
using mint_lock_contention_syscall_grants =
    std::tuple<::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::bpf>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::perf_event_open>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::mmap>>;

namespace detail::v179_lock_contention_grant_check {
namespace fsc = ::crucible::fixy::grant::syscall;
namespace fll = ::crucible::algebra::lattices;
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::bpf>> == fll::SyscallFamily::Privilege);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::perf_event_open>>
              == fll::SyscallFamily::Privilege);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::mmap>>
              == fll::SyscallFamily::MemoryMapping);
static_assert(std::tuple_size_v<mint_lock_contention_syscall_grants> == 3,
              "mint_lock_contention_syscall_grants must list exactly 3 syscalls.");
}  // namespace detail::v179_lock_contention_grant_check

template <::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsLockContentionMint<Ctx>
// §XXI carve-out: cx=alloc — the load path maps the timeline ring and
// heap-allocates State.  Compile-time evaluation would lie about the
// runtime cost.
[[nodiscard]] inline std::optional<LockContention> mint_lock_contention(Ctx const&,
                                                                        ::crucible::effects::Init init) noexcept {
    return LockContention::load(init);
}

static_assert(CtxFitsLockContentionMint<::crucible::effects::ColdInitCtx>);
static_assert(!CtxFitsLockContentionMint<::crucible::effects::BgDrainCtx>);
static_assert(!CtxFitsLockContentionMint<::crucible::effects::HotFgCtx>);

}  // namespace crucible::perf
