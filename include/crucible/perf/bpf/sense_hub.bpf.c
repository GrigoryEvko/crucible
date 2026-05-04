/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * sense_hub.bpf.c — The organism's complete sensory nervous system.
 *
 * 96 counters in a single BPF_F_MMAPABLE array = 768 bytes = 12 cache lines.
 * Userspace reads via mmap pointer dereference: ~200ns same-CCD on Zen 3.
 *
 * Domains tracked:
 *   Network    — TCP states, UDP/UNIX counts, bytes tx/rx
 *   I/O        — syscall-level read/write byte counts and ops
 *   Files      — open FD count, open/close operations
 *   Memory     — mmap/munmap, page faults, RSS breakdown, reclaim, THP, NUMA
 *   Scheduler  — vol/invol switches, migrations, runtime, wait, sleep/iowait/blocked
 *   Contention — futex wait + kernel lock contention
 *   Threads    — creation + exit tracking
 *   Block I/O  — actual disk bytes, latency, unplug batching
 *   Page Cache — cache misses, readahead pages
 *   Writeback  — dirty page throttling
 *   Net Health — TCP retransmits, resets, errors, packet drops, RTT, congestion
 *   Reliability— signals, OOM, thermal, MCE
 *
 * 59 tracepoint programs attached.
 *
 * TODO(perf): sense_sched_switch runs system-wide (no early tgid gate).
 * On a busy box ~200K ctx switches/sec × 2 map lookups adds ~6 ms/s of
 * overhead. We deliberately tolerate this cost rather than gating on
 * another map: the lookups ARE the filter — our_tids is the source of
 * truth, and adding a second "populated" flag creates a race window.
 * If overhead becomes painful, re-evaluate then.
 */

#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

/* ─── Constants ────────────────────────────────────────────────────── */

#ifndef BPF_F_MMAPABLE
#define BPF_F_MMAPABLE (1U << 10)
#endif

#define AF_INET     2
#define AF_INET6    10
#define AF_UNIX     1
#define SOCK_STREAM 1
#define SOCK_DGRAM  2
#define IPPROTO_TCP 6
#define TCP_CLOSE   7

#define STYPE_UDP   1
#define STYPE_UNIX  2

/* FUTEX operations */
#define FUTEX_WAIT          0
#define FUTEX_WAIT_BITSET   9
#define FUTEX_LOCK_PI       6
#define FUTEX_CMD_MASK      127

/* Task states */
#define TASK_RUNNING        0

/* RSS stat members */
#define MM_FILEPAGES  0
#define MM_ANONPAGES  1
#define MM_SWAPENTS   2
#define MM_SHMEMPAGES 3

/* vmscan write_folio flags */
#define RECLAIM_WB_ANON  0x0001u

/* TCP congestion states */
#define TCP_CA_Loss  4

/* Fatal signals */
#define SIGBUS   7
#define SIGFPE   8
#define SIGKILL  9
#define SIGSEGV  11
#define SIGABRT  6

/* ─── PID filter (set from userspace via .rodata) ──────────────────── */

const volatile __u32 target_tgid = 0;

/*
 * Inverted default: when target_tgid is 0 (the userspace rewrite of .rodata
 * silently failed, or the loader forgot to do it), is_target() returns
 * false for every task. Result: zero readings, loud and visible to the
 * user. The old `target_tgid == 0 || tgid == target_tgid` would have
 * recorded every tracepoint system-wide in that scenario — a firehose.
 */
static __always_inline bool is_target(void)
{
    __u32 tgid = bpf_get_current_pid_tgid() >> 32;
    return target_tgid != 0 && tgid == target_tgid;
}

static __always_inline __u32 get_tid(void)
{
    return (__u32)bpf_get_current_pid_tgid();
}

static __always_inline __u32 get_tgid(void)
{
    return bpf_get_current_pid_tgid() >> 32;
}

/* ─── Counter indices (MUST match Rust SenseCounters layout) ───────── */

enum sense_idx {
    /* ── Cache line 0 (bytes 0-63): Network State ────────────────── */
    NET_TCP_ESTABLISHED = 0,
    NET_TCP_LISTEN      = 1,
    NET_TCP_TIME_WAIT   = 2,
    NET_TCP_CLOSE_WAIT  = 3,
    NET_TCP_OTHER       = 4,
    NET_UDP_ACTIVE      = 5,
    NET_UNIX_ACTIVE     = 6,
    NET_TX_BYTES        = 7,

    /* ── Cache line 1 (bytes 64-127): I/O + Files ────────────────── */
    NET_RX_BYTES        = 8,
    FD_CURRENT          = 9,
    FD_OPEN_OPS         = 10,
    IO_READ_BYTES       = 11,
    IO_WRITE_BYTES      = 12,
    IO_READ_OPS         = 13,
    IO_WRITE_OPS        = 14,
    MEM_MMAP_COUNT      = 15,

    /* ── Cache line 2 (bytes 128-191): Memory + Scheduler Core ───── */
    MEM_MUNMAP_COUNT    = 16,
    MEM_PAGE_FAULTS_MIN = 17,
    MEM_PAGE_FAULTS_MAJ = 18,
    MEM_BRK_CALLS       = 19,
    SCHED_CTX_VOL       = 20,
    SCHED_CTX_INVOL     = 21,
    SCHED_MIGRATIONS    = 22,
    SCHED_RUNTIME_NS    = 23,

    /* ── Cache line 3 (bytes 192-255): Scheduler + Contention ────── */
    SCHED_WAIT_NS       = 24,
    FUTEX_WAIT_COUNT    = 25,
    FUTEX_WAIT_NS       = 26,
    THREADS_CREATED     = 27,
    SCHED_SLEEP_NS      = 28,
    SCHED_IOWAIT_NS     = 29,
    SCHED_BLOCKED_NS    = 30,
    WAKEUPS_RECEIVED    = 31,

    /* ── Cache line 4 (bytes 256-319): CPU Extended ──────────────── */
    KERNEL_LOCK_COUNT   = 32,
    KERNEL_LOCK_NS      = 33,
    SOFTIRQ_STOLEN_NS   = 34,
    THREADS_EXITED      = 35,
    CPU_FREQ_CHANGES    = 36,
    WAKEUPS_SENT        = 37,
    _RESERVED_38        = 38,
    _RESERVED_39        = 39,

    /* ── Cache line 5 (bytes 320-383): Memory Pressure ───────────── */
    RSS_ANON_BYTES      = 40,
    RSS_FILE_BYTES      = 41,
    RSS_SWAP_ENTRIES    = 42,
    RSS_SHMEM_BYTES     = 43,
    DIRECT_RECLAIM_COUNT = 44,
    DIRECT_RECLAIM_NS   = 45,
    SWAP_OUT_PAGES      = 46,
    THP_COLLAPSE_OK     = 47,

    /* ── Cache line 6 (bytes 384-447): Memory Adv + Block I/O ────── */
    THP_COLLAPSE_FAIL   = 48,
    NUMA_MIGRATE_PAGES  = 49,
    COMPACTION_STALLS   = 50,
    EXTFRAG_EVENTS      = 51,
    DISK_READ_BYTES     = 52,
    DISK_WRITE_BYTES    = 53,
    DISK_IO_LATENCY_NS  = 54,
    DISK_IO_COUNT       = 55,

