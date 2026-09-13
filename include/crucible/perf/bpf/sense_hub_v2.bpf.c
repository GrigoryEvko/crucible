/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#include "common.h"

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED
#define SENSE_HUB_NUM_COUNTERS 256
#define SENSE_HUB_NUM_GAUGES 64
#define SENSE_HUB_BUILD_TAG 0xDEB6 /* "DEBUG" */
#else
#define SENSE_HUB_NUM_COUNTERS 128
#define SENSE_HUB_NUM_GAUGES 32
#define SENSE_HUB_BUILD_TAG 0xBA51 /* "BASIC" */
#endif

#define SENSE_HUB_VERSION 2
#define SENSE_HUB_MAGIC 0x4352424CU /* 'CRBL' */

/* Userspace computes this same value from its own mirrored constants and
 * refuses to load on a mismatch. The two build modes therefore cannot be
 * mistaken for each other. */
#define SENSE_HUB_LAYOUT_HASH                                                                              \
    (((unsigned long long)SENSE_HUB_NUM_COUNTERS << 48) | ((unsigned long long)SENSE_HUB_NUM_GAUGES << 32) \
     | ((unsigned long long)SENSE_HUB_VERSION << 16) | (unsigned long long)SENSE_HUB_BUILD_TAG)

#define FUTEX_WAIT 0
#define FUTEX_WAIT_BITSET 9
#define FUTEX_LOCK_PI 6
#define FUTEX_CMD_MASK 127

/* The kernel names roughly eighty distinct drop reasons. These buckets fold
 * that set down, and anything unlisted lands in the OTHER bucket. */
#define SKB_DROP_NEIGH_FAILED_BUCKET 0
#define SKB_DROP_NETFILTER_DROP_BUCKET 1
#define SKB_DROP_TCP_INVALID_BUCKET 2
#define SKB_DROP_TCP_RESET_BUCKET 3
#define SKB_DROP_TCP_OFOMERGE_BUCKET 4
#define SKB_DROP_PROTO_MEM_BUCKET 5
#define SKB_DROP_NO_SOCKET_BUCKET 6
#define SKB_DROP_RX_NO_NETDEV_BUCKET 7
#define SKB_DROP_OTHER_BUCKET 8

#define IPI_REASON_RESCHEDULE_BUCKET 0
#define IPI_REASON_CALL_FUNCTION_BUCKET 1
#define IPI_REASON_NMI_BUCKET 2
#define IPI_REASON_CPU_STOP_BUCKET 3
#define IPI_REASON_REBOOT_BUCKET 4
#define IPI_REASON_THERMAL_BUCKET 5
#define IPI_REASON_OTHER_BUCKET 6

/*
 * Userspace mmaps the counter array and indexes it by these values, so the
 * order is the shared contract. The slots are grouped into four domains of
 * thirty-two, each domain ending in reserved slots so that a later addition
 * does not renumber what is already there.
 */
enum sense_idx {
    NET_TCP_ESTABLISHED = 0,
    NET_TCP_LISTEN = 1,
    NET_TCP_TIME_WAIT = 2,
    NET_TCP_CLOSE_WAIT = 3,
    NET_TCP_OTHER = 4,
    NET_UDP_ACTIVE = 5,
    NET_UNIX_ACTIVE = 6,
    NET_TX_BYTES = 7,

    NET_RX_BYTES = 8,
    TCP_RETRANSMIT_COUNT = 9,
    TCP_RST_SENT = 10,
    TCP_ERROR_COUNT = 11,
    SKB_DROP_COUNT = 12,
    TCP_CONG_LOSS = 13,
    MPTCP_SUBFLOW_ESTABLISHED = 14,
    _NET_RESERVED_15 = 15,

    _NET_RESERVED_16 = 16,
    _NET_RESERVED_17 = 17,
    _NET_RESERVED_18 = 18,
    _NET_RESERVED_19 = 19,
    _NET_RESERVED_20 = 20,
    _NET_RESERVED_21 = 21,
    _NET_RESERVED_22 = 22,
    _NET_RESERVED_23 = 23,
    _NET_RESERVED_24 = 24,
    _NET_RESERVED_25 = 25,
    _NET_RESERVED_26 = 26,
    _NET_RESERVED_27 = 27,
    _NET_RESERVED_28 = 28,
    _NET_RESERVED_29 = 29,
    _NET_RESERVED_30 = 30,
    _NET_RESERVED_31 = 31,

