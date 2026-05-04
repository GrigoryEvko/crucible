/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * sense_hub_v2.bpf.c — SenseHub v2: 128-counter basic + opt-in 256-counter debug.
 *
 * STATUS: PROMOTED — production surface lives at
 *         `include/crucible/perf/bpf/sense_hub_v2.bpf.c` alongside the
 *         existing v1 `sense_hub.bpf.c`.  Built by CMake under
 *         CRUCIBLE_HAVE_BPF + CRUCIBLE_SENSE_HUB_V2 (default OFF until
 *         the v1-handler-carry-forward + extended-handler bodies (see
 *         TODO sections below) are filled in).  v1 remains the active
 *         production loader during the transition; v2 surface is
 *         compiled-and-validated but not consumed yet.
 *
 * ─── DESIGN GOALS ─────────────────────────────────────────────────────
 *
 *  1. Two ship modes selected at build time:
 *
 *     • DEFAULT (no flag)            — 128 counters + 32 gauges,
 *       always-on, target overhead ≤ 0.5 % CPU on a 96-core / 10G NIC
 *       host.  Only LOW-RATE tracepoints attached (event rate × handler
 *       cost ≤ 100 µs/sec/CPU per slot).
 *
 *     • CRUCIBLE_SENSE_HUB_EXTENDED — 256 counters + 64 gauges, opt-in
 *       "debug hub", target overhead 1-2 % CPU.  Adds higher-rate
 *       tracepoints (skb_drop reason rebucket, ipi per-reason,
 *       context_tracking user_enter/exit, workqueue queued, napi_poll
 *       metrics, block_rq lifecycle, io_uring submit/complete,
 *       nf_conntrack churn, sched_wakeup latency, hardirq breakdown,
 *       cpu_idle residency, rcu_gp summary, rwsem/rtmutex contention,
 *       drm_fences, etc.)  Most extended handlers gate on `is_target()`
 *       so the per-event cost is amortized over the population sample.
 *
 *  2. Two value-shape surfaces, NOT one mixed array (fixes a latent bug
 *     in v1 where TCP_MIN_SRTT_US / TCP_MAX_SRTT_US / TCP_LAST_CWND /
 *     FD_CURRENT / THERMAL_MAX_TRIP / SIGNAL_LAST_SIGNO were stored as
 *     "counters" but `Snapshot::operator-` against gauge slots returns
 *     garbage):
 *
 *     • `counters[]`  — monotone, subtractable.  Userspace
 *       `CounterSnapshot::operator-` works on these.
 *     • `gauges[]`    — point-in-time + running max.  Snapshot is the
 *       value; subtraction is undefined and the userspace API does not
 *       offer it.
 *
 *  3. Wire-contract versioning via `meta` map.  SenseHub v2 publishes
 *     LAYOUT_HASH that bakes (NUM_COUNTERS, NUM_GAUGES, build-tag).
 *     Userspace verifies on load(); mismatch returns nullopt with a
 *     clear diagnostic.  Default and extended builds publish DIFFERENT
 *     hashes so neither can silently masquerade as the other.
 *
 *  4. Audit-1 discipline applied UPFRONT (vs v1 where 5 inflight
 *     hash maps and 12 ktime calls and zero compiler barriers landed
 *     and were retrofitted later):
 *       • inflight maps are BPF_MAP_TYPE_LRU_HASH (orphan auto-eviction)
 *       • single bpf_ktime_get_ns() per event, captured at function
 *         entry, reused for delta + timestamp
 *       • compiler barrier (`__asm__ __volatile__("" ::: "memory")`)
 *         before any event-publication store so clang -O2 cannot
 *         reorder field stores past the publication marker
 *
 *  5. Slot-allocation discipline:
 *       • Counters laid out in 4 cache-line-aligned domains × 32 slots
 *       • Each domain has reserved slots at the end for additive growth
 *       • Mis-classified v1 slots moved to gauges[] under same-spelled
 *         names (no name churn, just kind change)
 *       • Each NEW counter justified against the rate × handler-cost
 *         budget in a comment at its enum declaration
 *
 *  6. Maximum-perf path: high-rate signals do NOT live here.  They live
 *     in standalone facades (SchedSwitch, LockContention, SyscallLatency,
 *     LockContention, future BlockRq, IoUring, Workqueue, NfConntrack)
 *     which the user opts into per workload via the `Senses` aggregator
 *     (#1287 GAPS-004y).  For signals that the kernel ALREADY exposes
 *     in /proc and /sys (NAPI poll count, hardirq per vector, slab byte
 *     totals, skb_drop reason counts), userspace polls them at snapshot
 *     time via `crucible::perf::ProcGauges` (no BPF cost at all).
 *
 * ─── EVENT-RATE BUDGETS ───────────────────────────────────────────────
 *
 *  A "slot budget" for inclusion in the basic build is rate × handler
 *  cost ≤ 100 µs/sec/CPU (≈ 0.01 % CPU per slot).  The handler cost
 *  baseline is ~80-150 ns including the is_target() gate, the array
 *  lookup, and the atomic_fetch_add.  So:
 *
 *    • <1 K events/sec aggregate → unconditionally cheap (most TLB,
 *      mmap_lock, kswapd, IOMMU, AER, EDAC, NMI, MCE, OOM, signals,
 *      forks, execs, swiotlb, iocost, wbt, filelock, mptcp, printk,
 *      avc, capability, module load, alarmtimer, acpi gpe, clocksource,
 *      resctrl, kvm vmexit fall in this bucket on typical hosts)
 *
 *    • 1 K-100 K events/sec → borderline; gate on is_target() so the
 *      per-tenant rate is < 1 K (futex, kernel lock, page cache miss,
 *      vmscan, csd, thp, numa migrate, tcp retransmit, sched runtime
 *      accounting fall here)
 *
 *    • >100 K events/sec → too expensive even with is_target().  These
 *      go to standalone facades (sched_switch, syscalls, kmem, irq,
 *      block, io_uring, napi, workqueue) OR to userspace polling
 *      (skb_drop reason, hardirq per vector, slab byte totals).
 *
 * ─── BUILD-TIME SELECTION ─────────────────────────────────────────────
 *
 *  CMake option (top-level CMakeLists.txt):
 *
 *      option(CRUCIBLE_SENSE_HUB_EXTENDED
 *             "Use 256-counter / 64-gauge SenseHub debug hub
 *              (vs 128/32 default)" OFF)
 *
 *      if (CRUCIBLE_SENSE_HUB_EXTENDED)
 *        set(_HUB_DEF "-DCRUCIBLE_SENSE_HUB_EXTENDED=1")
 *      endif()
 *
 *      crucible_bpf_program(sense_hub
 *          include/crucible/perf/bpf/sense_hub.bpf.c
 *          EXTRA_FLAGS ${_HUB_DEF})
 *      target_compile_definitions(crucible_perf PRIVATE ${_HUB_DEF})
 *
 *  Both BPF and userspace must agree on the flag.  The `meta` map's
 *  LAYOUT_HASH gates ABI mismatch at runtime as defense-in-depth.
 *
 * ─── SIBLING REFS ─────────────────────────────────────────────────────
 *  • SenseHubV2.h               — userspace facade
 *  • proc_gauges.h              — userspace /proc gauge poller
 *  • Senses (planned #1287)     — multi-facade aggregator
 *  • SchedSwitch / LockContention / SyscallLatency / PmuSample — high-rate
 *    siblings the basic hub deliberately does NOT subsume
 *  • _ext_sense_hub.md          — original 14-fold-in design (folded into
 *    this v2; doc can be retired when v2 ships)
 */

#include "common.h"

/* ─── Build-mode detection ────────────────────────────────────────────
 *
 * BASIC (default):     CRUCIBLE_SENSE_HUB_EXTENDED undefined
 *                      → 128 counters, 32 gauges, ~0.5 % CPU
 * EXTENDED (opt-in):   CRUCIBLE_SENSE_HUB_EXTENDED defined to 1
 *                      → 256 counters, 64 gauges, ~1-2 % CPU
 */
#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
#  define SENSE_HUB_NUM_COUNTERS   256
#  define SENSE_HUB_NUM_GAUGES     64
#  define SENSE_HUB_BUILD_TAG      0xDEB6  /* "DEBUG" */
#else
#  define SENSE_HUB_NUM_COUNTERS   128
#  define SENSE_HUB_NUM_GAUGES     32
#  define SENSE_HUB_BUILD_TAG      0xBA51  /* "BASIC" */
#endif

#define SENSE_HUB_VERSION          2
#define SENSE_HUB_MAGIC            0x4352424CU /* 'CRBL' */

/* LAYOUT_HASH — gates ABI mismatch at userspace load() time.
 * Same formula must be computed in SenseHubV2.h.  Differing builds
 * produce differing hashes; userspace refuses load() on mismatch. */
#define SENSE_HUB_LAYOUT_HASH \
    (((unsigned long long)SENSE_HUB_NUM_COUNTERS << 48) | \
     ((unsigned long long)SENSE_HUB_NUM_GAUGES   << 32) | \
     ((unsigned long long)SENSE_HUB_VERSION      << 16) | \
     (unsigned long long)SENSE_HUB_BUILD_TAG)

/* Constants from FUTEX cmd field, kernel includes */
#define FUTEX_WAIT          0
#define FUTEX_WAIT_BITSET   9
#define FUTEX_LOCK_PI       6
#define FUTEX_CMD_MASK      127

/* skb_drop_reason buckets we care about (extended only) — derived from
 * include/net/dropreason-core.h.  ~80 reasons exist; we bucket into the
 * load-bearing ones plus OTHER. */
#define SKB_DROP_NEIGH_FAILED_BUCKET        0
#define SKB_DROP_NETFILTER_DROP_BUCKET      1
#define SKB_DROP_TCP_INVALID_BUCKET         2
#define SKB_DROP_TCP_RESET_BUCKET           3
#define SKB_DROP_TCP_OFOMERGE_BUCKET        4
#define SKB_DROP_PROTO_MEM_BUCKET           5
#define SKB_DROP_NO_SOCKET_BUCKET           6
#define SKB_DROP_RX_NO_NETDEV_BUCKET        7
#define SKB_DROP_OTHER_BUCKET               8

/* IPI reason buckets (extended only) — see arch/x86/include/asm/irq_vectors.h.
 * Mirrored from linux/sched.h and arch IPI definitions. */
#define IPI_REASON_RESCHEDULE_BUCKET        0
#define IPI_REASON_CALL_FUNCTION_BUCKET     1
#define IPI_REASON_NMI_BUCKET               2
#define IPI_REASON_CPU_STOP_BUCKET          3
#define IPI_REASON_REBOOT_BUCKET            4
#define IPI_REASON_THERMAL_BUCKET           5
#define IPI_REASON_OTHER_BUCKET             6

/* ═══════════════════════════════════════════════════════════════════════
 * ──── enum sense_idx — basic 128-slot counter allocation ──────────────
 * ═══════════════════════════════════════════════════════════════════════
 *
 * Layout: 4 domains × 32 slots = 4 × 4 cache lines = 16 cache lines = 1024 B.
 *
 * Each domain has reserved slots at the end for additive future growth.
 * Cache-line boundaries occur at slot 8/16/24/32 (each line = 8 slots × 8 B).
 * Domain boundaries at 32/64/96/128 (each domain = 4 lines × 8 slots).
 *
 * Naming: every NEW slot gets a "// rate: <events/sec>" comment so future
 * maintainers can re-evaluate the budget.  Slots inherited from v1 are
 * marked "// (v1)" — their rate justification is in the v1 file.
 */
enum sense_idx {

    /* ── Domain 0 — Network (slots 0-31, 15 used, 17 reserved) ──────── */

    /* Cache line 0 (bytes 0-63) */
    NET_TCP_ESTABLISHED          =  0, /* (v1) */
    NET_TCP_LISTEN               =  1, /* (v1) */
    NET_TCP_TIME_WAIT            =  2, /* (v1) */
    NET_TCP_CLOSE_WAIT           =  3, /* (v1) */
    NET_TCP_OTHER                =  4, /* (v1) */
    NET_UDP_ACTIVE               =  5, /* (v1) */
    NET_UNIX_ACTIVE              =  6, /* (v1) */
    NET_TX_BYTES                 =  7, /* (v1) */

    /* Cache line 1 (bytes 64-127) */
    NET_RX_BYTES                 =  8, /* (v1) */
    TCP_RETRANSMIT_COUNT         =  9, /* (v1) — rate: 100-10K/sec */
    TCP_RST_SENT                 = 10, /* (v1) */
    TCP_ERROR_COUNT              = 11, /* (v1) */
    SKB_DROP_COUNT               = 12, /* (v1) — rate: 1K-100K/sec aggregate;
                                        * is_target() gate keeps per-tenant
                                        * rate << 1K */
    TCP_CONG_LOSS                = 13, /* (v1) */
    MPTCP_SUBFLOW_ESTABLISHED    = 14, /* NEW — rate: <0.15 µs/sec
                                        * (very rare) */
    _NET_RESERVED_15             = 15,

    /* Cache lines 2-3 (slots 16-31) — NETWORK reserved */
    _NET_RESERVED_16             = 16, _NET_RESERVED_17 = 17,
    _NET_RESERVED_18             = 18, _NET_RESERVED_19 = 19,
    _NET_RESERVED_20             = 20, _NET_RESERVED_21 = 21,
    _NET_RESERVED_22             = 22, _NET_RESERVED_23 = 23,
    _NET_RESERVED_24             = 24, _NET_RESERVED_25 = 25,
    _NET_RESERVED_26             = 26, _NET_RESERVED_27 = 27,
    _NET_RESERVED_28             = 28, _NET_RESERVED_29 = 29,
    _NET_RESERVED_30             = 30, _NET_RESERVED_31 = 31,

    /* ── Domain 1 — I/O + Storage (slots 32-63, 22 used, 10 reserved) ─ */

    /* Cache line 4 */
    FD_OPEN_OPS                  = 32, /* (v1) */
    IO_READ_BYTES                = 33, /* (v1) */
    IO_WRITE_BYTES               = 34, /* (v1) */
    IO_READ_OPS                  = 35, /* (v1) */
    IO_WRITE_OPS                 = 36, /* (v1) */
    DISK_READ_BYTES              = 37, /* (v1) */
    DISK_WRITE_BYTES             = 38, /* (v1) */
    DISK_IO_LATENCY_NS           = 39, /* (v1) */

    /* Cache line 5 */
    DISK_IO_COUNT                = 40, /* (v1) */
    PAGE_CACHE_MISSES            = 41, /* (v1) */
    PAGE_CACHE_EVICTIONS         = 42, /* NEW — rate: 100-1K/sec aggregate
                                        * (filemap eviction events; typical
                                        * IO-mostly workload) */
    READAHEAD_PAGES              = 43, /* (v1) */
    WRITE_THROTTLE_JIFFIES       = 44, /* (v1) */
    IO_UNPLUG_COUNT              = 45, /* (v1) */
    IOCOST_IDLE_COUNT            = 46, /* NEW — rate: <100/sec
                                        * (cgroup iocg went idle —
                                        * proxy for "throttled hard
                                        * enough that the IO source
                                        * gave up").  Kernel 6.17
                                        * iocost tracepoints don't
                                        * expose a direct "throttle"
                                        * event; iocg_idle/iocg_activate
                                        * pair captures the under-
                                        * pressure transitions. */
    IOCOST_ACTIVATE_COUNT        = 47, /* NEW — paired counterpart;
                                        * delta IOCOST_IDLE - ACTIVATE
                                        * across a window estimates
                                        * "currently-throttled cgroups". */

    /* Cache line 6 */
    WBT_DELAY_COUNT              = 48, /* NEW — rate: <100/sec
                                        * (writeback throttle; rare on
                                        * healthy hosts) */
    WBT_DELAY_NS                 = 49, /* NEW — paired */
    FILELOCK_WAITS               = 50, /* NEW — rate: <100/sec on most
                                        * workloads (NFS / shared-FS
                                        * heavier) */
    FILELOCK_NS                  = 51, /* NEW — paired */
    SWIOTLB_BOUNCE_COUNT         = 52, /* NEW — rate: <1K/sec
                                        * (only fires when DMA mask
                                        * doesn't cover phys addr;
                                        * critical signal for non-IOMMU
                                        * GPU/NIC traffic) */
    SWIOTLB_BOUNCE_BYTES         = 53, /* NEW — paired */
    _STORAGE_RESERVED_54         = 54,
    _STORAGE_RESERVED_55         = 55,

    /* Cache line 7 — STORAGE reserved */
    _STORAGE_RESERVED_56         = 56, _STORAGE_RESERVED_57 = 57,
    _STORAGE_RESERVED_58         = 58, _STORAGE_RESERVED_59 = 59,
    _STORAGE_RESERVED_60         = 60, _STORAGE_RESERVED_61 = 61,
    _STORAGE_RESERVED_62         = 62, _STORAGE_RESERVED_63 = 63,

    /* ── Domain 2 — Memory (slots 64-95, 28 used, 4 reserved) ───────── */

    /* Cache line 8 */
    MEM_MMAP_COUNT               = 64, /* (v1) */
    MEM_MUNMAP_COUNT             = 65, /* (v1) */
    MEM_PAGE_FAULTS_MIN          = 66, /* (v1) */
    MEM_PAGE_FAULTS_MAJ          = 67, /* (v1) */
    MEM_BRK_CALLS                = 68, /* (v1) */
    RSS_ANON_BYTES               = 69, /* (v1) */
    RSS_FILE_BYTES               = 70, /* (v1) */
    RSS_SWAP_ENTRIES             = 71, /* (v1) */

    /* Cache line 9 */
    RSS_SHMEM_BYTES              = 72, /* (v1) */
    DIRECT_RECLAIM_COUNT         = 73, /* (v1) */
    DIRECT_RECLAIM_NS            = 74, /* (v1) */
    SWAP_OUT_PAGES               = 75, /* (v1) */
    THP_COLLAPSE_OK              = 76, /* (v1) */
    THP_COLLAPSE_FAIL            = 77, /* (v1) */
    _MEM_RESERVED_78             = 78, /* WAS: THP_SPLIT_COUNT — kernel
                                        * 6.17 has no `huge_memory/
                                        * mm_split_huge_page` tracepoint;
                                        * THP split count is read at
                                        * snapshot time from /proc/vmstat
                                        * (`thp_split_*` lines) via
                                        * ProcGauges → Gauge::THP_SPLIT */
    NUMA_MIGRATE_PAGES           = 79, /* (v1) — total */

    /* Cache line 10 */
    NUMA_MIG_NUMA_HINT           = 80, /* NEW — discriminate rate:
                                        * 100-1K/sec; subset of total */
    NUMA_MIG_OTHER               = 81, /* NEW — paired */
    COMPACTION_STALLS            = 82, /* (v1) */
    EXTFRAG_EVENTS               = 83, /* (v1) */
    KSWAPD_WAKES                 = 84, /* NEW — rate: <100/sec (memory
                                        * pressure signal; complements
                                        * existing DIRECT_RECLAIM_*) */
    MMAP_LOCK_WAITS              = 85, /* NEW — rate: <1K/sec (only fires
                                        * on contention; kernel 5.16+
                                        * tracepoint) */
    MMAP_LOCK_NS                 = 86, /* NEW — paired */
    IOMMU_FAULTS                 = 87, /* NEW — rate: ~0 on healthy host
                                        * (>0 == serious bug) */

    /* Cache line 11 */
    TLB_SHOOTDOWNS               = 88, /* NEW — rate: 100-1K/sec
                                        * (cross-CPU TLB invalidates
                                        * stolen from us by other tenants) */
    VMSCAN_LRU_ISOLATIONS        = 89, /* NEW — rate: <1K/sec; gated on
                                        * is_target() to keep per-tenant
                                        * rate < 100 */
    VMSCAN_SCAN_NS               = 90, /* NEW — paired */
    RECLAIM_STALL_LOOPS          = 91, /* (v1) */
    _MEM_RESERVED_92             = 92,
    _MEM_RESERVED_93             = 93,
    _MEM_RESERVED_94             = 94,
    _MEM_RESERVED_95             = 95,

    /* ── Domain 3 — Sched + Sync + Reliability (slots 96-127,
     *               32 used, 0 reserved) ──────────────────────────────
     *
     * SATURATED — no headroom for additive growth.  When a new
     * Domain-3 counter is needed, either (a) bump Domain 0/1 reserved
     * slots into Domain 3 (renumber + bench-harness sweep), or
     * (b) bump CRUCIBLE_SENSE_HUB_EXTENDED slots 224-255.  See L1
     * audit finding for context. */

    /* Cache line 12 */
    SCHED_CTX_VOL                =  96, /* (v1) */
    SCHED_CTX_INVOL              =  97, /* (v1) */
    SCHED_MIGRATIONS             =  98, /* (v1) */
    SCHED_RUNTIME_NS             =  99, /* (v1) */
    SCHED_WAIT_NS                = 100, /* (v1) */
    SCHED_SLEEP_NS               = 101, /* (v1) */
    SCHED_IOWAIT_NS              = 102, /* (v1) */
    SCHED_BLOCKED_NS             = 103, /* (v1) */

    /* Cache line 13 */
    WAKEUPS_RECEIVED             = 104, /* (v1) */
    WAKEUPS_SENT                 = 105, /* (v1) */
    KERNEL_LOCK_COUNT            = 106, /* (v1) */
    KERNEL_LOCK_NS               = 107, /* (v1) */
    FUTEX_WAIT_COUNT             = 108, /* (v1) */
    FUTEX_WAIT_NS                = 109, /* (v1) */
    THREADS_CREATED              = 110, /* (v1) */
    THREADS_EXITED               = 111, /* (v1) */

    /* Cache line 14 */
    CPU_FREQ_CHANGES             = 112, /* (v1) */
    PROCESS_FORKS                = 113, /* NEW — rate: 10-100/sec */
    PROCESS_EXECS                = 114, /* NEW — rate: 10-100/sec
                                         * (should be 0 in steady-state
                                         * compute) */
    SIGNAL_DELIVERED             = 115, /* NEW — rate: 10-100/sec
                                         * (complements existing
                                         * SIGNAL_FATAL_COUNT and the
                                         * SIGNAL_LAST_SIGNO gauge) */
    SIGNAL_FATAL_COUNT           = 116, /* (v1) */
    OOM_KILLS_SYSTEM             = 117, /* (v1) */
    OOM_KILL_US                  = 118, /* (v1) */
    MCE_COUNT                    = 119, /* (v1) */

    /* Cache line 15 */
    NMI_COUNT                    = 120, /* NEW — rate: <1K/sec (PMU sample
                                         * IRQ + watchdog NMIs).
                                         * NMI-context-safe: only touches
                                         * array map via fetch_and_add. */
    OSNOISE_NS_TOTAL             = 121, /* NEW — rate: 0 unless
                                         * CONFIG_OSNOISE_TRACER on (kernel
                                         * 5.14+); otherwise stays 0 */
    CSD_QUEUE_COUNT              = 122, /* NEW — rate: 100-10K/sec
                                         * aggregate; gates is_target() */
    PCIE_AER_CORR                = 123, /* NEW — rate: 0-1/sec (extreme
                                         * rare; high rate predicts UE) */
    PCIE_AER_UNCORR              = 124, /* NEW — rate: 0-rare (any non-zero
                                         * is alert-worthy) */
    EDAC_DRAM_CE                 = 125, /* NEW — rate: rare */
    SOFTIRQ_STOLEN_NS            = 126, /* (v1) */
    MAP_FULL_DROPS               = 127, /* (v1) — self-observability:
                                         * BPF map_update_elem returned
                                         * non-zero (LRU eviction failure,
                                         * NOEXIST collision, ENOMEM) */

    /* ═══════════════════════════════════════════════════════════════════
     * ── EXTENDED SLOTS (128-255) — only present when
     *    CRUCIBLE_SENSE_HUB_EXTENDED is defined.
     *
     * Higher-rate signals that warrant 1-2 % CPU overhead for
     * bench/oncall observability.  Most extended handlers use
     * BPF_MAP_TYPE_PERCPU_ARRAY for the inflight tracking maps to
     * avoid cross-CPU MESI ping-pong on the increment.
     * ═══════════════════════════════════════════════════════════════════ */

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED

    /* ── Extended Domain 4 — Network detail (slots 128-159) ─────────── */

    /* skb_drop_reason rebucket — 9 buckets of skb/kfree_skb's
     * `reason` field (kernel 5.18+).  Each bucket is one slot.  Total
     * skb drop count remains in slot 12 (SKB_DROP_COUNT). */
    SKB_DROP_NEIGH_FAILED        = 128, /* rate: variable; high on
                                          * misconfigured fleets */
    SKB_DROP_NETFILTER_DROP      = 129, /* rate: variable */
    SKB_DROP_TCP_INVALID         = 130, /* rate: variable */
    SKB_DROP_TCP_RESET           = 131, /* rate: variable */
    SKB_DROP_TCP_OFOMERGE        = 132, /* rate: variable */
    SKB_DROP_PROTO_MEM           = 133, /* rate: rare; CRITICAL
                                          * (TCP memory exhaustion) */
    SKB_DROP_NO_SOCKET           = 134, /* rate: variable; can spike */
    SKB_DROP_RX_NO_NETDEV        = 135, /* rate: rare */
    SKB_DROP_OTHER               = 136, /* rate: residual */

    /* Cache line 17 */
    NF_CONNTRACK_NEW             = 137, /* rate: 1K-10K/sec on busy
                                          * federation NAT */
    NF_CONNTRACK_DESTROY         = 138, /* rate: paired with NEW */
    NF_CONNTRACK_DROPS           = 139, /* rate: rare; high == table full */
    NETIF_RECV_COUNT             = 140, /* rate: 10K-100K/sec; PERCPU map
                                          * for the inflight side */
    NAPI_POLL_COUNT              = 141, /* rate: 10K-100K/sec; PERCPU */
    NAPI_RESCHED_COUNT           = 142, /* rate: 1K-10K/sec */
    NAPI_BUDGET_EXHAUSTED        = 143, /* rate: 100-10K/sec
                                          * (budget exhaustion ==
                                          * NIC RX overload) */
    SOCK_OPS_RTT_SAMPLE_COUNT    = 144, /* rate: 100-1K/sec per active
                                          * connection */

    /* Cache line 18 */
    TCP_RECV_QUEUE_FULL          = 145,
    TCP_FAST_RETRANSMIT          = 146,
    TCP_TLP_FIRES                = 147,
    TCP_LOSS_PROBE_COUNT         = 148,
    TCP_DSACK_COUNT              = 149,
    UDP_PROTO_MEM_ERRORS         = 150,
    SOCKMAP_REDIRECT_COUNT       = 151,
    QDISC_OVERLIMITS             = 152,

    /* Cache line 19 — NETWORK_EXT reserved */
    _NETWORK_EXT_RESERVED_153    = 153, _NETWORK_EXT_RESERVED_154 = 154,
    _NETWORK_EXT_RESERVED_155    = 155, _NETWORK_EXT_RESERVED_156 = 156,
    _NETWORK_EXT_RESERVED_157    = 157, _NETWORK_EXT_RESERVED_158 = 158,
    _NETWORK_EXT_RESERVED_159    = 159,

    /* ── Extended Domain 5 — Storage detail (slots 160-191) ─────────── */

    BLOCK_INSERT_COUNT           = 160, /* rate: 10K/sec NVMe */
    BLOCK_ISSUE_COUNT            = 161,
    BLOCK_COMPLETE_COUNT         = 162,
    BLOCK_BACKMERGE_COUNT        = 163,
    BLOCK_FRONTMERGE_COUNT       = 164,
    IOURING_SUBMIT_COUNT         = 165, /* rate: 10K-100K/sec on async
                                          * I/O hosts; PERCPU map */
    IOURING_COMPLETE_COUNT       = 166,
    IOURING_SQ_FULL_COUNT        = 167, /* rate: rare unless overloaded */

    NVME_RQ_SETUP_COUNT          = 168,
    NVME_RQ_COMPLETE_COUNT       = 169,
    NVME_QUEUE_DEPTH_GAUGE_VALID_AT_GAUGE = 170, /* placeholder; use gauge
                                                  * slot for actual value */
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

    /* Cache line 23 — STORAGE_EXT reserved */
    _STORAGE_EXT_RESERVED_184    = 184, _STORAGE_EXT_RESERVED_185 = 185,
    _STORAGE_EXT_RESERVED_186    = 186, _STORAGE_EXT_RESERVED_187 = 187,
    _STORAGE_EXT_RESERVED_188    = 188, _STORAGE_EXT_RESERVED_189 = 189,
    _STORAGE_EXT_RESERVED_190    = 190, _STORAGE_EXT_RESERVED_191 = 191,

    /* ── Extended Domain 6 — Memory + cgroup detail (slots 192-223) ── */

    PAGE_ALLOC_NORMAL            = 192,
    PAGE_ALLOC_DMA32             = 193,
    PAGE_ALLOC_MOVABLE           = 194,
    PAGE_ALLOC_RETRY             = 195,
    PAGE_ALLOC_OOM               = 196,
    /* Slots 197-198 RESERVED — earlier draft had SLAB_ALLOC_TOTAL +
     * SLAB_FREE_TOTAL; rate 1M-10M/sec aggregate would be 5-15% CPU
     * even with PERCPU.  Use Gauge::SLAB_TOTAL_BYTES (read from
     * /proc/slabinfo at snapshot time) instead — zero BPF cost. */
    _MEMORY_EXT_RESERVED_197     = 197,
    _MEMORY_EXT_RESERVED_198     = 198,
    KMEMLEAK_OBJECTS_TRACKED     = 199, /* rate: only if CONFIG_DEBUG_KMEMLEAK */

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

    /* Cache line 27 — MEMORY_EXT reserved */
    _MEMORY_EXT_RESERVED_216     = 216, _MEMORY_EXT_RESERVED_217 = 217,
    _MEMORY_EXT_RESERVED_218     = 218, _MEMORY_EXT_RESERVED_219 = 219,
    _MEMORY_EXT_RESERVED_220     = 220, _MEMORY_EXT_RESERVED_221 = 221,
    _MEMORY_EXT_RESERVED_222     = 222, _MEMORY_EXT_RESERVED_223 = 223,

    /* ── Extended Domain 7 — Sched + Sync + Reliability detail
     *                        (slots 224-255) ─────────────────────────── */

    /* IPI per-reason buckets */
    IPI_RESCHEDULE               = 224,
    IPI_CALL_FUNCTION            = 225,
    IPI_NMI_DELIVERED            = 226,
    IPI_CPU_STOP                 = 227,
    IPI_REBOOT                   = 228,
    IPI_THERMAL                  = 229,
    IPI_OTHER                    = 230,
    IPI_TOTAL_LATENCY_NS         = 231, /* total queue→handler latency */

    /* Sched extras */
    SCHED_WAKEUP_LATENCY_TOTAL   = 232, /* rate: high; PERCPU mandatory */
    CONTEXT_TRACKING_USER_ENTER  = 233, /* rate: 1 per syscall;
                                          * PERCPU mandatory */
    CONTEXT_TRACKING_USER_EXIT   = 234, /* paired */
    CPU_ONLINE_TRANSITIONS       = 235, /* rate: rare */
    CPU_OFFLINE_TRANSITIONS      = 236, /* rate: rare */
    SCHED_EXT_DECISIONS          = 237, /* rate: 1 per ctx switch under
                                          * sched_ext */
    PREEMPT_DISABLE_NS_TOTAL     = 238, /* rate: high; PERCPU */
    IRQ_DISABLE_NS_TOTAL         = 239, /* paired */

    /* Lock detail */
    RWSEM_WAIT_COUNT             = 240,
    RWSEM_WAIT_NS                = 241,
    RTMUTEX_WAIT_COUNT           = 242,
    RTMUTEX_WAIT_NS              = 243,

    /* Reliability + system extras.
     * Slots 244-245 RESERVED — earlier draft had HARDIRQ_TOTAL_COUNT +
     * _NS; rate 100K-1M/sec is too expensive even with PERCPU.  Use
     * Gauge::HARDIRQ_TOTAL_COUNT (sourced from /proc/interrupts at
     * snapshot time, kernel already aggregates per-CPU per-IRQ).
     * For per-IRQ-vector latency drilldown use the standalone
     * `hardirq.bpf.c` planned facade. */
    _RELIABILITY_EXT_RESERVED_244 = 244,
    _RELIABILITY_EXT_RESERVED_245 = 245,
    PRINTK_LINE_COUNT            = 246, /* rate: 0 typical, 1K/sec spikes
                                          * on errors */
    AVC_DENIALS                  = 247, /* rate: rare */
    CAPABILITY_FAILS             = 248, /* rate: rare */
    KVM_VMEXIT_COUNT             = 249, /* rate: VM-only; varies */
    RESCTRL_BREACHES             = 250, /* rate: rare */
    BPF_PROG_VERIFIER_FAILS      = 251, /* rate: 0 in steady state */

    /* Less critical */
    MODULE_LOAD_COUNT            = 252,
    MODULE_UNLOAD_COUNT          = 253,
    ALARMTIMER_FIRES             = 254,
    ACPI_GPE_FIRES               = 255,

#endif /* CRUCIBLE_SENSE_HUB_EXTENDED */

    /* ── Sentinel ──────────────────────────────────────────────────── */
    SENSE_HUB_NUM_COUNTERS_SENTINEL = SENSE_HUB_NUM_COUNTERS,
};

/* ═══════════════════════════════════════════════════════════════════════
 * ──── enum sense_gauge — basic 32-slot gauge allocation ───────────────
 * ═══════════════════════════════════════════════════════════════════════
 *
 * Gauges are point-in-time values OR running max.  NEVER subtractable.
 * `Snapshot::operator-` does NOT operate on these.
 *
 * Userspace-sampled gauges are populated by ProcGauges at snapshot time
 * (no BPF cost); BPF-side gauges are written by handlers via
 * __sync_val_compare_and_swap (for max) or __atomic_store_n (for
 * point-in-time).
 */
enum sense_gauge {

    /* ── Mis-classified-from-v1 (slots 0-7) ─────────────────────────── */

    GAUGE_FD_CURRENT             = 0, /* (v1) — point-in-time */
    GAUGE_TCP_MIN_SRTT_US        = 1, /* (v1) — running min */
    GAUGE_TCP_MAX_SRTT_US        = 2, /* (v1) — running max */
    GAUGE_TCP_LAST_CWND          = 3, /* (v1) — last value */
    GAUGE_THERMAL_MAX_TRIP       = 4, /* (v1) — running max */
    GAUGE_SIGNAL_LAST_SIGNO      = 5, /* (v1) — last value */
    _GAUGE_MISCLASS_RESERVED_6   = 6,
    _GAUGE_MISCLASS_RESERVED_7   = 7,

    /* ── BPF-side max-watermarks (slots 8-15) ───────────────────────── */

    GAUGE_CSD_MAX_QUEUE_TO_START_NS = 8,  /* NEW */
    GAUGE_OSNOISE_MAX_NS            = 9,  /* NEW */
    GAUGE_NMI_HANDLER_MAX_NS        = 10, /* NEW */
    GAUGE_MMAP_LOCK_MAX_WAIT_NS     = 11, /* NEW */
    _GAUGE_MAX_RESERVED_12          = 12,
    _GAUGE_MAX_RESERVED_13          = 13,
    _GAUGE_MAX_RESERVED_14          = 14,
    _GAUGE_MAX_RESERVED_15          = 15,

    /* ── Userspace-sampled at snapshot time (slots 16-31) ──────────────
     * These slots are written by `crucible::perf::ProcGauges::populate()`
     * at userspace snapshot time.  Zero BPF cost. */

    GAUGE_SLAB_TOTAL_BYTES          = 16, /* /proc/slabinfo */
    GAUGE_HARDIRQ_TOTAL_COUNT       = 17, /* /proc/interrupts */
    GAUGE_NAPI_POLL_TOTAL           = 18, /* /proc/net/softnet_stat */
    GAUGE_SKB_DROP_REASON_TOTAL     = 19, /* /proc/net/snmp */
    GAUGE_TCP_RECV_BUFFER_MAX       = 20, /* /proc/net/tcp */
    GAUGE_BLOCK_QUEUE_DEPTH_MAX     = 21, /* /sys/block/<dev>/stat */
    GAUGE_PRINTK_RING_BYTES_FREE    = 22, /* /sys/kernel/debug optional */
    _GAUGE_PROC_RESERVED_23         = 23,
    _GAUGE_PROC_RESERVED_24         = 24,
    _GAUGE_PROC_RESERVED_25         = 25,
    _GAUGE_PROC_RESERVED_26         = 26,
    _GAUGE_PROC_RESERVED_27         = 27,
    _GAUGE_PROC_RESERVED_28         = 28,
    _GAUGE_PROC_RESERVED_29         = 29,
    _GAUGE_PROC_RESERVED_30         = 30,
    _GAUGE_PROC_RESERVED_31         = 31,

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED

    /* ── Extended gauges (slots 32-63) — PMU + extras ───────────────── */

    /* PMU ratios projected from PmuSample sample ring at read time
     * (NOT BPF tracepoint counters) */
    GAUGE_PMU_IPC_X1000             = 32,
    GAUGE_PMU_FRONTEND_STALL_PCT_X100 = 33,
    GAUGE_PMU_BACKEND_STALL_PCT_X100  = 34,
    GAUGE_PMU_BAD_SPEC_PCT_X100       = 35,
    GAUGE_PMU_RETIRING_PCT_X100       = 36,
    GAUGE_PMU_LLC_MISS_RATE_X100      = 37,
    GAUGE_PMU_BR_MISS_PER_KINST       = 38,
    GAUGE_PMU_RAPL_PKG_JOULES_X1000   = 39,
    GAUGE_PMU_RAPL_CORES_JOULES_X1000 = 40,
    GAUGE_PMU_RAPL_DRAM_JOULES_X1000  = 41,

    /* Extra max-watermarks (BPF-side) */
    GAUGE_WAKEUP_LATENCY_MAX_NS     = 42,
    GAUGE_BLOCK_MAX_LATENCY_NS      = 43,
    GAUGE_PREEMPT_DISABLE_MAX_NS    = 44,
    GAUGE_IRQ_DISABLE_MAX_NS        = 45,
    GAUGE_GP_MAX_NS                 = 46, /* RCU GP max */
    GAUGE_RQ_DEPTH_MAX              = 47,
    GAUGE_WQ_DEPTH_MAX              = 48,

    /* Extra userspace-sampled */
    GAUGE_NUMA_HIT_RATIO_X100       = 49, /* /proc/vmstat numa_hit/miss */
    GAUGE_TCP_ESTABLISHED_CURRENT   = 50, /* /proc/net/snmp */
    GAUGE_LOAD_AVG_1M_X100          = 51, /* /proc/loadavg */
    GAUGE_LOAD_AVG_5M_X100          = 52,
    GAUGE_LOAD_AVG_15M_X100         = 53,
    GAUGE_PSI_CPU_SOME_AVG10_X100   = 54, /* /proc/pressure/cpu */
    GAUGE_PSI_MEM_SOME_AVG10_X100   = 55, /* /proc/pressure/memory */
    GAUGE_PSI_IO_SOME_AVG10_X100    = 56, /* /proc/pressure/io */

    _GAUGE_EXT_RESERVED_57          = 57, _GAUGE_EXT_RESERVED_58 = 58,
    _GAUGE_EXT_RESERVED_59          = 59, _GAUGE_EXT_RESERVED_60 = 60,
    _GAUGE_EXT_RESERVED_61          = 61, _GAUGE_EXT_RESERVED_62 = 62,
    _GAUGE_EXT_RESERVED_63          = 63,

#endif /* CRUCIBLE_SENSE_HUB_EXTENDED */

    SENSE_HUB_NUM_GAUGES_SENTINEL   = SENSE_HUB_NUM_GAUGES,
};

/* ═══════════════════════════════════════════════════════════════════════
 * ──── struct sense_meta — wire-contract sentinel ──────────────────────
 * ═══════════════════════════════════════════════════════════════════════
 *
 * One-cache-line struct in its own ARRAY map.  Userspace reads this
 * BEFORE any other map and refuses load() on mismatch.  Catches:
 *   • basic-build userspace running against extended-build kernel
 *     (or vice-versa)
 *   • slot reorder between v2 minor versions
 *   • wholly-incompatible v1 ↔ v2 ABI mix
 */
struct sense_meta {
    __u32 magic;          /* SENSE_HUB_MAGIC = 'CRBL'      — offset 0  */
    __u32 version;        /* SENSE_HUB_VERSION = 2          — offset 4  */
    __u32 num_counters;   /* SENSE_HUB_NUM_COUNTERS         — offset 8  */
    __u32 num_gauges;     /* SENSE_HUB_NUM_GAUGES           — offset 12 */
    __u64 layout_hash;    /* SENSE_HUB_LAYOUT_HASH          — offset 16 */
    __u32 build_tag;      /* SENSE_HUB_BUILD_TAG            — offset 24 */
    __u8  _pad[36];       /* tail padding                   — offset 28..63
                           * total = 4+4+4+4+8+4+36 = 64 B = one cache line */
};

/* ─── Maps ──────────────────────────────────────────────────────────── */

/* Counters — shared mmap'd array, monotone subtractable */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, SENSE_HUB_NUM_COUNTERS);
    __type(key, __u32);
    __type(value, __u64);
    __uint(map_flags, BPF_F_MMAPABLE);
} counters SEC(".maps");

/* Gauges — shared mmap'd array, point-in-time + max.  Read with
 * __atomic_load_n; never subtracted. */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, SENSE_HUB_NUM_GAUGES);
    __type(key, __u32);
    __type(value, __u64);
    __uint(map_flags, BPF_F_MMAPABLE);
} gauges SEC(".maps");

