# `PerfRecordObserver` — consume the mmap'd perf_event ring's full record taxonomy

**STATUS**: doc-only stub.  Tier-1 self-observation.  Eventual path:
`include/crucible/perf/PerfRecordObserver.h`.  Userspace-only — NO
custom BPF program (consumes records the kernel emits naturally).

## Problem

The kernel's `perf_event_open` mmap ring delivers MUCH more than just
`PERF_RECORD_SAMPLE` — it carries a full taxonomy of metadata records
that Crucible structurally ignores today.  Every facade that uses
`PerfEventRing` (planned) or any sampling-mode perf_event leaves these
on the floor:

- **`PERF_RECORD_LOST`** / **`PERF_RECORD_LOST_SAMPLES`** — kernel
  dropped N samples.  This is the **correct** mechanism for what the
  deleted `perf_throttle.bpf.c` plan tried to observe.
- **`PERF_RECORD_THROTTLE`** / **`PERF_RECORD_UNTHROTTLE`** — kernel
  rate-limit kicked in / lifted on a specific perf_event_id.
- **`PERF_RECORD_MMAP`** / **`PERF_RECORD_MMAP2`** — code-page mappings
  (DSOs loaded/unloaded; required for IP→symbol resolution on samples).
- **`PERF_RECORD_COMM`** — process renamed (`prctl(PR_SET_NAME)`).
- **`PERF_RECORD_FORK`** / **`PERF_RECORD_EXIT`** — process lifecycle.
- **`PERF_RECORD_KSYMBOL`** — kernel symbol load/unload (BPF JIT bodies
  count here; required for resolving BPF program addresses).
- **`PERF_RECORD_BPF_EVENT`** — BPF program loaded/unloaded.
- **`PERF_RECORD_CGROUP`** — cgroup creation/destruction with id.
- **`PERF_RECORD_TEXT_POKE`** — JIT/static-key/livepatch text patches.
- **`PERF_RECORD_NAMESPACES`** — process namespace transitions.
- **`PERF_RECORD_SWITCH`** / **`PERF_RECORD_SWITCH_CPU_WIDE`** — context
  switches with sched-out/sched-in attribution.

ALL of these arrive on the SAME mmap ring.  One observer, twelve
record types, one consolidated source of truth.

## Mechanism

`perf_event_open` with `attr.sample_type` plus `attr.{mmap,comm,fork_event,
ksymbol,bpf_event,cgroup,text_poke,namespaces,context_switch} = 1` enables
the corresponding record types.  Mmap'd ring (per-CPU) is consumed via
the standard `data_head` / `data_tail` seqlock pattern documented in
`include/uapi/linux/perf_event.h`.

This facade does NOT collect samples (that's PmuSample / PerfEventRing).
It opens a per-CPU `attr.type = PERF_TYPE_SOFTWARE` /
`attr.config = PERF_COUNT_SW_DUMMY` event with sample_period=0 and
EVERY metadata-record bit set — so the ring contains ONLY metadata,
no sample payload.  Cheap.

## Reader cadence

Drained on bg thread at 10 Hz (or on PERF_RECORD_LOST count).  Each
record dispatched to a typed handler:

```cpp
struct PerfRecordSink {
    virtual void on_lost(uint32_t cpu, uint64_t lost_count) = 0;
    virtual void on_throttle(uint64_t event_id, uint64_t ts_ns) = 0;
    virtual void on_unthrottle(uint64_t event_id, uint64_t ts_ns) = 0;
    virtual void on_mmap2(const PerfMmap2&) = 0;
    virtual void on_comm(uint32_t pid, uint32_t tid, std::string_view) = 0;
    virtual void on_fork(uint32_t parent, uint32_t child) = 0;
    virtual void on_exit(uint32_t pid, uint32_t tid, uint64_t ts_ns) = 0;
    virtual void on_ksymbol(uint64_t addr, std::string_view, bool unregister) = 0;
    virtual void on_bpf_event(uint16_t type, uint32_t prog_id, std::string_view) = 0;
    virtual void on_cgroup(uint64_t id, std::string_view path) = 0;
    virtual void on_text_poke(uint64_t addr, /* old/new bytes */) = 0;
    virtual void on_switch(uint32_t prev_pid, uint32_t next_pid, bool out) = 0;
};
```

(Internal sink — virtual is acceptable here because it's bg-thread cold
path; not on hot path.)

## Cost model

- One perf_event_open + one mmap per CPU at startup (~100 µs/CPU once).
- Per-record decode: ~100 ns each.
- Background drain rate: depends on workload — typically 10-1000 records/sec/CPU.
- Per-sec overhead: <0.1% per CPU.  Effectively free always-on.

## Wire contract

Each record translates into one entry in the bg-side `TraceRing`
analog (`PerfMetaRing`) for downstream consumers:

```cpp
struct PerfMetaEntry {
    uint16_t type;        // PERF_RECORD_*
    uint16_t cpu;
    uint32_t payload_idx; // index into PerfMetaPayloadArena
    uint64_t ts_ns;       // WRITTEN LAST per common.h convention
};  // 16 B
```

## Bench harness display

```
└─ perf_meta: lost=0  throttle=0  bpf_load=2 (sense_hub.bpf.c +
    sched_switch.bpf.c)  text_poke=4 (kernel static_keys flipped)
```

Lost > 0 in ANY bench window = sample data partial; flag inline.

## Known limits

- Per-event throttle semantics: kernel emits THROTTLE/UNTHROTTLE once
  per event-id; correlate with PmuSample's owned event-ids.
- TEXT_POKE coverage requires kernel ≥ 5.4 + perf_event_paranoid ≤ 1.
- KSYMBOL/BPF_EVENT require ≥ 5.0.
- CGROUP records require ≥ 5.7 + `attr.cgroup = 1`.
- Switch records require `attr.context_switch = 1`; high rate but
  cheaper than SchedSwitch (no off-CPU duration).

## Sibling refs

- **Self-observation triad** with: `BpfStats` (per-program runtime cost) and `TracingSubscriberStats` (tracepoint subscriber lag).
- **Replaces** the deleted `perf_throttle.bpf.c` plan with the actually-correct mechanism (PERF_RECORD_THROTTLE in this ring, not a tracepoint).
- **Symbol resolution input** for IntelPtOutlierReplay + PmuSample IP attribution (KSYMBOL + MMAP2 records).
- **PerfEventRing** is the sample-side; this is the metadata-side; both consume the same ring data structure but different record types.
