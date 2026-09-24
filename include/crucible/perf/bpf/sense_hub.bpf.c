/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>

#ifndef BPF_F_MMAPABLE
#define BPF_F_MMAPABLE (1U << 10)
#endif

#define AF_INET 2
#define AF_INET6 10
#define AF_UNIX 1
#define SOCK_STREAM 1
#define SOCK_DGRAM 2
#define IPPROTO_TCP 6
#define TCP_CLOSE 7

#define STYPE_UDP 1
#define STYPE_UNIX 2

#define FUTEX_WAIT 0
#define FUTEX_WAIT_BITSET 9
#define FUTEX_LOCK_PI 6
#define FUTEX_CMD_MASK 127

#define TASK_RUNNING 0

#define MM_FILEPAGES 0
#define MM_ANONPAGES 1
#define MM_SWAPENTS 2
#define MM_SHMEMPAGES 3

#define RECLAIM_WB_ANON 0x0001u

#define TCP_CA_Loss 4

#define SIGBUS 7
#define SIGFPE 8
#define SIGKILL 9
#define SIGSEGV 11
#define SIGABRT 6

/* Userspace rewrites this in .rodata before the program is loaded. */
const volatile __u32 target_tgid = 0;

/*
 * A target_tgid of 0 means the rewrite never happened, and every task then
 * fails this test. The reading comes back empty, which is visible. Treating 0
 * as a wildcard instead would record every tracepoint on the host.
 */
static __always_inline bool is_target(void) {
    __u32 tgid = bpf_get_current_pid_tgid() >> 32;
    return target_tgid != 0 && tgid == target_tgid;
}

static __always_inline __u32 get_tid(void) { return (__u32)bpf_get_current_pid_tgid(); }

static __always_inline __u32 get_tgid(void) { return bpf_get_current_pid_tgid() >> 32; }

/*
 * Userspace mmaps the counter array and indexes it by these values. The order
 * is the shared contract, and the groups of eight are laid out so that one
 * domain occupies one cache line.
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
    FD_CURRENT = 9,
    FD_OPEN_OPS = 10,
    IO_READ_BYTES = 11,
    IO_WRITE_BYTES = 12,
    IO_READ_OPS = 13,
    IO_WRITE_OPS = 14,
    MEM_MMAP_COUNT = 15,

    MEM_MUNMAP_COUNT = 16,
    MEM_PAGE_FAULTS_MIN = 17,
    MEM_PAGE_FAULTS_MAJ = 18,
    MEM_BRK_CALLS = 19,
    SCHED_CTX_VOL = 20,
    SCHED_CTX_INVOL = 21,
    SCHED_MIGRATIONS = 22,
    SCHED_RUNTIME_NS = 23,

    SCHED_WAIT_NS = 24,
    FUTEX_WAIT_COUNT = 25,
    FUTEX_WAIT_NS = 26,
    THREADS_CREATED = 27,
    SCHED_SLEEP_NS = 28,
    SCHED_IOWAIT_NS = 29,
    SCHED_BLOCKED_NS = 30,
    WAKEUPS_RECEIVED = 31,

    KERNEL_LOCK_COUNT = 32,
    KERNEL_LOCK_NS = 33,
    SOFTIRQ_STOLEN_NS = 34,
    THREADS_EXITED = 35,
    CPU_FREQ_CHANGES = 36,
    WAKEUPS_SENT = 37,
    _RESERVED_38 = 38,
    _RESERVED_39 = 39,

    RSS_ANON_BYTES = 40,
    RSS_FILE_BYTES = 41,
    RSS_SWAP_ENTRIES = 42,
    RSS_SHMEM_BYTES = 43,
    DIRECT_RECLAIM_COUNT = 44,
    DIRECT_RECLAIM_NS = 45,
    SWAP_OUT_PAGES = 46,
    THP_COLLAPSE_OK = 47,

    THP_COLLAPSE_FAIL = 48,
    NUMA_MIGRATE_PAGES = 49,
    COMPACTION_STALLS = 50,
    EXTFRAG_EVENTS = 51,
    DISK_READ_BYTES = 52,
    DISK_WRITE_BYTES = 53,
    DISK_IO_LATENCY_NS = 54,
    DISK_IO_COUNT = 55,

    PAGE_CACHE_MISSES = 56,
    READAHEAD_PAGES = 57,
    WRITE_THROTTLE_JIFFIES = 58,
    IO_UNPLUG_COUNT = 59,
    TCP_RETRANSMIT_COUNT = 60,
    TCP_RST_SENT = 61,
    TCP_ERROR_COUNT = 62,
    SKB_DROP_COUNT = 63,

    TCP_MIN_SRTT_US = 64,
    TCP_MAX_SRTT_US = 65,
    TCP_LAST_CWND = 66,
    TCP_CONG_LOSS = 67,
    SIGNAL_FATAL_COUNT = 68,
    SIGNAL_LAST_SIGNO = 69,
    OOM_KILLS_SYSTEM = 70,
    OOM_KILL_US = 71,

    RECLAIM_STALL_LOOPS = 72,
    THERMAL_MAX_TRIP = 73,
    MCE_COUNT = 74,
    MAP_FULL_DROPS = 75, /* map updates that returned non-zero */
    _RESERVED_76 = 76,
    _RESERVED_77 = 77,
    _RESERVED_78 = 78,
    _RESERVED_79 = 79,

    _RESERVED_80 = 80,
    _RESERVED_81 = 81,
    _RESERVED_82 = 82,
    _RESERVED_83 = 83,
    _RESERVED_84 = 84,
    _RESERVED_85 = 85,
    _RESERVED_86 = 86,
    _RESERVED_87 = 87,
    _RESERVED_88 = 88,
    _RESERVED_89 = 89,
    _RESERVED_90 = 90,
    _RESERVED_91 = 91,
    _RESERVED_92 = 92,
    _RESERVED_93 = 93,
    _RESERVED_94 = 94,
    _RESERVED_95 = 95,

    NUM_COUNTERS = 96,
};

struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, NUM_COUNTERS);
    __type(key, __u32);
    __type(value, __u64);
    __uint(map_flags, BPF_F_MMAPABLE);
} counters SEC(".maps");

/* A workload with more live sockets than this map holds would, under a plain
 * HASH, have every later socket silently dropped. LRU_HASH evicts the oldest
 * entry instead. */
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

/* An interrupted socket() call leaves an enter with no exit. LRU_HASH evicts
 * the orphan rather than letting it hold a slot forever. */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 256);
    __type(key, __u32);
    __type(value, struct socket_args);
} socket_enter SEC(".maps");

/* A thread switched out and never seen switching back in leaves an orphan
 * timestamp. LRU_HASH evicts it rather than letting it hold a slot forever. */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, __u64);
} switch_ts SEC(".maps");

/* Userspace fills this map with the tids of the target process, at load time
 * for the main thread and again as threads are created. */
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 4096);
    __type(key, __u32);
    __type(value, __u8);
} our_tids SEC(".maps");

/* An enter with no matching exit leaves an orphan, which LRU_HASH evicts. */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, __u64);
} futex_ts SEC(".maps");

/* An enter with no matching exit leaves an orphan, which LRU_HASH evicts. */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 1024);
    __type(key, __u32);
    __type(value, __u64);
} lock_ts SEC(".maps");

/*
 * One slot per softirq vector, of which the kernel defines ten. A softirq can
 * nest on another on the same CPU, and separate slots keep the outer start
 * timestamp intact while the inner one runs.
 */
struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 10);
    __type(key, __u32);
    __type(value, __u64);
} softirq_ts SEC(".maps");

/* An enter with no matching exit leaves an orphan, which LRU_HASH evicts. */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 256);
    __type(key, __u32);
    __type(value, __u64);
} reclaim_ts SEC(".maps");

/*
 * Two concurrent requests collide here only when they target the same device
 * and the same sector with the same length. Keying by the request pointer
 * would be exact, but the tracepoint exposes only the decoded fields, not the
 * pointer. LRU_HASH bounds the memory even when a driver path completes a
 * request without firing the done tracepoint.
 */
struct {
    __uint(type, BPF_MAP_TYPE_LRU_HASH);
    __uint(max_entries, 4096);
    __type(key, __u64);
    __type(value, __u64);
} bio_ts SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERCPU_ARRAY);
    __uint(max_entries, 1);
    __type(key, __u32);
    __type(value, __u64);
} tcp_probe_ts SEC(".maps");

static __always_inline void counter_add(__u32 idx, __u64 delta) {
    __u64* val = bpf_map_lookup_elem(&counters, &idx);
    if (val) __sync_fetch_and_add(val, delta);
}

static __always_inline void counter_sub(__u32 idx, __u64 delta) {
    __u64* val = bpf_map_lookup_elem(&counters, &idx);
    if (val) __sync_fetch_and_sub(val, delta);
}

/* Not atomic. A lost update on a gauge costs one stale reading. */
static __always_inline void counter_set(__u32 idx, __u64 new_val) {
    __u64* val = bpf_map_lookup_elem(&counters, &idx);
    if (val) *val = new_val;
}

/*
 * A plain read-then-write loses the larger value when two CPUs interleave, so
 * the write commits only if the compared value is still in place. The loop is
 * bounded because the verifier has to see it terminate.
 */
static __always_inline void counter_max(__u32 idx, __u64 new_val) {
    __u64* val = bpf_map_lookup_elem(&counters, &idx);
    if (!val) return;
#pragma unroll
    for (int i = 0; i < 4; i++) {
        __u64 cur = *val;
        if (new_val <= cur) return;
        if (__sync_val_compare_and_swap(val, cur, new_val) == cur) return;
    }
}

/* Zero means unset, so it is never a candidate minimum. */
static __always_inline void counter_min_nz(__u32 idx, __u64 new_val) {
    if (new_val == 0) return;
    __u64* val = bpf_map_lookup_elem(&counters, &idx);
    if (!val) return;
#pragma unroll
    for (int i = 0; i < 4; i++) {
        __u64 cur = *val;
        if (cur != 0 && new_val >= cur) return;
        if (__sync_val_compare_and_swap(val, cur, new_val) == cur) return;
    }
}

/*
 * A close of a descriptor inherited through exec has no matching open on this
 * side, so a plain subtract would wrap the counter to its maximum.
 */
static __always_inline void counter_sub_sat(__u32 idx, __u64 delta) {
    __u64* val = bpf_map_lookup_elem(&counters, &idx);
    if (!val) return;
#pragma unroll
    for (int i = 0; i < 4; i++) {
        __u64 cur = *val;
        __u64 newv = cur > delta ? cur - delta : 0;
        if (__sync_val_compare_and_swap(val, cur, newv) == cur) return;
    }
}

/*
 * Records that tracepoint state is dropped, so a reader can tell an empty
 * result from a lossy one. The LRU maps evict rather than fail, so what
 * reaches here is the plain hash filling up, or a malformed key.
 */
static __always_inline void note_map_full(void) { counter_add(MAP_FULL_DROPS, 1); }