/* Meta — single-cache-line wire-contract sentinel.  Pre-populated in
 * an init-only program; never touched on hot path. */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct sense_meta);
    __uint(map_flags, BPF_F_MMAPABLE);
} meta SEC(".maps");

/* Inflight tracking maps (LRU_HASH per audit-1 discipline).
 * Same set as v1; orphan entries auto-evict on capacity pressure. */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 4096);
    __type(key, __u32);
    __type(value, __u8);
} our_tids SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, __u64);
} futex_ts SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, __u64);
} mmap_lock_ts SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, __u64);
} csd_inflight SEC(".maps");

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
/* Extended-only inflight maps — PERCPU where event rate demands it */

/* Per-CPU sched_wakeup tracking: tid → wake_ts.  PERCPU avoids
 * cross-CPU contention on the LRU root. */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
    __uint(max_entries, 4096);
    __type(key, __u32);
    __type(value, __u64);
} wake_ts_pcpu SEC(".maps");

/* Per-CPU block_rq tracking: req ptr → setup_ts. */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
    __uint(max_entries, 4096);
    __type(key, __u64);
    __type(value, __u64);
} block_rq_pcpu SEC(".maps");

/* Per-CPU napi_poll inflight (napi instance → poll_start_ts) */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
    __uint(max_entries, 256);
    __type(key, __u64);
    __type(value, __u64);
} napi_inflight_pcpu SEC(".maps");

