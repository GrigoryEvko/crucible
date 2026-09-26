#pragma once

#include <crucible/perf/SchedSwitch.h>

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

class SchedTpBtf {
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

    // The BTF-typed raw tracepoint needs a kernel built with
    // CONFIG_DEBUG_INFO_BTF=y, which also puts the type information
    // at /sys/kernel/btf/vmlinux.  Returns nullopt on a kernel that
    // carries no such type information, and when CAP_BPF or
    // CAP_PERFMON is missing or the verifier rejects the program.
    //
    // The load takes the startup load context.  Its row carries Block,
    // because the load waits in the kernel while the verifier examines
    // the program.
    [[nodiscard]] static std::optional<SchedTpBtf> load(::fixy::InitLoadCtx const&) noexcept;

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
    [[nodiscard]] ::fixy::Borrowed<const TimelineSchedEvent, SchedTpBtf> timeline_view() const noexcept;

    // The index counts events forever, so the most recently written
    // slot is `(write_idx - 1) & TIMELINE_MASK`.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    [[nodiscard]] ::fixy::Refined<::fixy::bounded_above<8>, std::size_t> attached_programs() const noexcept;

    // A non-zero count means this kernel carries no BTF type
    // information.  Set CRUCIBLE_PERF_VERBOSE=1 in the environment
    // for the detail.
    [[nodiscard]] ::fixy::Refined<::fixy::bounded_above<8>, std::size_t> attach_failures() const noexcept;

    SchedTpBtf(const SchedTpBtf&) = delete("SchedTpBtf owns unique BPF object + mmap — copying would double-close");
    SchedTpBtf&
    operator=(const SchedTpBtf&) = delete("SchedTpBtf owns unique BPF object + mmap — copying would double-close");
    SchedTpBtf(SchedTpBtf&&) noexcept;
    SchedTpBtf& operator=(SchedTpBtf&&) noexcept;
    ~SchedTpBtf();

private:
    struct State;
    SchedTpBtf() noexcept;

    std::unique_ptr<State> state_;
};

// The load attaches to the sched_switch raw tracepoint, maps the
// timeline ring, and calls bpf(BPF_PROG_LOAD).  That last call enters
// the kernel and waits while the verifier walks the program.  The wait
// is why the row carries Block.  IO covers the bpf, perf_event_open and
// mmap traffic.  Alloc covers the state the load path takes from the
// heap.
//
// The startup load context and the background load context each claim
// Block on top of Alloc and IO, so each passes this gate.  The cold init
// context and the compile context stop at IO, and the gate refuses
// both.  The second argument is the startup load context that the load
// takes.

using sched_tp_btf_required_row =
    ::foundation::effects::Row<::foundation::effects::Effect::Alloc, ::foundation::effects::Effect::IO,
                               ::foundation::effects::Effect::Block>;

template <class Ctx>
concept CtxFitsSchedTpBtfMint = ::foundation::effects::IsExecCtx<Ctx>
                             && ::foundation::effects::Subrow<sched_tp_btf_required_row, typename Ctx::row_type>;

template <::foundation::effects::IsExecCtx Ctx>
    requires CtxFitsSchedTpBtfMint<Ctx>
// §XXI carve-out: cx=alloc — the load path maps the timeline ring and
// heap-allocates State.  Compile-time evaluation would lie about the
// runtime cost.
[[nodiscard]] inline std::optional<SchedTpBtf> mint_sched_tp_btf(Ctx const&, ::fixy::InitLoadCtx const& init) noexcept {
    return SchedTpBtf::load(init);
}

// Block is the atom that decides this gate.  ColdInitCtx and
// BgCompileCtx both carry Alloc and IO, and the gate rejects both for
// the same missing atom.  The gate reads the wait, not the capability
// source.
static_assert(!CtxFitsSchedTpBtfMint<::fixy::ColdInitCtx>);
static_assert(!CtxFitsSchedTpBtfMint<::fixy::BgCompileCtx>);
static_assert(!CtxFitsSchedTpBtfMint<::fixy::BgDrainCtx>);
static_assert(!CtxFitsSchedTpBtfMint<::fixy::HotFgCtx>);
static_assert(CtxFitsSchedTpBtfMint<::fixy::InitLoadCtx>);
static_assert(CtxFitsSchedTpBtfMint<::fixy::BgLoadCtx>);
static_assert(CtxFitsSchedTpBtfMint<::fixy::TestRunnerCtx>);

// The two assertions below hold for a capability source rather than for
// one named context, so a new alias on either side cannot evade them.
static_assert(::foundation::effects::Subrow<sched_tp_btf_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Init>>,
              "The initialization capability must permit every atom this gate demands, or no startup "
              "context could reach this mint.");
static_assert(::foundation::effects::Subrow<sched_tp_btf_required_row,
                                            ::foundation::effects::cap_permitted_row_t<::foundation::effects::Bg>>,
              "The background capability must permit every atom this gate demands, or no production "
              "context could reach this mint.");

}  // namespace crucible::perf