    /* ── Cache line 7 (bytes 448-511): I/O Adv + Net Health ──────── */
    PAGE_CACHE_MISSES   = 56,
    READAHEAD_PAGES     = 57,
    WRITE_THROTTLE_JIFFIES = 58,
    IO_UNPLUG_COUNT     = 59,
    TCP_RETRANSMIT_COUNT = 60,
    TCP_RST_SENT        = 61,
    TCP_ERROR_COUNT     = 62,
    SKB_DROP_COUNT      = 63,

    /* ── Cache line 8 (bytes 512-575): Net Health + Reliability ──── */
    TCP_MIN_SRTT_US     = 64,
    TCP_MAX_SRTT_US     = 65,
    TCP_LAST_CWND       = 66,
    TCP_CONG_LOSS       = 67,
    SIGNAL_FATAL_COUNT  = 68,
    SIGNAL_LAST_SIGNO   = 69,
    OOM_KILLS_SYSTEM    = 70,
    OOM_KILL_US         = 71,

    /* ── Cache line 9 (bytes 576-639): Reliability + Reserved ────── */
    RECLAIM_STALL_LOOPS = 72,
    THERMAL_MAX_TRIP    = 73,
    MCE_COUNT           = 74,
    MAP_FULL_DROPS      = 75, /* BPF map updates that returned non-zero */
    _RESERVED_76        = 76,
    _RESERVED_77        = 77,
    _RESERVED_78        = 78,
    _RESERVED_79        = 79,

    /* ── Cache lines 10-11 (bytes 640-767): Reserved ─────────────── */
    _RESERVED_80 = 80, _RESERVED_81 = 81, _RESERVED_82 = 82, _RESERVED_83 = 83,
    _RESERVED_84 = 84, _RESERVED_85 = 85, _RESERVED_86 = 86, _RESERVED_87 = 87,
    _RESERVED_88 = 88, _RESERVED_89 = 89, _RESERVED_90 = 90, _RESERVED_91 = 91,
    _RESERVED_92 = 92, _RESERVED_93 = 93, _RESERVED_94 = 94, _RESERVED_95 = 95,

    NUM_COUNTERS        = 96,
};

/* ─── The mmapable counter array ────────────────────────────────────── */

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, NUM_COUNTERS);
    __type(key, __u32);
    __type(value, __u64);
    __uint(map_flags, BPF_F_MMAPABLE);
} counters SEC(".maps");

/* ─── Helper maps (BPF-internal) ────────────────────────────────────── */

/* GAPS-004g-AUDIT-3 (2026-05-04): LRU_HASH (was plain HASH).
 * socket_fds tracks open sockets across the process lifetime; close()
 * removes the entry.  With plain HASH and MAX_ENTRIES=4096, a workload
 * opening >4096 sockets has subsequent socket() calls silently dropped.
 * LRU evicts the oldest entry — graceful degradation. */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 4096);
    __type(key, __u32);
    __type(value, __u8);
} socket_fds SEC(".maps");

struct socket_args {
    __u32 domain;
    __u32 type;
};

/* GAPS-004g-AUDIT-3: LRU_HASH (was plain HASH).  enter/exit-paired —
 * orphan if socket() syscall is interrupted mid-call.  Rare but
 * accumulates to MAX_ENTRIES=256 then silently rejects new enters. */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 256);
    __type(key, __u32);
    __type(value, struct socket_args);
} socket_enter SEC(".maps");

/* GAPS-004g-AUDIT-3: LRU_HASH (was plain HASH).  Same orphaned-entry
 * leak class as lock_contention.bpf.c's wait_start (fixed GAPS-004d-
 * AUDIT) and syscall_latency.bpf.c's syscall_start (fixed GAPS-004e):
 * a sched_switch enter not followed by a matching exit (thread killed
 * mid-context-switch, prev_pid filtering rejected exit, etc.) orphans
 * the timestamp; orphans accumulate to MAX_ENTRIES=1024 then silently
 * block new inserts. */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, __u64);
} switch_ts SEC(".maps");

/* Thread IDs belonging to our target process. Userspace populates this
 * at load time (main TID) and at thread create time. Multi-threaded
 * benches can spawn hundreds of worker threads, so 4096 gives headroom. */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __type(key, __u32);
    __type(value, __u8);
} our_tids SEC(".maps");

/* Futex enter timestamps: tid -> ktime_ns
 *
 * GAPS-004g-AUDIT-3: LRU_HASH (was plain HASH).  Same fix class as
 * lock_contention.bpf.c's wait_start (GAPS-004d-AUDIT). */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, __u64);
} futex_ts SEC(".maps");

/* Kernel lock contention timestamps: tid -> ktime_ns
 *
 * GAPS-004g-AUDIT-3: LRU_HASH (was plain HASH).  Same fix class. */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, __u64);
} lock_ts SEC(".maps");

/*
 * Softirq entry timestamps: per-cpu array, keyed by softirq vector.
 * Linux has 10 softirq vectors (HI, TIMER, NET_TX, NET_RX, BLOCK,
 * IRQ_POLL, TASKLET, SCHED, HRTIMER, RCU). Nested softirqs on the same
 * CPU — e.g. TIMER interrupting NET_RX — need separate slots so they
 * don't clobber each other's timestamps.
 */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 10);
    __type(key, __u32);
    __type(value, __u64);
} softirq_ts SEC(".maps");

/* Direct reclaim timestamps: tid -> ktime_ns
 *
 * GAPS-004g-AUDIT-3: LRU_HASH (was plain HASH).  Same fix class as
 * lock_contention.bpf.c's wait_start (GAPS-004d-AUDIT). */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 256);
    __type(key, __u32);
    __type(value, __u64);
} reclaim_ts SEC(".maps");

/*
 * Block I/O start timestamps: bio_key -> ktime_ns.
 *
 * The key is (dev, sector, bytes) combined — see bio_key() below. Two
 * concurrent BIOs can collide on identical (dev, sector, bytes) only if
 * they both target the same disk extent with the same length, which is
 * vanishingly rare during a bench but possible under heavy contention.
 *
 * LRU_HASH auto-evicts the oldest entry when full. That gives bounded
 * memory even if block_io_done tracepoints miss (e.g. a device driver
 * path that skips the normal completion), and keeps latency attribution
 * approximately correct under collision.
 *
 * Ideal fix would be keying by `struct request *` but the raw tracepoint
 * exposes only the decoded (dev, sector, bytes, ...) tuple, not the
 * request pointer itself.
 */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 4096);
    __type(key, __u64);
    __type(value, __u64);
} bio_ts SEC(".maps");

/* TCP probe rate limit: per-cpu last timestamp */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} tcp_probe_ts SEC(".maps");

/* ─── Atomic counter helpers ────────────────────────────────────────── */

static __always_inline void counter_add(__u32 idx, __u64 delta)
{
    __u64 *val = bpf_map_lookup_elem(&counters, &idx);
    if (val)
        __sync_fetch_and_add(val, delta);
}

static __always_inline void counter_sub(__u32 idx, __u64 delta)
{
    __u64 *val = bpf_map_lookup_elem(&counters, &idx);
    if (val)
        __sync_fetch_and_sub(val, delta);
}

/* Set a gauge counter (non-atomic but fine for gauges) */
static __always_inline void counter_set(__u32 idx, __u64 new_val)
{
    __u64 *val = bpf_map_lookup_elem(&counters, &idx);
    if (val)
        *val = new_val;
}