/*
 * Each struct below mirrors one tracepoint format, and the offsets are the
 * kernel's, not this program's. They are hand-rolled where vmlinux.h has no
 * clean type for the event. Where the kernel type does exist, the handler
 * takes it and reads through BPF_CORE_READ so that the offsets relocate at
 * load time.
 */

/* sched/sched_waking */
struct sense_sched_waking_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    char comm[16]; /* offset 8 */
    __s32 pid; /* offset 24 */
    __s32 prio; /* offset 28 */
    __s32 target_cpu; /* offset 32 */
};

/* sched/sched_process_exit */
struct sense_sched_exit_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    char comm[16]; /* offset 8 */
    __s32 pid; /* offset 24 */
    __s32 prio; /* offset 28 */
    __u8 group_dead; /* offset 32 */
};

/* power/cpu_frequency */
struct sense_cpu_freq_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    __u32 state; /* offset 8: frequency in KHz */
    __u32 cpu_id; /* offset 12 */
};

/* lock/contention_begin */
struct sense_lock_begin_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    __u64 lock_addr; /* offset 8 */
    __u32 flags; /* offset 16 */
};

/* lock/contention_end */
struct sense_lock_end_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    __u64 lock_addr; /* offset 8 */
    __s32 ret; /* offset 16 */
};

/* vmscan/mm_vmscan_direct_reclaim_end */
struct sense_reclaim_end_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    unsigned long nr_reclaimed; /* offset 8 */
};

/* vmscan/mm_vmscan_write_folio */
struct sense_write_folio_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    unsigned long pfn; /* offset 8 */
    __s32 reclaim_flags; /* offset 16 */
};

/* huge_memory/mm_collapse_huge_page */
struct sense_thp_collapse_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    __u64 mm; /* offset 8: struct mm_struct* */
    __s32 isolated; /* offset 16 */
    __s32 status; /* offset 20: 1=succeeded */
};

/* migrate/mm_migrate_pages */
struct sense_migrate_pages_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    unsigned long succeeded; /* offset 8 */
    unsigned long failed; /* offset 16 */
    unsigned long thp_succeeded; /* offset 24 */
    unsigned long thp_failed; /* offset 32 */
    unsigned long thp_split; /* offset 40 */
    unsigned long large_folio_split; /* offset 48 */
    __u32 mode; /* offset 56 */
    __s32 reason; /* offset 60: 5=numa_misplaced */
};

/* compaction/mm_compaction_end */
struct sense_compaction_end_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    unsigned long zone_start; /* offset 8 */
    unsigned long migrate_pfn; /* offset 16 */
    unsigned long free_pfn; /* offset 24 */
    unsigned long zone_end; /* offset 32 */
    __u8 sync; /* offset 40 */
    __u8 __pad[3];
    __s32 status; /* offset 44 */
};

/* kmem/mm_page_alloc_extfrag */
struct sense_extfrag_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    unsigned long pfn; /* offset 8 */
    __s32 alloc_order; /* offset 16 */
    __s32 fallback_order; /* offset 20 */
    __s32 alloc_migratetype; /* offset 24 */
    __s32 fallback_migratetype; /* offset 28 */
    __s32 change_ownership; /* offset 32 */
};

/* block/block_io_start, block/block_io_done */
struct sense_block_io_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    __u32 dev; /* offset 8 */
    __u32 __pad;
    __u64 sector; /* offset 16 */
    __u32 nr_sector; /* offset 24 */
    __u32 bytes; /* offset 28 */
    __u16 ioprio; /* offset 32 */
    char rwbs[10]; /* offset 34 */
    char comm[16]; /* offset 44 */
};

/* block/block_unplug */
struct sense_block_unplug_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    __s32 nr_rq; /* offset 8 */
};

/* filemap/mm_filemap_add_to_page_cache */
struct sense_filemap_add_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    unsigned long pfn; /* offset 8 */
    unsigned long i_ino; /* offset 16 */
    unsigned long index; /* offset 24 */
    __u32 s_dev; /* offset 32 */
    __u8 order; /* offset 36 */
};

/* iomap/iomap_readahead */
struct sense_readahead_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    __u32 dev; /* offset 8 */
    __u32 __pad;
    __u64 ino; /* offset 16 */
    __s32 nr_pages; /* offset 24 */
};

/* writeback/balance_dirty_pages (only the fields we need) */
struct sense_dirty_pages_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    char bdi[32]; /* offset 8 */
    unsigned long limit; /* offset 40 */
    unsigned long setpoint; /* offset 48 */
    unsigned long dirty; /* offset 56 */
    unsigned long wb_setpoint; /* offset 64 */
    unsigned long wb_dirty; /* offset 72 */
    unsigned long dirty_ratelimit; /* offset 80 */
    unsigned long task_ratelimit; /* offset 88 */
    __u32 dirtied; /* offset 96 */
    __u32 dirtied_pause; /* offset 100 */
    unsigned long paused; /* offset 104 */
    __s64 pause; /* offset 112: jiffies, >0 = throttled */
};

/* tcp/tcp_probe (only the fields we need) */
struct sense_tcp_probe_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    __u8 saddr[28]; /* offset 8 */
    __u8 daddr[28]; /* offset 36 */
    __u16 sport; /* offset 64 */
    __u16 dport; /* offset 66 */
    __u16 family; /* offset 68 */
    __u32 mark; /* offset 72 */
    __u16 data_len; /* offset 76 */
    __u16 __pad;
    __u32 snd_nxt; /* offset 80 */
    __u32 snd_una; /* offset 84 */
    __u32 snd_cwnd; /* offset 88 */
    __u32 ssthresh; /* offset 92 */
    __u32 snd_wnd; /* offset 96 */
    __u32 srtt; /* offset 100 */
    __u32 rcv_wnd; /* offset 104 */
};