    FD_OPEN_OPS = 32,
    IO_READ_BYTES = 33,
    IO_WRITE_BYTES = 34,
    IO_READ_OPS = 35,
    IO_WRITE_OPS = 36,
    DISK_READ_BYTES = 37,
    DISK_WRITE_BYTES = 38,
    DISK_IO_LATENCY_NS = 39,

    DISK_IO_COUNT = 40,
    PAGE_CACHE_MISSES = 41,
    PAGE_CACHE_EVICTIONS = 42,
    READAHEAD_PAGES = 43,
    WRITE_THROTTLE_JIFFIES = 44,
    IO_UNPLUG_COUNT = 45,
    IOCOST_IDLE_COUNT = 46,
    IOCOST_ACTIVATE_COUNT = 47,

    WBT_DELAY_COUNT = 48,
    WBT_DELAY_NS = 49,
    FILELOCK_WAITS = 50,
    FILELOCK_NS = 51,
    SWIOTLB_BOUNCE_COUNT = 52,
    SWIOTLB_BOUNCE_BYTES = 53,
    _STORAGE_RESERVED_54 = 54,
    _STORAGE_RESERVED_55 = 55,

    _STORAGE_RESERVED_56 = 56,
    _STORAGE_RESERVED_57 = 57,
    _STORAGE_RESERVED_58 = 58,
    _STORAGE_RESERVED_59 = 59,
    _STORAGE_RESERVED_60 = 60,
    _STORAGE_RESERVED_61 = 61,
    _STORAGE_RESERVED_62 = 62,
    _STORAGE_RESERVED_63 = 63,

    MEM_MMAP_COUNT = 64,
    MEM_MUNMAP_COUNT = 65,
    MEM_PAGE_FAULTS_MIN = 66,
    MEM_PAGE_FAULTS_MAJ = 67,
    MEM_BRK_CALLS = 68,
    RSS_ANON_BYTES = 69,
    RSS_FILE_BYTES = 70,
    RSS_SWAP_ENTRIES = 71,

    RSS_SHMEM_BYTES = 72,
    DIRECT_RECLAIM_COUNT = 73,
    DIRECT_RECLAIM_NS = 74,
    SWAP_OUT_PAGES = 75,
    THP_COLLAPSE_OK = 76,
    THP_COLLAPSE_FAIL = 77,
    _MEM_RESERVED_78 = 78,
    NUMA_MIGRATE_PAGES = 79,

    NUMA_MIG_NUMA_HINT = 80,
    NUMA_MIG_OTHER = 81,
    COMPACTION_STALLS = 82,
    EXTFRAG_EVENTS = 83,
    KSWAPD_WAKES = 84,
    MMAP_LOCK_WAITS = 85, /* the tracepoint behind this needs kernel 5.16 or newer */
    MMAP_LOCK_NS = 86,
    IOMMU_FAULTS = 87,

    TLB_SHOOTDOWNS = 88,
    VMSCAN_LRU_ISOLATIONS = 89,
    VMSCAN_SCAN_NS = 90,
    RECLAIM_STALL_LOOPS = 91,
    _MEM_RESERVED_92 = 92,
    _MEM_RESERVED_93 = 93,
    _MEM_RESERVED_94 = 94,
    _MEM_RESERVED_95 = 95,

    SCHED_CTX_VOL = 96,
    SCHED_CTX_INVOL = 97,
    SCHED_MIGRATIONS = 98,
    SCHED_RUNTIME_NS = 99,
    SCHED_WAIT_NS = 100,
    SCHED_SLEEP_NS = 101,
    SCHED_IOWAIT_NS = 102,
    SCHED_BLOCKED_NS = 103,

    WAKEUPS_RECEIVED = 104,
    WAKEUPS_SENT = 105,
    KERNEL_LOCK_COUNT = 106,
    KERNEL_LOCK_NS = 107,
    FUTEX_WAIT_COUNT = 108,
    FUTEX_WAIT_NS = 109,
    THREADS_CREATED = 110,
    THREADS_EXITED = 111,

    CPU_FREQ_CHANGES = 112,
    PROCESS_FORKS = 113,
    PROCESS_EXECS = 114,
    SIGNAL_DELIVERED = 115,
    SIGNAL_FATAL_COUNT = 116,
    OOM_KILLS_SYSTEM = 117,
    OOM_KILL_US = 118,
    MCE_COUNT = 119,

    NMI_COUNT = 120, /* written from NMI context, so array map only */
    OSNOISE_NS_TOTAL = 121,
    CSD_QUEUE_COUNT = 122,
    PCIE_AER_CORR = 123,
    PCIE_AER_UNCORR = 124,
    EDAC_DRAM_CE = 125,
    SOFTIRQ_STOLEN_NS = 126,
    MAP_FULL_DROPS = 127, /* map updates that returned non-zero */

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED

