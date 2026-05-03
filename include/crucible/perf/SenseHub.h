#pragma once

// crucible::perf::SenseHub — eBPF-backed observability substrate.
//
// 96 counters spanning 11 cache lines, populated by the kernel-side
// BPF program at include/crucible/perf/bpf/sense_hub.bpf.c via 58
// tracepoints (sched_switch, sched_migrate_task, futex_wait, all the
// page-fault / mmap / brk / iowait / softirq / page-cache /
// readahead / TCP / UDP / signal / OOM tracepoints).  Counters live
// in a BPF_F_MMAPABLE array map; userspace reads are volatile mmap
// loads — one syscall-free volatile read of 12 cache lines, ~50 ns.
// Zero cost on the foreground path: tracepoint handlers run in
// kernel context on the event, not on our measurement path.
//
// ─── History ──────────────────────────────────────────────────────
//
// Promoted to include/crucible/perf/ on 2026-05-03 (GAPS-004a) from
// its original home under bench/bpf_senses.{h,cpp}.  The substrate
// is production-grade — full ERR_PTR-aware libbpf binding, autoload
// disable for unavailable tracepoints, partial-coverage diagnostics,
// PIMPL'd state, RAII destructor — and conceptually belongs to
// Augur (the L15 prediction/monitoring layer) rather than to bench.
// Bench remains a consumer via a one-line `namespace bench::bpf =
// crucible::perf;` alias; the canonical API is `crucible::perf::*`.
//
// ─── Two-tier observability design ─────────────────────────────────
//
// SenseHub answers "is something slow" with cheap, always-on global
// counters.  The 6 sibling per-key probes (sched_switch, pmu_sample,
// lock_contention, syscall_latency, sched_tp_btf, syscall_tp_btf)
// answer "where is it slow" — per-lock-address, per-IP, per-call-
// site, per-syscall attribution that's heavier and opt-in.
//
// Production usage:
//
//     if (auto h = crucible::perf::SenseHub::load()) {
//         auto pre  = h->read();
//         // ... run workload ...
//         auto post = h->read();
//         auto delta = post - pre;
//         // delta.counters[SCHED_CTX_INVOL] = preemptions during run
//     }
//
// If BPF is unavailable (no CAP_BPF, paranoid >2, kernel too old,
// libbpf load/verify fails), load() returns nullopt and the caller
// proceeds without sensory data — ns/cycles latencies are unaffected.

#include <crucible/effects/Capabilities.h>  // effects::Init capability tag
#include <crucible/safety/Borrowed.h>       // safety::Borrowed<T, Source>
#include <crucible/safety/Refined.h>        // safety::Refined / bounded_above

#include <array>
#include <cstdint>
#include <memory>
#include <optional>

namespace crucible::perf {

// Counter indices — must match enum sense_idx in
// include/crucible/perf/bpf/sense_hub.bpf.c.  Gaps in the numbering
// (e.g. 38–39, 75–95) are reserved slots mapped to zero; they exist
// so each subsystem lives on its own cache line.
enum Idx : uint32_t {
    // ── Cache line 0: Network State ──────────────────────────────
    NET_TCP_ESTABLISHED     = 0,
    NET_TCP_LISTEN          = 1,
    NET_TCP_TIME_WAIT       = 2,
    NET_TCP_CLOSE_WAIT      = 3,
    NET_TCP_OTHER           = 4,
    NET_UDP_ACTIVE          = 5,
    NET_UNIX_ACTIVE         = 6,
    NET_TX_BYTES            = 7,

    // ── Cache line 1: I/O + Files ────────────────────────────────
    NET_RX_BYTES            = 8,
    FD_CURRENT              = 9,
    FD_OPEN_OPS             = 10,
    IO_READ_BYTES           = 11,
    IO_WRITE_BYTES          = 12,
    IO_READ_OPS             = 13,
    IO_WRITE_OPS            = 14,
    MEM_MMAP_COUNT          = 15,

    // ── Cache line 2: Memory + Scheduler Core ────────────────────
    MEM_MUNMAP_COUNT        = 16,
    MEM_PAGE_FAULTS_MIN     = 17,
    MEM_PAGE_FAULTS_MAJ     = 18,
    MEM_BRK_CALLS           = 19,
    SCHED_CTX_VOL           = 20,
    SCHED_CTX_INVOL         = 21,
    SCHED_MIGRATIONS        = 22,
    SCHED_RUNTIME_NS        = 23,

    // ── Cache line 3: Scheduler + Contention ─────────────────────
    SCHED_WAIT_NS           = 24,
    FUTEX_WAIT_COUNT        = 25,
    FUTEX_WAIT_NS           = 26,
    THREADS_CREATED         = 27,
    SCHED_SLEEP_NS          = 28,
    SCHED_IOWAIT_NS         = 29,
    SCHED_BLOCKED_NS        = 30,
    WAKEUPS_RECEIVED        = 31,

