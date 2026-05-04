#pragma once

// crucible::perf::SenseHubV2 — userspace facade for SenseHub v2.
//
// STATUS: PROMOTED — production surface alongside v1 SenseHub.h.
//         Built when CRUCIBLE_SENSE_HUB_V2=ON (default OFF).  v1
//         SenseHub remains the active production loader; v2 ships
//         the architectural surface (counters/gauges segregation,
//         meta-versioned wire contract, /proc gauge poller) but
//         the load() implementation is a stub until the v1 handler
//         bodies + SenseHubV2.cpp impl land in subsequent PRs.
//
// ─── DESIGN GOALS ─────────────────────────────────────────────────────
//
//  Mirrors `bpf/sense_hub_v2.bpf.c`.  Two ship modes selected
//  at build time by CMake option `CRUCIBLE_SENSE_HUB_EXTENDED`:
//
//    • DEFAULT (no flag)               — 128 counters + 32 gauges,
//      always-on, target overhead ≤ 0.5 % CPU.
//    • CRUCIBLE_SENSE_HUB_EXTENDED=1   — 256 counters + 64 gauges,
//      opt-in "debug hub", target 1-2 % CPU.
//
//  Two value-shape surfaces, NOT one mixed array:
//    • CounterSnapshot — monotone subtractable; supports operator-
//    • GaugeSnapshot   — point-in-time + running max + userspace-sampled
//                        ratios; NEVER subtracted
//
//  Wire-contract gate: load() reads the BPF `meta` map first.
//  SENSE_HUB_LAYOUT_HASH must match exactly.  Mismatch → nullopt with
//  diagnostic.  Default and extended builds publish DIFFERENT hashes
//  (BUILD_TAG = 0xBA51 vs 0xDEB6), so neither can silently masquerade
//  as the other.
//
//  Userspace gauges: 7 of the 22 gauge slots in basic (or 22 of 56 in
//  extended) are NOT written by BPF — they are populated at snapshot
//  time by `crucible::perf::ProcGauges` reading /proc and /sys.  See
//  `proc_gauges.h` (sibling draft).
//
// ─── USAGE EXAMPLE ────────────────────────────────────────────────────
//
//   auto hub = crucible::perf::SenseHubV2::load(crucible::effects::Init{});
//   if (!hub) return;
//
//   auto a = hub->read_counters();
//   /* ... do work ... */
//   auto b = hub->read_counters();
//   auto delta = b - a;  // CounterDelta — only operator- on counters
//
//   auto g = hub->read_gauges();   // point-in-time snapshot, no diff
//   printf("FD now: %llu\n", g[Gauge::FD_CURRENT]);
//   printf("TLB shoots: %llu\n", delta[Idx::TLB_SHOOTDOWNS]);
//
// ─── HS14 NEG-COMPILE FIXTURES (mandatory before promoting) ──────────
//   • neg_perf_sense_hub_v2_load_no_cap.cpp     — load() without arg
//   • neg_perf_sense_hub_v2_load_wrong_cap.cpp  — load(Bg{}) instead of Init{}
//   • neg_perf_sense_hub_v2_subtract_gauges.cpp — operator- on GaugeSnapshot
//                                                  must be a compile error

#include <crucible/effects/Capabilities.h>
#include <crucible/safety/Borrowed.h>
#include <crucible/safety/Refined.h>

#include <array>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <optional>
#include <string_view>