/* Per-CPU io_uring inflight */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
    __uint(max_entries, 4096);
    __type(key, __u64);
    __type(value, __u64);
} iouring_inflight_pcpu SEC(".maps");
#endif /* CRUCIBLE_SENSE_HUB_EXTENDED */

/* ─── Helpers ───────────────────────────────────────────────────────── */
//
// get_tid() and is_target() are provided by include/crucible/perf/bpf/
// common.h (shared with the v1 SenseHub + SchedSwitch + LockContention +
// SyscallLatency facades).  Don't redefine them here — the linker would
// reject the BPF object.

static __always_inline void counter_add(__u32 idx, __u64 delta)
{
    __u64 *v = bpf_map_lookup_elem(&counters, &idx);
    if (v)
        __sync_fetch_and_add(v, delta);
    else {
        __u32 mfd = MAP_FULL_DROPS;
        __u64 *m = bpf_map_lookup_elem(&counters, &mfd);
        if (m)
            __sync_fetch_and_add(m, 1);
    }
}

static __always_inline void gauge_set(__u32 idx, __u64 value)
{
    __u64 *v = bpf_map_lookup_elem(&gauges, &idx);
    if (v)
        __atomic_store_n(v, value, __ATOMIC_RELAXED);
}

static __always_inline void gauge_max(__u32 idx, __u64 value)
{
    __u64 *v = bpf_map_lookup_elem(&gauges, &idx);
    if (!v)
        return;

    /* atomic_fetch_max via CAS retry — the BPF verifier's bounded
     * loop pragma keeps this tractable */
    __u64 old, cur;
#pragma unroll
    for (int i = 0; i < 4; i++) {
        cur = __atomic_load_n(v, __ATOMIC_RELAXED);
        if (value <= cur)
            return;
        old = __sync_val_compare_and_swap(v, cur, value);
        if (old == cur)
            return;
    }
}