    /* One slot per bucket of the drop reason field, which the kernel has
     * carried since 5.18. The undivided total stays in SKB_DROP_COUNT. */
    SKB_DROP_NEIGH_FAILED = 128,
    SKB_DROP_NETFILTER_DROP = 129,
    SKB_DROP_TCP_INVALID = 130,
    SKB_DROP_TCP_RESET = 131,
    SKB_DROP_TCP_OFOMERGE = 132,
    SKB_DROP_PROTO_MEM = 133, /* TCP memory exhaustion */
    SKB_DROP_NO_SOCKET = 134,
    SKB_DROP_RX_NO_NETDEV = 135,
    SKB_DROP_OTHER = 136,

    NF_CONNTRACK_NEW = 137,
    NF_CONNTRACK_DESTROY = 138,
    NF_CONNTRACK_DROPS = 139, /* a high count means the table is full */
    NETIF_RECV_COUNT = 140,
    NAPI_POLL_COUNT = 141,
    NAPI_RESCHED_COUNT = 142,
    NAPI_BUDGET_EXHAUSTED = 143,
    SOCK_OPS_RTT_SAMPLE_COUNT = 144,

    TCP_RECV_QUEUE_FULL = 145,
    TCP_FAST_RETRANSMIT = 146,
    TCP_TLP_FIRES = 147,
    TCP_LOSS_PROBE_COUNT = 148,
    TCP_DSACK_COUNT = 149,
    UDP_PROTO_MEM_ERRORS = 150,
    SOCKMAP_REDIRECT_COUNT = 151,
    QDISC_OVERLIMITS = 152,

    _NETWORK_EXT_RESERVED_153 = 153,
    _NETWORK_EXT_RESERVED_154 = 154,
    _NETWORK_EXT_RESERVED_155 = 155,
    _NETWORK_EXT_RESERVED_156 = 156,
    _NETWORK_EXT_RESERVED_157 = 157,
    _NETWORK_EXT_RESERVED_158 = 158,
    _NETWORK_EXT_RESERVED_159 = 159,

    BLOCK_INSERT_COUNT = 160,
    BLOCK_ISSUE_COUNT = 161,
    BLOCK_COMPLETE_COUNT = 162,
    BLOCK_BACKMERGE_COUNT = 163,
    BLOCK_FRONTMERGE_COUNT = 164,
    IOURING_SUBMIT_COUNT = 165,
    IOURING_COMPLETE_COUNT = 166,
    IOURING_SQ_FULL_COUNT = 167,

    NVME_RQ_SETUP_COUNT = 168,
    NVME_RQ_COMPLETE_COUNT = 169,
    NVME_QUEUE_DEPTH_GAUGE_VALID_AT_GAUGE = 170, /* placeholder, the value lives in a gauge slot */
    SCSI_DISPATCH_COUNT = 171,
    SCSI_COMPLETE_COUNT = 172,
    EXT4_TXN_COUNT = 173,
    XFS_TXN_COUNT = 174,
    BTRFS_TXN_COUNT = 175,

    WRITEBACK_INODE_COUNT = 176,
    WRITEBACK_PAGES_TOTAL = 177,
    DIRTY_PAGES_TOTAL_TRANSIENT = 178,
    JBD2_COMMIT_COUNT = 179,
    XFS_LOG_FORCE_COUNT = 180,
    FUSE_RQ_COUNT = 181,
    KYBER_LATENCY_VIOLATIONS = 182,
    BFQ_DECISIONS = 183,

    _STORAGE_EXT_RESERVED_184 = 184,
    _STORAGE_EXT_RESERVED_185 = 185,
    _STORAGE_EXT_RESERVED_186 = 186,
    _STORAGE_EXT_RESERVED_187 = 187,
    _STORAGE_EXT_RESERVED_188 = 188,
    _STORAGE_EXT_RESERVED_189 = 189,
    _STORAGE_EXT_RESERVED_190 = 190,
    _STORAGE_EXT_RESERVED_191 = 191,

    PAGE_ALLOC_NORMAL = 192,
    PAGE_ALLOC_DMA32 = 193,
    PAGE_ALLOC_MOVABLE = 194,
    PAGE_ALLOC_RETRY = 195,
    PAGE_ALLOC_OOM = 196,
    /* These two slots stay empty. A per-allocation slab counter is far too
     * hot for a handler, so the totals come from /proc/slabinfo at snapshot
     * time instead. */
    _MEMORY_EXT_RESERVED_197 = 197,
    _MEMORY_EXT_RESERVED_198 = 198,
    KMEMLEAK_OBJECTS_TRACKED = 199, /* stays zero without CONFIG_DEBUG_KMEMLEAK */