/*
 * Update gauge to max(current, new) via bounded CAS loop.
 * Concurrent CPUs in tracepoints can race: A reads 100, B reads 100, A
 * writes 200, B writes 150 → lost update, final gauge = 150. CAS ensures
 * we only commit if the value we compared against is still there.
 * Loop bounded so the BPF verifier can prove termination.
 */
static __always_inline void counter_max(__u32 idx, __u64 new_val)
{
    __u64 *val = bpf_map_lookup_elem(&counters, &idx);
    if (!val) return;
    #pragma unroll
    for (int i = 0; i < 4; i++) {
        __u64 cur = *val;
        if (new_val <= cur) return;
        if (__sync_val_compare_and_swap(val, cur, new_val) == cur)
            return;
    }
}

/*
 * Update gauge to min(current, new), treating 0 as "not set".
 * Same CAS pattern as counter_max. Skip new_val == 0 early.
 */
static __always_inline void counter_min_nz(__u32 idx, __u64 new_val)
{
    if (new_val == 0) return;
    __u64 *val = bpf_map_lookup_elem(&counters, &idx);
    if (!val) return;
    #pragma unroll
    for (int i = 0; i < 4; i++) {
        __u64 cur = *val;
        if (cur != 0 && new_val >= cur) return;
        if (__sync_val_compare_and_swap(val, cur, new_val) == cur)
            return;
    }
}

/*
 * Saturating subtract: never goes below zero. Counters like FD_CURRENT
 * get underflowed when we see close(3) for an FD inherited from exec
 * that we never counted in open. CAS loop keeps the invariant *val >= 0.
 */
static __always_inline void counter_sub_sat(__u32 idx, __u64 delta)
{
    __u64 *val = bpf_map_lookup_elem(&counters, &idx);
    if (!val) return;
    #pragma unroll
    for (int i = 0; i < 4; i++) {
        __u64 cur = *val;
        __u64 newv = cur > delta ? cur - delta : 0;
        if (__sync_val_compare_and_swap(val, cur, newv) == cur)
            return;
    }
}

/*
 * Record a map-update failure so userspace can see when tracepoint state
 * is getting dropped due to capacity pressure. Cheap: one map lookup +
 * one atomic add. Call site: wherever bpf_map_update_elem returns non-zero.
 */
static __always_inline void note_map_full(void)
{
    counter_add(MAP_FULL_DROPS, 1);
}

/* ─── Manual tracepoint context structs ─────────────────────────────── */
/* Used where vmlinux.h lacks a clean type or the layout is stable enough
 * that hand-rolling is simpler than pulling in CO-RE machinery. For the
 * authoritative ABI (sched_stat, rss_stat, etc.) we prefer the
 * vmlinux.h struct trace_event_raw_* types via BPF_CORE_READ. */

/* sched/sched_waking */
struct sense_sched_waking_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    char  comm[16];        /* offset 8 */
    __s32 pid;             /* offset 24 */
    __s32 prio;            /* offset 28 */
    __s32 target_cpu;      /* offset 32 */
};

/* sched/sched_process_exit */
struct sense_sched_exit_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    char  comm[16];        /* offset 8 */
    __s32 pid;             /* offset 24 */
    __s32 prio;            /* offset 28 */
    __u8  group_dead;      /* offset 32 */
};

/* power/cpu_frequency */
struct sense_cpu_freq_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    __u32 state;           /* offset 8: frequency in KHz */
    __u32 cpu_id;          /* offset 12 */
};

/* lock/contention_begin */
struct sense_lock_begin_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    __u64 lock_addr;       /* offset 8 */
    __u32 flags;           /* offset 16 */
};

/* lock/contention_end */
struct sense_lock_end_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    __u64 lock_addr;       /* offset 8 */
    __s32 ret;             /* offset 16 */
};

/* vmscan/mm_vmscan_direct_reclaim_end */
struct sense_reclaim_end_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    unsigned long nr_reclaimed; /* offset 8 */
};

/* vmscan/mm_vmscan_write_folio */
struct sense_write_folio_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    unsigned long pfn;     /* offset 8 */
    __s32 reclaim_flags;   /* offset 16 */
};

/* huge_memory/mm_collapse_huge_page */
struct sense_thp_collapse_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    __u64 mm;              /* offset 8: struct mm_struct* */
    __s32 isolated;        /* offset 16 */
    __s32 status;          /* offset 20: 1=succeeded */
};

/* migrate/mm_migrate_pages */
struct sense_migrate_pages_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    unsigned long succeeded; /* offset 8 */
    unsigned long failed;    /* offset 16 */
    unsigned long thp_succeeded; /* offset 24 */
    unsigned long thp_failed;    /* offset 32 */
    unsigned long thp_split;     /* offset 40 */
    unsigned long large_folio_split; /* offset 48 */
    __u32 mode;              /* offset 56 */
    __s32 reason;            /* offset 60: 5=numa_misplaced */
};

/* compaction/mm_compaction_end */
struct sense_compaction_end_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    unsigned long zone_start;  /* offset 8 */
    unsigned long migrate_pfn; /* offset 16 */
    unsigned long free_pfn;    /* offset 24 */
    unsigned long zone_end;    /* offset 32 */
    __u8  sync;                /* offset 40 */
    __u8  __pad[3];
    __s32 status;              /* offset 44 */
};

/* kmem/mm_page_alloc_extfrag */
struct sense_extfrag_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    unsigned long pfn;         /* offset 8 */
    __s32 alloc_order;         /* offset 16 */
    __s32 fallback_order;      /* offset 20 */
    __s32 alloc_migratetype;   /* offset 24 */
    __s32 fallback_migratetype;/* offset 28 */
    __s32 change_ownership;    /* offset 32 */
};

/* block/block_io_start, block/block_io_done */
struct sense_block_io_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    __u32 dev;             /* offset 8 */
    __u32 __pad;
    __u64 sector;          /* offset 16 */
    __u32 nr_sector;       /* offset 24 */
    __u32 bytes;           /* offset 28 */
    __u16 ioprio;          /* offset 32 */
    char  rwbs[10];        /* offset 34 */
    char  comm[16];        /* offset 44 */
};

/* block/block_unplug */
struct sense_block_unplug_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    __s32 nr_rq;           /* offset 8 */
};

/* filemap/mm_filemap_add_to_page_cache */
struct sense_filemap_add_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    unsigned long pfn;     /* offset 8 */
    unsigned long i_ino;   /* offset 16 */
    unsigned long index;   /* offset 24 */
    __u32 s_dev;           /* offset 32 */
    __u8  order;           /* offset 36 */
};

/* iomap/iomap_readahead */
struct sense_readahead_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    __u32 dev;             /* offset 8 */
    __u32 __pad;
    __u64 ino;             /* offset 16 */
    __s32 nr_pages;        /* offset 24 */
};

/* writeback/balance_dirty_pages (only the fields we need) */
struct sense_dirty_pages_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    char  bdi[32];         /* offset 8 */
    unsigned long limit;   /* offset 40 */
    unsigned long setpoint;/* offset 48 */
    unsigned long dirty;   /* offset 56 */
    unsigned long wb_setpoint; /* offset 64 */
    unsigned long wb_dirty;    /* offset 72 */
    unsigned long dirty_ratelimit; /* offset 80 */
    unsigned long task_ratelimit;  /* offset 88 */
    __u32 dirtied;         /* offset 96 */
    __u32 dirtied_pause;   /* offset 100 */
    unsigned long paused;  /* offset 104 */
    __s64 pause;           /* offset 112: jiffies, >0 = throttled */
};

