#pragma once

// crucible::perf::ProcGauges — userspace /proc + /sys gauge poller for
// SenseHub v2.
//
// STATUS: PROMOTED — production surface alongside SenseHubV2.h.
//         Built when CRUCIBLE_SENSE_HUB_V2=ON.  Header-only declaration;
//         the corresponding ProcGauges.cpp impl lands in a follow-up PR.
//
// ─── DESIGN GOAL ──────────────────────────────────────────────────────
//
// Several signals SenseHub v2 wants to expose ARE ALREADY aggregated by
// the kernel and exposed via /proc and /sys.  Attaching a BPF tracepoint
// to count what the kernel already counts wastes 0.1-15 % CPU per
// signal (the highest-rate ones — slab alloc/free at 1M-10M/sec is the
// extreme case).
//
// ProcGauges reads these signals at SNAPSHOT time, NOT per-event.  Cost:
// ~50-100 µs once per snapshot (typically once per bench window ≥ 1 ms,
// so amortized cost is < 0.01 % CPU).
//
// Slots populated (basic build):
//   • SLAB_TOTAL_BYTES               from /proc/slabinfo
//   • HARDIRQ_TOTAL_COUNT            from /proc/interrupts
//   • NAPI_POLL_TOTAL                from /proc/net/softnet_stat
//   • SKB_DROP_REASON_TOTAL          from /proc/net/snmp
//   • TCP_RECV_BUFFER_MAX            from /proc/net/tcp
//   • BLOCK_QUEUE_DEPTH_MAX          from /sys/block/*/stat
//   • PRINTK_RING_BYTES_FREE         from /sys/kernel/debug optional
//
// Extended build adds:
//   • NUMA_HIT_RATIO_X100            from /proc/vmstat
//   • TCP_ESTABLISHED_CURRENT        from /proc/net/snmp
//   • LOAD_AVG_{1M,5M,15M}_X100      from /proc/loadavg
//   • PSI_{CPU,MEM,IO}_SOME_AVG10    from /proc/pressure/*
//
// ─── COST DISCIPLINE ──────────────────────────────────────────────────
//
// File-open overhead is the bulk cost (~5-10 µs per syscall).  We
// open() each file ONCE per ProcGauges instance, hold the fd, and
// pread() at offset 0 for each snapshot.  Files like /proc/interrupts
// (~50 KB on a 96-core box) are the most expensive read; budget ~30 µs.
//
// Total snapshot cost target: ≤ 100 µs end-to-end for basic, ≤ 200 µs
// extended.  Measured via bench/bench_perf_loader.cpp (#1285 GAPS-004i).
//
// ─── SIBLING REFS ─────────────────────────────────────────────────────
//   • SenseHubV2.h          — calls populate() at gauge snapshot time
//   • bpf/_drafts/sense_hub_v2.bpf.c — defines the gauge slot indices
//   • PmuSample.h           — extended PMU ratios projected separately

#include <crucible/effects/Capabilities.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/Linear.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace crucible::perf {

// Forward — gauge enum lives in SenseHubV2.h
enum class Gauge : uint32_t;

// ─── ScopedFd — Linear<int> RAII wrapper for /proc/sys file handles ──
//
// One per long-lived /proc/sys file.  Closed on destruction.  Move-only.
//
// (Deliberately a small private RAII helper rather than reusing
// safety::Linear — the file-descriptor invariants are local to this
// header and a 30-line embedded type costs less to read than the cross-
// header indirection.)

class ScopedFd {
public:
    ScopedFd() noexcept = default;
    explicit ScopedFd(int fd) noexcept : fd_{fd} {}
    ~ScopedFd() noexcept;
    ScopedFd(const ScopedFd&) =
        delete("ScopedFd owns a Linux file descriptor; copying would double-close on destruct");
    ScopedFd& operator=(const ScopedFd&) =
        delete("ScopedFd owns a Linux file descriptor; copying would double-close on destruct");
    ScopedFd(ScopedFd&& other) noexcept : fd_{other.fd_} { other.fd_ = -1; }
    ScopedFd& operator=(ScopedFd&& other) noexcept {
        if (this != &other) { close_(); fd_ = other.fd_; other.fd_ = -1; }
        return *this;
    }

    [[nodiscard]] int  raw()    const noexcept { return fd_; }
    [[nodiscard]] bool valid()  const noexcept { return fd_ >= 0; }

private:
    int fd_ = -1;
    void close_() noexcept;
};

// ─── ProcGauges — owns /proc + /sys file handles, populates gauges ───