    // ── Cache line 4: CPU Extended ───────────────────────────────
    KERNEL_LOCK_COUNT       = 32,
    KERNEL_LOCK_NS          = 33,
    SOFTIRQ_STOLEN_NS       = 34,
    THREADS_EXITED          = 35,
    CPU_FREQ_CHANGES        = 36,
    WAKEUPS_SENT            = 37,

    // ── Cache line 5: Memory Pressure ────────────────────────────
    RSS_ANON_BYTES          = 40,
    RSS_FILE_BYTES          = 41,
    RSS_SWAP_ENTRIES        = 42,
    RSS_SHMEM_BYTES         = 43,
    DIRECT_RECLAIM_COUNT    = 44,
    DIRECT_RECLAIM_NS       = 45,
    SWAP_OUT_PAGES          = 46,
    THP_COLLAPSE_OK         = 47,

    // ── Cache line 6: Memory Advanced + Block I/O ────────────────
    THP_COLLAPSE_FAIL       = 48,
    NUMA_MIGRATE_PAGES      = 49,
    COMPACTION_STALLS       = 50,
    EXTFRAG_EVENTS          = 51,
    DISK_READ_BYTES         = 52,
    DISK_WRITE_BYTES        = 53,
    DISK_IO_LATENCY_NS      = 54,
    DISK_IO_COUNT           = 55,

    // ── Cache line 7: I/O Advanced + Net Health ──────────────────
    PAGE_CACHE_MISSES       = 56,
    READAHEAD_PAGES         = 57,
    WRITE_THROTTLE_JIFFIES  = 58,
    IO_UNPLUG_COUNT         = 59,
    TCP_RETRANSMIT_COUNT    = 60,
    TCP_RST_SENT            = 61,
    TCP_ERROR_COUNT         = 62,
    SKB_DROP_COUNT          = 63,

    // ── Cache line 8: Net Health + Reliability ───────────────────
    TCP_MIN_SRTT_US         = 64,
    TCP_MAX_SRTT_US         = 65,
    TCP_LAST_CWND           = 66,
    TCP_CONG_LOSS           = 67,
    SIGNAL_FATAL_COUNT      = 68,
    SIGNAL_LAST_SIGNO       = 69,
    OOM_KILLS_SYSTEM        = 70,
    OOM_KILL_US             = 71,

    // ── Cache line 9: Reliability + Reserved ─────────────────────
    RECLAIM_STALL_LOOPS     = 72,
    THERMAL_MAX_TRIP        = 73,
    MCE_COUNT               = 74,

    // 11-cache-line layout — slots 75..95 reserved for future growth.
    NUM_COUNTERS            = 96,
};

// A point-in-time read of all 96 counters.  Diffing two snapshots
// gives per-run deltas; the subsystems with monotonic semantics
// (NET_TX_BYTES, MEM_PAGE_FAULTS_*, SCHED_*, etc.) read meaningfully
// as b - a.  Counters that behave as gauges (FD_CURRENT,
// TCP_MIN_SRTT_US, THERMAL_MAX_TRIP) are snapshots of instantaneous
// state — use b.
struct Snapshot {
    std::array<uint64_t, NUM_COUNTERS> counters{};

    [[nodiscard]] uint64_t operator[](Idx i) const noexcept {
        return counters[static_cast<uint32_t>(i)];
    }