/* tcp/tcp_cong_state_set */
struct sense_cong_state_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    __u64 skaddr; /* offset 8 */
    __u16 sport; /* offset 16 */
    __u16 dport; /* offset 18 */
    __u16 family; /* offset 20 */
    __u8 saddr[4]; /* offset 22 */
    __u8 daddr[4]; /* offset 26 */
    __u8 saddr_v6[16]; /* offset 30 */
    __u8 daddr_v6[16]; /* offset 46 */
    __u8 cong_state; /* offset 62 */
};

/* signal/signal_generate */
struct sense_signal_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    __s32 sig; /* offset 8 */
    __s32 err; /* offset 12 */
    __s32 code; /* offset 16 */
    char comm[16]; /* offset 20 */
    __s32 pid; /* offset 36: target PID */
    __s32 group; /* offset 40 */
    __s32 result; /* offset 44 */
};

/* oom/mark_victim */
struct sense_oom_victim_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    __s32 pid; /* offset 8 */
};

/* oom/reclaim_retry_zone */
struct sense_reclaim_retry_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    __s32 node; /* offset 8 */
    __s32 zone_idx; /* offset 12 */
    __s32 order; /* offset 16 */
    __u32 __pad;
    unsigned long reclaimable; /* offset 24 */
    unsigned long available; /* offset 32 */
    unsigned long min_wmark; /* offset 40 */
    __s32 no_progress_loops; /* offset 48 */
};

/* thermal/thermal_zone_trip */
struct sense_thermal_trip_ctx {
    __u16 common_type;
    __u8 common_flags;
    __u8 common_preempt_count;
    __s32 common_pid;
    __u32 __data_loc_name; /* offset 8 */
    __s32 id; /* offset 12 */
    __s32 trip; /* offset 16 */
    __u32 trip_type; /* offset 20: 0=ACTIVE,1=PASSIVE,2=HOT,3=CRITICAL */
};

static __always_inline __u32 tcp_state_to_idx(int state) {
    switch (state) {
        case 1:
            return NET_TCP_ESTABLISHED;
        case 10:
            return NET_TCP_LISTEN;
        case 6:
            return NET_TCP_TIME_WAIT;
        case 8:
            return NET_TCP_CLOSE_WAIT;
        default:
            return NET_TCP_OTHER;
    }
}

/*
 * Folding the length in as well as the device and sector separates two
 * requests that share a start sector but differ in size. The result is not
 * collision free.
 */
static __always_inline __u64 bio_key(__u32 dev, __u64 sector, __u32 bytes) {
    return (sector << 20) ^ ((__u64)dev << 52) ^ (__u64)bytes;
}

SEC("tracepoint/sock/inet_sock_set_state")
int sense_tcp_state(struct trace_event_raw_inet_sock_set_state* ctx) {
    if (ctx->protocol != IPPROTO_TCP) return 0;

    __u32 tgid = bpf_get_current_pid_tgid() >> 32;
    /* A tgid of 0 is allowed through because a TCP state change can fire from
     * softirq, where the current task is the idle task rather than the socket
     * owner. A target_tgid of 0 still rejects everything. */
    if (target_tgid == 0) return 0;
    if (tgid != target_tgid && tgid != 0) return 0;

    if (ctx->oldstate != TCP_CLOSE) counter_sub_sat(tcp_state_to_idx(ctx->oldstate), 1);
    if (ctx->newstate != TCP_CLOSE) counter_add(tcp_state_to_idx(ctx->newstate), 1);

    return 0;
}

SEC("tracepoint/syscalls/sys_enter_socket")
int sense_socket_enter(struct trace_event_raw_sys_enter* ctx) {
    if (!is_target()) return 0;
    __u32 tid = get_tid();
    struct socket_args args = {
        .domain = (__u32)ctx->args[0],
        .type = (__u32)ctx->args[1] & 0xFF,
    };
    if (bpf_map_update_elem(&socket_enter, &tid, &args, BPF_ANY) != 0) note_map_full();
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_socket")
int sense_socket_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;

    __u32 tid = get_tid();
    struct socket_args* args = bpf_map_lookup_elem(&socket_enter, &tid);
    if (!args) return 0;

    struct socket_args local = *args;
    bpf_map_delete_elem(&socket_enter, &tid);

    long fd = ctx->ret;
    if (fd < 0) return 0;

    __u32 ufd = (__u32)fd;
    if (local.domain == AF_INET || local.domain == AF_INET6) {
        if (local.type == SOCK_DGRAM) {
            __u8 stype = STYPE_UDP;
            if (bpf_map_update_elem(&socket_fds, &ufd, &stype, BPF_ANY) != 0) note_map_full();
            counter_add(NET_UDP_ACTIVE, 1);
        }
    } else if (local.domain == AF_UNIX) {
        __u8 stype = STYPE_UNIX;
        if (bpf_map_update_elem(&socket_fds, &ufd, &stype, BPF_ANY) != 0) note_map_full();
        counter_add(NET_UNIX_ACTIVE, 1);
    }

    counter_add(FD_CURRENT, 1);
    counter_add(FD_OPEN_OPS, 1);
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_sendto")
int sense_sendto_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;
    if (ctx->ret > 0) counter_add(NET_TX_BYTES, (__u64)ctx->ret);
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_sendmsg")
int sense_sendmsg_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;
    if (ctx->ret > 0) counter_add(NET_TX_BYTES, (__u64)ctx->ret);
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_recvfrom")
int sense_recvfrom_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;
    if (ctx->ret > 0) counter_add(NET_RX_BYTES, (__u64)ctx->ret);
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_recvmsg")
int sense_recvmsg_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;
    if (ctx->ret > 0) counter_add(NET_RX_BYTES, (__u64)ctx->ret);
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_read")
int sense_read_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;
    if (ctx->ret > 0) {
        counter_add(IO_READ_BYTES, (__u64)ctx->ret);
        counter_add(IO_READ_OPS, 1);
    }
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_readv")
int sense_readv_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;
    if (ctx->ret > 0) {
        counter_add(IO_READ_BYTES, (__u64)ctx->ret);
        counter_add(IO_READ_OPS, 1);
    }
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_write")
int sense_write_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;
    if (ctx->ret > 0) {
        counter_add(IO_WRITE_BYTES, (__u64)ctx->ret);
        counter_add(IO_WRITE_OPS, 1);
    }
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_writev")
int sense_writev_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;
    if (ctx->ret > 0) {
        counter_add(IO_WRITE_BYTES, (__u64)ctx->ret);
        counter_add(IO_WRITE_OPS, 1);
    }
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_openat")
int sense_openat_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;
    if (ctx->ret >= 0) {
        counter_add(FD_CURRENT, 1);
        counter_add(FD_OPEN_OPS, 1);
    }
    return 0;
}