class ProcGauges {
public:
    // Sentinel value written to a gauge slot when its source file is
    // unavailable (kernel missing CONFIG_PSI, /proc/slabinfo not
    // readable, etc.).  Distinguishes "no signal" from "signal == 0".
    // Bench-harness display recognises this and prints "n/a".
    static constexpr uint64_t UNAVAILABLE = static_cast<uint64_t>(-1);

    // Maximum number of /sys/block/* devices we track in the basic
    // build.  /sys/block iteration enumerates devices ONCE at init();
    // typical hosts have 1-8 (NVMe namespaces + virtual loops);
    // 32 covers production storage nodes with ample headroom.
    static constexpr std::size_t MAX_BLOCK_DEVS = 32;

    // init() opens every /proc/sys file ProcGauges needs.  Failures
    // per-file are recorded but never fatal — populate() simply writes
    // UNAVAILABLE to slots whose source failed to open.  Capability-
    // typed (Init only — opening these files is one-time-at-startup
    // work).
    [[nodiscard]] static std::optional<ProcGauges>
        init(::crucible::effects::Init) noexcept;

    // populate() reads every owned fd via pread(offset=0), parses,
    // and writes the resulting gauge values into `gauge_array` at the
    // user-sampled slot indices (16-22 in basic, 16-56 in extended).
    //
    // `gauge_array` must point to the SenseHub v2 gauges mmap region
    // (NUM_GAUGES uint64_t entries).  Caller responsibility — usually
    // SenseHubV2::read_gauges() drives this.
    //
    // Cost: ~50-100 µs in basic mode, ~150-200 µs extended.
    // Synchronous; do not call on the hot path.  The expectation is
    // ONE call per bench-window snapshot.
    void populate(uint64_t* gauge_array, std::size_t gauge_count) const noexcept;

    // Diagnostic — number of /proc files that opened successfully.
    [[nodiscard]] std::size_t open_count() const noexcept;

    // Move-only — owns multiple ScopedFd + heap scratch buffer.
    ProcGauges(const ProcGauges&) =
        delete("ProcGauges owns multiple ScopedFds + scratch buffer; copying would double-close all FDs");
    ProcGauges& operator=(const ProcGauges&) =
        delete("ProcGauges owns multiple ScopedFds + scratch buffer; copying would double-close all FDs");
    ProcGauges(ProcGauges&&) noexcept = default;
    ProcGauges& operator=(ProcGauges&&) noexcept = default;
    ~ProcGauges() noexcept = default;

private:
    // Each fd is opened once at init() and held for the process
    // lifetime.  pread(offset=0) is used at populate() time so we
    // don't need to lseek between reads.  When a file fails to open
    // (kernel CONFIG missing, EACCES, etc.), the fd remains invalid
    // and populate() writes UNAVAILABLE to the corresponding slot
    // instead of 0.
    ScopedFd fd_slabinfo_;        // /proc/slabinfo
    ScopedFd fd_interrupts_;      // /proc/interrupts
    ScopedFd fd_softnet_stat_;    // /proc/net/softnet_stat
    ScopedFd fd_snmp_;            // /proc/net/snmp
    ScopedFd fd_proc_net_tcp_;    // /proc/net/tcp

    // /sys/block requires per-device fds (each `stat` is an
    // independent file).  init() enumerates /sys/block/* once via
    // readdir(), opens up to MAX_BLOCK_DEVS `stat` files, and stashes
    // them here.  populate() iterates this fixed array; new devices
    // hot-plugged after init() are NOT picked up — re-init the
    // ProcGauges instance to refresh.
    std::array<ScopedFd, MAX_BLOCK_DEVS> fd_block_stats_;
    std::size_t                          num_block_devs_ = 0;

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    ScopedFd fd_vmstat_;          // /proc/vmstat
    ScopedFd fd_loadavg_;         // /proc/loadavg
    ScopedFd fd_pressure_cpu_;    // /proc/pressure/cpu  (kernel 4.20+
                                  // + CONFIG_PSI=y; UNAVAILABLE if
                                  // either is missing)
    ScopedFd fd_pressure_memory_; // /proc/pressure/memory
    ScopedFd fd_pressure_io_;     // /proc/pressure/io
#endif

    // Per-source scratch buffer.  Sized for the largest /proc file we
    // parse: /proc/interrupts on a 96-core host with ~200 IRQs is
    // ~50-80 KB; 128 KB gives margin for >256-core hosts.  Heap-
    // allocated ONCE at init() so populate() does not stack-alloc
    // the buffer (which would risk blowing thread stacks at 8 KB
    // pages for guard-only configurations).  Cost: 128 KB heap per
    // ProcGauges instance — negligible (one instance per process).
    static constexpr std::size_t SCRATCH_BYTES = 128 * 1024;
    std::unique_ptr<char[]> scratch_;  // owned heap buffer, alignof 64