/* ═══════════════════════════════════════════════════════════════════════
 * ──── Meta-map initialization — done from USERSPACE, not here ─────────
 * ═══════════════════════════════════════════════════════════════════════
 *
 * The earlier draft used a tracepoint handler attached to
 * `syscalls/sys_enter_setpriority` to populate `meta` lazily.  Three
 * problems with that approach:
 *
 *   1. The handler fires on EVERY setpriority(2) call from EVERY
 *      process system-wide — a multi-tenant information-leak surface.
 *   2. If no setpriority caller exists, `meta` stays zero and
 *      userspace SenseHubV2::load() fails on `magic != 'CRBL'`.
 *   3. Adds an unnecessary attach + extra BPF program slot.
 *
 * v2 design: SenseHubV2::load() calls
 *
 *     bpf_map_update_elem(meta_fd, &zero, &meta_struct, BPF_ANY);
 *
 * directly via libbpf, AFTER bpf_object__load() succeeds and BEFORE
 * any read of the counters/gauges map.  The struct values come from
 * the SenseHubV2.h-side mirror constants (SENSE_HUB_VERSION /
 * SENSE_HUB_LAYOUT_HASH / etc.) — guaranteed equal to the BPF-side
 * #define-resolved constants because both TUs were compiled with the
 * same `CRUCIBLE_SENSE_HUB_EXTENDED` flag value.  Mismatches would
 * indicate a build-system bug, not a runtime race.  See SenseHubV2.h
 * SenseHubV2::load() for the exact call site. */