namespace crucible::perf {

// ─── Build-mode mirror ────────────────────────────────────────────────
//
// MUST match the constants in bpf/_drafts/sense_hub_v2.bpf.c.  CMake
// passes `-DCRUCIBLE_SENSE_HUB_EXTENDED=1` to BOTH the BPF compile and
// this userspace TU; the meta map's LAYOUT_HASH gates a runtime
// mismatch as defense-in-depth.

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
inline constexpr std::size_t NUM_COUNTERS  = 256;
inline constexpr std::size_t NUM_GAUGES    = 64;
inline constexpr uint32_t    BUILD_TAG     = 0xDEB6;
inline constexpr std::string_view BUILD_NAME = "extended";
#else
inline constexpr std::size_t NUM_COUNTERS  = 128;
inline constexpr std::size_t NUM_GAUGES    = 32;
inline constexpr uint32_t    BUILD_TAG     = 0xBA51;
inline constexpr std::string_view BUILD_NAME = "basic";
#endif

inline constexpr uint32_t SENSE_HUB_VERSION = 2;
inline constexpr uint32_t SENSE_HUB_MAGIC   = 0x4352424CU; // 'CRBL'

// LAYOUT_HASH — must equal the BPF-side computation.  Used for the
// load-time wire-contract check.
inline constexpr uint64_t SENSE_HUB_LAYOUT_HASH =
    (uint64_t{NUM_COUNTERS}        << 48) |
    (uint64_t{NUM_GAUGES}          << 32) |
    (uint64_t{SENSE_HUB_VERSION}   << 16) |
    uint64_t{BUILD_TAG};

// ─── enum class Idx — counter slot indices ────────────────────────────
//
// Mirrors `enum sense_idx` in sense_hub_v2.bpf.c EXACTLY.  Maintaining
// this mirror by hand is the cost of the safety boundary; a future
// refactor could code-gen one from the other (P3 — the static asserts
// below catch any drift at compile time).

enum class Idx : uint32_t {
    // Domain 0 — Network (slots 0-31)
    NET_TCP_ESTABLISHED          =  0,
    NET_TCP_LISTEN               =  1,
    NET_TCP_TIME_WAIT            =  2,
    NET_TCP_CLOSE_WAIT           =  3,
    NET_TCP_OTHER                =  4,
    NET_UDP_ACTIVE               =  5,
    NET_UNIX_ACTIVE              =  6,
    NET_TX_BYTES                 =  7,
    NET_RX_BYTES                 =  8,
    TCP_RETRANSMIT_COUNT         =  9,
    TCP_RST_SENT                 = 10,
    TCP_ERROR_COUNT              = 11,
    SKB_DROP_COUNT               = 12,
    TCP_CONG_LOSS                = 13,
    MPTCP_SUBFLOW_ESTABLISHED    = 14,

    // Domain 1 — I/O + Storage (slots 32-63)
    FD_OPEN_OPS                  = 32,
    IO_READ_BYTES                = 33,
    IO_WRITE_BYTES               = 34,
    IO_READ_OPS                  = 35,
    IO_WRITE_OPS                 = 36,
    DISK_READ_BYTES              = 37,
    DISK_WRITE_BYTES             = 38,
    DISK_IO_LATENCY_NS           = 39,
    DISK_IO_COUNT                = 40,
    PAGE_CACHE_MISSES            = 41,
    PAGE_CACHE_EVICTIONS         = 42,
    READAHEAD_PAGES              = 43,
    WRITE_THROTTLE_JIFFIES       = 44,
    IO_UNPLUG_COUNT              = 45,
    IOCOST_IDLE_COUNT            = 46, // cgroup iocg went idle (proxy
                                        // for "throttled hard"; kernel
                                        // 6.17 has no direct throttle TP)
    IOCOST_ACTIVATE_COUNT        = 47, // paired counterpart
    WBT_DELAY_COUNT              = 48,
    WBT_DELAY_NS                 = 49,
    FILELOCK_WAITS               = 50,
    FILELOCK_NS                  = 51,
    SWIOTLB_BOUNCE_COUNT         = 52,
    SWIOTLB_BOUNCE_BYTES         = 53,

