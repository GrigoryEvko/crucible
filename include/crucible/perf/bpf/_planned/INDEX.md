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

## Audit-round-3 additions (2026-05-04, kernel 6.17 verified)

Added after the `_planned/` audit pruned bullshit and corrected facts.
ALL cited tracepoints verified against `/sys/kernel/tracing/events/`
on running 6.17 + `/root/Downloads/linux/include/trace/events/` —
no fabrications.

### Self-observation (1 BPF; companion userspace stubs in sibling tree)

| Stub | One-liner |
|---|---|
| `bpf_trace.bpf.c` | `tracepoint/bpf_trace/bpf_trace_printk` — catch any BPF program (ours or third-party) using debug `bpf_printk` in production (~1 µs per call slow path). |

Companions in `include/crucible/perf/_planned/`: `BpfStats.md`,
`PerfRecordObserver.md`, `TracingSubscriberStats.md`, `DamonReader.md`.

### Verified-real tracepoint additions (10 BPF)

| Stub | Subsystem (event count, verified) | One-liner |
|---|---|---|
| `csd.bpf.c` | `csd/` (3) | Cross-CPU `smp_call_function_single` queue → entry → exit latency. |
| `irq_vectors.bpf.c` | `irq_vectors/` (~36 events x86) | Per-APIC-vector entry/exit (`reschedule`, `call_function`, `local_timer`, `thermal`, `irq_work`, `vector_alloc/clear/teardown`).  Far richer than generic `irq/irq_handler_entry`. |
| `error_report.bpf.c` | `error_report/error_report_end` (1, load-bearing) | KASAN/KFENCE/UBSAN/WARN bench-reliability disaster early warning. |
| `power_amd_pstate.bpf.c` | `amd_cpu/amd_pstate_*` (2) | AMD-specific P-state EPP / performance request changes. |
| `damon.bpf.c` | `damon/` (4) | DAMON memory-access-pattern aggregation events.  Pairs with userspace `DamonReader.md`. |
| `handshake.bpf.c` | `handshake/` (16 events) | Kernel TLS handshake upcall + tls_alert_recv/send + handshake-daemon command interface.  SUPERSEDES partial `ktls_offload.bpf.c` coverage (corrects audit-round-2 mistake — `tls_alert_*` exists under `handshake/`, not `tls/`). |
| `fib_lookup.bpf.c` | `fib/fib_table_lookup` + `fib6/fib6_table_lookup` | IPv4 + IPv6 routing-decision attribution.  Federation cold-start latency. |
| `alarmtimer.bpf.c` | `alarmtimer/` (4) | POSIX alarm sleep/wake observation.  Bench-window preemption attribution. |
| `filemap.bpf.c` | `filemap/` (5 incl. `mm_filemap_fault`) | Page cache lookup/fault/map per file.  Sample-period gated (high rate). |
| `context_tracking.bpf.c` | `context_tracking/user_enter|user_exit` (2) | Kernel↔user mode transitions per CPU. |

## Tier-Z — aggregator (1)

| Stub | One-liner |
|---|---|
| `senses.md`            | `crucible::perf::Senses` — load-all + on-demand-subset façade composing every per-program facade.  Single entry point for "the organism's complete sensory nervous system" — the original aspirational name in `sense_hub.bpf.c` line 3. |

## Userspace facades (sibling tree)

Pure userspace — no custom BPF program.  Stubs live at
`include/crucible/perf/_planned/`; see that directory's `INDEX.md`
for the master listing.  Categories:

### Self-observation (4) — Tier-1, NEW 2026-05-04

| Stub | Eventual path | Mechanism |
|---|---|---|
| `BpfStats.md` | `perf/BpfStats.h` | `BPF_OBJ_GET_INFO_BY_FD` per-program runtime accounting |
| `PerfRecordObserver.md` | `perf/PerfRecordObserver.h` | mmap'd perf_event ring: PERF_RECORD_LOST/THROTTLE/MMAP2/COMM/FORK/EXIT/KSYMBOL/BPF_EVENT/CGROUP/TEXT_POKE/NAMESPACES/SWITCH (replaces deleted `perf_throttle.bpf.c`) |
| `TracingSubscriberStats.md` | `perf/TracingSubscriberStats.h` | `/sys/kernel/debug/tracing/per_cpu/cpuN/stats` polling — detect tracepoint subscriber overrun |
| `DamonReader.md` | `perf/DamonReader.h` | `/sys/kernel/mm/damon/admin/` + `tracepoint/damon/*` — memory access patterns |