/* ═══════════════════════════════════════════════════════════════════════
 * ──── EXISTING v1 HANDLERS — carried forward with audit-1 fixes ───────
 *
 * The 59 SEC programs from sense_hub.bpf.c v1 are preserved here with:
 *   • All inflight HASH maps promoted to LRU_HASH
 *   • Single bpf_ktime_get_ns() per event (was 2-3 in some handlers)
 *   • Compiler barriers before any field-publication store
 *   • Mis-classified slots redirected to gauges via gauge_set/gauge_max
 *
 * For DRAFT brevity we omit the full bodies — they follow the v1
 * pattern with the discipline above applied uniformly.  See:
 *   include/crucible/perf/bpf/sense_hub.bpf.c (v1) for the source
 *   and the GAPS-004b-AUDIT / GAPS-004d-AUDIT / GAPS-004e diffs for
 *   the discipline pattern.
 *
 * Indicative example — v1 sched_switch retrofitted:
 * ═══════════════════════════════════════════════════════════════════════ */

SEC("tracepoint/sched/sched_switch")
int sense_sched_switch(void *ctx)
{
    /* No is_target() gate — sched_switch fires SYSTEM-WIDE; it is the
     * canonical "this CPU got rescheduled" signal regardless of tenant.
     * This is the single most expensive program in v1 (~0.6% CPU on
     * a 96-core box).  v2 KEEPS it in basic — alternative is moving
     * to standalone facade SchedSwitch.h which already exists. */
    counter_add(SCHED_CTX_INVOL, 1);
    /* TODO: full v1 body w/ pid/migration/runtime accumulation, with
     * single ktime_get_ns + barrier discipline applied. */
    return 0;
}