    // Delta semantics with saturation-on-underflow.
    //
    // Most counters are monotonic (SCHED_CTX_*, NET_TX_BYTES, MEM_PAGE_
    // FAULTS_*, …) and b - a reads meaningfully as the run's delta.
    //
    // A handful of slots are instantaneous gauges rather than running
    // totals — the BPF program sets them via counter_set / counter_max
    // on each event rather than fetch-and-add:
    //
    //   FD_CURRENT, TCP_{MIN,MAX}_SRTT_US, TCP_LAST_CWND,
    //   THERMAL_MAX_TRIP, SIGNAL_LAST_SIGNO, OOM_KILL_US,
    //   RECLAIM_STALL_LOOPS, NET_TCP_*, NET_UDP_ACTIVE,
    //   NET_UNIX_ACTIVE, RSS_{ANON,FILE,SHMEM}_BYTES,
    //   RSS_SWAP_ENTRIES
    //
    // For these, post < pre is physically possible (a TCP connection
    // closed, RSS shrank).  A raw u64 subtraction wraps to ~2^64 and
    // the pretty-printer would render "18.4E" garbage.  Saturate the
    // subtraction to 0 so gauges that decreased simply read as "no
    // delta" in diffs — accurate enough without poisoning the table.
    //
    // Callers that need the true gauge value should pull it from the
    // post snapshot directly.
    //
    // std::sub_sat (P0543, C++26) would fit perfectly here, but
    // libstdc++ 16.0.1 hasn't wired __glibcxx_saturation_arithmetic
    // through yet.  Using __builtin_sub_overflow directly lowers to a
    // single SUB + CMOV on x86-64, identical to what std::sub_sat
    // would emit once available.
    [[nodiscard]] Snapshot operator-(const Snapshot& older) const noexcept {
        Snapshot r;
        for (size_t i = 0; i < NUM_COUNTERS; ++i) {
            uint64_t diff = 0;
            // Equivalent to std::sub_sat(counters[i], older.counters[i]) —
            // migrate once libstdc++ exposes __cpp_lib_saturation_arithmetic.
            if (__builtin_sub_overflow(counters[i], older.counters[i], &diff))
                [[unlikely]] {
                diff = 0;  // gauge decreased; saturate to zero
            }
            r.counters[i] = diff;
        }
        return r;
    }
};

// Snapshot is a thin wrapper over std::array<uint64_t, NUM_COUNTERS>.
// 768 bytes = 12 cache lines — load-bearing for the mmap contract:
// userspace reads/copies the array in one shot, any padding or extra
// members would break the wire-compatible layout with the BPF map.
static_assert(sizeof(Snapshot) == NUM_COUNTERS * sizeof(uint64_t),
              "Snapshot must be a tight 96*u64 = 768B = 12 cache lines; "
              "mmap contract with BPF_F_MMAPABLE array map depends on it");

class SenseHub {
 public:
    // Load the embedded BPF program, set target_tgid to getpid(),
    // attach every tracepoint, mmap the counter array.  Returns
    // std::nullopt if any step fails (missing CAP_BPF, kernel lacks
    // a tracepoint, verifier rejects, etc.).
    //
    // The first parameter is a `::crucible::effects::Init` capability
    // tag (1 byte, EBO-collapsed at most call sites) — load() is a
    // one-shot startup operation that consumes Alloc + IO and must
    // never be reachable from a hot-path frame.  Hot-path frames
    // hold no `effects::Init` token and therefore cannot construct
    // the argument; the absence of the cap is the type-system gate
    // that prevents accidental hot-path SenseHub::load() calls.
    // Init-time call sites construct the tag inline:
    //
    //     auto hub = crucible::perf::SenseHub::load(crucible::effects::Init{});
    //
    // The returned optional is typically cached in a process-wide
    // static so the load cost is paid exactly once.
    //
    // The diagnostic line (if any) is printed to stderr unless
    // CRUCIBLE_PERF_QUIET=1 (or the legacy CRUCIBLE_BENCH_BPF_QUIET=1)
    // is set in the environment.
    [[nodiscard]] static std::optional<SenseHub>
        load(::crucible::effects::Init) noexcept;

    // ~50 ns volatile read of 12 cache lines.
    [[nodiscard]] Snapshot read() const noexcept;

    // Direct view into the mmap'd counter array — for advanced
    // readers that want to pull specific slots without cloning the
    // whole vector.  Returned as a `safety::Borrowed<...>` typed
    // span: the lifetime of the underlying mmap is owned by the
    // SenseHub instance, so the Borrowed view's `Source` parameter
    // names that owner explicitly.  The view always spans
    // NUM_COUNTERS elements when present, and is the empty span
    // (`.empty() == true`) on a moved-from / un-loaded SenseHub.
    [[nodiscard]] safety::Borrowed<const volatile uint64_t, SenseHub>
        counters_view() const noexcept;

    // Number of bpf_link attachments the kernel accepted.  Bounded
    // above by the State::links inplace_vector capacity (64); the
    // `Refined<bounded_above<64>, size_t>` return type encodes the
    // bound at the type level so consumers can rely on it without
    // re-validating, and a future bump of the inplace_vector cap
    // will surface as a single point of edit (this header) rather
    // than an audit across every consumer.  Use `.value()` for the
    // raw count.  For every unattachable tracepoint (old kernel,
    // CONFIG_* missing), one program is silently dropped; inspect
    // this to know how much coverage we have.
    [[nodiscard]] safety::Refined<safety::bounded_above<64>, std::size_t>
        attached_programs() const noexcept;

    // Number of bpf_program__attach calls that failed (returned
    // NULL or an ERR_PTR).  Same `bounded_above<64>` envelope as
    // attached_programs() — both counters originate from the same
    // bounded program-iteration loop.  Non-zero means some
    // subsystems are dark; set CRUCIBLE_PERF_VERBOSE=1 (or the
    // legacy CRUCIBLE_BENCH_BPF_VERBOSE=1) to see which ones.
    [[nodiscard]] safety::Refined<safety::bounded_above<64>, std::size_t>
        attach_failures() const noexcept;

    SenseHub(const SenseHub&) =
        delete("SenseHub owns unique BPF object + mmap — copying would double-close");
    SenseHub& operator=(const SenseHub&) =
        delete("SenseHub owns unique BPF object + mmap — copying would double-close");
    SenseHub(SenseHub&&) noexcept;
    SenseHub& operator=(SenseHub&&) noexcept;
    ~SenseHub();

 private:
    struct State;
    SenseHub() noexcept;

    std::unique_ptr<State> state_;
};

}  // namespace crucible::perf