### PMU facades (11)

| Stub | Eventual path |
|---|---|
| `PmuCounters.md` | `perf/PmuCounters.h` (Tier-A keystone) |
| `PmuTopDown.md` | `perf/PmuTopDown.h` (Intel-only after audit-round-2 correction) |
| `PmuRapl.md` | `perf/PmuRapl.h` (dynamic-PMU-type, not PERF_TYPE_POWER) |
| `PmuUncoreImc.md` | `perf/PmuUncoreImc.h` |
| `PmuUncoreFabric.md` | `perf/PmuUncoreFabric.h` |
| `PmuUncoreIio.md` | `perf/PmuUncoreIio.h` |
| `PmuCmtMbm.md` | `perf/PmuCmtMbm.h` (resctrl path on AMD; PERF_TYPE_INTEL_CQM removed) |
| `PmuCxl.md` | `perf/PmuCxl.h` (`cxl_pmu_mem<N>.<idx>` — dot, not underscore) |
| `IntelPtOutlierReplay.md` | `perf/IntelPtOutlierReplay.h` |
| `PerfEventRing.md` | `perf/PerfEventRing.h` (the **third** perf_event mode) |
| `PsiReader.md` | `perf/PsiReader.h` |
| `NvmePmu.md` (DELETED 2026-05-04) | no `nvme_pmu` driver in mainline |

### Cheap polling readers (10) — Tier-3, NEW 2026-05-04

`/proc` and `/sys` polling at 1 Hz; <0.01% CPU each.  Effectively free.

| Stub | Path read |
|---|---|
| `MsrAperfMperfReader.md` | `msr` PMU `aperf`/`mperf`/`tsc`/`irperf` events — TRUE delivered CPU freq |
| `SchedstatReader.md` | `/proc/schedstat` |
| `InterruptsReader.md` | `/proc/interrupts` |
| `SoftirqsReader.md` | `/proc/softirqs` |
| `BuddyinfoReader.md` | `/proc/buddyinfo` |
| `VmstatReader.md` | `/proc/vmstat` |
| `CpufreqReader.md` | `/sys/devices/system/cpu/cpufreq/policy*/` |
| `CpuidleReader.md` | `/sys/devices/system/cpu/cpu*/cpuidle/state*/` |
| `NumastatReader.md` | `/sys/devices/system/node/node*/numastat` |
| `DiskstatsReader.md` | `/proc/diskstats` |

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

BPF tree (`include/crucible/perf/bpf/_planned/`):
- 45 .bpf.c stubs (34 audit-round-1 + 11 audit-round-3, post-deletion of `perf_throttle.bpf.c`)
- 3 extension docs (`_ext_*.md`)
- 1 aggregator doc (`senses.md`)
- 1 INDEX.md
- = **50 files**

Userspace tree (`include/crucible/perf/_planned/`):
- 11 PMU facade stubs (post-NvmePmu deletion)
- 4 self-observation stubs (Tier-1, NEW 2026-05-04)
- 10 cheap polling reader stubs (Tier-3, NEW 2026-05-04)
- 1 INDEX.md
- = **26 files**

**Grand total: 76 files across both `_planned/` trees.**

Per ship-unit (counting facades + stubs but not INDEX.md):
- 45 BPF programs + 3 extension docs + 1 aggregator = 49 BPF ship-units
- 25 userspace facades = 25 userspace ship-units
- = **74 ship-units total** (was 49 before audit-round-3 added 25)

All audit-round-3 additions verified against `/sys/kernel/tracing/events/`
on running kernel 6.17 + `/root/Downloads/linux/include/trace/events/`.
Self-observation triad (BpfStats + PerfRecordObserver + TracingSubscriberStats)
makes the "<1% overhead" claim structurally verifiable for the first time.
