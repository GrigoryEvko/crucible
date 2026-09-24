# `IntelPtOutlierReplay` — Intel PT / ARM ETM on-demand instruction trace

**STATUS**: doc-only stub.  Tier-C.  Eventual path:
`include/crucible/perf/IntelPtOutlierReplay.h`.  Userspace-only,
on-demand (NOT always-on).

## Problem

Bench detects an outlier (max=10× p50).  All metrics are aggregate
— we know SOMETHING happened in those 5 µs but not WHAT.  Intel
Processor Trace (PT) records every instruction at ~5-10% sustained
overhead; replay-on-outlier drops the overhead to near-zero by only
keeping the trace when triggered.

The use case is forensic: "the last 10 ms of execution leading up
to this anomaly, decoded into instruction stream".  Pair with
disassembly + DWARF for line-level attribution.

## Mechanism

```cpp
struct perf_event_attr attr{};
attr.type   = read_dynamic_pmu_type("/sys/bus/event_source/devices/intel_pt/type");
attr.config = 0;  // basic; richer flags for branch / cycle / time
attr.sample_period = 0;
// AUX area for trace data:
attr.aux_watermark = AUX_RING_BYTES / 2;
```

Open one perf_event per CPU, mmap both the regular page AND the AUX
area (`MAP_SHARED` at offset `0` for the page, and offset
`page->aux_offset` for the AUX trace ring).

PT writes compressed packets into the AUX ring continuously.  When
outlier triggers: snapshot the AUX ring, save to disk, decode
offline with `perf-tools`'s `intel_pt` decoder + objdump.

## API shape

```cpp
class IntelPtOutlierReplay {
public:
    [[nodiscard]] static std::optional<IntelPtOutlierReplay>
        load(::crucible::effects::Init,
             size_t aux_ring_bytes_per_cpu = 16 * 1024 * 1024) noexcept;

    // Snapshot the current AUX content for ALL CPUs to a file.
    // Returns the file path; outlier-detection code calls this when
    // a max=N× p50 spike is detected.  Cost: ~5-50 ms (depends on
    // ring size); blocking but rare.
    [[nodiscard]] std::expected<std::string, SnapshotError>
        snapshot_to_file(std::string_view path_prefix) const noexcept;

    // Decode hint for offline analysis: returns the perf-tools
    // command line to decode the snapshot.
    [[nodiscard]] std::string decode_command(std::string_view path) const;
};
```

## Cost

- Continuous: 5-10% CPU (PT writing to AUX ring).
- Snapshot trigger: 5-50 ms blocking (copy AUX → disk).
- Decode (offline): ~1 sec per MB of trace data.

## Known limits

- Always-on use is too costly for production; only enable in
  diagnostic mode (env var `CRUCIBLE_PERF_PT=1`).
- AUX ring overwrites old data; snapshot captures last
  `aux_ring_bytes / per-CPU-rate` µs.  Default 16 MB → ~10-50 ms
  of trace at typical workload.
- Intel only on x86_64.  ARM equivalent: ETM (CoreSight Embedded
  Trace Macrocell) via `cs_etm` PMU — different config, similar
  shape; vendor-discriminated impl.
- AUX area requires CAP_PERFMON (or paranoid <= 1).

## Sibling refs

- All other facades — this is the "when shit goes wrong, replay"
  primitive.  Trigger source is the bench harness's outlier
  detection (currently emits `(× N)` markers in the SchedSwitch
  off-CPU section); future: also from PmuCounters IPC drop.
