# `include/crucible/perf/_planned/`

**Doc-only stubs for userspace facades — PMU readers, perf_event ring,
self-observation, and cheap `/proc` + `/sys` polling readers.**

Sibling tree to `include/crucible/perf/bpf/_planned/`.  Files here
target eventual production paths at `include/crucible/perf/<Name>.h`
(no `bpf/` subdir — these facades use NO custom BPF program; they
read kernel-exposed counters or files directly).

For the BPF program stubs (kernel tracepoints, kprobes, perf_event-
sample programs), see `bpf/_planned/INDEX.md`.

## Three perf_event modes

| Mode | Facade | Where |
|---|---|---|
| BPF program on overflow → IP/payload to BPF map | `PmuSample` | `bpf/pmu_sample.bpf.c` (shipped) |
| Counting mode + RDPMC fast read | `PmuCounters` family | HERE (planned) |
| High-freq sampling → kernel mmap'd ring | `PerfEventRing` + `PerfRecordObserver` | HERE (planned) |

## Tier-1 — self-observation (4) **CRITICAL**

The single biggest gap before this round: Crucible's BPF stack observes
the kernel but nothing observes the BPF stack itself.  Without these,
the "<1% overhead" claim is structurally unverifiable.

| Stub | Eventual path | Mechanism |
|---|---|---|
| `BpfStats.md` | `perf/BpfStats.h` | `BPF_OBJ_GET_INFO_BY_FD` + `/proc/sys/kernel/bpf_stats_enabled` → per-program `run_cnt` + `run_time_ns` |
| `PerfRecordObserver.md` | `perf/PerfRecordObserver.h` | mmap'd `perf_event_mmap_page` ring → consumes 11 PERF_RECORD_* metadata types (LOST, MMAP2, COMM, FORK, EXIT, KSYMBOL, BPF_EVENT, CGROUP, THROTTLE, TEXT_POKE, NAMESPACES, SWITCH).  REPLACES deleted `perf_throttle.bpf.c` plan. |
| `TracingSubscriberStats.md` | `perf/TracingSubscriberStats.h` | `/sys/kernel/debug/tracing/per_cpu/cpuN/stats` polling → detect when our tracepoint subscriber falls behind |
| `DamonReader.md` | `perf/DamonReader.h` | `/sys/kernel/mm/damon/admin/` + `tracepoint/damon/*` (kernel 5.15+) — memory-access patterns at lower cost than PmuSample DTLB-miss IP sampling |

## Tier-A — PMU keystones (3)

| Stub | Eventual path | Mechanism |
|---|---|---|
| `PmuCounters.md` | `perf/PmuCounters.h` | `perf_event_open(group_fd, sample_period=0)` + mmap + RDPMC fast read |
| `PmuTopDown.md` | `perf/PmuTopDown.h` | Intel SPR+ MSR_PERF_METRICS direct read → 4-quadrant pipeline classification |
| `PmuRapl.md` | `perf/PmuRapl.h` | Dynamic-PMU `power` per-domain — package/cores/dram/gpu joules |

## Tier-B — uncore counters (4)

| Stub | Eventual path | Mechanism |
|---|---|---|
| `PmuUncoreImc.md` | `perf/PmuUncoreImc.h` | `uncore_imc_*` per-channel DRAM bandwidth |
| `PmuUncoreFabric.md` | `perf/PmuUncoreFabric.h` | `uncore_upi_*` (Intel) / `amd_df` (AMD) per-link cross-socket traffic |
| `PmuUncoreIio.md` | `perf/PmuUncoreIio.h` | `uncore_iio_*` per-PCIe-stack throughput |
| `PmuCmtMbm.md` | `perf/PmuCmtMbm.h` | Intel CMT (LLC occupancy) + MBM (memory bandwidth) per-process via resctrl |

## Tier-C — modern fabric / device PMUs (2)

| Stub | Eventual path | Mechanism |
|---|---|---|
| `PmuCxl.md` | `perf/PmuCxl.h` | `cxl_pmu_mem<N>.<idx>` CXL fabric counters (kernel 6.6+) |
| `IntelPtOutlierReplay.md` | `perf/IntelPtOutlierReplay.h` | `intel_pt//u` on-demand instruction trace for outlier post-mortem |

## Tier-D — non-PMU perf_event paths (2)

| Stub | Eventual path | Mechanism |
|---|---|---|
| `PerfEventRing.md` | `perf/PerfEventRing.h` | mmap'd `perf_event_mmap_page` ring buffer for high-frequency sampling (cycles, L1D miss); kernel writes events, userspace drains.  Path that BPF can't afford. |
| `PsiReader.md` | `perf/PsiReader.h` | `/proc/pressure/{cpu,memory,io,irq}` reader |

