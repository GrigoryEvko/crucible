#pragma once

// crucible::perf::SchedTpBtf — BTF-typed sched_switch off-CPU facade.
//
// GAPS-004f sibling of SchedSwitch.  Wire-equivalent (writes the same
// `TimelineSchedEvent` struct into a `BPF_F_MMAPABLE` ring buffer);
// the only difference is the BPF program uses
// SEC("tp_btf/sched_switch") instead of
// SEC("tracepoint/sched/sched_switch").
//
// ─── Why a parallel facade and not a flag on SchedSwitch ───────────
//
// `tp_btf` and `tracepoint` are both "raw tracepoint" classes from
// libbpf's perspective, but they cost differently and have different
// kernel-availability gates:
//
//   • Legacy `tracepoint/sched/sched_switch` — present on every kernel
//     with CONFIG_FTRACE, parses a per-tracepoint format string at
//     dispatch time, ~150-250 ns per event.  Available since ~3.5.
//
//   • BTF-typed `tp_btf/sched_switch` — direct CO-RE access to
//     `struct task_struct *prev/next` via BPF_CORE_READ.  ~30% lower
//     overhead (~100-180 ns) because the format string parse is
//     skipped.  Requires CONFIG_DEBUG_INFO_BTF=y AND kernel ≥ 5.5.
//
// Production callers that know they're on a recent kernel and care
// about the per-event budget pick SchedTpBtf.  Callers that need to
// run on older kernels (CentOS 7, Ubuntu 18.04 LTS) pick SchedSwitch.
// `Senses::load_*` masks let the caller pick at runtime; this facade
// is the BTF half of that pair.
//
// ─── Promote-First architecture (CLAUDE.md) ────────────────────────
//
// This is the SIXTH per-program facade (after SenseHub, SchedSwitch,
// PmuSample, LockContention, SyscallLatency).  The duplication
// between SchedTpBtf and SchedSwitch is INTENTIONAL — same
// hand-coded loader as the others, same field shapes, same Tagged
// source tags.  GAPS-004x (BpfLoader extraction) will surface the
// commonality after we have ≥6 production loaders to generalize from.
//
// ─── Production usage ──────────────────────────────────────────────
//
//     if (auto h = crucible::perf::SchedTpBtf::load(crucible::effects::Init{})) {
//         const uint64_t cs_pre = h->context_switches();
//         // ... run workload ...
//         const uint64_t cs_delta = h->context_switches() - cs_pre;
//
//         const auto timeline = h->timeline_view();
//         // Same TimelineSchedEvent shape as SchedSwitch — readers
//         // walk it identically.
//     }
//
// Returns nullopt when:
//   • CAP_BPF / CAP_PERFMON missing.
//   • CONFIG_DEBUG_INFO_BTF=n (kernel built without BTF debug info).
//   • Kernel < 5.5 (tp_btf raw tracepoints unavailable).
//   • bpf_object__load() rejects the program (verifier failure).
//
// The fallback story for older kernels: SchedSwitch.h works on every
// kernel ≥ 3.5; SchedTpBtf.h works on every kernel ≥ 5.5.  Senses
// aggregator can attempt both and report whichever attached via
// coverage().

// SchedSwitch.h provides TimelineSchedEvent (32-byte struct), plus
// TimelineHeader / TIMELINE_CAPACITY / TIMELINE_MASK constants.  All
// wire-equivalent for the BTF variant; we just re-use them.
#include <crucible/perf/SchedSwitch.h>

#include <crucible/effects/Capabilities.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/Refined.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace crucible::perf {

class SchedTpBtf {
 public:
    // ─── Snapshot — consumer-shaped delta semantics ──────────────────
    //
    // Same shape as SchedSwitch::Snapshot — BTF-typed sched_switch
    // produces the same TimelineSchedEvent layout, so consumers that
    // accept either facade can use the same Snapshot diff idiom.
    struct Snapshot {
        uint64_t ctx_switches   = 0;
        uint64_t timeline_index = 0;

        [[nodiscard]] Snapshot operator-(const Snapshot& older) const noexcept {
            Snapshot r;
            if (__builtin_sub_overflow(ctx_switches, older.ctx_switches,
                                        &r.ctx_switches)) [[unlikely]] {
                r.ctx_switches = 0;
            }
            if (__builtin_sub_overflow(timeline_index, older.timeline_index,
                                        &r.timeline_index)) [[unlikely]] {
                r.timeline_index = 0;
            }
            return r;
        }
    };

    [[nodiscard]] Snapshot snapshot() const noexcept;

    // Load the embedded BPF program (sched_tp_btf.bpf.c), set
    // target_tgid to getpid(), populate our_tids with the main TID,
    // attach the tp_btf/sched_switch program, mmap the
    // sched_timeline ring.  Returns std::nullopt on:
    //   • missing CAP_BPF / CAP_PERFMON
    //   • CONFIG_DEBUG_INFO_BTF=n (no /sys/kernel/btf/vmlinux)
    //   • kernel < 5.5 (no tp_btf support)
    //   • verifier rejection (corrupt embedded bytecode)
    //
    // Same `effects::Init` capability gate as the other GAPS-004
    // facades — hot-path code holds no Init token, so this can never
    // be called from a hot frame.
    [[nodiscard]] static std::optional<SchedTpBtf>
        load(::crucible::effects::Init) noexcept;

    // Total context switches recorded for our process since load().
    // ~1 µs (one bpf_map_lookup_elem against the cs_count ARRAY map).
    // 0 on moved-from / un-loaded.
    [[nodiscard]] uint64_t context_switches() const noexcept;

    // Borrowed view over the sched_timeline ring buffer.  Element
    // type is the SAME `TimelineSchedEvent` struct as SchedSwitch —
    // the BPF program writes the identical 32-byte layout, so a
    // reader that handles SchedSwitch's timeline handles this one
    // unchanged.  See SchedSwitch.h for the ts_ns-LAST completion
    // discipline and ACQUIRE-load reader idiom.  Empty span on
    // moved-from / un-loaded.
    [[nodiscard]] safety::Borrowed<const TimelineSchedEvent, SchedTpBtf>
        timeline_view() const noexcept;

    // Current write_idx of the sched_timeline ring buffer.  Reader
    // uses this to find the latest valid slot via
    // `(write_idx - 1) & TIMELINE_MASK`.  ~1 ns volatile load.  0 on
    // moved-from / un-loaded.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    // Programs attached.  sched_tp_btf.bpf.c contains exactly ONE
    // SEC("tp_btf/sched_switch") program; cap of 8 matches the
    // inplace_vector<...,8> shape used by every other facade for
    // uniformity.
    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t>
        attached_programs() const noexcept;

    // bpf_program__attach failures.  Same bound as attached_programs.
    // Non-zero means BTF is unavailable on this kernel — set
    // CRUCIBLE_PERF_VERBOSE=1 to see why.
    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t>
        attach_failures() const noexcept;

    SchedTpBtf(const SchedTpBtf&) =
        delete("SchedTpBtf owns unique BPF object + mmap — copying would double-close");
    SchedTpBtf& operator=(const SchedTpBtf&) =
        delete("SchedTpBtf owns unique BPF object + mmap — copying would double-close");
    SchedTpBtf(SchedTpBtf&&) noexcept;
    SchedTpBtf& operator=(SchedTpBtf&&) noexcept;
    ~SchedTpBtf();

 private:
    struct State;
    SchedTpBtf() noexcept;

    std::unique_ptr<State> state_;
};

}  // namespace crucible::perf
