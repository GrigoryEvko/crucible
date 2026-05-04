# `include/crucible/perf/_planned/`

**Doc-only stubs for the userspace-only PMU facades + the
high-frequency perf_event ring path.**

Sibling tree to `include/crucible/perf/bpf/_planned/`.  Files here
target eventual production paths at `include/crucible/perf/<Name>.h`
(no `bpf/` subdir because these facades use NO custom BPF program —
they're pure userspace `perf_event_open` + mmap + RDPMC machinery).

For the **35 BPF-program stubs + 3 extension docs + senses aggregator**,
see `include/crucible/perf/bpf/_planned/INDEX.md` (the master index).
This sibling tree adds the userspace-facade slice of the same
"benchmarkmaxxing" planning artifact (2026-05-04).

## Three perf_event modes

The full perf_event observability picture has THREE modes; each gets
a different facade:

| Mode | Facade | Where |
|---|---|---|
| BPF program on overflow → IP/payload to BPF map | `PmuSample` | `perf/bpf/pmu_sample.bpf.c` (shipped) |
| **Counting mode + RDPMC fast read** | **`PmuCounters` + family** | **HERE (planned)** |
| **High-freq sampling → kernel mmap'd ring buffer** | **`PerfEventRing`** | **HERE (planned)** |

The first is for "WHERE the event happened" (rare events, sample
period 1K-100K).  The second is for "HOW MUCH happened total" (every
event counted in silicon, RDPMC reads cumulative deltas at bench
boundaries).  The third is for HIGH-frequency sampling that BPF can't
afford (cycles, instructions, L1D miss — millions/sec).

## The 12 userspace-only facades

### Tier-A (3) — keystones

| Stub | Eventual path | Mechanism |
|---|---|---|
| `PmuCounters.md` | `perf/PmuCounters.h` | `perf_event_open(group_fd, sample_period=0)` + mmap `perf_event_mmap_page` + RDPMC fast read |
| `PmuTopDown.md` | `perf/PmuTopDown.h` | Intel SPR+ MSR_PERF_METRICS direct read → 4-quadrant pipeline classification (Frontend/BadSpec/Backend/Retiring %) |
| `PmuRapl.md` | `perf/PmuRapl.h` | `PERF_TYPE_POWER` per power domain — package/cores/dram/gpu joules |

### Tier-B (4) — uncore counters (per-socket)

| Stub | Eventual path | Mechanism |
|---|---|---|
| `PmuUncoreImc.md` | `perf/PmuUncoreImc.h` | `uncore_imc_*` per-channel DRAM bandwidth |
| `PmuUncoreFabric.md` | `perf/PmuUncoreFabric.h` | `uncore_upi_*` (Intel) / AMD Infinity Fabric per-link traffic |
| `PmuUncoreIio.md` | `perf/PmuUncoreIio.h` | `uncore_iio_*` per-PCIe-stack throughput |
| `PmuCmtMbm.md` | `perf/PmuCmtMbm.h` | Intel CMT (LLC occupancy) + MBM (memory bandwidth) per-process |

### Tier-C (3) — modern fabric / device PMUs

| Stub | Eventual path | Mechanism |
|---|---|---|
| `PmuCxl.md` | `perf/PmuCxl.h` | `cxl_pmu` CXL fabric counters (kernel 6.6+) |
| `NvmePmu.md` | `perf/NvmePmu.h` | `nvme_pmu` per-queue NVMe controller counters (kernel 6.5+) |
| `IntelPtOutlierReplay.md` | `perf/IntelPtOutlierReplay.h` | `intel_pt//u` on-demand instruction trace for outlier post-mortem |

### Tier-D (2) — non-PMU perf_event paths

| Stub | Eventual path | Mechanism |
|---|---|---|
| `PerfEventRing.md` | `perf/PerfEventRing.h` | mmap'd `perf_event_mmap_page` ring buffer for high-frequency sampling (cycles, L1D miss); kernel writes events, userspace drains.  Path that BPF can't afford. |
| `PsiReader.md` | `perf/PsiReader.h` | `/proc/pressure/{cpu,memory,io}` reader (no perf_event at all; just an OS-pressure reader to round out the suite) |

## Composition with the BPF tree

Several BPF programs in `bpf/_planned/` need PmuCounters' counter
FDs to read HW counters from inside BPF (via
`bpf_perf_event_read_value` + `BPF_MAP_TYPE_PERF_EVENT_ARRAY`):

| BPF stub | Needs PmuCounters? | What for |
|---|---|---|
| `_ext_sched_switch.md` | ✓ | Cycle attribution per off-CPU event |
| `sched_wakeup.bpf.c` | ✓ (could add) | Cycles consumed during wakeup latency |
| `nvme_rq.bpf.c` | (optional) | Cycles per NVMe command |

Implementation order accordingly: **PmuCounters MUST land before any
BPF program that reads its counter FDs from inside BPF**.

## Why userspace-only

These facades all use the SAME mechanism: `perf_event_open` with
`sample_period = 0` (counting mode) + mmap of the resulting FD's
`perf_event_mmap_page` + RDPMC userspace instruction (~5-15 ns per
counter read on Intel/AMD).

There's no custom BPF program needed — the kernel's perf_event
infrastructure does the silicon-side counter management, and
userspace just reads the cumulative count via RDPMC.  Crucible could
read via `read(fd, ...)` syscall instead (~500 ns per syscall) but
RDPMC is the canonical fast path used by `perf stat --no-syscall`,
Intel PCM, librdpmc, PAPI.

The sole exception is `IntelPtOutlierReplay` — Intel PT writes its
trace to a separate AUX area (kernel-managed); userspace mmaps that.
Different mechanism, same "no BPF program" property.

## Implementation TODO (for any of these)

1. Author `include/crucible/perf/<Name>.h` (facade with `load(Init)`
   + `read()` snapshot + `Borrowed`/`Refined` types per
   `SenseHub.h` / `SchedSwitch.h` precedent)
2. Author `src/perf/<Name>.cpp` (libbpf NOT required — straight
   `<linux/perf_event.h>` syscall + mmap + RDPMC inline asm)
3. Author `test/test_perf_<name>_smoke.cpp` (+ ≥2 neg-compile
   fixtures per HS14)
4. Wire into `Senses` aggregator (`bpf/_planned/senses.md`)
5. Wire into `bench/bench_harness.h` for inline diagnostic display

## Counting

This sibling tree contains:
- 12 userspace facade stubs
- 1 INDEX.md (this file)
- = **13 files**

Combined with the BPF dir's 40 files:
- 35 .bpf.c stubs + 5 .md docs (INDEX + 3 extensions + senses)
- = **40 files**

**Grand total planning artifact: 53 files, ~3,500-4,000 lines of
documentation across both `_planned/` trees.**