SEC("tracepoint/syscalls/sys_enter_close")
int sense_close_enter(struct trace_event_raw_sys_enter* ctx) {
    if (!is_target()) return 0;

    __u32 fd = (__u32)ctx->args[0];
    /* A descriptor inherited through exec is closed here without ever having
     * been counted at open. */
    counter_sub_sat(FD_CURRENT, 1);

    __u8* stype = bpf_map_lookup_elem(&socket_fds, &fd);
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

SEC("tracepoint/syscalls/sys_exit_mmap")
int sense_mmap_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;
    if (ctx->ret >= 0) counter_add(MEM_MMAP_COUNT, 1);
    return 0;
}

SEC("tracepoint/syscalls/sys_enter_munmap")
int sense_munmap_enter(struct trace_event_raw_sys_enter* ctx) {
    if (!is_target()) return 0;
    counter_add(MEM_MUNMAP_COUNT, 1);
    return 0;
}

/* exceptions/page_fault_user */
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
 * Every user fault counts as minor. Bit 0 of the x86 error code says whether
 * the page table entry was present, which separates a copy-on-write or
 * protection fault from an absent one, and neither of those is the same
 * question as whether the fault needed backing store. MEM_PAGE_FAULTS_MAJ
 * stays at zero.
 */
SEC("tracepoint/exceptions/page_fault_user")
int sense_page_fault(struct trace_event_raw_page_fault_user* ctx) {
    if (!is_target()) return 0;
    counter_add(MEM_PAGE_FAULTS_MIN, 1);
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_brk")
int sense_brk_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;
    counter_add(MEM_BRK_CALLS, 1);
    return 0;
}

/*
 * The field layout of this tracepoint changed twice between kernel 5.18 and
 * 6.2, first gaining a field and then changing the type of member. Reading
 * through BPF_CORE_READ relocates the offsets at load time, so one build
 * covers that range.
 */
SEC("tracepoint/kmem/rss_stat")
int sense_rss_stat(struct trace_event_raw_rss_stat* ctx) {
    if (!is_target()) return 0;

    int member = BPF_CORE_READ(ctx, member);
    long size = BPF_CORE_READ(ctx, size);

    /* The kernel reports a byte count that goes negative on a decrement. */
    __u64 abs_size = size >= 0 ? (__u64)size : 0;
    switch (member) {
        case MM_FILEPAGES:
            counter_set(RSS_FILE_BYTES, abs_size);
            break;
        case MM_ANONPAGES:
            counter_set(RSS_ANON_BYTES, abs_size);
            break;
        case MM_SWAPENTS:
            counter_set(RSS_SWAP_ENTRIES, abs_size);
            break;
        case MM_SHMEMPAGES:
            counter_set(RSS_SHMEM_BYTES, abs_size);
            break;
    }
    return 0;
}

/* The parameter is void* on purpose. This is not a syscall tracepoint, so a
 * syscall context type would name fields that are not there. */
SEC("tracepoint/vmscan/mm_vmscan_direct_reclaim_begin")
int sense_reclaim_begin(void* ctx) {
    if (!is_target()) return 0;
    __u32 tid = get_tid();
    __u64 ts = bpf_ktime_get_ns();
    if (bpf_map_update_elem(&reclaim_ts, &tid, &ts, BPF_ANY) != 0) note_map_full();
    counter_add(DIRECT_RECLAIM_COUNT, 1);
    return 0;
}

SEC("tracepoint/vmscan/mm_vmscan_direct_reclaim_end")
int sense_reclaim_end(struct sense_reclaim_end_ctx* ctx) {
    if (!is_target()) return 0;
    __u64 now = bpf_ktime_get_ns();
    __u32 tid = get_tid();
    __u64* start = bpf_map_lookup_elem(&reclaim_ts, &tid);
    if (start && *start > 0) {
        __u64 elapsed = now - *start;
        counter_add(DIRECT_RECLAIM_NS, elapsed);
    }
    bpf_map_delete_elem(&reclaim_ts, &tid);
    return 0;
}

/* Not filtered to the target. Anonymous pages going out to swap is memory
 * pressure on the whole host, whoever owns the pages. */
SEC("tracepoint/vmscan/mm_vmscan_write_folio")
int sense_write_folio(struct sense_write_folio_ctx* ctx) {
    if (ctx->reclaim_flags & RECLAIM_WB_ANON) counter_add(SWAP_OUT_PAGES, 1);
    return 0;
}

