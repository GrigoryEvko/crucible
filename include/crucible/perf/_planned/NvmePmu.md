# `NvmePmu` — per-queue NVMe controller counters (kernel 6.5+)

**STATUS**: doc-only stub.  Tier-C.  Eventual path:
`include/crucible/perf/NvmePmu.h`.  Userspace-only.

## Problem

Cipher cold tier sustains 100K-1M IOPS to NVMe.  Per-queue
controller-side counters (submission queue depth, completion queue
depth, controller-internal command processing time) tell you
"queue 7 is the bottleneck" or "controller fills SQ faster than CQ
drains" — diagnostics impossible from the host-side block layer
alone.

Sibling to `nvme_rq.bpf.c` (which measures host-side request
lifecycle); this measures CONTROLLER-SIDE counters.

## Mechanism

```
/sys/bus/event_source/devices/nvme0/  — per-controller PMU
events: read_commands, write_commands, queue_depth_*,
        controller_busy_time, etc.
```

Same `perf_event_open` + mmap shape as uncore PMUs.

## API shape

```cpp
struct NvmePmuSnapshot {
    struct Controller {
        uint64_t read_commands;
        uint64_t write_commands;
        uint64_t controller_busy_ns;
        uint32_t avg_sq_depth;
        uint32_t avg_cq_depth;
        uint32_t controller_id;
    };
    std::vector<Controller> controllers;
};
```

## Cost

- ~50 ns per counter read.

## Limits

- Kernel 6.5+ minimum.
- Per-controller (not per-namespace).  Per-namespace requires the
  NVMe driver's CLI tool (`nvme list-ns`).
- Vendor-specific events: SK Hynix, Samsung, WD all expose different
  internal counters via the `nvme0/events/*` directory.  Pick the
  controller-vendor-agnostic ones first.

## Sibling refs

- `nvme_rq.bpf.c` — host-side per-command latency
- `PmuUncoreIio.md` — PCIe BW to/from NVMe
