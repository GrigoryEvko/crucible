# `include/crucible/perf/bpf/_planned/`

**Doc-only stubs for the post-GAPS-004{a,b,c} observability menu.**

These files are NOT in the build.  CMake's `crucible_bpf_program(...)`
calls in the top-level `CMakeLists.txt` use explicit paths
(`include/crucible/perf/bpf/<name>.bpf.c`), so the leading underscore
`_planned/` directory is invisible to the build.  Each stub ships
with:

- SPDX header (matching the convention of the shipped programs)
- Comprehensive design doc covering: PROBLEM / MECHANISM / MAPS /
  WIRE CONTRACT / COST MODEL / KNOWN LIMITS / SIBLING REFS
- Minimal stub body with `TODO: implement` placeholder
- License footer (so the file looks structurally complete)

When implementing one of these:

1. MOVE the file out of `_planned/` to its sibling slot (e.g.,
   `_planned/mmap_lock.bpf.c` → `mmap_lock.bpf.c`)
2. Replace the stub body with a real implementation following the
   `sched_switch.bpf.c` / `pmu_sample.bpf.c` pattern
3. Add the matching `crucible_bpf_program(<name>
   include/crucible/perf/bpf/<name>.bpf.c)` line to top-level
   `CMakeLists.txt` and the `add_library(crucible_perf STATIC ...)`
   list to include the new `.cpp`
4. Author the userspace facade (`include/crucible/perf/<Name>.h` +
   `src/perf/<Name>.cpp`) following the established sibling pattern
5. Author the sentinel test (`test/test_perf_<name>_smoke.cpp`) +
   ≥2 negative-compile fixtures (HS14)
6. Wire the new facade into `bench/bench_harness.h` for inline
   diagnostic display
7. Wire as a sub-program of `Senses` aggregator when that lands

## Provenance

These stubs were authored 2026-05-04 in one sitting as the
"benchmarkmaxxing" planning artifact for the post-GAPS-004 series.
The full BPF observability menu was assembled by inventory across:

- BPF program types (~25 attachment kinds in modern kernel)
- perf_event types (`PERF_TYPE_HARDWARE` / `_SOFTWARE` / `_HW_CACHE` /
  `_RAW` / `_TRACEPOINT` + dynamic PMUs: `ibs_op`, `ibs_fetch`,
  `intel_pt`, `cs_etm`, `msr`, `power`, `cstate_*`, `pstate_*`,
  `uncore_imc_*`, `uncore_cha_*`, `uncore_iio_*`, `uncore_upi_*`,
  `uncore_m2m_*`, `uncore_irp`, `amd_iommu`, `amd_l3`,
  `nvme_pmu`, `cxl_pmu`)
- BPF helpers for state read at probe point (`bpf_perf_event_read_value`,
  `bpf_get_branch_snapshot`, `bpf_get_stack`, `bpf_get_attach_cookie`,
  `bpf_get_current_task`, `bpf_for_each_map_elem`, `bpf_loop`,
  `bpf_dynptr_*`, BPF iterators)

Out-of-scope items are documented at the end of this file.

## Tier-A — keystones (ship FIRST)

The first three are **userspace-only** PMU facades — they need NO
BPF program (counting-mode `perf_event_open` + RDPMC), so they live
outside this directory at `include/crucible/perf/_planned/`.  Their
BPF cross-references are listed for completeness.

| Stub / Doc | One-liner |
|---|---|
| (`PmuCounters` — userspace-only) | Counting-mode HW PMU + RDPMC fast read.  Read at bench boundaries.  Unlocks IPC, cache-hit-rate, mispredict-rate, frontend/backend stall ratios. |
| (`PmuTopDown` — userspace-only)  | Intel SPR+ / AMD Zen5+ TopDown 4-quadrant via `MSR_PERF_METRICS` — direct Frontend / BadSpec / Backend / Retiring %. |
| (`PmuRapl` — userspace-only)     | Joules per region via `PERF_TYPE_POWER` — package / cores / dram / gpu energy counters. |
| `_ext_pmu_sample.md`             | Extend existing `pmu_sample.bpf.c` with PEBS extended payload (DATA_SRC, ADDR, WEIGHT) + IBS extended payload (`IBS_OP_DATA[1-3]` MSRs) + LBR snapshot via `bpf_get_branch_snapshot`.  Each sample now answers WHICH cache, WHICH address, last 32 branches. |
| `_ext_sense_hub.md`              | Fold 14 cheap tracepoints into existing `sense_hub.bpf.c`: `tlb_flush`, `mmap_lock_acquire_returned`, `kswapd_wake`, `hrtimer_expire`, `hardirq_count`, `iommu/io_page_fault`, `edac/dram_ce`, `process_fork/exec/free`, `signal_deliver`, `numa_balance/per-reason`, `mm_filemap_delete`, `kmem_cache_alloc summary`, `mm_page_alloc summary`, `skb_drop_reason` (~80 reason buckets). |
| `_ext_sched_switch.md`           | Extend existing `sched_switch.bpf.c` with cycle attribution per off-CPU event via `bpf_perf_event_read_value` reading PmuCounters' CYCLES counter. |