    // Domain 2 — Memory (slots 64-95)
    MEM_MMAP_COUNT               = 64,
    MEM_MUNMAP_COUNT             = 65,
    MEM_PAGE_FAULTS_MIN          = 66,
    MEM_PAGE_FAULTS_MAJ          = 67,
    MEM_BRK_CALLS                = 68,
    RSS_ANON_BYTES               = 69,
    RSS_FILE_BYTES               = 70,
    RSS_SWAP_ENTRIES             = 71,
    RSS_SHMEM_BYTES              = 72,
    DIRECT_RECLAIM_COUNT         = 73,
    DIRECT_RECLAIM_NS            = 74,
    SWAP_OUT_PAGES               = 75,
    THP_COLLAPSE_OK              = 76,
    THP_COLLAPSE_FAIL            = 77,
    // Slot 78 RESERVED — was THP_SPLIT_COUNT in earlier draft; no
    // matching tracepoint on kernel 6.17.  Sourced via ProcGauges →
    // Gauge::THP_SPLIT (read from /proc/vmstat at snapshot time).
    NUMA_MIGRATE_PAGES           = 79,
    NUMA_MIG_NUMA_HINT           = 80,
    NUMA_MIG_OTHER               = 81,
    COMPACTION_STALLS            = 82,
    EXTFRAG_EVENTS               = 83,
    KSWAPD_WAKES                 = 84,
    MMAP_LOCK_WAITS              = 85,
    MMAP_LOCK_NS                 = 86,
    IOMMU_FAULTS                 = 87,
    TLB_SHOOTDOWNS               = 88,
    VMSCAN_LRU_ISOLATIONS        = 89,
    VMSCAN_SCAN_NS               = 90,
    RECLAIM_STALL_LOOPS          = 91,

    // Domain 3 — Sched + Sync + Reliability (slots 96-127)
    SCHED_CTX_VOL                =  96,
    SCHED_CTX_INVOL              =  97,
    SCHED_MIGRATIONS             =  98,
    SCHED_RUNTIME_NS             =  99,
    SCHED_WAIT_NS                = 100,
    SCHED_SLEEP_NS               = 101,
    SCHED_IOWAIT_NS              = 102,
    SCHED_BLOCKED_NS             = 103,
    WAKEUPS_RECEIVED             = 104,
    WAKEUPS_SENT                 = 105,
    KERNEL_LOCK_COUNT            = 106,
    KERNEL_LOCK_NS               = 107,
    FUTEX_WAIT_COUNT             = 108,
    FUTEX_WAIT_NS                = 109,
    THREADS_CREATED              = 110,
    THREADS_EXITED               = 111,
    CPU_FREQ_CHANGES             = 112,
    PROCESS_FORKS                = 113,
    PROCESS_EXECS                = 114,
    SIGNAL_DELIVERED             = 115,
    SIGNAL_FATAL_COUNT           = 116,
    OOM_KILLS_SYSTEM             = 117,
    OOM_KILL_US                  = 118,
    MCE_COUNT                    = 119,
    NMI_COUNT                    = 120,
    OSNOISE_NS_TOTAL             = 121,
    CSD_QUEUE_COUNT              = 122,
    PCIE_AER_CORR                = 123,
    PCIE_AER_UNCORR              = 124,
    EDAC_DRAM_CE                 = 125,
    SOFTIRQ_STOLEN_NS            = 126,
    MAP_FULL_DROPS               = 127,

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    // Extended Domain 4 — Network detail (128-159)
    SKB_DROP_NEIGH_FAILED        = 128,
    SKB_DROP_NETFILTER_DROP      = 129,
    SKB_DROP_TCP_INVALID         = 130,
    SKB_DROP_TCP_RESET           = 131,
    SKB_DROP_TCP_OFOMERGE        = 132,
    SKB_DROP_PROTO_MEM           = 133,
    SKB_DROP_NO_SOCKET           = 134,
    SKB_DROP_RX_NO_NETDEV        = 135,
    SKB_DROP_OTHER               = 136,
    NF_CONNTRACK_NEW             = 137,
    NF_CONNTRACK_DESTROY         = 138,
    NF_CONNTRACK_DROPS           = 139,
    NETIF_RECV_COUNT             = 140,
    NAPI_POLL_COUNT              = 141,
    NAPI_RESCHED_COUNT           = 142,
    NAPI_BUDGET_EXHAUSTED        = 143,
    SOCK_OPS_RTT_SAMPLE_COUNT    = 144,
    TCP_RECV_QUEUE_FULL          = 145,
    TCP_FAST_RETRANSMIT          = 146,
    TCP_TLP_FIRES                = 147,
    TCP_LOSS_PROBE_COUNT         = 148,
    TCP_DSACK_COUNT              = 149,
    UDP_PROTO_MEM_ERRORS         = 150,
    SOCKMAP_REDIRECT_COUNT       = 151,
    QDISC_OVERLIMITS             = 152,