    VMALLOC_BYTES_TOTAL = 200,
    HUGETLB_FAULTS = 201,
    KSM_PAGES_SHARING = 202,
    DAMON_AGGR_COUNT = 203,
    MEMCG_HIGH_BREACHES = 204,
    MEMCG_MAX_BREACHES = 205,
    MEMCG_OOM_KILLS = 206,
    MEMCG_SOFT_RECLAIM_BYTES = 207,

    CGROUP_FREEZER_FREEZES = 208,
    CGROUP_PIDS_FORKS_DENIED = 209,
    CGROUP_DEVICE_DENIES = 210,
    KSWAPD_RECLAIM_BYTES = 211,
    THP_PROMOTE_COUNT = 212,
    THP_DEMOTE_COUNT = 213,
    BALLOON_INFLATE_PAGES = 214,
    BALLOON_DEFLATE_PAGES = 215,

    _MEMORY_EXT_RESERVED_216 = 216,
    _MEMORY_EXT_RESERVED_217 = 217,
    _MEMORY_EXT_RESERVED_218 = 218,
    _MEMORY_EXT_RESERVED_219 = 219,
    _MEMORY_EXT_RESERVED_220 = 220,
    _MEMORY_EXT_RESERVED_221 = 221,
    _MEMORY_EXT_RESERVED_222 = 222,
    _MEMORY_EXT_RESERVED_223 = 223,

    IPI_RESCHEDULE = 224,
    IPI_CALL_FUNCTION = 225,
    IPI_NMI_DELIVERED = 226,
    IPI_CPU_STOP = 227,
    IPI_REBOOT = 228,
    IPI_THERMAL = 229,
    IPI_OTHER = 230,
    IPI_TOTAL_LATENCY_NS = 231, /* queue to handler latency */

    SCHED_WAKEUP_LATENCY_TOTAL = 232,
    CONTEXT_TRACKING_USER_ENTER = 233, /* one per syscall */
    CONTEXT_TRACKING_USER_EXIT = 234,
    CPU_ONLINE_TRANSITIONS = 235,
    CPU_OFFLINE_TRANSITIONS = 236,
    SCHED_EXT_DECISIONS = 237, /* one per context switch under sched_ext */
    PREEMPT_DISABLE_NS_TOTAL = 238,
    IRQ_DISABLE_NS_TOTAL = 239,

    RWSEM_WAIT_COUNT = 240,
    RWSEM_WAIT_NS = 241,
    RTMUTEX_WAIT_COUNT = 242,
    RTMUTEX_WAIT_NS = 243,

    /* Slots 244 and 245 stay empty. A per-interrupt counter is far too hot
     * for a handler, so the total comes from /proc/interrupts at snapshot
     * time instead. */
    _RELIABILITY_EXT_RESERVED_244 = 244,
    _RELIABILITY_EXT_RESERVED_245 = 245,
    PRINTK_LINE_COUNT = 246,
    AVC_DENIALS = 247,
    CAPABILITY_FAILS = 248,
    KVM_VMEXIT_COUNT = 249,
    RESCTRL_BREACHES = 250,
    BPF_PROG_VERIFIER_FAILS = 251,

    MODULE_LOAD_COUNT = 252,
    MODULE_UNLOAD_COUNT = 253,
    ALARMTIMER_FIRES = 254,
    ACPI_GPE_FIRES = 255,

#endif /* CRUCIBLE_SENSE_HUB_EXTENDED */

    SENSE_HUB_NUM_COUNTERS_SENTINEL = SENSE_HUB_NUM_COUNTERS,
};

/*
 * A gauge holds a current value or a running extreme. Subtracting two
 * snapshots of a gauge is meaningless, which is why these live in their own
 * array rather than among the counters. Some slots are written by handlers
 * here and some by userspace at snapshot time, as marked.
 */
enum sense_gauge {
    GAUGE_FD_CURRENT = 0, /* point-in-time */
    GAUGE_TCP_MIN_SRTT_US = 1, /* running min */
    GAUGE_TCP_MAX_SRTT_US = 2, /* running max */
    GAUGE_TCP_LAST_CWND = 3, /* last value */
    GAUGE_THERMAL_MAX_TRIP = 4, /* running max */
    GAUGE_SIGNAL_LAST_SIGNO = 5, /* last value */
    _GAUGE_MISCLASS_RESERVED_6 = 6,
    _GAUGE_MISCLASS_RESERVED_7 = 7,