## Tier-B — kernel hot-path attribution (4)

| Stub | One-liner |
|---|---|
| `rcu_gp.bpf.c`         | RCU grace period duration distribution + per-CPU quiescent-state report rate. |
| `workqueue.bpf.c`      | Per-workqueue queue depth + execute_start→execute_end latency. |
| `hardirq.bpf.c`        | Per-IRQ handler entry→exit duration timeline (sibling to softirq, which we have). |
| `hrtimer.bpf.c`        | Per-source hrtimer fire-frequency histogram + worst-case slack. |

## Tier-C — memory subsystem (5 standalone, beyond SenseHub extensions)

| Stub | One-liner |
|---|---|
| `mmap_lock.bpf.c`      | mmap_lock contention timeline (per-mm, per-op).  Kernel 5.16+ tracepoints. |
| `vmscan_ext.bpf.c`     | Extended vmscan: kswapd wake/sleep duration, lru_isolate counts, reclaim path attribution. |
| `page_allocator.bpf.c` | `kmem/mm_page_alloc` per zone × per order × per GFP flags. |
| `slab_allocator.bpf.c` | `kmem/kmem_cache_alloc` per cache rate + size class. |
| `thp_ext.bpf.c`        | Extended THP: collapse/split per reason with stack attribution. |

## Tier-D — scheduler (4)

| Stub | One-liner |
|---|---|
| `sched_ext.bpf.c`      | Custom Crucible-aware kernel scheduler via `struct_ops/sched_ext`.  Bench-pin worker; quiesce interferers.  Kernel 6.12+. |
| `sched_wakeup.bpf.c`   | Wakeup latency (sched_waking → sched_switch on next_pid) + wakee/waker pairs. |
| `sched_rq.bpf.c`       | Run-queue depth at switch via fentry on `enqueue_task_fair` / `dequeue_task_fair`. |
| `cfs_bandwidth.bpf.c`  | CFS throttle/unthrottle per cgroup; quota exhaustion attribution. |

## Tier-E — block I/O (2)

| Stub | One-liner |
|---|---|
| `nvme_rq.bpf.c`        | Per-NVMe-command setup→complete latency; per-queue per-opcode histogram. |
| `block_rq.bpf.c`       | `block_rq_*` lifecycle (insert→issue→complete) for I/O scheduler attribution (mq-deadline / kyber / bfq). |

## Tier-F — networking (14, since CNTP is multi-layer over AF_XDP)

| Stub | One-liner |
|---|---|
| `xdp_rx.bpf.c`         | XDP at NIC RX before kernel stack: per-flow rate + RSS attribution + protocol bucket. |
| `af_xdp_frame.bpf.c`   | AF_XDP socket frame ingress/egress accounting (CNTP fast path). |
| `tc_egress.bpf.c`      | tc-bpf egress: per-class CNTP shaping accounting. |
| `tc_ingress.bpf.c`     | tc-bpf ingress: post-XDP frame attribution + per-skb metadata enrichment. |
| `sock_ops.bpf.c`       | Per-socket TCP RTT/RTO/retransmit telemetry via `BPF_PROG_TYPE_SOCK_OPS` callbacks (`SOCK_OPS_RTT_CB`, `SOCK_OPS_RTO_CB`). |
| `sockmap.bpf.c`        | sk_msg/sk_skb sockmap forwarding observation for in-process CNTP routing. |
| `tcp_lifetime.bpf.c`   | TCP connection lifecycle: `tcp_destroy_sock`, `tcp_rcv_space_adjust`, `tcp_send_loss_probe`. |
| `netif_receive.bpf.c`  | `net/netif_receive_skb` kernel-stack arrival latency per skb. |
| `napi_poll.bpf.c`      | `napi/napi_poll` driver poll cycles per NAPI instance. |
| `qdisc_backlog.bpf.c`  | `qdisc/qdisc_enqueue` / `qdisc_dequeue` per-qdisc backlog depth. |
| `nf_conntrack.bpf.c`   | netfilter conntrack tracepoints for stateful federation NAT traversal. |
| `netfilter_hooks.bpf.c`| netfilter hook events (kernel 6.4+ BPF netfilter program type). |
| `ktls_offload.bpf.c`   | KTLS hardware offload events (NIC AES) for federation mTLS. |
| `quic_uprobe.bpf.c`    | QUIC library uprobe (lsquic / msquic / quiche) for federation transport. |

## Tier-G — process / lifecycle (Iter programs, 4)