SEC("tracepoint/huge_memory/mm_collapse_huge_page")
int sense_thp_collapse(struct sense_thp_collapse_ctx* ctx) {
    /* status 1 is success */
    if (ctx->status == 1)
        counter_add(THP_COLLAPSE_OK, 1);
    else
        counter_add(THP_COLLAPSE_FAIL, 1);
    return 0;
}

SEC("tracepoint/migrate/mm_migrate_pages")
int sense_migrate_pages(struct sense_migrate_pages_ctx* ctx) {
    /* reason 5 is numa_misplaced */
    if (ctx->reason == 5) counter_add(NUMA_MIGRATE_PAGES, ctx->succeeded + ctx->thp_succeeded);
    return 0;
}

SEC("tracepoint/compaction/mm_compaction_end")
int sense_compaction_end(struct sense_compaction_end_ctx* ctx) {
    /* sync == true means synchronous compaction (blocking the thread) */
    if (ctx->sync) counter_add(COMPACTION_STALLS, 1);
    return 0;
}

SEC("tracepoint/kmem/mm_page_alloc_extfrag")
int sense_extfrag(struct sense_extfrag_ctx* ctx) {
    if (ctx->change_ownership) counter_add(EXTFRAG_EVENTS, 1);
    return 0;
}

/*
 * This one runs for every switch on the host, not only for the target. The
 * two map lookups are the filter. A separate flag saying the tid map is
 * populated would gate them earlier, but it would also open a window where
 * the flag and the map disagree.
 */
SEC("tracepoint/sched/sched_switch")
int sense_sched_switch(struct trace_event_raw_sched_switch* ctx) {
    __u64 now = bpf_ktime_get_ns();
    __u32 prev_pid = ctx->prev_pid;
    __u32 next_pid = ctx->next_pid;

    __u8* is_prev = bpf_map_lookup_elem(&our_tids, &prev_pid);
    if (is_prev) {
        if (ctx->prev_state == TASK_RUNNING)
            counter_add(SCHED_CTX_INVOL, 1);
        else
            counter_add(SCHED_CTX_VOL, 1);
        if (bpf_map_update_elem(&switch_ts, &prev_pid, &now, BPF_ANY) != 0) note_map_full();
    }

    __u8* is_next = bpf_map_lookup_elem(&our_tids, &next_pid);
    if (is_next) {
        __u64* ts = bpf_map_lookup_elem(&switch_ts, &next_pid);
        if (ts && *ts > 0) {
            counter_add(SCHED_WAIT_NS, now - *ts);
            __u64 zero = 0;
            bpf_map_update_elem(&switch_ts, &next_pid, &zero, BPF_ANY);
        }
    }
    return 0;
}

SEC("tracepoint/sched/sched_migrate_task")
int sense_migrate(struct trace_event_raw_sched_migrate_task* ctx) {
    __u32 pid = ctx->pid;
    __u8* is_ours = bpf_map_lookup_elem(&our_tids, &pid);
    if (is_ours) counter_add(SCHED_MIGRATIONS, 1);
    return 0;
}

SEC("tracepoint/sched/sched_stat_runtime")
int sense_runtime(struct trace_event_raw_sched_stat_runtime* ctx) {
    if (!is_target()) return 0;
    counter_add(SCHED_RUNTIME_NS, ctx->runtime);
    return 0;
}

/*
 * The three handlers below stay silent unless the kernel is built with
 * CONFIG_SCHEDSTATS and sched_schedstats is on at run time. They take the
 * kernel's own context type and read through BPF_CORE_READ, so a field
 * reordering relocates rather than corrupting the read.
 */

SEC("tracepoint/sched/sched_stat_sleep")
int sense_stat_sleep(struct trace_event_raw_sched_stat_template* ctx) {
    __u32 pid = (__u32)BPF_CORE_READ(ctx, pid);
    __u8* is_ours = bpf_map_lookup_elem(&our_tids, &pid);
    if (is_ours) counter_add(SCHED_SLEEP_NS, BPF_CORE_READ(ctx, delay));
    return 0;
}

SEC("tracepoint/sched/sched_stat_iowait")
int sense_stat_iowait(struct trace_event_raw_sched_stat_template* ctx) {
    __u32 pid = (__u32)BPF_CORE_READ(ctx, pid);
    __u8* is_ours = bpf_map_lookup_elem(&our_tids, &pid);
    if (is_ours) counter_add(SCHED_IOWAIT_NS, BPF_CORE_READ(ctx, delay));
    return 0;
}

SEC("tracepoint/sched/sched_stat_blocked")
int sense_stat_blocked(struct trace_event_raw_sched_stat_template* ctx) {
    __u32 pid = (__u32)BPF_CORE_READ(ctx, pid);
    __u8* is_ours = bpf_map_lookup_elem(&our_tids, &pid);
    if (is_ours) counter_add(SCHED_BLOCKED_NS, BPF_CORE_READ(ctx, delay));
    return 0;
}

SEC("tracepoint/sched/sched_waking")
int sense_waking(struct sense_sched_waking_ctx* ctx) {
    __u32 wakee = (__u32)ctx->pid;
    __u32 waker = get_tid();

    __u8* wakee_ours = bpf_map_lookup_elem(&our_tids, &wakee);
    if (wakee_ours) counter_add(WAKEUPS_RECEIVED, 1);

    __u8* waker_ours = bpf_map_lookup_elem(&our_tids, &waker);
    if (waker_ours) counter_add(WAKEUPS_SENT, 1);

    return 0;
}