/* TODO: carry forward v1 handlers
 *   sense_inet_sock_set_state, sense_socket_enter, sense_socket_exit,
 *   sense_sendto_exit, sense_sendmsg_exit, sense_recvfrom_exit,
 *   sense_recvmsg_exit, sense_read_exit, sense_readv_exit,
 *   sense_write_exit, sense_writev_exit, sense_openat_exit,
 *   sense_close_enter, sense_mmap_exit, sense_munmap_enter,
 *   sense_page_fault_user, sense_brk_exit, sense_rss_stat,
 *   sense_vmscan_direct_reclaim_begin/end, sense_vmscan_write_folio,
 *   sense_thp_collapse, sense_migrate_pages, sense_compaction_end,
 *   sense_extfrag, sense_sched_migrate, sense_sched_runtime,
 *   sense_sched_sleep, sense_sched_iowait, sense_sched_blocked,
 *   sense_sched_waking, sense_sched_process_exit,
 *   sense_cpu_frequency, sense_futex_enter, sense_futex_exit,
 *   sense_lock_contention_begin/end, sense_softirq_entry/exit,
 *   sense_clone_exit, sense_clone3_exit, sense_block_io_start/done,
 *   sense_block_unplug, sense_filemap_add, sense_iomap_readahead,
 *   sense_balance_dirty_pages, sense_tcp_retransmit_skb,
 *   sense_tcp_send_reset, sense_inet_sk_error_report,
 *   sense_kfree_skb, sense_tcp_probe, sense_tcp_cong_state_set,
 *   sense_signal_generate, sense_oom_mark_victim,
 *   sense_oom_reclaim_retry_zone, sense_thermal_zone_trip,
 *   sense_mce_record. */