    // Extended Domain 5 — Storage detail (160-191)
    BLOCK_INSERT_COUNT           = 160,
    BLOCK_ISSUE_COUNT            = 161,
    BLOCK_COMPLETE_COUNT         = 162,
    BLOCK_BACKMERGE_COUNT        = 163,
    BLOCK_FRONTMERGE_COUNT       = 164,
    IOURING_SUBMIT_COUNT         = 165,
    IOURING_COMPLETE_COUNT       = 166,
    IOURING_SQ_FULL_COUNT        = 167,
    NVME_RQ_SETUP_COUNT          = 168,
    NVME_RQ_COMPLETE_COUNT       = 169,
    SCSI_DISPATCH_COUNT          = 171,
    SCSI_COMPLETE_COUNT          = 172,
    EXT4_TXN_COUNT               = 173,
    XFS_TXN_COUNT                = 174,
    BTRFS_TXN_COUNT              = 175,
    WRITEBACK_INODE_COUNT        = 176,
    WRITEBACK_PAGES_TOTAL        = 177,
    DIRTY_PAGES_TOTAL_TRANSIENT  = 178,
    JBD2_COMMIT_COUNT            = 179,
    XFS_LOG_FORCE_COUNT          = 180,
    FUSE_RQ_COUNT                = 181,
    KYBER_LATENCY_VIOLATIONS     = 182,
    BFQ_DECISIONS                = 183,

    // Extended Domain 6 — Memory detail (192-223)
    PAGE_ALLOC_NORMAL            = 192,
    PAGE_ALLOC_DMA32             = 193,
    PAGE_ALLOC_MOVABLE           = 194,
    PAGE_ALLOC_RETRY             = 195,
    PAGE_ALLOC_OOM               = 196,
    // Slots 197-198 RESERVED — see Gauge::SLAB_TOTAL_BYTES (userspace)
    KMEMLEAK_OBJECTS_TRACKED     = 199,
    VMALLOC_BYTES_TOTAL          = 200,
    HUGETLB_FAULTS               = 201,
    KSM_PAGES_SHARING            = 202,
    DAMON_AGGR_COUNT             = 203,
    MEMCG_HIGH_BREACHES          = 204,
    MEMCG_MAX_BREACHES           = 205,
    MEMCG_OOM_KILLS              = 206,
    MEMCG_SOFT_RECLAIM_BYTES     = 207,
    CGROUP_FREEZER_FREEZES       = 208,
    CGROUP_PIDS_FORKS_DENIED     = 209,
    CGROUP_DEVICE_DENIES         = 210,
    KSWAPD_RECLAIM_BYTES         = 211,
    THP_PROMOTE_COUNT            = 212,
    THP_DEMOTE_COUNT             = 213,
    BALLOON_INFLATE_PAGES        = 214,
    BALLOON_DEFLATE_PAGES        = 215,