SEC("tracepoint/sched/sched_process_exit")
int sense_process_exit(struct sense_sched_exit_ctx* ctx) {
    __u32 pid = (__u32)ctx->pid;
    __u8* is_ours = bpf_map_lookup_elem(&our_tids, &pid);
    if (is_ours) {
        counter_add(THREADS_EXITED, 1);
        bpf_map_delete_elem(&our_tids, &pid);
        bpf_map_delete_elem(&switch_ts, &pid);
        bpf_map_delete_elem(&futex_ts, &pid);
        bpf_map_delete_elem(&lock_ts, &pid);
        bpf_map_delete_elem(&reclaim_ts, &pid);
    }
    return 0;
}

/* Not filtered to the target. A frequency change moves the clock for every
 * task on that CPU. */
SEC("tracepoint/power/cpu_frequency")
int sense_cpu_freq(struct sense_cpu_freq_ctx* ctx) {
    counter_add(CPU_FREQ_CHANGES, 1);
    return 0;
}

SEC("tracepoint/syscalls/sys_enter_futex")
int sense_futex_enter(struct trace_event_raw_sys_enter* ctx) {
    if (!is_target()) return 0;

    __u32 op = (__u32)ctx->args[1] & FUTEX_CMD_MASK;
    if (op != FUTEX_WAIT && op != FUTEX_WAIT_BITSET && op != FUTEX_LOCK_PI) return 0;

    __u32 tid = get_tid();
    __u64 ts = bpf_ktime_get_ns();
    if (bpf_map_update_elem(&futex_ts, &tid, &ts, BPF_ANY) != 0) note_map_full();
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_futex")
int sense_futex_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;

    __u64 now = bpf_ktime_get_ns();
    __u32 tid = get_tid();
    __u64* enter_ts = bpf_map_lookup_elem(&futex_ts, &tid);
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

SEC("tracepoint/lock/contention_begin")
int sense_lock_begin(struct sense_lock_begin_ctx* ctx) {
    if (!is_target()) return 0;
    __u32 tid = get_tid();
    __u64 ts = bpf_ktime_get_ns();
    if (bpf_map_update_elem(&lock_ts, &tid, &ts, BPF_ANY) != 0) note_map_full();
    return 0;
}

SEC("tracepoint/lock/contention_end")
int sense_lock_end(struct sense_lock_end_ctx* ctx) {
    if (!is_target()) return 0;
    __u64 now = bpf_ktime_get_ns();
    __u32 tid = get_tid();
    __u64* start = bpf_map_lookup_elem(&lock_ts, &tid);
    if (start && *start > 0) {
        __u64 elapsed = now - *start;
        counter_add(KERNEL_LOCK_COUNT, 1);
        counter_add(KERNEL_LOCK_NS, elapsed);
    }
    bpf_map_delete_elem(&lock_ts, &tid);
    return 0;
}

/*
 * A softirq runs in interrupt context on whichever task happened to be current
 * when the interrupt arrived, so bpf_get_current_pid_tgid() names that task
 * and not the owner of the work. Filtering on it here would be wrong, and
 * SOFTIRQ_STOLEN_NS is a per-CPU total summed across CPUs rather than a
 * per-process figure.
 */

SEC("tracepoint/irq/softirq_entry")
int sense_softirq_entry(struct trace_event_raw_softirq* ctx) {
    __u32 vec = BPF_CORE_READ(ctx, vec);
    if (vec >= 10) return 0; /* the kernel defines NR_SOFTIRQS vectors */
    __u64 ts = bpf_ktime_get_ns();
    if (bpf_map_update_elem(&softirq_ts, &vec, &ts, BPF_ANY) != 0) note_map_full();
    return 0;
}

SEC("tracepoint/irq/softirq_exit")
int sense_softirq_exit(struct trace_event_raw_softirq* ctx) {
    __u32 vec = BPF_CORE_READ(ctx, vec);
    if (vec >= 10) return 0;
    __u64 now = bpf_ktime_get_ns();
    __u64* start = bpf_map_lookup_elem(&softirq_ts, &vec);
    if (start && *start > 0) {
        __u64 elapsed = now - *start;
        counter_add(SOFTIRQ_STOLEN_NS, elapsed);
        __u64 z = 0;
        bpf_map_update_elem(&softirq_ts, &vec, &z, BPF_ANY);
    }
    return 0;
}

SEC("tracepoint/syscalls/sys_exit_clone")
int sense_clone_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;
    if (ctx->ret > 0) counter_add(THREADS_CREATED, 1);
    return 0;
}

/* Current C libraries call clone3 rather than clone, so both are hooked. */
SEC("tracepoint/syscalls/sys_exit_clone3")
int sense_clone3_exit(struct trace_event_raw_sys_exit* ctx) {
    if (!is_target()) return 0;
    if (ctx->ret > 0) counter_add(THREADS_CREATED, 1);
    return 0;
}

SEC("tracepoint/block/block_io_start")
int sense_block_io_start(struct sense_block_io_ctx* ctx) {
    if (!is_target()) return 0;

    if (ctx->rwbs[0] == 'R')
        counter_add(DISK_READ_BYTES, (__u64)ctx->bytes);
    else if (ctx->rwbs[0] == 'W')
        counter_add(DISK_WRITE_BYTES, (__u64)ctx->bytes);

    counter_add(DISK_IO_COUNT, 1);

    __u64 key = bio_key(ctx->dev, ctx->sector, ctx->bytes);
    __u64 ts = bpf_ktime_get_ns();
    /* The map evicts under pressure rather than failing, so a non-zero return
     * here means a malformed key rather than a full map. */
    if (bpf_map_update_elem(&bio_ts, &key, &ts, BPF_ANY) != 0) note_map_full();

    return 0;
}