    ProcGauges() noexcept = default;

    // Per-source readers.  Each reads the fd via pread into the
    // shared heap-owned `scratch_` buffer, parses the relevant
    // fields, returns the aggregated value.  Failures (EAGAIN, parse
    // errors, fd-not-valid) return UNAVAILABLE — ProcGauges never
    // throws.  All readers are `const` because writes target only the
    // caller-provided gauge_array, not member state.
    [[nodiscard]] uint64_t read_slab_total_bytes_() const noexcept;
    [[nodiscard]] uint64_t read_hardirq_total_count_() const noexcept;
    [[nodiscard]] uint64_t read_napi_poll_total_() const noexcept;
    [[nodiscard]] uint64_t read_skb_drop_reason_total_() const noexcept;
    [[nodiscard]] uint64_t read_tcp_recv_buffer_max_() const noexcept;
    [[nodiscard]] uint64_t read_block_queue_depth_max_() const noexcept;
    [[nodiscard]] uint64_t read_printk_ring_bytes_free_() const noexcept;

    // C2 fix — THP_SPLIT moved to userspace gauge.  Sourced from
    // /proc/vmstat fields `thp_split_*` (page/pmd/pud splits).
    [[nodiscard]] uint64_t read_thp_split_total_() const noexcept;

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    // Extended readers — see vmstat/loadavg/PSI parse helpers
    [[nodiscard]] uint64_t read_numa_hit_ratio_x100_() const noexcept;
    [[nodiscard]] uint64_t read_tcp_established_current_() const noexcept;
    void                   read_loadavg_x100_(uint64_t& out_1m,
                                              uint64_t& out_5m,
                                              uint64_t& out_15m) const noexcept;
    // PSI readers return UNAVAILABLE if /proc/pressure/* failed to
    // open (kernel <4.20 OR CONFIG_PSI=n).
    [[nodiscard]] uint64_t read_pressure_avg10_x100_(int fd,
                                                     std::string_view kind) const noexcept;
#endif
};

// ─── Per-source parser specs (declarative, document-only) ────────────
//
// Each /proc file has a stable wire format we depend on.  These specs
// are reference documentation — the actual parsing is in proc_gauges.cpp
// (production) or its draft equivalent.  Format breakage between kernel
// versions is the integration risk; mitigated by:
//   • Sentinel test that asserts at least N>0 gauge populates per slot
//     on `make test` (catches "we are reading 0 from a working source"
//     class of breakage)
//   • Per-source try/catch in populate() — one busted file does not
//     poison the rest

// /proc/slabinfo format (kernel 2.6+, format slabinfo: 2.1):
//   slabinfo - version: 2.1
//   # name <active_objs> <num_objs> <objsize> ...
//   ...
// Sum = Σ(num_objs × objsize) over all slabs.

// /proc/interrupts format:
//   "            CPU0       CPU1       CPU2 ..."
//   "  0:        ..."
//   "  1:        ..."
//   ...
// Sum = Σ(per-CPU columns) over all IRQ rows.  ~50 KB on 96-core box.

// /proc/net/softnet_stat format (one line per CPU):
//   "<packets_processed> <packets_dropped> <time_squeeze> ..."
// NAPI poll total = Σ(packets_processed) over all CPUs.

// /proc/net/snmp format:
//   "Tcp: ... <RetransSegs> ..."
// SKB drop = various fields; consult Documentation/networking/snmp_counter.rst.

// /proc/net/tcp format:
//   "  sl  local_address rem_address st tx_queue rx_queue tr tm->when retrnsmt uid timeout inode"
//   "   0: 0100007F:1F90 ... <rx_queue> ..."
// TCP_RECV_BUFFER_MAX = max(rx_queue) over all entries.

// /sys/block/<dev>/stat format:
//   "<read_ios> <read_merges> <read_sectors> <read_ticks>
//    <write_ios> <write_merges> <write_sectors> <write_ticks>
//    <in_flight> <io_ticks> <time_in_queue>"
// BLOCK_QUEUE_DEPTH_MAX = max(in_flight) over all block devs.

// /proc/loadavg format:
//   "<1m> <5m> <15m> <running/total> <last_pid>"
// Multiply by 100, store as uint64.

// /proc/pressure/{cpu,memory,io} format (kernel 4.20+, PSI):
//   "some avg10=<float> avg60=<float> avg300=<float> total=<u64>"
//   "full avg10=<float> avg60=<float> avg300=<float> total=<u64>"
// We expose `some avg10` × 100 as the basic indicator.

} // namespace crucible::perf