/* tcp/tcp_probe (only the fields we need) */
struct sense_tcp_probe_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    __u8  saddr[28];      /* offset 8 */
    __u8  daddr[28];      /* offset 36 */
    __u16 sport;           /* offset 64 */
    __u16 dport;           /* offset 66 */
    __u16 family;          /* offset 68 */
    __u32 mark;            /* offset 72 */
    __u16 data_len;        /* offset 76 */
    __u16 __pad;
    __u32 snd_nxt;         /* offset 80 */
    __u32 snd_una;         /* offset 84 */
    __u32 snd_cwnd;        /* offset 88 */
    __u32 ssthresh;        /* offset 92 */
    __u32 snd_wnd;         /* offset 96 */
    __u32 srtt;            /* offset 100 */
    __u32 rcv_wnd;         /* offset 104 */
};

/* tcp/tcp_cong_state_set */
struct sense_cong_state_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    __u64 skaddr;          /* offset 8 */
    __u16 sport;           /* offset 16 */
    __u16 dport;           /* offset 18 */
    __u16 family;          /* offset 20 */
    __u8  saddr[4];       /* offset 22 */
    __u8  daddr[4];       /* offset 26 */
    __u8  saddr_v6[16];   /* offset 30 */
    __u8  daddr_v6[16];   /* offset 46 */
    __u8  cong_state;      /* offset 62 */
};

/* signal/signal_generate */
struct sense_signal_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    __s32 sig;             /* offset 8 */
    __s32 err;             /* offset 12 */
    __s32 code;            /* offset 16 */
    char  comm[16];        /* offset 20 */
    __s32 pid;             /* offset 36: target PID */
    __s32 group;           /* offset 40 */
    __s32 result;          /* offset 44 */
};

/* oom/mark_victim */
struct sense_oom_victim_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    __s32 pid;             /* offset 8 */
};

/* oom/reclaim_retry_zone */
struct sense_reclaim_retry_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    __s32 node;            /* offset 8 */
    __s32 zone_idx;        /* offset 12 */
    __s32 order;           /* offset 16 */
    __u32 __pad;
    unsigned long reclaimable;  /* offset 24 */
    unsigned long available;    /* offset 32 */
    unsigned long min_wmark;    /* offset 40 */
    __s32 no_progress_loops;    /* offset 48 */
};

/* thermal/thermal_zone_trip */
struct sense_thermal_trip_ctx {
    __u16 common_type;
    __u8  common_flags;
    __u8  common_preempt_count;
    __s32 common_pid;
    __u32 __data_loc_name; /* offset 8 */
    __s32 id;              /* offset 12 */
    __s32 trip;            /* offset 16 */
    __u32 trip_type;       /* offset 20: 0=ACTIVE,1=PASSIVE,2=HOT,3=CRITICAL */
};

/* ─── TCP state mapping ─────────────────────────────────────────────── */

static __always_inline __u32 tcp_state_to_idx(int state)
{
    switch (state) {
    case 1:  return NET_TCP_ESTABLISHED;
    case 10: return NET_TCP_LISTEN;
    case 6:  return NET_TCP_TIME_WAIT;
    case 8:  return NET_TCP_CLOSE_WAIT;
    default: return NET_TCP_OTHER;
    }
}

/*
 * Block I/O key: combine dev, sector, and bytes so concurrent BIOs on
 * the same sector but different lengths don't collide. Not collision-
 * free but far better than sector^dev alone, and LRU eviction bounds
 * the damage of any collision we do hit.
 */
static __always_inline __u64 bio_key(__u32 dev, __u64 sector, __u32 bytes)
{
    return (sector << 20) ^ ((__u64)dev << 52) ^ (__u64)bytes;
}

/* ═══════════════════════════════════════════════════════════════════════
 * NETWORK DOMAIN
 * ═══════════════════════════════════════════════════════════════════════ */

SEC("tracepoint/sock/inet_sock_set_state")
int sense_tcp_state(struct trace_event_raw_inet_sock_set_state *ctx)
{
    if (ctx->protocol != IPPROTO_TCP)
        return 0;

    __u32 tgid = bpf_get_current_pid_tgid() >> 32;
    /* Inverted default: zero readings if loader didn't rewrite .rodata.
     * Keep the tgid==0 allowance: TCP state transitions sometimes fire
     * from softirq where current is idle/swapper, and we still want to
     * count those when they belong to our sockets. */
    if (target_tgid == 0) return 0;
    if (tgid != target_tgid && tgid != 0)
        return 0;

    if (ctx->oldstate != TCP_CLOSE)
        counter_sub_sat(tcp_state_to_idx(ctx->oldstate), 1);
    if (ctx->newstate != TCP_CLOSE)
        counter_add(tcp_state_to_idx(ctx->newstate), 1);

    return 0;
}

SEC("tracepoint/syscalls/sys_enter_socket")
int sense_socket_enter(struct trace_event_raw_sys_enter *ctx)
{
    if (!is_target()) return 0;
    __u32 tid = get_tid();
    struct socket_args args = {
        .domain = (__u32)ctx->args[0],
        .type   = (__u32)ctx->args[1] & 0xFF,
    };
    if (bpf_map_update_elem(&socket_enter, &tid, &args, BPF_ANY) != 0)
        note_map_full();
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_socket")
int sense_socket_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target()) return 0;

    __u32 tid = get_tid();
    struct socket_args *args = bpf_map_lookup_elem(&socket_enter, &tid);
    if (!args) return 0;

    struct socket_args local = *args;
    bpf_map_delete_elem(&socket_enter, &tid);

    long fd = ctx->ret;
    if (fd < 0) return 0;

    __u32 ufd = (__u32)fd;
    if (local.domain == AF_INET || local.domain == AF_INET6) {
        if (local.type == SOCK_DGRAM) {
            __u8 stype = STYPE_UDP;
            if (bpf_map_update_elem(&socket_fds, &ufd, &stype, BPF_ANY) != 0)
                note_map_full();
            counter_add(NET_UDP_ACTIVE, 1);
        }
    } else if (local.domain == AF_UNIX) {
        __u8 stype = STYPE_UNIX;
        if (bpf_map_update_elem(&socket_fds, &ufd, &stype, BPF_ANY) != 0)
            note_map_full();
        counter_add(NET_UNIX_ACTIVE, 1);
    }

    counter_add(FD_CURRENT, 1);
    counter_add(FD_OPEN_OPS, 1);
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_sendto")
int sense_sendto_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target()) return 0;
    if (ctx->ret > 0)
        counter_add(NET_TX_BYTES, (__u64)ctx->ret);
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_sendmsg")
int sense_sendmsg_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target()) return 0;
    if (ctx->ret > 0)
        counter_add(NET_TX_BYTES, (__u64)ctx->ret);
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_recvfrom")
int sense_recvfrom_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target()) return 0;
    if (ctx->ret > 0)
        counter_add(NET_RX_BYTES, (__u64)ctx->ret);
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_recvmsg")
int sense_recvmsg_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target()) return 0;
    if (ctx->ret > 0)
        counter_add(NET_RX_BYTES, (__u64)ctx->ret);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * I/O DOMAIN (syscall-level)
 * ═══════════════════════════════════════════════════════════════════════ */