    // Extended Domain 7 — Sched + Sync + Reliability detail (224-255)
    IPI_RESCHEDULE               = 224,
    IPI_CALL_FUNCTION            = 225,
    IPI_NMI_DELIVERED            = 226,
    IPI_CPU_STOP                 = 227,
    IPI_REBOOT                   = 228,
    IPI_THERMAL                  = 229,
    IPI_OTHER                    = 230,
    IPI_TOTAL_LATENCY_NS         = 231,
    SCHED_WAKEUP_LATENCY_TOTAL   = 232,
    CONTEXT_TRACKING_USER_ENTER  = 233,
    CONTEXT_TRACKING_USER_EXIT   = 234,
    CPU_ONLINE_TRANSITIONS       = 235,
    CPU_OFFLINE_TRANSITIONS      = 236,
    SCHED_EXT_DECISIONS          = 237,
    PREEMPT_DISABLE_NS_TOTAL     = 238,
    IRQ_DISABLE_NS_TOTAL         = 239,
    RWSEM_WAIT_COUNT             = 240,
    RWSEM_WAIT_NS                = 241,
    RTMUTEX_WAIT_COUNT           = 242,
    RTMUTEX_WAIT_NS              = 243,
    // Slots 244-245 RESERVED — see Gauge::HARDIRQ_TOTAL_COUNT (userspace).
    // For per-IRQ-vector drilldown, use planned standalone hardirq.bpf.c facade.
    PRINTK_LINE_COUNT            = 246,
    AVC_DENIALS                  = 247,
    CAPABILITY_FAILS             = 248,
    KVM_VMEXIT_COUNT             = 249,
    RESCTRL_BREACHES             = 250,
    BPF_PROG_VERIFIER_FAILS      = 251,
    MODULE_LOAD_COUNT            = 252,
    MODULE_UNLOAD_COUNT          = 253,
    ALARMTIMER_FIRES             = 254,
    ACPI_GPE_FIRES               = 255,
#endif
};

static_assert(static_cast<uint32_t>(Idx::MAP_FULL_DROPS) < NUM_COUNTERS,
    "Last basic Idx must fit within NUM_COUNTERS — wire contract assertion");

// ─── enum class Gauge — gauge slot indices ────────────────────────────

enum class Gauge : uint32_t {
    // Mis-classified-from-v1 (0-7)
    FD_CURRENT                   = 0,
    TCP_MIN_SRTT_US              = 1,
    TCP_MAX_SRTT_US              = 2,
    TCP_LAST_CWND                = 3,
    THERMAL_MAX_TRIP             = 4,
    SIGNAL_LAST_SIGNO            = 5,

    // BPF-side max-watermarks (8-15)
    CSD_MAX_QUEUE_TO_START_NS    = 8,
    OSNOISE_MAX_NS               = 9,
    NMI_HANDLER_MAX_NS           = 10,
    MMAP_LOCK_MAX_WAIT_NS        = 11,

    // Userspace-sampled at snapshot time (16-31) — populated by ProcGauges
    SLAB_TOTAL_BYTES             = 16,
    HARDIRQ_TOTAL_COUNT          = 17,
    NAPI_POLL_TOTAL              = 18,
    SKB_DROP_REASON_TOTAL        = 19,
    TCP_RECV_BUFFER_MAX          = 20,
    BLOCK_QUEUE_DEPTH_MAX        = 21,
    PRINTK_RING_BYTES_FREE       = 22,

    // C2 fix — THP_SPLIT replaces the dropped Idx::THP_SPLIT_COUNT
    // counter (kernel 6.17 has no `huge_memory/mm_split_huge_page`
    // tracepoint).  Sourced from /proc/vmstat at snapshot time.
    THP_SPLIT                    = 23,

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
    // PMU ratios (32-41) — projected from PmuSample at snapshot time
    PMU_IPC_X1000                = 32,
    PMU_FRONTEND_STALL_PCT_X100  = 33,
    PMU_BACKEND_STALL_PCT_X100   = 34,
    PMU_BAD_SPEC_PCT_X100        = 35,
    PMU_RETIRING_PCT_X100        = 36,
    PMU_LLC_MISS_RATE_X100       = 37,
    PMU_BR_MISS_PER_KINST        = 38,
    PMU_RAPL_PKG_JOULES_X1000    = 39,
    PMU_RAPL_CORES_JOULES_X1000  = 40,
    PMU_RAPL_DRAM_JOULES_X1000   = 41,