SEC("tracepoint/block/block_io_done")
int sense_block_io_done(struct sense_block_io_ctx* ctx) {
    __u64 now = bpf_ktime_get_ns();
    __u64 key = bio_key(ctx->dev, ctx->sector, ctx->bytes);
    __u64* start = bpf_map_lookup_elem(&bio_ts, &key);
    if (start && *start > 0) {
        __u64 elapsed = now - *start;
        counter_add(DISK_IO_LATENCY_NS, elapsed);
    }
    bpf_map_delete_elem(&bio_ts, &key);
    return 0;
}

SEC("tracepoint/block/block_unplug")
int sense_block_unplug(struct sense_block_unplug_ctx* ctx) {
    if (!is_target()) return 0;
    counter_add(IO_UNPLUG_COUNT, 1);
    return 0;
}

SEC("tracepoint/filemap/mm_filemap_add_to_page_cache")
int sense_page_cache_add(struct sense_filemap_add_ctx* ctx) {
    if (!is_target()) return 0;
    counter_add(PAGE_CACHE_MISSES, 1);
    return 0;
}

SEC("tracepoint/iomap/iomap_readahead")
int sense_readahead(struct sense_readahead_ctx* ctx) {
    if (!is_target()) return 0;
    if (ctx->nr_pages > 0) counter_add(READAHEAD_PAGES, (__u64)ctx->nr_pages);
    return 0;
}

SEC("tracepoint/writeback/balance_dirty_pages")
int sense_dirty_pages(struct sense_dirty_pages_ctx* ctx) {
    if (!is_target()) return 0;
    if (ctx->pause > 0) counter_add(WRITE_THROTTLE_JIFFIES, (__u64)ctx->pause);
    return 0;
}

/* The parameter is void* on purpose in the handlers below. None of them is a
 * syscall tracepoint, so a syscall context type would name fields that are not
 * there.
 *
 * A retransmit fires in the context of the task that owns the connection, so
 * the target test is meaningful here. */
SEC("tracepoint/tcp/tcp_retransmit_skb")
int sense_tcp_retransmit(void* ctx) {
    if (!is_target()) return 0;
    counter_add(TCP_RETRANSMIT_COUNT, 1);
    return 0;
}

SEC("tracepoint/tcp/tcp_send_reset")
int sense_tcp_reset(void* ctx) {
    counter_add(TCP_RST_SENT, 1);
    return 0;
}

SEC("tracepoint/sock/inet_sk_error_report")
int sense_sk_error(void* ctx) {
    if (!is_target()) return 0;
    counter_add(TCP_ERROR_COUNT, 1);
    return 0;
}

SEC("tracepoint/skb/kfree_skb")
int sense_skb_drop(void* ctx) {
    counter_add(SKB_DROP_COUNT, 1);
    return 0;
}

SEC("tracepoint/tcp/tcp_probe")
int sense_tcp_probe(struct sense_tcp_probe_ctx* ctx) {
    if (!is_target()) return 0;

    /* At most one sample per millisecond per CPU. */
    __u32 zero = 0;
    __u64 now = bpf_ktime_get_ns();
    __u64* last = bpf_map_lookup_elem(&tcp_probe_ts, &zero);
    if (last && (now - *last) < 1000000) return 0;
    bpf_map_update_elem(&tcp_probe_ts, &zero, &now, BPF_ANY);

    if (ctx->srtt > 0) {
        counter_min_nz(TCP_MIN_SRTT_US, (__u64)ctx->srtt);
        counter_max(TCP_MAX_SRTT_US, (__u64)ctx->srtt);
    }
    counter_set(TCP_LAST_CWND, (__u64)ctx->snd_cwnd);

    return 0;
}

SEC("tracepoint/tcp/tcp_cong_state_set")
int sense_cong_state(struct sense_cong_state_ctx* ctx) {
    if (!is_target()) return 0;
    if (ctx->cong_state == TCP_CA_Loss) counter_add(TCP_CONG_LOSS, 1);
    return 0;
}

SEC("tracepoint/signal/signal_generate")
int sense_signal(struct sense_signal_ctx* ctx) {
    __u32 target = (__u32)ctx->pid;
    if (target_tgid != 0 && target != (__s32)target_tgid) return 0;

    counter_set(SIGNAL_LAST_SIGNO, (__u64)ctx->sig);

    int sig = ctx->sig;
    if (sig == SIGSEGV || sig == SIGBUS || sig == SIGKILL || sig == SIGABRT || sig == SIGFPE)
        counter_add(SIGNAL_FATAL_COUNT, 1);

    return 0;
}

SEC("tracepoint/oom/mark_victim")
int sense_oom_kill(struct sense_oom_victim_ctx* ctx) {
    counter_add(OOM_KILLS_SYSTEM, 1);

    if (target_tgid != 0 && (__u32)ctx->pid == target_tgid) counter_set(OOM_KILL_US, 1);

    return 0;
}

SEC("tracepoint/oom/reclaim_retry_zone")
int sense_oom_retry(struct sense_reclaim_retry_ctx* ctx) {
    if (ctx->no_progress_loops > 0) counter_max(RECLAIM_STALL_LOOPS, (__u64)ctx->no_progress_loops);
    return 0;
}

SEC("tracepoint/thermal/thermal_zone_trip")
int sense_thermal_trip(struct sense_thermal_trip_ctx* ctx) {
    counter_max(THERMAL_MAX_TRIP, (__u64)ctx->trip_type);
    return 0;
}

SEC("tracepoint/mce/mce_record")
int sense_mce(void* ctx) {
    counter_add(MCE_COUNT, 1);
    return 0;
}

char LICENSE[] SEC("license") = "Dual BSD/GPL";
