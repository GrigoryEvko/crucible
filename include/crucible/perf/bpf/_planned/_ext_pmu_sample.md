# Extension: `pmu_sample.bpf.c` — PEBS / IBS extended payload + LBR

**STATUS**: doc-only stub.  In-place extension to existing
`pmu_sample.bpf.c` (no new file).  Tier-A.

## Problem

Today every PmuSample event carries `(ip, tid, event_type, ts_ns)`
— useful for "WHERE the cache miss happened" but blind to "WHICH
ADDRESS got missed", "WHICH cache LEVEL served the load", and
"how did we get here" (call-stack).  Modern HW exposes all three
on the same overflow event for free; we throw the data away.

## Mechanism

Three additions, all on the existing `attr` and `bpf_perf_event_data`:

### 1. PEBS extended payload (Intel Skylake+)

Set `attr.precise_ip = 2` (zero-skid PEBS) and OR into
`attr.sample_type`:

```
PERF_SAMPLE_IP            (already implicit via ctx->regs.ip)
PERF_SAMPLE_DATA_SRC      → which memory level served the load
                            (encoded: L1/L2/LLC/DRAM/Remote-NUMA + snoop status)
PERF_SAMPLE_ADDR          → data linear address (faulting load addr)
PERF_SAMPLE_WEIGHT        → load latency in cycles
PERF_SAMPLE_PHYS_ADDR     → data physical address (root-only; opt-in)
PERF_SAMPLE_DATA_PAGE_SIZE→ HW-detected page size at the access
```

In the BPF program, these are accessible via the `bpf_perf_event_data`
extended fields exposed by the kernel for SEC("perf_event") progs
that opted in via `attr.sample_type`.  Read once, append to the
existing slot in `pmu_sample_buf`.

### 2. IBS extended payload (AMD Zen)

For the existing `pmu_ibs_op` and `pmu_ibs_fetch` programs:
the bare IP we capture today is from `ctx->regs.ip`.  IBS additionally
publishes per-op MSRs that the kernel exposes through the perf_event
sample if `PERF_SAMPLE_RAW` is enabled.  Capture in BPF:

- `IBS_OP_DATA1` — completion latency, branch info
- `IBS_OP_DATA2` — data source: DCache hit / L2 / L3 / DRAM /
  Remote NUMA / Cache-fill source ID
- `IBS_OP_DATA3` — TLB info (DTLB hit/miss, page size), NB cache state
- `IBS_DC_LIN_AD` — data linear address (the ADDR equivalent)
- `IBS_DC_PHYS_AD` — data physical address (root-only)

### 3. LBR / BRS / BRBE call-stack snapshot

Set `attr.branch_sample_type = PERF_SAMPLE_BRANCH_USER |
PERF_SAMPLE_BRANCH_CALL_STACK` (Intel LBR / AMD BRS / ARM BRBE
uniformly exposed).  In BPF, call:

```c
struct bpf_perf_branch_entry buf[32];
long n = bpf_get_branch_snapshot(buf, sizeof(buf), 0);
```

Each entry: `{from, to, mispredict, in_tx, abort, cycles}`.
~32 last branches → cheap call stack WITHOUT walking frame pointers.

## Maps

Existing `pmu_sample_buf` (`BPF_F_MMAPABLE` array of
`pmu_sample_timeline`) — but the per-event struct grows.

Choose one of two shapes:
- **(A) Widen `pmu_sample_event`** from 32 B to 96 B / 128 B per
  event, adding `data_src`, `data_addr`, `weight`, plus a
  fixed-size LBR slice (`pmu_branch_entry[8]` truncating to 8 most-
  recent).  Cache-line implications: 96 B straddles two lines;
  128 B fits two lines cleanly.
- **(B) Keep the 32 B base event, emit RICH events to a parallel
  ring** (`pmu_sample_extended_buf`) keyed by the same write_idx
  so userspace correlates by index.  Two separate mmaps; richer
  events cost mmap space proportional to event rate.

Recommendation: **(B)** — keeps the hot ring slim, lets richer
events be opt-in via env-var.

## Wire contract

Mirror existing convention: ts_ns LAST, `__atomic_load_n(&ts_ns,
ACQUIRE)` on the reader side, compiler barrier before the ts_ns
store on the BPF side.  For shape (B), the extended ring's
`write_idx` advances IN STEP with the base ring's write_idx (BPF
emits both atomically per overflow); reader correlates by index.

## Cost

- PEBS sample with extended payload: ~5-7 µs vs ~3-5 µs today
  (one extra MSR read + larger sample copy).  Already-rare event.
- IBS extended: ~50 ns extra per overflow (3 MSR reads).
- LBR snapshot: ~100-200 ns extra per overflow (kernel reads 32
  LBR entries via `rdmsr` loop).

All three are "extra cost on rare events" — sample period unchanged.

## Limits

- `bpf_get_branch_snapshot` requires kernel ≥ 5.16 + Intel LBR
  enabled + CAP_PERFMON.  Falls back to no-op on older kernels
  (returns 0 entries; userspace handles).
- AMD BRS requires kernel ≥ 6.1 + Zen3+.  ARM BRBE requires ≥ 6.5 +
  Neoverse V2+.
- PEBS+LBR combined ("PEBS-via-PT" mode) has more skid than pure
  PEBS — choose one or the other per-event-spec.

## Implementation TODO

1. Update `kEventSpecs[]` in `src/perf/PmuSample.cpp` with per-spec
   `precise_ip` + `sample_type` overrides.
2. Add userspace env-var: `CRUCIBLE_PERF_PMU_RICH=1` to opt into
   shape-(B) extended ring.
3. Extend `PmuSample.h` with `extended_view()` accessor returning
   `Borrowed<const PmuSampleExtended, PmuSample>` parallel to
   `timeline_view()`.
4. Extend sentinel test with offset asserts on the extended struct.
5. Add 2 neg-compile fixtures: extended_view returns empty span
   when not opted in; reading extended fields without RICH env-var
   gates is undefined.

## Sibling refs

Consumes: PmuCounters group_fd (for cycles delta per sample).
Aggregated by: future `Augur::loadHotIPs()` for per-function
attribution.