    GAUGE_CSD_MAX_QUEUE_TO_START_NS = 8,
    GAUGE_OSNOISE_MAX_NS = 9,
    GAUGE_NMI_HANDLER_MAX_NS = 10,
    GAUGE_MMAP_LOCK_MAX_WAIT_NS = 11,
    _GAUGE_MAX_RESERVED_12 = 12,
    _GAUGE_MAX_RESERVED_13 = 13,
    _GAUGE_MAX_RESERVED_14 = 14,
    _GAUGE_MAX_RESERVED_15 = 15,

    /* Userspace writes the slots below at snapshot time, from the sources
     * named against each one. */

    GAUGE_SLAB_TOTAL_BYTES = 16, /* /proc/slabinfo */
    GAUGE_HARDIRQ_TOTAL_COUNT = 17, /* /proc/interrupts */
    GAUGE_NAPI_POLL_TOTAL = 18, /* /proc/net/softnet_stat */
    GAUGE_SKB_DROP_REASON_TOTAL = 19, /* /proc/net/snmp */
    GAUGE_TCP_RECV_BUFFER_MAX = 20, /* /proc/net/tcp */
    GAUGE_BLOCK_QUEUE_DEPTH_MAX = 21, /* /sys/block/<dev>/stat */
    GAUGE_PRINTK_RING_BYTES_FREE = 22, /* /sys/kernel/debug optional */
    _GAUGE_PROC_RESERVED_23 = 23,
    _GAUGE_PROC_RESERVED_24 = 24,
    _GAUGE_PROC_RESERVED_25 = 25,
    _GAUGE_PROC_RESERVED_26 = 26,
    _GAUGE_PROC_RESERVED_27 = 27,
    _GAUGE_PROC_RESERVED_28 = 28,
    _GAUGE_PROC_RESERVED_29 = 29,
    _GAUGE_PROC_RESERVED_30 = 30,
    _GAUGE_PROC_RESERVED_31 = 31,

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED

    /* The gauges below are computed from the sample ring at read time rather
     * than counted by a handler. */
    GAUGE_PMU_IPC_X1000 = 32,
    GAUGE_PMU_FRONTEND_STALL_PCT_X100 = 33,
    GAUGE_PMU_BACKEND_STALL_PCT_X100 = 34,
    GAUGE_PMU_BAD_SPEC_PCT_X100 = 35,
    GAUGE_PMU_RETIRING_PCT_X100 = 36,
    GAUGE_PMU_LLC_MISS_RATE_X100 = 37,
    GAUGE_PMU_BR_MISS_PER_KINST = 38,
    GAUGE_PMU_RAPL_PKG_JOULES_X1000 = 39,
    GAUGE_PMU_RAPL_CORES_JOULES_X1000 = 40,
    GAUGE_PMU_RAPL_DRAM_JOULES_X1000 = 41,

    GAUGE_WAKEUP_LATENCY_MAX_NS = 42,
    GAUGE_BLOCK_MAX_LATENCY_NS = 43,
    GAUGE_PREEMPT_DISABLE_MAX_NS = 44,
    GAUGE_IRQ_DISABLE_MAX_NS = 45,
    GAUGE_GP_MAX_NS = 46, /* longest RCU grace period */
    GAUGE_RQ_DEPTH_MAX = 47,
    GAUGE_WQ_DEPTH_MAX = 48,

    GAUGE_NUMA_HIT_RATIO_X100 = 49, /* /proc/vmstat numa_hit/miss */
    GAUGE_TCP_ESTABLISHED_CURRENT = 50, /* /proc/net/snmp */
    GAUGE_LOAD_AVG_1M_X100 = 51, /* /proc/loadavg */
    GAUGE_LOAD_AVG_5M_X100 = 52,
    GAUGE_LOAD_AVG_15M_X100 = 53,
    GAUGE_PSI_CPU_SOME_AVG10_X100 = 54, /* /proc/pressure/cpu */
    GAUGE_PSI_MEM_SOME_AVG10_X100 = 55, /* /proc/pressure/memory */
    GAUGE_PSI_IO_SOME_AVG10_X100 = 56, /* /proc/pressure/io */

    _GAUGE_EXT_RESERVED_57 = 57,
    _GAUGE_EXT_RESERVED_58 = 58,
    _GAUGE_EXT_RESERVED_59 = 59,
    _GAUGE_EXT_RESERVED_60 = 60,
    _GAUGE_EXT_RESERVED_61 = 61,
    _GAUGE_EXT_RESERVED_62 = 62,
    _GAUGE_EXT_RESERVED_63 = 63,

#endif /* CRUCIBLE_SENSE_HUB_EXTENDED */

