# Extension: `sense_hub.bpf.c` — fold 14 cheap tracepoints

**STATUS**: doc-only stub.  In-place extension to existing
`sense_hub.bpf.c` (no new file, ~14 new SEC programs added).
Tier-A.

## Problem

SenseHub today has 96 counter slots, 75 used.  Adding cheap
single-tracepoint single-counter handlers is essentially free
(both for build + runtime) and unlocks "weird outcome" diagnosis
that's currently invisible.  These are all "fire-and-forget
counter bumps" — no per-event timeline, no aggregation, just a
delta available at `read()` time.

## The 14 additions

Each wires to one tracepoint and bumps one (or two) `Idx` slots.
Cost per event: ~30-100 ns (one map_lookup + one
`__sync_fetch_and_add`).  All gated by `is_target()` where
applicable.

| Tracepoint | Idx slot(s) added | Why |
|---|---|---|
| `tlb/tlb_flush` | `TLB_SHOOTDOWNS` | Cross-CPU IPI count: another core's mmap/munmap stole 1-5 µs from us via cross-CPU TLB invalidate.  Currently invisible. |
| `mmap_lock/mmap_lock_acquire_returned` | `MMAP_LOCK_WAITS`, `MMAP_LOCK_NS` | Page-fault handler / mmap call on another thread held mm->mmap_sem → bench thread's faults stalled.  Kernel 5.16+. |
| `vmscan/mm_vmscan_kswapd_wake` | `KSWAPD_WAKES` | kswapd kicked in during our bench → memory pressure stole bandwidth.  Distinct from `DIRECT_RECLAIM_*` (sync) — kswapd is async. |
| `timer/hrtimer_expire_entry` | `HRTIMER_FIRES` | High-frequency hrtimer (a profiler running concurrent? a test harness leak?) → wakeups that look like preemptions in our SchedSwitch view. |
| `irq/irq_handler_entry` + `irq/irq_handler_exit` | `HARDIRQ_COUNT`, `HARDIRQ_NS` | Aggregate hardware IRQ time stolen (sibling to existing `SOFTIRQ_STOLEN_NS`).  Per-IRQ breakdown is in the standalone `hardirq.bpf.c` Tier-B program. |
| `iommu/io_page_fault` | `IOMMU_FAULTS` | Process tried to DMA from unmapped page.  Should be zero in healthy compute; non-zero = serious bug. |
| `ras/aer_uncorrectable_error` + `ras/aer_correctable_error` | `PCIE_AER_CORR`, `PCIE_AER_UNCORR` | PCIe AER (Advanced Error Reporting) → flaky link to GPU/NIC.  Reliability signal. |
| `edac/memory_failure_event` | `EDAC_DRAM_CE` | DRAM correctable error (channel-attributed via the standalone if needed).  Reliability signal — high CE rate predicts UE. |
| `sched/sched_process_fork` | `PROCESS_FORKS` | Fork rate.  Pairs with existing `THREADS_CREATED` (clone) — fork-bomb-style behavior detection. |
| `sched/sched_process_exec` | `PROCESS_EXECS` | exec() rate.  Should be zero during steady-state bench. |
| `signal/signal_deliver` | `SIGNAL_DELIVERED` | Signal actually delivered (vs SenseHub's existing `SIGNAL_LAST_SIGNO` / `SIGNAL_FATAL_COUNT` which track generation). |
| `migrate/mm_migrate_pages` (per-reason discriminate) | `NUMA_MIGRATE_PAGES_NUMA_HINT`, `NUMA_MIGRATE_PAGES_OTHER` | Existing `NUMA_MIGRATE_PAGES` aggregates.  Split by reason 5 (numa_misplaced) vs other reasons. |
| `mm_filemap_delete_from_page_cache` | `PAGE_CACHE_EVICTIONS` | Page-cache eviction rate.  Pairs with existing `PAGE_CACHE_MISSES` (adds).  Eviction-heavy workload = cache thrash. |
| `kmem/kmem_cache_alloc` (1 sample per N for cost) + `_free` | `SLAB_ALLOCS`, `SLAB_FREES` | Aggregate slab churn.  Per-cache breakdown in standalone `slab_allocator.bpf.c`. |
| `skb/kfree_skb` ENRICHMENT | rebucket existing `SKB_DROP_COUNT` into ~10 reason buckets (use `skb_drop_reason` enum, kernel 5.18+) | We have a single drop count today; the kernel has been emitting ~80 distinct reasons since 5.18. |

## Index allocation

- Slots 75-89 currently `_RESERVED_*` — claim 14 of these for the
  new counters.
- Update `enum sense_idx` in `sense_hub.bpf.c` AND mirror in
  `SenseHub.h`'s `Idx` enum (wire-compat).
- Update `crucible::perf::Snapshot::operator-` if any of these
  are gauges (none should be; all are deltas).
- Update `bench/bench_harness.h`'s `kAll` table to display the
  ones worth showing inline (`tlb_shoot=N`, `mmlock=N`, `kswap=N`,
  `iommu=N`, etc.).

## skb_drop_reason rebucket

The kernel 5.18+ `skb/kfree_skb` tracepoint exposes a `reason`
field (enum `skb_drop_reason`).  ~80 reasons including:

```
SKB_DROP_REASON_NOT_SPECIFIED
SKB_DROP_REASON_NO_SOCKET
SKB_DROP_REASON_TCP_INVALID_SEQUENCE
SKB_DROP_REASON_TCP_RESET
SKB_DROP_REASON_TCP_OFOMERGE
SKB_DROP_REASON_NETFILTER_DROP
SKB_DROP_REASON_NEIGH_FAILED
SKB_DROP_REASON_PROTO_MEM       (running out of TCP memory)
... ~75 more
```

Bucket strategy: reserve 10 hot buckets (NEIGH_*, NETFILTER_*,
TCP_INVALID_*, PROTO_MEM, RX_NO_NETDEV, etc.) and one OTHER bucket;
attribute by switch on `ctx->reason` truncated to the bucket.

## Cost

Net: ~14 additional tracepoint handlers in SenseHub.  Total cost
add at ~10K events/sec aggregate across all 14: ~140K ops/sec
× 50 ns each = 7 ms/sec = 0.7% CPU.  Within budget for an
always-on sensory layer.

The skb_drop_reason rebucket has the highest event rate
(potentially 100K-1M skb drops/sec on a busy network host) — gate
behind a switch (default ON for compute hosts; OFF for routers).

## Implementation TODO

1. Allocate Idx values 75-89 in both `sense_hub.bpf.c` and
   `SenseHub.h` (must match — wire contract).
2. Author 14 new SEC tracepoint handlers in `sense_hub.bpf.c`
   (mirror existing pattern: `is_target()` gate where applicable,
   `counter_add(IDX, 1)` for counts, `counter_add(IDX, delta)` for
   ns measurements).
3. Update `Snapshot` static_assert size: `NUM_COUNTERS * 8` should
   stay 768 (we're using reserved slots, not adding new ones).
4. Update `bench_harness.h` `kAll` to display the new fields.
5. Update sentinel test to assert each new Idx survives a load+
   diff cycle.

## Sibling refs

Subsumes: nothing (additive).
Promotes-to-standalone: `hardirq.bpf.c` (per-IRQ breakdown),
`slab_allocator.bpf.c` (per-cache breakdown), `mmap_lock.bpf.c`
(timeline grade with per-(mm,op) attribution).
