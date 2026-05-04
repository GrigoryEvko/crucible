#pragma once

// crucible::perf::SyscallTpBtf — BTF-typed sys_enter/sys_exit facade.
//
// GAPS-004f sibling of SyscallLatency.  Wire-equivalent (writes the
// same `TimelineSyscallEvent` struct into a `BPF_F_MMAPABLE` ring
// buffer); the only difference is the BPF program uses
// SEC("tp_btf/sys_enter") + SEC("tp_btf/sys_exit") instead of
// SEC("tracepoint/raw_syscalls/sys_{enter,exit}").
//
// ─── Why a parallel facade and not a flag on SyscallLatency ────────
//
// `tp_btf` and `tracepoint` are both raw tracepoint classes from
// libbpf's perspective, but they cost differently and have different
// kernel-availability gates:
//
//   • Legacy `tracepoint/raw_syscalls/sys_{enter,exit}` — present
//     on every kernel with CONFIG_FTRACE_SYSCALLS, parses a per-
//     tracepoint format string at dispatch time, ~150-250 ns per
//     event.  Available since ~3.7.
//
//   • BTF-typed `tp_btf/sys_{enter,exit}` — direct CO-RE access to
//     `struct pt_regs *regs` via BPF_CORE_READ.  ~30% lower
//     overhead (~100-180 ns) because the format string parse is
//     skipped.  Requires CONFIG_DEBUG_INFO_BTF=y AND kernel ≥ 5.5.
//
// On a syscall-heavy workload (1M syscalls/sec), the per-event
// difference compounds:
//   • Legacy: 1M × 200 ns = 200 ms/sec ≈ 20% of one core
//   • BTF:    1M × 130 ns = 130 ms/sec ≈ 13% of one core
// — a 7-percentage-point CPU saving for the same observability.
//
// ─── Promote-First architecture (CLAUDE.md) ────────────────────────
//
// Seventh per-program facade after SenseHub, SchedSwitch, PmuSample,
// LockContention, SyscallLatency, SchedTpBtf.  Same hand-coded
// loader as the others, same Tagged source tags, same
// inplace_vector<...,8> link cap.  GAPS-004x will surface the
// duplication as a shared BpfLoader after we have ≥7 production
// loaders to generalize from.
//
// ─── Production usage ──────────────────────────────────────────────
//
//     if (auto h = crucible::perf::SyscallTpBtf::load(crucible::effects::Init{})) {
//         const uint64_t syscalls_pre = h->total_syscalls();
//         // ... run workload ...
//         const uint64_t syscalls_delta =
//             h->total_syscalls() - syscalls_pre;
//
//         const auto timeline = h->timeline_view();
//         // Same TimelineSyscallEvent shape as SyscallLatency —
//         // readers walk it identically.
//     }
//
// Returns nullopt when:
//   • CAP_BPF / CAP_PERFMON missing.
//   • CONFIG_DEBUG_INFO_BTF=n.
//   • Kernel < 5.5.
//   • Either of the two tp_btf programs fails to attach (a half-
//     attach would leak syscall_start map entries until LRU eviction
//     kicks in — same all-or-nothing policy as SyscallLatency).
//
// Senses aggregator pairs SyscallTpBtf with SyscallLatency: callers
// that want syscall observability ask for both, the aggregator
// reports whichever attached.

// SyscallLatency.h provides TimelineSyscallEvent (32-byte struct);
// it transitively includes SchedSwitch.h for TimelineHeader /
// TIMELINE_CAPACITY / TIMELINE_MASK.  Wire-equivalent for the BTF
// variant — we just re-use them.
#include <crucible/perf/SyscallLatency.h>

#include <crucible/effects/Capabilities.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/Refined.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>

namespace crucible::perf {

class SyscallTpBtf {
 public:
    // Load the embedded BPF program (syscall_tp_btf.bpf.c), set
    // target_tgid to getpid(), attach the tp_btf/sys_enter +
    // tp_btf/sys_exit programs, mmap the syscall_timeline ring.
    // Returns std::nullopt on:
    //   • missing CAP_BPF / CAP_PERFMON
    //   • CONFIG_DEBUG_INFO_BTF=n
    //   • kernel < 5.5
    //   • fewer than 2 programs attach (half-attach forbidden)
    //
    // Same `effects::Init` capability gate as every other facade in
    // the GAPS-004 series.
    [[nodiscard]] static std::optional<SyscallTpBtf>
        load(::crucible::effects::Init) noexcept;

    // Total syscalls recorded for our process since load().  ~1 µs
    // (one bpf_map_lookup_elem against the total_syscalls ARRAY map).
    // 0 on moved-from / un-loaded.
    //
    // Each event represents ONE syscall return — including the
    // bpf_map_lookup_elem this method itself issues, so calling
    // total_syscalls() in a tight loop measures the loop, not the
    // workload.  Use pre/post deltas across a workload region.
    [[nodiscard]] uint64_t total_syscalls() const noexcept;

    // Borrowed view over the syscall_timeline ring buffer.  Element
    // type is the SAME `TimelineSyscallEvent` struct as
    // SyscallLatency — the BPF program writes the identical 32-byte
    // layout, so a reader that handles SyscallLatency's timeline
    // handles this one unchanged.  See SyscallLatency.h for the
    // ts_ns-LAST completion discipline and ACQUIRE-load reader
    // idiom.  Empty span on moved-from / un-loaded.
    [[nodiscard]] safety::Borrowed<const TimelineSyscallEvent, SyscallTpBtf>
        timeline_view() const noexcept;

    // Current write_idx of the syscall_timeline ring buffer.  Reader
    // uses this to find the latest valid slot via
    // `(write_idx - 1) & TIMELINE_MASK`.  ~1 ns volatile load.  0 on
    // moved-from / un-loaded.
    [[nodiscard]] uint64_t timeline_write_index() const noexcept;

    // Programs attached.  syscall_tp_btf.bpf.c contains exactly TWO
    // tp_btf programs (sys_enter + sys_exit); both must attach for
    // load() to succeed.  Cap of 8 matches the
    // inplace_vector<...,8> shape used by every other facade.
    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t>
        attached_programs() const noexcept;

    // bpf_program__attach failures.  Same bound as attached_programs.
    // Non-zero means BTF is unavailable on this kernel — set
    // CRUCIBLE_PERF_VERBOSE=1 to see why.
    [[nodiscard]] safety::Refined<safety::bounded_above<8>, std::size_t>
        attach_failures() const noexcept;

    SyscallTpBtf(const SyscallTpBtf&) =
        delete("SyscallTpBtf owns unique BPF object + mmap — copying would double-close");
    SyscallTpBtf& operator=(const SyscallTpBtf&) =
        delete("SyscallTpBtf owns unique BPF object + mmap — copying would double-close");
    SyscallTpBtf(SyscallTpBtf&&) noexcept;
    SyscallTpBtf& operator=(SyscallTpBtf&&) noexcept;
    ~SyscallTpBtf();

 private:
    struct State;
    SyscallTpBtf() noexcept;

    std::unique_ptr<State> state_;
};

}  // namespace crucible::perf