/* ═══════════════════════════════════════════════════════════════════════
 * ──── NEW v2 BASIC HANDLERS — added at slots 14, 42, 46-53,
 *      78-90, 113-115, 120-125 ─────────────────────────────────────────
 * ═══════════════════════════════════════════════════════════════════════ */

SEC("tracepoint/mptcp/mptcp_subflow_get_send")
int sense_mptcp_subflow_send(void *ctx)
{
    if (!is_target())
        return 0;
    counter_add(MPTCP_SUBFLOW_ESTABLISHED, 1);
    return 0;
}

SEC("tracepoint/filemap/mm_filemap_delete_from_page_cache")
int sense_filemap_evict(void *ctx)
{
    if (!is_target())
        return 0;
    counter_add(PAGE_CACHE_EVICTIONS, 1);
    return 0;
}

/* Iocost throttling proxy: kernel 6.17 has no direct "throttle"
 * tracepoint.  Available iocost events: iocost_iocg_idle,
 * iocost_iocg_activate, iocost_iocg_forgive_debt, iocost_inuse_*.
 * The idle/activate pair captures cgroups going under/coming out of
 * heavy IO pressure, which is the load-bearing signal we want.
 * Delta(IDLE - ACTIVATE) over a window ≈ "currently-throttled
 * cgroups".  iocost_iocg_forgive_debt fires when iocost gives up
 * trying to throttle (debt-write-off — extreme pressure indicator). */
SEC("tracepoint/iocost/iocost_iocg_idle")
int sense_iocost_idle(void *ctx)
{
    if (!is_target())
        return 0;
    counter_add(IOCOST_IDLE_COUNT, 1);
    return 0;
}

SEC("tracepoint/iocost/iocost_iocg_activate")
int sense_iocost_activate(void *ctx)
{
    if (!is_target())
        return 0;
    counter_add(IOCOST_ACTIVATE_COUNT, 1);
    return 0;
}

SEC("tracepoint/wbt/wbt_lat")
int sense_wbt_lat(void *ctx)
{
    if (!is_target())
        return 0;
    counter_add(WBT_DELAY_COUNT, 1);
    return 0;
}

SEC("tracepoint/filelock/locks_get_lock_context")
int sense_filelock_wait(void *ctx)
{
    if (!is_target())
        return 0;
    counter_add(FILELOCK_WAITS, 1);
    return 0;
}

SEC("tracepoint/swiotlb/swiotlb_bounced")
int sense_swiotlb_bounced(void *ctx)
{
    if (!is_target())
        return 0;
    counter_add(SWIOTLB_BOUNCE_COUNT, 1);
    /* TODO: read size field from ctx, add to SWIOTLB_BOUNCE_BYTES */
    return 0;
}

/* THP split tracking: kernel 6.17 exposes no `huge_memory/mm_split_huge_page`
 * tracepoint.  Sourced from /proc/vmstat (`thp_split_*` rows) via
 * userspace ProcGauges at snapshot time — see Gauge::THP_SPLIT in
 * SenseHubV2.h.  No SEC handler in this file. */

SEC("tracepoint/migrate/mm_migrate_pages")
int sense_numa_migrate_v2(void *ctx)
{
    if (!is_target())
        return 0;
    /* TODO: branch on reason field — reason=5 (numa_misplaced) goes
     * to NUMA_MIG_NUMA_HINT, others to NUMA_MIG_OTHER */
    counter_add(NUMA_MIG_OTHER, 1);
    return 0;
}

SEC("tracepoint/vmscan/mm_vmscan_kswapd_wake")
int sense_kswapd_wake(void *ctx)
{
    counter_add(KSWAPD_WAKES, 1);  /* system-wide signal */
    return 0;
}

SEC("tracepoint/mmap_lock/mmap_lock_acquire_returned")
int sense_mmap_lock_returned(void *ctx)
{
    if (!is_target())
        return 0;
    /* TODO: record start ts in mmap_lock_ts; on release pair, compute
     * delta, add to MMAP_LOCK_NS, increment MMAP_LOCK_WAITS, update
     * GAUGE_MMAP_LOCK_MAX_WAIT_NS via gauge_max. */
    counter_add(MMAP_LOCK_WAITS, 1);
    return 0;
}

SEC("tracepoint/iommu/io_page_fault")
int sense_iommu_fault(void *ctx)
{
    counter_add(IOMMU_FAULTS, 1);  /* system-wide; rare; alert-worthy */
    return 0;
}

SEC("tracepoint/tlb/tlb_flush")
int sense_tlb_shootdown(void *ctx)
{
    /* No is_target() — TLB shootdowns from OTHER tenants steal time
     * from us, so we want the system-wide count regardless of which
     * task asked for the flush. */
    counter_add(TLB_SHOOTDOWNS, 1);
    return 0;
}

SEC("tracepoint/vmscan/mm_vmscan_lru_isolate")
int sense_vmscan_lru_isolate(void *ctx)
{
    if (!is_target())
        return 0;
    counter_add(VMSCAN_LRU_ISOLATIONS, 1);
    /* TODO: VMSCAN_SCAN_NS via paired tracepoint mm_vmscan_lru_shrink_inactive */
    return 0;
}

