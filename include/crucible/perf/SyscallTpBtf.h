#pragma once

#include <crucible/perf/SyscallLatency.h>  // for TimelineSyscallEvent, TimelineHeader, TIMELINE_CAPACITY

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

class SyscallTpBtf {
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

    // Both the syscall-enter and the syscall-exit program must
    // attach.  A half attach records entry timestamps that no exit
    // ever consumes, so the load fails instead.
    //
    // The BTF-typed raw tracepoints need a kernel built with
    // CONFIG_DEBUG_INFO_BTF=y, which also puts the type information
    // at /sys/kernel/btf/vmlinux.  Returns nullopt on a kernel that
    // carries no such type information, and when CAP_BPF or
    // CAP_PERFMON is missing or the verifier rejects the program.
    [[nodiscard]] static std::optional<SyscallTpBtf> load(::crucible::effects::Init) noexcept;

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
    [[nodiscard]] safety::Borrowed<const TimelineSyscallEvent, SyscallTpBtf> timeline_view() const noexcept;

    // The index counts events forever, so the most recently written
    // slot is `(write_idx - 1) & TIMELINE_MASK`.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t> attached_programs() const noexcept;

    // A non-zero count means this kernel carries no BTF type
    // information.  Set CRUCIBLE_PERF_VERBOSE=1 in the environment
    // for the detail.
    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t> attach_failures() const noexcept;

    SyscallTpBtf(const SyscallTpBtf&) =
        delete("SyscallTpBtf owns unique BPF object + mmap — copying would double-close");
    SyscallTpBtf&
    operator=(const SyscallTpBtf&) = delete("SyscallTpBtf owns unique BPF object + mmap — copying would double-close");
    SyscallTpBtf(SyscallTpBtf&&) noexcept;
    SyscallTpBtf& operator=(SyscallTpBtf&&) noexcept;
    ~SyscallTpBtf();

private:
    struct State;
    SyscallTpBtf() noexcept;

    std::unique_ptr<State> state_;
};

// The load attaches to the raw syscall tracepoints, maps the timeline
// ring, and calls bpf(BPF_PROG_LOAD).  That last call enters the kernel
// and waits while the verifier walks the program.  The wait is why the
// row carries Block.  IO covers the bpf, perf_event_open and mmap
// traffic.  Alloc covers the state the load path takes from the heap.
//
// An Init-capability context cannot reach this surface.  The permitted
// row of that capability is Row<Init, Alloc, IO> and carries no Block,
// so a startup context widens no further than IO and the gate is
// unsatisfiable there.  A background context permits all three atoms
// and widens to them.

using syscall_tp_btf_required_row =
    ::crucible::effects::Row<::crucible::effects::Effect::Alloc, ::crucible::effects::Effect::IO,
                             ::crucible::effects::Effect::Block>;

template <class Ctx>
concept CtxFitsSyscallTpBtfMint = ::crucible::effects::IsExecCtx<Ctx>
                               && ::crucible::effects::Subrow<syscall_tp_btf_required_row, typename Ctx::row_type>;

// These grants classify the privileged syscalls the load path issues.
// They do not tighten the effect row.  The row gate above does that.
using mint_syscall_tp_btf_syscall_grants =
    std::tuple<::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::bpf>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::perf_event_open>,
               ::crucible::fixy::grant::syscall::per<::crucible::fixy::grant::syscall::SyscallId::mmap>>;

namespace detail::v179_syscall_tp_btf_grant_check {
namespace fsc = ::crucible::fixy::grant::syscall;
namespace fll = ::crucible::algebra::lattices;
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::bpf>> == fll::SyscallFamily::Privilege);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::perf_event_open>>
              == fll::SyscallFamily::Privilege);
static_assert(::crucible::fixy::grant::family_tier_v<fsc::per<fsc::SyscallId::mmap>>
              == fll::SyscallFamily::MemoryMapping);
static_assert(std::tuple_size_v<mint_syscall_tp_btf_syscall_grants> == 3,
              "mint_syscall_tp_btf_syscall_grants must list exactly 3 syscalls.");
}  // namespace detail::v179_syscall_tp_btf_grant_check

template <::crucible::effects::IsExecCtx Ctx>
    requires CtxFitsSyscallTpBtfMint<Ctx>
// §XXI carve-out: cx=alloc — the load path maps the timeline ring and
// heap-allocates State.  Compile-time evaluation would lie about the
// runtime cost.
[[nodiscard]] inline std::optional<SyscallTpBtf> mint_syscall_tp_btf(Ctx const&,
                                                                     ::crucible::effects::Init init) noexcept {
    return SyscallTpBtf::load(init);
}

// Block is the atom that decides this gate.  ColdInitCtx and
// BgCompileCtx both carry Alloc and IO, and the gate rejects both for
// the same missing atom.  The gate reads the wait, not the capability
// source.
static_assert(!CtxFitsSyscallTpBtfMint<::crucible::effects::ColdInitCtx>);
static_assert(!CtxFitsSyscallTpBtfMint<::crucible::effects::BgCompileCtx>);
static_assert(!CtxFitsSyscallTpBtfMint<::crucible::effects::BgDrainCtx>);
static_assert(!CtxFitsSyscallTpBtfMint<::crucible::effects::HotFgCtx>);
static_assert(CtxFitsSyscallTpBtfMint<::crucible::effects::TestRunnerCtx>);

// The two assertions below hold for a capability source rather than for
// one named context, so a new alias on either side cannot evade them.
static_assert(!::crucible::effects::Subrow<syscall_tp_btf_required_row,
                                          ::crucible::effects::cap_permitted_row_t<::crucible::effects::Init>>,
              "The initialization capability must never permit every atom this gate demands.  It omits "
              "Block because a startup scope must not wait, and the BPF program load waits on the "
              "kernel verifier.  No widening rescues an initialization context.");
static_assert(::crucible::effects::Subrow<syscall_tp_btf_required_row,
                                          ::crucible::effects::cap_permitted_row_t<::crucible::effects::Bg>>,
              "The background capability must permit every atom this gate demands, or no production "
              "context could reach this mint.");

}  // namespace crucible::perf