SEC("tracepoint/syscalls/sys_exit_read")
int sense_read_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target()) return 0;
    if (ctx->ret > 0) {
        counter_add(IO_READ_BYTES, (__u64)ctx->ret);
        counter_add(IO_READ_OPS, 1);
    }
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_readv")
int sense_readv_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target()) return 0;
    if (ctx->ret > 0) {
        counter_add(IO_READ_BYTES, (__u64)ctx->ret);
        counter_add(IO_READ_OPS, 1);
    }
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_write")
int sense_write_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target()) return 0;
    if (ctx->ret > 0) {
        counter_add(IO_WRITE_BYTES, (__u64)ctx->ret);
        counter_add(IO_WRITE_OPS, 1);
    }
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_writev")
int sense_writev_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target()) return 0;
    if (ctx->ret > 0) {
        counter_add(IO_WRITE_BYTES, (__u64)ctx->ret);
        counter_add(IO_WRITE_OPS, 1);
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * FILES DOMAIN
 * ═══════════════════════════════════════════════════════════════════════ */

SEC("tracepoint/syscalls/sys_exit_openat")
int sense_openat_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target()) return 0;
    if (ctx->ret >= 0) {
        counter_add(FD_CURRENT, 1);
        counter_add(FD_OPEN_OPS, 1);
    }
    return 0;
}

SEC("tracepoint/syscalls/sys_enter_close")
int sense_close_enter(struct trace_event_raw_sys_enter *ctx)
{
    if (!is_target()) return 0;

    __u32 fd = (__u32)ctx->args[0];
    /* Saturating: a bench may close an FD it inherited from exec
     * (stdin/stdout/stderr) without us having counted it in open.
     * Old counter_sub would underflow to UINT64_MAX. */
    counter_sub_sat(FD_CURRENT, 1);

    __u8 *stype = bpf_map_lookup_elem(&socket_fds, &fd);
    if (stype) {
        __u8 t = *stype;
        bpf_map_delete_elem(&socket_fds, &fd);
        if (t == STYPE_UDP)
            counter_sub_sat(NET_UDP_ACTIVE, 1);
        else if (t == STYPE_UNIX)
            counter_sub_sat(NET_UNIX_ACTIVE, 1);
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * MEMORY DOMAIN
 * ═══════════════════════════════════════════════════════════════════════ */

SEC("tracepoint/syscalls/sys_exit_mmap")
int sense_mmap_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target()) return 0;
    if (ctx->ret >= 0)
        counter_add(MEM_MMAP_COUNT, 1);
    return 0;
}

SEC("tracepoint/syscalls/sys_enter_munmap")
int sense_munmap_enter(struct trace_event_raw_sys_enter *ctx)
{
    if (!is_target()) return 0;
    counter_add(MEM_MUNMAP_COUNT, 1);
    return 0;
}

/* Page faults — manual struct for exceptions/page_fault_user */
struct trace_event_raw_page_fault_user {
    unsigned short common_type;
    unsigned char common_flags;
    unsigned char common_preempt_count;
    int common_pid;
    unsigned long address;
    unsigned long ip;
    unsigned long error_code;
};

/*
 * Count ALL user page faults as MEM_PAGE_FAULTS_MIN.
 *
 * The old code tried to split minor/major via error_code bit 0, but
 * that bit is x86 "present": 1 means the PTE was present (CoW or
 * protection fault), 0 means not present. Neither correlates with
 * "major" (backing store I/O required). Real major faults need
 * disk/swap touches — which we already track elsewhere (SWAP_OUT_PAGES
 * via vmscan_write_folio, DISK_*_BYTES via block_io_start).
 *
 * MEM_PAGE_FAULTS_MAJ is left as zero; a future fix could populate it
 * from exceptions/page_fault_kernel or a dedicated major-fault probe.
 */
SEC("tracepoint/exceptions/page_fault_user")
int sense_page_fault(struct trace_event_raw_page_fault_user *ctx)
{
    if (!is_target()) return 0;
    counter_add(MEM_PAGE_FAULTS_MIN, 1);
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_brk")
int sense_brk_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target()) return 0;
    counter_add(MEM_BRK_CALLS, 1);
    return 0;
}

/* ── NEW: RSS stat — real-time RSS breakdown by type ────────────────── */

/*
 * CO-RE read of the kernel's trace_event_raw_rss_stat. The field layout
 * changed twice between 5.18 and 6.2 (curr was added, then member went
 * from enum to int). BPF_CORE_READ relocates at load time so this works
 * across that range without rebuilding.
 */
SEC("tracepoint/kmem/rss_stat")
int sense_rss_stat(struct trace_event_raw_rss_stat *ctx)
{
    if (!is_target()) return 0;

    int member = BPF_CORE_READ(ctx, member);
    long size = BPF_CORE_READ(ctx, size);

    /* size is in bytes (can be negative for decrements) */
    __u64 abs_size = size >= 0 ? (__u64)size : 0;
    switch (member) {
    case MM_FILEPAGES:  counter_set(RSS_FILE_BYTES, abs_size); break;
    case MM_ANONPAGES:  counter_set(RSS_ANON_BYTES, abs_size); break;
    case MM_SWAPENTS:   counter_set(RSS_SWAP_ENTRIES, abs_size); break;
    case MM_SHMEMPAGES: counter_set(RSS_SHMEM_BYTES, abs_size); break;
    }
    return 0;
}

/* ── NEW: Direct reclaim — thread stalled doing kernel GC ───────────── */

/* ctx is never dereferenced — this tracepoint carries order/gfp_flags we
 * don't need, and trace_event_raw_sys_enter is the wrong type for a
 * non-syscall tracepoint. Use void* so we can't accidentally misread
 * fields that aren't there. */
SEC("tracepoint/vmscan/mm_vmscan_direct_reclaim_begin")
int sense_reclaim_begin(void *ctx)
{
    if (!is_target()) return 0;
    __u32 tid = get_tid();
    __u64 ts = bpf_ktime_get_ns();
    if (bpf_map_update_elem(&reclaim_ts, &tid, &ts, BPF_ANY) != 0)
        note_map_full();
    counter_add(DIRECT_RECLAIM_COUNT, 1);
    return 0;
}

SEC("tracepoint/vmscan/mm_vmscan_direct_reclaim_end")
int sense_reclaim_end(struct sense_reclaim_end_ctx *ctx)
{
    if (!is_target()) return 0;
    /* GAPS-004g-AUDIT-5 (2026-05-04): single bpf_ktime_get_ns() call —
     * was two before (one for delta, one wasted on the early-out path).
     * Same discipline as lock_contention.bpf.c GAPS-004d-AUDIT and
     * syscall_latency.bpf.c GAPS-004e: capture once at handler entry,
     * reuse for delta. Saves ~50 ns per event. */
    __u64 now = bpf_ktime_get_ns();
    __u32 tid = get_tid();
    __u64 *start = bpf_map_lookup_elem(&reclaim_ts, &tid);
    if (start && *start > 0) {
        __u64 elapsed = now - *start;
        counter_add(DIRECT_RECLAIM_NS, elapsed);
    }
    bpf_map_delete_elem(&reclaim_ts, &tid);
    return 0;
}