## Tier-3 — cheap `/proc` + `/sys` polling readers (10)

No BPF, no perf_event — just file polling.  Each is 200-500 ns/read,
1 Hz cadence ≈ 0.005% CPU.  Effectively free always-on.

| Stub | Eventual path | Source |
|---|---|---|
| `MsrAperfMperfReader.md` | `perf/MsrAperfMperfReader.h` | `msr` PMU `aperf`/`mperf`/`tsc`/`irperf` events — TRUE delivered CPU frequency (under turbo, under power cap) |
| `SchedstatReader.md` | `perf/SchedstatReader.h` | `/proc/schedstat` per-CPU + per-domain scheduling stats |
| `InterruptsReader.md` | `perf/InterruptsReader.h` | `/proc/interrupts` per-CPU per-source IRQ counts |
| `SoftirqsReader.md` | `perf/SoftirqsReader.h` | `/proc/softirqs` per-CPU softirq vector counts (HI/TIMER/NET_RX/...) |
| `BuddyinfoReader.md` | `perf/BuddyinfoReader.h` | `/proc/buddyinfo` page allocator order distribution per zone |
| `VmstatReader.md` | `perf/VmstatReader.h` | `/proc/vmstat` ~120 memory counters (allocation, reclaim, faults, swap, NUMA, THP, compaction, OOM) |
| `CpufreqReader.md` | `perf/CpufreqReader.h` | `/sys/devices/system/cpu/cpufreq/policy*/` governor + target frequency |
| `CpuidleReader.md` | `perf/CpuidleReader.h` | `/sys/devices/system/cpu/cpu*/cpuidle/state*/{name,time,usage,latency}` C-state residencies |
| `NumastatReader.md` | `perf/NumastatReader.h` | `/sys/devices/system/node/node*/numastat` per-node memory stats |
| `DiskstatsReader.md` | `perf/DiskstatsReader.h` | `/proc/diskstats` per-block-device I/O statistics |

## Removed

- `NvmePmu.md` — DELETED 2026-05-04 (audit-round-2): no `nvme_pmu`
  driver in mainline kernel.  NVMe controller-side observation goes
  through `/sys/block/nvme*n*/stat`, `nvme smart-log` (covered by
  `DiskstatsReader.md` for the per-device aggregate path).

## Composition with the BPF tree

Several BPF programs in `bpf/_planned/` need PmuCounters' counter FDs
to read HW counters from inside BPF (via `bpf_perf_event_read_value`
+ `BPF_MAP_TYPE_PERF_EVENT_ARRAY`):

| BPF stub | Needs PmuCounters? | What for |
|---|---|---|
| `_ext_sched_switch.md` | ✓ | Cycle attribution per off-CPU event |
| `sched_wakeup.bpf.c` | optional | Cycles consumed during wakeup latency |
| `nvme_rq.bpf.c` | optional | Cycles per NVMe command |

Implementation order: **PmuCounters MUST land before any BPF program
that reads its counter FDs from inside BPF.**

## Self-observation triad

`BpfStats` + `PerfRecordObserver` + `TracingSubscriberStats` together
make Crucible's "<1% overhead" claim verifiable.  Ship as a unit:

- BpfStats — per-BPF-program `run_time_ns / run_cnt` (cost per invocation)
- PerfRecordObserver — per-CPU PERF_RECORD_LOST count (sample drops)
- TracingSubscriberStats — per-CPU tracepoint ring overrun (drop attribution)

All three are pure userspace, no BPF, no perf_event-sample-mode.

## Counting

Tier breakdown:
- Tier-1 self-observation: 4 stubs (NEW 2026-05-04)
- Tier-A keystones: 3 stubs
- Tier-B uncore: 4 stubs
- Tier-C device PMUs: 2 stubs (after NvmePmu deletion)
- Tier-D non-PMU perf_event: 2 stubs
- Tier-3 polling readers: 10 stubs (NEW 2026-05-04)
- INDEX.md: 1
- = **26 files** (was 12 before audit-round-3)

Combined planning artifact (post-audit-round-3, 2026-05-04):
- 53 BPF stubs (after audit-round-2 deletion of perf_throttle.bpf.c, plus +11 audit-round-3 additions)
- 4 .md docs in BPF tree (INDEX + 3 extensions + senses)
- 25 userspace facade stubs (Tier-1 + Tier-A + Tier-B + Tier-C + Tier-D + Tier-3)
- 1 INDEX.md
- = **~83 files total** across both `_planned/` trees