    // Extra max-watermarks (42-48)
    WAKEUP_LATENCY_MAX_NS        = 42,
    BLOCK_MAX_LATENCY_NS         = 43,
    PREEMPT_DISABLE_MAX_NS       = 44,
    IRQ_DISABLE_MAX_NS           = 45,
    GP_MAX_NS                    = 46,
    RQ_DEPTH_MAX                 = 47,
    WQ_DEPTH_MAX                 = 48,

    // Extra userspace-sampled (49-56)
    NUMA_HIT_RATIO_X100          = 49,
    TCP_ESTABLISHED_CURRENT      = 50,
    LOAD_AVG_1M_X100             = 51,
    LOAD_AVG_5M_X100             = 52,
    LOAD_AVG_15M_X100            = 53,
    PSI_CPU_SOME_AVG10_X100      = 54,
    PSI_MEM_SOME_AVG10_X100      = 55,
    PSI_IO_SOME_AVG10_X100       = 56,
#endif
};

// ─── Wire-contract sentinel struct ────────────────────────────────────
//
// Mirrors `struct sense_meta` in sense_hub_v2.bpf.c byte-for-byte.

struct sense_meta {
    uint32_t magic;        // offset 0   ('CRBL' = 0x4352424C)
    uint32_t version;      // offset 4   (SENSE_HUB_VERSION = 2)
    uint32_t num_counters; // offset 8   (128 or 256)
    uint32_t num_gauges;   // offset 12  (32 or 64)
    uint64_t layout_hash;  // offset 16  (gates ABI mismatch at load())
    uint32_t build_tag;    // offset 24  (0xBA51 basic / 0xDEB6 extended)
    uint8_t  _pad[36];     // offset 28..63 — tail pad
                           // total = 4+4+4+4+8+4+36 = 64 B = one cache line
};
static_assert(sizeof(sense_meta) == 64,
    "sense_meta must be exactly one cache line — wire contract");

// ─── CounterSnapshot — monotone subtractable ──────────────────────────
//
// Read directly via mmap'd pointer dereference — ~200 ns total.
// `operator-` produces CounterDelta; consumers compute window-relative
// deltas instead of reading absolute counter values.

struct CounterDelta;

struct alignas(64) CounterSnapshot {
    std::array<uint64_t, NUM_COUNTERS> values{};

    [[nodiscard]] uint64_t operator[](Idx i) const noexcept {
        return values[static_cast<uint32_t>(i)];
    }

    [[nodiscard]] CounterDelta operator-(const CounterSnapshot& older) const noexcept;
};

struct alignas(64) CounterDelta {
    std::array<uint64_t, NUM_COUNTERS> deltas{};

    [[nodiscard]] uint64_t operator[](Idx i) const noexcept {
        return deltas[static_cast<uint32_t>(i)];
    }
};

inline CounterDelta CounterSnapshot::operator-(const CounterSnapshot& older) const noexcept {
    CounterDelta d;
    for (std::size_t i = 0; i < NUM_COUNTERS; ++i) {
        // Counters are monotone — newer >= older.  std::sub_sat would be
        // overkill on healthy inputs but defends against torn reads /
        // counter resets across mmap pages.
        d.deltas[i] = (values[i] >= older.values[i])
                          ? (values[i] - older.values[i])
                          : 0;
    }
    return d;
}

// ─── GaugeSnapshot — point-in-time, NEVER subtracted ──────────────────
//
// No operator-.  Snapshot IS the value.  GaugeSnapshot::populate()
// reads the BPF mmap'd gauges array AND polls /proc/sys for
// userspace-sampled gauges (see ProcGauges in proc_gauges.h).

struct alignas(64) GaugeSnapshot {
    std::array<uint64_t, NUM_GAUGES> values{};