/* ── NEW: Swap-out detection — system-wide, not PID-filtered ────────── */

SEC("tracepoint/vmscan/mm_vmscan_write_folio")
int sense_write_folio(struct sense_write_folio_ctx *ctx)
{
    /* System-wide: if anonymous pages are being swapped out, the
     * environment is under memory pressure affecting everyone */
    if (ctx->reclaim_flags & RECLAIM_WB_ANON)
        counter_add(SWAP_OUT_PAGES, 1);
    return 0;
}

/* ── NEW: THP collapse tracking ─────────────────────────────────────── */

SEC("tracepoint/huge_memory/mm_collapse_huge_page")
int sense_thp_collapse(struct sense_thp_collapse_ctx *ctx)
{
    /* status: 1 = succeeded */
    if (ctx->status == 1)
        counter_add(THP_COLLAPSE_OK, 1);
    else
        counter_add(THP_COLLAPSE_FAIL, 1);
    return 0;
}

/* ── NEW: NUMA migration detection ──────────────────────────────────── */

SEC("tracepoint/migrate/mm_migrate_pages")
int sense_migrate_pages(struct sense_migrate_pages_ctx *ctx)
{
    /* reason 5 = numa_misplaced — the most interesting one */
    if (ctx->reason == 5)
        counter_add(NUMA_MIGRATE_PAGES, ctx->succeeded + ctx->thp_succeeded);
    return 0;
}

/* ── NEW: Compaction stalls — system-wide fragmentation indicator ───── */

SEC("tracepoint/compaction/mm_compaction_end")
int sense_compaction_end(struct sense_compaction_end_ctx *ctx)
{
    /* sync == true means synchronous compaction (blocking the thread) */
    if (ctx->sync)
        counter_add(COMPACTION_STALLS, 1);
    return 0;
}

/* ── NEW: External fragmentation events ─────────────────────────────── */