    SENSE_HUB_NUM_GAUGES_SENTINEL = SENSE_HUB_NUM_GAUGES,
};

/*
 * Userspace reads this before any other map and refuses to load on a
 * mismatch, which catches a build-mode disagreement, a slot reorder between
 * minor versions, and a mix of incompatible layouts.
 */
struct sense_meta {
    __u32 magic; /* offset 0 */
    __u32 version; /* offset 4 */
    __u32 num_counters; /* offset 8 */
    __u32 num_gauges; /* offset 12 */
    __u64 layout_hash; /* offset 16 */
    __u32 build_tag; /* offset 24 */
    __u8 _pad[36]; /* offset 28, filling the record out to one cache line */
};

/* Monotone. A difference of two snapshots is meaningful. */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, SENSE_HUB_NUM_COUNTERS);
    __type(key, __u32);
    __type(value, __u64);
    __uint(map_flags, BPF_F_MMAPABLE);
} counters SEC(".maps");

/* Current values and running extremes. A difference of two snapshots is not
 * meaningful. */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, SENSE_HUB_NUM_GAUGES);
    __type(key, __u32);
    __type(value, __u64);
    __uint(map_flags, BPF_F_MMAPABLE);
} gauges SEC(".maps");

/*
 * Userspace writes this record itself, once, after the object loads and
 * before it reads any counter. No handler touches it. The rejected
 * alternative was to fill it from a tracepoint handler, but such a handler
 * runs for every caller of its event on the host, and the record stays empty
 * on a host where that event never fires.
 */
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, struct sense_meta);
    __uint(map_flags, BPF_F_MMAPABLE);
} meta SEC(".maps");

/* An enter with no matching exit leaves an orphan in the maps below, which
 * LRU_HASH evicts rather than letting it hold a slot forever. */
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
/* The four maps below are per-CPU so that the update never contends on a
 * shared root across cores. */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
    __uint(max_entries, 4096);
    __type(key, __u32);
    __type(value, __u64);
} wake_ts_pcpu SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
    __uint(max_entries, 4096);
    __type(key, __u64);
    __type(value, __u64);
} block_rq_pcpu SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
    __uint(max_entries, 256);
    __type(key, __u64);
    __type(value, __u64);
} napi_inflight_pcpu SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_HASH);
    __uint(max_entries, 4096);
    __type(key, __u64);
    __type(value, __u64);
} iouring_inflight_pcpu SEC(".maps");
#endif /* CRUCIBLE_SENSE_HUB_EXTENDED */

/* get_tid() and is_target() come from the included header. Defining them here
 * as well makes the object fail to link. */

static __always_inline void counter_add(__u32 idx, __u64 delta) {
    __u64* v = bpf_map_lookup_elem(&counters, &idx);
    if (v)
        __sync_fetch_and_add(v, delta);
    else {
        __u32 mfd = MAP_FULL_DROPS;
        __u64* m = bpf_map_lookup_elem(&counters, &mfd);
        if (m) __sync_fetch_and_add(m, 1);
    }
}

static __always_inline void gauge_set(__u32 idx, __u64 value) {
    __u64* v = bpf_map_lookup_elem(&gauges, &idx);
    if (v) __atomic_store_n(v, value, __ATOMIC_RELAXED);
}

static __always_inline void gauge_max(__u32 idx, __u64 value) {
    __u64* v = bpf_map_lookup_elem(&gauges, &idx);
    if (!v) return;

    /* A plain read-then-write loses the larger value when two CPUs interleave.
     * The loop is bounded because the verifier has to see it terminate. */
    __u64 old, cur;
#pragma unroll
    for (int i = 0; i < 4; i++) {
        cur = __atomic_load_n(v, __ATOMIC_RELAXED);
        if (value <= cur) return;
        old = __sync_val_compare_and_swap(v, cur, value);
        if (old == cur) return;
    }
}

SEC("tracepoint/sched/sched_switch")
int sense_sched_switch(void* ctx) {
    /* Not filtered to the target. A context switch is the signal that this
     * CPU was rescheduled, whoever it was rescheduled for. */
    counter_add(SCHED_CTX_INVOL, 1);
    /* TODO: accumulate pid, migration and runtime as well. */
    return 0;
}

/* TODO: write the handler bodies for every counter slot that has none. */