| Stub | One-liner |
|---|---|
| `iter_task.bpf.c`      | `iter/task` walk: every task in process → CPU usage, RSS, cgroup, comm, state. |
| `iter_sock.bpf.c`      | `iter/sock` walk: every socket → state, byte counts, peer, congestion-control name. |
| `iter_mmap.bpf.c`      | `iter/task_vma` walk: every mmap region → file, size, prot, flags. |
| `iter_cgroup.bpf.c`    | `iter/cgroup` walk: cgroup hierarchy → resource limits + current usage. |

## Tier-H — power / filesystem (2)

| Stub | One-liner |
|---|---|
| `cpu_idle.bpf.c`       | `power/cpu_idle` per-CPU idle state residency (C0/C1/C1E/C2/C3...). |
| `vfs_hot.bpf.c`        | fentry on `vfs_read` / `vfs_write` / `vfs_open` for VFS-layer attribution. |

## Tier-Z — aggregator (1)

| Stub | One-liner |
|---|---|
| `senses.md`            | `crucible::perf::Senses` — load-all + on-demand-subset façade composing every per-program facade.  Single entry point for "the organism's complete sensory nervous system" — the original aspirational name in `sense_hub.bpf.c` line 3. |

## Userspace-only PMU facades (NOT in this directory)

These need no BPF program (counting-mode `perf_event_open` +
mmap'd `perf_event_mmap_page` + `RDPMC` instruction).  Planned at
`include/crucible/perf/_planned/` (a sibling `_planned/` dir to be
created when needed):

| Facade | Mechanism |
|---|---|
| `PmuCounters.h`   | 8 HW counters in counting mode, group_fd batched, RDPMC read at bench boundaries |
| `PmuTopDown.h`    | `MSR_PERF_METRICS` direct read → 4 quadrant percentages |
| `PmuRapl.h`       | `PERF_TYPE_POWER` per power domain → joules per region |
| `PmuUncoreImc.h`  | `uncore_imc_*` per-channel DRAM bandwidth |
| `PmuUncoreFabric.h` | `uncore_upi_*` / AMD Infinity Fabric per-link traffic |
| `PmuUncoreIio.h`  | `uncore_iio_*` per-PCIe-stack throughput |
| `PmuCmtMbm.h`     | Intel CMT (LLC occupancy) + MBM (memory bandwidth) per-process |
| `PmuCxl.h`        | `cxl_pmu` CXL fabric counters (kernel 6.6+) |
| `NvmePmu.h`       | `nvme_pmu` per-queue NVMe controller counters (kernel 6.5+) |
| `IntelPtOutlierReplay.h` | `intel_pt//u` on-demand instruction trace for outlier post-mortem |
| `PsiReader.h`     | `/proc/pressure/{cpu,memory,io}` reader (no perf_event) |

## Out-of-scope (explicitly NOT planned)

Per the design discussion (commit history of GAPS-004c-AUDIT-2),
these are deliberately excluded.  Listed here so a future maintainer
doesn't ask "why didn't we do X" without finding the rationale:

- **Orchestrator policy**: `cgroup_skb`, `cgroup_sock_addr`,
  `cgroup_device`, `cgroup_sysctl` — Crucible is a workload, not
  a pod scheduler.
- **Security policy enforcement**: `lsm/<hook>`, bpf-lsm enforcement
  mode, `capable()` audit kprobe — we measure, we don't enforce.
- **Truly deprecated / superseded**: `socket_filter` BPF program
  type, `bpf_perf_event_read` (use `_value`), `itimer`/`setitimer`
  tracepoints (use hrtimer).
- **Kernel-developer machinery**: `function_graph` tracer (banned
  by cost discipline), KASAN/KMSAN/KFENCE (kernel-bug hunting),
  `fmod_ret` (kernel-test fault injection), `PERF_TYPE_BREAKPOINT`
  (debugger).
- **Storage protocols we don't speak**: SCSI tracepoints (we're
  NVMe / NVMe-oF), per-FS journal internals (Cipher manages own
  durability), POSIX flock/fcntl contention (we use our own
  primitives), fsnotify/inotify/fanotify (we don't watch files).
- **Vendor-niche SoC PMUs**: `hisi_*`, Marvell-specific, NXP-specific
  — add per-vendor when the fleet ships there.
- **GPU counters via kernel `perf_event` PMU drivers**
  (`i915_pmu` / `amdgpu_pmu` / `xe_pmu`) — Mimic pulls the FULL
  vendor counter sets via NVML / ROCm SMI / cupti / neuron-profile
  directly.  The kernel PMU path duplicates with worse coverage.

## Counting

- 35 BPF program stubs (this directory's `.bpf.c` files)
- 3 extension docs (`_ext_*.md`)
- 1 aggregator doc (`senses.md`)
- 11 userspace-only PMU facade stubs (sibling tree)
- = **50 ship-units total** (per "benchmarkmaxxing" plan, 2026-05-04)