SEC("tracepoint/kmem/mm_page_alloc_extfrag")
int sense_extfrag(struct sense_extfrag_ctx *ctx)
{
    if (ctx->change_ownership)
        counter_add(EXTFRAG_EVENTS, 1);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * SCHEDULER DOMAIN
 * ═══════════════════════════════════════════════════════════════════════ */

SEC("tracepoint/sched/sched_switch")
int sense_sched_switch(struct trace_event_raw_sched_switch *ctx)
{
    __u64 now = bpf_ktime_get_ns();
    __u32 prev_pid = ctx->prev_pid;
    __u32 next_pid = ctx->next_pid;

    /* Switched OUT */
    __u8 *is_prev = bpf_map_lookup_elem(&our_tids, &prev_pid);
    if (is_prev) {
        if (ctx->prev_state == TASK_RUNNING)
            counter_add(SCHED_CTX_INVOL, 1);
        else
            counter_add(SCHED_CTX_VOL, 1);
        if (bpf_map_update_elem(&switch_ts, &prev_pid, &now, BPF_ANY) != 0)
            note_map_full();
    }

    /* Switched IN — compute wait time */
    __u8 *is_next = bpf_map_lookup_elem(&our_tids, &next_pid);
    if (is_next) {
        __u64 *ts = bpf_map_lookup_elem(&switch_ts, &next_pid);
        if (ts && *ts > 0) {
            counter_add(SCHED_WAIT_NS, now - *ts);
            __u64 zero = 0;
            bpf_map_update_elem(&switch_ts, &next_pid, &zero, BPF_ANY);
        }
    }
    return 0;
}

SEC("tracepoint/sched/sched_migrate_task")
int sense_migrate(struct trace_event_raw_sched_migrate_task *ctx)
{
    __u32 pid = ctx->pid;
    __u8 *is_ours = bpf_map_lookup_elem(&our_tids, &pid);
    if (is_ours)
        counter_add(SCHED_MIGRATIONS, 1);
    return 0;
}

SEC("tracepoint/sched/sched_stat_runtime")
int sense_runtime(struct trace_event_raw_sched_stat_runtime *ctx)
{
    if (!is_target()) return 0;
    counter_add(SCHED_RUNTIME_NS, ctx->runtime);
    return 0;
}

/* ── NEW: Off-CPU breakdown — sleep / iowait / blocked ──────────────── */
/* These require CONFIG_SCHEDSTATS=y and sched_schedstats=1 at runtime.
 *
 * Uses the vmlinux.h trace_event_raw_sched_stat_template + BPF_CORE_READ
 * so we get the authoritative kernel-side field layout with load-time
 * relocation. Avoids the trap of hand-rolled offsets falling behind
 * kernel field-reorderings. */

SEC("tracepoint/sched/sched_stat_sleep")
int sense_stat_sleep(struct trace_event_raw_sched_stat_template *ctx)
{
    __u32 pid = (__u32)BPF_CORE_READ(ctx, pid);
    __u8 *is_ours = bpf_map_lookup_elem(&our_tids, &pid);
    if (is_ours)
        counter_add(SCHED_SLEEP_NS, BPF_CORE_READ(ctx, delay));
    return 0;
}

SEC("tracepoint/sched/sched_stat_iowait")
int sense_stat_iowait(struct trace_event_raw_sched_stat_template *ctx)
{
    __u32 pid = (__u32)BPF_CORE_READ(ctx, pid);
    __u8 *is_ours = bpf_map_lookup_elem(&our_tids, &pid);
    if (is_ours)
        counter_add(SCHED_IOWAIT_NS, BPF_CORE_READ(ctx, delay));
    return 0;
}

SEC("tracepoint/sched/sched_stat_blocked")
int sense_stat_blocked(struct trace_event_raw_sched_stat_template *ctx)
{
    __u32 pid = (__u32)BPF_CORE_READ(ctx, pid);
    __u8 *is_ours = bpf_map_lookup_elem(&our_tids, &pid);
    if (is_ours)
        counter_add(SCHED_BLOCKED_NS, BPF_CORE_READ(ctx, delay));
    return 0;
}

/* ── NEW: Wakeup tracking — who woke whom ───────────────────────────── */

SEC("tracepoint/sched/sched_waking")
int sense_waking(struct sense_sched_waking_ctx *ctx)
{
    __u32 wakee = (__u32)ctx->pid;
    __u32 waker = get_tid();

    /* Count wakeups received by our threads */
    __u8 *wakee_ours = bpf_map_lookup_elem(&our_tids, &wakee);
    if (wakee_ours)
        counter_add(WAKEUPS_RECEIVED, 1);

    /* Count wakeups sent by our threads */
    __u8 *waker_ours = bpf_map_lookup_elem(&our_tids, &waker);
    if (waker_ours)
        counter_add(WAKEUPS_SENT, 1);

    return 0;
}

/* ── NEW: Thread exit tracking ──────────────────────────────────────── */

SEC("tracepoint/sched/sched_process_exit")
int sense_process_exit(struct sense_sched_exit_ctx *ctx)
{
    __u32 pid = (__u32)ctx->pid;
    __u8 *is_ours = bpf_map_lookup_elem(&our_tids, &pid);
    if (is_ours) {
        counter_add(THREADS_EXITED, 1);
        /* Clean up maps to prevent leaks */
        bpf_map_delete_elem(&our_tids, &pid);
        bpf_map_delete_elem(&switch_ts, &pid);
        bpf_map_delete_elem(&futex_ts, &pid);
        bpf_map_delete_elem(&lock_ts, &pid);
        bpf_map_delete_elem(&reclaim_ts, &pid);
    }
    return 0;
}

/* ── NEW: CPU frequency changes ─────────────────────────────────────── */

SEC("tracepoint/power/cpu_frequency")
int sense_cpu_freq(struct sense_cpu_freq_ctx *ctx)
{
    /* Count frequency change events (system-wide indicator) */
    counter_add(CPU_FREQ_CHANGES, 1);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * CONTENTION DOMAIN
 * ═══════════════════════════════════════════════════════════════════════ */

SEC("tracepoint/syscalls/sys_enter_futex")
int sense_futex_enter(struct trace_event_raw_sys_enter *ctx)
{
    if (!is_target()) return 0;

    __u32 op = (__u32)ctx->args[1] & FUTEX_CMD_MASK;
    if (op != FUTEX_WAIT && op != FUTEX_WAIT_BITSET && op != FUTEX_LOCK_PI)
        return 0;

    __u32 tid = get_tid();
    __u64 ts = bpf_ktime_get_ns();
    if (bpf_map_update_elem(&futex_ts, &tid, &ts, BPF_ANY) != 0)
        note_map_full();
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_futex")
int sense_futex_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target()) return 0;

    /* GAPS-004g-AUDIT-5 (2026-05-04): single bpf_ktime_get_ns() — captured
     * once at exit-handler entry, reused for delta. Mirrors the discipline
     * applied in lock_contention.bpf.c (GAPS-004d-AUDIT) and the upfront
     * fix in syscall_latency.bpf.c. */
    __u64 now = bpf_ktime_get_ns();
    __u32 tid = get_tid();
    __u64 *enter_ts = bpf_map_lookup_elem(&futex_ts, &tid);
    if (!enter_ts) return 0;

    __u64 start = *enter_ts;
    bpf_map_delete_elem(&futex_ts, &tid);

    if (start > 0) {
        __u64 elapsed = now - start;
        counter_add(FUTEX_WAIT_COUNT, 1);
        counter_add(FUTEX_WAIT_NS, elapsed);
    }
    return 0;
}

/* ── NEW: Kernel lock contention ────────────────────────────────────── */

SEC("tracepoint/lock/contention_begin")
int sense_lock_begin(struct sense_lock_begin_ctx *ctx)
{
    if (!is_target()) return 0;
    __u32 tid = get_tid();
    __u64 ts = bpf_ktime_get_ns();
    if (bpf_map_update_elem(&lock_ts, &tid, &ts, BPF_ANY) != 0)
        note_map_full();
    return 0;
}

SEC("tracepoint/lock/contention_end")
int sense_lock_end(struct sense_lock_end_ctx *ctx)
{
    if (!is_target()) return 0;
    /* GAPS-004g-AUDIT-5 (2026-05-04): single bpf_ktime_get_ns() captured
     * once at exit-handler entry, reused for delta. Same fix-class as
     * lock_contention.bpf.c GAPS-004d-AUDIT. */
    __u64 now = bpf_ktime_get_ns();
    __u32 tid = get_tid();
    __u64 *start = bpf_map_lookup_elem(&lock_ts, &tid);
    if (start && *start > 0) {
        __u64 elapsed = now - *start;
        counter_add(KERNEL_LOCK_COUNT, 1);
        counter_add(KERNEL_LOCK_NS, elapsed);
    }
    bpf_map_delete_elem(&lock_ts, &tid);
    return 0;
}

/* ── NEW: Softirq stolen time ───────────────────────────────────────── */
/*
 * Softirqs run in interrupt context on whatever task was unlucky enough
 * to be current when the IRQ fired — bpf_get_current_pid_tgid() does NOT
 * identify US, so is_target() is wrong here.
 *
 * Instead we record softirq duration system-wide. This gauge measures
 * per-CPU softirq time during the bench window; elevated values mean
 * the kernel was stealing cycles from whatever was running on that CPU,
 * including our bench. The PERCPU_ARRAY is keyed by softirq vector
 * (0..9) so nested softirqs on the same CPU — e.g. TIMER interrupting
 * NET_RX — keep independent start timestamps.
 *
 * SOFTIRQ_STOLEN_NS is therefore "system-wide per-CPU softirq time,
 * summed across CPUs during the bench window", NOT tgid-filtered.
 */

SEC("tracepoint/irq/softirq_entry")
int sense_softirq_entry(struct trace_event_raw_softirq *ctx)
{
    __u32 vec = BPF_CORE_READ(ctx, vec);
    if (vec >= 10) return 0; /* bound to NR_SOFTIRQS */
    __u64 ts = bpf_ktime_get_ns();
    if (bpf_map_update_elem(&softirq_ts, &vec, &ts, BPF_ANY) != 0)
        note_map_full();
    return 0;
}

SEC("tracepoint/irq/softirq_exit")
int sense_softirq_exit(struct trace_event_raw_softirq *ctx)
{
    __u32 vec = BPF_CORE_READ(ctx, vec);
    if (vec >= 10) return 0;
    /* GAPS-004g-AUDIT-5 (2026-05-04): single bpf_ktime_get_ns() captured
     * once at exit-handler entry, reused for delta. Same fix-class as
     * lock_contention.bpf.c GAPS-004d-AUDIT. */
    __u64 now = bpf_ktime_get_ns();
    __u64 *start = bpf_map_lookup_elem(&softirq_ts, &vec);
    if (start && *start > 0) {
        __u64 elapsed = now - *start;
        counter_add(SOFTIRQ_STOLEN_NS, elapsed);
        __u64 z = 0;
        bpf_map_update_elem(&softirq_ts, &vec, &z, BPF_ANY);
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * THREAD DOMAIN
 * ═══════════════════════════════════════════════════════════════════════ */

SEC("tracepoint/syscalls/sys_exit_clone")
int sense_clone_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target()) return 0;
    if (ctx->ret > 0)
        counter_add(THREADS_CREATED, 1);
    return 0;
}

/* clone3() — modern glibc/musl use this instead of clone() */
SEC("tracepoint/syscalls/sys_exit_clone3")
int sense_clone3_exit(struct trace_event_raw_sys_exit *ctx)
{
    if (!is_target()) return 0;
    if (ctx->ret > 0)
        counter_add(THREADS_CREATED, 1);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * BLOCK I/O DOMAIN — actual disk-level metrics
 * ═══════════════════════════════════════════════════════════════════════ */

SEC("tracepoint/block/block_io_start")
int sense_block_io_start(struct sense_block_io_ctx *ctx)
{
    if (!is_target()) return 0;

    /* Track bytes by direction */
    if (ctx->rwbs[0] == 'R')
        counter_add(DISK_READ_BYTES, (__u64)ctx->bytes);
    else if (ctx->rwbs[0] == 'W')
        counter_add(DISK_WRITE_BYTES, (__u64)ctx->bytes);

    counter_add(DISK_IO_COUNT, 1);

    /* Store timestamp for latency tracking */
    __u64 key = bio_key(ctx->dev, ctx->sector, ctx->bytes);
    __u64 ts = bpf_ktime_get_ns();
    /* LRU_HASH auto-evicts on pressure, but bpf_map_update_elem can
     * still return non-zero (e.g. E2BIG if the key somehow exceeds the
     * configured size). Count those. */
    if (bpf_map_update_elem(&bio_ts, &key, &ts, BPF_ANY) != 0)
        note_map_full();

    return 0;
}

SEC("tracepoint/block/block_io_done")
int sense_block_io_done(struct sense_block_io_ctx *ctx)
{
    /* GAPS-004g-AUDIT-5 (2026-05-04): single bpf_ktime_get_ns() captured
     * once at exit-handler entry, reused for delta. Same fix-class as
     * lock_contention.bpf.c GAPS-004d-AUDIT. */
    __u64 now = bpf_ktime_get_ns();
    __u64 key = bio_key(ctx->dev, ctx->sector, ctx->bytes);
    __u64 *start = bpf_map_lookup_elem(&bio_ts, &key);
    if (start && *start > 0) {
        __u64 elapsed = now - *start;
        counter_add(DISK_IO_LATENCY_NS, elapsed);
    }
    bpf_map_delete_elem(&bio_ts, &key);
    return 0;
}

SEC("tracepoint/block/block_unplug")
int sense_block_unplug(struct sense_block_unplug_ctx *ctx)
{
    if (!is_target()) return 0;
    counter_add(IO_UNPLUG_COUNT, 1);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * PAGE CACHE DOMAIN
 * ═══════════════════════════════════════════════════════════════════════ */

SEC("tracepoint/filemap/mm_filemap_add_to_page_cache")
int sense_page_cache_add(struct sense_filemap_add_ctx *ctx)
{
    if (!is_target()) return 0;
    counter_add(PAGE_CACHE_MISSES, 1);
    return 0;
}

SEC("tracepoint/iomap/iomap_readahead")
int sense_readahead(struct sense_readahead_ctx *ctx)
{
    if (!is_target()) return 0;
    if (ctx->nr_pages > 0)
        counter_add(READAHEAD_PAGES, (__u64)ctx->nr_pages);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * WRITEBACK DOMAIN — dirty page throttling
 * ═══════════════════════════════════════════════════════════════════════ */

SEC("tracepoint/writeback/balance_dirty_pages")
int sense_dirty_pages(struct sense_dirty_pages_ctx *ctx)
{
    if (!is_target()) return 0;
    /* pause > 0 means the kernel throttled our writes (in jiffies) */
    if (ctx->pause > 0)
        counter_add(WRITE_THROTTLE_JIFFIES, (__u64)ctx->pause);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * NETWORK HEALTH DOMAIN
 * ═══════════════════════════════════════════════════════════════════════ */

/* ctx type: none of these are syscalls, and we never read fields from
 * ctx. Using void* documents intent and prevents silent misuse if a
 * future edit tries to dereference ctx->args[] or similar. */
SEC("tracepoint/tcp/tcp_retransmit_skb")
int sense_tcp_retransmit(void *ctx)
{
    /* tcp_retransmit fires in context of the connection owner */
    if (!is_target()) return 0;
    counter_add(TCP_RETRANSMIT_COUNT, 1);
    return 0;
}

SEC("tracepoint/tcp/tcp_send_reset")
int sense_tcp_reset(void *ctx)
{
    /* System-wide: any RST is interesting */
    counter_add(TCP_RST_SENT, 1);
    return 0;
}

SEC("tracepoint/sock/inet_sk_error_report")
int sense_sk_error(void *ctx)
{
    if (!is_target()) return 0;
    counter_add(TCP_ERROR_COUNT, 1);
    return 0;
}

SEC("tracepoint/skb/kfree_skb")
int sense_skb_drop(void *ctx)
{
    /* System-wide packet drop counter */
    counter_add(SKB_DROP_COUNT, 1);
    return 0;
}

/* TCP probe — rate-limited to once per ms per CPU to control overhead */
SEC("tracepoint/tcp/tcp_probe")
int sense_tcp_probe(struct sense_tcp_probe_ctx *ctx)
{
    if (!is_target()) return 0;

    /* Rate limit: once per 1ms per CPU */
    __u32 zero = 0;
    __u64 now = bpf_ktime_get_ns();
    __u64 *last = bpf_map_lookup_elem(&tcp_probe_ts, &zero);
    if (last && (now - *last) < 1000000)
        return 0;
    bpf_map_update_elem(&tcp_probe_ts, &zero, &now, BPF_ANY);

    /* Update srtt min/max and cwnd gauge */
    if (ctx->srtt > 0) {
        counter_min_nz(TCP_MIN_SRTT_US, (__u64)ctx->srtt);
        counter_max(TCP_MAX_SRTT_US, (__u64)ctx->srtt);
    }
    counter_set(TCP_LAST_CWND, (__u64)ctx->snd_cwnd);

    return 0;
}

SEC("tracepoint/tcp/tcp_cong_state_set")
int sense_cong_state(struct sense_cong_state_ctx *ctx)
{
    if (!is_target()) return 0;
    /* Count transitions to Loss state (the worst) */
    if (ctx->cong_state == TCP_CA_Loss)
        counter_add(TCP_CONG_LOSS, 1);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════════
 * RELIABILITY DOMAIN — is the organism sick or dying?
 * ═══════════════════════════════════════════════════════════════════════ */

SEC("tracepoint/signal/signal_generate")
int sense_signal(struct sense_signal_ctx *ctx)
{
    /* Check if the signal targets our process */
    __u32 target = (__u32)ctx->pid;
    if (target_tgid != 0 && target != (__s32)target_tgid)
        return 0;

    /* Record any signal */
    counter_set(SIGNAL_LAST_SIGNO, (__u64)ctx->sig);

    /* Count fatal signals */
    int sig = ctx->sig;
    if (sig == SIGSEGV || sig == SIGBUS || sig == SIGKILL ||
        sig == SIGABRT || sig == SIGFPE)
        counter_add(SIGNAL_FATAL_COUNT, 1);

    return 0;
}

SEC("tracepoint/oom/mark_victim")
int sense_oom_kill(struct sense_oom_victim_ctx *ctx)
{
    /* Count all OOM kills (system-wide awareness) */
    counter_add(OOM_KILLS_SYSTEM, 1);

    /* Flag if WE are the victim */
    if (target_tgid != 0 && (__u32)ctx->pid == target_tgid)
        counter_set(OOM_KILL_US, 1);

    return 0;
}

SEC("tracepoint/oom/reclaim_retry_zone")
int sense_oom_retry(struct sense_reclaim_retry_ctx *ctx)
{
    /* Track worst-case stall loops (system-wide pressure indicator) */
    if (ctx->no_progress_loops > 0)
        counter_max(RECLAIM_STALL_LOOPS, (__u64)ctx->no_progress_loops);
    return 0;
}

SEC("tracepoint/thermal/thermal_zone_trip")
int sense_thermal_trip(struct sense_thermal_trip_ctx *ctx)
{
    /* Track highest trip type: 0=ACTIVE, 1=PASSIVE, 2=HOT, 3=CRITICAL */
    counter_max(THERMAL_MAX_TRIP, (__u64)ctx->trip_type);
    return 0;
}

/* Not a syscall; ctx carries mce_record fields we don't use. */
SEC("tracepoint/mce/mce_record")
int sense_mce(void *ctx)
{
    /* Any MCE is noteworthy — hardware error */
    counter_add(MCE_COUNT, 1);
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