SEC("tracepoint/mptcp/mptcp_subflow_get_send")
int sense_mptcp_subflow_send(void* ctx) {
    if (!is_target()) return 0;
    counter_add(MPTCP_SUBFLOW_ESTABLISHED, 1);
    return 0;
}

SEC("tracepoint/filemap/mm_filemap_delete_from_page_cache")
int sense_filemap_evict(void* ctx) {
    if (!is_target()) return 0;
    counter_add(PAGE_CACHE_EVICTIONS, 1);
    return 0;
}

/* The kernel exposes no direct throttle event for iocost. The idle and
 * activate pair marks a cgroup going under heavy pressure and coming back
 * out, and the difference of the two counts over a window approximates how
 * many are throttled right now. */
SEC("tracepoint/iocost/iocost_iocg_idle")
int sense_iocost_idle(void* ctx) {
    if (!is_target()) return 0;
    counter_add(IOCOST_IDLE_COUNT, 1);
    return 0;
}

SEC("tracepoint/iocost/iocost_iocg_activate")
int sense_iocost_activate(void* ctx) {
    if (!is_target()) return 0;
    counter_add(IOCOST_ACTIVATE_COUNT, 1);
    return 0;
}

SEC("tracepoint/wbt/wbt_lat")
int sense_wbt_lat(void* ctx) {
    if (!is_target()) return 0;
    counter_add(WBT_DELAY_COUNT, 1);
    return 0;
}

SEC("tracepoint/filelock/locks_get_lock_context")
int sense_filelock_wait(void* ctx) {
    if (!is_target()) return 0;
    counter_add(FILELOCK_WAITS, 1);
    return 0;
}

SEC("tracepoint/swiotlb/swiotlb_bounced")
int sense_swiotlb_bounced(void* ctx) {
    if (!is_target()) return 0;
    counter_add(SWIOTLB_BOUNCE_COUNT, 1);
    /* TODO: read size field from ctx, add to SWIOTLB_BOUNCE_BYTES */
    return 0;
}

/* There is no handler for huge page splits because the kernel exposes no
 * tracepoint for them. Userspace reads the thp_split rows of /proc/vmstat at
 * snapshot time instead. */

SEC("tracepoint/migrate/mm_migrate_pages")
int sense_numa_migrate_v2(void* ctx) {
    if (!is_target()) return 0;
    /* TODO: branch on reason field — reason=5 (numa_misplaced) goes
     * to NUMA_MIG_NUMA_HINT, others to NUMA_MIG_OTHER */
    counter_add(NUMA_MIG_OTHER, 1);
    return 0;
}

SEC("tracepoint/vmscan/mm_vmscan_kswapd_wake")
int sense_kswapd_wake(void* ctx) {
    counter_add(KSWAPD_WAKES, 1);
    return 0;
}

SEC("tracepoint/mmap_lock/mmap_lock_acquire_returned")
int sense_mmap_lock_returned(void* ctx) {
    if (!is_target()) return 0;
    /* TODO: record the start in mmap_lock_ts, then on the release event
     * compute the delta into MMAP_LOCK_NS, MMAP_LOCK_WAITS and
     * GAUGE_MMAP_LOCK_MAX_WAIT_NS. */
    counter_add(MMAP_LOCK_WAITS, 1);
    return 0;
}

SEC("tracepoint/iommu/io_page_fault")
int sense_iommu_fault(void* ctx) {
    counter_add(IOMMU_FAULTS, 1);
    return 0;
}

SEC("tracepoint/tlb/tlb_flush")
int sense_tlb_shootdown(void* ctx) {
    /* Not filtered to the target. A shootdown requested by any task on the
     * host costs every CPU it reaches. */
    counter_add(TLB_SHOOTDOWNS, 1);
    return 0;
}

SEC("tracepoint/vmscan/mm_vmscan_lru_isolate")
int sense_vmscan_lru_isolate(void* ctx) {
    if (!is_target()) return 0;
    counter_add(VMSCAN_LRU_ISOLATIONS, 1);
    /* TODO: VMSCAN_SCAN_NS via paired tracepoint mm_vmscan_lru_shrink_inactive */
    return 0;
}

SEC("tracepoint/sched/sched_process_fork")
int sense_process_fork(void* ctx) {
    counter_add(PROCESS_FORKS, 1);
    return 0;
}

SEC("tracepoint/sched/sched_process_exec")
int sense_process_exec(void* ctx) {
    counter_add(PROCESS_EXECS, 1);
    return 0;
}

SEC("tracepoint/signal/signal_deliver")
int sense_signal_deliver(void* ctx) {
    if (!is_target()) return 0;
    counter_add(SIGNAL_DELIVERED, 1);
    return 0;
}