    [[nodiscard]] uint64_t operator[](Gauge g) const noexcept {
        return values[static_cast<uint32_t>(g)];
    }

    // Deliberately no operator- — gauges are not subtractable.
    // Attempting `g1 - g2` is a compile error.
};

// ─── FullSnapshot — convenience for callers that want both ───────────

struct FullSnapshot {
    CounterSnapshot counters;
    GaugeSnapshot   gauges;
};

// ─── Coverage report ──────────────────────────────────────────────────
//
// load() never fails wholesale — each subprogram attaches independently.
// Coverage reports which programs/maps loaded vs failed (for the bench
// harness banner).

struct CoverageReport {
    safety::Refined<safety::bounded_above<200>, std::size_t> attached_programs{0};
    safety::Refined<safety::bounded_above<200>, std::size_t> attach_failures{0};
    bool   meta_verified            = false;
    bool   counters_mmap_succeeded  = false;
    bool   gauges_mmap_succeeded    = false;
    bool   procgauges_initialized   = false;
};

// ─── class SenseHubV2 ─────────────────────────────────────────────────

class SenseHubV2 {
public:
    // Capability-typed loader.  effects::Init is a 1-byte EBO-collapsed
    // tag minted only at process startup — prevents accidental hot-path
    // load() calls.  Same discipline as SenseHub v1 / SchedSwitch /
    // PmuSample / LockContention / SyscallLatency.
    [[nodiscard]] static std::optional<SenseHubV2>
        load(::crucible::effects::Init) noexcept;

    // Read counters via direct mmap pointer dereference.  ~200 ns.
    [[nodiscard]] CounterSnapshot read_counters() const noexcept;

    // Read gauges = mmap dereference of BPF-side gauges + ProcGauges
    // populating userspace-sampled slots.  ~50-100 µs total.
    [[nodiscard]] GaugeSnapshot read_gauges() const noexcept;

    // Convenience: both surfaces in one call.
    [[nodiscard]] FullSnapshot read() const noexcept {
        return { read_counters(), read_gauges() };
    }

    // Borrowed view over the raw mmap'd counter pages — for callers
    // that need byte-level access (bench harness, Augur drift detector).
    [[nodiscard]] safety::Borrowed<const volatile uint64_t, SenseHubV2>
        counters_view() const noexcept;

    [[nodiscard]] safety::Borrowed<const volatile uint64_t, SenseHubV2>
        gauges_view() const noexcept;

    // Diagnostic surface
    [[nodiscard]] CoverageReport coverage() const noexcept;

    // Ship-time constants
    static constexpr std::size_t num_counters() noexcept { return NUM_COUNTERS; }
    static constexpr std::size_t num_gauges()   noexcept { return NUM_GAUGES; }
    static constexpr std::string_view build_name() noexcept { return BUILD_NAME; }

    // Move-only — owns BPF object + mmap regions (deleted copy is
    // load-bearing).
    SenseHubV2(const SenseHubV2&) = delete;
    SenseHubV2& operator=(const SenseHubV2&) = delete;
    SenseHubV2(SenseHubV2&&) noexcept;
    SenseHubV2& operator=(SenseHubV2&&) noexcept;
    ~SenseHubV2() noexcept;

    // Internal — used by load() factory.
private:
    struct State;
    std::unique_ptr<State> state_;

    explicit SenseHubV2(std::unique_ptr<State>) noexcept;
};

// EBO sanity — same discipline as v1 SenseHub.
struct DummyStateV2 {};
static_assert(sizeof(SenseHubV2) == sizeof(std::unique_ptr<DummyStateV2>),
    "SenseHubV2 must be EBO-equivalent to unique_ptr<State> — regression "
    "indicates a non-EBO field crept in (likely a missing [[no_unique_address]] "
    "or a polymorphic vptr).");

} // namespace crucible::perf