SEC("tracepoint/sched/sched_process_fork")
int sense_process_fork(void *ctx)
{
    counter_add(PROCESS_FORKS, 1);
    return 0;
}

SEC("tracepoint/sched/sched_process_exec")
int sense_process_exec(void *ctx)
{
    counter_add(PROCESS_EXECS, 1);
    return 0;
}

SEC("tracepoint/signal/signal_deliver")
int sense_signal_deliver(void *ctx)
{
    if (!is_target())
        return 0;
    counter_add(SIGNAL_DELIVERED, 1);
    return 0;
}

SEC("tracepoint/nmi/nmi_handler")
int sense_nmi(void *ctx)
{
    /* NMI-CONTEXT-SAFE: only touches array map via __sync_fetch_and_add.
     * NEVER add a hash map touch to this handler (LRU eviction is
     * not NMI-safe). */
    __u64 now = bpf_ktime_get_ns();
    counter_add(NMI_COUNT, 1);
    /* TODO: read handler_ns from ctx, add to NMI_HANDLER_NS_TOTAL,
     * gauge_max GAUGE_NMI_HANDLER_MAX_NS. */
    (void)now;
    return 0;
}

SEC("tracepoint/osnoise/osnoise_sample")
int sense_osnoise(void *ctx)
{
    /* OSNOISE event reports {duration, max_thread_id} per sample window.
     * TODO: read `noise` field, add to OSNOISE_NS_TOTAL,
     * gauge_max GAUGE_OSNOISE_MAX_NS. */
    return 0;
}

SEC("tracepoint/csd/csd_queue_cpu")
int sense_csd_queue(void *ctx)
{
    if (!is_target())
        return 0;
    /* TODO: stash queue ts in csd_inflight; on csd_function_entry,
     * compute queue→start latency, add to total, gauge_max
     * GAUGE_CSD_MAX_QUEUE_TO_START_NS. */
    counter_add(CSD_QUEUE_COUNT, 1);
    return 0;
}

SEC("tracepoint/ras/aer_event")
int sense_pcie_aer(void *ctx)
{
    /* TODO: branch on severity field — 2=Corrected → PCIE_AER_CORR,
     * 0/1=Uncorrected → PCIE_AER_UNCORR. */
    counter_add(PCIE_AER_CORR, 1);
    return 0;
}

SEC("tracepoint/ras/mc_event")
int sense_edac_dram_ce(void *ctx)
{
    /* TODO: gate on err_type=corrected (most events) */
    counter_add(EDAC_DRAM_CE, 1);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * ──── EXTENDED HANDLERS — only present under CRUCIBLE_SENSE_HUB_EXTENDED
 * ═══════════════════════════════════════════════════════════════════════ */

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED

/* skb_drop_reason rebucket — write 1 to one of 9 buckets */
SEC("tracepoint/skb/kfree_skb")
int sense_skb_drop_reason(void *ctx)
{
    /* skb/kfree_skb's TP_PROTO has `reason` (skb_drop_reason enum).
     * TODO: read `reason`, switch into one of 9 buckets:
     *   case SKB_DROP_REASON_NEIGH_FAILED:   counter_add(SKB_DROP_NEIGH_FAILED, 1); break;
     *   case SKB_DROP_REASON_NETFILTER_DROP: counter_add(SKB_DROP_NETFILTER_DROP, 1); break;
     *   case SKB_DROP_REASON_TCP_INVALID_SEQUENCE: ... break;
     *   ...
     *   default: counter_add(SKB_DROP_OTHER, 1);
     */
    counter_add(SKB_DROP_OTHER, 1);
    return 0;
}

/* IPI per-reason rebucket */
SEC("tracepoint/ipi/ipi_send_cpu")
int sense_ipi_send_cpu(void *ctx)
{
    /* TODO: arch-specific reason discrimination (x86 via callsite,
     * arm64 via ipi_raise reason field). */
    counter_add(IPI_OTHER, 1);
    return 0;
}

/* Sched wakeup latency aggregation — PERCPU inflight, shared counter */
SEC("tracepoint/sched/sched_waking")
int sense_wake_start(void *ctx)
{
    /* TODO: stash ts_ns in wake_ts_pcpu keyed by tid */
    return 0;
}

SEC("tracepoint/sched/sched_switch")
int sense_wake_finish(void *ctx)
{
    /* TODO: lookup ts in wake_ts_pcpu, compute latency, add to
     * SCHED_WAKEUP_LATENCY_TOTAL, gauge_max GAUGE_WAKEUP_LATENCY_MAX_NS. */
    return 0;
}

/* context_tracking — every kernel↔user transition */
SEC("tracepoint/context_tracking/user_enter")
int sense_user_enter(void *ctx)
{
    counter_add(CONTEXT_TRACKING_USER_ENTER, 1);
    return 0;
}

SEC("tracepoint/context_tracking/user_exit")
int sense_user_exit(void *ctx)
{
    counter_add(CONTEXT_TRACKING_USER_EXIT, 1);
    return 0;
}

/* hardirq aggregation: removed — Idx::HARDIRQ_TOTAL_COUNT was dropped
 * during audit (rate 100K-1M/sec is too expensive even with PERCPU).
 * The signal is sourced from /proc/interrupts at snapshot time via
 * Gauge::HARDIRQ_TOTAL_COUNT (zero BPF cost).  For per-IRQ-vector
 * latency drilldown, use the planned standalone hardirq.bpf.c facade. */

/* io_uring submission/completion */
SEC("tracepoint/io_uring/io_uring_submit_req")
int sense_iouring_submit(void *ctx)
{
    if (!is_target())
        return 0;
    counter_add(IOURING_SUBMIT_COUNT, 1);
    return 0;
}

SEC("tracepoint/io_uring/io_uring_complete")
int sense_iouring_complete(void *ctx)
{
    if (!is_target())
        return 0;
    counter_add(IOURING_COMPLETE_COUNT, 1);
    return 0;
}

/* napi/napi_poll */
SEC("tracepoint/napi/napi_poll")
int sense_napi_poll(void *ctx)
{
    counter_add(NAPI_POLL_COUNT, 1);
    /* TODO: branch on `work` vs `budget` to detect budget exhaustion */
    return 0;
}

/* avc denials — security signal */
SEC("tracepoint/avc/selinux_audited")
int sense_avc_denial(void *ctx)
{
    counter_add(AVC_DENIALS, 1);
    return 0;
}

/* printk surge detection */
SEC("tracepoint/printk/console")
int sense_printk_line(void *ctx)
{
    counter_add(PRINTK_LINE_COUNT, 1);
    return 0;
}

/* TODO carry forward extended handlers:
 *   sense_iouring_sq_full, sense_block_rq_insert/issue/complete/merge,
 *   sense_nvme_setup/complete, sense_scsi_dispatch/complete,
 *   sense_ext4_txn, sense_xfs_txn, sense_btrfs_txn,
 *   sense_writeback_inode, sense_jbd2_commit, sense_xfs_log_force,
 *   sense_fuse_rq, sense_kyber_lat_violation,
 *   sense_page_alloc_per_zone (DMA32/normal/movable),
 *   sense_slab_alloc/free, sense_kmemleak_object,
 *   sense_vmalloc_alloc, sense_hugetlb_fault,
 *   sense_ksm_pages_sharing, sense_damon_aggr,
 *   sense_memcg_high/max/oom, sense_cgroup_freezer,
 *   sense_thp_promote/demote, sense_balloon_inflate/deflate,
 *   sense_rwsem_wait_begin/end, sense_rtmutex_wait_begin/end,
 *   sense_kvm_vmexit, sense_resctrl_breach,
 *   sense_bpf_prog_verifier_fail,
 *   sense_module_load/unload, sense_alarmtimer_fire,
 *   sense_acpi_gpe, sense_capability_fail,
 *   sense_preempt_disable_begin/end, sense_irq_disable_begin/end. */

#endif /* CRUCIBLE_SENSE_HUB_EXTENDED */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