SEC("tracepoint/nmi/nmi_handler")
int sense_nmi(void* ctx) {
    /* This runs in NMI context. It may touch the array map and nothing else.
     * A hash map lookup here is unsafe, because LRU eviction is not. */
    __u64 now = bpf_ktime_get_ns();
    counter_add(NMI_COUNT, 1);
    /* TODO: read handler_ns from ctx, add to NMI_HANDLER_NS_TOTAL,
     * gauge_max GAUGE_NMI_HANDLER_MAX_NS. */
    (void)now;
    return 0;
}

SEC("tracepoint/osnoise/osnoise_sample")
int sense_osnoise(void* ctx) {
    /* The event carries a duration and a thread id per sample window.
     * TODO: read the noise field into OSNOISE_NS_TOTAL and the matching
     * gauge. */
    return 0;
}

SEC("tracepoint/csd/csd_queue_cpu")
int sense_csd_queue(void* ctx) {
    if (!is_target()) return 0;
    /* TODO: stash the queue timestamp in csd_inflight, then on
     * csd_function_entry compute the latency into the total and into
     * GAUGE_CSD_MAX_QUEUE_TO_START_NS. */
    counter_add(CSD_QUEUE_COUNT, 1);
    return 0;
}

SEC("tracepoint/ras/aer_event")
int sense_pcie_aer(void* ctx) {
    /* TODO: branch on severity field — 2=Corrected → PCIE_AER_CORR,
     * 0/1=Uncorrected → PCIE_AER_UNCORR. */
    counter_add(PCIE_AER_CORR, 1);
    return 0;
}

SEC("tracepoint/ras/mc_event")
int sense_edac_dram_ce(void* ctx) {
    /* TODO: gate on err_type=corrected (most events) */
    counter_add(EDAC_DRAM_CE, 1);
    return 0;
}

#ifdef CRUCIBLE_SENSE_HUB_EXTENDED

SEC("tracepoint/skb/kfree_skb")
int sense_skb_drop_reason(void* ctx) {
    /* TODO: read the reason field and fold it into one of the nine buckets,
     * with anything unlisted going to SKB_DROP_OTHER. */
    counter_add(SKB_DROP_OTHER, 1);
    return 0;
}

SEC("tracepoint/ipi/ipi_send_cpu")
int sense_ipi_send_cpu(void* ctx) {
    /* TODO: split by reason. On x86 that means the call site, on arm64 the
     * reason field of ipi_raise. */
    counter_add(IPI_OTHER, 1);
    return 0;
}

SEC("tracepoint/sched/sched_waking")
int sense_wake_start(void* ctx) {
    /* TODO: stash the timestamp in wake_ts_pcpu, keyed by tid. */
    return 0;
}

SEC("tracepoint/sched/sched_switch")
int sense_wake_finish(void* ctx) {
    /* TODO: pair with the stashed timestamp and record the latency. */
    return 0;
}

SEC("tracepoint/context_tracking/user_enter")
int sense_user_enter(void* ctx) {
    counter_add(CONTEXT_TRACKING_USER_ENTER, 1);
    return 0;
}

SEC("tracepoint/context_tracking/user_exit")
int sense_user_exit(void* ctx) {
    counter_add(CONTEXT_TRACKING_USER_EXIT, 1);
    return 0;
}

SEC("tracepoint/io_uring/io_uring_submit_req")
int sense_iouring_submit(void* ctx) {
    if (!is_target()) return 0;
    counter_add(IOURING_SUBMIT_COUNT, 1);
    return 0;
}

SEC("tracepoint/io_uring/io_uring_complete")
int sense_iouring_complete(void* ctx) {
    if (!is_target()) return 0;
    counter_add(IOURING_COMPLETE_COUNT, 1);
    return 0;
}

SEC("tracepoint/napi/napi_poll")
int sense_napi_poll(void* ctx) {
    counter_add(NAPI_POLL_COUNT, 1);
    /* TODO: compare work against budget to detect budget exhaustion. */
    return 0;
}

SEC("tracepoint/avc/selinux_audited")
int sense_avc_denial(void* ctx) {
    counter_add(AVC_DENIALS, 1);
    return 0;
}

SEC("tracepoint/printk/console")
int sense_printk_line(void* ctx) {
    counter_add(PRINTK_LINE_COUNT, 1);
    return 0;
}

/* TODO: write the handler bodies for every extended counter slot that has
 * none. */

#endif /* CRUCIBLE_SENSE_HUB_EXTENDED */

char LICENSE[] SEC("license") = "Dual BSD/GPL";
